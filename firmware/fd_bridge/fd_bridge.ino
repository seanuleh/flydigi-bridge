/*
 * fd_bridge — Flydigi Vader 4 (Pro) wireless controllers -> USB Xbox 360 (XInput) controllers
 * Board: Adafruit Feather nRF52840 (Adafruit nRF52 core, TinyUSB)
 *
 * Radio: emulates the official dongle for up to 2 controllers at once (ESB PRX, 2 Mbps, one
 *        address per radio pipe, ack payloads). USB: one Xbox 360 interface per controller + CDC debug.
 *
 * Slots (controller addresses, names, player order) live in flash: store.cpp.
 * Console: word commands with JSON replies for the web page, plus the old one-letter debug commands.
 * See docs/firmware.md.
 */
#include <Arduino.h>
#include <Adafruit_TinyUSB.h>

// USB mode. 0 (default) = Windows composite mode: own VID/PID + MS OS 2.0 descriptors, Xbox pads + COM port.
// 1 = Microsoft 045E:028E, needed for macOS to see a pad (Windows then hides the COM port).
#ifndef USB_XBOX_IDS
#define USB_XBOX_IDS 0
#endif
// Bump when the MS OS descriptors change: Windows caches them per VID/PID/bcdDevice.
#define USB_BCD_DEVICE 0x0101
#define FW_VERSION "0.2.0"
#include "xinput.h"
#include "store.h"

// Slot n listens on radio pipe n (pipe 0 = BASE0, pipe 1 = BASE1, each with its own prefix byte).
static uint8_t channel = 12;
static int8_t verbose_slot = -1;
// Reply to non-report packets (search pings, 21-B handshake): 0='02', 1='02 aa <b19+1> 00' (needed for handshake),
// 2=none, 3='02 aa <b19> 00', 4=free-running clock, 5=clock for pings + '02' for 21-B
static volatile uint8_t other_mode = 1;
static bool scan_enabled = true;      // channel hunting and automatic channel moves ("channel auto")
static uint32_t cq_moved_ms = 0;       // last automatic channel move (hunting pauses so controllers can follow)
#define CQ_GRACE_MS 6000
static uint8_t pipe_mask = 0x03;   // debug override (`p<mask>`); ANDed with the used slots
static uint8_t pipes_enabled() {
  uint8_t m = 0;
  for (int i = 0; i < NSLOT; i++) if (cfg.slot[i].used) m |= 1 << i;
  return m & pipe_mask;
}

struct Slot {
  uint8_t report[32];
  volatile bool fresh;
  volatile uint32_t last_ms;
  volatile uint32_t n_report, n_other;
  volatile uint32_t n_new, n_dup;      // reports with a new counter (byte 30) / retransmits of the last one
  volatile uint8_t last_ctr; volatile bool have_ctr;
  volatile int16_t rssi_x16;           // smoothed RSSI, dBm * 16
  uint8_t other[34]; volatile bool other_fresh;
};
static Slot slot[NSLOT];

static uint8_t rx_buf[40], tx_buf[40];
static volatile bool in_tx = false;
static volatile uint32_t n_bad = 0, any_rx_ms = 0;

static uint8_t rb(uint8_t x) { return __RBIT(x) >> 24; }
static uint32_t base_of(const uint8_t *a) {
  return ((uint32_t)rb(a[3]) << 24) | ((uint32_t)rb(a[2]) << 16) | ((uint32_t)rb(a[1]) << 8) | rb(a[0]);
}
static const uint32_t SH_RX = RADIO_SHORTS_READY_START_Msk | RADIO_SHORTS_END_DISABLE_Msk |
                              RADIO_SHORTS_ADDRESS_RSSISTART_Msk | RADIO_SHORTS_DISABLED_RSSISTOP_Msk;
// Ack timing: RADIO END -> (PPI) TIMER3 start; TIMER3 CC0 -> (PPI) RADIO TXEN. TX ramp-up is ~130 us after TXEN.
// Real dongle: ack END ~192 us after the controller packet END.
static volatile uint32_t tx_delay_us = 20;
static volatile uint32_t t_rx_end = 0, turn_sum = 0, turn_n = 0;
static void ack_timer_init() {
  NRF_TIMER3->TASKS_STOP = 1; NRF_TIMER3->TASKS_CLEAR = 1;
  NRF_TIMER3->MODE = TIMER_MODE_MODE_Timer; NRF_TIMER3->BITMODE = TIMER_BITMODE_BITMODE_32Bit;
  NRF_TIMER3->PRESCALER = 4;                        // 1 MHz
  NRF_TIMER3->CC[0] = tx_delay_us;
  NRF_TIMER3->CC[1] = 0;
  NRF_TIMER3->SHORTS = TIMER_SHORTS_COMPARE0_STOP_Msk;
  NRF_PPI->CH[10].EEP = (uint32_t)&NRF_RADIO->EVENTS_END;
  NRF_PPI->CH[10].TEP = (uint32_t)&NRF_TIMER3->TASKS_START;
  NRF_PPI->FORK[10].TEP = (uint32_t)&NRF_TIMER3->TASKS_CLEAR;
  NRF_PPI->CH[11].EEP = (uint32_t)&NRF_TIMER3->EVENTS_COMPARE[0];
  NRF_PPI->CH[11].TEP = (uint32_t)&NRF_RADIO->TASKS_TXEN;
  NRF_PPI->CHENCLR = (1 << 10) | (1 << 11);
}
static inline void ack_arm(bool on) {           // arm/disarm the END->TXEN path for the next RX packet
  if (on) NRF_PPI->CHENSET = (1 << 10) | (1 << 11); else NRF_PPI->CHENCLR = (1 << 10) | (1 << 11);
}
static const uint32_t SH_TX = RADIO_SHORTS_READY_START_Msk | RADIO_SHORTS_END_DISABLE_Msk |
                              RADIO_SHORTS_DISABLED_RXEN_Msk;

