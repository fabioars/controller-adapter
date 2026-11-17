#include "ds3_report.h"

#include <string.h>

// Offsets into the 48-byte payload (wire offset - 1, since the report ID is stripped).
enum {
    OFF_BUTTONS1 = 1,  // select, L3, R3, start, up, right, down, left
    OFF_BUTTONS2 = 2,  // L2, R2, L1, R1, triangle, circle, cross, square
    OFF_BUTTONS3 = 3,  // PS
    OFF_LX = 5,
    OFF_LY = 6,
    OFF_RX = 7,
    OFF_RY = 8,
    OFF_PRESS_UP = 13,  // then right, down, left, L2, R2, L1, R1, triangle, circle, cross, square
    OFF_PLUGGED = 28,
    OFF_POWER = 29,
    OFF_CONNECTION = 30,
    OFF_ACCEL_X = 40,  // accel X/Y/Z then gyro Z, 16-bit big-endian each
};

#define DS3_PLUGGED 0x02
#define DS3_POWER_FULL 0x05
#define DS3_WIRED_RUMBLE 0x10
#define DS3_SIXAXIS_CENTER 0x01FF  // ~512, a controller lying flat and still

static inline uint8_t full_if(bool on) {
    return on ? 0xFF : 0x00;
}

void ds3_build_input(const pad_state_t* pad, uint8_t out[DS3_INPUT_PAYLOAD_LEN]) {
    memset(out, 0, DS3_INPUT_PAYLOAD_LEN);
    const uint16_t b = pad->buttons;
    const bool l2 = pad->l2 > DS3_TRIGGER_DIGITAL_THRESHOLD;
    const bool r2 = pad->r2 > DS3_TRIGGER_DIGITAL_THRESHOLD;

    out[OFF_BUTTONS1] = ((b & PAD_SELECT) ? 0x01 : 0) | ((b & PAD_L3) ? 0x02 : 0) | ((b & PAD_R3) ? 0x04 : 0) |
                        ((b & PAD_START) ? 0x08 : 0) | ((b & PAD_UP) ? 0x10 : 0) | ((b & PAD_RIGHT) ? 0x20 : 0) |
                        ((b & PAD_DOWN) ? 0x40 : 0) | ((b & PAD_LEFT) ? 0x80 : 0);
    out[OFF_BUTTONS2] = (l2 ? 0x01 : 0) | (r2 ? 0x02 : 0) | ((b & PAD_L1) ? 0x04 : 0) | ((b & PAD_R1) ? 0x08 : 0) |
                        ((b & PAD_TRIANGLE) ? 0x10 : 0) | ((b & PAD_CIRCLE) ? 0x20 : 0) |
                        ((b & PAD_CROSS) ? 0x40 : 0) | ((b & PAD_SQUARE) ? 0x80 : 0);
    out[OFF_BUTTONS3] = (b & PAD_PS) ? 0x01 : 0;

    out[OFF_LX] = pad->lx;
    out[OFF_LY] = pad->ly;
    out[OFF_RX] = pad->rx;
    out[OFF_RY] = pad->ry;

    // Pressure-sensitive buttons: the Xbox pad only has digital face buttons,
    // so they report fully pressed or released. Triggers are truly analog.
    uint8_t* p = &out[OFF_PRESS_UP];
    p[0] = full_if(b & PAD_UP);
    p[1] = full_if(b & PAD_RIGHT);
    p[2] = full_if(b & PAD_DOWN);
    p[3] = full_if(b & PAD_LEFT);
    p[4] = pad->l2;
    p[5] = pad->r2;
    p[6] = full_if(b & PAD_L1);
    p[7] = full_if(b & PAD_R1);
    p[8] = full_if(b & PAD_TRIANGLE);
    p[9] = full_if(b & PAD_CIRCLE);
    p[10] = full_if(b & PAD_CROSS);
    p[11] = full_if(b & PAD_SQUARE);

    out[OFF_PLUGGED] = DS3_PLUGGED;
    out[OFF_POWER] = DS3_POWER_FULL;
    out[OFF_CONNECTION] = DS3_WIRED_RUMBLE;

    for (int i = 0; i < 4; i++) {
        out[OFF_ACCEL_X + i * 2] = DS3_SIXAXIS_CENTER >> 8;
        out[OFF_ACCEL_X + i * 2 + 1] = DS3_SIXAXIS_CENTER & 0xFF;
    }
}

bool ds3_parse_output(const uint8_t* payload, uint16_t len, rumble_state_t* rumble) {
    // [0] padding, [1] right duration, [2] right (weak) on/off,
    // [3] left duration, [4] left (strong) force 0..255, ... [9] LED bitmap.
    if (len < 5) {
        return false;
    }
    rumble->weak = payload[2] ? 0xFF : 0x00;
    rumble->strong = payload[4];
    return true;
}
