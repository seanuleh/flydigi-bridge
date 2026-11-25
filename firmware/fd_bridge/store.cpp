// Config file in internal flash: magic, version, length, payload, CRC-32.
// Flash erase/write halts the CPU (about 85 ms per page erase), so callers only save on explicit config
// commands, never during normal running, and stop the radio around it so controllers just retransmit.
#include <Arduino.h>
#include <stddef.h>
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>
#include "store.h"

using namespace Adafruit_LittleFS_Namespace;

#ifdef FD_FS_ADDR
// Boards with Nordic's bootloader (HolyIoT 21017): it starts at 0xE0000, so the core's InternalFS area
// (0xED000-0xF4000) lies inside it. Same LittleFS layout, moved to FD_FS_ADDR: the pages just below the
// bootloader that Nordic DFU keeps for app data (NRF_DFU_APP_DATA_AREA_SIZE, 3 pages by default).
#include "flash/flash_nrf5x.h"
#define FS_BLOCK 128
#define FS_SIZE (FD_FS_PAGES * FLASH_NRF52_PAGE_SIZE)
static_assert(FD_FS_ADDR + FS_SIZE <= 0xE0000, "config area overlaps the Nordic bootloader");
static int fs_read(const struct lfs_config *, lfs_block_t b, lfs_off_t off, void *buf, lfs_size_t n) {
  return flash_nrf5x_read(buf, FD_FS_ADDR + b * FS_BLOCK + off, n) > 0 ? 0 : -1;
}
static int fs_prog(const struct lfs_config *, lfs_block_t b, lfs_off_t off, const void *buf, lfs_size_t n) {
  return flash_nrf5x_write(FD_FS_ADDR + b * FS_BLOCK + off, buf, n) ? 0 : -1;
}
static int fs_erase(const struct lfs_config *, lfs_block_t b) {
  for (int i = 0; i < FS_BLOCK; i++) flash_nrf5x_write8(FD_FS_ADDR + b * FS_BLOCK + i, 0xFF);
  return 0;
}
static int fs_sync(const struct lfs_config *) { flash_nrf5x_flush(); return 0; }
static struct lfs_config fs_cfg = {
  .context = NULL, .read = fs_read, .prog = fs_prog, .erase = fs_erase, .sync = fs_sync,
  .read_size = FS_BLOCK, .prog_size = FS_BLOCK, .block_size = FS_BLOCK, .block_count = FS_SIZE / FS_BLOCK,
  .lookahead = 128, .read_buffer = NULL, .prog_buffer = NULL, .lookahead_buffer = NULL, .file_buffer = NULL};
class BridgeFS : public Adafruit_LittleFS {
 public:
  BridgeFS() : Adafruit_LittleFS(&fs_cfg) {}
  bool begin() {   // as InternalFileSystem::begin: on a failed mount, erase, format, mount again
    if (Adafruit_LittleFS::begin()) return true;
    for (uint32_t a = FD_FS_ADDR; a < FD_FS_ADDR + FS_SIZE; a += FLASH_NRF52_PAGE_SIZE) flash_nrf5x_erase(a);
    format();
    return Adafruit_LittleFS::begin();
  }
};
static BridgeFS FS;
#else
#define FS InternalFS
#endif

#define CFG_FILE "/fdbridge.cfg"
#define CFG_MAGIC 0x46444231u   // "FDB1"
#define CFG_VERSION 2
#define CFG_V1_LEN offsetof(Config, channel)   // version 1 had no channel field

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
  FS.begin();
  File f(FS);
  if (f.open(CFG_FILE, FILE_O_READ)) {
    Header h; Config c; uint32_t crc;
    memset(&c, 0, sizeof c);
    bool ok = f.read(&h, sizeof h) == sizeof h && h.magic == CFG_MAGIC &&
              ((h.version == CFG_VERSION && h.len == sizeof c) || (h.version == 1 && h.len == CFG_V1_LEN)) &&
              f.read(&c, h.len) == h.len && f.read(&crc, 4) == 4 && crc == crc32((const uint8_t *)&c, h.len);
    if (ok && h.version == 1) c.channel = 0;
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
  FS.remove(CFG_FILE);
  File f(FS);
  if (!f.open(CFG_FILE, FILE_O_WRITE)) return false;
  Header h = {CFG_MAGIC, CFG_VERSION, sizeof(Config)};
  uint32_t crc = crc32((const uint8_t *)&cfg, sizeof cfg);
  bool ok = f.write((const uint8_t *)&h, sizeof h) == sizeof h &&
            f.write((const uint8_t *)&cfg, sizeof cfg) == sizeof cfg && f.write((const uint8_t *)&crc, 4) == 4;
  f.close();
  return ok;
}
