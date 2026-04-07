#pragma once
#include <pebble.h>

// Solar elevation angle thresholds (degrees)
#define SOLAR_THRESH_ASTRO_TWILIGHT  (-6.0f)
#define SOLAR_THRESH_GOLDEN_HOUR     (-4.0f)
#define SOLAR_THRESH_SUNRISE         (-0.833f)
#define SOLAR_THRESH_GOLDEN_PEAK     (6.0f)

typedef enum {
  SOLAR_EVENT_FIRST_LIGHT = 0,   // Astronomical twilight start  (-18° rising)
  SOLAR_EVENT_GOLDEN_MORNING,    // Morning golden hour start    (-4° rising)
  SOLAR_EVENT_SUNRISE,           // Sunrise                      (-0.833° rising)
  SOLAR_EVENT_GOLDEN_EVENING,    // Evening golden hour start    (+6° setting)
  SOLAR_EVENT_SUNSET,            // Sunset                       (-0.833° setting)
  SOLAR_EVENT_DUSK_GLOW,         // Evening golden hour end      (-4° setting)
  SOLAR_EVENT_LAST_LIGHT,        // Astronomical twilight end    (-18° setting)
  SOLAR_EVENT_COUNT
} SolarEventType;

typedef enum {
  SOLAR_STATUS_OK,           // Event occurs today; local_minutes is valid
  SOLAR_STATUS_POLAR_DAY,    // Sun stays above this threshold all day
  SOLAR_STATUS_POLAR_NIGHT,  // Sun stays below this threshold all day
  SOLAR_STATUS_NO_EVENT      // Event falls outside today's window
} SolarEventStatus;

typedef struct {
  SolarEventStatus status;
  int32_t          local_minutes;  // Minutes from local midnight (valid if OK)
} SolarEvent;

typedef struct {
  SolarEvent events[SOLAR_EVENT_COUNT];
  bool       is_polar_day;
  bool       is_polar_night;
  // For polar conditions: YYYYMMDD of the next day when sunrise/sunset exists.
  // 0 = not searched / not applicable.
  int32_t    next_sunrise_date;
  int32_t    next_sunset_date;
} SolarDayResult;

// Calculate solar events for a given LOCAL date at the given location.
// lat_deg, lon_deg: decimal degrees. utc_offset_min: e.g. +540 for JST.
SolarDayResult solar_calc(int year, int month, int day,
                          double lat_deg, double lon_deg,
                          int utc_offset_min);

// Derive the local calendar date at a given UTC offset from a UTC timestamp.
void solar_local_date(time_t utc_time, int utc_offset_min,
                      int *year, int *month, int *day);

// Add delta calendar days to a date (in-place).
void solar_date_add_days(int *year, int *month, int *day, int delta);

// Short display name for a solar event type.
const char *solar_event_name(SolarEventType event);
