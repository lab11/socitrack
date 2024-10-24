#ifndef __IMU_HEADER_H__
#define __IMU_HEADER_H__

// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "app_config.h"


// Peripheral Type Definitions -----------------------------------------------------------------------------------------

#define RAD_TO_DEG(radian) (radian * 180.0 / 3.14159265358979f)

#define BNO055_ID 0xA0

#define ACC_DATA_LEN  6
#define GYRO_DATA_LEN 6
#define LACC_DATA_LEN 6
#define GACC_DATA_LEN 6
#define QUAT_DATA_LEN 8
#define STAT_DATA_LEN 1

// IMU data callback type definitions
typedef void (*motion_change_callback_t)(bool in_motion);
#ifdef _TEST_IMU_DATA
typedef void (*data_ready_callback_t)(uint8_t *raw_data, uint32_t raw_data_length);
#else
typedef void (*data_ready_callback_t)(uint8_t *calib_data, int16_t *linear_accel_data);
#endif

// Burst data transfer definitions
#define BURST_READ_BASE_ADDR    BNO055_GYRO_DATA_X_LSB_ADDR
#define BURST_READ_LAST_ADDR    BNO055_INTR_STAT_ADDR
#define BURST_READ_LEN          (BURST_READ_LAST_ADDR - BURST_READ_BASE_ADDR + 1)

