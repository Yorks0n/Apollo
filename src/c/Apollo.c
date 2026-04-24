#include <pebble.h>
#include "solar.h"
#include "storage.h"
#include "ui_main.h"
#include "ui_locations.h"
#include "ui_detail.h"
#include "message_keys.auto.h"

// -------------------------------------------------------------------------
// Global state
// -------------------------------------------------------------------------
static LocationList   s_locations;
static SolarDayResult s_today;
static SolarDayResult s_tomorrow;
static QualityCache   s_quality;
static int            s_prev_local_day = -1;
static time_t         s_last_quality_sync_at = 0;
static bool           s_force_quality_after_phone_sync = false;

// AppMessage sync state
static int s_sync_expected = 0;
static uint8_t s_sync_selected = 0;

#define QUALITY_AUTO_REFRESH_INTERVAL_S (6 * 60 * 60)

typedef enum {
  QUALITY_SYNC_NONE = 0,
  QUALITY_SYNC_NORMAL = 1,
  QUALITY_SYNC_FORCE = 2
} QualitySyncMode;

static int32_t prv_date_to_int(int year, int month, int day) {
  return year * 10000 + month * 100 + day;
}

static void prv_get_local_dates_for_selected(int32_t *today_date, int32_t *tomorrow_date) {
  if (today_date) {
    *today_date = 0;
  }
  if (tomorrow_date) {
    *tomorrow_date = 0;
  }

  if (s_locations.count == 0) {
    return;
  }

  {
    int idx = s_locations.selected_index;
    int year = 0;
    int month = 0;
    int day = 0;
    const Location *loc;

    if (idx >= s_locations.count) {
      idx = 0;
    }
    loc = &s_locations.locs[idx];
    solar_local_date(time(NULL), loc->utc_offset_min, &year, &month, &day);
    if (today_date) {
      *today_date = prv_date_to_int(year, month, day);
    }
    solar_date_add_days(&year, &month, &day, 1);
    if (tomorrow_date) {
      *tomorrow_date = prv_date_to_int(year, month, day);
    }
  }
}

static void prv_request_phone_sync(void) {
  if (!connection_service_peek_pebble_app_connection()) {
    return;
  }

  DictionaryIterator *iter = NULL;
  AppMessageResult result = app_message_outbox_begin(&iter);
  if (result != APP_MSG_OK || !iter) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "Sync request begin failed: %d", (int)result);
    return;
  }

  dict_write_uint8(iter, MESSAGE_KEY_SYNC_REQUEST, 1);
  result = app_message_outbox_send();
  if (result != APP_MSG_OK) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "Sync request send failed: %d", (int)result);
  }
}

static void prv_request_quality_sync(bool force_refresh) {
  DictionaryIterator *iter = NULL;
  AppMessageResult result;
  int32_t today_date = 0;
  int32_t tomorrow_date = 0;
  int idx;

  if (s_locations.count == 0 || !connection_service_peek_pebble_app_connection()) {
    return;
  }

  idx = s_locations.selected_index;
  if (idx >= s_locations.count) {
    idx = 0;
  }

  prv_get_local_dates_for_selected(&today_date, &tomorrow_date);

  result = app_message_outbox_begin(&iter);
  if (result != APP_MSG_OK || !iter) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "Quality request begin failed: %d", (int)result);
    return;
  }

  dict_write_uint8(iter, MESSAGE_KEY_QUALITY_REQUEST, 1);
  dict_write_uint8(iter, MESSAGE_KEY_QUALITY_REQ_LOC_INDEX, (uint8_t)idx);
  dict_write_int32(iter, MESSAGE_KEY_QUALITY_REQ_DATE_0, today_date);
  dict_write_int32(iter, MESSAGE_KEY_QUALITY_REQ_DATE_1, tomorrow_date);
  if (force_refresh) {
    dict_write_uint8(iter, MESSAGE_KEY_QUALITY_FORCE_REFRESH, 1);
  }

  result = app_message_outbox_send();
  if (result != APP_MSG_OK) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "Quality request send failed: %d", (int)result);
    return;
  }

  s_last_quality_sync_at = time(NULL);
}

