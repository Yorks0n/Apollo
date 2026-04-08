#include "solar.h"
#include <math.h>  // only for fabsf, fmodf

// The ARM math library's sinf/cos/sqrtf use lookup tables accessed via absolute
// addresses, which break in Pebble's PIC app loader.  Use Pebble SDK's own
// sin_lookup/cos_lookup (system call table, always accessible) instead, and
// implement sqrt via fast inverse sqrt (bit-manipulation, no tables).
//
// TRIG_MAX_ANGLE = 65536 units = 360 degrees
// TRIG_MAX_RATIO = 65535 = output scale for unit trig value

#define S_PI  3.14159265f
#define S_D2R (S_PI / 180.0f)
#define S_R2D (180.0f / S_PI)

// sin/cos in degrees via Pebble SDK lookup tables
static float s_sin_d(float deg) {
  int32_t a = (int32_t)(deg * ((float)TRIG_MAX_ANGLE / 360.0f));
  return (float)sin_lookup(a) / (float)TRIG_MAX_RATIO;
}
static float s_cos_d(float deg) {
  int32_t a = (int32_t)(deg * ((float)TRIG_MAX_ANGLE / 360.0f));
  return (float)cos_lookup(a) / (float)TRIG_MAX_RATIO;
}

// sqrt via fast inverse sqrt (Quake III, Q_rsqrt) — no lookup tables needed.
// Argument is always in [0,1] from s_acos; two Newton-Raphson steps give
// ~23 bits of accuracy (sufficient for solar calculations).
static float s_sqrtf(float x) {
  if (x <= 0.0f) return 0.0f;
  union { float f; uint32_t i; } u;
  u.f = x;
  u.i = 0x5f3759dfu - (u.i >> 1);   // initial approx of 1/sqrt(x)
  float y = u.f;
  y = y * (1.5f - 0.5f * x * y * y);  // Newton-Raphson refinement #1
  y = y * (1.5f - 0.5f * x * y * y);  // Newton-Raphson refinement #2
  return x * y;  // x * (1/sqrt(x)) = sqrt(x)
}

// acos via A&S polynomial + s_sqrtf above (no lookup tables).
// Max error ~1.7e-5 rad (~0.001°), sufficient for solar calculations.
static float s_acos(float x) {
  if (x >= 1.0f)  return 0.0f;
  if (x <= -1.0f) return S_PI;
  float negate = (x < 0.0f) ? 1.0f : 0.0f;
  float ax = fabsf(x);
  float ret = (((-0.0187293f * ax + 0.0742610f) * ax) - 0.2121144f) * ax + 1.5707288f;
  ret *= s_sqrtf(1.0f - ax);
  ret = ret - 2.0f * negate * ret;
  return negate * S_PI + ret;
}

static float s_asin(float x) { return (S_PI * 0.5f) - s_acos(x); }

static float s_fmod_pos(float x, float m) {
  float r = fmodf(x, m);
  return (r < 0.0f) ? r + m : r;
}

// Julian Date for Gregorian date at UT midnight.
static float s_julian_date(int year, int month, int day) {
  if (month <= 2) { year--; month += 12; }
  int A = year / 100;
  int B = 2 - A + (A / 4);
  return (float)((int)(365.25f * (year + 4716)))
       + (float)((int)(30.6001f * (month + 1)))
       + (float)day + (float)B - 1524.5f;
}

static void s_jd_to_gregorian(float jd, int *year, int *month, int *day) {
  int Z = (int)(jd + 0.5f);
  int A;
  if (Z < 2299161) {
    A = Z;
  } else {
    int alpha = (int)((Z - 1867216.25f) / 36524.25f);
    A = Z + 1 + alpha - alpha / 4;
  }
  int B = A + 1524;
  int C = (int)((B - 122.1f) / 365.25f);
  int D = (int)(365.25f * C);
  int E = (int)((B - D) / 30.6001f);
  *day   = B - D - (int)(30.6001f * E);
  *month = (E < 14) ? E - 1 : E - 13;
  *year  = (*month > 2) ? C - 4716 : C - 4715;
}

void solar_date_add_days(int *year, int *month, int *day, int delta) {
  float jd = s_julian_date(*year, *month, *day) + (float)delta;
  s_jd_to_gregorian(jd, year, month, day);
}

