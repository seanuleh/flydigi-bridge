/*
 * fd_dongle — emulate the Flydigi Vader 4 dongle (ESB PRX with ack payloads)
 * Replies:  21B hello -> 1B "02"      32B report -> 4B "02 aa <r[30]+1> 00"
 *           4B search -> configurable (mode s0: none, s1: "02", s2: 02 aa x+1 00)
 * Serial commands: c<ch>  s<0|2>  x<10hex addr>  q (quiet toggle)
 * Output: P <us> <ch> <rssi> <len> <hex>     plus "# stat" lines each second
 */
#include <Arduino.h>
#include <Adafruit_TinyUSB.h>

static uint8_t addr[5] = {0x69, 0xcc, 0x96, 0x6c, 0xee};
static uint8_t channel = 4, search_mode = 1;
static bool quiet = false;

static uint8_t rx_buf[40], tx_buf[40];
static volatile bool in_tx = false;
struct Pkt { uint32_t t; uint8_t rssi; uint8_t d[36]; };
static Pkt q[64]; static volatile uint8_t qh = 0, qt = 0;
static volatile uint32_t n_rx = 0, n_ack = 0, n_bad = 0, n_hello = 0, n_report = 0, n_search = 0;

static uint8_t rb(uint8_t x) { return __RBIT(x) >> 24; }

static const uint32_t SH_RX = RADIO_SHORTS_READY_START_Msk | RADIO_SHORTS_END_DISABLE_Msk |
                              RADIO_SHORTS_DISABLED_TXEN_Msk | RADIO_SHORTS_ADDRESS_RSSISTART_Msk |
                              RADIO_SHORTS_DISABLED_RSSISTOP_Msk;
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
  NRF_RADIO->PACKETPTR = (uint32_t)rx_buf;
  NRF_RADIO->SHORTS = SH_RX;
  NRF_RADIO->EVENTS_DISABLED = 0;
  NRF_RADIO->TASKS_RXEN = 1;
}

static void radio_init() {
  radio_off();
  NRF_RADIO->MODE = RADIO_MODE_MODE_Nrf_2Mbit << RADIO_MODE_MODE_Pos;
  NRF_RADIO->MODECNF0 = RADIO_MODECNF0_RU_Default << RADIO_MODECNF0_RU_Pos;
  NRF_RADIO->TXPOWER = RADIO_TXPOWER_TXPOWER_0dBm;
  NRF_RADIO->FREQUENCY = channel;
  NRF_RADIO->PCNF0 = (6 << RADIO_PCNF0_LFLEN_Pos) | (3 << RADIO_PCNF0_S1LEN_Pos);
  NRF_RADIO->PCNF1 = (RADIO_PCNF1_ENDIAN_Big << RADIO_PCNF1_ENDIAN_Pos) | (4 << RADIO_PCNF1_BALEN_Pos) | (32 << RADIO_PCNF1_MAXLEN_Pos);
  NRF_RADIO->BASE0 = ((uint32_t)rb(addr[3]) << 24) | ((uint32_t)rb(addr[2]) << 16) | ((uint32_t)rb(addr[1]) << 8) | rb(addr[0]);
  NRF_RADIO->PREFIX0 = rb(addr[4]);
  NRF_RADIO->TXADDRESS = 0;
  NRF_RADIO->RXADDRESSES = 0x01;
  NRF_RADIO->CRCCNF = RADIO_CRCCNF_LEN_Two << RADIO_CRCCNF_LEN_Pos;
  NRF_RADIO->CRCINIT = 0xFFFF; NRF_RADIO->CRCPOLY = 0x11021;
  NRF_RADIO->INTENSET = RADIO_INTENSET_DISABLED_Msk;
  NVIC_SetPriority(RADIO_IRQn, 0);
  NVIC_EnableIRQ(RADIO_IRQn);
  start_rx();
}

static void abort_tx_and_rx() {
  // DISABLED_TXEN already fired; cancel the TX ramp and go back to RX
  NRF_RADIO->SHORTS = 0;
  NRF_RADIO->TASKS_DISABLE = 1;
  while (!NRF_RADIO->EVENTS_DISABLED);
  NRF_RADIO->EVENTS_DISABLED = 0;
  start_rx();
}

