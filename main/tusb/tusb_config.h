// TinyUSB configuration for this adapter (device only, full speed).
// Injected into the TinyUSB component's include path by main/CMakeLists.txt.
#pragma once

#include "tusb_option.h"

#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)
#define CFG_TUSB_OS OPT_OS_FREERTOS
#define CFG_TUD_DWC2_SLAVE_ENABLE 1
#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN TU_ATTR_ALIGNED(4)
#define CFG_TUD_ENDPOINT0_SIZE 64
#define CFG_TUSB_DEBUG 0
#define CFG_TUSB_DEBUG_PRINTF esp_rom_printf

// DualShock 3 mode: one HID interface per player (up to 4).
#define CFG_TUD_HID 4
#define CFG_TUD_HID_EP_BUFSIZE 64

// Xbox 360 mode uses an application class driver (x360_usb.c), not a built-in class.
#define CFG_TUD_CDC 0
#define CFG_TUD_MSC 0
#define CFG_TUD_MIDI 0
#define CFG_TUD_VENDOR 0
#define CFG_TUD_ECM_RNDIS 0
#define CFG_TUD_NCM 0
#define CFG_TUD_DFU 0
#define CFG_TUD_DFU_RUNTIME 0
#define CFG_TUD_BTH 0
#define CFG_TUD_AUDIO 0
#define CFG_TUD_VIDEO 0
#define CFG_TUD_PRINTER 0
#define CFG_TUD_USBTMC 0
#define CFG_TUD_MTP 0