static void radio_off() {
  NRF_RADIO->SHORTS = 0; NRF_RADIO->INTENCLR = 0xFFFFFFFF;
  NRF_RADIO->EVENTS_DISABLED = 0; NRF_RADIO->TASKS_DISABLE = 1;
  while (!NRF_RADIO->EVENTS_DISABLED);
  NRF_RADIO->EVENTS_DISABLED = 0;
}
static void start_rx() {
  in_tx = false;
  ack_arm(true);
  NRF_RADIO->PACKETPTR = (uint32_t)rx_buf;
  NRF_RADIO->SHORTS = SH_RX;
  NRF_RADIO->EVENTS_DISABLED = 0;
  NRF_RADIO->TASKS_RXEN = 1;
}
static void radio_init() {
  NVIC_DisableIRQ(RADIO_IRQn);
  radio_off();
  NRF_RADIO->MODE = RADIO_MODE_MODE_Nrf_2Mbit << RADIO_MODE_MODE_Pos;
  NRF_RADIO->MODECNF0 = RADIO_MODECNF0_RU_Default << RADIO_MODECNF0_RU_Pos;
  NRF_RADIO->TXPOWER = RADIO_TXPOWER_TXPOWER_Pos8dBm;   // max; only affects our acks (controllers -> bridge is their power)
  NRF_RADIO->FREQUENCY = channel;
  NRF_RADIO->PCNF0 = (6 << RADIO_PCNF0_LFLEN_Pos) | (3 << RADIO_PCNF0_S1LEN_Pos);
  NRF_RADIO->PCNF1 = (RADIO_PCNF1_ENDIAN_Big << RADIO_PCNF1_ENDIAN_Pos) | (4 << RADIO_PCNF1_BALEN_Pos) | (32 << RADIO_PCNF1_MAXLEN_Pos);
  NRF_RADIO->BASE0 = base_of(cfg.slot[0].addr);
  NRF_RADIO->BASE1 = base_of(cfg.slot[1].addr);
  NRF_RADIO->PREFIX0 = ((uint32_t)rb(cfg.slot[1].addr[4]) << 8) | rb(cfg.slot[0].addr[4]);
  NRF_RADIO->TXADDRESS = 0;
  NRF_RADIO->RXADDRESSES = pipes_enabled();
  NRF_RADIO->CRCCNF = RADIO_CRCCNF_LEN_Two << RADIO_CRCCNF_LEN_Pos;
  NRF_RADIO->CRCINIT = 0xFFFF; NRF_RADIO->CRCPOLY = 0x11021;
  NRF_RADIO->INTENSET = RADIO_INTENSET_DISABLED_Msk;
  ack_timer_init();
  NVIC_SetPriority(RADIO_IRQn, 0);
  NVIC_ClearPendingIRQ(RADIO_IRQn);
  NVIC_EnableIRQ(RADIO_IRQn);
  start_rx();
}
static void abort_tx_and_rx() {
  ack_arm(false);
  NRF_TIMER3->TASKS_STOP = 1;
  NRF_RADIO->SHORTS = 0;
  if (NRF_RADIO->STATE != RADIO_STATE_STATE_Disabled) {
    NRF_RADIO->EVENTS_DISABLED = 0;
    NRF_RADIO->TASKS_DISABLE = 1;
    while (!NRF_RADIO->EVENTS_DISABLED);
  }
  NRF_RADIO->EVENTS_DISABLED = 0;
  start_rx();
}

extern "C" void RADIO_IRQHandler() {
  if (!NRF_RADIO->EVENTS_DISABLED) return;
  NRF_RADIO->EVENTS_DISABLED = 0;
  if (in_tx) {                         // ack sent; DISABLED_RXEN short is ramping RX
    uint32_t now = DWT->CYCCNT;
    turn_sum += (now - t_rx_end) >> 6; turn_n++;
    in_tx = false;
    NRF_RADIO->PACKETPTR = (uint32_t)rx_buf;
    NRF_RADIO->SHORTS = SH_RX;
    ack_arm(true);
    return;
  }
  t_rx_end = DWT->CYCCNT;
  NRF_PPI->CHENCLR = (1 << 10);        // TXEN already scheduled in TIMER3; don't let the ack's END restart it
  NRF_TIMER3->EVENTS_COMPARE[0] = 0;
  // RX done; DISABLED_TXEN short already started the TX ramp-up (~130 us)
  uint32_t pipe = NRF_RADIO->RXMATCH;
  any_rx_ms = millis();
  if (NRF_RADIO->CRCSTATUS != 1 || pipe >= NSLOT) { n_bad++; abort_tx_and_rx(); return; }
  NRF_RADIO->TXADDRESS = pipe;         // reply on the controller's own address
  Slot &S = slot[pipe];
  uint8_t len = rx_buf[0] & 0x3f;
  tx_buf[1] = rx_buf[1];
  int16_t rssi = -(int16_t)(NRF_RADIO->RSSISAMPLE & 0x7F) * 16;
  S.rssi_x16 = S.rssi_x16 ? S.rssi_x16 + (rssi - S.rssi_x16) / 8 : rssi;
  if (len == 32) {
    S.n_report++;
    uint8_t ctr = rx_buf[2 + 30];
    if (S.have_ctr && ctr == S.last_ctr) S.n_dup++; else S.n_new++;
    S.last_ctr = ctr; S.have_ctr = true;
    memcpy(S.report, rx_buf + 2, 32);
    S.fresh = true;
    S.last_ms = millis();
    tx_buf[0] = 4; tx_buf[2] = 0x02; tx_buf[3] = 0xaa; tx_buf[4] = rx_buf[2 + 30] + 1; tx_buf[5] = 0;
  } else {
    S.n_other++;                       // vendor replies / search pings: 1-byte ack
    if (!S.other_fresh) { memcpy(S.other, rx_buf, 34); S.other_fresh = true; }
    if (other_mode == 2) { abort_tx_and_rx(); return; }
    if (other_mode == 0) { tx_buf[0] = 1; tx_buf[2] = 0x02; }
    else if (other_mode == 4 || (other_mode == 5 && len == 4)) {   // free-running clock like the real dongle
      tx_buf[0] = 4; tx_buf[2] = 0x02; tx_buf[3] = 0xaa; tx_buf[4] = (uint8_t)(millis() >> 1); tx_buf[5] = 0; }
    else if (other_mode == 5) { tx_buf[0] = 1; tx_buf[2] = 0x02; }
    else { uint8_t ctr = (len >= 20) ? rx_buf[2 + 19] : rx_buf[2 + 2];
           tx_buf[0] = 4; tx_buf[2] = 0x02; tx_buf[3] = 0xaa; tx_buf[4] = (other_mode == 1) ? ctr + 1 : ctr; tx_buf[5] = 0; }
  }
  NRF_RADIO->PACKETPTR = (uint32_t)tx_buf;
  NRF_RADIO->SHORTS = SH_TX;
  in_tx = true;
}

