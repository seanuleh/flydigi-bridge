#pragma once
// Persistent bridge configuration (slot table + player order), kept in internal flash (LittleFS).
#include <stdint.h>

#define NSLOT 2

struct SlotCfg {
  uint8_t used;        // 0 = empty slot (radio pipe disabled)
  uint8_t kind;        // 1 = learned (address from an official dongle's pairing); 2 = paired (ours, future)
  uint8_t addr[5];     // on-air (MSB-first) address
  char name[16];       // NUL-terminated
};

struct Config {
  SlotCfg slot[NSLOT];  // slot n = radio pipe n
  uint8_t order[NSLOT]; // player priority: order[0] is player 1 when connected (see docs/firmware.md)
  uint8_t channel;      // last channel that worked well (0 = none yet); added in config version 2
};

extern Config cfg;

// Load from flash; on a missing or corrupt file, fill the defaults (red + blue) and save them.
// Returns true if the file was loaded, false if defaults were written.
bool store_load(void);
// Write cfg to flash. Stalls the CPU for tens of ms: stop the radio first (see store.cpp).
bool store_save(void);
void store_defaults(void);
