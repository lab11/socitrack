#include "imu.h"
#include "logging.h"
#include "system.h"

static void motion_changed(bool in_motion)
{
   print("Device is %s\n", in_motion ? "IN MOTION" : "STATIONARY");
}

static void data_ready(imu_data_type_t data_types_ready)
{
   static float x, y, z;
   static int16_t q_x, q_y, q_z;
   static uint8_t accuracy;

   // Read and print a summary of the IMU data
   if ((data_types_ready & IMU_LINEAR_ACCELEROMETER))
   {
      imu_read_linear_accel_data(&q_x, &q_y, &q_z, &accuracy);
      imu_convert_q_format_to_float(IMU_LINEAR_ACCELEROMETER, q_x, q_y, q_z, 0, 0, &x, &y, &z, NULL, NULL);
      print("Accelerometer Data: X = %0.4f, Y = %0.4f, Z = %0.4f (Accuracy = %u)\n", x, y, z, accuracy);
   }
   if ((data_types_ready & IMU_GYROSCOPE))
   {
      imu_read_gyro_data(&q_x, &q_y, &q_z, &accuracy);
      imu_convert_q_format_to_float(IMU_GYROSCOPE, q_x, q_y, q_z, 0, 0, &x, &y, &z, NULL, NULL);
      print("Gyroscope Data: X = %0.4f, Y = %0.4f, Z = %0.4f (Accuracy = %u)\n", x, y, z, accuracy);
   }
}

int main(void)
{
   // Set up system hardware
   setup_hardware();
   imu_init();
   system_enable_interrupts(true);

   // Register IMU data callbacks and sensors of interest
   imu_register_motion_change_callback(motion_changed);
   imu_register_data_ready_callback(data_ready);
   imu_enable_data_outputs(IMU_LINEAR_ACCELEROMETER | IMU_GYROSCOPE | IMU_MOTION_DETECT, 100000);

   while (true)
   {
      // Sleep until awoken by an interrupt
      am_hal_sysctrl_sleep(AM_HAL_SYSCTRL_SLEEP_DEEP);
#if REVISION_ID < REVISION_N
      imu_clear_interrupts();
#endif
   }

   // Should never reach this point
   return 0;
}
