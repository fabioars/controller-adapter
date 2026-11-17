// DualShock 3 USB device emulation.
//
// Descriptors and feature-report replies match a real DS3 (VID 054C / PID 0268).
// The feature replies are the ones GP2040-CE (MIT) uses with TinyUSB and are known
// to satisfy the PS3's handshake. Note TinyUSB prepends the report ID to GET_REPORT
// replies, so none of the tables below include it.

#include "ds3_usb.h"

#include <string.h>

#include "adapter_config.h"
#include "class/hid/hid_device.h"
#include "ds3_report.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pad_state.h"
#include "tusb.h"
#include "usb_out.h"

static const char* TAG = "ds3_usb";

#define DS3_VID 0x054C
#define DS3_PID 0x0268
#define REPORT_INTERVAL_MS 4    // DS3 is polled at 1 ms; 4 ms is plenty for Bluetooth input
#define REPORT_KEEPALIVE_MS 100  // resend unchanged state at this rate

static const tusb_desc_device_t s_device_desc = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = 64,
    .idVendor = DS3_VID,
    .idProduct = DS3_PID,
    .bcdDevice = 0x0100,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 0,
    .bNumConfigurations = 1,
};

static const char* const s_strings[] = {
    NULL,  // index 0: language (handled by usb_out)
    "Sony",
    "PLAYSTATION(R)3 Controller",
};

static const uint8_t s_report_desc[] = {
    0x05, 0x01,              // Usage Page (Generic Desktop)
    0x09, 0x04,              // Usage (Joystick)
    0xA1, 0x01,              // Collection (Application)
    0xA1, 0x02,              //   Collection (Logical)
    0x85, 0x01,              //     Report ID (1)
    0x75, 0x08, 0x95, 0x01,  //     8 bits x1
    0x15, 0x00, 0x26, 0xFF, 0x00,
    0x81, 0x03,              //     Input (Const) - reserved byte
    0x75, 0x01, 0x95, 0x13,  //     1 bit x19
    0x15, 0x00, 0x25, 0x01, 0x35, 0x00, 0x45, 0x01,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x13,
    0x81, 0x02,              //     Input - 19 buttons
    0x75, 0x01, 0x95, 0x0D,  //     1 bit x13
    0x06, 0x00, 0xFF,
    0x81, 0x03,              //     Input (Const) - padding
    0x15, 0x00, 0x26, 0xFF, 0x00,
    0x05, 0x01, 0x09, 0x01,
    0xA1, 0x00,              //     Collection (Physical)
    0x75, 0x08, 0x95, 0x04,
    0x35, 0x00, 0x46, 0xFF, 0x00,
    0x09, 0x30, 0x09, 0x31, 0x09, 0x32, 0x09, 0x35,
    0x81, 0x02,              //       Input - 4 stick axes
    0xC0,                    //     End Collection
    0x05, 0x01,
    0x75, 0x08, 0x95, 0x27, 0x09, 0x01,
    0x81, 0x02,              //     Input - 39 bytes (pressures, status, sixaxis)
    0x75, 0x08, 0x95, 0x30, 0x09, 0x01,
    0x91, 0x02,              //     Output - 48 bytes (rumble, LEDs)
    0x75, 0x08, 0x95, 0x30, 0x09, 0x01,
    0xB1, 0x02,              //     Feature - 48 bytes
    0xC0,                    //   End Collection
    0xA1, 0x02, 0x85, 0x02, 0x75, 0x08, 0x95, 0x30, 0x09, 0x01, 0xB1, 0x02, 0xC0,  // Feature 0x02
    0xA1, 0x02, 0x85, 0xEE, 0x75, 0x08, 0x95, 0x30, 0x09, 0x01, 0xB1, 0x02, 0xC0,  // Feature 0xEE
    0xA1, 0x02, 0x85, 0xEF, 0x75, 0x08, 0x95, 0x30, 0x09, 0x01, 0xB1, 0x02, 0xC0,  // Feature 0xEF
    0xC0,                    // End Collection
};
_Static_assert(sizeof(s_report_desc) == 148, "DS3 report descriptor must be 148 bytes");

// Configuration descriptor, built at boot for the configured number of players.
// One player = exactly a real DS3 (one HID interface, EP 0x02 OUT + EP 0x81 IN).
// Several players (experimental) = one HID interface per player. The ESP32-S3 has
// 4 IN endpoints besides EP0 and 6 endpoint numbers, so with 4 players the OUT
// endpoints are dropped and the PS3 sends rumble over the control pipe instead.
static uint8_t s_config_desc[9 + MAX_PLAYERS * (9 + 9 + 7 + 7)];
static int s_players = 1;