typedef enum
{
   // Page ID register definition
   BNO055_PAGE_ID_ADDR = 0X07,

   // PAGE0 REGISTER DEFINITION START
   BNO055_CHIP_ID_ADDR = 0x00,
   BNO055_ACCEL_REV_ID_ADDR = 0x01,
   BNO055_MAG_REV_ID_ADDR = 0x02,
   BNO055_GYRO_REV_ID_ADDR = 0x03,
   BNO055_SW_REV_ID_LSB_ADDR = 0x04,
   BNO055_SW_REV_ID_MSB_ADDR = 0x05,
   BNO055_BL_REV_ID_ADDR = 0X06,

   // Accel data registers
   BNO055_ACCEL_DATA_X_LSB_ADDR = 0X08,
   BNO055_ACCEL_DATA_X_MSB_ADDR = 0X09,
   BNO055_ACCEL_DATA_Y_LSB_ADDR = 0X0A,
   BNO055_ACCEL_DATA_Y_MSB_ADDR = 0X0B,
   BNO055_ACCEL_DATA_Z_LSB_ADDR = 0X0C,
   BNO055_ACCEL_DATA_Z_MSB_ADDR = 0X0D,

   // Mag data registers
   BNO055_MAG_DATA_X_LSB_ADDR = 0X0E,
   BNO055_MAG_DATA_X_MSB_ADDR = 0X0F,
   BNO055_MAG_DATA_Y_LSB_ADDR = 0X10,
   BNO055_MAG_DATA_Y_MSB_ADDR = 0X11,
   BNO055_MAG_DATA_Z_LSB_ADDR = 0X12,
   BNO055_MAG_DATA_Z_MSB_ADDR = 0X13,

   // Gyro data registers
   BNO055_GYRO_DATA_X_LSB_ADDR = 0X14,
   BNO055_GYRO_DATA_X_MSB_ADDR = 0X15,
   BNO055_GYRO_DATA_Y_LSB_ADDR = 0X16,
   BNO055_GYRO_DATA_Y_MSB_ADDR = 0X17,
   BNO055_GYRO_DATA_Z_LSB_ADDR = 0X18,
   BNO055_GYRO_DATA_Z_MSB_ADDR = 0X19,

   // Euler data registers
   BNO055_EULER_H_LSB_ADDR = 0X1A,
   BNO055_EULER_H_MSB_ADDR = 0X1B,
   BNO055_EULER_R_LSB_ADDR = 0X1C,
   BNO055_EULER_R_MSB_ADDR = 0X1D,
   BNO055_EULER_P_LSB_ADDR = 0X1E,
   BNO055_EULER_P_MSB_ADDR = 0X1F,

   // Quaternion data registers
   BNO055_QUATERNION_DATA_W_LSB_ADDR = 0X20,
   BNO055_QUATERNION_DATA_W_MSB_ADDR = 0X21,
   BNO055_QUATERNION_DATA_X_LSB_ADDR = 0X22,
   BNO055_QUATERNION_DATA_X_MSB_ADDR = 0X23,
   BNO055_QUATERNION_DATA_Y_LSB_ADDR = 0X24,
   BNO055_QUATERNION_DATA_Y_MSB_ADDR = 0X25,
   BNO055_QUATERNION_DATA_Z_LSB_ADDR = 0X26,
   BNO055_QUATERNION_DATA_Z_MSB_ADDR = 0X27,

   // Linear acceleration data registers
   BNO055_LINEAR_ACCEL_DATA_X_LSB_ADDR = 0X28,
   BNO055_LINEAR_ACCEL_DATA_X_MSB_ADDR = 0X29,
   BNO055_LINEAR_ACCEL_DATA_Y_LSB_ADDR = 0X2A,
   BNO055_LINEAR_ACCEL_DATA_Y_MSB_ADDR = 0X2B,
   BNO055_LINEAR_ACCEL_DATA_Z_LSB_ADDR = 0X2C,
   BNO055_LINEAR_ACCEL_DATA_Z_MSB_ADDR = 0X2D,

   // Gravity data registers
   BNO055_GRAVITY_DATA_X_LSB_ADDR = 0X2E,
   BNO055_GRAVITY_DATA_X_MSB_ADDR = 0X2F,
   BNO055_GRAVITY_DATA_Y_LSB_ADDR = 0X30,
   BNO055_GRAVITY_DATA_Y_MSB_ADDR = 0X31,
   BNO055_GRAVITY_DATA_Z_LSB_ADDR = 0X32,
   BNO055_GRAVITY_DATA_Z_MSB_ADDR = 0X33,

   // Temperature data register
   BNO055_TEMP_ADDR = 0X34,

   // Status registers
   BNO055_CALIB_STAT_ADDR = 0X35,
   BNO055_SELFTEST_RESULT_ADDR = 0X36,
   BNO055_INTR_STAT_ADDR = 0X37,
   BNO055_SYS_CLK_STAT_ADDR = 0X38,
   BNO055_SYS_STAT_ADDR = 0X39,
   BNO055_SYS_ERR_ADDR = 0X3A,

   // Unit selection register
   BNO055_UNIT_SEL_ADDR = 0X3B,

   // Mode registers
   BNO055_OPR_MODE_ADDR = 0X3D,
   BNO055_PWR_MODE_ADDR = 0X3E,

   BNO055_SYS_TRIGGER_ADDR = 0X3F,
   BNO055_TEMP_SOURCE_ADDR = 0X40,

   // Axis remap registers
   BNO055_AXIS_MAP_CONFIG_ADDR = 0X41,
   BNO055_AXIS_MAP_SIGN_ADDR = 0X42,

   // SIC registers
   BNO055_SIC_MATRIX_0_LSB_ADDR = 0X43,
   BNO055_SIC_MATRIX_0_MSB_ADDR = 0X44,
   BNO055_SIC_MATRIX_1_LSB_ADDR = 0X45,
   BNO055_SIC_MATRIX_1_MSB_ADDR = 0X46,
   BNO055_SIC_MATRIX_2_LSB_ADDR = 0X47,
   BNO055_SIC_MATRIX_2_MSB_ADDR = 0X48,
   BNO055_SIC_MATRIX_3_LSB_ADDR = 0X49,
   BNO055_SIC_MATRIX_3_MSB_ADDR = 0X4A,
   BNO055_SIC_MATRIX_4_LSB_ADDR = 0X4B,
   BNO055_SIC_MATRIX_4_MSB_ADDR = 0X4C,
   BNO055_SIC_MATRIX_5_LSB_ADDR = 0X4D,
   BNO055_SIC_MATRIX_5_MSB_ADDR = 0X4E,
   BNO055_SIC_MATRIX_6_LSB_ADDR = 0X4F,
   BNO055_SIC_MATRIX_6_MSB_ADDR = 0X50,
   BNO055_SIC_MATRIX_7_LSB_ADDR = 0X51,
   BNO055_SIC_MATRIX_7_MSB_ADDR = 0X52,
   BNO055_SIC_MATRIX_8_LSB_ADDR = 0X53,
   BNO055_SIC_MATRIX_8_MSB_ADDR = 0X54,

   // Accelerometer Offset registers
   ACCEL_OFFSET_X_LSB_ADDR = 0X55,
   ACCEL_OFFSET_X_MSB_ADDR = 0X56,
   ACCEL_OFFSET_Y_LSB_ADDR = 0X57,
   ACCEL_OFFSET_Y_MSB_ADDR = 0X58,
   ACCEL_OFFSET_Z_LSB_ADDR = 0X59,
   ACCEL_OFFSET_Z_MSB_ADDR = 0X5A,

   // Magnetometer Offset registers
   MAG_OFFSET_X_LSB_ADDR = 0X5B,
   MAG_OFFSET_X_MSB_ADDR = 0X5C,
   MAG_OFFSET_Y_LSB_ADDR = 0X5D,
   MAG_OFFSET_Y_MSB_ADDR = 0X5E,
   MAG_OFFSET_Z_LSB_ADDR = 0X5F,
   MAG_OFFSET_Z_MSB_ADDR = 0X60,

   // Gyroscope Offset registers
   GYRO_OFFSET_X_LSB_ADDR = 0X61,
   GYRO_OFFSET_X_MSB_ADDR = 0X62,
   GYRO_OFFSET_Y_LSB_ADDR = 0X63,
   GYRO_OFFSET_Y_MSB_ADDR = 0X64,
   GYRO_OFFSET_Z_LSB_ADDR = 0X65,
   GYRO_OFFSET_Z_MSB_ADDR = 0X66,

   // Radius registers
   ACCEL_RADIUS_LSB_ADDR = 0X67,
   ACCEL_RADIUS_MSB_ADDR = 0X68,
   MAG_RADIUS_LSB_ADDR = 0X69,
   MAG_RADIUS_MSB_ADDR = 0X6A,

   // PAGE1 REGISTER DEFINITION START
   ACC_CONFIG = 0x08,
   INT_MSK = 0x0F,
   INT_EN = 0x10,
   ACC_AM_THRE = 0x11,
   ACC_INT_SET = 0x12,
   ACC_NM_THRE = 0x15,
   ACC_NM_SET = 0x16,

   //configs
   BNO055_MAG_CONFIG_ADDR = 0X09,
   BNO055_GYRO_CONFIG_ADDR =0X0A,
   BNO055_GYRO_MODE_CONFIG_ADDR = 0X0B,

} bno055_reg_t;

