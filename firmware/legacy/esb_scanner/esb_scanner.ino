/*
 * ESB Promiscuous Scanner — nice!nano (nRF52840)
 *
 * PURPOSE
 *   Capture Nordic Enhanced ShockBurst (ESB) packets from a Flydigi Vader 4
 *   controller + its original dongle to reverse-engineer the wireless protocol.
 *
 * SETUP
 *   1. Arduino IDE → Preferences → add board URL:
 *        https://adafruit.github.io/arduino-board-index/package_adafruit_index.json
 *      Install "Adafruit nRF52" board package (1.6.0+)
 *      Select board: Adafruit Feather nRF52840  (compatible with nice!nano bootloader)
 *   2. Double-press reset → nice!nano appears as USB drive "NICENANO"
 *      Sketch → Export Compiled Binary, then drag the .uf2 onto the drive
 *   3. Open Serial Monitor (any baud — it's USB CDC)
 *   4. Turn on the Vader 4, plug the original Flydigi dongle into a nearby USB port
 *      Wait a few seconds for them to pair, then watch packets appear
 *
 * OUTPUT (one line per CRC-OK packet)
 *   CH:<2-80>  RSSI:-<dBm>  RATE:<1|2>M  LEN:<0-32>  PID:<0-3>  NOACK:<0|1>  DATA:<hex>
 *
 * HOW THE PROMISCUOUS TRICK WORKS
 *   ESB always transmits a 1-byte preamble before the address:
 *     0xAA  if the first bit of the address is 1
 *     0x55  if the first bit of the address is 0
 *   By setting our receive address to 0xAAAAAAAAAA (pipe 0) and 0x5555555555
 *   (pipe 1) the radio syncs to the preamble and captures any ESB packet
 *   regardless of its actual address.  The true address bytes appear inside
 *   the captured payload — decode them in post-processing.
 *
 * IMPORTANT
 *   Do NOT call Bluefruit.begin() or any BLE function — we need direct RADIO
 *   access.  The SoftDevice is present in flash but remains inactive here.
 */

#include <Arduino.h>
#include <Adafruit_TinyUSB.h>

// ── tunables ──────────────────────────────────────────────────────────────────
static constexpr uint8_t  CH_MIN        =  2;   // 2402 MHz
static constexpr uint8_t  CH_MAX        = 80;   // 2480 MHz
static constexpr uint32_t DWELL_MS      =  3;   // ms per channel when idle
static constexpr uint32_t DWELL_HIT_MS = 100;   // ms to stay after a CRC-OK packet
static constexpr uint8_t  BUF_LEN       = 36;   // 2 header + 32 payload + slack

// CRC modes to cycle through:
//   0 = ESB    CRC-16/CCITT  poly=0x11021 init=0xFFFF
//   1 = Gazell CRC-16/CCITT  poly=0x11021 init=0x1D0F  (Nordic HID/gaming devices)
static constexpr uint32_t CRC_INITS[]  = { 0xFFFF, 0x1D0F };
static constexpr const char* CRC_NAMES[] = { "ESB", "GAZ" };
static uint8_t crc_mode = 0;

// ── shared state (ISR ↔ loop) ─────────────────────────────────────────────────
static uint8_t          rx_buf[BUF_LEN];   // live DMA target — do not read outside ISR
static uint8_t          pkt_buf[BUF_LEN];  // snapshot copied inside ISR
static volatile bool    pkt_ready   = false;
static volatile bool    pkt_crc_ok  = false;
static volatile uint8_t pkt_channel = 0;
static volatile uint8_t pkt_rssi    = 0;
static volatile bool    pkt_2m      = true;

// ── RADIO ISR ─────────────────────────────────────────────────────────────────
extern "C" void RADIO_IRQHandler() {
    if (NRF_RADIO->EVENTS_END) {
        NRF_RADIO->EVENTS_END = 0;

        if (!pkt_ready) {
            memcpy(pkt_buf, rx_buf, BUF_LEN);
            pkt_channel = (uint8_t)NRF_RADIO->FREQUENCY;
            pkt_rssi    = (uint8_t)NRF_RADIO->RSSISAMPLE;
            pkt_2m      = (NRF_RADIO->MODE ==
                           (RADIO_MODE_MODE_Nrf_2Mbit << RADIO_MODE_MODE_Pos));
            pkt_crc_ok  = (NRF_RADIO->CRCSTATUS == 1);
            pkt_ready   = true;
        }

        NRF_RADIO->TASKS_START = 1;  // keep RX running back-to-back
    }
}

