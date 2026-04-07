#include "ui_main.h"
#include "solar.h"
#include "storage.h"

// -------------------------------------------------------------------------
// Layout constants
// -------------------------------------------------------------------------
#define STATUS_BAR_H   16

// Vertical positions for rectangular screens (origin = top of window)
#define RECT_LOC_Y     (STATUS_BAR_H + 2)
#define RECT_LOC_H     18
#define RECT_STATUS_Y  (RECT_LOC_Y + RECT_LOC_H + 1)
#define RECT_STATUS_H  16
#define RECT_NAME_Y    (RECT_STATUS_Y + RECT_STATUS_H + 8)
#define RECT_NAME_H    22
#define RECT_TIME_Y    (RECT_NAME_Y + RECT_NAME_H + 2)
#define RECT_TIME_H    36
#define RECT_CD_Y      (RECT_TIME_Y + RECT_TIME_H + 4)
#define RECT_CD_H      16

// Vertical positions for round screens (chalk 180×180)
#define ROUND_INSET    18
#define ROUND_W        144  // safe inner width
#define ROUND_LOC_Y    (ROUND_INSET + 6)
#define ROUND_LOC_H    18
#define ROUND_STATUS_Y (ROUND_LOC_Y + ROUND_LOC_H + 2)
#define ROUND_STATUS_H 16
#define ROUND_NAME_Y   (ROUND_STATUS_Y + ROUND_STATUS_H + 6)
#define ROUND_NAME_H   22
#define ROUND_TIME_Y   (ROUND_NAME_Y + ROUND_NAME_H + 2)
#define ROUND_TIME_H   36
#define ROUND_CD_Y     (ROUND_TIME_Y + ROUND_TIME_H + 4)
#define ROUND_CD_H     16

// -------------------------------------------------------------------------
// Window data
// -------------------------------------------------------------------------
static Window    *s_window;
static TextLayer *s_tl_location;
static TextLayer *s_tl_status;
static TextLayer *s_tl_event_name;
static TextLayer *s_tl_event_time;
static TextLayer *s_tl_countdown;

// Buffers (static so they persist after window load)
static char s_buf_location[LOC_NAME_LEN + 4];
static char s_buf_status[24];
static char s_buf_event_name[24];
static char s_buf_event_time[16];
static char s_buf_countdown[24];

// -------------------------------------------------------------------------
// Helpers
// -------------------------------------------------------------------------
typedef struct {
  bool      found;
  int32_t   local_minutes;  // of the event
  bool      is_tomorrow;
  bool      is_sunrise;     // true=sunrise, false=sunset
} NextEvent;

// Determine current local time in minutes from midnight
static int32_t s_local_minutes_now(int utc_offset_min) {
  time_t now     = time(NULL);
  time_t loc     = now + (time_t)utc_offset_min * 60;
  struct tm *t   = gmtime(&loc);
  return (int32_t)(t->tm_hour * 60 + t->tm_min);
}

// Find the next SUNRISE or SUNSET relative to current local time.
static NextEvent s_find_next_event(const SolarDayResult *today,
                                   const SolarDayResult *tomorrow,
                                   int32_t current_min) {
  NextEvent ne = {false, 0, false, false};

  // Try today's sunset first (if we're before it)
  if (today->events[SOLAR_EVENT_SUNSET].status == SOLAR_STATUS_OK) {
    int32_t ss = today->events[SOLAR_EVENT_SUNSET].local_minutes;
    if (current_min < ss) {
      // Daytime: next event is today's sunset
      ne.found = true;
      ne.local_minutes = ss;
      ne.is_tomorrow   = false;
      ne.is_sunrise    = false;
      return ne;
    }
  }

  // Try today's sunrise (if we're before it — i.e. very early morning)
  if (today->events[SOLAR_EVENT_SUNRISE].status == SOLAR_STATUS_OK) {
    int32_t sr = today->events[SOLAR_EVENT_SUNRISE].local_minutes;
    if (current_min < sr) {
      ne.found = true;
      ne.local_minutes = sr;
      ne.is_tomorrow   = false;
      ne.is_sunrise    = true;
      return ne;
    }
  }

  // Past sunset (or no sunset today) → find tomorrow's sunrise
  if (tomorrow->events[SOLAR_EVENT_SUNRISE].status == SOLAR_STATUS_OK) {
    ne.found = true;
    ne.local_minutes = tomorrow->events[SOLAR_EVENT_SUNRISE].local_minutes;
    ne.is_tomorrow   = true;
    ne.is_sunrise    = true;
    return ne;
  }

  // Fallback: tomorrow's sunset
  if (tomorrow->events[SOLAR_EVENT_SUNSET].status == SOLAR_STATUS_OK) {
    ne.found = true;
    ne.local_minutes = tomorrow->events[SOLAR_EVENT_SUNSET].local_minutes;
    ne.is_tomorrow   = true;
    ne.is_sunrise    = false;
  }

  return ne;
}

