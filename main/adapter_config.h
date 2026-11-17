// Persistent adapter settings (NVS). Read once at boot; changing them needs a restart
// because the USB descriptors depend on them.
#pragma once

#include <stdbool.h>

typedef enum {
    OUTPUT_AUTO = 0,  // detect the console (remembers the last one detected)
    OUTPUT_PS3 = 1,   // DualShock 3
    OUTPUT_X360 = 2,  // wired Xbox 360 controller
} output_setting_t;

typedef enum {
    CONSOLE_PS3 = 1,
    CONSOLE_X360 = 2,
} console_t;

void adapter_config_load(void);

// Players presented to the console: the saved setting on PS3, always 1 on Xbox 360.
int adapter_config_players(void);
// The saved players setting (1..MAX_PLAYERS), regardless of the console.
int adapter_config_players_setting(void);
bool adapter_config_set_players(int players);

output_setting_t adapter_config_output_setting(void);
bool adapter_config_set_output(output_setting_t setting);

// The console this boot emulates a controller for.
console_t adapter_config_console(void);
// Auto mode: remember the console that was detected, for the next boot.
void adapter_config_remember_console(console_t console);
// Auto mode: boot once as the other console (used when the current guess was wrong).
// Only allowed once per power-up, so a host that is neither console can't cause a reboot loop.
bool adapter_config_try_other_console(void);
