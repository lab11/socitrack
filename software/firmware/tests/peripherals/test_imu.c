#include "imu.h"
#include "logging.h"
#include "system.h"

static void motion_changed(bool in_motion)
{
   print("Device is %s\n", in_motion ? "IN MOTION" : "STATIONARY");
}

static void data_ready(int16_t *gyro_data, int16_t *linear_accel_data, int16_t *gravity_data, int16_t *quaternion_data, uint8_t *calib_data, uint8_t *raw_data, uint32_t raw_data_length)
{
   // Print out calibration status
   print("IMU Data:\n");
   print("  Calibration status: sys %u, gyro %u, accel %u, mag %u\n", calib_data[0] >> 6, (calib_data[0] >> 4) & 0x03, (calib_data[0] >> 2) & 0x03, calib_data[0] & 0x03);
   print("  Linear Accel: X = %d, Y = %d, Z = %d\n", linear_accel_data[0], linear_accel_data[1], linear_accel_data[2]);
   print("  Quaternion: qw = %d, qx = %d, qy = %d, qz = %d\n", quaternion_data[0], quaternion_data[1], quaternion_data[2], quaternion_data[3]);
   print("  Gyroscope: gx = %d, gy = %d, gz = %d\n", gyro_data[0], gyro_data[1], gyro_data[2]);
}

int main(void)
{
   // Set up system hardware
   setup_hardware();
   imu_init();
   system_enable_interrupts(true);

   uint8_t rev_msb, rev_lsb;
   //bno055_axis_remap_t remap = {.x_remap_val = 1, .y_remap_val = 0, .z_remap_val = 2};
   //bno055_axis_remap_t remap = {0};
   imu_read_fw_version(&rev_msb, &rev_lsb);
   print("BNO055 firmware version: %x.%x\n", rev_msb, rev_lsb);

   imu_register_motion_change_callback(motion_changed);
   imu_register_data_ready_callback(data_ready);

   imu_set_power_mode(POWER_MODE_NORMAL);
   imu_set_fusion_mode(OPERATION_MODE_NDOF);


   //imu_read_axis_remap(&remap);
   //if (imu_set_axis_remap(remap)){print("remap success!\n");}
   //print("BNO055 X mapping:%u, Y mapping:%u, Z mapping:%u, X sign:%u, Y sign:%u, Z sign:%u\n", remap.x_remap_val, remap.y_remap_val, remap.z_remap_val, remap.x_remap_sign, remap.y_remap_sign, remap.z_remap_sign);

   while (true);

   // Should never reach this point
   return 0;
}