// ───────────────────────── report translation ─────────────────────────
static int16_t axis(uint8_t v, bool invert) {
  int s = (int)v - 128;
  int out = s >= 0 ? s * 32767 / 127 : s * 256;
  if (invert) out = (out == -32768) ? 32767 : -out;
  return (int16_t)out;
}
static void build_xinput(const uint8_t *r, uint8_t *o) {
  memset(o, 0, 20);
  o[1] = 0x14;
  uint8_t b3 = r[3], b4 = r[4], lo = 0, hi = 0;
  if (b3 & 0x01) lo |= 0x01;   // dpad up
  if (b3 & 0x04) lo |= 0x02;   // dpad down
  if (b3 & 0x08) lo |= 0x04;   // dpad left
  if (b3 & 0x02) lo |= 0x08;   // dpad right
  if (b4 & 0x02) lo |= 0x10;   // start
  if (b3 & 0x40) lo |= 0x20;   // back
  if (b4 & 0x40) lo |= 0x40;   // LS click
  if (b4 & 0x80) lo |= 0x80;   // RS click
  if (b4 & 0x04) hi |= 0x01;   // LB
  if (b4 & 0x08) hi |= 0x02;   // RB
  if (b3 & 0x10) hi |= 0x10;   // A
  if (b3 & 0x20) hi |= 0x20;   // B
  if (b3 & 0x80) hi |= 0x40;   // X
  if (b4 & 0x01) hi |= 0x80;   // Y
  o[2] = lo; o[3] = hi;
  o[4] = r[28]; o[5] = r[29];
  int16_t lx = axis(r[24], false), ly = axis(r[25], true), rx = axis(r[26], false), ry = axis(r[27], true);
  memcpy(o + 6, &lx, 2); memcpy(o + 8, &ly, 2); memcpy(o + 10, &rx, 2); memcpy(o + 12, &ry, 2);
}


// ───────────────────────── players: slot -> Xbox interface ─────────────────────────
// A slot is "linked" once reports arrive, and unlinked after LINK_LOST_MS of silence (hysteresis, so a short
// dropout doesn't reshuffle players). Linked slots take the Xbox interfaces in cfg.order: with both linked,
// order[0] is player 1; a lone controller is always player 1. An interface whose controller is gone, or
// whose reports stopped for NEUTRAL_MS, gets one neutral report so no button stays held.
#define LINK_LOST_MS 3000
#define NEUTRAL_MS 150
static bool linked[NSLOT];
static int8_t itf_of[NSLOT] = {-1, -1};     // Xbox interface per slot, -1 = none
static int8_t slot_on_itf[XI_MAX] = {-1, -1};
static bool itf_neutral_due[XI_MAX];
static uint32_t n_usb[NSLOT];
static bool remap_due = true;

static bool slot_up(int i) { return cfg.slot[i].used && millis() - slot[i].last_ms < 500 && slot[i].last_ms; }

static void update_players() {
  bool changed = remap_due;
  remap_due = false;
  uint32_t now = millis();
  for (int i = 0; i < NSLOT; i++) {
    bool l = cfg.slot[i].used && slot[i].last_ms &&
             (linked[i] ? now - slot[i].last_ms < LINK_LOST_MS : now - slot[i].last_ms < 100);
    if (l != linked[i]) { linked[i] = l; changed = true; }
  }
  if (!changed) return;
  int8_t want[XI_MAX] = {-1, -1};
  int n = 0;
  for (int k = 0; k < NSLOT; k++) { int s = cfg.order[k]; if (linked[s] && n < XI_MAX) want[n++] = s; }
  for (int i = 0; i < NSLOT; i++) itf_of[i] = -1;
  for (int x = 0; x < XI_MAX; x++) {
    if (want[x] != slot_on_itf[x]) itf_neutral_due[x] = true;
    slot_on_itf[x] = want[x];
    if (want[x] >= 0) itf_of[want[x]] = x;
  }
}

