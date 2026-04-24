#include "ui_main.h"
#include "solar.h"
#include "storage.h"
#include <string.h>

#define RING_PI 3.14159265f
typedef enum {
  SOLAR_PHASE_NIGHT = 0,
  SOLAR_PHASE_TWILIGHT,
  SOLAR_PHASE_GOLDEN,
  SOLAR_PHASE_DAY,
  SOLAR_PHASE_POLAR_DAY,
  SOLAR_PHASE_POLAR_NIGHT,
} SolarPhase;

typedef struct {
  bool      found;
  int32_t   local_minutes;
  bool      is_tomorrow;
  bool      is_sunrise;
} NextEvent;

typedef struct {
  bool      found;
  int32_t   local_minutes;
  bool      is_tomorrow;
} NextGoldenHour;

typedef struct {
  GRect rect;
  int   corner_radius;
  int   stroke_width;
  int   golden_stroke_width;
  int   marker_radius;
} RingMetrics;

static Window         *s_window;
static Layer          *s_ring_layer;
static TextLayer      *s_tl_location;
static TextLayer      *s_tl_status;
static TextLayer      *s_tl_event_name;
static TextLayer      *s_tl_event_time;
static TextLayer      *s_tl_countdown;
static TextLayer      *s_tl_quality;
static SolarDayResult  s_cached_today;
static SolarDayResult  s_cached_tomorrow;
static Location        s_cached_loc;
static QualityCache    s_cached_quality;
static int             s_cached_loc_index;
static bool            s_has_data;
static bool            s_has_quality;

static char s_buf_location[LOC_NAME_LEN + 4];
static char s_buf_status[32];
static char s_buf_event_name[32];
static char s_buf_event_time[16];
static char s_buf_countdown[24];
static char s_buf_quality[QUALITY_TEXT_LEN + 8];
static int  s_countdown_x;
static int  s_countdown_y;
static int  s_countdown_w;
static int  s_countdown_h_single;
static int  s_countdown_h_multi;
static int  s_quality_x;
static int  s_quality_y;
static int  s_quality_w;
static int  s_quality_h;
static int  s_quality_gap;
static int  s_quality_max_y;

static int s_scale_from_width(int width, int numerator, int denominator) {
  return (width * numerator + denominator / 2) / denominator;
}

#ifndef PBL_ROUND
static bool s_is_large_rect_display(GRect bounds) {
  return bounds.size.w >= 180 || bounds.size.h >= 200;
}
#endif

static int32_t s_normalize_minutes(int32_t minutes) {
  minutes %= MINUTES_PER_DAY;
  if (minutes < 0) {
    minutes += MINUTES_PER_DAY;
  }
  return minutes;
}

static bool s_event_ok(const SolarEvent *event) {
  return event->status == SOLAR_STATUS_OK;
}

static int32_t s_local_minutes_now(int utc_offset_min) {
  time_t now = time(NULL);
  time_t local = now + (time_t)utc_offset_min * 60;
  struct tm *t = gmtime(&local);
  return (int32_t)(t->tm_hour * 60 + t->tm_min);
}

static int32_t s_local_date_now(int utc_offset_min) {
  time_t now = time(NULL);
  int year = 0;
  int month = 0;
  int day = 0;
  solar_local_date(now, utc_offset_min, &year, &month, &day);
  return year * 10000 + month * 100 + day;
}

static void s_format_time_minutes(int32_t minutes, char *buf, size_t len) {
  minutes = s_normalize_minutes(minutes);
  snprintf(buf, len, "%02d:%02d", (int)(minutes / 60), (int)(minutes % 60));
}

static void s_format_duration_minutes(int32_t delta_minutes, char *buf, size_t len) {
  if (delta_minutes <= 0) {
    snprintf(buf, len, "Now");
    return;
  }

  if (delta_minutes >= 60) {
    snprintf(buf, len, "%dh %02dm",
             (int)(delta_minutes / 60),
             (int)(delta_minutes % 60));
    return;
  }

  snprintf(buf, len, "%dm", (int)delta_minutes);
}

static void s_format_date(int32_t yyyymmdd, char *buf, size_t len) {
  if (yyyymmdd == 0) {
    snprintf(buf, len, "?");
    return;
  }

  int month = (int)((yyyymmdd % 10000) / 100);
  int day = (int)(yyyymmdd % 100);
  static const char *months[] = {
    "", "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
  };
  const char *month_name = (month >= 1 && month <= 12) ? months[month] : "?";
  snprintf(buf, len, "%s %d", month_name, day);
}

static bool s_minute_in_interval(int32_t minute, int32_t start, int32_t end) {
  minute = s_normalize_minutes(minute);
  start = s_normalize_minutes(start);
  end = s_normalize_minutes(end);

  if (start == end) {
    return false;
  }
  if (start < end) {
    return minute >= start && minute < end;
  }
  return minute >= start || minute < end;
}

static int32_t s_minutes_until(int32_t current_min, int32_t target_min, bool is_tomorrow) {
  int32_t delta = target_min - current_min;
  if (is_tomorrow) {
    delta += MINUTES_PER_DAY;
  }
  if (delta < 0) {
    delta += MINUTES_PER_DAY;
  }
  return delta;
}

static void s_format_event_line(const char *label,
                                int32_t local_minutes,
                                bool is_tomorrow,
                                char *buf,
                                size_t len) {
  char time_buf[8];
  s_format_time_minutes(local_minutes, time_buf, sizeof(time_buf));

  if (is_tomorrow) {
    snprintf(buf, len, "Tomorrow %s\n%s", label, time_buf);
    return;
  }

  snprintf(buf, len, "%s %s", label, time_buf);
}

