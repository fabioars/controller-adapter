// Bluepad32 "custom platform": accepts Xbox Wireless (Series X|S) controllers over BLE,
// one per player slot (1 by default, up to MAX_PLAYERS in multi-player mode), publishes
// their state to pad_state, and forwards PS3 rumble back to them.
//
// Everything here runs on the BTstack thread, except the xbox_bt_request_*() functions,
// which only queue work onto that thread.

#include "xbox_bt.h"

#include <string.h>

#include <ble/le_device_db.h>
#include <btstack_run_loop.h>
#include <gap.h>
#include <uni.h>

#include "adapter_config.h"
#include "bt_status.h"
#include "freertos/FreeRTOS.h"
#include "pad_state.h"

#define POLL_MS 20
#define STATUS_EVERY_N_POLLS 25  // publish status every 500 ms
#define RUMBLE_REFRESH_MS 1000   // re-send an ongoing effect before it expires
#define RUMBLE_EFFECT_MS 1500    // duration of each sent effect
#define BUZZ_MS 120              // player-number buzzes on connect
#define BUZZ_GAP_MS 320
#define DROP_BLOCK_MS 30000  // a controller we dropped can't rejoin for this long

typedef struct {
    uni_hid_device_t* dev;  // NULL = free
    uint8_t battery;
    rumble_state_t rumble_sent;
    uint32_t rumble_sent_at;
    int buzz_left;
    uint32_t buzz_next_at;
} slot_t;

static slot_t s_slots[MAX_PLAYERS];
static int s_players = 1;
static bool s_scanning;
static btstack_timer_source_t s_poll_timer;
static int s_poll_count;
static bd_addr_t s_blocked_addr;
static uint32_t s_blocked_until;

// ---- Helpers (BTstack thread) ----

