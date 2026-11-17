// Xbox Series X|S controller (Bluetooth LE) -> wired controller for PS3 (DualShock 3)
// or Xbox 360 (wired Xbox 360 controller), over the USB port.
// Target: ESP32-S3.

#include <btstack_port_esp32.h>
#include <btstack_run_loop.h>
#include <btstack_stdio_esp32.h>
#include <uni.h>

#include "adapter_config.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "status_led.h"
#include "usb_out.h"
#include "web_config.h"
#include "xbox_bt.h"

#ifndef CONFIG_BLUEPAD32_PLATFORM_CUSTOM
#error "Requires CONFIG_BLUEPAD32_PLATFORM_CUSTOM=y (see sdkconfig.defaults)"
#endif
#ifndef CONFIG_IDF_TARGET_ESP32S3
#error "This adapter needs the ESP32-S3 (native USB device + BLE)"
#endif

static void init_nvs(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void app_main(void) {
    init_nvs();  // settings, Wi-Fi and BTstack (paired controller keys) all use NVS
    adapter_config_load();

    // USB first, so the console sees a controller even before the Xbox pad connects.
    usb_out_start();

#ifdef CONFIG_ADAPTER_WIFI_ENABLE
    web_config_start();
#endif
#ifdef CONFIG_ADAPTER_STATUS_LED
    status_led_start();
#endif

#ifndef CONFIG_BLUEPAD32_USB_CONSOLE_ENABLE
    btstack_stdio_init();
#endif
    btstack_init();
    uni_platform_set_custom(xbox_bt_platform());
    uni_init(0, NULL);

    // Runs the Bluetooth stack forever.
    btstack_run_loop_execute();
}