static void send_neutrals() {
  uint32_t now = millis();
  for (int x = 0; x < XI_MAX; x++) {
    int s = slot_on_itf[x];
    if (s >= 0 && !itf_neutral_due[x] && now - slot[s].last_ms > NEUTRAL_MS) itf_neutral_due[x] = true;
    if (!itf_neutral_due[x] || !xinput_ready(x)) continue;
    if (s >= 0 && slot[s].fresh) { itf_neutral_due[x] = false; continue; }   // real data is about to go out
    uint8_t o[20] = {0x00, 0x14};
    if (xinput_send(x, o, 20)) itf_neutral_due[x] = false;
  }
}

// ───────────────────────── telemetry ─────────────────────────
struct Tele { uint32_t prev_new, prev_dup; uint16_t rate, dup; };
static Tele tele[NSLOT];
static uint32_t prev_bad, tele_ms;
static uint16_t bad_rate;
static bool update_tele() {              // per-second rates; true once a second
  if (millis() - tele_ms < 1000) return false;
  tele_ms = millis();
  for (int i = 0; i < NSLOT; i++) {
    uint32_t nn = slot[i].n_new, nd = slot[i].n_dup;
    tele[i].rate = nn - tele[i].prev_new; tele[i].dup = nd - tele[i].prev_dup;
    tele[i].prev_new = nn; tele[i].prev_dup = nd;
  }
  bad_rate = n_bad - prev_bad; prev_bad = n_bad;
  return true;
}
static bool hunting();

// ───────────────────────── console ─────────────────────────
// Word commands answer with one JSON object per line (see docs/firmware.md). Lines that don't start with a
// known word fall through to the old one-letter debug commands.
static char cmd[96]; static int cl = 0;
static uint8_t watch_level = 0;
static uint32_t last_watch_status = 0, last_input_ms[NSLOT];
static bool legacy_stats = true;

static void addr_hex(const uint8_t *a, char *out) { for (int k = 0; k < 5; k++) sprintf(out + 2 * k, "%02x", a[k]); }
static bool parse_addr(const char *h, uint8_t *a) {
  if (strlen(h) != 10) return false;
  for (int k = 0; k < 5; k++) {
    char b[3] = {h[2 * k], h[2 * k + 1], 0}; char *e;
    a[k] = strtoul(b, &e, 16);
    if (*e) return false;
  }
  return true;
}

static void print_status() {
  Serial.printf("{\"type\":\"status\",\"fw\":\"" FW_VERSION "\",\"ch\":%u,\"hunt\":%s,\"bad\":%u,\"order\":[%u,%u],\"slots\":[",
                channel, hunting() ? "true" : "false", bad_rate, cfg.order[0], cfg.order[1]);
  for (int i = 0; i < NSLOT; i++) {
    const SlotCfg &c = cfg.slot[i];
    char a[11] = "";
    if (c.used) addr_hex(c.addr, a);
    const char *link = !c.used ? "empty" : slot_up(i) ? "up" : linked[i] ? "lost" : "down";
    Serial.printf("%s{\"slot\":%d,\"name\":\"%s\",\"addr\":\"%s\",\"kind\":%u,\"link\":\"%s\",\"player\":%d,"
                  "\"rate\":%u,\"dup\":%u,\"rssi\":%d,\"usb\":%lu}",
                  i ? "," : "", i, c.used ? c.name : "", a, c.kind, link, itf_of[i] + 1, tele[i].rate, tele[i].dup,
                  slot_up(i) ? slot[i].rssi_x16 / 16 : 0, n_usb[i]);
  }
  Serial.println("]}");
}
static void print_input(int i, const uint8_t *o) {
  int16_t lx, ly, rx, ry;
  memcpy(&lx, o + 6, 2); memcpy(&ly, o + 8, 2); memcpy(&rx, o + 10, 2); memcpy(&ry, o + 12, 2);
  Serial.printf("{\"type\":\"input\",\"slot\":%d,\"btn\":%u,\"lt\":%u,\"rt\":%u,\"lx\":%d,\"ly\":%d,\"rx\":%d,\"ry\":%d}\n",
                i, o[2] | (o[3] << 8), o[4], o[5], lx, ly, rx, ry);
}
static void reply_ok(const char *c) { Serial.printf("{\"type\":\"ok\",\"cmd\":\"%s\"}\n", c); }
static void reply_err(const char *msg) { Serial.printf("{\"type\":\"error\",\"msg\":\"%s\"}\n", msg); }

static void save_cfg() {                 // flash write stalls the CPU: take the radio down around it
  NVIC_DisableIRQ(RADIO_IRQn);
  radio_off();
  bool ok = store_save();
  radio_init();
  if (!ok) reply_err("flash write failed");
}
static int parse_slot(const char *t) {
  if (!t || !*t) return -1;
  char *e; long v = strtol(t, &e, 10);
  return (*e || v < 0 || v >= NSLOT) ? -1 : (int)v;
}
static void clear_slot_state(int i) {
  NVIC_DisableIRQ(RADIO_IRQn);
  memset((void *)&slot[i], 0, sizeof slot[i]);
  NVIC_EnableIRQ(RADIO_IRQn);
  memset(&tele[i], 0, sizeof tele[i]);
  remap_due = true;
}


