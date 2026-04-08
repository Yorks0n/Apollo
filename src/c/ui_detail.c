#include "ui_detail.h"

// -------------------------------------------------------------------------
// Shared state
// -------------------------------------------------------------------------
static Window       *s_det_window;

// Page index: 0=today, 1=tomorrow
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
#ifndef PBL_ROUND
typedef struct {
  int   row_h;
  int   header_h;
  int   left_pad;
  int   right_pad;
  int   name_col_w;
  int   time_col_x;
  GFont name_font;
  GFont time_font;
  GFont header_font;
} DetailRectLayout;

static bool s_is_large_rect_display(GRect bounds) {
  return bounds.size.w >= 180 || bounds.size.h >= 200;
}

static DetailRectLayout s_get_rect_layout(GRect bounds) {
  DetailRectLayout layout;
  bool large = s_is_large_rect_display(bounds);
  int time_col_w = large ? 68 : 44;

  layout.row_h = large ? 22 : 18;
  layout.header_h = large ? 28 : 22;
  layout.left_pad = large ? 8 : 4;
  layout.right_pad = large ? 8 : 4;
  layout.time_col_x = bounds.size.w - layout.right_pad - time_col_w;
  layout.name_col_w = layout.time_col_x - layout.left_pad - 4;
  layout.name_font = fonts_get_system_font(large ? FONT_KEY_GOTHIC_18 : FONT_KEY_GOTHIC_14);
  layout.time_font = fonts_get_system_font(large ? FONT_KEY_GOTHIC_18_BOLD : FONT_KEY_GOTHIC_14_BOLD);
  layout.header_font = fonts_get_system_font(large ? FONT_KEY_GOTHIC_18_BOLD : FONT_KEY_GOTHIC_14_BOLD);
  return layout;
}

static void s_draw_event_row(GContext *ctx, int y, int w,
                             const SolarEvent *event, SolarEventType type,
                             const DetailRectLayout *layout) {

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

  graphics_draw_text(ctx, name, layout->name_font,
                     GRect(layout->left_pad, y, layout->name_col_w, layout->row_h),
                     GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentLeft, NULL);
  graphics_draw_text(ctx, time_buf, layout->time_font,
                     GRect(layout->time_col_x, y, w - layout->time_col_x - layout->right_pad, layout->row_h),
                     GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentRight, NULL);
}
#endif // !PBL_ROUND

#ifndef PBL_ROUND
static void s_draw_section(GContext *ctx, int y_start, int w,
                           const SolarDayResult *result,
                           const char *header,
                           const DetailRectLayout *layout) {
  // Header with separator line
  graphics_draw_text(ctx, header, layout->header_font,
                     GRect(layout->left_pad, y_start, w - layout->left_pad - layout->right_pad, layout->header_h),
                     GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentLeft, NULL);

  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_draw_line(ctx,
                     GPoint(layout->left_pad, y_start + layout->header_h - 3),
                     GPoint(w - layout->right_pad, y_start + layout->header_h - 3));

  int y = y_start + layout->header_h;
  for (int i = 0; i < SOLAR_EVENT_COUNT; i++) {
    // Alternate row shading on color devices
#ifdef PBL_COLOR
    if (i % 2 == 0) {
      graphics_context_set_fill_color(ctx, GColorLightGray);
      graphics_fill_rect(ctx, GRect(0, y, w, layout->row_h), 0, GCornerNone);
    }
#endif
    s_draw_event_row(ctx, y, w, &result->events[i], (SolarEventType)i, layout);
    y += layout->row_h;
  }
}
#endif // !PBL_ROUND

// -------------------------------------------------------------------------
// Rect: single-section paged view (s_page 0=today, 1=tomorrow)
// -------------------------------------------------------------------------
#ifndef PBL_ROUND
static void prv_content_update(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  int w = b.size.w;
  DetailRectLayout layout = s_get_rect_layout(b);
  int content_h = layout.header_h + layout.row_h * SOLAR_EVENT_COUNT;
  int y_start = s_is_large_rect_display(b) ? (b.size.h - content_h) / 2 : 0;
  if (y_start < 0) {
    y_start = 0;
  }

  graphics_context_set_text_color(ctx, GColorBlack);

  const SolarDayResult *result = (s_page == 0) ? &s_today : &s_tomorrow;
  const char *header = (s_page == 0) ? "Today" : "Tomorrow";
  s_draw_section(ctx, y_start, w, result, header, &layout);
}
#endif // !PBL_ROUND

// -------------------------------------------------------------------------
// Round: one page per day, centered content
// -------------------------------------------------------------------------
#ifdef PBL_ROUND
static TextLayer *s_round_header;
static TextLayer *s_round_rows[SOLAR_EVENT_COUNT];
static char       s_round_row_bufs[SOLAR_EVENT_COUNT][32];
#endif // PBL_ROUND

#ifdef PBL_ROUND
static void prv_round_refresh(void) {
  const SolarDayResult *result = (s_page == 0) ? &s_today : &s_tomorrow;

  for (int i = 0; i < SOLAR_EVENT_COUNT; i++) {
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
             "%s %s", solar_event_name(i), time_buf);
    text_layer_set_text(s_round_rows[i], s_round_row_bufs[i]);
  }

  static char s_hdr_buf[24];
  const char *day_str = (s_page == 0) ? "Today" : "Tomorrow";
  snprintf(s_hdr_buf, sizeof(s_hdr_buf), "%s", day_str);
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
  if (s_page < 1) {
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
  bool large_round = bounds.size.w >= 220 || bounds.size.h >= 220;
  int inset = large_round ? 28 : 16;
  int header_h = large_round ? 28 : 22;
  int row_h = large_round ? 22 : 18;
  int header_gap = large_round ? 10 : 8;
  int w = bounds.size.w;
  int eff_w = w - 2 * inset;
  int content_h = header_h + header_gap + row_h * SOLAR_EVENT_COUNT;
  int y = (bounds.size.h - content_h) / 2;
  if (y < (large_round ? 18 : 12)) {
    y = large_round ? 18 : 12;
  }

  s_round_header = text_layer_create(GRect(inset, y, eff_w, header_h));
  text_layer_set_font(s_round_header,
                      fonts_get_system_font(large_round ? FONT_KEY_GOTHIC_24_BOLD
                                                        : FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_text_alignment(s_round_header, GTextAlignmentCenter);
  text_layer_set_background_color(s_round_header, GColorClear);
  layer_add_child(root, text_layer_get_layer(s_round_header));
  y += header_h + header_gap;

  for (int i = 0; i < SOLAR_EVENT_COUNT; i++) {
    s_round_rows[i] = text_layer_create(GRect(inset, y + i * row_h, eff_w, row_h));
    text_layer_set_font(s_round_rows[i],
                        fonts_get_system_font(large_round ? FONT_KEY_GOTHIC_18
                                                          : FONT_KEY_GOTHIC_14));
    text_layer_set_text_alignment(s_round_rows[i], GTextAlignmentCenter);
    text_layer_set_overflow_mode(s_round_rows[i], GTextOverflowModeTrailingEllipsis);
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
