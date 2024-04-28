#ifndef __MAINTENANCE_FUNCTIONALITY_HEADER_H__
#define __MAINTENANCE_FUNCTIONALITY_HEADER_H__

// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "app_tasks.h"


// BLE-Based TotTag Maintenance Definitions ----------------------------------------------------------------------------

#define MIN(a, b) (((a) < (b)) ? (a) : (b))

#define BLE_MAINTENANCE_NEW_EXPERIMENT                  0x01
#define BLE_MAINTENANCE_DELETE_EXPERIMENT               0x02
#define BLE_MAINTENANCE_DOWNLOAD_LOG                    0x03
#define BLE_MAINTENANCE_SET_LOG_DOWNLOAD_DATES          0x04
#define BLE_MAINTENANCE_PACKET_COMPLETE                 0xFF


// Public API ----------------------------------------------------------------------------------------------------------

uint8_t handleDeviceMaintenanceRead(dmConnId_t connId, uint16_t handle, uint8_t operation, uint16_t offset, attsAttr_t *pAttr);
uint8_t handleDeviceMaintenanceWrite(dmConnId_t connId, uint16_t handle, uint8_t operation, uint16_t offset, uint16_t len, uint8_t *pValue, attsAttr_t *pAttr);
void continueSendingLogData(dmConnId_t connId, uint16_t max_length);

#endif  // #ifndef __MAINTENANCE_FUNCTIONALITY_HEADER_H__