static bool s_is_daytime(const SolarDayResult *today, int32_t current_min) {
  bool has_sr = today->events[SOLAR_EVENT_SUNRISE].status == SOLAR_STATUS_OK;
  bool has_ss = today->events[SOLAR_EVENT_SUNSET].status == SOLAR_STATUS_OK;
  if (!has_sr || !has_ss) return today->is_polar_day;
  int32_t sr = today->events[SOLAR_EVENT_SUNRISE].local_minutes;
  int32_t ss = today->events[SOLAR_EVENT_SUNSET].local_minutes;
  return (current_min >= sr && current_min < ss);
}

// Format a date encoded as YYYYMMDD into a readable string (e.g. "Apr 15")
static void s_format_date(int32_t yyyymmdd, char *buf, size_t len) {
  if (yyyymmdd == 0) { snprintf(buf, len, "?"); return; }
  int month = (int)((yyyymmdd % 10000) / 100);
  int day   = (int)(yyyymmdd % 100);
  static const char *months[] = {
    "", "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
  };
  const char *mname = (month >= 1 && month <= 12) ? months[month] : "?";
  snprintf(buf, len, "%s %d", mname, day);
}

// -------------------------------------------------------------------------
// Update logic
// -------------------------------------------------------------------------
static void s_do_update(const SolarDayResult *today,
                        const SolarDayResult *tomorrow,
                        const Location *loc) {
  // Location name
  snprintf(s_buf_location, sizeof(s_buf_location), "%s", loc->name);
  text_layer_set_text(s_tl_location, s_buf_location);

  int32_t current_min = s_local_minutes_now(loc->utc_offset_min);
  bool daytime        = s_is_daytime(today, current_min);

  // Status label
  if (today->is_polar_day) {
    snprintf(s_buf_status, sizeof(s_buf_status), "Polar Day");
  } else if (today->is_polar_night) {
    snprintf(s_buf_status, sizeof(s_buf_status), "Polar Night");
  } else {
    snprintf(s_buf_status, sizeof(s_buf_status), "%s", daytime ? "Day" : "Night");
  }
  text_layer_set_text(s_tl_status, s_buf_status);

  // Color the status bar on color devices
#ifdef PBL_COLOR
  if (today->is_polar_day) {
    text_layer_set_background_color(s_tl_status, GColorChromeYellow);
    text_layer_set_text_color(s_tl_status, GColorBlack);
  } else if (today->is_polar_night) {
    text_layer_set_background_color(s_tl_status, GColorMidnightGreen);
    text_layer_set_text_color(s_tl_status, GColorWhite);
  } else if (daytime) {
    text_layer_set_background_color(s_tl_status, GColorOxfordBlue);
    text_layer_set_text_color(s_tl_status, GColorWhite);
  } else {
    text_layer_set_background_color(s_tl_status, GColorBlack);
    text_layer_set_text_color(s_tl_status, GColorCyan);
  }
#else
  // B&W: invert the status bar to signal day/night
  text_layer_set_background_color(s_tl_status, daytime ? GColorBlack : GColorWhite);
  text_layer_set_text_color(s_tl_status, daytime ? GColorWhite : GColorBlack);
#endif

  // Polar condition: show next event date instead of time
  if (today->is_polar_night) {
    snprintf(s_buf_event_name, sizeof(s_buf_event_name), "Next Sunrise");
    char date_buf[16];
    s_format_date(today->next_sunrise_date, date_buf, sizeof(date_buf));
    snprintf(s_buf_event_time, sizeof(s_buf_event_time), "%s", date_buf);
    snprintf(s_buf_countdown, sizeof(s_buf_countdown), " ");
    text_layer_set_text(s_tl_event_name, s_buf_event_name);
    text_layer_set_text(s_tl_event_time, s_buf_event_time);
    text_layer_set_text(s_tl_countdown, s_buf_countdown);
    return;
  }
  if (today->is_polar_day) {
    snprintf(s_buf_event_name, sizeof(s_buf_event_name), "Next Sunset");
    char date_buf[16];
    s_format_date(today->next_sunset_date, date_buf, sizeof(date_buf));
    snprintf(s_buf_event_time, sizeof(s_buf_event_time), "%s", date_buf);
    snprintf(s_buf_countdown, sizeof(s_buf_countdown), " ");
    text_layer_set_text(s_tl_event_name, s_buf_event_name);
    text_layer_set_text(s_tl_event_time, s_buf_event_time);
    text_layer_set_text(s_tl_countdown, s_buf_countdown);
    return;
  }

  // Normal case: find the next sunrise or sunset
  NextEvent ne = s_find_next_event(today, tomorrow, current_min);

  if (ne.found) {
    snprintf(s_buf_event_name, sizeof(s_buf_event_name),
             "%s", ne.is_sunrise ? "Sunrise" : "Sunset");

    int32_t em = ne.local_minutes;
    // Handle cross-midnight times
    int h = (int)((em % 1440) / 60);
    int m = (int)(em % 60);
    if (h < 0) h += 24;
    if (m < 0) m += 60;
    snprintf(s_buf_event_time, sizeof(s_buf_event_time), "%02d:%02d", h, m);

    // Countdown
    int32_t effective_now = current_min;
    int32_t effective_ev  = em;
    if (ne.is_tomorrow) effective_ev += 1440;
    int32_t delta = effective_ev - effective_now;
    if (delta < 0) delta = 0;
    int dh = (int)(delta / 60);
    int dm = (int)(delta % 60);
    if (dh > 0) {
      snprintf(s_buf_countdown, sizeof(s_buf_countdown), "in %dh %dm", dh, dm);
    } else {
      snprintf(s_buf_countdown, sizeof(s_buf_countdown), "in %dm", dm);
    }
  } else {
    snprintf(s_buf_event_name, sizeof(s_buf_event_name), "---");
    snprintf(s_buf_event_time, sizeof(s_buf_event_time), "--:--");
    snprintf(s_buf_countdown, sizeof(s_buf_countdown), " ");
  }

  text_layer_set_text(s_tl_event_name, s_buf_event_name);
  text_layer_set_text(s_tl_event_time, s_buf_event_time);
  text_layer_set_text(s_tl_countdown, s_buf_countdown);
}

