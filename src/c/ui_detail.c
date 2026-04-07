#include "ui_detail.h"

// -------------------------------------------------------------------------
// Shared state
// -------------------------------------------------------------------------
static Window       *s_det_window;

// Page index: 0=today, 1=tomorrow on rect; 0-3 on round
static int s_page;

#ifndef PBL_ROUND
static Layer        *s_content_layer;
#endif

// Copies of data passed in (so we own the lifetime)
static SolarDayResult s_today;
static SolarDayResult s_tomorrow;
static char           s_loc_name[LOC_NAME_LEN];
static int            s_utc_offset_min;

// -------------------------------------------------------------------------
// Drawing
// -------------------------------------------------------------------------
#define ROW_H         18
#define HEADER_H      22
#define SECTION_GAP    6
#define NAME_COL_W    92
#define TIME_COL_X    96

#ifndef PBL_ROUND
static void s_draw_event_row(GContext *ctx, int y, int w,
                              const SolarEvent *event, SolarEventType type) {
  GFont name_font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  GFont time_font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);

  const char *name = solar_event_name(type);
  char time_buf[16];

  if (event->status == SOLAR_STATUS_OK) {
    int32_t m = event->local_minutes;
    // Clamp to 0-1439 (cross-midnight values)
    m = ((m % 1440) + 1440) % 1440;
    snprintf(time_buf, sizeof(time_buf), "%02d:%02d",
             (int)(m / 60), (int)(m % 60));
  } else if (event->status == SOLAR_STATUS_POLAR_DAY) {
    snprintf(time_buf, sizeof(time_buf), "All day");
  } else if (event->status == SOLAR_STATUS_POLAR_NIGHT) {
    snprintf(time_buf, sizeof(time_buf), "All night");
  } else {
    snprintf(time_buf, sizeof(time_buf), "---");
  }

  graphics_draw_text(ctx, name, name_font,
                     GRect(4, y, NAME_COL_W, ROW_H),
                     GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentLeft, NULL);
  graphics_draw_text(ctx, time_buf, time_font,
                     GRect(TIME_COL_X, y, w - TIME_COL_X - 4, ROW_H),
                     GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentRight, NULL);
}
#endif // !PBL_ROUND

#ifndef PBL_ROUND
static void s_draw_section(GContext *ctx, int y_start, int w,
                            const SolarDayResult *result,
                            const char *header) {
  GFont hdr_font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);

  // Header with separator line
  graphics_draw_text(ctx, header, hdr_font,
                     GRect(4, y_start, w - 8, HEADER_H),
                     GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentLeft, NULL);

  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_draw_line(ctx,
                     GPoint(4, y_start + HEADER_H - 3),
                     GPoint(w - 4, y_start + HEADER_H - 3));

  int y = y_start + HEADER_H;
  for (int i = 0; i < SOLAR_EVENT_COUNT; i++) {
    // Alternate row shading on color devices
#ifdef PBL_COLOR
    if (i % 2 == 0) {
      graphics_context_set_fill_color(ctx, GColorLightGray);
      graphics_fill_rect(ctx, GRect(0, y, w, ROW_H), 0, GCornerNone);
    }
#endif
    s_draw_event_row(ctx, y, w, &result->events[i], (SolarEventType)i);
    y += ROW_H;
  }
}
#endif // !PBL_ROUND

// -------------------------------------------------------------------------
// Rect: single-section paged view (s_page 0=today, 1=tomorrow)
// -------------------------------------------------------------------------
#ifndef PBL_ROUND
static void prv_content_update(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  int w   = b.size.w;

  graphics_context_set_text_color(ctx, GColorBlack);

  const SolarDayResult *result = (s_page == 0) ? &s_today : &s_tomorrow;
  const char *header = (s_page == 0) ? "Today" : "Tomorrow";
  s_draw_section(ctx, 0, w, result, header);
}
#endif // !PBL_ROUND

// -------------------------------------------------------------------------
// Round: paged navigation (UP/DOWN scroll between pages)
// -------------------------------------------------------------------------
#ifdef PBL_ROUND
#define ROUND_EVENTS_PER_PAGE  4
// Page 0: events 0-3 (First Light, Golden Morn, Sunrise, Golden Eve)
// Page 1: events 4-6 (Sunset, Dusk Glow, Last Light)

static TextLayer *s_round_header;
static TextLayer *s_round_rows[SOLAR_EVENT_COUNT];
static char       s_round_row_bufs[SOLAR_EVENT_COUNT][32];
#endif // PBL_ROUND

