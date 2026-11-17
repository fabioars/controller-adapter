// Host unit tests for the DS3 report encoder. Run: ./test/run.sh
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ds3_report.h"

const pad_state_t PAD_NEUTRAL = {.lx = 128, .ly = 128, .rx = 128, .ry = 128};

// Wire offsets (report ID at 0) -> payload index.
#define W(off) ((off) - 1)

static int failures;
#define CHECK_EQ(a, b)                                                                              \
    do {                                                                                            \
        unsigned _a = (a), _b = (b);                                                                \
        if (_a != _b) {                                                                             \
            fprintf(stderr, "%s:%d: %s == 0x%02x, expected 0x%02x\n", __FILE__, __LINE__, #a, _a, _b); \
            failures++;                                                                             \
        }                                                                                           \
    } while (0)

static void test_neutral(void) {
    uint8_t out[DS3_INPUT_PAYLOAD_LEN];
    ds3_build_input(&PAD_NEUTRAL, out);
    CHECK_EQ(out[W(2)], 0);
    CHECK_EQ(out[W(3)], 0);
    CHECK_EQ(out[W(4)], 0);
    CHECK_EQ(out[W(6)], 128);
    CHECK_EQ(out[W(9)], 128);
    for (int i = 14; i <= 25; i++) CHECK_EQ(out[W(i)], 0);
    CHECK_EQ(out[W(29)], 0x02);  // plugged
    CHECK_EQ(out[W(41)], 0x01);  // accel X high byte (big-endian 0x01FF)
    CHECK_EQ(out[W(42)], 0xFF);
    CHECK_EQ(out[W(48)], 0xFF);  // gyro Z low byte = last byte of 49-byte report
}

static void test_buttons(void) {
    struct {
        uint16_t pad;
        int wire_off;
        uint8_t bit;
        int pressure_off;  // 0 = none
    } cases[] = {
        {PAD_SELECT, 2, 0x01, 0},   {PAD_L3, 2, 0x02, 0},       {PAD_R3, 2, 0x04, 0},
        {PAD_START, 2, 0x08, 0},    {PAD_UP, 2, 0x10, 14},      {PAD_RIGHT, 2, 0x20, 15},
        {PAD_DOWN, 2, 0x40, 16},    {PAD_LEFT, 2, 0x80, 17},    {PAD_L1, 3, 0x04, 20},
        {PAD_R1, 3, 0x08, 21},      {PAD_TRIANGLE, 3, 0x10, 22}, {PAD_CIRCLE, 3, 0x20, 23},
        {PAD_CROSS, 3, 0x40, 24},   {PAD_SQUARE, 3, 0x80, 25},  {PAD_PS, 4, 0x01, 0},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        pad_state_t p = PAD_NEUTRAL;
        p.buttons = cases[i].pad;
        uint8_t out[DS3_INPUT_PAYLOAD_LEN];
        ds3_build_input(&p, out);
        CHECK_EQ(out[W(cases[i].wire_off)], cases[i].bit);
        if (cases[i].pressure_off) CHECK_EQ(out[W(cases[i].pressure_off)], 0xFF);
    }
}

static void test_triggers_and_sticks(void) {
    pad_state_t p = PAD_NEUTRAL;
    p.l2 = 10;  // below digital threshold
    p.r2 = 200;
    p.lx = 0;
    p.ly = 255;
    p.rx = 7;
    p.ry = 250;
    uint8_t out[DS3_INPUT_PAYLOAD_LEN];
    ds3_build_input(&p, out);
    CHECK_EQ(out[W(3)], 0x02);  // R2 digital only
    CHECK_EQ(out[W(18)], 10);   // L2 pressure
    CHECK_EQ(out[W(19)], 200);  // R2 pressure
    CHECK_EQ(out[W(6)], 0);
    CHECK_EQ(out[W(7)], 255);
    CHECK_EQ(out[W(8)], 7);
    CHECK_EQ(out[W(9)], 250);
}

static void test_output_parse(void) {
    // Payload after report ID 0x01: pad, r_dur, r_on, l_dur, l_force, ...
    uint8_t buf[48] = {0x00, 0xfe, 0x01, 0xfe, 0x80};
    rumble_state_t r;
    assert(ds3_parse_output(buf, sizeof(buf), &r));
    CHECK_EQ(r.weak, 0xFF);
    CHECK_EQ(r.strong, 0x80);

    uint8_t off[48] = {0};
    assert(ds3_parse_output(off, sizeof(off), &r));
    CHECK_EQ(r.weak, 0);
    CHECK_EQ(r.strong, 0);

    assert(!ds3_parse_output(buf, 4, &r));
}

int main(void) {
    test_neutral();
    test_buttons();
    test_triggers_and_sticks();
    test_output_parse();
    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    puts("all ds3_report tests passed");
    return 0;
}