static void s_set_countdown_text(const char *text) {
  bool multiline = strchr(text, '\n') != NULL;
  int countdown_h = multiline ? s_countdown_h_multi : s_countdown_h_single;
  int quality_y = s_countdown_y + countdown_h + s_quality_gap;
  if (quality_y > s_quality_max_y) {
    quality_y = s_quality_max_y;
  }
  layer_set_frame(text_layer_get_layer(s_tl_countdown),
                  GRect(s_countdown_x,
                        s_countdown_y,
                        s_countdown_w,
                        countdown_h));
  text_layer_set_text(s_tl_countdown, text);

  if (s_tl_quality) {
    layer_set_frame(text_layer_get_layer(s_tl_quality),
                    GRect(s_quality_x,
                          quality_y,
                          s_quality_w,
                          s_quality_h));
  }
}

static void s_set_quality_text(const char *text) {
  bool has_text = text && text[0] != '\0';
  if (s_tl_quality) {
    layer_set_hidden(text_layer_get_layer(s_tl_quality), !has_text);
  }
  if (has_text) {
    text_layer_set_text(s_tl_quality, text);
  } else if (s_tl_quality) {
    text_layer_set_text(s_tl_quality, "");
  }
}

static NextEvent s_find_next_event(const SolarDayResult *today,
                                   const SolarDayResult *tomorrow,
                                   int32_t current_min) {
  NextEvent next = {false, 0, false, false};

  if (today->events[SOLAR_EVENT_SUNRISE].status == SOLAR_STATUS_OK) {
    int32_t sunrise = today->events[SOLAR_EVENT_SUNRISE].local_minutes;
    if (current_min < sunrise) {
      next.found = true;
      next.local_minutes = sunrise;
      next.is_tomorrow = false;
      next.is_sunrise = true;
      return next;
    }
  }

  if (today->events[SOLAR_EVENT_SUNSET].status == SOLAR_STATUS_OK) {
    int32_t sunset = today->events[SOLAR_EVENT_SUNSET].local_minutes;
    if (current_min < sunset) {
      next.found = true;
      next.local_minutes = sunset;
      next.is_tomorrow = false;
      next.is_sunrise = false;
      return next;
    }
  }

  if (tomorrow->events[SOLAR_EVENT_SUNRISE].status == SOLAR_STATUS_OK) {
    next.found = true;
    next.local_minutes = tomorrow->events[SOLAR_EVENT_SUNRISE].local_minutes;
    next.is_tomorrow = true;
    next.is_sunrise = true;
    return next;
  }

  if (tomorrow->events[SOLAR_EVENT_SUNSET].status == SOLAR_STATUS_OK) {
    next.found = true;
    next.local_minutes = tomorrow->events[SOLAR_EVENT_SUNSET].local_minutes;
    next.is_tomorrow = true;
    next.is_sunrise = false;
  }

  return next;
}

static NextGoldenHour s_find_next_golden_hour(const SolarDayResult *today,
                                              const SolarDayResult *tomorrow,
                                              int32_t current_min) {
  NextGoldenHour next = {false, 0, false};

  if (today->events[SOLAR_EVENT_GOLDEN_MORNING].status == SOLAR_STATUS_OK) {
    int32_t morning = today->events[SOLAR_EVENT_GOLDEN_MORNING].local_minutes;
    if (current_min < morning) {
      next.found = true;
      next.local_minutes = morning;
      next.is_tomorrow = false;
      return next;
    }
  }

  if (today->events[SOLAR_EVENT_GOLDEN_EVENING].status == SOLAR_STATUS_OK) {
    int32_t evening = today->events[SOLAR_EVENT_GOLDEN_EVENING].local_minutes;
    if (current_min < evening) {
      next.found = true;
      next.local_minutes = evening;
      next.is_tomorrow = false;
      return next;
    }
  }

  if (tomorrow->events[SOLAR_EVENT_GOLDEN_MORNING].status == SOLAR_STATUS_OK) {
    next.found = true;
    next.local_minutes = tomorrow->events[SOLAR_EVENT_GOLDEN_MORNING].local_minutes;
    next.is_tomorrow = true;
    return next;
  }

  if (tomorrow->events[SOLAR_EVENT_GOLDEN_EVENING].status == SOLAR_STATUS_OK) {
    next.found = true;
    next.local_minutes = tomorrow->events[SOLAR_EVENT_GOLDEN_EVENING].local_minutes;
    next.is_tomorrow = true;
  }

  return next;
}

static bool s_is_daytime(const SolarDayResult *today, int32_t current_min) {
  bool has_sunrise = s_event_ok(&today->events[SOLAR_EVENT_SUNRISE]);
  bool has_sunset = s_event_ok(&today->events[SOLAR_EVENT_SUNSET]);

  if (!has_sunrise || !has_sunset) {
    return today->is_polar_day;
  }

  return s_minute_in_interval(current_min,
                              today->events[SOLAR_EVENT_SUNRISE].local_minutes,
                              today->events[SOLAR_EVENT_SUNSET].local_minutes);
}

static bool s_is_morning_golden(const SolarDayResult *today, int32_t current_min) {
  return s_event_ok(&today->events[SOLAR_EVENT_GOLDEN_MORNING]) &&
         s_event_ok(&today->golden_morning_end) &&
         s_minute_in_interval(current_min,
                              today->events[SOLAR_EVENT_GOLDEN_MORNING].local_minutes,
                              today->golden_morning_end.local_minutes);
}

static bool s_is_evening_golden(const SolarDayResult *today, int32_t current_min) {
  return s_event_ok(&today->events[SOLAR_EVENT_GOLDEN_EVENING]) &&
         s_event_ok(&today->events[SOLAR_EVENT_DUSK_GLOW]) &&
         s_minute_in_interval(current_min,
                              today->events[SOLAR_EVENT_GOLDEN_EVENING].local_minutes,
                              today->events[SOLAR_EVENT_DUSK_GLOW].local_minutes);
}

