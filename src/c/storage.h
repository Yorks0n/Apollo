#pragma once
#include <pebble.h>

#define MAX_LOCATIONS   8
#define LOC_NAME_LEN    24

// Pebble persist_write_data max is 256 bytes per key.
// We split into two keys: A holds metadata + locs[0..5], B holds locs[6..11].
#define STORAGE_KEY_A   1
#define STORAGE_KEY_B   2

typedef struct __attribute__((packed)) {
  char    name[LOC_NAME_LEN]; // null-terminated display name
  int32_t lat_e6;             // latitude  × 1,000,000
  int32_t lon_e6;             // longitude × 1,000,000
  int16_t utc_offset_min;     // UTC offset in minutes (e.g. +540 = JST)
} Location;
// sizeof(Location) = 24 + 4 + 4 + 2 = 34 bytes

typedef struct {
  uint8_t  count;
  uint8_t  selected_index;
  Location locs[MAX_LOCATIONS];
} LocationList;

// Load the location list from persistent storage.
// Populates *out; clears it first. Safe to call if nothing is stored.
void storage_load(LocationList *out);

// Write the location list to persistent storage.
void storage_save(const LocationList *list);
