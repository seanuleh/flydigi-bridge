/*
 * fd_sniff — Flydigi Vader 4 ESB sniffer (nRF52840, Adafruit nRF52 core)
 * Fixed ESB address, 2Mbps, dynamic payload (9-bit PCF), CRC16 incl. address.
 *
 * Serial commands (newline-terminated):
 *   a            channel list = 0..100
 *   l1,2,3       channel list
 *   d<ms>        dwell per channel (0 = never hop)
 *   h<ms>        extra dwell after a hit
 *   b            also print CRC-bad packets (toggle)
 *   x<10hex>     set address (on-air byte order, e.g. x66cc69c6ee)
 * Output: P <us> <ch> <rssi> <pipe> <crcok> <len> <pid> <noack> <hex>
 */
#include <Arduino.h>
#include <Adafruit_TinyUSB.h>

static uint8_t addr[5] = {0x69, 0xcc, 0x96, 0x6c, 0xee};
static uint8_t chans[101]; static uint8_t nch = 0, chi = 0;
static uint32_t dwell_ms = 20, hit_ms = 0;
static bool show_bad = false;
// raw register mode: m<balen>,<prefix0 hex>,<prefix1 hex>,<base0 hex>,<base1 hex>,<raw 0/1>
static bool reg_mode = false, raw_mode = false; static uint32_t r_balen = 4, r_pre0, r_pre1, r_base0, r_base1;

static uint8_t rx_buf[40];
struct Pkt { uint32_t t; uint8_t ch, rssi, pipe, crc; uint8_t d[36]; };
static Pkt q[64]; static volatile uint8_t qh = 0, qt = 0;
static volatile uint32_t last_hit = 0;

extern "C" void RADIO_IRQHandler() {
  if (NRF_RADIO->EVENTS_END) {
    NRF_RADIO->EVENTS_END = 0;
    bool ok = NRF_RADIO->CRCSTATUS == 1;
    if (raw_mode) ok = true;
    if (ok || show_bad) {
      uint8_t n = (qh + 1) & 63;
      if (n != qt) {
        Pkt &p = q[qh];
        p.t = DWT->CYCCNT >> 6; p.ch = NRF_RADIO->FREQUENCY; p.rssi = NRF_RADIO->RSSISAMPLE;
        p.pipe = NRF_RADIO->RXMATCH; p.crc = ok;
        memcpy(p.d, rx_buf, 36);
        qh = n;
      }
      if (ok) last_hit = millis();
    }
    NRF_RADIO->TASKS_START = 1;
  }
}

static uint8_t rb(uint8_t x) { return __RBIT(x) >> 24; }

static void radio_stop() {
  NRF_RADIO->SHORTS = 0;
  NRF_RADIO->EVENTS_DISABLED = 0;
  NRF_RADIO->TASKS_DISABLE = 1;
  while (!NRF_RADIO->EVENTS_DISABLED);
  NRF_RADIO->EVENTS_DISABLED = 0;
}

static void radio_start(uint8_t ch) {
  radio_stop();
  NRF_RADIO->FREQUENCY = ch;
  NRF_RADIO->MODE = RADIO_MODE_MODE_Nrf_2Mbit << RADIO_MODE_MODE_Pos;
  if (reg_mode) {
    NRF_RADIO->PCNF0 = raw_mode ? 0 : ((6 << RADIO_PCNF0_LFLEN_Pos) | (3 << RADIO_PCNF0_S1LEN_Pos));
    NRF_RADIO->PCNF1 = (RADIO_PCNF1_ENDIAN_Big << RADIO_PCNF1_ENDIAN_Pos) | (r_balen << RADIO_PCNF1_BALEN_Pos) |
                       ((raw_mode ? 34 : 0) << RADIO_PCNF1_STATLEN_Pos) | ((raw_mode ? 34 : 32) << RADIO_PCNF1_MAXLEN_Pos);
    NRF_RADIO->BASE0 = r_base0; NRF_RADIO->BASE1 = r_base1;
    NRF_RADIO->PREFIX0 = (r_pre1 << 8) | r_pre0;
    NRF_RADIO->RXADDRESSES = 0x03;
    if (raw_mode) NRF_RADIO->CRCCNF = 0;
    else { NRF_RADIO->CRCCNF = (RADIO_CRCCNF_LEN_Two << RADIO_CRCCNF_LEN_Pos); NRF_RADIO->CRCINIT = 0xFFFF; NRF_RADIO->CRCPOLY = 0x11021; }
    goto common;
  }
  NRF_RADIO->PCNF0 = (0 << RADIO_PCNF0_S0LEN_Pos) | (6 << RADIO_PCNF0_LFLEN_Pos) | (3 << RADIO_PCNF0_S1LEN_Pos);
  NRF_RADIO->PCNF1 = (RADIO_PCNF1_WHITEEN_Disabled << RADIO_PCNF1_WHITEEN_Pos) |
                     (RADIO_PCNF1_ENDIAN_Big << RADIO_PCNF1_ENDIAN_Pos) |
                     (4 << RADIO_PCNF1_BALEN_Pos) | (0 << RADIO_PCNF1_STATLEN_Pos) |
                     (32 << RADIO_PCNF1_MAXLEN_Pos);
  // nRF52 sends address bits LSB-first, base LSByte first, prefix LAST.
  // addr[] is in on-air (MSB-first) order, as seen in raw captures.
  NRF_RADIO->BASE0 = ((uint32_t)rb(addr[3]) << 24) | ((uint32_t)rb(addr[2]) << 16) | ((uint32_t)rb(addr[1]) << 8) | rb(addr[0]);
  NRF_RADIO->PREFIX0 = rb(addr[4]);
  NRF_RADIO->RXADDRESSES = 0x01;
  NRF_RADIO->CRCCNF = (RADIO_CRCCNF_LEN_Two << RADIO_CRCCNF_LEN_Pos) | (RADIO_CRCCNF_SKIPADDR_Include << RADIO_CRCCNF_SKIPADDR_Pos);
  NRF_RADIO->CRCINIT = 0xFFFF;
  NRF_RADIO->CRCPOLY = 0x11021;
common:
  NRF_RADIO->PACKETPTR = (uint32_t)rx_buf;
  NRF_RADIO->SHORTS = RADIO_SHORTS_READY_START_Msk | RADIO_SHORTS_ADDRESS_RSSISTART_Msk | RADIO_SHORTS_DISABLED_RSSISTOP_Msk;
  NRF_RADIO->INTENSET = RADIO_INTENSET_END_Msk;
  NVIC_SetPriority(RADIO_IRQn, 1);
  NVIC_EnableIRQ(RADIO_IRQn);
  NRF_RADIO->EVENTS_READY = 0;
  NRF_RADIO->TASKS_RXEN = 1;
}

