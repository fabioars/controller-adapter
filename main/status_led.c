// Status on the board's WS2812 RGB LED:
//   blue blinking   searching for a controller (alternates with green/yellow
//                   when some players are already connected)
//   yellow          controller(s) connected, console not connected (yet)
//   green           controller(s) and console connected: ready to play
//   dim red         idle (not searching, no controller)

#include "status_led.h"

#include "bt_status.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "sdkconfig.h"
#include "usb_out.h"

#define LEVEL 24  // brightness 0..255; these LEDs are very bright

static const char* TAG = "led";
static led_strip_handle_t s_strip;

static void set(uint8_t r, uint8_t g, uint8_t b) {
    led_strip_set_pixel(s_strip, 0, r, g, b);
    led_strip_refresh(s_strip);
}

static void led_task(void* arg) {
    (void)arg;
    for (int tick = 0;; tick++) {
        bt_status_t bt = bt_status_get();
        bool usb = usb_out_is_mounted();
        bool any = false;
        for (int i = 0; i < MAX_PLAYERS; i++) any |= bt.players[i].connected;
        bool blink_on = (tick / 5) % 2;  // 1 Hz

        if (bt.scanning && blink_on) {
            set(0, 0, LEVEL);
        } else if (any) {
            usb ? set(0, LEVEL, 0) : set(LEVEL, LEVEL * 2 / 3, 0);
        } else if (bt.scanning) {
            set(0, 0, 0);
        } else {
            set(LEVEL / 3, 0, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void status_led_start(void) {
    led_strip_config_t strip_cfg = {
        .strip_gpio_num = CONFIG_ADAPTER_STATUS_LED_GPIO,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };
    led_strip_rmt_config_t rmt_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
    };
    esp_err_t err = led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "no status LED (%s)", esp_err_to_name(err));
        return;
    }
    xTaskCreate(led_task, "status_led", 2560, NULL, 2, NULL);
}