static bool s_is_twilight(const SolarDayResult *today, int32_t current_min) {
  bool morning_twilight = s_event_ok(&today->events[SOLAR_EVENT_FIRST_LIGHT]) &&
                          s_event_ok(&today->events[SOLAR_EVENT_GOLDEN_MORNING]) &&
                          s_minute_in_interval(current_min,
                                               today->events[SOLAR_EVENT_FIRST_LIGHT].local_minutes,
                                               today->events[SOLAR_EVENT_GOLDEN_MORNING].local_minutes);
  bool evening_twilight = s_event_ok(&today->events[SOLAR_EVENT_DUSK_GLOW]) &&
                          s_event_ok(&today->events[SOLAR_EVENT_LAST_LIGHT]) &&
                          s_minute_in_interval(current_min,
                                               today->events[SOLAR_EVENT_DUSK_GLOW].local_minutes,
                                               today->events[SOLAR_EVENT_LAST_LIGHT].local_minutes);
  return morning_twilight || evening_twilight;
}

static SolarPhase s_phase_for_minute(const SolarDayResult *today, int32_t current_min) {
  if (today->is_polar_day) {
    return SOLAR_PHASE_POLAR_DAY;
  }
  if (today->is_polar_night) {
    return SOLAR_PHASE_POLAR_NIGHT;
  }
  if (s_is_morning_golden(today, current_min) || s_is_evening_golden(today, current_min)) {
    return SOLAR_PHASE_GOLDEN;
  }
  if (s_is_twilight(today, current_min)) {
    return SOLAR_PHASE_TWILIGHT;
  }
  if (s_is_daytime(today, current_min)) {
    return SOLAR_PHASE_DAY;
  }
  return SOLAR_PHASE_NIGHT;
}

static const char *s_phase_label(const SolarDayResult *today, int32_t current_min) {
  if (today->is_polar_day) {
    return "Polar Day";
  }
  if (today->is_polar_night) {
    return "Polar Night";
  }
  if (s_is_morning_golden(today, current_min)) {
    return "Morning Golden Hour";
  }
  if (s_is_evening_golden(today, current_min)) {
    return "Evening Golden Hour";
  }
  if (s_is_twilight(today, current_min)) {
    return "Twilight";
  }
  return s_is_daytime(today, current_min) ? "Day" : "Night";
}

static const char *s_quality_text_for_target(int loc_index,
                                             bool is_sunrise,
                                             bool is_tomorrow,
                                             int32_t today_date,
                                             int32_t tomorrow_date) {
  int32_t target_date = is_tomorrow ? tomorrow_date : today_date;
  const char *text = NULL;

  if (!s_has_quality || !s_cached_quality.synced ||
      s_cached_quality.loc_index != (uint8_t)loc_index) {
    return NULL;
  }

  if (target_date == s_cached_quality.date_0) {
    text = is_sunrise ? s_cached_quality.sunrise_0 : s_cached_quality.sunset_0;
  } else if (target_date == s_cached_quality.date_1) {
    text = is_sunrise ? s_cached_quality.sunrise_1 : s_cached_quality.sunset_1;
  }

  return (text && text[0] != '\0') ? text : NULL;
}

static const char *s_current_quality_text(const SolarDayResult *today,
                                          const SolarDayResult *tomorrow,
                                          const Location *loc,
                                          int loc_index,
                                          int32_t current_min) {
  int32_t today_date = s_local_date_now(loc->utc_offset_min);
  int year = (int)(today_date / 10000);
  int month = (int)((today_date % 10000) / 100);
  int day = (int)(today_date % 100);
  int32_t tomorrow_date;
  SolarPhase phase;

  solar_date_add_days(&year, &month, &day, 1);
  tomorrow_date = year * 10000 + month * 100 + day;
  phase = s_phase_for_minute(today, current_min);

  if (today->is_polar_day || today->is_polar_night) {
    return NULL;
  }

  if (phase == SOLAR_PHASE_GOLDEN) {
    bool morning = s_is_morning_golden(today, current_min);
    return s_quality_text_for_target(loc_index,
                                     morning,
                                     false,
                                     today_date,
                                     tomorrow_date);
  }

  {
    NextEvent next = s_find_next_event(today, tomorrow, current_min);
    if (!next.found) {
      return NULL;
    }

    return s_quality_text_for_target(loc_index,
                                     next.is_sunrise,
                                     next.is_tomorrow,
                                     today_date,
                                     tomorrow_date);
  }
}

#ifdef PBL_COLOR
#define APOLLO_RING_NIGHT  GColorDukeBlue
#define APOLLO_RING_DAY    GColorPictonBlue
#define APOLLO_RING_GOLDEN GColorOrange
#define APOLLO_SUN_COLOR   GColorRed
#define APOLLO_TIME_SUN    GColorOrange
#define APOLLO_TIME_GOLDEN GColorYellow

static GColor s_status_color_for_phase(SolarPhase phase) {
  switch (phase) {
    case SOLAR_PHASE_GOLDEN:
      return GColorChromeYellow;
    case SOLAR_PHASE_TWILIGHT:
      return GColorCyan;
    case SOLAR_PHASE_DAY:
    case SOLAR_PHASE_POLAR_DAY:
      return GColorWhite;
    case SOLAR_PHASE_POLAR_NIGHT:
    case SOLAR_PHASE_NIGHT:
    default:
      return GColorLightGray;
  }
}
#endif

