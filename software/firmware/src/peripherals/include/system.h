#ifndef __SYSTEM_HEADER_H__
#define __SYSTEM_HEADER_H__

// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "app_config.h"


// Watchdog Participants -----------------------------------------------------------------------------------------------

typedef enum {
   WATCHDOG_TASK_TIME_ALIGNED = 0,
   WATCHDOG_TASK_STORAGE,
   WATCHDOG_TASK_APP,
   WATCHDOG_TASK_BLE,
   WATCHDOG_TASK_RANGING,
   WATCHDOG_NUM_TASKS
} watchdog_task_t;


// Reset Diagnostics ---------------------------------------------------------------------------------------------------

// The low 12 bits of a stored reset reason are the raw am_hal_reset_status_e bits.
// The top 4 carry why the firmware itself gave up, which the hardware status cannot express.
#define RESET_DIAGNOSTIC_SHIFT                      12
#define RESET_DIAGNOSTIC_MASK                       0xF

typedef enum {
   RESET_DIAGNOSTIC_NONE = 0,
   RESET_DIAGNOSTIC_STALL_TIME_ALIGNED,
   RESET_DIAGNOSTIC_STALL_STORAGE,
   RESET_DIAGNOSTIC_STALL_APP,
   RESET_DIAGNOSTIC_STALL_BLE,
   RESET_DIAGNOSTIC_STALL_RANGING,
   RESET_DIAGNOSTIC_STALL_MULTIPLE,
   RESET_DIAGNOSTIC_HARD_FAULT,
   RESET_DIAGNOSTIC_STACK_OVERFLOW,
   RESET_DIAGNOSTIC_ASSERT,
   RESET_DIAGNOSTIC_MALLOC_FAILED,
   RESET_DIAGNOSTIC_STORAGE_FATAL,           // the flash stopped answering and the log gave up on it
   RESET_DIAGNOSTIC_STORAGE_UNWRITABLE,      // the flash answers but will not retain a page anywhere
   RESET_DIAGNOSTIC_CLOCK_STOPPED,           // the 32 kHz crystal behind the tick and the check-in clock died
   RESET_DIAGNOSTIC_NOTHING_RECORDED,
} reset_diagnostic_t;

_Static_assert(RESET_DIAGNOSTIC_NOTHING_RECORDED <= RESET_DIAGNOSTIC_MASK, "reset diagnostics no longer fit in the spare bits of a stored reset-reason record");
_Static_assert((RESET_DIAGNOSTIC_STALL_TIME_ALIGNED + WATCHDOG_NUM_TASKS) == RESET_DIAGNOSTIC_STALL_MULTIPLE, "the per-task stall codes must stay contiguous with, and in the same order as, watchdog_task_t");


// Public API Functions ------------------------------------------------------------------------------------------------

void setup_hardware(void);
void system_reset(bool immediate);
uint16_t system_get_reset_reason(void);
void system_record_diagnostic(reset_diagnostic_t diagnostic);
void system_watchdog_enable(void);
void system_watchdog_disable(void);
void system_watchdog_register(watchdog_task_t task);
void system_watchdog_pet(watchdog_task_t task);
void system_enable_interrupts(bool enabled);
void system_enter_power_off_mode(uint32_t wake_on_gpio, uint32_t wake_on_timestamp);
void system_read_UID(uint8_t *uid, uint32_t uid_length);

#endif  // #ifndef __SYSTEM_HEADER_H__