// ───────────────────────── learn mode ─────────────────────────
// Adopt a searching controller's existing address (docs/learn-and-pairing-plan.md §4). Normal serving stops;
// the radio runs promiscuous (2-byte match on preamble-like bits, 34 raw bytes, no hardware CRC) and hops
// channels 2-80. Every capture is searched in software for an ESB packet: at each bit offset take 5 address
// bytes + 9-bit header, and check CRC-16 (init FFFF, poly 1021) over address, header and payload.
// An address with LEARN_HITS valid packets becomes the candidate; then normal mode takes it on the slot's pipe
// and waits for reports before saving.
#define LEARN_DWELL_MS 15
#define LEARN_HITS 5
#define LEARN_CONFIRM_MS 6000
enum { L_OFF, L_SCAN, L_CONFIRM };
static uint8_t learn_state = L_OFF, learn_slot;
static uint32_t learn_deadline, learn_hop_ms, learn_msg_ms, learn_confirm_ms;
static uint8_t learn_raw[34];
static SlotCfg learn_prev;
struct Cand { uint8_t addr[5]; uint8_t hits, ch; int8_t rssi; };
#define NCAND 6
static Cand cand[NCAND];
static uint8_t ncand;

static inline uint8_t rawbit(const uint8_t *b, int i) { return (b[i >> 3] >> (7 - (i & 7))) & 1; }
static uint32_t rawbits(const uint8_t *b, int i, int n) { uint32_t v = 0; while (n--) v = (v << 1) | rawbit(b, i++); return v; }
// Returns true and fills addr if an ESB packet with a valid CRC starts at bit `off`.
static bool esb_at(const uint8_t *b, int off, uint8_t *addr) {
  const int total = 34 * 8;
  if (off + 40 + 9 > total) return false;
  uint8_t len = rawbits(b, off + 40, 6);
  if (len == 0 || len > 32) return false;
  int nbits = 40 + 9 + len * 8;
  if (off + nbits + 16 > total) return false;
  uint16_t crc = 0xFFFF;
  for (int i = 0; i < nbits; i++) {
    crc ^= (uint16_t)rawbit(b, off + i) << 15;
    crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
  }
  if (crc != rawbits(b, off + nbits, 16)) return false;
  for (int k = 0; k < 5; k++) addr[k] = rawbits(b, off + 8 * k, 8);
  return true;
}
static void learn_radio_rx() {
  NRF_RADIO->EVENTS_END = 0; NRF_RADIO->EVENTS_DISABLED = 0;
  NRF_RADIO->PACKETPTR = (uint32_t)learn_raw;
  NRF_RADIO->SHORTS = RADIO_SHORTS_READY_START_Msk | RADIO_SHORTS_END_DISABLE_Msk |
                      RADIO_SHORTS_ADDRESS_RSSISTART_Msk | RADIO_SHORTS_DISABLED_RSSISTOP_Msk;
  NRF_RADIO->TASKS_RXEN = 1;
}
static void learn_radio_init() {
  NVIC_DisableIRQ(RADIO_IRQn);
  ack_arm(false); NRF_TIMER3->TASKS_STOP = 1;
  radio_off();
  NRF_RADIO->MODE = RADIO_MODE_MODE_Nrf_2Mbit << RADIO_MODE_MODE_Pos;
  NRF_RADIO->FREQUENCY = channel;
  NRF_RADIO->PCNF0 = 0;
  NRF_RADIO->PCNF1 = (RADIO_PCNF1_ENDIAN_Big << RADIO_PCNF1_ENDIAN_Pos) | (1 << RADIO_PCNF1_BALEN_Pos) |
                     (34 << RADIO_PCNF1_STATLEN_Pos) | (34 << RADIO_PCNF1_MAXLEN_Pos);
  NRF_RADIO->BASE0 = 0; NRF_RADIO->BASE1 = 0;
  NRF_RADIO->PREFIX0 = (0x55 << 8) | 0xAA;
  NRF_RADIO->RXADDRESSES = 0x03;
  NRF_RADIO->CRCCNF = 0;
  learn_radio_rx();
}
static void learn_msg(const char *state) {
  Serial.printf("{\"type\":\"learn\",\"state\":\"%s\",\"slot\":%u,\"ch\":%u,\"left\":%ld,\"candidates\":[",
                state, learn_slot, channel, (long)((int32_t)(learn_deadline - millis()) / 1000));
  for (int i = 0; i < ncand; i++) {
    char a[11]; addr_hex(cand[i].addr, a);
    Serial.printf("%s{\"addr\":\"%s\",\"hits\":%u,\"ch\":%u,\"rssi\":%d}", i ? "," : "", a, cand[i].hits, cand[i].ch, cand[i].rssi);
  }
  Serial.println("]}");
}
static void learn_end(const char *state, const char *reason) {
  Serial.printf("{\"type\":\"learn\",\"state\":\"%s\",\"slot\":%u,\"reason\":\"%s\"}\n", state, learn_slot, reason);
  learn_state = L_OFF;
}
static bool learn_start(int s, int timeout_s) {
  learn_slot = s; learn_prev = cfg.slot[s];
  ncand = 0;
  learn_deadline = millis() + (uint32_t)timeout_s * 1000;
  learn_state = L_SCAN;
  channel = 2; learn_hop_ms = learn_msg_ms = millis();
  learn_radio_init();
  learn_msg("listening");
  return true;
}
static void learn_cancel() {
  if (learn_state == L_OFF) return;
  cfg.slot[learn_slot] = learn_prev;
  clear_slot_state(learn_slot);
  learn_end("cancelled", "");
  radio_init();
}
static void learn_found(int k) {
  const Cand &c = cand[k];
  learn_state = L_CONFIRM;
  SlotCfg &sc = cfg.slot[learn_slot];
  sc.used = 1; sc.kind = 1; memcpy(sc.addr, c.addr, 5);
  if (!learn_prev.used) snprintf(sc.name, sizeof sc.name, "Pad %u", learn_slot + 1);
  clear_slot_state(learn_slot);
  channel = c.ch;
  radio_init();                          // normal serving, all slots, on the channel it was heard on
  learn_confirm_ms = millis();
  learn_msg("confirming");
}
static void learn_step() {
  if (learn_state == L_OFF) return;
  uint32_t now = millis();
  if (learn_state == L_CONFIRM) {
    if (slot[learn_slot].n_new >= 20) {  // steady reports: keep it
      save_cfg();
      char a[11]; addr_hex(cfg.slot[learn_slot].addr, a);
      Serial.printf("{\"type\":\"learn\",\"state\":\"done\",\"slot\":%u,\"addr\":\"%s\"}\n", learn_slot, a);
      learn_state = L_OFF;
    } else if (now - learn_confirm_ms > LEARN_CONFIRM_MS) {
      cfg.slot[learn_slot] = learn_prev;
      clear_slot_state(learn_slot);
      radio_init();
      learn_end("failed", "heard the controller but it didn't connect");
    }
    return;
  }
  if ((int32_t)(now - learn_deadline) > 0) {
    cfg.slot[learn_slot] = learn_prev;
    radio_init();
    learn_end("failed", ncand ? "not enough packets" : "nothing heard");
    return;
  }
  if (NRF_RADIO->EVENTS_DISABLED) {      // a capture is ready
    int8_t rssi = -(int8_t)(NRF_RADIO->RSSISAMPLE & 0x7F);
    uint8_t buf[34]; memcpy(buf, learn_raw, 34);
    learn_radio_rx();
    uint8_t a[5];
    for (int off = 0; off <= 24; off++) {
      if (!esb_at(buf, off, a)) continue;
      bool other = false;
      for (int i = 0; i < NSLOT; i++)
        if (i != learn_slot && cfg.slot[i].used && !memcmp(cfg.slot[i].addr, a, 5)) other = true;
      if (other) break;
      int k = 0;
      while (k < ncand && memcmp(cand[k].addr, a, 5)) k++;
      if (k == ncand) {
        if (ncand == NCAND) break;
        memcpy(cand[k].addr, a, 5); cand[k].hits = 0; ncand++;
      }
      Cand &c = cand[k];
      if (c.hits < 255) c.hits++;
      c.ch = channel; c.rssi = rssi;
      learn_hop_ms = now;                // stay on this channel while it's talking
      if (c.hits >= LEARN_HITS) { learn_found(k); return; }
      break;
    }
  }
  if (now - learn_hop_ms >= LEARN_DWELL_MS) {
    learn_hop_ms = now;
    channel = (channel >= 80) ? 2 : channel + 1;
    NRF_RADIO->SHORTS = 0; NRF_RADIO->EVENTS_DISABLED = 0; NRF_RADIO->TASKS_DISABLE = 1;
    while (!NRF_RADIO->EVENTS_DISABLED);
    NRF_RADIO->FREQUENCY = channel;
    learn_radio_rx();
  }
  if (now - learn_msg_ms >= 500) { learn_msg_ms = now; learn_msg("listening"); }
}


