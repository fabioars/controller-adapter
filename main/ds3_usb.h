// USB personality: Sony DualShock 3 for the PS3 (one HID interface per player).
#pragma once

#include <stdint.h>

void ds3_init(void);   // before TinyUSB starts: builds descriptors for the configured players
void ds3_start(void);  // after TinyUSB starts: input report task

const uint8_t* ds3_device_descriptor(void);
const uint8_t* ds3_config_descriptor(void);
const char* ds3_string(uint8_t index);  // NULL = no such string