#ifdef PBL_ROUND
static void prv_round_refresh(void) {
  const SolarDayResult *result = (s_page < 2) ? &s_today : &s_tomorrow;
  int section_page = s_page % 2;  // 0 or 1 within a day

  int start = section_page * ROUND_EVENTS_PER_PAGE;
  int end   = start + ROUND_EVENTS_PER_PAGE;
  if (end > SOLAR_EVENT_COUNT) end = SOLAR_EVENT_COUNT;

  for (int i = 0; i < SOLAR_EVENT_COUNT; i++) {
    bool visible = (i >= start && i < end);
    layer_set_hidden(text_layer_get_layer(s_round_rows[i]), !visible);
    if (!visible) continue;

    const SolarEvent *ev = &result->events[i];
    char time_buf[12];
    if (ev->status == SOLAR_STATUS_OK) {
      int32_t m = ((ev->local_minutes % 1440) + 1440) % 1440;
      snprintf(time_buf, sizeof(time_buf), "%02d:%02d",
               (int)(m / 60), (int)(m % 60));
    } else if (ev->status == SOLAR_STATUS_POLAR_DAY) {
      snprintf(time_buf, sizeof(time_buf), "All day");
    } else if (ev->status == SOLAR_STATUS_POLAR_NIGHT) {
      snprintf(time_buf, sizeof(time_buf), "All night");
    } else {
      snprintf(time_buf, sizeof(time_buf), "---");
    }
    snprintf(s_round_row_bufs[i], sizeof(s_round_row_bufs[i]),
             "%-14s%s", solar_event_name(i), time_buf);
    text_layer_set_text(s_round_rows[i], s_round_row_bufs[i]);
  }

  // Page header
  static char s_hdr_buf[24];
  const char *day_str = (s_page < 2) ? "Today" : "Tomorrow";
  snprintf(s_hdr_buf, sizeof(s_hdr_buf), "%s (%d/2)", day_str, (s_page % 2) + 1);
  text_layer_set_text(s_round_header, s_hdr_buf);
}
#endif // PBL_ROUND

// -------------------------------------------------------------------------
// Click handlers
// -------------------------------------------------------------------------
static void prv_up_click(ClickRecognizerRef r, void *ctx) {
#ifdef PBL_ROUND
  if (s_page > 0) {
    s_page--;
    prv_round_refresh();
  }
#else
  if (s_page > 0) {
    s_page--;
    layer_mark_dirty(s_content_layer);
  } else {
    window_stack_pop(true);
  }
#endif
}

static void prv_down_click(ClickRecognizerRef r, void *ctx) {
#ifdef PBL_ROUND
  if (s_page < 3) {
    s_page++;
    prv_round_refresh();
  }
#else
  if (s_page < 1) {
    s_page++;
    layer_mark_dirty(s_content_layer);
  }
#endif
}

static void prv_select_click(ClickRecognizerRef r, void *ctx) {
  window_stack_pop(true);
}

static void prv_click_config(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_UP,     prv_up_click);
  window_single_click_subscribe(BUTTON_ID_DOWN,   prv_down_click);
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_select_click);
}

// -------------------------------------------------------------------------
// Window lifecycle
// -------------------------------------------------------------------------
static void prv_window_load(Window *window) {
  Layer *root   = window_get_root_layer(window);
  GRect  bounds = layer_get_bounds(root);

  window_set_click_config_provider(window, prv_click_config);

#ifdef PBL_ROUND
  int inset = 16;
  int w     = bounds.size.w;
  int eff_w = w - 2 * inset;
  int y     = inset + 4;

  s_round_header = text_layer_create(GRect(inset, y, eff_w, 20));
  text_layer_set_font(s_round_header, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
  text_layer_set_text_alignment(s_round_header, GTextAlignmentCenter);
  text_layer_set_background_color(s_round_header, GColorClear);
  layer_add_child(root, text_layer_get_layer(s_round_header));
  y += 22;

  for (int i = 0; i < SOLAR_EVENT_COUNT; i++) {
    s_round_rows[i] = text_layer_create(GRect(inset, y + i * ROW_H, eff_w, ROW_H));
    text_layer_set_font(s_round_rows[i], fonts_get_system_font(FONT_KEY_GOTHIC_14));
    text_layer_set_background_color(s_round_rows[i], GColorClear);
    layer_add_child(root, text_layer_get_layer(s_round_rows[i]));
  }

  s_page = 0;
  prv_round_refresh();
#else
  s_content_layer = layer_create(bounds);
  layer_set_update_proc(s_content_layer, prv_content_update);
  layer_add_child(root, s_content_layer);
#endif
}

static void prv_window_unload(Window *window) {
#ifdef PBL_ROUND
  text_layer_destroy(s_round_header);
  for (int i = 0; i < SOLAR_EVENT_COUNT; i++) {
    text_layer_destroy(s_round_rows[i]);
  }
  s_round_header = NULL;
#else
  layer_destroy(s_content_layer);
  s_content_layer = NULL;
#endif
}

// -------------------------------------------------------------------------
// Public API
// -------------------------------------------------------------------------
void detail_window_push(const SolarDayResult *today,
                        const SolarDayResult *tomorrow,
                        const Location *loc) {
  s_today          = *today;
  s_tomorrow       = *tomorrow;
  s_utc_offset_min = loc->utc_offset_min;
  snprintf(s_loc_name, sizeof(s_loc_name), "%s", loc->name);
  s_page = 0;

  if (!s_det_window) {
    s_det_window = window_create();
    window_set_window_handlers(s_det_window, (WindowHandlers){
      .load   = prv_window_load,
      .unload = prv_window_unload,
    });
  }
  window_stack_push(s_det_window, true);
}