// ── radio config + start ──────────────────────────────────────────────────────
static void radio_start(uint8_t channel, bool mbps2) {
    NRF_RADIO->TASKS_DISABLE = 1;
    while (!NRF_RADIO->EVENTS_DISABLED);
    NRF_RADIO->EVENTS_DISABLED = 0;

    NRF_RADIO->FREQUENCY = channel;
    NRF_RADIO->MODE = mbps2
        ? (RADIO_MODE_MODE_Nrf_2Mbit << RADIO_MODE_MODE_Pos)
        : (RADIO_MODE_MODE_Nrf_1Mbit << RADIO_MODE_MODE_Pos);

    // ESB packet layout written to RAM by the DMA engine:
    //   buf[0]         length   (bits 5:0 = 6-bit payload length)
    //   buf[1]         S1 field (bits 2:1 = PID,  bit 0 = NO_ACK)
    //   buf[2..len+1]  payload bytes
    NRF_RADIO->PCNF0 =
        (0UL << RADIO_PCNF0_S0LEN_Pos) |  // no S0 byte
        (6UL << RADIO_PCNF0_LFLEN_Pos) |  // 6-bit length field
        (3UL << RADIO_PCNF0_S1LEN_Pos);   // 3-bit S1 (PID[1:0] + NO_ACK)

    NRF_RADIO->PCNF1 =
        (RADIO_PCNF1_WHITEEN_Disabled << RADIO_PCNF1_WHITEEN_Pos) |
        (RADIO_PCNF1_ENDIAN_Big       << RADIO_PCNF1_ENDIAN_Pos)  |
        (2UL  << RADIO_PCNF1_BALEN_Pos)   |  // base = 2 bytes → 3-byte total address
        (0UL  << RADIO_PCNF1_STATLEN_Pos) |
        (32UL << RADIO_PCNF1_MAXLEN_Pos);

    // 1-byte promiscuous addresses — one per preamble type:
    //   pipe 0: prefix 0xAA → addr MSB=1 packets
    //   pipe 1: prefix 0x55 → addr MSB=0 packets
    NRF_RADIO->BASE0   = 0xAAAAAAAAUL;  // lower 2 bytes used: 0xAAAA
    NRF_RADIO->BASE1   = 0x55555555UL;  // lower 2 bytes used: 0x5555
    NRF_RADIO->PREFIX0 = (0x55UL << 8) | 0xAAUL;  // AP1=0x55, AP0=0xAA
    NRF_RADIO->RXADDRESSES = 0x03;                  // enable pipes 0 and 1

    // CRC-16/CCITT, address bytes excluded from CRC
    NRF_RADIO->CRCCNF =
        (RADIO_CRCCNF_LEN_Two       << RADIO_CRCCNF_LEN_Pos) |
        (RADIO_CRCCNF_SKIPADDR_Skip << RADIO_CRCCNF_SKIPADDR_Pos);
    NRF_RADIO->CRCINIT = CRC_INITS[crc_mode];
    NRF_RADIO->CRCPOLY = 0x11021UL;

    NRF_RADIO->PACKETPTR = (uint32_t)rx_buf;

    // READY→START:            auto-start RX after ramp-up
    // ADDRESS→RSSISTART:      measure RSSI as soon as address is matched
    // DISABLED→RSSISTOP:      clean up RSSI when radio is disabled
    NRF_RADIO->SHORTS =
        RADIO_SHORTS_READY_START_Msk         |
        RADIO_SHORTS_ADDRESS_RSSISTART_Msk   |
        RADIO_SHORTS_DISABLED_RSSISTOP_Msk;

    NRF_RADIO->INTENSET = RADIO_INTENSET_END_Msk;
    NVIC_SetPriority(RADIO_IRQn, 2);
    NVIC_EnableIRQ(RADIO_IRQn);

    NRF_RADIO->EVENTS_READY = 0;
    NRF_RADIO->TASKS_RXEN   = 1;
}

// ── setup ─────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(0);           // USB CDC — baud rate is ignored
    while (!Serial) delay(10);
    delay(200);

    // Radio requires the 64 MHz external crystal (HFXO)
    NRF_CLOCK->TASKS_HFCLKSTART = 1;
    while (!NRF_CLOCK->EVENTS_HFCLKSTARTED);
    NRF_CLOCK->EVENTS_HFCLKSTARTED = 0;

    Serial.println(F("# ESB Promiscuous Scanner — nice!nano nRF52840"));
    Serial.println(F("# Power on Vader 4, plug in original Flydigi dongle, wait for pairing"));
    Serial.println(F("# CH:<ch>  RSSI:-<dBm>  RATE:<M>  LEN:<n>  PID:<n>  NOACK:<b>  DATA:<hex>"));
    Serial.println(F("#"));

    radio_start(CH_MIN, true);
}

// ── loop ──────────────────────────────────────────────────────────────────────
static uint8_t  ch       = CH_MIN;
static bool     mbps2    = true;
static uint32_t last_hop = 0;
static bool     had_hit  = false;

void loop() {
    if (pkt_ready) {
        pkt_ready = false;
        had_hit   = true;
        last_hop  = millis();

        uint8_t len    =  pkt_buf[0] & 0x3F;
        uint8_t pid    = (pkt_buf[1] >> 1) & 0x03;
        uint8_t no_ack =  pkt_buf[1] & 0x01;

        Serial.printf("%s CH:%-3u RSSI:-%3u RATE:%dM CRC:%s LEN:%-2u PID:%u NOACK:%u DATA:",
                      pkt_crc_ok ? "OK " : "BAD",
                      pkt_channel, pkt_rssi,
                      pkt_2m ? 2 : 1,
                      CRC_NAMES[crc_mode],
                      len, pid, no_ack);

        for (uint8_t i = 2; i < 2 + len && i < BUF_LEN; i++) {
            if (i > 2) Serial.print(' ');
            Serial.printf("%02X", pkt_buf[i]);
        }
        Serial.println();
    }

    uint32_t now   = millis();
    uint32_t dwell = had_hit ? DWELL_HIT_MS : DWELL_MS;

    if ((now - last_hop) >= dwell) {
        last_hop = now;
        had_hit  = false;

        if (++ch > CH_MAX) {
            ch    = CH_MIN;
            mbps2 = !mbps2;
            if (!mbps2) {
                crc_mode = (crc_mode + 1) % 2;  // rotate CRC mode each full cycle
            }
            Serial.printf("# Sweep done — %dMbps CRC:%s\n", mbps2 ? 2 : 1, CRC_NAMES[crc_mode]);
        }
        radio_start(ch, mbps2);
    }
}