// ───────────────────────── channel quality ─────────────────────────
// When the connected controllers' average report rate stays below CQ_BAD_RATE for CQ_BAD_SECS, leave the
// channel (it cools down for 5 minutes). Controllers only search a set of channels of their own, so the bridge
// moves to the best channel where it has had controllers before (they followed from 55 to 12 within 2 s), and
// otherwise hunts, skipping channels that are cooling down. (2026-11-10: a jump to 25, not in their set, cost a
// 6 s drop.) A channel that stays good for a minute is saved and used after the next boot.
#define CQ_BAD_RATE 85
#define CQ_BAD_SECS 3
#define CQ_COOLDOWN_MS 300000
#define CQ_SAVE_MS 60000
static uint8_t cq_bad_secs = 0;
static uint32_t cq_good_since = 0, cq_bad_until[101];
static uint8_t cq_score[101];           // smoothed average reports/s seen on each channel; 0 = never had controllers
static bool cq_cooling(uint8_t c) { return cq_bad_until[c] && (int32_t)(millis() - cq_bad_until[c]) < 0; }
static void channel_monitor() {
  if (!scan_enabled || learn_state != L_OFF) return;
  int n = 0, sum = 0;
  for (int i = 0; i < NSLOT; i++) if (slot_up(i)) { n++; sum += tele[i].rate; }
  uint32_t now = millis();
  if (!n) { cq_bad_secs = 0; cq_good_since = 0; return; }
  int avg = sum / n;
  if (now - cq_moved_ms < 2000) return;   // first second on a new channel is partial
  cq_score[channel] = cq_score[channel] ? (cq_score[channel] * 3 + avg) / 4 : (avg ? avg : 1);
  if (avg >= CQ_BAD_RATE) {
    cq_bad_secs = 0;
    if (!cq_good_since) cq_good_since = now;
    if (cfg.channel != channel && now - cq_good_since > CQ_SAVE_MS) { cfg.channel = channel; save_cfg(); }
    return;
  }
  cq_good_since = 0;
  if (++cq_bad_secs < CQ_BAD_SECS) return;
  cq_bad_secs = 0;
  cq_bad_until[channel] = now + CQ_COOLDOWN_MS;
  uint8_t next = 0;
  for (int c = 2; c <= 80; c++)
    if (c != channel && cq_score[c] && !cq_cooling(c) && (!next || cq_score[c] > cq_score[next])) next = c;
  if (next) {
    Serial.printf("{\"type\":\"log\",\"msg\":\"channel %u: %d reports/s, moving to %u\"}\n", channel, avg, next);
    channel = next;
    cq_moved_ms = now;                  // hunting waits CQ_GRACE_MS for the controllers to follow
  } else {
    Serial.printf("{\"type\":\"log\",\"msg\":\"channel %u: %d reports/s, searching for a better one\"}\n", channel, avg);
    channel = (channel >= 80) ? 2 : channel + 1;
    cq_moved_ms = now - CQ_GRACE_MS;    // no grace: hunt straight away
  }
  radio_init();
}

