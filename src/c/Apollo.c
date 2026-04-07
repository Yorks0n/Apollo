#include <pebble.h>
#include "solar.h"
#include "storage.h"
#include "ui_main.h"
#include "ui_locations.h"
#include "ui_detail.h"

// -------------------------------------------------------------------------
// Global state
// -------------------------------------------------------------------------
static LocationList   s_locations;
static SolarDayResult s_today;
static SolarDayResult s_tomorrow;
static int            s_prev_local_day = -1;

// AppMessage sync state
static int s_sync_expected = 0;
static uint8_t s_sync_selected = 0;

// -------------------------------------------------------------------------
// Calculation
// -------------------------------------------------------------------------
static void prv_recalculate(void) {
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

  main_window_update(&s_today, &s_tomorrow, loc);
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
    prv_recalculate();
    return;
  }

  main_window_update_countdown(&s_today, &s_tomorrow, loc);
}

// -------------------------------------------------------------------------
// Location selection callback (from ui_locations)
// -------------------------------------------------------------------------
static void prv_location_selected(int index) {
  if (index < 0 || index >= (int)s_locations.count) return;
  s_locations.selected_index = (uint8_t)index;
  storage_save(&s_locations);
  prv_recalculate();
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
    prv_recalculate();
    APP_LOG(APP_LOG_LEVEL_INFO, "Synced %d locations", (int)s_locations.count);
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
  app_message_open(256, 64);

  tick_timer_service_subscribe(MINUTE_UNIT, prv_tick_handler);

  Window *win = main_window_create();
  window_set_click_config_provider(win, prv_click_config_provider);
  window_stack_push(win, true);

  prv_recalculate();
}

static void prv_deinit(void) {
  tick_timer_service_unsubscribe();
  main_window_destroy();
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
  return 0;
}
