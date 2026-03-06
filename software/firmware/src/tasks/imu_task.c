// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "app_tasks.h"
#include "imu.h"
#include "logging.h"


#if defined(_TEST_IMU_DATA) && defined(__USE_FREERTOS__) && (REVISION_ID >= REVISION_N)

// IMU Batch Types ------------------------------------------------------------------------------------------------------

typedef struct
{
   uint8_t report_id, value_count;
   uint16_t status_and_timestamp;
   int16_t values[IMU_BATCH_MAX_VALUES_PER_SAMPLE];
} imu_batch_sample_t;


// Static Global Variables ---------------------------------------------------------------------------------------------

static TaskHandle_t imu_task_handle;
static imu_batch_sample_t imu_batch_samples[IMU_BATCH_MAX_SAMPLES];
static uint8_t imu_batch_payload[UINT8_MAX];
static uint8_t imu_batch_sample_count;
static bool imu_batch_invalid;


// Private Helper Functions --------------------------------------------------------------------------------------------

static void data_pending_handler(void)
{
   BaseType_t xHigherPriorityTaskWoken = pdFALSE;
   vTaskNotifyGiveFromISR(imu_task_handle, &xHigherPriorityTaskWoken);
   portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

static bool build_imu_batch_payload(uint8_t *payload_length)
{
   uint16_t length = 0;

   // Group samples with the same BNO report ID into one sensor block.
   for (uint8_t i = 0; i < imu_batch_sample_count; ++i)
   {
      bool first_sample_for_report = true;
      for (uint8_t j = 0; j < i; ++j)
         if (imu_batch_samples[j].report_id == imu_batch_samples[i].report_id)
         {
            first_sample_for_report = false;
            break;
         }
      if (!first_sample_for_report)
         continue;

      if ((length + IMU_SENSOR_BLOCK_HEADER_LENGTH) > UINT8_MAX)
         return false;
      const uint16_t block_header = length;
      imu_batch_payload[length++] = imu_batch_samples[i].report_id;
      imu_batch_payload[length++] = 0;

      for (uint8_t j = i; j < imu_batch_sample_count; ++j)
         if (imu_batch_samples[j].report_id == imu_batch_samples[i].report_id)
         {
            const uint8_t sample_length = sizeof(imu_batch_samples[j].status_and_timestamp) +
               (imu_batch_samples[j].value_count * sizeof(imu_batch_samples[j].values[0]));
            if ((imu_batch_samples[j].value_count != imu_batch_samples[i].value_count) ||
                ((length + sample_length) > UINT8_MAX))
               return false;
            memcpy(&imu_batch_payload[length], &imu_batch_samples[j].status_and_timestamp,
               sizeof(imu_batch_samples[j].status_and_timestamp));
            length += sizeof(imu_batch_samples[j].status_and_timestamp);
            memcpy(&imu_batch_payload[length], imu_batch_samples[j].values,
               imu_batch_samples[j].value_count * sizeof(imu_batch_samples[j].values[0]));
            length += imu_batch_samples[j].value_count * sizeof(imu_batch_samples[j].values[0]);
            ++imu_batch_payload[block_header + 1];
         }
   }

   *payload_length = (uint8_t)length;
   return true;
}

static void finish_imu_batch(void)
{
   uint8_t payload_length = 0;
   const bool payload_ready = !imu_batch_invalid && build_imu_batch_payload(&payload_length);

   if (payload_ready && payload_length)
   {
      const uint32_t elapsed_ticks = am_hal_stimer_counter_get() - imu_read_isr_timestamp_ticks();
      const int32_t anchor_time_offset_ms = -(int32_t)((((uint64_t)elapsed_ticks * 1000) +
         (configSTIMER_CLOCK_HZ / 2)) / configSTIMER_CLOCK_HZ);
      storage_write_imu_batch(imu_batch_payload, payload_length, anchor_time_offset_ms);
   }
   else if (!payload_ready)
      print("WARNING: IMU batch does not fit the storage record format\n");

   imu_batch_sample_count = 0;
   imu_batch_invalid = false;
}


// Public API Functions ------------------------------------------------------------------------------------------------

void append_imu_batch_sample(const int16_t *values, uint8_t value_count, uint8_t status)
{
   const int32_t timestamp_offset_us = imu_read_sample_time_offset_us();
   const int32_t timestamp_offset_100us = timestamp_offset_us / 100;
   if ((imu_batch_sample_count >= IMU_BATCH_MAX_SAMPLES) ||
       (value_count > IMU_BATCH_MAX_VALUES_PER_SAMPLE) ||
       ((timestamp_offset_100us * 100) != timestamp_offset_us) ||
       (timestamp_offset_100us < IMU_SAMPLE_MIN_TIMESTAMP_OFFSET_100US) ||
       (timestamp_offset_100us > IMU_SAMPLE_MAX_TIMESTAMP_OFFSET_100US))
   {
      imu_batch_invalid = true;
      return;
   }

   imu_batch_sample_t *sample = &imu_batch_samples[imu_batch_sample_count++];
   sample->report_id = imu_read_report_id();
   sample->value_count = value_count;
   // The low 14 bits are a signed two's-complement offset in 100 us units.
   sample->status_and_timestamp = ((uint16_t)(status & IMU_SAMPLE_STATUS_MASK) << IMU_SAMPLE_STATUS_SHIFT) |
      ((uint16_t)timestamp_offset_100us & IMU_SAMPLE_TIMESTAMP_MASK);
   memcpy(sample->values, values, value_count * sizeof(*values));
}


// Task Function --------------------------------------------------------------------------------------------------------

void IMUTask(void *params)
{
   (void)params;
   imu_task_handle = xTaskGetCurrentTaskHandle();
   imu_register_data_pending_callback(data_pending_handler);

   while (true)
   {
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      if (imu_process_pending_data())
         finish_imu_batch();
   }
}

#endif