// Returns false if the line isn't a word command.
static bool handle_word(char *line) {
  char *save, *w = strtok_r(line, " ", &save);
  if (!w) return true;
  char *a1 = strtok_r(NULL, " ", &save);
  if (!strcmp(w, "status")) { print_status(); return true; }
  if (!strcmp(w, "watch")) {
    watch_level = a1 ? atoi(a1) : 1;
    if (watch_level > 2) watch_level = 2;
    legacy_stats = watch_level == 0;
    reply_ok(w); return true;
  }
  if (!strcmp(w, "name")) {
    int s = parse_slot(a1);
    char *txt = save;                    // rest of the line
    if (s < 0 || !cfg.slot[s].used || !txt || !*txt) { reply_err("usage: name <slot> <text>"); return true; }
    char clean[16]; int n = 0;
    for (char *c = txt; *c && n < 15; c++) if (*c >= 0x20 && *c < 0x7f && *c != '"' && *c != '\\') clean[n++] = *c;
    clean[n] = 0;
    memcpy(cfg.slot[s].name, clean, 16);
    save_cfg(); reply_ok(w); return true;
  }
  if (!strcmp(w, "forget")) {
    int s = parse_slot(a1);
    if (s < 0) { reply_err("usage: forget <slot>"); return true; }
    memset(&cfg.slot[s], 0, sizeof cfg.slot[s]);
    clear_slot_state(s);
    save_cfg(); reply_ok(w); return true;
  }
  if (!strcmp(w, "addr")) {              // set a slot's address by hand: addr <slot> <10 hex> [name]
    int s = parse_slot(a1);
    char *h = strtok_r(NULL, " ", &save);
    uint8_t a[5];
    if (s < 0 || !h || !parse_addr(h, a)) { reply_err("usage: addr <slot> <10 hex on-air> [name]"); return true; }
    for (int i = 0; i < NSLOT; i++)
      if (i != s && cfg.slot[i].used && !memcmp(cfg.slot[i].addr, a, 5)) { reply_err("address already in another slot"); return true; }
    SlotCfg &c = cfg.slot[s];
    c.used = 1; c.kind = 1; memcpy(c.addr, a, 5);
    char *nm = save && *save ? save : (char *)(s == 0 ? "Red" : "Blue");
    memset(c.name, 0, 16); strncpy(c.name, nm, 15);
    clear_slot_state(s);
    save_cfg(); reply_ok(w); return true;
  }
  if (!strcmp(w, "order")) {             // order <slot for player 1> <slot for player 2>
    int s0 = parse_slot(a1), s1 = parse_slot(strtok_r(NULL, " ", &save));
    if (s0 < 0 || s1 < 0 || s0 == s1) { reply_err("usage: order <slot> <slot>"); return true; }
    cfg.order[0] = s0; cfg.order[1] = s1;
    remap_due = true;
    save_cfg(); reply_ok(w); return true;
  }
  if (!strcmp(w, "channel")) {
    if (a1 && !strcmp(a1, "auto")) scan_enabled = true;
    else if (a1 && atoi(a1) >= 0 && atoi(a1) <= 100) { scan_enabled = false; channel = atoi(a1); radio_init(); }
    else { reply_err("usage: channel <auto|0-100>"); return true; }
    reply_ok(w); return true;
  }
  if (!strcmp(w, "learn")) {            // learn <slot> [timeout_s]
    int sl = parse_slot(a1);
    char *t = strtok_r(NULL, " ", &save);
    int to = t ? atoi(t) : 60;
    if (sl < 0 || to < 5 || to > 600) { reply_err("usage: learn <slot> [5-600 s]"); return true; }
    if (learn_state != L_OFF) learn_cancel();
    reply_ok(w); learn_start(sl, to); return true;
  }
  if (!strcmp(w, "cancel")) { learn_cancel(); reply_ok(w); return true; }
  if (!strcmp(w, "defaults")) { store_defaults(); for (int i = 0; i < NSLOT; i++) clear_slot_state(i); save_cfg(); reply_ok(w); return true; }
  if (!strcmp(w, "reboot")) { reply_ok(w); Serial.flush(); delay(50); NVIC_SystemReset(); }
  if (!strcmp(w, "bootloader")) { reply_ok(w); Serial.flush(); delay(50); enterUf2Dfu(); }
  if (!strcmp(w, "version")) { Serial.println("{\"type\":\"version\",\"fw\":\"" FW_VERSION "\"}"); return true; }
  return false;
}