static void build_config_desc(int players) {
    const bool with_out = players <= 3;
    uint8_t* p = s_config_desc;
    uint16_t total = 9 + players * (9 + 9 + 7 + (with_out ? 7 : 0));

    // Configuration: bus powered, 500 mA
    const uint8_t cfg[] = {9, TUSB_DESC_CONFIGURATION, U16_TO_U8S_LE(total), (uint8_t)players, 1, 0, 0x80, 250};
    memcpy(p, cfg, sizeof(cfg));
    p += sizeof(cfg);

    for (int i = 0; i < players; i++) {
        const uint8_t itf[] = {
            // Interface i: HID
            9, TUSB_DESC_INTERFACE, (uint8_t)i, 0, (uint8_t)(with_out ? 2 : 1), TUSB_CLASS_HID, 0, 0, 0,
            // HID descriptor, bcdHID 1.11, report descriptor length 148
            9, HID_DESC_TYPE_HID, U16_TO_U8S_LE(0x0111), 0, 1, HID_DESC_TYPE_REPORT,
            U16_TO_U8S_LE(sizeof(s_report_desc)),
        };
        memcpy(p, itf, sizeof(itf));
        p += sizeof(itf);
        if (with_out) {
            // OUT 0x02, 0x03, 0x04: interrupt, 64 bytes, 1 ms
            const uint8_t ep_out[] = {7, TUSB_DESC_ENDPOINT, (uint8_t)(0x02 + i), TUSB_XFER_INTERRUPT,
                                      U16_TO_U8S_LE(64), 1};
            memcpy(p, ep_out, sizeof(ep_out));
            p += sizeof(ep_out);
        }
        // IN 0x81, 0x82, ...: interrupt, 64 bytes, 1 ms
        const uint8_t ep_in[] = {7, TUSB_DESC_ENDPOINT, (uint8_t)(0x81 + i), TUSB_XFER_INTERRUPT, U16_TO_U8S_LE(64), 1};
        memcpy(p, ep_in, sizeof(ep_in));
        p += sizeof(ep_in);
    }
}

// ---- Feature reports the PS3 queries during setup ----

enum {
    FEATURE_01 = 0x01,
    FEATURE_EF = 0xEF,
    FEATURE_F2 = 0xF2,  // controller Bluetooth address
    FEATURE_F4 = 0xF4,  // "enable reports" command (host -> device)
    FEATURE_F5 = 0xF5,  // paired host (console) Bluetooth address
    FEATURE_F7 = 0xF7,
    FEATURE_F8 = 0xF8,
};