static void all_chans() { nch = 0; for (int c = 0; c <= 100; c++) chans[nch++] = c; chi = 0; }

static char cmd[400]; static int cl = 0;
static void handle(char *c) {
  switch (c[0]) {
    case 'a': all_chans(); break;
    case 'l': { nch = 0; char *s = c + 1; while (*s && nch < 101) { chans[nch++] = strtoul(s, &s, 10); if (*s == ',') s++; else break; } chi = 0; } break;
    case 'd': dwell_ms = strtoul(c + 1, 0, 10); break;
    case 'h': hit_ms = strtoul(c + 1, 0, 10); break;
    case 'm': { char *t = c + 1; r_balen = strtoul(t, &t, 10); t++; r_pre0 = strtoul(t, &t, 16); t++; r_pre1 = strtoul(t, &t, 16); t++;
                r_base0 = strtoul(t, &t, 16); t++; r_base1 = strtoul(t, &t, 16); t++; raw_mode = strtoul(t, &t, 10); reg_mode = true; } break;
    case 'n': reg_mode = raw_mode = false; break;
    case 'b': show_bad = !show_bad; break;
    case 'x': for (int i = 0; i < 5; i++) { char t[3] = {c[1 + 2*i], c[2 + 2*i], 0}; addr[i] = strtoul(t, 0, 16); } break;
  }
  Serial.printf("# ok nch=%u dwell=%lu hit=%lu bad=%d addr=%02x%02x%02x%02x%02x\n", nch, dwell_ms, hit_ms, show_bad, addr[0], addr[1], addr[2], addr[3], addr[4]);
  if (nch) radio_start(chans[chi]);
}

void setup() {
  Serial.begin(115200);
  NRF_CLOCK->TASKS_HFCLKSTART = 1;
  while (!NRF_CLOCK->EVENTS_HFCLKSTARTED);
  NRF_CLOCK->EVENTS_HFCLKSTARTED = 0;
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk; DWT->CYCCNT = 0; DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  all_chans();
  radio_start(chans[0]);
}

static uint32_t last_hop = 0;
void loop() {
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n' || ch == '\r') { if (cl) { cmd[cl] = 0; handle(cmd); cl = 0; } }
    else if (cl < (int)sizeof(cmd) - 1) cmd[cl++] = ch;
  }
  while (qt != qh) {
    Pkt &p = q[qt];
    uint8_t len = p.d[0] & 0x3f; if (len > 32) len = 32;
    if (raw_mode) { char line[120]; int n = snprintf(line, sizeof line, "R %lu %u %u %u ", p.t, p.ch, p.rssi, p.pipe);
      for (int i = 0; i < 34; i++) n += snprintf(line + n, sizeof line - n, "%02x", p.d[i]);
      Serial.println(line); qt = (qt + 1) & 63; continue; }
    char line[120]; int n = snprintf(line, sizeof line, "P %lu %u %u %u %u %u %u %u ", p.t, p.ch, p.rssi, p.pipe, p.crc, len, (p.d[1] >> 1) & 3, p.d[1] & 1);
    for (int i = 0; i < len; i++) n += snprintf(line + n, sizeof line - n, "%02x", p.d[2 + i]);
    Serial.println(line);
    qt = (qt + 1) & 63;
  }
  uint32_t now = millis();
  if (dwell_ms && nch > 1 && now - last_hop >= dwell_ms && now - last_hit >= hit_ms) {
    last_hop = now;
    chi = (chi + 1) % nch;
    radio_start(chans[chi]);
  }
}
