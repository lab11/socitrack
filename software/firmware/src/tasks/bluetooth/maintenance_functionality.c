// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "app_config.h"
#include "wsf_types.h"
#include "att_main.h"
#include "bluetooth.h"
#include "logging.h"
#include "maintenance_functionality.h"
#include "maintenance_service.h"
#include "nandlog.h"
#include "storage_records.h"


// Static Global Variables ---------------------------------------------------------------------------------------------

static uint32_t download_start_timestamp = 0, download_end_timestamp = 0;
static uint16_t previous_max_length = 0;
static bool retransmitting = false;


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
            if (storage_store_experiment_details(new_details))
               app_set_experiment_start_time(new_details->experiment_start_time);
            break;
         }
         case BLE_MAINTENANCE_DELETE_EXPERIMENT:
         {
            experiment_details_t old_details = { 0 };
            storage_retrieve_experiment_details(&old_details);
            old_details.is_terminated = 1;
            if (storage_store_experiment_details(&old_details))
               app_set_experiment_start_time(old_details.experiment_start_time);
            break;
         }
         case BLE_MAINTENANCE_SET_LOG_DOWNLOAD_DATES:
         {
            download_start_timestamp = *(uint32_t*)(pValue + 1);
            download_end_timestamp = *(uint32_t*)(pValue + 1 + sizeof(download_start_timestamp));
            nandlog_retransmit_clear();
            break;
         }
         case BLE_MAINTENANCE_DOWNLOAD_LOG:
            bluetooth_request_fast_connection((uint8_t)connId, true);
#ifdef __USE_SEGGER__
            app_download_log_file(download_start_timestamp, download_end_timestamp);
#else
            continueSendingLogData(connId, 0, false);
#endif
            break;
         case BLE_MAINTENANCE_DOWNLOAD_LOG_CONTINUE:
            continueSendingLogData(connId, previous_max_length, true);
            break;
         case BLE_MAINTENANCE_RETRANSMIT_PAGES:
         {
            // [cmd][count][seq0..seqN-1]; a count of zero clears a list left over from an abandoned round
            const uint8_t count = (len > 1) ? MIN(pValue[1], BLE_MAINTENANCE_MAX_SEQS_PER_WRITE) : 0;
            if (!count)
               nandlog_retransmit_clear();
            else if (len >= (2 + (count * sizeof(uint32_t))))
            {
               // Copied out rather than cast in place, since the ATT payload is not word-aligned
               uint32_t seqs[BLE_MAINTENANCE_MAX_SEQS_PER_WRITE];
               memcpy(seqs, pValue + 2, count * sizeof(uint32_t));
               nandlog_retransmit_add(seqs, count);
            }
            break;
         }
         default:
            break;
   }
   return ATT_SUCCESS;
}

static uint16_t append_framed_page(uint8_t *buffer, uint32_t index)
{
   // Append the next page to the transmit buffer, framed with its own header
   nandlog_page_header_t page;
   const uint32_t length = retransmitting ? nandlog_retrieve_retransmit_page(index, buffer + sizeof(nandlog_wire_page_t), &page)
                                          : nandlog_retrieve_next_page(buffer + sizeof(nandlog_wire_page_t), &page);
   const nandlog_wire_page_t wire = {
      .seq = page.seq,
      .first_timestamp = page.first_timestamp,
      .last_timestamp = page.last_timestamp,
      .payload_length = page.payload_length,
      .record_count = page.record_count,
      .payload_crc = page.payload_crc
   };
   memcpy(buffer, &wire, sizeof(wire));
   return (uint16_t)(sizeof(wire) + length);
}