typedef enum
{
   GYRO_DATA,
   ACC_DATA,
   LACC_DATA,
   GACC_DATA,
   QUAT_DATA,
   STAT_DATA,
} bno055_data_type_t;

typedef enum
{
   OPERATION_MODE_CONFIG = 0X00,
   OPERATION_MODE_ACCONLY = 0X01,
   OPERATION_MODE_MAGONLY = 0X02,
   OPERATION_MODE_GYRONLY = 0X03,
   OPERATION_MODE_ACCMAG = 0X04,
   OPERATION_MODE_ACCGYRO = 0X05,
   OPERATION_MODE_MAGGYRO = 0X06,
   OPERATION_MODE_AMG = 0X07,
   OPERATION_MODE_IMUPLUS = 0X08,
   OPERATION_MODE_COMPASS = 0X09,
   OPERATION_MODE_M4G = 0X0A,
   OPERATION_MODE_NDOF_FMC_OFF = 0X0B,
   OPERATION_MODE_NDOF = 0X0C
} bno055_opmode_t;

typedef enum
{
   ACC_NM = 0b10000000,
   ACC_AM = 0b01000000,
   ACC_HIGH_G = 0b00100000,
   GYR_DRDY = 0b00010000,
   GYR_HIGH_RATE = 0b00001000,
   GYRO_AM = 0b00000100,
   MAG_DRDY = 0b00000010,
   ACC_BSX_DRDY = 0b00000001
} bno055_intmsk_t;

