#include "imu.h"
#include "logging.h"
#include "system.h"

#define IMU_SAMPLE_QUEUE_LENGTH 128

typedef struct
{
   imu_data_type_t data_types_ready;
   int16_t accel[3], gyro[3];
   uint8_t accel_accuracy, gyro_accuracy;
   uint8_t shtp_sequence;
   uint32_t shtp_continuation_count;
   uint32_t isr_timestamp_ticks;
   int32_t sample_offset_us;
} imu_sample_t;

static imu_sample_t sample_queue[IMU_SAMPLE_QUEUE_LENGTH];
static volatile uint16_t sample_read_index, sample_write_index;
static volatile uint32_t dropped_samples;
static volatile bool motion_change_pending, current_motion_state;

static void motion_changed(bool in_motion)
{
   current_motion_state = in_motion;
   motion_change_pending = true;
}

static void data_ready(imu_data_type_t data_types_ready)
{
   if (!(data_types_ready & (IMU_ACCELEROMETER | IMU_GYROSCOPE)))
      return;

   const uint16_t next_write_index = (sample_write_index + 1) % IMU_SAMPLE_QUEUE_LENGTH;
   if (next_write_index == sample_read_index)
   {
      ++dropped_samples;
      return;
   }

   imu_sample_t *sample = &sample_queue[sample_write_index];
   sample->data_types_ready = data_types_ready;
#if REVISION_ID >= REVISION_N
   sample->shtp_sequence = imu_read_shtp_sequence();
   sample->shtp_continuation_count = imu_read_shtp_continuation_count();
   sample->isr_timestamp_ticks = imu_read_isr_timestamp_ticks();
   sample->sample_offset_us = imu_read_sample_time_offset_us();
#endif
   if (data_types_ready & IMU_ACCELEROMETER)
      imu_read_accel_data(&sample->accel[0], &sample->accel[1], &sample->accel[2], &sample->accel_accuracy);
   if (data_types_ready & IMU_GYROSCOPE)
      imu_read_gyro_data(&sample->gyro[0], &sample->gyro[1], &sample->gyro[2], &sample->gyro_accuracy);
   sample_write_index = next_write_index;
}

static bool read_sample(imu_sample_t *sample)
{
   bool available;
   AM_CRITICAL_BEGIN
   available = sample_read_index != sample_write_index;
   if (available)
   {
      *sample = sample_queue[sample_read_index];
      sample_read_index = (sample_read_index + 1) % IMU_SAMPLE_QUEUE_LENGTH;
   }
   AM_CRITICAL_END
   return available;
}

static void print_sample(const imu_sample_t *sample)
{
   static float x, y, z;
#if REVISION_ID >= REVISION_N
   static uint16_t previous_shtp_sequence = 0x100;
   const uint32_t isr_timestamp_us = (uint32_t)(((uint64_t)sample->isr_timestamp_ticks * 1000000) / configSTIMER_CLOCK_HZ);
   if (sample->shtp_sequence != previous_shtp_sequence)
   {
      previous_shtp_sequence = sample->shtp_sequence;
      print("IMU batch ending at SHTP packet %u: continuations=%u, HINT proxy=%u us (first ISR entry)\n",
         sample->shtp_sequence, sample->shtp_continuation_count, isr_timestamp_us);
   }
   print("sample timestamp=%u us (HINT offset=%d us)\n", (uint32_t)((int64_t)isr_timestamp_us + sample->sample_offset_us), sample->sample_offset_us);
#endif
   print("data_ready called!");
   if (sample->data_types_ready & IMU_ACCELEROMETER)
   {
      imu_convert_q_format_to_float(IMU_ACCELEROMETER, sample->accel[0], sample->accel[1], sample->accel[2], 0, 0, &x, &y, &z, NULL, NULL);
      print("Accelerometer Data: X = %0.4f, Y = %0.4f, Z = %0.4f (Accuracy = %u)\n", x, y, z, sample->accel_accuracy);
   }
   if (sample->data_types_ready & IMU_GYROSCOPE)
   {
      imu_convert_q_format_to_float(IMU_GYROSCOPE, sample->gyro[0], sample->gyro[1], sample->gyro[2], 0, 0, &x, &y, &z, NULL, NULL);
      print("Gyroscope Data: X = %0.4f, Y = %0.4f, Z = %0.4f (Accuracy = %u)\n", x, y, z, sample->gyro_accuracy);
   }
}

int main(void)
{
   // Set up system hardware
   setup_hardware();
   imu_init();

   // Register IMU data callbacks and sensors of interest
   imu_register_motion_change_callback(motion_changed);
   imu_register_data_ready_callback(data_ready);
#if REVISION_ID >= REVISION_N
   am_hal_stimer_config(AM_HAL_STIMER_XTAL_32KHZ);
   am_hal_stimer_counter_clear();
   imu_set_batch_interval(50000); //50 ms
#endif
   system_enable_interrupts(true);
   imu_enable_data_outputs(IMU_ACCELEROMETER | IMU_GYROSCOPE| IMU_MOTION_DETECT, 10000);

   while (true)
   {
      static imu_sample_t sample;
      const bool sample_available = read_sample(&sample);
      if (sample_available)
         print_sample(&sample);

      uint32_t dropped;
      bool report_motion_change, in_motion;
      AM_CRITICAL_BEGIN
      dropped = dropped_samples;
      dropped_samples = 0;
      report_motion_change = motion_change_pending;
      motion_change_pending = false;
      in_motion = current_motion_state;
      AM_CRITICAL_END
      if (dropped)
         print("WARNING: Dropped %u IMU samples\n", dropped);
      if (report_motion_change)
         print("Device is %s\n", in_motion ? "IN MOTION" : "STATIONARY");
      if (sample_available)
         continue;

      // Sleep until awoken by an interrupt
      am_hal_sysctrl_sleep(AM_HAL_SYSCTRL_SLEEP_DEEP);
#if REVISION_ID < REVISION_N
      imu_clear_interrupts();
#endif
   }

   // Should never reach this point
   return 0;
}
