#pragma once
#include <pebble.h>
#include "solar.h"
#include "storage.h"

// Push the detail window showing the full event list for today and tomorrow.
void detail_window_push(const SolarDayResult *today,
                        const SolarDayResult *tomorrow,
                        const Location *loc);
