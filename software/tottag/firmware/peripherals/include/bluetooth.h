#ifndef __BLUETOOTH_HEADER_H
#define __BLUETOOTH_HEADER_H

// Header inclusions ---------------------------------------------------------------------------------------------------

#include "nrfx_atomic.h"


// Public Bluetooth API ------------------------------------------------------------------------------------------------

nrfx_err_t ble_init(nrfx_atomic_flag_t* squarepoint_enabled_flag, nrfx_atomic_flag_t* ble_is_advertising_flag, nrfx_atomic_flag_t* ble_is_scanning_flag, nrfx_atomic_u32_t* calibration_index);
uint8_t ble_get_device_role(void);
const uint8_t* ble_get_eui(void);
const uint8_t* ble_get_empty_eui(void);
const uint8_t* ble_get_scheduler_eui(void);
const uint8_t* ble_get_highest_network_eui(void);
void ble_start_advertising(void);
void ble_start_scanning(void);
void ble_stop_advertising(void);
void ble_stop_scanning(void);
void ble_clear_scheduler_eui(void);
uint8_t ble_set_scheduler_eui(const uint8_t* eui, uint8_t num_eui_bytes);
void ble_update_ranging_data(const uint8_t *data, volatile uint16_t *length);
void ble_second_has_elapsed(void);
uint32_t ble_request_timestamp(void);
uint32_t ble_is_network_available(void);

#endif // #ifndef __BLUETOOTH_HEADER_H