// -------------------------------------------------------------------------
// Window lifecycle
// -------------------------------------------------------------------------
static void prv_window_load(Window *window) {
  Layer *root  = window_get_root_layer(window);
  GRect  bounds = layer_get_bounds(root);
  int    w      = bounds.size.w;

#ifdef PBL_ROUND
  int inset = ROUND_INSET;
  int loc_y    = ROUND_LOC_Y;
  int loc_h    = ROUND_LOC_H;
  int stat_y   = ROUND_STATUS_Y;
  int stat_h   = ROUND_STATUS_H;
  int name_y   = ROUND_NAME_Y;
  int name_h   = ROUND_NAME_H;
  int time_y   = ROUND_TIME_Y;
  int time_h   = ROUND_TIME_H;
  int cd_y     = ROUND_CD_Y;
  int cd_h     = ROUND_CD_H;
#else
  int inset  = 0;
  int loc_y    = RECT_LOC_Y;
  int loc_h    = RECT_LOC_H;
  int stat_y   = RECT_STATUS_Y;
  int stat_h   = RECT_STATUS_H;
  int name_y   = RECT_NAME_Y;
  int name_h   = RECT_NAME_H;
  int time_y   = RECT_TIME_Y;
  int time_h   = RECT_TIME_H;
  int cd_y     = RECT_CD_Y;
  int cd_h     = RECT_CD_H;
#endif

  int eff_w = w - 2 * inset;

  // Location name
  s_tl_location = text_layer_create(GRect(inset, loc_y, eff_w, loc_h));
  text_layer_set_font(s_tl_location,
    fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
  text_layer_set_text_alignment(s_tl_location, GTextAlignmentCenter);
  text_layer_set_background_color(s_tl_location, GColorClear);
  layer_add_child(root, text_layer_get_layer(s_tl_location));

  // Status label (Day / Night / Polar Day / Polar Night)
  s_tl_status = text_layer_create(GRect(inset, stat_y, eff_w, stat_h));
  text_layer_set_font(s_tl_status,
    fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_alignment(s_tl_status, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_tl_status));

  // Event name (Sunrise / Sunset / etc.)
  s_tl_event_name = text_layer_create(GRect(inset, name_y, eff_w, name_h));
  text_layer_set_font(s_tl_event_name,
    fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_text_alignment(s_tl_event_name, GTextAlignmentCenter);
  text_layer_set_background_color(s_tl_event_name, GColorClear);
  layer_add_child(root, text_layer_get_layer(s_tl_event_name));

  // Event time (large)
  s_tl_event_time = text_layer_create(GRect(inset, time_y, eff_w, time_h));
  text_layer_set_font(s_tl_event_time,
    fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text_alignment(s_tl_event_time, GTextAlignmentCenter);
  text_layer_set_background_color(s_tl_event_time, GColorClear);
#ifdef PBL_COLOR
  text_layer_set_text_color(s_tl_event_time, GColorChromeYellow);
#endif
  layer_add_child(root, text_layer_get_layer(s_tl_event_time));

  // Countdown
  s_tl_countdown = text_layer_create(GRect(inset, cd_y, eff_w, cd_h));
  text_layer_set_font(s_tl_countdown,
    fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_alignment(s_tl_countdown, GTextAlignmentCenter);
  text_layer_set_background_color(s_tl_countdown, GColorClear);
  layer_add_child(root, text_layer_get_layer(s_tl_countdown));

  // Placeholder text until first update
  text_layer_set_text(s_tl_location,   "---");
  text_layer_set_text(s_tl_status,     "---");
  text_layer_set_text(s_tl_event_name, "---");
  text_layer_set_text(s_tl_event_time, "--:--");
  text_layer_set_text(s_tl_countdown,  "");
}

static void prv_window_unload(Window *window) {
  text_layer_destroy(s_tl_location);
  text_layer_destroy(s_tl_status);
  text_layer_destroy(s_tl_event_name);
  text_layer_destroy(s_tl_event_time);
  text_layer_destroy(s_tl_countdown);
}

// -------------------------------------------------------------------------
// Public API
// -------------------------------------------------------------------------
Window *main_window_create(void) {
  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers){
    .load   = prv_window_load,
    .unload = prv_window_unload,
  });
  return s_window;
}

void main_window_destroy(void) {
  if (s_window) {
    window_destroy(s_window);
    s_window = NULL;
  }
}

void main_window_update(const SolarDayResult *today,
                        const SolarDayResult *tomorrow,
                        const Location *loc) {
  if (!s_window) return;
  s_do_update(today, tomorrow, loc);
}

void main_window_update_countdown(const SolarDayResult *today,
                                  const SolarDayResult *tomorrow,
                                  const Location *loc) {
  // For now, do a full update; the buffers are already allocated.
  main_window_update(today, tomorrow, loc);
}
