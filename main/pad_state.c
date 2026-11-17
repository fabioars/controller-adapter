#include "pad_state.h"

#include "freertos/FreeRTOS.h"

const pad_state_t PAD_NEUTRAL = {
    .buttons = 0,
    .lx = PAD_STICK_CENTER,
    .ly = PAD_STICK_CENTER,
    .rx = PAD_STICK_CENTER,
    .ry = PAD_STICK_CENTER,
    .l2 = 0,
    .r2 = 0,
};

// The BT and USB tasks may run on different cores, so use a spinlock.
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static pad_state_t s_pad[MAX_PLAYERS] = {
    [0 ... MAX_PLAYERS - 1] = {.lx = PAD_STICK_CENTER, .ly = PAD_STICK_CENTER, .rx = PAD_STICK_CENTER, .ry = PAD_STICK_CENTER},
};
static rumble_state_t s_rumble[MAX_PLAYERS];

static inline bool valid(int player) {
    return player >= 0 && player < MAX_PLAYERS;
}

void pad_state_set(int player, const pad_state_t* s) {
    if (!valid(player)) return;
    taskENTER_CRITICAL(&s_lock);
    s_pad[player] = *s;
    taskEXIT_CRITICAL(&s_lock);
}

pad_state_t pad_state_get(int player) {
    if (!valid(player)) return PAD_NEUTRAL;
    taskENTER_CRITICAL(&s_lock);
    pad_state_t s = s_pad[player];
    taskEXIT_CRITICAL(&s_lock);
    return s;
}

void rumble_state_set(int player, rumble_state_t r) {
    if (!valid(player)) return;
    taskENTER_CRITICAL(&s_lock);
    s_rumble[player] = r;
    taskEXIT_CRITICAL(&s_lock);
}

rumble_state_t rumble_state_get(int player) {
    if (!valid(player)) return (rumble_state_t){0};
    taskENTER_CRITICAL(&s_lock);
    rumble_state_t r = s_rumble[player];
    taskEXIT_CRITICAL(&s_lock);
    return r;
}
