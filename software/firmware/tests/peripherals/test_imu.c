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
      //int8_t temp;
      bno55_calib_status_t status = {0};
      //bno055_calib_offsets_t offsets = {0};
      //bno055_axis_remap_t remap = {0};
      bno055_quaternion_t quaternion = {0};
      bno055_euler_t euler = {0};
      //bno055_euler_t device_calculated_euler = {0};
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

static void handle_burst_data(uint8_t *localBuffer){
    int16_t read_buffer[BURST_READ_SIZE/2] = {0};

    for (uint32_t i = 0; i < BURST_READ_SIZE/2; i++)
    {
       read_buffer[i] = ((int16_t)localBuffer[i*2]) | (((int16_t)localBuffer[i*2+1]) << 8);
    }

    uint8_t reg_value = localBuffer[BNO055_CALIB_STAT_ADDR-BURST_READ_BASE_ADDR];
    bno55_calib_status_t status = {0};
    status.mag = reg_value & 0x03;
    status.accel = (reg_value >> 2) & 0x03;
    status.gyro = (reg_value >> 4) & 0x03;
    status.sys = (reg_value >> 6) & 0x03;
    print("Calibration status: sys %u, gyro %u, accel %u, mag %u\n",status.sys, status.gyro, status.accel, status.mag);

    int16_t quaternion_data[4];
    memcpy(quaternion_data, read_buffer + (BNO055_QUATERNION_DATA_W_LSB_ADDR-BURST_READ_BASE_ADDR)/2, 4 * sizeof(int16_t));
    //temporary fix of MSB sign bit flipping problem
    for (uint8_t i = 0; i < 4; i++)
    {
        if (quaternion_data[i]>16384){
           quaternion_data[i] = quaternion_data[i] - 32768;
        }
        else if (quaternion_data[i]<-16384){
            quaternion_data[i] = quaternion_data[i] + 32768;
        }
    }
    bno055_acc_t lacc = {0};
    memcpy(&lacc, read_buffer + (BNO055_LINEAR_ACCEL_DATA_X_LSB_ADDR-BURST_READ_BASE_ADDR)/2, 3 * sizeof(int16_t));
    bno055_gyro_t gyro = {0};
    memcpy(&gyro, read_buffer + (BNO055_GYRO_DATA_X_LSB_ADDR-BURST_READ_BASE_ADDR)/2, 3 * sizeof(int16_t));

    print("Linear Accel X = %d, Y = %d, Z = %d, qw = %d, qx = %d, qy = %d, qz = %d, gx = %d, gy = %d, gz = %d\n",lacc.x,lacc.y,lacc.z,quaternion_data[0],quaternion_data[1],quaternion_data[2],quaternion_data[3],gyro.x,gyro.y,gyro.z);
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
   print("BNO055 firmware version:%x.%x\n",rev_msb, rev_lsb);

   imu_register_motion_change_callback(motion_interrupt);

#if NONBLOCKING
   imu_register_data_ready_callback(handle_burst_data);
#else
   imu_register_data_ready_callback(read_data);
#endif
   //imu_register_data_ready_callback(NULL);
   imu_set_power_mode(POWER_MODE_NORMAL);
   imu_set_fusion_mode(OPERATION_MODE_NDOF);


   //imu_read_axis_remap(&remap);
   //if (imu_set_axis_remap(remap)){print("remap success!\n");}
   //print("BNO055 X mapping:%u, Y mapping:%u, Z mapping:%u, X sign:%u, Y sign:%u, Z sign:%u\n", remap.x_remap_val, remap.y_remap_val, remap.z_remap_val, remap.x_remap_sign, remap.y_remap_sign, remap.z_remap_sign);

   while (true){
	   ;
   }

   // Should never reach this point
   return 0;
}