static RingMetrics s_get_ring_metrics(GRect bounds) {
  RingMetrics metrics;

#ifdef PBL_ROUND
  int inset = 4;
  int diameter = bounds.size.w < bounds.size.h ? bounds.size.w : bounds.size.h;
  diameter -= inset * 2;
  metrics.rect = GRect((bounds.size.w - diameter) / 2,
                       (bounds.size.h - diameter) / 2,
                       diameter,
                       diameter);
  metrics.corner_radius = diameter / 2;
  metrics.stroke_width = 12;
  metrics.golden_stroke_width = 12;
  metrics.marker_radius = 6;
#else
  bool large_rect = s_is_large_rect_display(bounds);
  int inset_x = large_rect ? 6 : 4;
  int inset_y = large_rect ? 6 : 4;
  metrics.rect = GRect(inset_x,
                       inset_y,
                       bounds.size.w - inset_x * 2,
                       bounds.size.h - inset_y * 2);
  metrics.corner_radius = metrics.rect.size.h / 6;
  if (metrics.corner_radius < (large_rect ? 22 : 18)) {
    metrics.corner_radius = large_rect ? 22 : 18;
  }
  if (metrics.corner_radius > (large_rect ? 32 : 26)) {
    metrics.corner_radius = large_rect ? 32 : 26;
  }
  metrics.stroke_width = large_rect ? 12 : 10;
  metrics.golden_stroke_width = large_rect ? 12 : 10;
  metrics.marker_radius = large_rect ? 6 : 5;
#endif

  return metrics;
}

static GPoint s_arc_point(float cx, float cy, int radius, int32_t angle_units) {
  int32_t cos_v = cos_lookup(angle_units);
  int32_t sin_v = sin_lookup(angle_units);
  return GPoint((int16_t)(cx + (radius * cos_v) / TRIG_MAX_RATIO),
                (int16_t)(cy + (radius * sin_v) / TRIG_MAX_RATIO));
}

#ifdef PBL_ROUND
static GPoint s_point_on_round_ring(const RingMetrics *metrics, float fraction) {
  int32_t angle = (int32_t)(fraction * TRIG_MAX_ANGLE) - TRIG_MAX_ANGLE / 4;
  int radius = metrics->rect.size.w / 2;
  float cx = metrics->rect.origin.x + metrics->rect.size.w / 2.0f;
  float cy = metrics->rect.origin.y + metrics->rect.size.h / 2.0f;
  return s_arc_point(cx, cy, radius, angle);
}
#else
static GPoint s_point_on_rect_ring(const RingMetrics *metrics, float fraction) {
  float left = metrics->rect.origin.x;
  float top = metrics->rect.origin.y;
  float right = metrics->rect.origin.x + metrics->rect.size.w - 1;
  float bottom = metrics->rect.origin.y + metrics->rect.size.h - 1;
  int radius = metrics->corner_radius;
  float top_half = (right - left - 2.0f * radius) * 0.5f;
  float side = bottom - top - 2.0f * radius;
  float bottom_edge = right - left - 2.0f * radius;
  float arc = RING_PI * radius * 0.5f;
  float total = top_half * 2.0f + bottom_edge + side * 2.0f + arc * 4.0f;
  float distance = fraction * total;
  float center_x = (left + right) * 0.5f;

  if (distance < top_half) {
    return GPoint((int16_t)(center_x + distance), (int16_t)top);
  }
  distance -= top_half;

  if (distance < arc) {
    float part = distance / arc;
    int32_t angle = -TRIG_MAX_ANGLE / 4 + (int32_t)(part * (TRIG_MAX_ANGLE / 4));
    return s_arc_point(right - radius, top + radius, radius, angle);
  }
  distance -= arc;

  if (distance < side) {
    return GPoint((int16_t)right, (int16_t)(top + radius + distance));
  }
  distance -= side;

  if (distance < arc) {
    float part = distance / arc;
    int32_t angle = (int32_t)(part * (TRIG_MAX_ANGLE / 4));
    return s_arc_point(right - radius, bottom - radius, radius, angle);
  }
  distance -= arc;

  if (distance < bottom_edge) {
    return GPoint((int16_t)(right - radius - distance), (int16_t)bottom);
  }
  distance -= bottom_edge;

  if (distance < arc) {
    float part = distance / arc;
    int32_t angle = TRIG_MAX_ANGLE / 4 + (int32_t)(part * (TRIG_MAX_ANGLE / 4));
    return s_arc_point(left + radius, bottom - radius, radius, angle);
  }
  distance -= arc;

  if (distance < side) {
    return GPoint((int16_t)left, (int16_t)(bottom - radius - distance));
  }
  distance -= side;

  if (distance < arc) {
    float part = distance / arc;
    int32_t angle = TRIG_MAX_ANGLE / 2 + (int32_t)(part * (TRIG_MAX_ANGLE / 4));
    return s_arc_point(left + radius, top + radius, radius, angle);
  }
  distance -= arc;

  return GPoint((int16_t)(left + radius + distance), (int16_t)top);
}
#endif

static GPoint s_point_on_ring(const RingMetrics *metrics, float fraction) {
  fraction += 0.5f;
  if (fraction >= 1.0f) {
    fraction -= 1.0f;
  }
#ifdef PBL_ROUND
  return s_point_on_round_ring(metrics, fraction);
#else
  return s_point_on_rect_ring(metrics, fraction);
#endif
}

static void s_draw_ring_range(GContext *ctx,
                              const RingMetrics *metrics,
                              int32_t start_min,
                              int32_t end_min,
                              GColor color,
                              int width) {
  int32_t duration = end_min - start_min;
  int steps;
  GPoint prev;

  if (duration <= 0) {
    return;
  }

  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, width);
  steps = (duration * 180) / MINUTES_PER_DAY;
  if (steps < 2) {
    steps = 2;
  }

  prev = s_point_on_ring(metrics, (float)start_min / (float)MINUTES_PER_DAY);
  for (int i = 1; i <= steps; i++) {
    int32_t minute = start_min + (duration * i) / steps;
    GPoint point = s_point_on_ring(metrics, (float)minute / (float)MINUTES_PER_DAY);
    graphics_draw_line(ctx, prev, point);
    prev = point;
  }
}

