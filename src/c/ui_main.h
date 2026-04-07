#pragma once
#include <pebble.h>
#include "solar.h"
#include "storage.h"

// Create the main window. Does not push it.
Window *main_window_create(void);
void    main_window_destroy(void);

// Refresh all content with new solar data and location.
void main_window_update(const SolarDayResult *today,
                        const SolarDayResult *tomorrow,
                        const Location *loc);

// Fast update: recompute the countdown only (called every minute).
void main_window_update_countdown(const SolarDayResult *today,
                                  const SolarDayResult *tomorrow,
                                  const Location *loc);
