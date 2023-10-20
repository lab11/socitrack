// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "app_config.h"
#include "wsf_types.h"
#include "att_main.h"
#include "logging.h"
#include "maintenance_functionality.h"
#include "maintenance_service.h"
#include "storage.h"


// Public API ----------------------------------------------------------------------------------------------------------

uint8_t handleDeviceMaintenanceRead(dmConnId_t connId, uint16_t handle, uint8_t operation, uint16_t offset, attsAttr_t *pAttr)
{
   print("TotTag BLE: Device Maintenance Read: connID = %d, handle = %d, operation = %d\n", connId, handle, operation);
#ifdef _TEST_BLUETOOTH
   memset(pAttr->pValue, 0, sizeof(experiment_details_t));
#else
   if (handle == MAINTENANCE_EXPERIMENT_HANDLE)
      storage_retrieve_experiment_details((experiment_details_t*)pAttr->pValue);
#endif
   return ATT_SUCCESS;
}

uint8_t handleDeviceMaintenanceWrite(dmConnId_t connId, uint16_t handle, uint8_t operation, uint16_t offset, uint16_t len, uint8_t *pValue, attsAttr_t *pAttr)
{
   // Handle the incoming BLE request
   print("TotTag BLE: Device Maintenance Write: connID = %d handle = %d, value = %d\n", connId, handle, *pValue);
   if (handle == MAINTENANCE_COMMAND_HANDLE)
      switch (*pValue)
      {
         case BLE_MAINTENANCE_NEW_EXPERIMENT:
         {
            const experiment_details_t* new_details = (const experiment_details_t*)(pValue + 1);
            storage_store_experiment_details(new_details);
            break;
         }
         case BLE_MAINTENANCE_DELETE_EXPERIMENT:
         {
            const experiment_details_t empty_details = { 0 };
            storage_store_experiment_details(&empty_details);
            break;
         }
         case BLE_MAINTENANCE_DOWNLOAD_LOG:
            continueSendingLogData(connId, 0);
            break;
         default:
            break;
   }
   return ATT_SUCCESS;
}

void continueSendingLogData(dmConnId_t connId, uint16_t max_length)
{
   // Define static transmission variables
   static uint8_t transmit_buffer[2 * MEMORY_PAGE_SIZE_BYTES];
   static uint32_t transmit_index, total_data_length;
   static uint16_t buffer_index, buffer_length;

   // Determine whether this is a new transmission or a continuation
   if (max_length == 0)
   {
      // Reset all transmission variables and send total data length
      storage_begin_reading();
      experiment_details_t details;
      transmit_index = buffer_index = 0;
      storage_retrieve_experiment_details(&details);
      total_data_length = storage_retrieve_data_length();
      buffer_length = (uint16_t)storage_retrieve_next_data_chunk(transmit_buffer);
      AttsHandleValueNtf(connId, MAINTENANCE_RESULT_HANDLE, sizeof(total_data_length), (uint8_t*)&total_data_length);
      AttsHandleValueNtf(connId, MAINTENANCE_RESULT_HANDLE, sizeof(details), (uint8_t*)&details);
   }
   else if (transmit_index < total_data_length)
   {
      // Transmit the next chunk of data
      const uint16_t transmit_length = MIN(max_length, buffer_length - buffer_index);
      AttsHandleValueNtf(connId, MAINTENANCE_RESULT_HANDLE, transmit_length, transmit_buffer + buffer_index);
      transmit_index += transmit_length;
      buffer_index += transmit_length;

      // Ensure that there is enough buffered data to transmit again in the future without reading
      if ((buffer_length - buffer_index) < max_length)
      {
         memmove(transmit_buffer, transmit_buffer + buffer_index, buffer_length - buffer_index);
         buffer_length -= buffer_index;
         buffer_length += (uint16_t)storage_retrieve_next_data_chunk(transmit_buffer + buffer_length);
         buffer_index = 0;
      }
   }
   else if (transmit_index == total_data_length)
   {
      // Transit a completion packet
      storage_end_reading();
      uint8_t completion_packet = BLE_MAINTENANCE_PACKET_COMPLETE;
      AttsHandleValueNtf(connId, MAINTENANCE_RESULT_HANDLE, sizeof(completion_packet), &completion_packet);
      ++transmit_index;
   }
}
