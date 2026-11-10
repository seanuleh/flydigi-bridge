// Config file in internal flash: magic, version, length, payload, CRC-32.
// Flash erase/write halts the CPU (about 85 ms per page erase), so callers only save on explicit config
// commands, never during normal running, and stop the radio around it so controllers just retransmit.
#include <Arduino.h>
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>
#include "store.h"

using namespace Adafruit_LittleFS_Namespace;

#define CFG_FILE "/fdbridge.cfg"
#define CFG_MAGIC 0x46444231u   // "FDB1"
#define CFG_VERSION 1

Config cfg;

struct Header { uint32_t magic; uint16_t version, len; };

static uint32_t crc32(const uint8_t *p, size_t n) {
  uint32_t c = 0xFFFFFFFF;
  while (n--) { c ^= *p++; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320 & -(c & 1)); }
  return ~c;
}

void store_defaults(void) {
  memset(&cfg, 0, sizeof cfg);
  // The two controllers paired with the official dongles (were hardcoded before the slot table existed).
  static const uint8_t red[5] = {0x69, 0xcc, 0x96, 0x6c, 0xee}, blue[5] = {0x66, 0xcc, 0x69, 0xc6, 0xee};
  const uint8_t *a[2] = {red, blue};
  const char *nm[2] = {"Red", "Blue"};
  for (int i = 0; i < 2; i++) {
    cfg.slot[i].used = 1; cfg.slot[i].kind = 1;
    memcpy(cfg.slot[i].addr, a[i], 5); strcpy(cfg.slot[i].name, nm[i]);
  }
  cfg.order[0] = 1; cfg.order[1] = 0;   // blue is player 1 when both are connected
}

static bool valid_order(void) {
  return NSLOT == 2 ? (cfg.order[0] < NSLOT && cfg.order[1] < NSLOT && cfg.order[0] != cfg.order[1]) : true;
}

bool store_load(void) {
  InternalFS.begin();
  File f(InternalFS);
  if (f.open(CFG_FILE, FILE_O_READ)) {
    Header h; Config c; uint32_t crc;
    bool ok = f.read(&h, sizeof h) == sizeof h && h.magic == CFG_MAGIC && h.version == CFG_VERSION &&
              h.len == sizeof c && f.read(&c, sizeof c) == sizeof c && f.read(&crc, 4) == 4 &&
              crc == crc32((const uint8_t *)&c, sizeof c);
    f.close();
    if (ok) {
      cfg = c;
      for (int i = 0; i < NSLOT; i++) cfg.slot[i].name[sizeof cfg.slot[i].name - 1] = 0;
      if (valid_order()) return true;
    }
  }
  store_defaults();
  store_save();
  return false;
}

bool store_save(void) {
  InternalFS.remove(CFG_FILE);
  File f(InternalFS);
  if (!f.open(CFG_FILE, FILE_O_WRITE)) return false;
  Header h = {CFG_MAGIC, CFG_VERSION, sizeof(Config)};
  uint32_t crc = crc32((const uint8_t *)&cfg, sizeof cfg);
  bool ok = f.write((const uint8_t *)&h, sizeof h) == sizeof h &&
            f.write((const uint8_t *)&cfg, sizeof cfg) == sizeof cfg && f.write((const uint8_t *)&crc, 4) == 4;
  f.close();
  return ok;
}