// -------------------------------------------------------------------------
// Calculation
// -------------------------------------------------------------------------
static void prv_recalculate(QualitySyncMode quality_sync_mode) {
  if (s_locations.count == 0) return;

  int idx = s_locations.selected_index;
  if (idx >= s_locations.count) idx = 0;
  const Location *loc = &s_locations.locs[idx];

  double lat = (double)loc->lat_e6 / 1000000.0;
  double lon = (double)loc->lon_e6 / 1000000.0;
  int    utc = (int)loc->utc_offset_min;

  time_t now = time(NULL);
  int year, month, day;
  solar_local_date(now, utc, &year, &month, &day);
  s_prev_local_day = day;

  s_today = solar_calc(year, month, day, lat, lon, utc);
  solar_date_add_days(&year, &month, &day, 1);
  s_tomorrow = solar_calc(year, month, day, lat, lon, utc);

  main_window_update(&s_today, &s_tomorrow, loc, idx);

  if (quality_sync_mode == QUALITY_SYNC_NORMAL) {
    prv_request_quality_sync(false);
  } else if (quality_sync_mode == QUALITY_SYNC_FORCE) {
    prv_request_quality_sync(true);
  }
}

// -------------------------------------------------------------------------
// Tick handler
// -------------------------------------------------------------------------
static void prv_tick_handler(struct tm *tick_time, TimeUnits changed) {
  if (s_locations.count == 0) return;

  int idx = s_locations.selected_index;
  const Location *loc = &s_locations.locs[idx];

  // Check for local midnight rollover at the target location
  time_t now = time(NULL);
  int y, mo, d;
  solar_local_date(now, loc->utc_offset_min, &y, &mo, &d);
  if (d != s_prev_local_day && s_prev_local_day != -1) {
    prv_recalculate(QUALITY_SYNC_NORMAL);
    return;
  }

  main_window_update_countdown(&s_today, &s_tomorrow, loc, idx);

  if (connection_service_peek_pebble_app_connection() &&
      s_last_quality_sync_at > 0 &&
      now - s_last_quality_sync_at >= QUALITY_AUTO_REFRESH_INTERVAL_S) {
    prv_request_quality_sync(false);
  }
}

// -------------------------------------------------------------------------
// Location selection callback (from ui_locations)
// -------------------------------------------------------------------------
static void prv_location_selected(int index) {
  if (index < 0 || index >= (int)s_locations.count) return;
  s_locations.selected_index = (uint8_t)index;
  storage_save(&s_locations);
  storage_clear_quality();
  main_window_clear_quality();
  prv_recalculate(QUALITY_SYNC_FORCE);
}

// -------------------------------------------------------------------------
// Main window button handlers
// -------------------------------------------------------------------------
static void prv_select_click(ClickRecognizerRef r, void *ctx) {
  locations_window_push(&s_locations,
                         s_locations.selected_index,
                         prv_location_selected);
}

static void prv_down_click(ClickRecognizerRef r, void *ctx) {
  if (s_locations.count == 0) return;
  const Location *loc = &s_locations.locs[s_locations.selected_index];
  detail_window_push(&s_today, &s_tomorrow, loc);
}

static void prv_click_config_provider(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_select_click);
  window_single_click_subscribe(BUTTON_ID_DOWN,   prv_down_click);
}

static void prv_phone_connection_handler(bool connected) {
  if (connected) {
    s_force_quality_after_phone_sync = true;
    prv_request_phone_sync();
  }
}