// -------------------------------------------------------------------------
// Solar position
// -------------------------------------------------------------------------
typedef struct {
  float solar_noon_local;  // minutes from local midnight
  float dec_deg;           // solar declination in DEGREES
} SolarPos;

static SolarPos s_solar_pos(int year, int month, int day,
                             float lon_deg, int utc_offset_min) {
  float JD = s_julian_date(year, month, day) + 0.5f;
  float T  = (JD - 2451545.0f) / 36525.0f;

  // Geometric mean longitude / anomaly (degrees)
  float L0 = s_fmod_pos(280.46646f + 36000.76983f * T + 0.0003032f * T * T, 360.0f);
  float M  = s_fmod_pos(357.52911f + 35999.05029f * T - 0.0001537f * T * T, 360.0f);

  // Equation of centre
  float C = (1.914602f - 0.004817f * T - 0.000014f * T * T) * s_sin_d(M)
           + (0.019993f - 0.000101f * T) * s_sin_d(2.0f * M)
           + 0.000289f * s_sin_d(3.0f * M);

  // Apparent longitude (degrees)
  float omega     = 125.04f - 1934.136f * T;
  float lambda_d  = L0 + C - 0.00569f - 0.00478f * s_sin_d(omega);

  // Obliquity of ecliptic (degrees)
  float eps_d = 23.439291f - 0.013004f * T + 0.00256f * s_cos_d(omega);

  // Declination (degrees via asin in radians, then convert)
  float sin_dec = s_sin_d(eps_d) * s_sin_d(lambda_d);
  SolarPos pos;
  pos.dec_deg = s_asin(sin_dec) * S_R2D;

  // Equation of Time (minutes)
  // y = tan²(eps/2)
  float eps_half = eps_d * 0.5f;
  float sc = s_cos_d(eps_half);
  float y = (sc == 0.0f) ? 0.0f : (s_sin_d(eps_half) / sc);
  y *= y;  // tan²
  float e = 0.016708634f - 0.000042037f * T;

  float EqT = 4.0f * S_R2D * (
    y       * s_sin_d(2.0f * L0)
    - 2.0f * e * s_sin_d(M)
    + 4.0f * e * y * s_sin_d(M) * s_cos_d(2.0f * L0)
    - 0.5f * y * y * s_sin_d(4.0f * L0)
    - 1.25f * e * e * s_sin_d(2.0f * M)
  );

  pos.solar_noon_local = 720.0f - 4.0f * lon_deg - EqT + (float)utc_offset_min;
  return pos;
}

// -------------------------------------------------------------------------
// Rise/set for a given elevation threshold
// -------------------------------------------------------------------------
typedef struct {
  SolarEventStatus status;
  float rise_min;
  float set_min;
} RiseSet;

static RiseSet s_rise_set(float noon, float dec_deg,
                           float lat_deg, float elev_deg) {
  RiseSet rs;
  float denom = s_cos_d(lat_deg) * s_cos_d(dec_deg);
  float cos_ha;
  if (fabsf(denom) < 1e-6f) {
    // Shouldn't happen for realistic lat/dec values
    rs.status = SOLAR_STATUS_POLAR_NIGHT;
    rs.rise_min = rs.set_min = 0;
    return rs;
  }
  cos_ha = (s_sin_d(elev_deg) - s_sin_d(lat_deg) * s_sin_d(dec_deg)) / denom;

  if (cos_ha > 1.0f) {
    rs.status = SOLAR_STATUS_POLAR_NIGHT;
    rs.rise_min = rs.set_min = 0;
  } else if (cos_ha < -1.0f) {
    rs.status = SOLAR_STATUS_POLAR_DAY;
    rs.rise_min = rs.set_min = 0;
  } else {
    rs.status = SOLAR_STATUS_OK;
    float ha_deg = s_acos(cos_ha) * S_R2D;
    rs.rise_min = noon - 4.0f * ha_deg;
    rs.set_min  = noon + 4.0f * ha_deg;
  }
  return rs;
}

static void s_fill_event(SolarEvent *ev, SolarEventStatus status, float minutes) {
  ev->status = status;
  if (status == SOLAR_STATUS_OK) {
    ev->local_minutes = (int32_t)minutes;
  }
}

