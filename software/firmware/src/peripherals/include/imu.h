#ifndef __IMU_HEADER_H__
#define __IMU_HEADER_H__

// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "app_config.h"


// Peripheral Type Definitions -----------------------------------------------------------------------------------------

typedef void (*motion_change_callback_t)(bool in_motion);


// Public API Functions ------------------------------------------------------------------------------------------------

void imu_init(void);
void imu_deinit(void);
void imu_register_motion_change_callback(motion_change_callback_t callback);
void imu_read_accel_data(int16_t *x, int16_t *y, int16_t *z);

#endif  // #ifndef __IMU_HEADER_H__
