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
   int16_t x, y, z;
   imu_register_motion_change_callback(motion_interrupt);
   while (true)
   {
      am_hal_delay_us(5000000);
      imu_read_accel_data(&x, &y, &z);
      print("X = %d, Y = %d, Z = %d\n", (int32_t)x, (int32_t)y, (int32_t)z);
   }

   // Should never reach this point
   return 0;
}
