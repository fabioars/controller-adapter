#include "adapter_config.h"

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs.h"
#include "pad_state.h"

static const char* TAG = "config";
static const char* NS = "adapter";

static int s_players_setting = 1;
static output_setting_t s_output = OUTPUT_AUTO;
static console_t s_console = CONSOLE_PS3;

// Survives esp_restart() (not power-up): lets auto mode try the other console once.
#define SWITCH_MAGIC 0x5A3C0000u
RTC_NOINIT_ATTR static uint32_t s_switch;

static uint8_t nvs_read_u8(const char* key, uint8_t def) {
    nvs_handle_t h;
    uint8_t v = def;
    if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, key, &v);
        nvs_close(h);
    }
    return v;
}

static bool nvs_write_u8(const char* key, uint8_t v) {
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t err = nvs_set_u8(h, key, v);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK;
}

static const char* console_name(console_t c) {
    return c == CONSOLE_X360 ? "Xbox 360" : "PS3";
}

void adapter_config_load(void) {
    uint8_t v = nvs_read_u8("players", 1);
    s_players_setting = (v >= 1 && v <= MAX_PLAYERS) ? v : 1;

    v = nvs_read_u8("output", OUTPUT_AUTO);
    s_output = v <= OUTPUT_X360 ? (output_setting_t)v : OUTPUT_AUTO;

    if (esp_reset_reason() == ESP_RST_POWERON) s_switch = 0;

    if (s_output == OUTPUT_PS3) {
        s_console = CONSOLE_PS3;
    } else if (s_output == OUTPUT_X360) {
        s_console = CONSOLE_X360;
    } else if ((s_switch & 0xFFFF0000u) == SWITCH_MAGIC) {
        s_console = (console_t)(s_switch & 0xFF);  // retrying as the other console
    } else {
        v = nvs_read_u8("last_console", CONSOLE_PS3);
        s_console = v == CONSOLE_X360 ? CONSOLE_X360 : CONSOLE_PS3;
    }
    ESP_LOGI(TAG, "output: %s -> %s controller, players: %d", s_output == OUTPUT_AUTO ? "auto" : "fixed",
             console_name(s_console), adapter_config_players());
}

int adapter_config_players(void) {
    return s_console == CONSOLE_PS3 ? s_players_setting : 1;
}

int adapter_config_players_setting(void) {
    return s_players_setting;
}

bool adapter_config_set_players(int players) {
    if (players < 1 || players > MAX_PLAYERS) return false;
    return nvs_write_u8("players", (uint8_t)players);
}

output_setting_t adapter_config_output_setting(void) {
    return s_output;
}

bool adapter_config_set_output(output_setting_t setting) {
    if (setting > OUTPUT_X360) return false;
    s_switch = 0;
    return nvs_write_u8("output", (uint8_t)setting);
}

console_t adapter_config_console(void) {
    return s_console;
}

void adapter_config_remember_console(console_t console) {
    if (nvs_read_u8("last_console", 0) != console) {
        nvs_write_u8("last_console", (uint8_t)console);
        ESP_LOGI(TAG, "remembering %s for next time", console_name(console));
    }
}

bool adapter_config_try_other_console(void) {
    if (s_output != OUTPUT_AUTO || (s_switch & 0xFFFF0000u) == SWITCH_MAGIC) {
        return false;  // fixed mode, or already switched once since power-up
    }
    console_t other = s_console == CONSOLE_PS3 ? CONSOLE_X360 : CONSOLE_PS3;
    ESP_LOGW(TAG, "host doesn't look like a %s; restarting as %s controller", console_name(s_console),
             console_name(other));
    s_switch = SWITCH_MAGIC | other;
    esp_restart();
    return true;
}
