// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "app_tasks.h"
#include "wsf_types.h"
#include "att_api.h"
#include "scheduling_functionality.h"
#include "scheduling_service.h"
#include "logging.h"
#include "rtc.h"


// Public API ----------------------------------------------------------------------------------------------------------

uint8_t handleSchedulingRead(dmConnId_t connId, uint16_t handle, uint8_t operation, uint16_t offset, attsAttr_t *pAttr)
{
   print("TotTag BLE: Scheduling Read: connID = %d, handle = %d, operation = %d\n", connId, handle, operation);
   return ATT_SUCCESS;
}

uint8_t handleSchedulingWrite(dmConnId_t connId, uint16_t handle, uint8_t operation, uint16_t offset, uint16_t len, uint8_t *pValue, attsAttr_t *pAttr)
{
   print("TotTag BLE: Scheduling Write: connID = %d handle = %d, value = %d\n", connId, handle, *pValue);
   if (handle == REQUEST_HANDLE)
      app_schedule_device(pValue);
   return ATT_SUCCESS;
}