static void print_stats() {
  Serial.printf("# stat ch=%u bad=%lu turn=%luus", channel, n_bad, turn_n ? turn_sum / turn_n : 0);
  turn_sum = turn_n = 0;
  for (int i = 0; i < NSLOT; i++)
    Serial.printf(" | c%d %s P%d rep=%lu dup=%lu oth=%lu usb=%lu rssi=%d", i + 1, slot_up(i) ? "UP" : "down", itf_of[i] + 1,
                  slot[i].n_report, slot[i].n_dup, slot[i].n_other, n_usb[i], slot[i].rssi_x16 / 16);
  Serial.println();
}
static void handle(char *c) {
  char copy[sizeof cmd]; strcpy(copy, c);
  if (handle_word(copy)) return;
  if (c[1] && !isdigit((unsigned char)c[1])) { reply_err("unknown command"); return; }   // one-letter + number only
  switch (c[0]) {
    case 'c': channel = strtoul(c + 1, 0, 10); radio_init(); break;
    case 'v': { int s = strtol(c + 1, 0, 10) - 1; verbose_slot = (verbose_slot == s) ? -1 : s; } break;
    case 's': print_stats(); return;
    case 'h': other_mode = strtoul(c + 1, 0, 10); break;
    case 't': tx_delay_us = strtoul(c + 1, 0, 10); radio_init(); break;
    case 'S': scan_enabled = !scan_enabled; break;
    case 'p': pipe_mask = strtoul(c + 1, 0, 10); radio_init(); break;
    default: reply_err("unknown command"); return;
  }
  Serial.printf("# ok ch=%u verbose=%d other_mode=%u tx_delay=%lu scan=%d\n", channel, verbose_slot + 1, other_mode, tx_delay_us, scan_enabled);
}

// ───────────────────────── main ─────────────────────────
void setup() {
#if USB_XBOX_IDS
  TinyUSBDevice.setID(0x045E, 0x028E);   // macOS sees a pad; Windows binds the whole device to the Xbox driver (no COM port)
#else
  TinyUSBDevice.setID(0x1209, 0x0001);   // pid.codes test PID: Windows loads usbccgp, so the COM port shows up
  TinyUSBDevice.setDeviceVersion(USB_BCD_DEVICE);
  xinput_enable_msos();
#endif
  TinyUSBDevice.setManufacturerDescriptor("Flydigi");
  TinyUSBDevice.setProductDescriptor("Flydigi Bridge");
  xinput_add(NSLOT);
  if (TinyUSBDevice.mounted()) { TinyUSBDevice.detach(); delay(10); TinyUSBDevice.attach(); }
  Serial.begin(115200);
  store_load();
  if (cfg.channel >= 2 && cfg.channel <= 80) channel = cfg.channel;
  NRF_CLOCK->TASKS_HFCLKSTART = 1;
  while (!NRF_CLOCK->EVENTS_HFCLKSTARTED);
  NRF_CLOCK->EVENTS_HFCLKSTARTED = 0;
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk; DWT->CYCCNT = 0; DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  radio_init();
}

static uint32_t last_hop = 0;
static bool hunting() {
  if (!scan_enabled || millis() - cq_moved_ms < CQ_GRACE_MS) return false;
  for (int i = 0; i < NSLOT; i++) if (cfg.slot[i].used && millis() - slot[i].last_ms < 1000 && slot[i].last_ms) return false;
  return millis() - any_rx_ms >= 300;
}
static void scan_step() {
  if (!hunting() || millis() - last_hop < 25) return;
  last_hop = millis();
  do channel = (channel >= 80) ? 2 : channel + 1; while (cq_cooling(channel));
  radio_init();
}

static uint32_t last_stat = 0;
void loop() {
  learn_step();
  if (learn_state != L_SCAN) scan_step();
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n' || ch == '\r') { if (cl) { cmd[cl] = 0; handle(cmd); cl = 0; } }
    else if (cl < (int)sizeof(cmd) - 1) cmd[cl++] = ch;
  }
  update_players();
  if (update_tele()) channel_monitor();
  for (int i = 0; i < NSLOT; i++) {
    int x = itf_of[i];
    if (!slot[i].fresh) continue;
    bool usb_ok = x >= 0 && xinput_ready(x);
    bool want_input = watch_level >= 2 && millis() - last_input_ms[i] >= 20;
    // Keep the report for USB if the interface is busy, unless the console wants it (it doesn't wait on USB).
    if (!usb_ok && !want_input && x >= 0) continue;
    uint8_t r[32], o[20];
    NVIC_DisableIRQ(RADIO_IRQn); memcpy(r, slot[i].report, 32); slot[i].fresh = false; NVIC_EnableIRQ(RADIO_IRQn);
    build_xinput(r, o);
    if (usb_ok && xinput_send(x, o, 20)) { n_usb[i]++; itf_neutral_due[x] = false; }
    if (want_input) { last_input_ms[i] = millis(); print_input(i, o); }
    if (verbose_slot == i) {
      char line[80]; int n = snprintf(line, sizeof line, "R%d ", i + 1);
      for (int k = 0; k < 32; k++) n += snprintf(line + n, sizeof line - n, "%02x", r[k]);
      Serial.println(line);
    }
  }
  send_neutrals();
  for (int i = 0; i < NSLOT; i++) {
    if (!slot[i].other_fresh) continue;
    if (verbose_slot == i) {
      uint8_t len = slot[i].other[0] & 0x3f; if (len > 32) len = 32;
      char line[90]; int n = snprintf(line, sizeof line, "O%d len=%u s1=%u ", i + 1, len, slot[i].other[1]);
      for (int k = 0; k < len; k++) n += snprintf(line + n, sizeof line - n, "%02x", slot[i].other[2 + k]);
      Serial.println(line);
    }
    slot[i].other_fresh = false;
  }
  if (watch_level && millis() - last_watch_status >= 200) { last_watch_status = millis(); print_status(); }
  if (legacy_stats && millis() - last_stat >= 2000) { last_stat = millis(); print_stats(); }
}
