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
   static uint32_t data_chunk_index, total_data_chunks;
   static uint16_t buffer_index, buffer_length;
   static bool is_reading = false;

   // Determine whether this is a new transmission or a continuation
   if (max_length == 0)
   {
      // Reset all transmission variables and send estimated total data length
      buffer_index = 0;
      is_reading = true;
      storage_begin_reading();
      experiment_details_t details;
      storage_retrieve_experiment_details(&details);
      total_data_chunks = storage_retrieve_num_data_chunks();
      uint32_t total_data_length = total_data_chunks * MEMORY_NUM_DATA_BYTES_PER_PAGE;
      memcpy(transmit_buffer, &total_data_length, sizeof(total_data_length));
      memcpy(transmit_buffer + sizeof(total_data_length), &details, sizeof(details));
      AttsHandleValueInd(connId, MAINTENANCE_RESULT_HANDLE, sizeof(total_data_length) + sizeof(details), transmit_buffer);
      buffer_length = (uint16_t)storage_retrieve_next_data_chunk(transmit_buffer);
      data_chunk_index = 1;
   }
   else if (is_reading)
   {
      // Determine if there is more data to be transmitted
      const uint16_t transmit_length = MIN(max_length, buffer_length - buffer_index);
      if (transmit_length)
      {
         // Transmit the next chunk of data
         AttsHandleValueInd(connId, MAINTENANCE_RESULT_HANDLE, transmit_length, transmit_buffer + buffer_index);
         buffer_index += transmit_length;

         // Ensure that there is enough buffered data to transmit again in the future without reading
         if (((buffer_length - buffer_index) < max_length) && (data_chunk_index < total_data_chunks))
         {
            memmove(transmit_buffer, transmit_buffer + buffer_index, buffer_length - buffer_index);
            buffer_length -= buffer_index;
            buffer_length += (uint16_t)storage_retrieve_next_data_chunk(transmit_buffer + buffer_length);
            ++data_chunk_index;
            buffer_index = 0;
         }
      }
      else
      {
         // Transit a completion packet
         is_reading = false;
         storage_end_reading();
         uint8_t completion_packet = BLE_MAINTENANCE_PACKET_COMPLETE;
         AttsHandleValueInd(connId, MAINTENANCE_RESULT_HANDLE, sizeof(completion_packet), &completion_packet);
      }
   }
}
