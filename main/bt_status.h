// Snapshot of the Bluetooth side, written by the BTstack thread and read by the web server.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "pad_state.h"

#define BT_STATUS_MAX_BONDS 8
#define BT_STATUS_NAME_LEN 32

typedef struct {
    uint8_t addr[6];
    uint8_t addr_type;
} bt_bond_t;

typedef struct {
    bool connected;
    char name[BT_STATUS_NAME_LEN];
    uint8_t addr[6];
    uint8_t battery;  // 0 = unknown, 1 = empty .. 255 = full
} bt_player_t;

typedef struct {
    bool scanning;
    bt_player_t players[MAX_PLAYERS];  // index = player number - 1
    int num_bonds;
    bt_bond_t bonds[BT_STATUS_MAX_BONDS];
} bt_status_t;

void bt_status_set(const bt_status_t* s);
bt_status_t bt_status_get(void);
