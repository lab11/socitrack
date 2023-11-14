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
   int8_t temp;
   uint8_t rev_msb, rev_lsb;
   bno55_calib_status_t status;
   bno055_calib_offsets_t offsets;
   bno055_axis_remap_t remap;
   bno055_quaternion_t quaternion;
   bno055_acc_t acc;

   imu_register_motion_change_callback(motion_interrupt, OPERATION_MODE_NDOF);

   imu_read_fw_version(&rev_msb, &rev_lsb);
   print("BNO055 firmware version:%u.%u\n",rev_msb, rev_lsb);

   imu_read_axis_remap(&remap);
   print("BNO055 X mapping:%u, Y mapping:%u, Z mapping:%u, X sign:%u, Y sign:%u, Z sign:%u\n", remap.x_remap_val, remap.y_remap_val, remap.z_remap_val, remap.x_remap_sign, remap.y_remap_sign, remap.z_remap_sign);

   while (true)
   {
      am_hal_delay_us(20000);
      //imu_read_accel_data(&acc);
      //print("Accel X = %d, Y = %d, Z = %d\n", (int32_t)acc.x, (int32_t)acc.y, (int32_t)acc.z);
      imu_read_linear_accel_data(&acc);
      imu_read_quaternion_data(&quaternion);
      print("Linear Accel X = %d, Y = %d, Z = %d, qw = %d, qx = %d, qy = %d, qz = %d\n", (int32_t)acc.x, (int32_t)acc.y, (int32_t)acc.z, (int32_t)quaternion.w, (int32_t)quaternion.x, (int32_t)quaternion.y, (int32_t)quaternion.z);
      //imu_read_gravity_accel_data(&acc);
      //print("Gravity Accel X = %d, Y = %d, Z = %d\n", (int32_t)acc.x, (int32_t)acc.y, (int32_t)acc.z);
      //imu_read_gyro_data(&x, &y, &z);
      //print("gyro 1 = %d, gyro 2 = %d, gyro 3 = %d\n", (int32_t)x, (int32_t)y, (int32_t)z);
      //imu_read_temp(&temp);
      //print("temp:%d\n", (int32_t)temp);

      //0: not calibrated; 3: fully calibrated
      //imu_read_calibration_status(&status);
      //print("Calibration status: sys %u, gyro %u, accel %u, mag %u\n",status.sys, status.gyro, status.accel, status.mag);
      //imu_read_calibration_offsets(&offsets);
      //print("Calibration offsets: %d, %d, %d \n", offsets.gyro_offset_x, offsets.gyro_offset_y, offsets.gyro_offset_z);

   }

   // Should never reach this point
   return 0;
}