typedef enum
{
   POWER_MODE_NORMAL = 0X00,
   POWER_MODE_LOWPOWER = 0X01,
   POWER_MODE_SUSPEND = 0X02
} bno055_powermode_t;

typedef struct
{
   uint8_t sys;
   uint8_t gyro;
   uint8_t accel;
   uint8_t mag;
} bno55_calib_status_t;

typedef struct
{
   int16_t accel_offset_x;
   int16_t accel_offset_y;
   int16_t accel_offset_z;

   int16_t mag_offset_x;
   int16_t mag_offset_y;
   int16_t mag_offset_z;

   int16_t gyro_offset_x;
   int16_t gyro_offset_y;
   int16_t gyro_offset_z;

   int16_t accel_radius;

   int16_t mag_radius;
} bno055_calib_offsets_t;

typedef struct
{
   uint8_t x_remap_val;
   uint8_t y_remap_val;
   uint8_t z_remap_val;
   uint8_t x_remap_sign;
   uint8_t y_remap_sign;
   uint8_t z_remap_sign;
} bno055_axis_remap_t;

typedef struct
{
   int16_t w;
   int16_t x;
   int16_t y;
   int16_t z;
} bno055_quaternion_t;

typedef struct
{
   double yaw;
   double pitch;
   double roll;
} bno055_euler_t;

typedef struct
{
   int16_t x;
   int16_t y;
   int16_t z;
} bno055_acc_t;

typedef struct
{
   int16_t x;
   int16_t y;
   int16_t z;
} bno055_gyro_t;


// Public API Functions ------------------------------------------------------------------------------------------------

void imu_init(void);
void imu_deinit(void);
void imu_register_motion_change_callback(motion_change_callback_t callback);
void imu_register_data_ready_callback(data_ready_callback_t callback);
void imu_set_power_mode(bno055_powermode_t power_mode);
void imu_set_fusion_mode(bno055_opmode_t fusion_mode);
void imu_read_accel_data(bno055_acc_t *acc);
void imu_read_linear_accel_data(bno055_acc_t *acc);
void imu_read_gravity_accel_data(bno055_acc_t *acc);
void imu_read_quaternion_data(bno055_quaternion_t *quaternion);
void imu_read_gyro_data(bno055_gyro_t *gyro);
void imu_read_temp(int8_t *temp);
void imu_read_fw_version(uint8_t *msb, uint8_t *lsb);
void imu_read_calibration_status(bno55_calib_status_t *status);
void imu_read_calibration_offsets(bno055_calib_offsets_t *offsets);
void imu_read_axis_remap(bno055_axis_remap_t *remap);
bool imu_set_axis_remap(bno055_axis_remap_t remap);
void imu_read_euler_data(bno055_euler_t *euler);
bool imu_read_in_motion(void);
void imu_clear_interrupts(void);
uint8_t imu_pick_data_from_raw(const uint8_t **const picked, const uint8_t *raw_data, bno055_data_type_t data_type);
uint8_t imu_copy_data_from_raw(uint8_t *picked, const uint8_t *raw_data, bno055_data_type_t data_type);

// Math utilities
void quaternion_to_euler(bno055_quaternion_t quaternion, bno055_euler_t *euler);

#endif  // #ifndef __IMU_HEADER_H__
