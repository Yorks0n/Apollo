#pragma once
#include <pebble.h>
#include "storage.h"

typedef void (*LocationSelectedCallback)(int index);

// Create and push the location-picker window.
// list: current location list.
// selected: currently selected index (for checkmark rendering).
// callback: called with the newly chosen index on SELECT.
void locations_window_push(const LocationList *list,
                           int selected,
                           LocationSelectedCallback callback);
