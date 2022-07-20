// Datasheet: https://www.ti.com/lit/ds/symlink/bq24040.pdf?ts=1593963502813&ref_url=https%253A%252F%252Fwww.ti.com%252Fproduct%252FBQ24040
#ifndef __BATTERY_HEADER_H
#define __BATTERY_HEADER_H

// Header inclusions ---------------------------------------------------------------------------------------------------

#include "ble_config.h"


// Battery-specific definitions ----------------------------------------------------------------------------------------

#define BATTERY_MONITOR_CHANNEL             0
#define BATTERY_VOLTAGE_MAX                 4200
#define BATTERY_VOLTAGE_NOMINAL             3700
#define BATTERY_VOLTAGE_CRITICAL            3500
#define BATTERY_VOLTAGE_EMPTY               3200
#if (BOARD_V >= 0x10)
#define BATTERY_VOLTAGE_DIV_UP              33
#define BATTERY_VOLTAGE_DIV_DOWN            100
#else
#define BATTERY_VOLTAGE_DIV_UP              100
#define BATTERY_VOLTAGE_DIV_DOWN            22
#endif


// Public battery monitoring API ---------------------------------------------------------------------------------------

void battery_monitor_init(nrfx_atomic_flag_t* battery_status_changed_flag);
uint16_t battery_monitor_get_level_mV(void);
bool battery_monitor_is_plugged_in(void);
bool battery_monitor_is_charging(void);

#endif // #ifndef __BATTERY_HEADER_H
