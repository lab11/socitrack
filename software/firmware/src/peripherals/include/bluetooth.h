#ifndef __BLUETOOTH_HEADER_H__
#define __BLUETOOTH_HEADER_H__

// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "app_config.h"


// Peripheral Type Definitions -----------------------------------------------------------------------------------------

typedef void (*ble_discovery_callback_t)(const uint8_t ble_address[6], uint8_t ranging_role);


// Public API Functions ------------------------------------------------------------------------------------------------

void bluetooth_init(uint8_t* uid);
void bluetooth_deinit(void);
void bluetooth_reset(void);
void bluetooth_start(void);
bool bluetooth_is_initialized(void);
void bluetooth_set_uninitialized(void);
void bluetooth_register_discovery_callback(ble_discovery_callback_t callback);
uint8_t bluetooth_get_current_ranging_role(void);
void bluetooth_set_current_ranging_role(uint8_t ranging_role);
void bluetooth_write_range_results(const uint8_t *results, uint16_t results_length);
void bluetooth_start_advertising(void);
void bluetooth_stop_advertising(void);
bool bluetooth_is_advertising(void);
void bluetooth_start_scanning(void);
void bluetooth_stop_scanning(void);
void bluetooth_reset_scanning(void);
bool bluetooth_is_scanning(void);
bool bluetooth_is_connected(void);
void bluetooth_clear_whitelist(void);
void bluetooth_add_device_to_whitelist(uint8_t* uid);

#endif  // #ifndef __BLUETOOTH_HEADER_H__
