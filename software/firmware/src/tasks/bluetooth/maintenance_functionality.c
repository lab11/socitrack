// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "app_config.h"
#include "wsf_types.h"
#include "att_main.h"
#include "logging.h"
#include "maintenance_functionality.h"
#include "maintenance_service.h"
#include "storage.h"


// Static Global Variables ---------------------------------------------------------------------------------------------

static uint32_t download_start_timestamp = 0, download_end_timestamp = 0;


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
         case BLE_MAINTENANCE_SET_LOG_DOWNLOAD_DATES:
         {
            download_start_timestamp = *(uint32_t*)(pValue + 1);
            download_end_timestamp = *(uint32_t*)(pValue + 1 + sizeof(download_start_timestamp));
            break;
         }
         case BLE_MAINTENANCE_DOWNLOAD_LOG:
            continueSendingLogData(connId, 0, false);
            break;
         default:
            break;
   }
   return ATT_SUCCESS;
}

void continueSendingLogData(dmConnId_t connId, uint16_t max_length, bool repeat)
{
   // Define static transmission variables
   static bool started_reading = false, is_reading = false, done_reading = false;
   static uint8_t transmit_buffer[2 * MEMORY_PAGE_SIZE_BYTES], previous_buffer[MEMORY_PAGE_SIZE_BYTES];
   static uint32_t data_chunk_index, total_data_chunks, total_data_length;
   static uint16_t buffer_index, buffer_length, previous_length;

   // Determine whether this is a new transmission or a continuation
   if (max_length == 0)
   {
      // Send meaningless packet just to kick off reading
      is_reading = started_reading = done_reading = false;
      AttsHandleValueInd(connId, MAINTENANCE_RESULT_HANDLE, 1, transmit_buffer);
   }
   else if (!started_reading)
   {
      // Reset all transmission variables and send estimated total data length
      buffer_index = 0;
      started_reading = true;
      experiment_details_t details;
      storage_begin_reading(download_start_timestamp);
      storage_retrieve_experiment_details(&details);
      total_data_chunks = storage_retrieve_num_data_chunks(download_end_timestamp);
      total_data_length = total_data_chunks * MEMORY_NUM_DATA_BYTES_PER_PAGE;
      memcpy(transmit_buffer, &total_data_length, sizeof(total_data_length));
      memcpy(transmit_buffer + sizeof(total_data_length), &details, sizeof(details));
      AttsHandleValueInd(connId, MAINTENANCE_RESULT_HANDLE, sizeof(total_data_length) + sizeof(experiment_details_t), transmit_buffer);
   }
   else if (!is_reading && started_reading && !done_reading)
   {
      if (repeat)
         AttsHandleValueInd(connId, MAINTENANCE_RESULT_HANDLE, sizeof(total_data_length) + sizeof(experiment_details_t), transmit_buffer);
      else
      {
         buffer_length = (uint16_t)storage_retrieve_next_data_chunk(transmit_buffer);
         data_chunk_index = 1;
         is_reading = true;
      }
   }

   // Handle data reading
   if (is_reading)
   {
      // Determine if there is more data to be transmitted
      const uint16_t transmit_length = MIN(max_length, buffer_length - buffer_index);
      if (repeat)
      {
         // Re-transmit the previous chunk of data
         AttsHandleValueInd(connId, MAINTENANCE_RESULT_HANDLE, previous_length, previous_buffer);
      }
      else if (transmit_length)
      {
         // Transmit the next chunk of data
         AttsHandleValueInd(connId, MAINTENANCE_RESULT_HANDLE, transmit_length, transmit_buffer + buffer_index);
         memcpy(previous_buffer, transmit_buffer + buffer_index, transmit_length);
         previous_length = transmit_length;
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
         done_reading = true;
         storage_end_reading();
         uint8_t completion_packet = BLE_MAINTENANCE_PACKET_COMPLETE;
         AttsHandleValueInd(connId, MAINTENANCE_RESULT_HANDLE, sizeof(completion_packet), &completion_packet);
      }
   }
}
