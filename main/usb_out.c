// USB device setup shared by both personalities: PHY, TinyUSB task, descriptor
// callbacks, and console auto-detection.

#include "usb_out.h"

#include <string.h>

#include "ds3_usb.h"
#include "esp_log.h"
#include "esp_private/usb_phy.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pad_state.h"
#include "tusb.h"
#include "x360_usb.h"

static const char* TAG = "usb_out";

// Auto mode: how long a host may stay silent before we assume it's the other console.
// Configured, but no console-specific request: the PS3 asks for DS3 reports at once;
// the Xbox 360 sets the player LED / starts authentication within a few seconds.
#define DETECT_AFTER_MOUNT_PS3_MS 4000
#define DETECT_AFTER_MOUNT_X360_MS 10000
#define DETECT_UNMOUNTED_MS 20000   // never configured at all (console may ignore the device)

static console_t s_console;
static usb_phy_handle_t s_phy;
static volatile bool s_confirmed;
static volatile int64_t s_mounted_at_us;  // 0 = not mounted

// ---- Descriptor callbacks ----

uint8_t const* tud_descriptor_device_cb(void) {
    return s_console == CONSOLE_X360 ? x360_device_descriptor() : ds3_device_descriptor();
}

uint8_t const* tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return s_console == CONSOLE_X360 ? x360_config_descriptor() : ds3_config_descriptor();
}

uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    static uint16_t desc[128];  // header + up to 127 UTF-16 chars
    size_t count;
    if (index == 0) {
        desc[1] = 0x0409;  // English (US)
        count = 1;
    } else {
        const char* str = s_console == CONSOLE_X360 ? x360_string(index) : ds3_string(index);
        if (str == NULL) return NULL;
        count = strnlen(str, 127);
        for (size_t i = 0; i < count; i++) desc[1 + i] = (uint8_t)str[i];  // Latin-1 -> UTF-16
    }
    desc[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * count + 2));
    return desc;
}

// ---- Hooks for the Xbox 360 personality ----

usbd_class_driver_t const* usbd_app_driver_get_cb(uint8_t* driver_count) {
    if (s_console == CONSOLE_X360) {
        *driver_count = 1;
        return x360_class_driver();
    }
    *driver_count = 0;
    return NULL;
}

bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const* request) {
    return s_console == CONSOLE_X360 && x360_vendor_control(rhport, stage, request);
}

// ---- Mount state ----

void tud_mount_cb(void) {
    s_mounted_at_us = esp_timer_get_time();
    ESP_LOGI(TAG, "configured by host");
}

void tud_umount_cb(void) {
    s_mounted_at_us = 0;
    ESP_LOGI(TAG, "unconfigured");
    for (int i = 0; i < MAX_PLAYERS; i++) rumble_state_set(i, (rumble_state_t){0});
}

bool usb_out_is_mounted(void) {
    return tud_mounted();
}

console_t usb_out_console(void) {
    return s_console;
}

void usb_out_host_confirmed(void) {
    if (!s_confirmed) {
        s_confirmed = true;
        ESP_LOGI(TAG, "host confirmed as %s", s_console == CONSOLE_X360 ? "Xbox 360" : "PS3");
        // NVS write happens in usb_task (not from the USB callback context).
    }
}

bool usb_out_is_host_confirmed(void) {
    return s_confirmed;
}

// ---- Tasks ----

static void usb_task(void* arg) {
    (void)arg;
    for (;;) {
        tud_task();  // blocks until there is USB work
    }
}

static void detect_task(void* arg) {
    (void)arg;
    bool remembered = false;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(250));
        if (s_confirmed) {
            if (!remembered && adapter_config_output_setting() == OUTPUT_AUTO) {
                adapter_config_remember_console(s_console);
            }
            remembered = true;
            continue;
        }
        if (adapter_config_output_setting() != OUTPUT_AUTO) continue;

        int64_t now = esp_timer_get_time();
        int64_t mounted = s_mounted_at_us;
        int64_t limit = s_console == CONSOLE_X360 ? DETECT_AFTER_MOUNT_X360_MS : DETECT_AFTER_MOUNT_PS3_MS;
        bool silent_mounted = mounted && (now - mounted) / 1000 > limit;
        bool never_mounted = !mounted && now / 1000 > DETECT_UNMOUNTED_MS;
        if (silent_mounted || never_mounted) {
            adapter_config_try_other_console();  // restarts, unless already tried since power-up
            vTaskDelete(NULL);                   // gave up: stay in this mode until power-off
        }
    }
}

void usb_out_start(void) {
    s_console = adapter_config_console();
    if (s_console == CONSOLE_X360) {
        x360_init();
    } else {
        ds3_init();
    }

    usb_phy_config_t phy = {
        .controller = USB_PHY_CTRL_OTG,
        .target = USB_PHY_TARGET_INT,
        .otg_mode = USB_OTG_MODE_DEVICE,
    };
    ESP_ERROR_CHECK(usb_new_phy(&phy, &s_phy));
    if (!tusb_init()) {
        ESP_LOGE(TAG, "TinyUSB init failed");
        return;
    }
    // The XSM3 challenge maths runs inside USB control callbacks: give it room.
    xTaskCreatePinnedToCore(usb_task, "tinyusb", 8192, NULL, 5, NULL, 0);
    xTaskCreate(detect_task, "usb_detect", 3072, NULL, 2, NULL);

    if (s_console == CONSOLE_X360) {
        x360_start();
    } else {
        ds3_start();
    }
}
