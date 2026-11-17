// USB personality: wired Xbox 360 controller for the Xbox 360 console (one player).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "device/usbd_pvt.h"
#include "tusb.h"

void x360_init(void);   // before TinyUSB starts
void x360_start(void);  // after TinyUSB starts: input report task

const uint8_t* x360_device_descriptor(void);
const uint8_t* x360_config_descriptor(void);
const char* x360_string(uint8_t index);  // NULL = no such string

const usbd_class_driver_t* x360_class_driver(void);
bool x360_vendor_control(uint8_t rhport, uint8_t stage, tusb_control_request_t const* request);

int x360_player(void);          // player number the console assigned (1-4), 0 = not yet
bool x360_authenticated(void);  // the console accepted the controller