// -------------------------------------------------------------------------
// AppMessage — location sync from phone
// -------------------------------------------------------------------------
static void prv_inbox_received(DictionaryIterator *iter, void *ctx) {
  Tuple *t_count  = dict_find(iter, MESSAGE_KEY_LOC_COUNT);
  Tuple *t_done   = dict_find(iter, MESSAGE_KEY_LOC_SYNC_DONE);
  Tuple *t_index  = dict_find(iter, MESSAGE_KEY_LOC_INDEX);
  Tuple *t_name   = dict_find(iter, MESSAGE_KEY_LOC_NAME);
  Tuple *t_lat    = dict_find(iter, MESSAGE_KEY_LOC_LAT);
  Tuple *t_lon    = dict_find(iter, MESSAGE_KEY_LOC_LON);
  Tuple *t_offset = dict_find(iter, MESSAGE_KEY_LOC_UTC_OFFSET);
  Tuple *t_quality_loc_index = dict_find(iter, MESSAGE_KEY_QUALITY_LOC_INDEX);
  Tuple *t_quality_date_0 = dict_find(iter, MESSAGE_KEY_QUALITY_DATE_0);
  Tuple *t_quality_date_1 = dict_find(iter, MESSAGE_KEY_QUALITY_DATE_1);
  Tuple *t_quality_sunrise_0 = dict_find(iter, MESSAGE_KEY_QUALITY_SUNRISE_0);
  Tuple *t_quality_sunset_0 = dict_find(iter, MESSAGE_KEY_QUALITY_SUNSET_0);
  Tuple *t_quality_sunrise_1 = dict_find(iter, MESSAGE_KEY_QUALITY_SUNRISE_1);
  Tuple *t_quality_sunset_1 = dict_find(iter, MESSAGE_KEY_QUALITY_SUNSET_1);

  if (t_count) {
    // Start of a new sync — note expected count and preserve selected index
    s_sync_expected = (int)t_count->value->int32;
    s_sync_selected = s_locations.selected_index;
    memset(&s_locations, 0, sizeof(s_locations));
  }

  if (t_index && t_name && t_lat && t_lon && t_offset) {
    int idx = (int)t_index->value->int32;
    if (idx >= 0 && idx < MAX_LOCATIONS) {
      Location *l = &s_locations.locs[idx];
      snprintf(l->name, LOC_NAME_LEN, "%s", t_name->value->cstring);
      l->lat_e6         = t_lat->value->int32;
      l->lon_e6         = t_lon->value->int32;
      l->utc_offset_min = (int16_t)t_offset->value->int32;
      if (idx + 1 > (int)s_locations.count) {
        s_locations.count = (uint8_t)(idx + 1);
      }
    }
  }

  if (t_done) {
    // Restore selection (clamped to new count)
    s_locations.selected_index = s_sync_selected;
    if (s_locations.count > 0 &&
        s_locations.selected_index >= s_locations.count) {
      s_locations.selected_index = 0;
    }
    storage_save(&s_locations);
    storage_clear_quality();
    main_window_clear_quality();
    prv_recalculate(s_force_quality_after_phone_sync ? QUALITY_SYNC_FORCE : QUALITY_SYNC_NORMAL);
    s_force_quality_after_phone_sync = false;
    APP_LOG(APP_LOG_LEVEL_INFO, "Synced %d locations", (int)s_locations.count);
  }

  if (t_quality_loc_index && t_quality_date_0 && t_quality_date_1 &&
      t_quality_sunrise_0 && t_quality_sunset_0 &&
      t_quality_sunrise_1 && t_quality_sunset_1) {
    memset(&s_quality, 0, sizeof(s_quality));
    s_quality.synced = 1;
    s_quality.loc_index = (uint8_t)t_quality_loc_index->value->int32;
    s_quality.date_0 = t_quality_date_0->value->int32;
    s_quality.date_1 = t_quality_date_1->value->int32;
    snprintf(s_quality.sunrise_0, QUALITY_TEXT_LEN, "%s", t_quality_sunrise_0->value->cstring);
    snprintf(s_quality.sunset_0, QUALITY_TEXT_LEN, "%s", t_quality_sunset_0->value->cstring);
    snprintf(s_quality.sunrise_1, QUALITY_TEXT_LEN, "%s", t_quality_sunrise_1->value->cstring);
    snprintf(s_quality.sunset_1, QUALITY_TEXT_LEN, "%s", t_quality_sunset_1->value->cstring);

    storage_save_quality(&s_quality);
    main_window_set_quality(&s_quality);
    s_last_quality_sync_at = time(NULL);
  }
}

static void prv_inbox_dropped(AppMessageResult reason, void *ctx) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "AppMessage dropped: %d", (int)reason);
}

// -------------------------------------------------------------------------
// Init / Deinit
// -------------------------------------------------------------------------
static void prv_init(void) {
  storage_load(&s_locations);
  storage_load_quality(&s_quality);

  if (s_locations.count == 0) {
    s_locations.count          = 1;
    s_locations.selected_index = 0;
    snprintf(s_locations.locs[0].name, LOC_NAME_LEN, "London");
    s_locations.locs[0].lat_e6         =  51507400;
    s_locations.locs[0].lon_e6         =  -127800;
    s_locations.locs[0].utc_offset_min =  0;
  }

  app_message_register_inbox_received(prv_inbox_received);
  app_message_register_inbox_dropped(prv_inbox_dropped);
  app_message_open(512, 128);

  tick_timer_service_subscribe(MINUTE_UNIT, prv_tick_handler);
  connection_service_subscribe((ConnectionHandlers) {
    .pebble_app_connection_handler = prv_phone_connection_handler,
  });

  Window *win = main_window_create();
  window_set_click_config_provider(win, prv_click_config_provider);
  window_stack_push(win, true);

  if (s_quality.synced) {
    main_window_set_quality(&s_quality);
  } else {
    main_window_clear_quality();
  }

  prv_recalculate(QUALITY_SYNC_NONE);
  prv_request_phone_sync();
}

static void prv_deinit(void) {
  tick_timer_service_unsubscribe();
  connection_service_unsubscribe();
  main_window_destroy();
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
  return 0;
}
