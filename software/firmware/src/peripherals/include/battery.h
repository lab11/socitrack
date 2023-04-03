#ifndef __BATTERY_HEADER_H__
#define __BATTERY_HEADER_H__

// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "app_config.h"


// Peripheral Type Definitions -----------------------------------------------------------------------------------------

typedef enum { BATTERY_PLUGGED = 1, BATTERY_UNPLUGGED, BATTERY_CHARGING, BATTERY_NOT_CHARGING } battery_event_t;
typedef void (*battery_event_callback_t)(battery_event_t battery_event);


// Public API Functions ------------------------------------------------------------------------------------------------

void battery_monitor_init(void);
void battery_monitor_deinit(void);
void battery_register_event_callback(battery_event_callback_t callback);
uint32_t battery_monitor_get_level_mV(void);
bool battery_monitor_is_plugged_in(void);
bool battery_monitor_is_charging(void);

#endif  // #ifndef __BATTERY_HEADER_H__
