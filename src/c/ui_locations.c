#include "ui_locations.h"

static Window               *s_loc_window;
static SimpleMenuLayer      *s_menu_layer;
static SimpleMenuSection     s_sections[1];
static SimpleMenuItem        s_items[MAX_LOCATIONS];
static LocationSelectedCallback s_callback;
static int                   s_current_selected;

static void prv_item_select(int index, void *ctx) {
  s_current_selected = index;
  if (s_callback) {
    s_callback(index);
  }
  window_stack_pop(true);
}

static void prv_window_load(Window *window) {
  Layer *root   = window_get_root_layer(window);
  GRect  bounds = layer_get_bounds(root);
  s_menu_layer = simple_menu_layer_create(bounds, window,
                                          s_sections, 1, NULL);
  layer_add_child(root, simple_menu_layer_get_layer(s_menu_layer));
  simple_menu_layer_set_selected_index(s_menu_layer, s_current_selected, false);
}

static void prv_window_unload(Window *window) {
  simple_menu_layer_destroy(s_menu_layer);
  s_menu_layer = NULL;
}

void locations_window_push(const LocationList *list,
                           int selected,
                           LocationSelectedCallback callback) {
  s_callback         = callback;
  s_current_selected = (selected >= 0 && selected < list->count) ? selected : 0;
  int count          = (list->count < MAX_LOCATIONS) ? list->count : MAX_LOCATIONS;

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

  if (!s_loc_window) {
    s_loc_window = window_create();
    window_set_window_handlers(s_loc_window, (WindowHandlers){
      .load   = prv_window_load,
      .unload = prv_window_unload,
    });
  }
  window_stack_push(s_loc_window, true);
}
