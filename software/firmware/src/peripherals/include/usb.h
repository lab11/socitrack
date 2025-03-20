#ifndef __USB_HEADER_H__
#define __USB_HEADER_H__

// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "app_config.h"
#include "maintenance_functionality.h"


// USB Type Definitions ------------------------------------------------------------------------------------------------

typedef enum {
   USB_VERSION_COMMAND = 0x20,
   USB_VOLTAGE_COMMAND = 0x10,
   USB_GET_TIMESTAMP_COMMAND = 0x11,
   USB_FIND_MY_TOTTAG_COMMAND = 0x12,
   USB_GET_EXPERIMENT_COMMAND = 0x13,
   USB_SET_TIMESTAMP_COMMAND = 0x14,
   USB_NEW_EXPERIMENT_COMMAND = BLE_MAINTENANCE_NEW_EXPERIMENT,
   USB_DELETE_EXPERIMENT_COMMAND = BLE_MAINTENANCE_DELETE_EXPERIMENT,
   USB_SET_LOG_DL_DATES_COMMAND = BLE_MAINTENANCE_SET_LOG_DOWNLOAD_DATES,
   USB_DOWNLOAD_LOG_COMMAND = BLE_MAINTENANCE_DOWNLOAD_LOG
} usb_command_t;


// Public API Functions ------------------------------------------------------------------------------------------------

void usb_init(void);
bool usb_cable_connected(void);
uint32_t usb_write(const void* data, uint32_t num_bytes);

#endif  // #ifndef __USB_HEADER_H__
