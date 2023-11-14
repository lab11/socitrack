#include "imu.h"
#include "logging.h"
#include "system.h"

static void motion_interrupt(bool in_motion)
{
   print("Device is %s\n", in_motion ? "IN MOTION" : "STATIONARY");
}

int main(void)
{
   // Set up system hardware
   setup_hardware();
   imu_init();
   system_enable_interrupts(true);

   // Loop forever, waiting for IMU interrupts
   int16_t w, x, y, z;
   int8_t temp;
   imu_register_motion_change_callback(motion_interrupt, OPERATION_MODE_NDOF);
   while (true)
   {
      am_hal_delay_us(1000000);
      imu_read_accel_data(&x, &y, &z);
      print("X = %d, Y = %d, Z = %d\n", (int32_t)x, (int32_t)y, (int32_t)z);
      imu_read_linear_accel_data(&x, &y, &z);
      print("X = %d, Y = %d, Z = %d\n", (int32_t)x, (int32_t)y, (int32_t)z);
      imu_read_gravity_accel_data(&x, &y, &z);
      print("X = %d, Y = %d, Z = %d\n", (int32_t)x, (int32_t)y, (int32_t)z);
      imu_read_quaternion_data(&w, &x, &y, &z);
      print("W = %d, X = %d, Y = %d, Z = %d\n", (int32_t)w, (int32_t)x, (int32_t)y, (int32_t)z);
      imu_read_gyro_data(&x, &y, &z);
      print("gyro 1 = %d, gyro 2 = %d, gyro 3 = %d\n", (int32_t)x, (int32_t)y, (int32_t)z);
      imu_read_temp(&temp);
      print("temp:%d\n", (int32_t)temp);
   }

   // Should never reach this point
   return 0;
}
