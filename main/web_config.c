// Wi-Fi access point + HTTP server for the configuration page.
//
//   GET  /                 the page (main/web/index.html, embedded)
//   GET  /api/status       JSON snapshot: USB, players (controller + live input), saved controllers
//   POST /api/pair         scan for a new controller (frees the last slot if all are taken)
//   POST /api/disconnect?player=N    drop player N's controller (1-based)
//   POST /api/forget?addr=AA:BB:CC:DD:EE:FF&type=N
//   POST /api/forget_all
//   POST /api/players?n=N  set the number of players (1-4, PS3 only) and restart
//   POST /api/output?mode=auto|ps3|x360   choose the console and restart
//   POST /api/wifi_off     turn Wi-Fi off until next power-up

#include "web_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "adapter_config.h"
#include "bt_status.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pad_state.h"
#include "sdkconfig.h"
#include "usb_out.h"
#include "x360_usb.h"
#include "xbox_bt.h"

static const char* TAG = "web";

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");

static httpd_handle_t s_server;

// ---- Helpers ----

static void format_addr(char out[18], const uint8_t a[6]) {
    snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X", a[0], a[1], a[2], a[3], a[4], a[5]);
}

static bool parse_addr(const char* s, uint8_t out[6]) {
    unsigned v[6];
    if (sscanf(s, "%2x:%2x:%2x:%2x:%2x:%2x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) {
        return false;
    }
    for (int i = 0; i < 6; i++) out[i] = (uint8_t)v[i];
    return true;
}

// Copies a device name into JSON-safe form (drops quotes, backslashes, control chars).
static void json_safe(char* out, size_t out_len, const char* in) {
    size_t j = 0;
    for (size_t i = 0; in[i] && j + 1 < out_len; i++) {
        char c = in[i];
        out[j++] = (c == '"' || c == '\\' || (unsigned char)c < 0x20) ? '?' : c;
    }
    out[j] = '\0';
}

static esp_err_t send_ok(httpd_req_t* req) {
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

// ---- Handlers ----

static esp_err_t index_get(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, index_html_start, index_html_end - index_html_start);
}

static esp_err_t status_get(httpd_req_t* req) {
    const int players = adapter_config_players();
    bt_status_t bt = bt_status_get();

    wifi_sta_list_t sta = {0};
    esp_wifi_ap_get_sta_list(&sta);

    const size_t cap = 3072;
    char* buf = malloc(cap);
    if (buf == NULL) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "out of memory");

    static const char* const settings[] = {"auto", "ps3", "x360"};
    const bool x360 = usb_out_console() == CONSOLE_X360;
    int n = snprintf(buf, cap,
                     "{\"usb\":%s,\"console\":\"%s\",\"output\":\"%s\",\"console_confirmed\":%s,"
                     "\"x360_auth\":%s,\"x360_player\":%d,\"wifi_clients\":%d,\"scanning\":%s,"
                     "\"players_setting\":%d,\"players_max\":%d,\"players\":[",
                     usb_out_is_mounted() ? "true" : "false", x360 ? "x360" : "ps3",
                     settings[adapter_config_output_setting()], usb_out_is_host_confirmed() ? "true" : "false",
                     x360 && x360_authenticated() ? "true" : "false", x360 ? x360_player() : 0, sta.num,
                     bt.scanning ? "true" : "false", adapter_config_players_setting(), players);
    for (int i = 0; i < players; i++) {
        const bt_player_t* p = &bt.players[i];
        pad_state_t pad = pad_state_get(i);
        char name[BT_STATUS_NAME_LEN], addr[18] = "";
        json_safe(name, sizeof(name), p->connected ? p->name : "");
        if (p->connected) format_addr(addr, p->addr);
        n += snprintf(buf + n, cap - n,
                      "%s{\"connected\":%s,\"name\":\"%s\",\"addr\":\"%s\",\"battery\":%u,"
                      "\"input\":{\"buttons\":%u,\"lx\":%u,\"ly\":%u,\"rx\":%u,\"ry\":%u,\"l2\":%u,\"r2\":%u}}",
                      i ? "," : "", p->connected ? "true" : "false", name, addr, p->battery, pad.buttons, pad.lx,
                      pad.ly, pad.rx, pad.ry, pad.l2, pad.r2);
    }
    n += snprintf(buf + n, cap - n, "],\"bonds\":[");
    for (int i = 0; i < bt.num_bonds; i++) {
        char addr[18];
        format_addr(addr, bt.bonds[i].addr);
        n += snprintf(buf + n, cap - n, "%s{\"addr\":\"%s\",\"type\":%u}", i ? "," : "", addr, bt.bonds[i].addr_type);
    }
    n += snprintf(buf + n, cap - n, "]}");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_send(req, buf, n);
    free(buf);
    return err;
}

static esp_err_t pair_post(httpd_req_t* req) {
    xbox_bt_request_pair();
    return send_ok(req);
}