static void s_draw_ring_segment(GContext *ctx,
                                const RingMetrics *metrics,
                                int32_t start_min,
                                int32_t end_min,
                                GColor color,
                                int width) {
  start_min = s_normalize_minutes(start_min);
  end_min = s_normalize_minutes(end_min);

  if (start_min == end_min) {
    return;
  }

  if (start_min < end_min) {
    s_draw_ring_range(ctx, metrics, start_min, end_min, color, width);
    return;
  }

  s_draw_ring_range(ctx, metrics, start_min, MINUTES_PER_DAY, color, width);
  s_draw_ring_range(ctx, metrics, 0, end_min, color, width);
}

#ifndef PBL_COLOR
static void s_fill_ring_range_bw(GContext *ctx,
                                 const RingMetrics *metrics,
                                 int32_t start_min,
                                 int32_t end_min,
                                 GColor color,
                                 int width) {
  int32_t duration = end_min - start_min;
  int steps;
  int radius;

  if (duration <= 0) {
    return;
  }

  radius = width / 2;
  if (radius < 1) {
    radius = 1;
  }

  steps = (duration * 720) / MINUTES_PER_DAY;
  if (steps < 6) {
    steps = 6;
  }

  graphics_context_set_fill_color(ctx, color);
  for (int i = 0; i <= steps; i++) {
    int32_t minute = start_min + (duration * i) / steps;
    GPoint point = s_point_on_ring(metrics, (float)minute / (float)MINUTES_PER_DAY);
    graphics_fill_circle(ctx, point, radius);
  }
}

static void s_fill_ring_segment_bw(GContext *ctx,
                                   const RingMetrics *metrics,
                                   int32_t start_min,
                                   int32_t end_min,
                                   GColor color,
                                   int width) {
  start_min = s_normalize_minutes(start_min);
  end_min = s_normalize_minutes(end_min);

  if (start_min == end_min) {
    return;
  }

  if (start_min < end_min) {
    s_fill_ring_range_bw(ctx, metrics, start_min, end_min, color, width);
    return;
  }

  s_fill_ring_range_bw(ctx, metrics, start_min, MINUTES_PER_DAY, color, width);
  s_fill_ring_range_bw(ctx, metrics, 0, end_min, color, width);
}
#endif

static void s_ring_update_proc(Layer *layer, GContext *ctx) {
  if (!s_has_data) {
    return;
  }

  RingMetrics metrics = s_get_ring_metrics(layer_get_bounds(layer));
  int32_t current_min = s_local_minutes_now(s_cached_loc.utc_offset_min);
  SolarPhase current_phase = s_phase_for_minute(&s_cached_today, current_min);

#ifdef PBL_COLOR
  s_draw_ring_range(ctx, &metrics, 0, MINUTES_PER_DAY, APOLLO_RING_NIGHT, metrics.stroke_width);

  if (s_cached_today.is_polar_day) {
    s_draw_ring_range(ctx, &metrics, 0, MINUTES_PER_DAY, APOLLO_RING_DAY, metrics.stroke_width);
  } else if (!s_cached_today.is_polar_night) {
    if (s_event_ok(&s_cached_today.events[SOLAR_EVENT_SUNRISE]) &&
        s_event_ok(&s_cached_today.events[SOLAR_EVENT_SUNSET])) {
      s_draw_ring_segment(ctx, &metrics,
                          s_cached_today.events[SOLAR_EVENT_SUNRISE].local_minutes,
                          s_cached_today.events[SOLAR_EVENT_SUNSET].local_minutes,
                          APOLLO_RING_DAY,
                          metrics.stroke_width);
    }

    if (s_event_ok(&s_cached_today.events[SOLAR_EVENT_GOLDEN_MORNING]) &&
        s_event_ok(&s_cached_today.golden_morning_end)) {
      s_draw_ring_segment(ctx, &metrics,
                          s_cached_today.events[SOLAR_EVENT_GOLDEN_MORNING].local_minutes,
                          s_cached_today.golden_morning_end.local_minutes,
                          APOLLO_RING_GOLDEN,
                          metrics.golden_stroke_width);
    }

    if (s_event_ok(&s_cached_today.events[SOLAR_EVENT_GOLDEN_EVENING]) &&
        s_event_ok(&s_cached_today.events[SOLAR_EVENT_DUSK_GLOW])) {
      s_draw_ring_segment(ctx, &metrics,
                          s_cached_today.events[SOLAR_EVENT_GOLDEN_EVENING].local_minutes,
                          s_cached_today.events[SOLAR_EVENT_DUSK_GLOW].local_minutes,
                          APOLLO_RING_GOLDEN,
                          metrics.golden_stroke_width);
    }
  }
#else
  s_draw_ring_range(ctx, &metrics, 0, MINUTES_PER_DAY, GColorBlack, 2);

  if (s_cached_today.is_polar_day) {
    s_draw_ring_range(ctx, &metrics, 0, MINUTES_PER_DAY, GColorBlack, metrics.stroke_width);
  } else if (!s_cached_today.is_polar_night) {
    if (s_event_ok(&s_cached_today.events[SOLAR_EVENT_SUNRISE]) &&
        s_event_ok(&s_cached_today.events[SOLAR_EVENT_SUNSET])) {
      s_draw_ring_segment(ctx, &metrics,
                          s_cached_today.events[SOLAR_EVENT_SUNRISE].local_minutes,
                          s_cached_today.events[SOLAR_EVENT_SUNSET].local_minutes,
                          GColorBlack,
                          metrics.stroke_width);
    }

    if (s_event_ok(&s_cached_today.events[SOLAR_EVENT_GOLDEN_MORNING]) &&
        s_event_ok(&s_cached_today.golden_morning_end)) {
      s_fill_ring_segment_bw(ctx, &metrics,
                             s_cached_today.events[SOLAR_EVENT_GOLDEN_MORNING].local_minutes,
                             s_cached_today.golden_morning_end.local_minutes,
                             GColorDarkGray,
                             metrics.golden_stroke_width);
    }

    if (s_event_ok(&s_cached_today.events[SOLAR_EVENT_GOLDEN_EVENING]) &&
        s_event_ok(&s_cached_today.events[SOLAR_EVENT_DUSK_GLOW])) {
      s_fill_ring_segment_bw(ctx, &metrics,
                             s_cached_today.events[SOLAR_EVENT_GOLDEN_EVENING].local_minutes,
                             s_cached_today.events[SOLAR_EVENT_DUSK_GLOW].local_minutes,
                             GColorDarkGray,
                             metrics.golden_stroke_width);
    }
  }
#endif

  int marker_radius = metrics.marker_radius;
  if (current_phase == SOLAR_PHASE_GOLDEN) {
    marker_radius += 1;
  }

  GPoint marker = s_point_on_ring(&metrics,
                                  (float)current_min / (float)MINUTES_PER_DAY);
#ifdef PBL_COLOR
  graphics_context_set_fill_color(ctx, APOLLO_SUN_COLOR);
  graphics_fill_circle(ctx, marker, marker_radius);
#else
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_circle(ctx, marker, marker_radius);
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_circle(ctx, marker, marker_radius);
#endif
}

