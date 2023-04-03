#ifndef __GATT_SERVICE_HEADER_H__
#define __GATT_SERVICE_HEADER_H__

// GAP Appearance Definitions ------------------------------------------------------------------------------------------

#define CH_APPEAR_SENSOR            1344
#define CH_APPEAR_SENSOR_PROXIMITY  1361


// GAP Handle Enumerations ---------------------------------------------------------------------------------------------

enum
{
   GAP_SERVICE_HANDLE = 0x0001,             // GAP service
   GAP_DEVICE_NAME_CHAR_HANDLE,             // Device name characteristic
   GAP_DEVICE_NAME_HANDLE,                  // Device name
   GAP_APPEARANCE_CHAR_HANDLE,              // Appearance characteristic
   GAP_APPEARANCE_HANDLE,                   // Appearance
   GAP_CENTRAL_ADDR_RES_CHAR_HANDLE,        // Central address resolution characteristic
   GAP_CENTRAL_ADDR_RES_HANDLE,             // Central address resolution
   GAP_MAX_HANDLE                           // Maximum GAP handle
};


// GATT Handle Enumerations --------------------------------------------------------------------------------------------

enum
{
   GATT_SERVICE_HANDLE = 0x0010,            // GATT service
   GATT_SERVICE_CHANGED_CHAR_HANDLE,        // Service changed characteristic
   GATT_SERVICE_CHANGED_HANDLE,             // Service changed
   GATT_SERVICE_CHANGED_CCC_HANDLE,         // Service changed client characteristic configuration descriptor
   GATT_CSF_CHAR_HANDLE,                    // Client supported features characteristic
   GATT_CSF_HANDLE,                         // Client supported features
   GATT_DB_HASH_CHAR_HANDLE,                // Database hash characteristic
   GATT_DB_HASH_HANDLE,                     // Database hash
   GATT_MAX_HANDLE                          // Maximum GATT handle
};


// Public API ----------------------------------------------------------------------------------------------------------

void gapGattAddGroup(void);
void gapGattRegisterCallbacks(attsReadCback_t readCallback, attsWriteCback_t writeCallback);

#endif  // #ifndef __GATT_SERVICE_HEADER_H__
