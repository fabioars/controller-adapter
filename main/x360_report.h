// Wired Xbox 360 controller report encoding/decoding. Pure functions, no ESP-IDF
// dependencies, so they can be unit-tested on the host (see test/).
#pragma once

#include <stdint.h>

#include "pad_state.h"

#define X360_INPUT_LEN 20

void x360_build_input(const pad_state_t* pad, uint8_t out[X360_INPUT_LEN]);

typedef enum {
    X360_OUT_UNKNOWN = 0,
    X360_OUT_RUMBLE,  // *rumble set
    X360_OUT_LED,     // *player set (1-4, or 0 when the LED pattern isn't a player number)
} x360_out_t;

// Parses a packet the console sends on the OUT endpoint.
x360_out_t x360_parse_output(const uint8_t* buf, uint16_t len, rumble_state_t* rumble, uint8_t* player);
