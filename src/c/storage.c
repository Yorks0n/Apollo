#include "storage.h"

// Block A: 2 bytes metadata + 6 locations = 206 bytes  (<= 256 limit)
typedef struct __attribute__((packed)) {
  uint8_t  count;
  uint8_t  selected_index;
  Location locs[6];
} StorageBlockA;

// Block B: 6 locations = 204 bytes  (<= 256 limit)
typedef struct __attribute__((packed)) {
  Location locs[6];
} StorageBlockB;

void storage_load(LocationList *out) {
  memset(out, 0, sizeof(LocationList));

  StorageBlockA a;
  StorageBlockB b;
  memset(&a, 0, sizeof(a));
  memset(&b, 0, sizeof(b));

  if (persist_exists(STORAGE_KEY_A)) {
    persist_read_data(STORAGE_KEY_A, &a, sizeof(StorageBlockA));
  }
  if (persist_exists(STORAGE_KEY_B)) {
    persist_read_data(STORAGE_KEY_B, &b, sizeof(StorageBlockB));
  }

  out->count = (a.count > MAX_LOCATIONS) ? MAX_LOCATIONS : a.count;
  out->selected_index = a.selected_index;
  if (out->count > 0 && out->selected_index >= out->count) {
    out->selected_index = 0;
  }

  int lo_count = (out->count < 6) ? out->count : 6;
  memcpy(&out->locs[0], &a.locs[0], (size_t)lo_count * sizeof(Location));

  if (out->count > 6) {
    int hi_count = out->count - 6;
    memcpy(&out->locs[6], &b.locs[0], (size_t)hi_count * sizeof(Location));
  }
}

void storage_save(const LocationList *list) {
  StorageBlockA a;
  StorageBlockB b;
  memset(&a, 0, sizeof(a));
  memset(&b, 0, sizeof(b));

  a.count          = list->count;
  a.selected_index = list->selected_index;

  int lo_count = (list->count < 6) ? list->count : 6;
  memcpy(&a.locs[0], &list->locs[0], (size_t)lo_count * sizeof(Location));

  if (list->count > 6) {
    int hi_count = list->count - 6;
    memcpy(&b.locs[0], &list->locs[6], (size_t)hi_count * sizeof(Location));
  }

  persist_write_data(STORAGE_KEY_A, &a, sizeof(StorageBlockA));
  persist_write_data(STORAGE_KEY_B, &b, sizeof(StorageBlockB));
}
