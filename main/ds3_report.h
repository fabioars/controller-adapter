// DualShock 3 USB report encoding/decoding. Pure functions, no ESP-IDF dependencies,
// so they can be unit-tested on the host (see test/).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "pad_state.h"

#define DS3_REPORT_ID_INPUT 0x01
// Input report 0x01 is 49 bytes on the wire; TinyUSB prepends the report ID,
// so we only build the 48 bytes that follow it.
#define DS3_INPUT_PAYLOAD_LEN 48

// Trigger value above which the digital L2/R2 bits are set.
#define DS3_TRIGGER_DIGITAL_THRESHOLD 32

void ds3_build_input(const pad_state_t* pad, uint8_t out[DS3_INPUT_PAYLOAD_LEN]);

// Parses an output report 0x01 payload (bytes after the report ID) sent by the PS3.
// Returns false if the payload is too short to contain rumble data.
bool ds3_parse_output(const uint8_t* payload, uint16_t len, rumble_state_t* rumble);
