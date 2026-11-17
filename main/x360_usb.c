// USB personality: wired Xbox 360 controller (VID 045E / PID 028E) for the Xbox 360 console.
//
// Descriptors follow a real wired controller (as used by GP2040-CE, MIT). The console
// authenticates wired controllers with "Xbox Security Method 3"; libxsm3 (LGPL-2.1)
// answers those challenges, so no real controller is needed for passthrough.

#include "x360_usb.h"

#include <stdio.h>
#include <string.h>

#include "device/usbd_pvt.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pad_state.h"
#include "tusb.h"
#include "usb_out.h"
#include "x360_report.h"
#include "xsm3.h"

static const char* TAG = "x360_usb";

#define REPORT_INTERVAL_MS 4
#define REPORT_KEEPALIVE_MS 100
#define OUT_PACKET_SIZE 32

// XSM3 vendor requests (on the security interface)
enum {
    XSM3_GET_SERIAL = 0x81,      // IN  29 bytes: identification data
    XSM3_INIT_AUTH = 0x82,       // OUT 34 bytes: challenge init
    XSM3_GET_RESPONSE = 0x83,    // IN  46 / 22 bytes: response to the last challenge
    XSM3_KEEPALIVE = 0x84,       // IN  0 bytes
    XSM3_GET_STATE = 0x86,       // IN  2 bytes: 1 = busy, 2 = response ready
    XSM3_VERIFY_AUTH = 0x87,     // OUT 22 bytes: challenge verify
};
#define XSM3_INIT_LEN 34
#define XSM3_INIT_RESPONSE_LEN 46
#define XSM3_VERIFY_LEN 22
#define XSM3_VERIFY_RESPONSE_LEN 22
#define XSM3_ID_LEN 29

static const tusb_desc_device_t s_device_desc = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0xFF,
    .bDeviceSubClass = 0xFF,
    .bDeviceProtocol = 0xFF,
    .bMaxPacketSize0 = 64,
    .idVendor = 0x045E,
    .idProduct = 0x028E,
    .bcdDevice = 0x0114,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 3,
    .bNumConfigurations = 1,
};

// 4 interfaces: gamepad, audio, plug-in module, security. Only the gamepad's
// endpoints are used; the others exist because the console expects them.
static const uint8_t s_config_desc[] = {
    0x09, 0x02, 0x99, 0x00, 0x04, 0x01, 0x00, 0xA0, 0xFA,  // config: 153 bytes, remote wakeup, 500 mA

    // Interface 0: gamepad (FF/5D/01)
    0x09, 0x04, 0x00, 0x00, 0x02, 0xFF, 0x5D, 0x01, 0x00,
    0x11, 0x21, 0x00, 0x01, 0x01, 0x25, 0x81, 0x14, 0x00, 0x00, 0x00, 0x00, 0x13, 0x02, 0x08, 0x00, 0x00,
    0x07, 0x05, 0x81, 0x03, 0x20, 0x00, 0x01,  // EP 0x81 IN, 32 bytes, 1 ms
    0x07, 0x05, 0x02, 0x03, 0x20, 0x00, 0x08,  // EP 0x02 OUT, 32 bytes, 8 ms

    // Interface 1: audio (FF/5D/03)
    0x09, 0x04, 0x01, 0x00, 0x04, 0xFF, 0x5D, 0x03, 0x00,
    0x1B, 0x21, 0x00, 0x01, 0x01, 0x01, 0x83, 0x40, 0x01, 0x04, 0x20, 0x16, 0x85, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x16, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x07, 0x05, 0x83, 0x03, 0x20, 0x00, 0x02,
    0x07, 0x05, 0x04, 0x03, 0x20, 0x00, 0x04,
    0x07, 0x05, 0x85, 0x03, 0x20, 0x00, 0x40,
    0x07, 0x05, 0x06, 0x03, 0x20, 0x00, 0x10,

    // Interface 2: plug-in module (FF/5D/02)
    0x09, 0x04, 0x02, 0x00, 0x01, 0xFF, 0x5D, 0x02, 0x00,
    0x09, 0x21, 0x00, 0x01, 0x01, 0x22, 0x86, 0x03, 0x00,
    0x07, 0x05, 0x86, 0x03, 0x20, 0x00, 0x10,

    // Interface 3: security (FF/FD/13), string 4 = XSM3 identification
    0x09, 0x04, 0x03, 0x00, 0x00, 0xFF, 0xFD, 0x13, 0x04,
    0x06, 0x41, 0x00, 0x01, 0x01, 0x03,
};
_Static_assert(sizeof(s_config_desc) == 0x99, "Xbox 360 configuration descriptor must be 153 bytes");

