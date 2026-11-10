/*
 * fd_bridge — Flydigi Vader 4 (Pro) wireless controllers -> USB Xbox 360 (XInput) controllers
 * Board: Adafruit Feather nRF52840 (Adafruit nRF52 core, TinyUSB)
 *
 * Radio: emulates the official dongle for up to 2 controllers at once (ESB PRX, 2 Mbps, one
 *        address per radio pipe, ack payloads). USB: one Xbox 360 interface per controller + CDC debug.
 *
 * Serial commands: c<ch>  S (toggle channel hunting)  v<slot> (raw dump)  s (stats)  p<mask> (slots)
 *                  t<us> (ack delay)  h<mode> (handshake reply mode)
 */
#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include "xinput.h"

#define NSLOT 2
// On-air (MSB-first) addresses, one per controller. Both must share the last byte (radio prefix rules
// allow different BASE0/BASE1, so only pipes 0 and 1 are used).
static uint8_t addrs[NSLOT][5] = {
  {0x69, 0xcc, 0x96, 0x6c, 0xee},   // controller 1
  {0x66, 0xcc, 0x69, 0xc6, 0xee},   // controller 2
};
static uint8_t channel = 12;
static int8_t verbose_slot = -1;
// Reply to non-report packets (search pings, 21-B handshake): 0='02', 1='02 aa <b19+1> 00' (needed for handshake),
// 2=none, 3='02 aa <b19> 00', 4=free-running clock, 5=clock for pings + '02' for 21-B
static volatile uint8_t other_mode = 1;
static bool scan_enabled = true;
static uint8_t pipe_mask = 0x03;   // radio pipes (slots) enabled

struct Slot {
  uint8_t report[32];
  volatile bool fresh;
  volatile uint32_t last_ms;
  volatile uint32_t n_report, n_other;
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
  NRF_RADIO->TXPOWER = RADIO_TXPOWER_TXPOWER_0dBm;
  NRF_RADIO->FREQUENCY = channel;
  NRF_RADIO->PCNF0 = (6 << RADIO_PCNF0_LFLEN_Pos) | (3 << RADIO_PCNF0_S1LEN_Pos);
  NRF_RADIO->PCNF1 = (RADIO_PCNF1_ENDIAN_Big << RADIO_PCNF1_ENDIAN_Pos) | (4 << RADIO_PCNF1_BALEN_Pos) | (32 << RADIO_PCNF1_MAXLEN_Pos);
  NRF_RADIO->BASE0 = base_of(addrs[0]);
  NRF_RADIO->BASE1 = base_of(addrs[1]);
  NRF_RADIO->PREFIX0 = ((uint32_t)rb(addrs[1][4]) << 8) | rb(addrs[0][4]);
  NRF_RADIO->TXADDRESS = 0;
  NRF_RADIO->RXADDRESSES = pipe_mask;
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
  if (len == 32) {
    S.n_report++;
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

// ───────────────────────── main ─────────────────────────
static char cmd[64]; static int cl = 0;
static uint32_t n_usb[NSLOT];
static void print_stats() {
  Serial.printf("# stat ch=%u bad=%lu turn=%luus", channel, n_bad, turn_n ? turn_sum / turn_n : 0);
  turn_sum = turn_n = 0;
  for (int i = 0; i < NSLOT; i++)
    Serial.printf(" | c%d %s rep=%lu oth=%lu usb=%lu rum=%u/%u", i + 1, (millis() - slot[i].last_ms < 500) ? "UP" : "down",
                  slot[i].n_report, slot[i].n_other, n_usb[i], xinput_rumble_l[i], xinput_rumble_r[i]);
  Serial.println();
}
static void handle(char *c) {
  switch (c[0]) {
    case 'c': channel = strtoul(c + 1, 0, 10); radio_init(); break;
    case 'v': { int s = strtol(c + 1, 0, 10) - 1; verbose_slot = (verbose_slot == s) ? -1 : s; } break;
    case 's': print_stats(); return;
    case 'h': other_mode = strtoul(c + 1, 0, 10); break;
    case 't': tx_delay_us = strtoul(c + 1, 0, 10); radio_init(); break;
    case 'S': scan_enabled = !scan_enabled; break;
    case 'p': pipe_mask = strtoul(c + 1, 0, 10); radio_init(); break;
  }
  Serial.printf("# ok ch=%u verbose=%d other_mode=%u tx_delay=%lu scan=%d\n", channel, verbose_slot + 1, other_mode, tx_delay_us, scan_enabled);
}

void setup() {
  TinyUSBDevice.setID(0x045E, 0x028E);
  TinyUSBDevice.setManufacturerDescriptor("Flydigi");
  TinyUSBDevice.setProductDescriptor("Flydigi Bridge");
  xinput_add(NSLOT);
  if (TinyUSBDevice.mounted()) { TinyUSBDevice.detach(); delay(10); TinyUSBDevice.attach(); }
  Serial.begin(115200);
  NRF_CLOCK->TASKS_HFCLKSTART = 1;
  while (!NRF_CLOCK->EVENTS_HFCLKSTARTED);
  NRF_CLOCK->EVENTS_HFCLKSTARTED = 0;
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk; DWT->CYCCNT = 0; DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  radio_init();
}

static uint32_t last_hop = 0;
static void scan_step() {
  if (!scan_enabled) return;
  uint32_t now = millis();
  bool anyone = false;
  for (int i = 0; i < NSLOT; i++) if (now - slot[i].last_ms < 1000) anyone = true;
  if (anyone || now - any_rx_ms < 300 || now - last_hop < 25) return;
  last_hop = now;
  channel = (channel >= 80) ? 2 : channel + 1;
  radio_init();
}

static uint32_t last_stat = 0;
void loop() {
  scan_step();
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n' || ch == '\r') { if (cl) { cmd[cl] = 0; handle(cmd); cl = 0; } }
    else if (cl < (int)sizeof(cmd) - 1) cmd[cl++] = ch;
  }
  for (int i = 0; i < NSLOT; i++) {
    if (!slot[i].fresh || !xinput_ready(i)) continue;
    uint8_t r[32], o[20];
    NVIC_DisableIRQ(RADIO_IRQn); memcpy(r, slot[i].report, 32); slot[i].fresh = false; NVIC_EnableIRQ(RADIO_IRQn);
    build_xinput(r, o);
    if (xinput_send(i, o, 20)) n_usb[i]++;
    if (verbose_slot == i) {
      char line[80]; int n = snprintf(line, sizeof line, "R%d ", i + 1);
      for (int k = 0; k < 32; k++) n += snprintf(line + n, sizeof line - n, "%02x", r[k]);
      Serial.println(line);
    }
  }
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
  if (millis() - last_stat >= 2000) { last_stat = millis(); print_stats(); }
}
