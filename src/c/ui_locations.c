#include "ui_locations.h"

static Window               *s_loc_window;
static LocationList          s_list_copy;
static int                   s_item_count;
#ifdef PBL_ROUND
static MenuLayer            *s_menu_layer;
#else
static SimpleMenuLayer      *s_menu_layer;
static SimpleMenuSection     s_sections[1];
static SimpleMenuItem        s_items[MAX_LOCATIONS];
#endif
static LocationSelectedCallback s_callback;
static int                   s_current_selected;

static void prv_item_select(int index, void *ctx) {
  s_current_selected = index;
  if (s_callback) {
    s_callback(index);
  }
  window_stack_pop(true);
}

#ifdef PBL_ROUND
static uint16_t prv_get_num_sections(MenuLayer *menu_layer, void *ctx) {
  return 1;
}

static uint16_t prv_get_num_rows(MenuLayer *menu_layer, uint16_t section_index, void *ctx) {
  return s_item_count;
}

static int16_t prv_get_cell_height(MenuLayer *menu_layer, MenuIndex *cell_index, void *ctx) {
  return menu_layer_is_index_selected(menu_layer, cell_index)
           ? MENU_CELL_ROUND_FOCUSED_SHORT_CELL_HEIGHT
           : MENU_CELL_ROUND_UNFOCUSED_SHORT_CELL_HEIGHT;
}

static int16_t prv_get_header_height(MenuLayer *menu_layer, uint16_t section_index, void *ctx) {
  return 22;
}

static void prv_draw_centered_text(GContext *ctx,
                                   const Layer *cell_layer,
                                   const char *text,
                                   GFont font) {
  GRect bounds = layer_get_bounds(cell_layer);
  GSize size = graphics_text_layout_get_content_size(text,
                                                     font,
                                                     bounds,
                                                     GTextOverflowModeTrailingEllipsis,
                                                     GTextAlignmentCenter);
  int y = (bounds.size.h - size.h) / 2;
  graphics_draw_text(ctx, text, font,
                     GRect(0, y, bounds.size.w, size.h + 2),
                     GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentCenter, NULL);
}

static void prv_draw_header(GContext *ctx,
                            const Layer *cell_layer,
                            uint16_t section_index,
                            void *ctx_data) {
  prv_draw_centered_text(ctx, cell_layer, "Locations",
                         fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
}

static void prv_draw_row(GContext *ctx,
                         const Layer *cell_layer,
                         MenuIndex *cell_index,
                         void *ctx_data) {
  bool highlighted = menu_cell_layer_is_highlighted(cell_layer);
  GFont font = fonts_get_system_font(highlighted ? FONT_KEY_GOTHIC_24_BOLD
                                                 : FONT_KEY_GOTHIC_18_BOLD);
  prv_draw_centered_text(ctx, cell_layer, s_list_copy.locs[cell_index->row].name, font);
}

static void prv_select_click(MenuLayer *menu_layer, MenuIndex *cell_index, void *ctx) {
  prv_item_select(cell_index->row, ctx);
}

static const MenuLayerCallbacks s_menu_callbacks = {
  .get_num_sections = prv_get_num_sections,
  .get_num_rows = prv_get_num_rows,
  .get_cell_height = prv_get_cell_height,
  .get_header_height = prv_get_header_height,
  .draw_row = prv_draw_row,
  .draw_header = prv_draw_header,
  .select_click = prv_select_click,
};
#endif

static void prv_window_load(Window *window) {
  Layer *root   = window_get_root_layer(window);
  GRect  bounds = layer_get_bounds(root);
#ifdef PBL_ROUND
  s_menu_layer = menu_layer_create(bounds);
  menu_layer_set_callbacks(s_menu_layer, NULL, s_menu_callbacks);
  menu_layer_set_click_config_onto_window(s_menu_layer, window);
  menu_layer_set_center_focused(s_menu_layer, true);
  layer_add_child(root, menu_layer_get_layer(s_menu_layer));
  menu_layer_set_selected_index(s_menu_layer,
                                (MenuIndex){ .section = 0, .row = s_current_selected },
                                MenuRowAlignCenter, false);
#else
  s_menu_layer = simple_menu_layer_create(bounds, window,
                                          s_sections, 1, NULL);
  layer_add_child(root, simple_menu_layer_get_layer(s_menu_layer));
  simple_menu_layer_set_selected_index(s_menu_layer, s_current_selected, false);
#endif
}

static void prv_window_unload(Window *window) {
#ifdef PBL_ROUND
  menu_layer_destroy(s_menu_layer);
#else
  simple_menu_layer_destroy(s_menu_layer);
#endif
  s_menu_layer = NULL;
}

void locations_window_push(const LocationList *list,
                           int selected,
                           LocationSelectedCallback callback) {
  s_callback         = callback;
  s_list_copy        = *list;
  int count          = (list->count < MAX_LOCATIONS) ? list->count : MAX_LOCATIONS;
  s_item_count       = count;
  s_current_selected = (selected >= 0 && selected < count) ? selected : 0;

#ifndef PBL_ROUND
  for (int i = 0; i < count; i++) {
    // Point directly into the static location list — no copy needed.
    s_items[i] = (SimpleMenuItem){
      .title    = list->locs[i].name,
      .callback = prv_item_select,
    };
  }

  s_sections[0] = (SimpleMenuSection){
    .title     = "Locations",
    .items     = s_items,
    .num_items = count,
  };
#endif

  if (!s_loc_window) {
    s_loc_window = window_create();
    window_set_window_handlers(s_loc_window, (WindowHandlers){
      .load   = prv_window_load,
      .unload = prv_window_unload,
    });
  }
  window_stack_push(s_loc_window, true);
}
