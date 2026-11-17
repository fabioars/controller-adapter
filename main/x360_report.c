#include "x360_report.h"

#include <string.h>

// buttons1
#define X_UP 0x01
#define X_DOWN 0x02
#define X_LEFT 0x04
#define X_RIGHT 0x08
#define X_START 0x10
#define X_BACK 0x20
#define X_LS 0x40
#define X_RS 0x80
// buttons2
#define X_LB 0x01
#define X_RB 0x02
#define X_GUIDE 0x04
#define X_A 0x10
#define X_B 0x20
#define X_X 0x40
#define X_Y 0x80

// 0..255 (128 = center) -> -32768..32767 with 128 mapping exactly to 0.
static int16_t to_s16(int v, int sign) {
    int32_t x = (int32_t)(v - 128) * 258 * sign;
    if (x > 32767) x = 32767;
    if (x < -32768) x = -32768;
    return (int16_t)x;
}

static void put_s16(uint8_t* p, int16_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((uint16_t)v >> 8);
}

void x360_build_input(const pad_state_t* pad, uint8_t out[X360_INPUT_LEN]) {
    memset(out, 0, X360_INPUT_LEN);
    const uint16_t b = pad->buttons;
    out[0] = 0x00;            // message type: input
    out[1] = X360_INPUT_LEN;  // length
    out[2] = ((b & PAD_UP) ? X_UP : 0) | ((b & PAD_DOWN) ? X_DOWN : 0) | ((b & PAD_LEFT) ? X_LEFT : 0) |
             ((b & PAD_RIGHT) ? X_RIGHT : 0) | ((b & PAD_START) ? X_START : 0) | ((b & PAD_SELECT) ? X_BACK : 0) |
             ((b & PAD_L3) ? X_LS : 0) | ((b & PAD_R3) ? X_RS : 0);
    out[3] = ((b & PAD_L1) ? X_LB : 0) | ((b & PAD_R1) ? X_RB : 0) | ((b & PAD_PS) ? X_GUIDE : 0) |
             ((b & PAD_CROSS) ? X_A : 0) | ((b & PAD_CIRCLE) ? X_B : 0) | ((b & PAD_SQUARE) ? X_X : 0) |
             ((b & PAD_TRIANGLE) ? X_Y : 0);
    out[4] = pad->l2;
    out[5] = pad->r2;
    // pad_state: 0 = left/up. Xbox 360: X negative = left, Y positive = up.
    put_s16(&out[6], to_s16(pad->lx, 1));
    put_s16(&out[8], to_s16(pad->ly, -1));
    put_s16(&out[10], to_s16(pad->rx, 1));
    put_s16(&out[12], to_s16(pad->ry, -1));
}

x360_out_t x360_parse_output(const uint8_t* buf, uint16_t len, rumble_state_t* rumble, uint8_t* player) {
    if (len >= 5 && buf[0] == 0x00 && buf[1] == 0x08) {
        rumble->strong = buf[3];  // left (large) motor
        rumble->weak = buf[4];    // right (small) motor
        return X360_OUT_RUMBLE;
    }
    if (len >= 3 && buf[0] == 0x01 && buf[1] == 0x03) {
        uint8_t pattern = buf[2];
        if (pattern >= 0x02 && pattern <= 0x05) {
            *player = pattern - 0x01;  // "N flashes, then on"
        } else if (pattern >= 0x06 && pattern <= 0x09) {
            *player = pattern - 0x05;  // "N on"
        } else {
            *player = 0;
        }
        return X360_OUT_LED;
    }
    return X360_OUT_UNKNOWN;
}
