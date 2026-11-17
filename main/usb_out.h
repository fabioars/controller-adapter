// The ESP32-S3's USB port, seen by the console as a DualShock 3 (PS3) or a wired
// Xbox 360 controller, depending on the output setting / console detection.
#pragma once

#include <stdbool.h>

#include "adapter_config.h"

// Starts TinyUSB with the personality for adapter_config_console().
void usb_out_start(void);

bool usb_out_is_mounted(void);  // a host has configured the device
console_t usb_out_console(void);

// Called by a personality when the host did something only the expected console does
// (PS3: DS3 feature reports; Xbox 360: security handshake / LED command). In auto mode an
// unconfirmed host makes the adapter retry as the other console once.
void usb_out_host_confirmed(void);
bool usb_out_is_host_confirmed(void);