static char s_serial[8] = "08FEC93";  // last 6 chars replaced with the chip MAC at init
static const char* const s_strings[] = {
    NULL,  // index 0: language (handled by usb_out)
    "\xA9Microsoft Corporation",
    "Controller",
    s_serial,
    "Xbox Security Method 3, Version 1.00, \xA9 2005 Microsoft Corporation. All rights reserved.",
};

// ---- State ----

static uint8_t s_ep_in, s_ep_out;
CFG_TUSB_MEM_SECTION CFG_TUSB_MEM_ALIGN static uint8_t s_out_buf[OUT_PACKET_SIZE];
CFG_TUSB_MEM_SECTION CFG_TUSB_MEM_ALIGN static uint8_t s_in_buf[X360_INPUT_LEN];
static uint8_t s_ctrl_buf[64];
static volatile uint8_t s_player;  // from the console's LED command, 0 = unknown
static volatile bool s_authenticated;
static uint8_t s_response_len;     // length of the pending XSM3 response, 0 = none

// ---- TinyUSB class driver for the vendor interfaces ----

static void drv_init(void) {}

static bool drv_deinit(void) {
    return true;
}

static void drv_reset(uint8_t rhport) {
    (void)rhport;
    s_ep_in = s_ep_out = 0;
    s_player = 0;
    s_authenticated = false;
    s_response_len = 0;
}

static uint16_t drv_open(uint8_t rhport, tusb_desc_interface_t const* itf, uint16_t max_len) {
    if (itf->bInterfaceClass != TUSB_CLASS_VENDOR_SPECIFIC) return 0;

    uint16_t len = sizeof(tusb_desc_interface_t) + itf->bNumEndpoints * sizeof(tusb_desc_endpoint_t);
    uint8_t const* p = tu_desc_next(itf);
    // Each interface carries one vendor descriptor (0x21, or 0x41 for security) before its endpoints.
    if (p[1] != 0x21 && p[1] != 0x41) return 0;
    len += p[0];
    TU_VERIFY(max_len >= len, 0);

    if (itf->bInterfaceSubClass == 0x5D && itf->bInterfaceProtocol == 0x01) {
        // Gamepad interface: open its interrupt endpoints and start listening for commands.
        TU_ASSERT(usbd_open_edpt_pair(rhport, tu_desc_next(p), itf->bNumEndpoints, TUSB_XFER_INTERRUPT, &s_ep_out,
                                      &s_ep_in),
                  0);
        usbd_edpt_xfer(rhport, s_ep_out, s_out_buf, sizeof(s_out_buf), false);
    }
    return len;
}

static bool drv_control_xfer(uint8_t rhport, uint8_t stage, tusb_control_request_t const* request) {
    (void)rhport;
    (void)stage;
    (void)request;
    return true;  // standard/class requests to these interfaces: accept
}

static bool drv_xfer(uint8_t rhport, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes) {
    if (ep_addr == s_ep_out) {
        if (result == XFER_RESULT_SUCCESS) {
            rumble_state_t rumble;
            uint8_t player;
            switch (x360_parse_output(s_out_buf, (uint16_t)xferred_bytes, &rumble, &player)) {
                case X360_OUT_RUMBLE:
                    rumble_state_set(0, rumble);
                    break;
                case X360_OUT_LED:
                    if (player != s_player) ESP_LOGI(TAG, "console assigned player %d", player);
                    s_player = player;
                    usb_out_host_confirmed();  // Xbox 360 (or Windows) talks XInput
                    break;
                default:
                    break;
            }
        }
        usbd_edpt_xfer(rhport, s_ep_out, s_out_buf, sizeof(s_out_buf), false);
    }
    return true;
}

static const usbd_class_driver_t s_driver = {
    .name = "XINPUT",
    .init = drv_init,
    .deinit = drv_deinit,
    .reset = drv_reset,
    .open = drv_open,
    .control_xfer_cb = drv_control_xfer,
    .xfer_cb = drv_xfer,
    .sof = NULL,
};

const usbd_class_driver_t* x360_class_driver(void) {
    return &s_driver;
}

// ---- XSM3 authentication (vendor control requests) ----

