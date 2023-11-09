#ifndef __APP_TASKS_HEADER_H__
#define __APP_TASKS_HEADER_H__

// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "battery.h"


// Task Type Definitions -----------------------------------------------------------------------------------------------

typedef enum { ROLE_IDLE = 0, ROLE_MASTER, ROLE_PARTICIPANT, ROLE_ASLEEP } schedule_role_t;

typedef enum {
   APP_NOTIFY_VERIFY_CONFIGURATION = 0b00000001,
   APP_NOTIFY_NETWORK_LOST = 0b00000010,
   APP_NOTIFY_NETWORK_FOUND = 0b00000100,
   APP_NOTIFY_BATTERY_EVENT = 0b00100000,
   APP_NOTIFY_FIND_MY_TOTTAG_ACTIVATED = 0b10000000
} app_notification_t;

typedef struct __attribute__ ((__packed__))
{
   uint32_t experiment_start_time, experiment_end_time;
   uint32_t daily_start_time, daily_end_time;
   uint8_t use_daily_times, num_devices, uids[MAX_NUM_RANGING_DEVICES][EUI_LEN];
   char uid_name_mappings[MAX_NUM_RANGING_DEVICES][EUI_NAME_MAX_LEN];
} experiment_details_t;


// Public API Functions ------------------------------------------------------------------------------------------------

// Application Task Public Functions
void app_notify(app_notification_t notification, bool from_isr);
void app_activate_find_my_tottag(uint32_t seconds_to_activate);

// Ranging Task Public Functions
void ranging_begin(schedule_role_t role);
bool ranging_active(void);

// Storage Task Public Functions
void storage_flush_and_shutdown(void);
void storage_write_battery_level(uint32_t battery_voltage_mV);
void storage_write_charging_event(battery_event_t battery_event);
void storage_write_motion_status(bool in_motion);
void storage_write_ranging_data(uint32_t timestamp, const uint8_t *ranging_data, uint32_t ranging_data_len);

// Main Task Functions
void AppTaskRanging(void *uid);
void AppTaskMaintenance(void *uid);
void BLETask(void *params);
void RangingTask(void *uid);
void StorageTask(void *params);
void TimeAlignedTask(void *params);

// Task Startup Function
void run_tasks(void);

#endif  // #ifndef __APP_TASKS_HEADER_H__
