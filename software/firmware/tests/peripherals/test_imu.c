#include "imu.h"
#include "logging.h"
#include "system.h"

static void motion_interrupt(bool in_motion)
{
   print("Device is %s\n", in_motion ? "IN MOTION" : "STATIONARY");
}

static void read_data(uint8_t interrupt_status)
{
   const bool acc_fusion_data_ready = interrupt_status & 0x01;//ACC_BSX_DRDY
   //const bool mag_data_ready = interrupt_status & 0x02;//MAG_DRDY
   //const bool gyro_data_ready = interrupt_status & 0x10;//GYR_DRDY
   if (acc_fusion_data_ready)
   {
      int8_t temp;
      bno55_calib_status_t status = {0};
      bno055_calib_offsets_t offsets = {0};
      bno055_axis_remap_t remap = {0};
      bno055_quaternion_t quaternion = {0};
      bno055_euler_t euler = {0};
      bno055_euler_t device_calculated_euler = {0};
      bno055_acc_t acc = {0};
      bno055_gyro_t gyro = {0};
      imu_read_calibration_status(&status);
      //imu_read_calibration_offsets(&offsets);
      imu_read_linear_accel_data(&acc);
      imu_read_quaternion_data(&quaternion);
      quaternion_to_euler(quaternion, &euler);
      imu_read_gyro_data(&gyro); 
      //imu_read_temp(&temp);
      //print("temp:%d\n", (int32_t)temp);
      print("Calibration status: sys %u, gyro %u, accel %u, mag %u\n",status.sys, status.gyro, status.accel, status.mag);
      //print("Calibration offsets: %d, %d, %d \n", offsets.gyro_offset_x, offsets.gyro_offset_y, offsets.gyro_offset_z);
      print("Linear Accel X = %d, Y = %d, Z = %d, qw = %d, qx = %d, qy = %d, qz = %d, gx = %d, gy = %d, gz = %d\n", acc.x, acc.y, acc.z, quaternion.w, quaternion.x, quaternion.y, quaternion.z, gyro.x, gyro.y,gyro.z);
   }
}

int main(void)
{
   // Set up system hardware
   setup_hardware();
   imu_init();
   system_enable_interrupts(true);

   uint8_t rev_msb, rev_lsb;
   //bno055_axis_remap_t remap = {.x_remap_val = 1, .y_remap_val = 0, .z_remap_val = 2};
   bno055_axis_remap_t remap = {0};

   imu_set_fusion_mode(OPERATION_MODE_NDOF);
   imu_register_motion_change_callback(motion_interrupt);
   //imu_register_data_ready_callback(read_data);
   imu_set_power_mode(POWER_MODE_NORMAL);

   imu_read_fw_version(&rev_msb, &rev_lsb);
   print("BNO055 firmware version:%x.%x\n",rev_msb, rev_lsb);

   //imu_read_axis_remap(&remap);
   //if (imu_set_axis_remap(remap)){print("remap success!\n");}
   //print("BNO055 X mapping:%u, Y mapping:%u, Z mapping:%u, X sign:%u, Y sign:%u, Z sign:%u\n", remap.x_remap_val, remap.y_remap_val, remap.z_remap_val, remap.x_remap_sign, remap.y_remap_sign, remap.z_remap_sign);

   while (true){
	   ;
   }

   // Should never reach this point
   return 0;
}