static void s_do_update(const SolarDayResult *today,
                        const SolarDayResult *tomorrow,
                        const Location *loc,
                        int loc_index) {
  int32_t current_min;
  SolarPhase phase;
  const char *quality_text;
#ifdef PBL_COLOR
  GColor event_time_color = GColorWhite;
#endif

  s_cached_today = *today;
  s_cached_tomorrow = *tomorrow;
  s_cached_loc = *loc;
  s_cached_loc_index = loc_index;
  s_has_data = true;

  snprintf(s_buf_location, sizeof(s_buf_location), "%s", loc->name);
  text_layer_set_text(s_tl_location, s_buf_location);

  current_min = s_local_minutes_now(loc->utc_offset_min);
  phase = s_phase_for_minute(today, current_min);
  snprintf(s_buf_status, sizeof(s_buf_status), "%s", s_phase_label(today, current_min));
  text_layer_set_text(s_tl_status, s_buf_status);

  if (today->is_polar_night) {
    char date_buf[16];
    s_format_date(today->next_sunrise_date, date_buf, sizeof(date_buf));
    snprintf(s_buf_event_time, sizeof(s_buf_event_time), "--");
    snprintf(s_buf_event_name, sizeof(s_buf_event_name), "Next Sunrise %s", date_buf);
    snprintf(s_buf_countdown, sizeof(s_buf_countdown), "No sunrise today");
  } else if (today->is_polar_day) {
    char date_buf[16];
    s_format_date(today->next_sunset_date, date_buf, sizeof(date_buf));
    snprintf(s_buf_event_time, sizeof(s_buf_event_time), "--");
    snprintf(s_buf_event_name, sizeof(s_buf_event_name), "Next Sunset %s", date_buf);
    snprintf(s_buf_countdown, sizeof(s_buf_countdown), "No sunset today");
  } else if (phase == SOLAR_PHASE_GOLDEN) {
    bool morning = s_is_morning_golden(today, current_min);
    const SolarEvent *transition = morning ?
      &today->events[SOLAR_EVENT_SUNRISE] :
      &today->events[SOLAR_EVENT_SUNSET];
    const SolarEvent *end = morning ?
      &today->golden_morning_end :
      &today->events[SOLAR_EVENT_DUSK_GLOW];
    const char *transition_label = morning ? "Sunrise" : "Sunset";
    bool before_transition = s_event_ok(transition) &&
                             current_min < transition->local_minutes;

    if (before_transition) {
      int32_t delta = s_minutes_until(current_min, transition->local_minutes, false);
      s_format_duration_minutes(delta, s_buf_event_time, sizeof(s_buf_event_time));
      snprintf(s_buf_event_name, sizeof(s_buf_event_name), "Next %s", transition_label);
      s_format_event_line(transition_label,
                          transition->local_minutes,
                          false,
                          s_buf_countdown,
                          sizeof(s_buf_countdown));
#ifdef PBL_COLOR
      event_time_color = APOLLO_TIME_SUN;
#endif
    } else if (s_event_ok(end)) {
      int32_t delta = s_minutes_until(current_min, end->local_minutes, false);
      s_format_duration_minutes(delta, s_buf_event_time, sizeof(s_buf_event_time));
      snprintf(s_buf_event_name, sizeof(s_buf_event_name), "Golden Hour Ends");
      s_format_event_line("Ends",
                          end->local_minutes,
                          false,
                          s_buf_countdown,
                          sizeof(s_buf_countdown));
#ifdef PBL_COLOR
      event_time_color = APOLLO_TIME_GOLDEN;
#endif
    } else {
      snprintf(s_buf_event_time, sizeof(s_buf_event_time), "--");
      snprintf(s_buf_event_name, sizeof(s_buf_event_name), "Golden Hour");
      snprintf(s_buf_countdown, sizeof(s_buf_countdown), " ");
    }
  } else {
    NextGoldenHour next_golden = s_find_next_golden_hour(today, tomorrow, current_min);
    NextEvent next = s_find_next_event(today, tomorrow, current_min);

    if (next_golden.found) {
      int32_t delta = s_minutes_until(current_min,
                                      next_golden.local_minutes,
                                      next_golden.is_tomorrow);
      s_format_duration_minutes(delta, s_buf_event_time, sizeof(s_buf_event_time));
      snprintf(s_buf_event_name, sizeof(s_buf_event_name), "Next Golden Hour");
#ifdef PBL_COLOR
      event_time_color = APOLLO_TIME_GOLDEN;
#endif
    } else {
      snprintf(s_buf_event_time, sizeof(s_buf_event_time), "--");
      snprintf(s_buf_event_name, sizeof(s_buf_event_name), "No Golden Hour");
    }

    if (next.found) {
      s_format_event_line(next.is_sunrise ? "Sunrise" : "Sunset",
                          next.local_minutes,
                          next.is_tomorrow,
                          s_buf_countdown,
                          sizeof(s_buf_countdown));
    } else {
      snprintf(s_buf_countdown, sizeof(s_buf_countdown), "No solar event");
    }
  }

  text_layer_set_text(s_tl_event_time, s_buf_event_time);
  text_layer_set_text(s_tl_event_name, s_buf_event_name);
  s_set_countdown_text(s_buf_countdown);
  quality_text = s_current_quality_text(today, tomorrow, loc, loc_index, current_min);
  if (quality_text) {
    snprintf(s_buf_quality, sizeof(s_buf_quality), "Quality %s", quality_text);
  } else {
    s_buf_quality[0] = '\0';
  }
  s_set_quality_text(s_buf_quality);

#ifdef PBL_COLOR
  window_set_background_color(s_window, GColorBlack);
  text_layer_set_text_color(s_tl_location, GColorWhite);
  text_layer_set_text_color(s_tl_status, s_status_color_for_phase(phase));
  text_layer_set_text_color(s_tl_event_name, GColorLightGray);
  text_layer_set_text_color(s_tl_event_time, event_time_color);
  text_layer_set_text_color(s_tl_countdown, GColorWhite);
  text_layer_set_text_color(s_tl_quality, GColorLightGray);
#else
  window_set_background_color(s_window, GColorWhite);
  text_layer_set_text_color(s_tl_location, GColorBlack);
  text_layer_set_text_color(s_tl_status, GColorBlack);
  text_layer_set_text_color(s_tl_event_name, GColorBlack);
  text_layer_set_text_color(s_tl_event_time, GColorBlack);
  text_layer_set_text_color(s_tl_countdown, GColorBlack);
  text_layer_set_text_color(s_tl_quality, GColorBlack);
#endif

  if (s_ring_layer) {
    layer_mark_dirty(s_ring_layer);
  }
}