bool x360_vendor_control(uint8_t rhport, uint8_t stage, tusb_control_request_t const* req) {
    if (req->bmRequestType_bit.direction == TUSB_DIR_IN) {
        if (stage != CONTROL_STAGE_SETUP) return true;
        uint16_t len = 0;
        switch (req->bRequest) {
            case XSM3_GET_SERIAL:
                ESP_LOGI(TAG, "console started authentication");
                usb_out_host_confirmed();  // only an Xbox 360 sends XSM3 requests
                xsm3_initialise_state();
                xsm3_set_identification_data(xsm3_id_data_ms_controller);
                memcpy(s_ctrl_buf, xsm3_id_data_ms_controller, XSM3_ID_LEN);
                len = XSM3_ID_LEN;
                break;
            case XSM3_GET_RESPONSE:
                if (s_response_len == 0) return false;  // nothing to answer: stall
                memcpy(s_ctrl_buf, xsm3_challenge_response, s_response_len);
                len = s_response_len;
                if (len == XSM3_VERIFY_RESPONSE_LEN && !s_authenticated) {
                    s_authenticated = true;
                    ESP_LOGI(TAG, "authenticated with the Xbox 360");
                }
                break;
            case XSM3_KEEPALIVE:
                len = 0;
                break;
            case XSM3_GET_STATE: {
                uint16_t state = s_response_len ? 2 : 1;
                memcpy(s_ctrl_buf, &state, sizeof(state));
                len = sizeof(state);
                break;
            }
            default:
                break;  // unknown IN request: answer with no data
        }
        return tud_control_xfer(rhport, req, s_ctrl_buf, len < req->wLength ? len : req->wLength);
    }

    // OUT requests carrying a challenge
    bool init = req->bRequest == XSM3_INIT_AUTH && req->wLength == XSM3_INIT_LEN;
    bool verify = req->bRequest == XSM3_VERIFY_AUTH && req->wLength == XSM3_VERIFY_LEN;
    if (!init && !verify) return false;

    if (stage == CONTROL_STAGE_SETUP) {
        s_response_len = 0;
        return tud_control_xfer(rhport, req, s_ctrl_buf, req->wLength);
    }
    if (stage == CONTROL_STAGE_DATA) {
        // A few ms of DES/SHA work; the console polls XSM3_GET_STATE until it's done.
        if (init) {
            xsm3_do_challenge_init(s_ctrl_buf);
            s_response_len = XSM3_INIT_RESPONSE_LEN;
        } else {
            xsm3_do_challenge_verify(s_ctrl_buf);
            s_response_len = XSM3_VERIFY_RESPONSE_LEN;
        }
    }
    return true;
}

// ---- Input reports ----

static void report_task(void* arg) {
    (void)arg;
    uint8_t last[X360_INPUT_LEN] = {0};
    TickType_t last_sent = 0;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(REPORT_INTERVAL_MS));
        if (!tud_mounted() || s_ep_in == 0) continue;
        if (tud_suspended()) {
            if (pad_state_get(0).buttons & PAD_PS) tud_remote_wakeup();
            continue;
        }

        uint8_t report[X360_INPUT_LEN];
        pad_state_t pad = pad_state_get(0);
        x360_build_input(&pad, report);

        TickType_t now = xTaskGetTickCount();
        bool changed = memcmp(report, last, sizeof(report)) != 0;
        if (!changed && (now - last_sent) < pdMS_TO_TICKS(REPORT_KEEPALIVE_MS)) continue;

        if (usbd_edpt_claim(0, s_ep_in)) {
            memcpy(s_in_buf, report, sizeof(report));
            if (usbd_edpt_xfer(0, s_ep_in, s_in_buf, sizeof(s_in_buf), false)) {
                memcpy(last, report, sizeof(report));
                last_sent = now;
            }
            usbd_edpt_release(0, s_ep_in);
        }
    }
}

// ---- Personality interface (used by usb_out.c) ----

const uint8_t* x360_device_descriptor(void) {
    return (const uint8_t*)&s_device_desc;
}

const uint8_t* x360_config_descriptor(void) {
    return s_config_desc;
}

const char* x360_string(uint8_t index) {
    return index < sizeof(s_strings) / sizeof(s_strings[0]) ? s_strings[index] : NULL;
}

int x360_player(void) {
    return s_player;
}

bool x360_authenticated(void) {
    return s_authenticated;
}

void x360_init(void) {
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_BT));
    snprintf(&s_serial[1], sizeof(s_serial) - 1, "%02X%02X%02X", mac[3], mac[4], mac[5]);
}

void x360_start(void) {
    xTaskCreatePinnedToCore(report_task, "x360_report", 4096, NULL, 5, NULL, 1);
    ESP_LOGI(TAG, "Xbox 360 wired controller started");
}
