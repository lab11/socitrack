#ifndef __DEVICE_INFO_SERVICE_HEADER_H__
#define __DEVICE_INFO_SERVICE_HEADER_H__

// Device Info Enumerations --------------------------------------------------------------------------------------------

enum
{
   DEVICE_INFO_SERVICE_HANDLE = 0x0030,             // Information service declaration
   DEVICE_INFO_MANUFACTURER_CHAR_HANDLE,            // Manufacturer name string characteristic
   DEVICE_INFO_MANUFACTURER_HANDLE,                 // Manufacturer name string
   DEVICE_INFO_SYSID_CHAR_HANDLE,                   // System ID characteristic
   DEVICE_INFO_SYSID_HANDLE,                        // System ID
   DEVICE_INFO_MODEL_NUM_CHAR_HANDLE,               // Model number string characteristic
   DEVICE_INFO_MODEL_NUM_HANDLE,                    // Model number string
   DEVICE_INFO_FIRMWARE_VER_CHAR_HANDLE,            // Firmware revision string characteristic
   DEVICE_INFO_FIRMWARE_VER_HANDLE,                 // Firmware revision string
   DEVICE_INFO_HARDWARE_VER_CHAR_HANDLE,            // Hardware revision string characteristic
   DEVICE_INFO_HARDWARE_VER_HANDLE,                 // Hardware revision string
   DEVICE_INFO_MAX_HANDLE                           // Maximum handle
};

// Public API ----------------------------------------------------------------------------------------------------------

void deviceInfoAddGroup(void);

#endif  // #ifndef __DEVICE_INFO_SERVICE_HEADER_H__