void continueSendingLogData(dmConnId_t connId, uint16_t max_length, bool repeat)
{
   // Define static transmission variables
   static bool started_reading = false, is_reading = false, done_reading = false;
   static uint8_t transmit_buffer[(2 * NANDLOG_MAX_PAGE_SIZE_BYTES) + sizeof(nandlog_wire_page_t)], previous_buffer[NANDLOG_MAX_PAGE_SIZE_BYTES];
   static uint32_t data_chunk_index, total_data_chunks, total_data_length;
   static uint16_t buffer_index, buffer_length, previous_length, header_length;
   static experiment_details_t pending_details;
   static bool sent_details = false;

   // Determine whether this is a new transmission or a continuation
   previous_max_length = max_length;
   if (max_length == 0)
   {
      // Send meaningless packet just to kick off reading
      is_reading = started_reading = done_reading = false;
      AttsHandleValueNtf(connId, MAINTENANCE_RESULT_HANDLE, 1, transmit_buffer);
   }
   else if (!started_reading)
   {
      // Reset all transmission variables and send estimated total data length
      buffer_index = 0;
      started_reading = true;
      experiment_details_t details;
      storage_retrieve_experiment_details(&details);
      nandlog_begin_reading(storage_experiment_ms_from_rtc(download_start_timestamp), storage_experiment_ms_from_rtc(download_end_timestamp));

      // A pending request list turns this into a repair round: only the named pages are sent, and the
      // experiment details are omitted because the host already holds them from the original transfer
      retransmitting = (nandlog_retransmit_count() > 0);
      if (retransmitting)
      {
         total_data_chunks = nandlog_retransmit_count();
         total_data_length = nandlog_retransmit_total_bytes();
      }
      else
      {
         nandlog_read_span(&total_data_chunks, &total_data_length);
      }
      const nandlog_stream_header_t stream_header = {
         .magic = NANDLOG_STREAM_MAGIC,
         .format_version = NANDLOG_FORMAT_VERSION,
         .details_length = retransmitting ? 0 : (uint16_t)sizeof(details),
         .total_pages = total_data_chunks,
         .total_payload_bytes = total_data_length
      };

      // The stream header and the experiment details are sent as SEPARATE indications - together they are
      // 255 bytes, which exceeds the 244-byte ATT payload limit at the negotiated 247-byte MTU
      pending_details = details;
      sent_details = retransmitting;
      memcpy(transmit_buffer, &stream_header, sizeof(stream_header));
      header_length = (uint16_t)sizeof(stream_header);
      AttsHandleValueNtf(connId, MAINTENANCE_RESULT_HANDLE, header_length, transmit_buffer);
   }
   else if (!sent_details)
   {
      // Second half of the header: the experiment details, in their own indication
      if (!repeat)
      {
         sent_details = true;
         memcpy(transmit_buffer, &pending_details, sizeof(pending_details));
         header_length = (uint16_t)sizeof(experiment_details_t);
      }
      AttsHandleValueNtf(connId, MAINTENANCE_RESULT_HANDLE, header_length, transmit_buffer);
   }
   else if (!is_reading && started_reading && !done_reading)
   {
      if (repeat)
         AttsHandleValueNtf(connId, MAINTENANCE_RESULT_HANDLE, header_length, transmit_buffer);
      else
      {
         buffer_length = append_framed_page(transmit_buffer, 0);
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
         AttsHandleValueNtf(connId, MAINTENANCE_RESULT_HANDLE, previous_length, previous_buffer);
      }
      else if (transmit_length)
      {
         // Transmit the next chunk of data
         AttsHandleValueNtf(connId, MAINTENANCE_RESULT_HANDLE, transmit_length, transmit_buffer + buffer_index);
         memcpy(previous_buffer, transmit_buffer + buffer_index, transmit_length);
         previous_length = transmit_length;
         buffer_index += transmit_length;

         // Ensure that there is enough buffered data to transmit again in the future without reading
         if (((buffer_length - buffer_index) < max_length) && (data_chunk_index < total_data_chunks))
         {
            memmove(transmit_buffer, transmit_buffer + buffer_index, buffer_length - buffer_index);
            buffer_length -= buffer_index;
            buffer_length += append_framed_page(transmit_buffer + buffer_length, data_chunk_index);
            ++data_chunk_index;
            buffer_index = 0;
         }
      }
      else
      {
         // Transit a completion packet
         is_reading = false;
         done_reading = true;
         nandlog_end_reading();
         nandlog_retransmit_clear();
         retransmitting = false;
         bluetooth_request_fast_connection((uint8_t)connId, false);
         uint8_t completion_packet = BLE_MAINTENANCE_PACKET_COMPLETE;
         AttsHandleValueNtf(connId, MAINTENANCE_RESULT_HANDLE, sizeof(completion_packet), &completion_packet);
         download_start_timestamp = download_end_timestamp = 0;
         bluetooth_print_buffer_stats("after log download");
      }
   }
}