static int32_t s_find_next_occurrence(int year, int month, int day,
                                       float lat_deg, float lon_deg,
                                       int utc_offset_min, int max_days) {
  int y = year, mo = month, d = day;
  for (int i = 0; i < max_days; i++) {
    solar_date_add_days(&y, &mo, &d, 1);
    SolarPos pos = s_solar_pos(y, mo, d, lon_deg, utc_offset_min);
    RiseSet rs   = s_rise_set(pos.solar_noon_local, pos.dec_deg,
                               lat_deg, SOLAR_THRESH_SUNRISE);
    if (rs.status == SOLAR_STATUS_OK) {
      return (int32_t)(y * 10000 + mo * 100 + d);
    }
  }
  return 0;
}

// -------------------------------------------------------------------------
// Public API
// -------------------------------------------------------------------------
SolarDayResult solar_calc(int year, int month, int day,
                           double lat_deg, double lon_deg,
                           int utc_offset_min) {
  SolarDayResult result;
  memset(&result, 0, sizeof(result));

  float flat   = (float)lat_deg;
  float flon   = (float)lon_deg;
  SolarPos pos = s_solar_pos(year, month, day, flon, utc_offset_min);

  RiseSet astro   = s_rise_set(pos.solar_noon_local, pos.dec_deg, flat, SOLAR_THRESH_ASTRO_TWILIGHT);
  RiseSet golden  = s_rise_set(pos.solar_noon_local, pos.dec_deg, flat, SOLAR_THRESH_GOLDEN_HOUR);
  RiseSet sr      = s_rise_set(pos.solar_noon_local, pos.dec_deg, flat, SOLAR_THRESH_SUNRISE);
  RiseSet gh_peak = s_rise_set(pos.solar_noon_local, pos.dec_deg, flat, SOLAR_THRESH_GOLDEN_PEAK);

  s_fill_event(&result.events[SOLAR_EVENT_FIRST_LIGHT],    astro.status,   astro.rise_min);
  s_fill_event(&result.events[SOLAR_EVENT_GOLDEN_MORNING], golden.status,  golden.rise_min);
  s_fill_event(&result.events[SOLAR_EVENT_SUNRISE],        sr.status,      sr.rise_min);

  if (gh_peak.status == SOLAR_STATUS_OK) {
    s_fill_event(&result.golden_morning_end, SOLAR_STATUS_OK, gh_peak.rise_min);
    s_fill_event(&result.events[SOLAR_EVENT_GOLDEN_EVENING], SOLAR_STATUS_OK, gh_peak.set_min);
  } else {
    result.golden_morning_end.status = gh_peak.status;
    result.events[SOLAR_EVENT_GOLDEN_EVENING].status = gh_peak.status;
  }

  s_fill_event(&result.events[SOLAR_EVENT_SUNSET],     sr.status,    sr.set_min);
  s_fill_event(&result.events[SOLAR_EVENT_DUSK_GLOW],  golden.status, golden.set_min);
  s_fill_event(&result.events[SOLAR_EVENT_LAST_LIGHT], astro.status,  astro.set_min);

  result.is_polar_day   = (sr.status == SOLAR_STATUS_POLAR_DAY);
  result.is_polar_night = (sr.status == SOLAR_STATUS_POLAR_NIGHT);

  if (result.is_polar_night) {
    result.next_sunrise_date = s_find_next_occurrence(
      year, month, day, flat, flon, utc_offset_min, 200);
  } else if (result.is_polar_day) {
    result.next_sunset_date = s_find_next_occurrence(
      year, month, day, flat, flon, utc_offset_min, 200);
  }

  return result;
}

void solar_local_date(time_t utc_time, int utc_offset_min,
                      int *year, int *month, int *day) {
  time_t local = utc_time + (time_t)utc_offset_min * 60;
  struct tm *t = gmtime(&local);
  *year  = t->tm_year + 1900;
  *month = t->tm_mon + 1;
  *day   = t->tm_mday;
}

const char *solar_event_name(SolarEventType event) {
  switch (event) {
    case SOLAR_EVENT_FIRST_LIGHT:    return "First Light";
    case SOLAR_EVENT_GOLDEN_MORNING: return "Morn. Golden Hr";
    case SOLAR_EVENT_SUNRISE:        return "Sunrise";
    case SOLAR_EVENT_GOLDEN_EVENING: return "Eve. Golden Hr";
    case SOLAR_EVENT_SUNSET:         return "Sunset";
    case SOLAR_EVENT_DUSK_GLOW:      return "Dusk Glow";
    case SOLAR_EVENT_LAST_LIGHT:     return "Last Light";
    default:                          return "---";
  }
}