// Reads an integer query parameter; returns false if missing.
static bool query_int(httpd_req_t* req, const char* key, int* out) {
    char query[64], val[8];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, key, val, sizeof(val)) != ESP_OK) {
        return false;
    }
    *out = atoi(val);
    return true;
}

static esp_err_t disconnect_post(httpd_req_t* req) {
    int player;
    if (!query_int(req, "player", &player) || player < 1 || player > adapter_config_players()) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "expected ?player=N");
    }
    xbox_bt_request_disconnect(player - 1);
    return send_ok(req);
}

static void restart_task(void* arg) {
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(500));  // let the HTTP response go out first
    esp_restart();
}

static esp_err_t output_post(httpd_req_t* req) {
    char query[32], mode[8];
    output_setting_t setting;
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "mode", mode, sizeof(mode)) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "expected ?mode=auto|ps3|x360");
    }
    if (strcmp(mode, "auto") == 0) {
        setting = OUTPUT_AUTO;
    } else if (strcmp(mode, "ps3") == 0) {
        setting = OUTPUT_PS3;
    } else if (strcmp(mode, "x360") == 0) {
        setting = OUTPUT_X360;
    } else {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "expected ?mode=auto|ps3|x360");
    }
    if (!adapter_config_set_output(setting)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "could not save");
    }
    ESP_LOGI(TAG, "output set to %s, restarting", mode);
    send_ok(req);
    xTaskCreate(restart_task, "restart", 2048, NULL, 3, NULL);
    return ESP_OK;
}

static esp_err_t players_post(httpd_req_t* req) {
    int players;
    if (!query_int(req, "n", &players) || !adapter_config_set_players(players)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "expected ?n=1..4");
    }
    ESP_LOGI(TAG, "players set to %d, restarting", players);
    send_ok(req);
    xTaskCreate(restart_task, "restart", 2048, NULL, 3, NULL);
    return ESP_OK;
}

static esp_err_t forget_post(httpd_req_t* req) {
    char query[64], addr_str[20], type_str[4];
    uint8_t addr[6];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "addr", addr_str, sizeof(addr_str)) != ESP_OK ||
        httpd_query_key_value(query, "type", type_str, sizeof(type_str)) != ESP_OK || !parse_addr(addr_str, addr)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "expected ?addr=AA:BB:CC:DD:EE:FF&type=N");
    }
    xbox_bt_request_forget(addr, (uint8_t)atoi(type_str));
    return send_ok(req);
}

static esp_err_t forget_all_post(httpd_req_t* req) {
    xbox_bt_request_forget_all();
    return send_ok(req);
}

static void wifi_off_task(void* arg) {
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(500));  // let the HTTP response go out first
    httpd_stop(s_server);
    s_server = NULL;
    esp_wifi_stop();
    ESP_LOGI(TAG, "Wi-Fi turned off until next power-up");
    vTaskDelete(NULL);
}

static esp_err_t wifi_off_post(httpd_req_t* req) {
    send_ok(req);
    xTaskCreate(wifi_off_task, "wifi_off", 3072, NULL, 3, NULL);
    return ESP_OK;
}

// ---- Setup ----

static void start_wifi_ap(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));

    wifi_config_t cfg = {
        .ap = {
            .channel = CONFIG_ADAPTER_WIFI_CHANNEL,
            .max_connection = 2,
            .authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    strlcpy((char*)cfg.ap.ssid, CONFIG_ADAPTER_WIFI_SSID, sizeof(cfg.ap.ssid));
    cfg.ap.ssid_len = strlen(CONFIG_ADAPTER_WIFI_SSID);
    strlcpy((char*)cfg.ap.password, CONFIG_ADAPTER_WIFI_PASSWORD, sizeof(cfg.ap.password));
    if (strlen(CONFIG_ADAPTER_WIFI_PASSWORD) < 8) {
        ESP_LOGW(TAG, "Wi-Fi password shorter than 8 characters: using an OPEN network");
        cfg.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "Wi-Fi '%s' up. Open http://192.168.4.1", CONFIG_ADAPTER_WIFI_SSID);
}

static void start_http(void) {
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 12;
    ESP_ERROR_CHECK(httpd_start(&s_server, &cfg));

    const httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = index_get},
        {.uri = "/api/status", .method = HTTP_GET, .handler = status_get},
        {.uri = "/api/pair", .method = HTTP_POST, .handler = pair_post},
        {.uri = "/api/disconnect", .method = HTTP_POST, .handler = disconnect_post},
        {.uri = "/api/forget", .method = HTTP_POST, .handler = forget_post},
        {.uri = "/api/forget_all", .method = HTTP_POST, .handler = forget_all_post},
        {.uri = "/api/players", .method = HTTP_POST, .handler = players_post},
        {.uri = "/api/output", .method = HTTP_POST, .handler = output_post},
        {.uri = "/api/wifi_off", .method = HTTP_POST, .handler = wifi_off_post},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &routes[i]));
    }
}

void web_config_start(void) {
    start_wifi_ap();
    start_http();
}
