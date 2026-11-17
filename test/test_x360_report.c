// Host unit tests for the Xbox 360 report encoder. Run: ./test/run.sh
#include <stdio.h>
#include <string.h>

#include "x360_report.h"

static const pad_state_t NEUTRAL = {.lx = 128, .ly = 128, .rx = 128, .ry = 128};

static int failures;
#define CHECK_EQ(a, b)                                                                            \
    do {                                                                                          \
        long _a = (long)(a), _b = (long)(b);                                                      \
        if (_a != _b) {                                                                           \
            fprintf(stderr, "%s:%d: %s == %ld, expected %ld\n", __FILE__, __LINE__, #a, _a, _b); \
            failures++;                                                                           \
        }                                                                                         \
    } while (0)

static int16_t s16(const uint8_t* p) {
    return (int16_t)(p[0] | (p[1] << 8));
}

static void test_header_and_neutral(void) {
    uint8_t r[X360_INPUT_LEN];
    x360_build_input(&NEUTRAL, r);
    CHECK_EQ(r[0], 0x00);
    CHECK_EQ(r[1], 20);
    CHECK_EQ(r[2], 0);
    CHECK_EQ(r[3], 0);
    CHECK_EQ(s16(&r[6]), 0);
    CHECK_EQ(s16(&r[8]), 0);
    CHECK_EQ(s16(&r[10]), 0);
    CHECK_EQ(s16(&r[12]), 0);
}

static void test_buttons(void) {
    struct {
        uint16_t pad;
        int byte;
        uint8_t bit;
    } cases[] = {
        {PAD_UP, 2, 0x01},   {PAD_DOWN, 2, 0x02},  {PAD_LEFT, 2, 0x04},   {PAD_RIGHT, 2, 0x08},
        {PAD_START, 2, 0x10}, {PAD_SELECT, 2, 0x20}, {PAD_L3, 2, 0x40},     {PAD_R3, 2, 0x80},
        {PAD_L1, 3, 0x01},   {PAD_R1, 3, 0x02},    {PAD_PS, 3, 0x04},     {PAD_CROSS, 3, 0x10},
        {PAD_CIRCLE, 3, 0x20}, {PAD_SQUARE, 3, 0x40}, {PAD_TRIANGLE, 3, 0x80},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        pad_state_t p = NEUTRAL;
        p.buttons = cases[i].pad;
        uint8_t r[X360_INPUT_LEN];
        x360_build_input(&p, r);
        CHECK_EQ(r[cases[i].byte], cases[i].bit);
        CHECK_EQ(r[cases[i].byte == 2 ? 3 : 2], 0);
    }
}

static void test_sticks_and_triggers(void) {
    pad_state_t p = NEUTRAL;
    p.lx = 0;    // full left
    p.ly = 0;    // full up
    p.rx = 255;  // full right
    p.ry = 255;  // full down
    p.l2 = 77;
    p.r2 = 255;
    uint8_t r[X360_INPUT_LEN];
    x360_build_input(&p, r);
    CHECK_EQ(r[4], 77);
    CHECK_EQ(r[5], 255);
    CHECK_EQ(s16(&r[6]), -32768);  // left = negative X
    CHECK_EQ(s16(&r[8]), 32767);   // up = positive Y
    CHECK_EQ(s16(&r[10]), 32766);  // right = positive X
    CHECK_EQ(s16(&r[12]), -32766); // down = negative Y
}

static void test_output(void) {
    rumble_state_t rumble = {0};
    uint8_t player = 99;
    const uint8_t rumble_pkt[8] = {0x00, 0x08, 0x00, 0xC0, 0x40, 0x00, 0x00, 0x00};
    CHECK_EQ(x360_parse_output(rumble_pkt, sizeof(rumble_pkt), &rumble, &player), X360_OUT_RUMBLE);
    CHECK_EQ(rumble.strong, 0xC0);
    CHECK_EQ(rumble.weak, 0x40);

    for (uint8_t pattern = 0x02; pattern <= 0x09; pattern++) {
        const uint8_t led[3] = {0x01, 0x03, pattern};
        CHECK_EQ(x360_parse_output(led, sizeof(led), &rumble, &player), X360_OUT_LED);
        CHECK_EQ(player, pattern <= 0x05 ? pattern - 1 : pattern - 5);
    }
    const uint8_t blink[3] = {0x01, 0x03, 0x0A};
    x360_parse_output(blink, sizeof(blink), &rumble, &player);
    CHECK_EQ(player, 0);

    const uint8_t junk[2] = {0x02, 0x00};
    CHECK_EQ(x360_parse_output(junk, sizeof(junk), &rumble, &player), X360_OUT_UNKNOWN);
}

int main(void) {
    test_header_and_neutral();
    test_buttons();
    test_sticks_and_triggers();
    test_output();
    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    puts("all x360_report tests passed");
    return 0;
}