static void prv_window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);
  int w = bounds.size.w;
  const char *location_font_key;
  const char *event_name_font_key;
  const char *event_time_font_key;
  const char *countdown_font_key;

#ifdef PBL_ROUND
  bool large_round = bounds.size.w >= 220 || bounds.size.h >= 220;
  int inset = 22;
  int loc_y = 34;
  int loc_h = 18;
  int status_y = 0;
  int status_h = 0;
  int next_y = 62;
  int next_h = 18;
  int time_y = 82;
  int time_h = 34;
  int countdown_y = 118;
  int countdown_h = 18;
  int quality_h = 18;
  inset = s_scale_from_width(w, 12, 100);
  loc_y = s_scale_from_width(w, 19, 100);
  loc_h = s_scale_from_width(w, 10, 100);
  next_y = s_scale_from_width(w, 34, 100);
  next_h = s_scale_from_width(w, 10, 100);
  time_y = s_scale_from_width(w, 45, 100);
  time_h = s_scale_from_width(w, 19, 100);
  countdown_y = s_scale_from_width(w, 66, 100);
  countdown_h = s_scale_from_width(w, 10, 100);
  quality_h = countdown_h;
  location_font_key = large_round ? FONT_KEY_GOTHIC_18_BOLD : FONT_KEY_GOTHIC_14_BOLD;
  event_name_font_key = large_round ? FONT_KEY_GOTHIC_18_BOLD : FONT_KEY_GOTHIC_14_BOLD;
  event_time_font_key = large_round ? FONT_KEY_GOTHIC_28_BOLD : FONT_KEY_GOTHIC_24_BOLD;
  countdown_font_key = large_round ? FONT_KEY_GOTHIC_18 : FONT_KEY_GOTHIC_14;
#else
  bool large_rect = s_is_large_rect_display(bounds);
  int inset = s_scale_from_width(w, 10, 100);
  int loc_y = s_scale_from_width(w, 17, 100);
  int loc_h = s_scale_from_width(w, 13, 100);
  int status_y = 0;
  int status_h = 0;
  int next_y = s_scale_from_width(w, 38, 100);
  int next_h = s_scale_from_width(w, 11, 100);
  int time_y = s_scale_from_width(w, 53, 100);
  int time_h = s_scale_from_width(w, 24, 100);
  int countdown_y = s_scale_from_width(w, 77, 100);
  int countdown_h = s_scale_from_width(w, 11, 100);
  int quality_h = countdown_h;

  location_font_key = large_rect ? FONT_KEY_GOTHIC_18_BOLD : FONT_KEY_GOTHIC_14_BOLD;
  event_name_font_key = large_rect ? FONT_KEY_GOTHIC_18_BOLD : FONT_KEY_GOTHIC_14_BOLD;
  event_time_font_key = large_rect ? FONT_KEY_GOTHIC_28_BOLD : FONT_KEY_GOTHIC_24_BOLD;
  countdown_font_key = large_rect ? FONT_KEY_GOTHIC_18 : FONT_KEY_GOTHIC_14;
