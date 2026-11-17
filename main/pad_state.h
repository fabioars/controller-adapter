// Controller state shared between the Bluetooth task (writer) and the USB task (reader),
// plus the rumble request flowing the other way (PS3 -> Xbox controller).
#pragma once

#include <stdbool.h>
#include <stdint.h>

// Logical buttons, named after the DualShock 3 they end up as.
enum {
    PAD_CROSS = 1 << 0,
    PAD_CIRCLE = 1 << 1,
    PAD_SQUARE = 1 << 2,
    PAD_TRIANGLE = 1 << 3,
    PAD_L1 = 1 << 4,
    PAD_R1 = 1 << 5,
    PAD_L3 = 1 << 6,
    PAD_R3 = 1 << 7,
    PAD_SELECT = 1 << 8,
    PAD_START = 1 << 9,
    PAD_PS = 1 << 10,
    PAD_UP = 1 << 11,
    PAD_DOWN = 1 << 12,
    PAD_LEFT = 1 << 13,
    PAD_RIGHT = 1 << 14,
};

#define PAD_STICK_CENTER 128

// Players one board can serve (one DS3 USB interface each). Limited by the
// ESP32-S3's USB endpoints: 4 IN endpoints besides EP0.
#define MAX_PLAYERS 4

typedef struct {
    uint16_t buttons;        // PAD_* bitmask
    uint8_t lx, ly, rx, ry;  // 0..255, 128 = center, 0 = left/up
    uint8_t l2, r2;          // analog triggers, 0..255
} pad_state_t;

typedef struct {
    uint8_t weak;    // small/high-frequency motor, 0..255
    uint8_t strong;  // large/low-frequency motor, 0..255
} rumble_state_t;

extern const pad_state_t PAD_NEUTRAL;

// player: 0 .. MAX_PLAYERS-1
void pad_state_set(int player, const pad_state_t* s);
pad_state_t pad_state_get(int player);

void rumble_state_set(int player, rumble_state_t r);
rumble_state_t rumble_state_get(int player);