static const uint8_t s_feature_01[] = {
    0x01, 0x04, 0x00, 0x0b, 0x0c, 0x01, 0x02, 0x18, 0x18, 0x18, 0x18, 0x09, 0x0a, 0x10, 0x11, 0x12,
    0x13, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x02, 0x02, 0x02, 0x02, 0x00, 0x00, 0x00, 0x04, 0x04,
    0x04, 0x04, 0x00, 0x00, 0x04, 0x00, 0x01, 0x02, 0x07, 0x00, 0x17, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const uint8_t s_feature_ef[] = {
    0xef, 0x04, 0x00, 0x0b, 0x03, 0x01, 0xa0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0xff, 0x01, 0xff, 0x01, 0xff, 0x01, 0xff, 0x01, 0xff, 0x01, 0xff, 0x01, 0xff, 0x01, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06,
};

static const uint8_t s_feature_f7[] = {
    0x02, 0x01, 0xf8, 0x02, 0xe2, 0x01, 0x05, 0xff, 0x04, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const uint8_t s_feature_f8[] = {
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// F2 reply per player: ff ff, 00 + device address, 00 + host address, 00.
static uint8_t s_feature_f2[MAX_PLAYERS][17];
// Byte the PS3 writes into feature 0xEF; it expects it echoed back in 0xEF/0xF8.
static volatile uint8_t s_ef_byte[MAX_PLAYERS];

static uint16_t reply(uint8_t* buf, uint16_t reqlen, const uint8_t* src, uint16_t srclen) {
    uint16_t n = reqlen < srclen ? reqlen : srclen;
    memcpy(buf, src, n);
    return n;
}

uint8_t const* tud_hid_descriptor_report_cb(uint8_t instance) {
    (void)instance;
    return s_report_desc;
}

uint16_t tud_hid_get_report_cb(uint8_t instance,
                               uint8_t report_id,
                               hid_report_type_t report_type,
                               uint8_t* buffer,
                               uint16_t reqlen) {
    const int player = instance;
    if (player >= s_players) return 0;
    if (report_type == HID_REPORT_TYPE_INPUT) {
        uint8_t payload[DS3_INPUT_PAYLOAD_LEN];
        pad_state_t pad = pad_state_get(player);
        ds3_build_input(&pad, payload);
        return reply(buffer, reqlen, payload, sizeof(payload));
    }
    if (report_type != HID_REPORT_TYPE_FEATURE) {
        return 0;
    }
    usb_out_host_confirmed();  // only a PS3 (or Linux's DS3 driver) asks for these

    uint16_t n;
    switch (report_id) {
        case FEATURE_01:
            return reply(buffer, reqlen, s_feature_01, sizeof(s_feature_01));
        case FEATURE_EF:
            n = reply(buffer, reqlen, s_feature_ef, sizeof(s_feature_ef));
            if (n > 6) buffer[6] = s_ef_byte[player];
            return n;
        case FEATURE_F2:
            return reply(buffer, reqlen, s_feature_f2[player], sizeof(s_feature_f2[player]));
        case FEATURE_F5:
            // Host address starting at byte 1 (same layout GP2040-CE uses); zero-filled.
            n = reqlen < 48 ? reqlen : 48;
            memset(buffer, 0, n);
            for (int i = 0; i < 6 && 1 + i < n; i++) buffer[1 + i] = s_feature_f2[player][9 + i];
            return n;
        case FEATURE_F7:
            return reply(buffer, reqlen, s_feature_f7, sizeof(s_feature_f7));
        case FEATURE_F8:
            n = reply(buffer, reqlen, s_feature_f8, sizeof(s_feature_f8));
            if (n > 6) buffer[6] = s_ef_byte[player];
            return n;
        default:
            ESP_LOGD(TAG, "unhandled GET_REPORT feature 0x%02x", report_id);
            return 0;  // STALL
    }
}

void tud_hid_set_report_cb(uint8_t instance,
                           uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const* buffer,
                           uint16_t bufsize) {
    const int player = instance;
    if (player >= s_players) return;
    if (report_type == HID_REPORT_TYPE_FEATURE) {
        usb_out_host_confirmed();  // only a PS3 (or Linux's DS3 driver) sends these
        if (report_id == FEATURE_EF && bufsize > 6) {
            s_ef_byte[player] = buffer[6];
        } else if (report_id == FEATURE_F4) {
            ESP_LOGI(TAG, "PS3 enabled controller %d", player + 1);
        }
        return;
    }

    // Output report: via the interrupt OUT endpoint the report ID is still the first
    // byte (report_id == 0); via a control SET_REPORT TinyUSB has already stripped it.
    if (report_id == 0 && bufsize > 0) {
        report_id = buffer[0];
        buffer++;
        bufsize--;
    }
    if (report_id != DS3_REPORT_ID_INPUT) {  // output report shares ID 0x01
        return;
    }
    rumble_state_t rumble;
    if (ds3_parse_output(buffer, bufsize, &rumble)) {
        rumble_state_set(player, rumble);
    }
}

static void report_task(void* arg) {
    (void)arg;
    uint8_t last[MAX_PLAYERS][DS3_INPUT_PAYLOAD_LEN] = {0};
    TickType_t last_sent[MAX_PLAYERS] = {0};

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(REPORT_INTERVAL_MS));
        if (!tud_mounted()) {
            continue;
        }
        if (tud_suspended()) {
            // Any player's PS button wakes the console's USB port.
            for (int i = 0; i < s_players; i++) {
                if (pad_state_get(i).buttons & PAD_PS) tud_remote_wakeup();
            }
            continue;
        }

        TickType_t now = xTaskGetTickCount();
        for (int i = 0; i < s_players; i++) {
            if (!tud_hid_n_ready(i)) {
                continue;
            }
            uint8_t payload[DS3_INPUT_PAYLOAD_LEN];
            pad_state_t pad = pad_state_get(i);
            ds3_build_input(&pad, payload);

            bool changed = memcmp(payload, last[i], sizeof(payload)) != 0;
            if (changed || (now - last_sent[i]) >= pdMS_TO_TICKS(REPORT_KEEPALIVE_MS)) {
                if (tud_hid_n_report(i, DS3_REPORT_ID_INPUT, payload, sizeof(payload))) {
                    memcpy(last[i], payload, sizeof(payload));
                    last_sent[i] = now;
                }
            }
        }
    }
}

// ---- Personality interface (used by usb_out.c) ----

const uint8_t* ds3_device_descriptor(void) {
    return (const uint8_t*)&s_device_desc;
}

const uint8_t* ds3_config_descriptor(void) {
    return s_config_desc;
}

const char* ds3_string(uint8_t index) {
    return index < sizeof(s_strings) / sizeof(s_strings[0]) ? s_strings[index] : NULL;
}

void ds3_init(void) {
    s_players = adapter_config_players();
    build_config_desc(s_players);

    // Each player's DS3 "device address" is the chip's Bluetooth MAC with the
    // player number added to the last byte, so it is stable and unique.
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_BT));
    for (int i = 0; i < MAX_PLAYERS; i++) {
        s_feature_f2[i][0] = 0xff;
        s_feature_f2[i][1] = 0xff;
        memcpy(&s_feature_f2[i][3], mac, 6);
        s_feature_f2[i][8] = (uint8_t)(mac[5] + i);
    }
}

void ds3_start(void) {
    xTaskCreatePinnedToCore(report_task, "ds3_report", 4096, NULL, 5, NULL, 1);
    ESP_LOGI(TAG, "DualShock 3 started (%d player%s)", s_players, s_players > 1 ? "s" : "");
}
