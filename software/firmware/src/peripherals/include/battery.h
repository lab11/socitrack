#ifndef __BATTERY_HEADER_H__
#define __BATTERY_HEADER_H__

// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "app_config.h"


// Peripheral Type Definitions -----------------------------------------------------------------------------------------

#define BATTERY_TEMPERATURE_UNKNOWN                 INT8_MIN

typedef enum { BATTERY_PLUGGED = 1, BATTERY_UNPLUGGED, BATTERY_CHARGING, BATTERY_NOT_CHARGING, BATTERY_CRITICAL_VOLTAGE } battery_event_t;
typedef void (*battery_event_callback_t)(battery_event_t battery_event);


// Public API Functions ------------------------------------------------------------------------------------------------

void battery_monitor_init(void);
void battery_monitor_deinit(void);
void battery_register_event_callback(battery_event_callback_t callback);
uint32_t battery_monitor_get_level_mV(void);
bool battery_monitor_is_plugged_in(void);
bool battery_monitor_is_charging(void);
bool battery_monitor_has_brownout_detection(void);
void battery_monitor_poll_charger_state(void);
uint32_t battery_monitor_get_suppressed_edge_count(void);
void battery_monitor_service_tempco(void);
uint32_t battery_monitor_ms_since_temperature_sample(void);
int8_t battery_monitor_get_temperature_c(void);
bool battery_monitor_tempco_available(void);
bool battery_monitor_tempco_applied(void);

#endif  // #ifndef __BATTERY_HEADER_H__