extern "C" void RADIO_IRQHandler() {
  if (!NRF_RADIO->EVENTS_DISABLED) return;
  NRF_RADIO->EVENTS_DISABLED = 0;
  if (in_tx) {             // ack sent, radio is ramping back to RX via short
    n_ack++;
    in_tx = false;
    NRF_RADIO->PACKETPTR = (uint32_t)rx_buf;
    NRF_RADIO->SHORTS = SH_RX;
    return;
  }
  // RX finished; radio is ramping up TX (DISABLED_TXEN)
  if (NRF_RADIO->CRCSTATUS != 1) { n_bad++; abort_tx_and_rx(); return; }
  n_rx++;
  uint8_t len = rx_buf[0] & 0x3f;
  bool reply = true;
  tx_buf[1] = rx_buf[1];   // same S1 (pid/noack)
  if (len == 21) { n_hello++; tx_buf[0] = 1; tx_buf[2] = 0x02; }
  else if (len == 32) { n_report++; tx_buf[0] = 4; tx_buf[2] = 0x02; tx_buf[3] = 0xaa; tx_buf[4] = rx_buf[2 + 30] + 1; tx_buf[5] = 0; }
  else if (len == 4) {
    n_search++;
    if (search_mode == 1) { tx_buf[0] = 1; tx_buf[2] = 0x02; }
    else if (search_mode == 2) { tx_buf[0] = 4; tx_buf[2] = 0x02; tx_buf[3] = 0xaa; tx_buf[4] = rx_buf[4] + 1; tx_buf[5] = 0; }
    else reply = false;
  } else reply = false;

  uint8_t n = (qh + 1) & 63;
  if (n != qt) { q[qh].t = DWT->CYCCNT >> 6; q[qh].rssi = NRF_RADIO->RSSISAMPLE; memcpy(q[qh].d, rx_buf, 36); qh = n; }

  if (!reply) { abort_tx_and_rx(); return; }
  NRF_RADIO->PACKETPTR = (uint32_t)tx_buf;
  NRF_RADIO->SHORTS = SH_TX;
  in_tx = true;
}

static char cmd[64]; static int cl = 0;
static void handle(char *c) {
  switch (c[0]) {
    case 'c': channel = strtoul(c + 1, 0, 10); break;
    case 's': search_mode = strtoul(c + 1, 0, 10); break;
    case 'q': quiet = !quiet; break;
    case 'x': for (int i = 0; i < 5; i++) { char t[3] = {c[1 + 2*i], c[2 + 2*i], 0}; addr[i] = strtoul(t, 0, 16); } break;
  }
  radio_init();
  Serial.printf("# ok ch=%u search=%u quiet=%d addr=%02x%02x%02x%02x%02x\n", channel, search_mode, quiet, addr[0], addr[1], addr[2], addr[3], addr[4]);
}

void setup() {
  Serial.begin(115200);
  NRF_CLOCK->TASKS_HFCLKSTART = 1;
  while (!NRF_CLOCK->EVENTS_HFCLKSTARTED);
  NRF_CLOCK->EVENTS_HFCLKSTARTED = 0;
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk; DWT->CYCCNT = 0; DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  radio_init();
}

static uint32_t last_stat = 0;
void loop() {
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n' || ch == '\r') { if (cl) { cmd[cl] = 0; handle(cmd); cl = 0; } }
    else if (cl < (int)sizeof(cmd) - 1) cmd[cl++] = ch;
  }
  while (qt != qh) {
    Pkt &p = q[qt];
    uint8_t len = p.d[0] & 0x3f; if (len > 32) len = 32;
    if (!quiet) {
      char line[120]; int n = snprintf(line, sizeof line, "P %lu %u %u %u ", p.t, channel, p.rssi, len);
      for (int i = 0; i < len; i++) n += snprintf(line + n, sizeof line - n, "%02x", p.d[2 + i]);
      Serial.println(line);
    }
    qt = (qt + 1) & 63;
  }
  if (millis() - last_stat >= 1000) {
    last_stat = millis();
    Serial.printf("# stat rx=%lu ack=%lu bad=%lu hello=%lu report=%lu search=%lu\n", n_rx, n_ack, n_bad, n_hello, n_report, n_search);
  }
}
