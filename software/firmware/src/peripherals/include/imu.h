#ifndef __IMU_HEADER_H__
#define __IMU_HEADER_H__

// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "app_config.h"


// Peripheral Type Definitions -----------------------------------------------------------------------------------------

typedef enum {
   IMU_UNKNOWN = 0x0000,
   IMU_ROTATION_VECTOR = 0x0001,
   IMU_GAME_ROTATION_VECTOR = 0x0002,
   IMU_ACCELEROMETER = 0x0004,
   IMU_LINEAR_ACCELEROMETER = 0x0008,
   IMU_GYROSCOPE = 0x0010,
   IMU_MAGNETOMETER = 0x0020,
   IMU_STEP_COUNTER = 0x0040,
   IMU_MOTION_DETECT = 0x0080,
   IMU_GRAVITY = 0x0100,
} imu_data_type_t;

typedef enum {
   IMU_CALIB_ACCELEROMETER = 0x01,
   IMU_CALIB_LINEAR_ACCELEROMETER = 0x02,
   IMU_CALIB_GYROSCOPE = 0x04,
   IMU_CALIB_MAGNETOMETER = 0x08,
   IMU_CALIB_ALL = 0x10,
   IMU_CALIB_STOP = 0x20,
} imu_calibration_data_t;


// IMU Data Callback Type Definitions ----------------------------------------------------------------------------------

typedef void (*motion_change_callback_t)(bool in_motion);
typedef void (*data_ready_callback_t)(imu_data_type_t data_types_ready);


// Public API Functions ------------------------------------------------------------------------------------------------

void imu_init(void);
void imu_deinit(void);
bool imu_calibrate_sensors(imu_calibration_data_t calibration_type);
bool imu_store_current_calibration(void);
void imu_enable_data_outputs(imu_data_type_t data_types, uint32_t report_interval_us);
void imu_register_motion_change_callback(motion_change_callback_t callback);
void imu_register_data_ready_callback(data_ready_callback_t callback);
void imu_read_accel_data(int16_t *x, int16_t *y, int16_t *z, uint8_t *accuracy);
void imu_read_linear_accel_data(int16_t *x, int16_t *y, int16_t *z, uint8_t *accuracy);
void imu_read_gravity_data(int16_t *x, int16_t *y, int16_t *z, uint8_t *accuracy);
void imu_read_quaternion_data(int16_t *w, int16_t *x, int16_t *y, int16_t *z, int16_t *radian_accuracy, uint8_t *accuracy);
void imu_read_gyro_data(int16_t *x, int16_t *y, int16_t *z, uint8_t *accuracy);
void imu_read_magnetometer_data(int16_t *x, int16_t *y, int16_t *z, uint8_t *accuracy);
uint16_t imu_read_step_count();
bool imu_read_in_motion(void);
void imu_convert_q_format_to_float(imu_data_type_t data_type, int16_t q_x, int16_t q_y, int16_t q_z, int16_t q_w_optional, int16_t q_accuracy_optional, float *x, float *y, float *z, float *w, float *accuracy);
imu_data_type_t imu_data_outputs_enabled(void);

#if REVISION_ID < REVISION_N
void imu_clear_interrupts(void);
#endif

#endif  // #ifndef __IMU_HEADER_H__
