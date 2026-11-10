/*
 * BLE Scanner — nice!nano (nRF52840)
 *
 * Scans for BLE advertising devices and prints their address, RSSI,
 * and raw advertisement data. Used to check whether the Flydigi Vader 4
 * advertises as a BLE device when its original dongle is unplugged.
 *
 * If the controller shows up here → it's BLE (can use ESP32-S3 natively)
 * If nothing shows up → it uses ESB/proprietary (need Nordic radio RE)
 *
 * Board: Adafruit Feather nRF52840
 */

#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <bluefruit.h>

void scan_callback(ble_gap_evt_adv_report_t* report) {
    Serial.printf("ADDR:%02X:%02X:%02X:%02X:%02X:%02X  RSSI:%-4d  TYPE:%s  LEN:%d  DATA:",
        report->peer_addr.addr[5], report->peer_addr.addr[4],
        report->peer_addr.addr[3], report->peer_addr.addr[2],
        report->peer_addr.addr[1], report->peer_addr.addr[0],
        report->rssi,
        report->type.connectable ? "CONN" : "NCON",
        report->data.len);

    for (uint8_t i = 0; i < report->data.len && i < 20; i++) {
        Serial.printf("%02X ", report->data.p_data[i]);
    }

    // Try to extract device name from AD type 0x08 or 0x09
    uint8_t* d = report->data.p_data;
    uint8_t  n = report->data.len;
    for (uint8_t i = 0; i + 1 < n; ) {
        uint8_t len  = d[i];
        uint8_t type = d[i + 1];
        if ((type == 0x08 || type == 0x09) && len > 1) {
            Serial.print(" NAME:\"");
            for (uint8_t j = 0; j < len - 1 && i + 2 + j < n; j++) {
                Serial.print((char)d[i + 2 + j]);
            }
            Serial.print("\"");
        }
        if (len == 0) break;
        i += len + 1;
    }

    Serial.println();

    Bluefruit.Scanner.resume();
}

void setup() {
    Serial.begin(0);
    while (!Serial) delay(10);
    delay(200);

    Bluefruit.begin();
    Bluefruit.setTxPower(4);

    Bluefruit.Scanner.setRxCallback(scan_callback);
    Bluefruit.Scanner.restartOnDisconnect(true);
    Bluefruit.Scanner.filterRssi(-80);    // only show signals > -80 dBm
    Bluefruit.Scanner.useActiveScan(false);
    Bluefruit.Scanner.start(0);           // 0 = scan forever

    Serial.println("# BLE Scanner running — unplug original dongle, then watch for Flydigi");
    Serial.println("# Plug dongle back in once scanning — controller should advertise briefly");
    Serial.println("#");
}

void loop() {
    // everything handled in callback
}
