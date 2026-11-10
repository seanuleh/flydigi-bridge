#pragma once
#include <stdint.h>
#define XI_MAX 2
void xinput_add(uint8_t count);
void xinput_enable_msos(void);                // Windows composite mode: bcdUSB 2.1 + MS OS 2.0 descriptors               // add `count` Xbox 360 interfaces (call before re-enumeration)
bool xinput_ready(uint8_t idx);
bool xinput_send(uint8_t idx, const uint8_t *buf, uint16_t len);
extern volatile uint8_t xinput_rumble_l[XI_MAX], xinput_rumble_r[XI_MAX];
