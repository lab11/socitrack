#ifndef __SYSTEM_HEADER_H__
#define __SYSTEM_HEADER_H__

// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "app_config.h"


// Public API Functions ------------------------------------------------------------------------------------------------

void setup_hardware(void);
void system_reset(bool immediate);
uint16_t system_get_reset_reason(void);
void system_watchdog_enable(void);
void system_watchdog_disable(void);
void system_watchdog_pet(const char *checked_in_from);
void system_enable_interrupts(bool enabled);
void system_enter_power_off_mode(uint32_t wake_on_gpio, uint32_t wake_on_timestamp);
void system_read_UID(uint8_t *uid, uint32_t uid_length);

#endif  // #ifndef __SYSTEM_HEADER_H__
