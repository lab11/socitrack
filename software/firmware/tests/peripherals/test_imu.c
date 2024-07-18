#include "imu.h"
#include "logging.h"
#include "system.h"

static void motion_changed(bool in_motion)
{
   print("Device is %s\n", in_motion ? "IN MOTION" : "STATIONARY");
}

static void data_ready(uint8_t *calib_data, int16_t *linear_accel_data, uint8_t *raw_data, uint32_t raw_data_length)
{
   // Pick the relevant data types from the raw data buffer
   uint8_t *calib, *lacc, *quat, *gyro;
   imu_pick_data_from_raw(&calib, raw_data, STAT_DATA);
   imu_pick_data_from_raw(&lacc, raw_data, LACC_DATA);
   imu_pick_data_from_raw(&quat, raw_data, QUAT_DATA);
   imu_pick_data_from_raw(&gyro, raw_data, GYRO_DATA);

   // Print a summary of the IMU data
   print("IMU Data:\n");
   print("  Calibration status: sys %u, gyro %u, accel %u, mag %u\n", calib[0] >> 6, (calib[0] >> 4) & 0x03, (calib[0] >> 2) & 0x03, calib[0] & 0x03);
   print("  Linear Accel: X = %d, Y = %d, Z = %d\n", lacc[0], lacc[1], lacc[2]);
   print("  Quaternion: qw = %d, qx = %d, qy = %d, qz = %d\n", quat[0], quat[1], quat[2], quat[3]);
   print("  Gyroscope: gx = %d, gy = %d, gz = %d\n", gyro[0], gyro[1], gyro[2]);
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
