// Bluetooth side: Bluepad32 platform that accepts a single Xbox Series X|S controller.
#pragma once

#include <stdint.h>

struct uni_platform;

struct uni_platform* xbox_bt_platform(void);

// Safe to call from any task: the work is queued onto the BTstack thread.
void xbox_bt_request_pair(void);  // scan for a new controller (frees the last slot if all are taken)
void xbox_bt_request_disconnect(int player);  // player: 0-based
void xbox_bt_request_forget(const uint8_t addr[6], uint8_t addr_type);
void xbox_bt_request_forget_all(void);