// Bluepad32 axes are -512..511; DS3 axes are 0..255 with 128 at center.
static uint8_t axis_to_u8(int32_t v) {
    v = (v + 512) >> 2;
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

// Bluepad32 triggers are 0..1023.
static uint8_t trigger_to_u8(int32_t v) {
    v >>= 2;
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

static pad_state_t map_gamepad(const uni_gamepad_t* gp) {
    pad_state_t s = PAD_NEUTRAL;
    uint16_t b = 0;

    // Positional mapping: Xbox A/B/X/Y sit where Cross/Circle/Square/Triangle do.
    if (gp->buttons & BUTTON_A) b |= PAD_CROSS;
    if (gp->buttons & BUTTON_B) b |= PAD_CIRCLE;
    if (gp->buttons & BUTTON_X) b |= PAD_SQUARE;
    if (gp->buttons & BUTTON_Y) b |= PAD_TRIANGLE;
    if (gp->buttons & BUTTON_SHOULDER_L) b |= PAD_L1;
    if (gp->buttons & BUTTON_SHOULDER_R) b |= PAD_R1;
    if (gp->buttons & BUTTON_THUMB_L) b |= PAD_L3;
    if (gp->buttons & BUTTON_THUMB_R) b |= PAD_R3;

    if (gp->misc_buttons & MISC_BUTTON_SELECT) b |= PAD_SELECT;  // View
    if (gp->misc_buttons & MISC_BUTTON_START) b |= PAD_START;    // Menu
    if (gp->misc_buttons & MISC_BUTTON_SYSTEM) b |= PAD_PS;      // Xbox button

    if (gp->dpad & DPAD_UP) b |= PAD_UP;
    if (gp->dpad & DPAD_DOWN) b |= PAD_DOWN;
    if (gp->dpad & DPAD_LEFT) b |= PAD_LEFT;
    if (gp->dpad & DPAD_RIGHT) b |= PAD_RIGHT;

    s.buttons = b;
    s.lx = axis_to_u8(gp->axis_x);
    s.ly = axis_to_u8(gp->axis_y);
    s.rx = axis_to_u8(gp->axis_rx);
    s.ry = axis_to_u8(gp->axis_ry);
    s.l2 = trigger_to_u8(gp->brake);
    s.r2 = trigger_to_u8(gp->throttle);
    return s;
}

static int slot_of(const uni_hid_device_t* d) {
    for (int i = 0; i < s_players; i++) {
        if (s_slots[i].dev == d) return i;
    }
    return -1;
}

static int free_slot(void) {
    return slot_of(NULL);
}

static bool is_blocked(const bd_addr_t addr) {
    return (int32_t)(s_blocked_until - btstack_run_loop_get_time_ms()) > 0 && memcmp(addr, s_blocked_addr, 6) == 0;
}

static void start_scanning(void) {
    if (!s_scanning) {
        uni_bt_start_scanning_and_autoconnect_unsafe();
        s_scanning = true;
    }
}

static void stop_scanning(void) {
    if (s_scanning) {
        uni_bt_stop_scanning_unsafe();
        s_scanning = false;
    }
}

// Drops a controller; it is kept from rejoining for DROP_BLOCK_MS so its slot can
// go to a different controller (e.g. when pairing a new one).
static void drop(int slot) {
    uni_hid_device_t* d = s_slots[slot].dev;
    if (d == NULL) return;
    memcpy(s_blocked_addr, d->conn.btaddr, 6);
    s_blocked_until = btstack_run_loop_get_time_ms() + DROP_BLOCK_MS;
    uni_hid_device_disconnect(d);  // on_device_disconnected frees the slot
}

static void publish_status(void) {
    bt_status_t st = {0};
    st.scanning = s_scanning;
    for (int i = 0; i < s_players; i++) {
        const uni_hid_device_t* d = s_slots[i].dev;
        if (d == NULL) continue;
        bt_player_t* p = &st.players[i];
        p->connected = true;
        strlcpy(p->name, d->name, sizeof(p->name));
        memcpy(p->addr, d->conn.btaddr, 6);
        p->battery = s_slots[i].battery;
    }

    // Bonded BLE devices (stored in NVS by BTstack).
    for (int i = 0; i < le_device_db_max_count() && st.num_bonds < BT_STATUS_MAX_BONDS; i++) {
        int addr_type = BD_ADDR_TYPE_UNKNOWN;
        bd_addr_t addr;
        sm_key_t irk;
        le_device_db_info(i, &addr_type, addr, irk);
        if (addr_type == BD_ADDR_TYPE_UNKNOWN) continue;
        bt_bond_t* b = &st.bonds[st.num_bonds++];
        memcpy(b->addr, addr, 6);
        b->addr_type = (uint8_t)addr_type;
    }
    bt_status_set(&st);
}

static void play_rumble(uni_hid_device_t* d, uint8_t weak, uint8_t strong, uint16_t duration_ms) {
    if (d->report_parser.play_dual_rumble != NULL) {
        d->report_parser.play_dual_rumble(d, 0, duration_ms, weak, strong);
    }
}

// Applies rumble requested by the PS3 (USB task) and the connect buzzes, and
// periodically publishes status for the web page. Polling keeps all Bluepad32
// calls on the BTstack thread.
static void poll_timer_cb(btstack_timer_source_t* ts) {
    uint32_t now = btstack_run_loop_get_time_ms();
    for (int i = 0; i < s_players; i++) {
        slot_t* s = &s_slots[i];
        if (s->dev == NULL) continue;

        if (s->buzz_left > 0) {
            if ((int32_t)(now - s->buzz_next_at) >= 0) {
                play_rumble(s->dev, 160, 80, BUZZ_MS);
                s->buzz_left--;
                s->buzz_next_at = now + BUZZ_GAP_MS;
            }
            continue;  // don't let game rumble cut the buzzes short
        }

        rumble_state_t want = rumble_state_get(i);
        bool active = want.weak || want.strong;
        bool changed = want.weak != s->rumble_sent.weak || want.strong != s->rumble_sent.strong;
        if (changed || (active && now - s->rumble_sent_at >= RUMBLE_REFRESH_MS)) {
            play_rumble(s->dev, want.weak, want.strong, active ? RUMBLE_EFFECT_MS : 0);
            s->rumble_sent = want;
            s->rumble_sent_at = now;
        }
    }
    if (++s_poll_count >= STATUS_EVERY_N_POLLS) {
        s_poll_count = 0;
        publish_status();
    }
    btstack_run_loop_set_timer(ts, POLL_MS);
    btstack_run_loop_add_timer(ts);
}

// ---- Commands from other tasks (web server) ----

static portMUX_TYPE s_cmd_lock = portMUX_INITIALIZER_UNLOCKED;
static bd_addr_t s_forget_addr;
static uint8_t s_forget_addr_type;
static int s_disconnect_player;

static void cmd_pair(void* ctx) {
    (void)ctx;
    logi("adapter: pairing mode requested\n");
    if (free_slot() < 0) {
        drop(s_players - 1);  // all slots taken: make room in the last one
    }
    start_scanning();
    publish_status();
}

static void cmd_disconnect(void* ctx) {
    (void)ctx;
    taskENTER_CRITICAL(&s_cmd_lock);
    int player = s_disconnect_player;
    taskEXIT_CRITICAL(&s_cmd_lock);
    if (player >= 0 && player < s_players && s_slots[player].dev != NULL) {
        logi("adapter: disconnecting player %d\n", player + 1);
        drop(player);
    }
}

static void cmd_forget(void* ctx) {
    (void)ctx;
    bd_addr_t addr;
    taskENTER_CRITICAL(&s_cmd_lock);
    memcpy(addr, s_forget_addr, 6);
    uint8_t type = s_forget_addr_type;
    taskEXIT_CRITICAL(&s_cmd_lock);

    for (int i = 0; i < s_players; i++) {
        if (s_slots[i].dev != NULL && memcmp(s_slots[i].dev->conn.btaddr, addr, 6) == 0) {
            uni_hid_device_disconnect(s_slots[i].dev);
        }
    }
    logi("adapter: forgetting %s\n", bd_addr_to_str(addr));
    gap_delete_bonding((bd_addr_type_t)type, addr);
    publish_status();
}

static void cmd_forget_all(void* ctx) {
    (void)ctx;
    logi("adapter: forgetting all controllers\n");
    for (int i = 0; i < s_players; i++) {
        if (s_slots[i].dev != NULL) uni_hid_device_disconnect(s_slots[i].dev);
    }
    uni_bt_del_keys_unsafe();
    publish_status();
}

static btstack_context_callback_registration_t s_reg_pair = {.callback = cmd_pair};
static btstack_context_callback_registration_t s_reg_disconnect = {.callback = cmd_disconnect};
static btstack_context_callback_registration_t s_reg_forget = {.callback = cmd_forget};
static btstack_context_callback_registration_t s_reg_forget_all = {.callback = cmd_forget_all};

// btstack_run_loop_execute_on_main_thread() is thread-safe and ignores a
// registration that is already queued, so static registrations are fine.
void xbox_bt_request_pair(void) {
    btstack_run_loop_execute_on_main_thread(&s_reg_pair);
}

void xbox_bt_request_disconnect(int player) {
    taskENTER_CRITICAL(&s_cmd_lock);
    s_disconnect_player = player;
    taskEXIT_CRITICAL(&s_cmd_lock);
    btstack_run_loop_execute_on_main_thread(&s_reg_disconnect);
}

void xbox_bt_request_forget(const uint8_t addr[6], uint8_t addr_type) {
    taskENTER_CRITICAL(&s_cmd_lock);
    memcpy(s_forget_addr, addr, 6);
    s_forget_addr_type = addr_type;
    taskEXIT_CRITICAL(&s_cmd_lock);
    btstack_run_loop_execute_on_main_thread(&s_reg_forget);
}

void xbox_bt_request_forget_all(void) {
    btstack_run_loop_execute_on_main_thread(&s_reg_forget_all);
}

// ---- Platform callbacks ----

static void plat_init(int argc, const char** argv) {
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    s_players = adapter_config_players();
}

static void plat_on_init_complete(void) {
    logi("adapter: Bluetooth ready for %d player(s). Hold the pair button on an Xbox controller to pair.\n",
         s_players);
    // Keep stored keys so paired controllers reconnect by just pressing the Xbox button.
    start_scanning();
    uni_bt_allow_incoming_connections(true);

    btstack_run_loop_set_timer_handler(&s_poll_timer, poll_timer_cb);
    btstack_run_loop_set_timer(&s_poll_timer, POLL_MS);
    btstack_run_loop_add_timer(&s_poll_timer);
    publish_status();
}

static uni_error_t plat_on_device_discovered(bd_addr_t addr, const char* name, uint16_t cod, uint8_t rssi) {
    ARG_UNUSED(cod);
    ARG_UNUSED(rssi);
    // The real filter is in on_device_ready (controller type is known there).
    if (free_slot() < 0 || is_blocked(addr)) {
        return UNI_ERROR_IGNORE_DEVICE;
    }
    logi("adapter: discovered '%s'\n", name ? name : "");
    return UNI_ERROR_SUCCESS;
}

static void plat_on_device_connected(uni_hid_device_t* d) {
    logi("adapter: device connected: %p\n", d);
}

static void plat_on_device_disconnected(uni_hid_device_t* d) {
    int i = slot_of(d);
    if (i < 0) return;
    logi("adapter: player %d disconnected\n", i + 1);
    s_slots[i] = (slot_t){0};
    pad_state_set(i, &PAD_NEUTRAL);
    start_scanning();
    publish_status();
}

static uni_error_t plat_on_device_ready(uni_hid_device_t* d) {
    if (d->controller_type != CONTROLLER_TYPE_XBoxOneController) {
        logi("adapter: rejecting non-Xbox controller (type=%d)\n", d->controller_type);
        return UNI_ERROR_IGNORE_DEVICE;
    }
    if (is_blocked(d->conn.btaddr)) {
        logi("adapter: rejecting recently dropped controller\n");
        return UNI_ERROR_IGNORE_DEVICE;
    }
    int i = slot_of(d);
    if (i < 0) i = free_slot();
    if (i < 0) {
        logi("adapter: all %d player slot(s) taken, rejecting controller\n", s_players);
        return UNI_ERROR_IGNORE_DEVICE;
    }

    s_slots[i] = (slot_t){.dev = d, .buzz_left = i + 1, .buzz_next_at = btstack_run_loop_get_time_ms()};
    logi("adapter: Xbox controller ready as player %d\n", i + 1);
    if (free_slot() < 0) {
        stop_scanning();
    }
    publish_status();
    return UNI_ERROR_SUCCESS;
}

static void plat_on_controller_data(uni_hid_device_t* d, uni_controller_t* ctl) {
    int i = slot_of(d);
    if (i < 0 || ctl->klass != UNI_CONTROLLER_CLASS_GAMEPAD) {
        return;
    }
    s_slots[i].battery = ctl->battery;
    pad_state_t s = map_gamepad(&ctl->gamepad);
    pad_state_set(i, &s);
}

static const uni_property_t* plat_get_property(uni_property_idx_t idx) {
    ARG_UNUSED(idx);
    return NULL;
}

static void plat_on_oob_event(uni_platform_oob_event_t event, void* data) {
    // The Xbox button is forwarded as PS through on_controller_data; nothing to do here.
    ARG_UNUSED(event);
    ARG_UNUSED(data);
}

struct uni_platform* xbox_bt_platform(void) {
    static struct uni_platform plat = {
        .name = "ps3-adapter",
        .init = plat_init,
        .on_init_complete = plat_on_init_complete,
        .on_device_discovered = plat_on_device_discovered,
        .on_device_connected = plat_on_device_connected,
        .on_device_disconnected = plat_on_device_disconnected,
        .on_device_ready = plat_on_device_ready,
        .on_oob_event = plat_on_oob_event,
        .on_controller_data = plat_on_controller_data,
        .get_property = plat_get_property,
    };
    return &plat;
}