#endif
  int eff_w = w - inset * 2;
  int countdown_h_multi = s_scale_from_width(w, 20, 100);
  if (countdown_h_multi < countdown_h) {
    countdown_h_multi = countdown_h;
  }

#ifdef PBL_COLOR
  window_set_background_color(window, GColorBlack);
#else
  window_set_background_color(window, GColorWhite);
#endif

  s_ring_layer = layer_create(bounds);
  layer_set_update_proc(s_ring_layer, s_ring_update_proc);
  layer_add_child(root, s_ring_layer);

  s_tl_location = text_layer_create(GRect(inset, loc_y, eff_w, loc_h));
  text_layer_set_font(s_tl_location, fonts_get_system_font(location_font_key));
  text_layer_set_text_alignment(s_tl_location, GTextAlignmentCenter);
  text_layer_set_background_color(s_tl_location, GColorClear);
  layer_add_child(root, text_layer_get_layer(s_tl_location));

  s_tl_status = text_layer_create(GRect(inset, status_y, eff_w, status_h));
  text_layer_set_font(s_tl_status, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_alignment(s_tl_status, GTextAlignmentCenter);
  text_layer_set_background_color(s_tl_status, GColorClear);
  layer_set_hidden(text_layer_get_layer(s_tl_status), true);
  layer_add_child(root, text_layer_get_layer(s_tl_status));

  s_tl_event_time = text_layer_create(GRect(inset, time_y, eff_w, time_h));
  text_layer_set_font(s_tl_event_time, fonts_get_system_font(event_time_font_key));
  text_layer_set_text_alignment(s_tl_event_time, GTextAlignmentCenter);
  text_layer_set_background_color(s_tl_event_time, GColorClear);
  layer_add_child(root, text_layer_get_layer(s_tl_event_time));

  s_tl_event_name = text_layer_create(GRect(inset, next_y, eff_w, next_h));
  text_layer_set_font(s_tl_event_name, fonts_get_system_font(event_name_font_key));
  text_layer_set_text_alignment(s_tl_event_name, GTextAlignmentCenter);
  text_layer_set_background_color(s_tl_event_name, GColorClear);
  layer_add_child(root, text_layer_get_layer(s_tl_event_name));

  s_countdown_x = inset;
  s_countdown_y = countdown_y;
  s_countdown_w = eff_w;
  s_countdown_h_single = countdown_h;
  s_countdown_h_multi = countdown_h_multi;
  s_quality_x = inset;
  s_quality_y = countdown_y + countdown_h;
  s_quality_w = eff_w;
  s_quality_h = quality_h;
  s_quality_gap = s_scale_from_width(w, 1, 100);
  s_quality_max_y = bounds.size.h - s_quality_h - s_scale_from_width(w, 1, 100);

  s_tl_countdown = text_layer_create(GRect(inset, countdown_y, eff_w, countdown_h));
  text_layer_set_font(s_tl_countdown, fonts_get_system_font(countdown_font_key));
  text_layer_set_text_alignment(s_tl_countdown, GTextAlignmentCenter);
  text_layer_set_overflow_mode(s_tl_countdown, GTextOverflowModeWordWrap);
  text_layer_set_background_color(s_tl_countdown, GColorClear);
  layer_add_child(root, text_layer_get_layer(s_tl_countdown));

  s_tl_quality = text_layer_create(GRect(inset, s_quality_y, eff_w, quality_h));
  text_layer_set_font(s_tl_quality, fonts_get_system_font(countdown_font_key));
  text_layer_set_text_alignment(s_tl_quality, GTextAlignmentCenter);
  text_layer_set_background_color(s_tl_quality, GColorClear);
  layer_set_hidden(text_layer_get_layer(s_tl_quality), true);
  layer_add_child(root, text_layer_get_layer(s_tl_quality));

  text_layer_set_text(s_tl_location, "---");
  text_layer_set_text(s_tl_status, "---");
  text_layer_set_text(s_tl_event_time, "--");
  text_layer_set_text(s_tl_event_name, "---");
  s_set_countdown_text("");
  s_set_quality_text("");
}

static void prv_window_unload(Window *window) {
  layer_destroy(s_ring_layer);
  s_ring_layer = NULL;
  text_layer_destroy(s_tl_location);
  text_layer_destroy(s_tl_status);
  text_layer_destroy(s_tl_event_name);
  text_layer_destroy(s_tl_event_time);
  text_layer_destroy(s_tl_countdown);
  text_layer_destroy(s_tl_quality);
  s_tl_quality = NULL;
}

Window *main_window_create(void) {
  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = prv_window_load,
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
                        const Location *loc,
                        int loc_index) {
  if (!s_window) {
    return;
  }
  s_do_update(today, tomorrow, loc, loc_index);
}

void main_window_update_countdown(const SolarDayResult *today,
                                  const SolarDayResult *tomorrow,
                                  const Location *loc,
                                  int loc_index) {
  main_window_update(today, tomorrow, loc, loc_index);
}

void main_window_set_quality(const QualityCache *quality) {
  s_cached_quality = *quality;
  s_has_quality = true;
  if (s_has_data && s_window) {
    s_do_update(&s_cached_today, &s_cached_tomorrow, &s_cached_loc, s_cached_loc_index);
  }
}

void main_window_clear_quality(void) {
  memset(&s_cached_quality, 0, sizeof(s_cached_quality));
  s_has_quality = false;
  if (s_window) {
    s_set_quality_text("");
    if (s_has_data) {
      s_do_update(&s_cached_today, &s_cached_tomorrow, &s_cached_loc, s_cached_loc_index);
    }
  }
}
