// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "imu.h"
#include "math.h"
#include "system.h"
#include "logging.h"


// IMU Definitions -----------------------------------------------------------------------------------------------------

// ISR Definitions
#define imu_iom_isr       am_iom_isr1(IMU_I2C_NUMBER)
#define am_iom_isr1(n)    am_iom_isr(n)
#define am_iom_isr(n)     am_iomaster ## n ## _isr


// Static Global Variables ---------------------------------------------------------------------------------------------

static void *i2c_handle;
static volatile bool previously_in_motion;
static uint32_t imu_buffer[BURST_READ_LEN];
static motion_change_callback_t motion_change_callback;
static data_ready_callback_t data_ready_callback;


// Private Helper Functions --------------------------------------------------------------------------------------------

static void i2c_write8(uint8_t reg_number, uint8_t reg_value)
{
   // Repeat the transfer until it succeeds or requires a device reset
   uint32_t body_buffer = reg_value, retries_remaining = 5;
   am_hal_iom_transfer_t write_transaction = {
      .uPeerInfo.ui32I2CDevAddr     = IMU_I2C_ADDRESS,
      .ui32InstrLen                 = 1,
      .ui64Instr                    = reg_number,
      .eDirection                   = AM_HAL_IOM_TX,
      .ui32NumBytes                 = 1,
      .pui32TxBuffer                = &body_buffer,
      .pui32RxBuffer                = NULL,
      .bContinue                    = false,
      .ui8RepeatCount               = 0,
      .ui8Priority                  = 1,
      .ui32PauseCondition           = 0,
      .ui32StatusSetClr             = 0
   };
   while (retries_remaining-- && (am_hal_iom_blocking_transfer(i2c_handle, &write_transaction) != AM_HAL_STATUS_SUCCESS));
}

static void i2c_read_complete(void *pCallbackCtxt, uint32_t transactionStatus)
{
   // Read the device motion status and trigger the registered callback
   uint8_t *raw_data = (uint8_t*)imu_buffer;
   const uint8_t interrupt_status = *(raw_data + BNO055_INTR_STAT_ADDR - BURST_READ_BASE_ADDR);
   const bool in_motion_fired = interrupt_status & ACC_AM, no_motion_fired = interrupt_status & ACC_NM;
   if ((in_motion_fired && !previously_in_motion) || (no_motion_fired && previously_in_motion))
   {
      previously_in_motion = !previously_in_motion;
      if (motion_change_callback)
         motion_change_callback(previously_in_motion);
   }

   // Read the data-ready status and trigger the registered callback
   if (data_ready_callback)// TODO: Only call if interrupt-bit set: && (interrupt_status & (ACC_BSX_DRDY | MAG_DRDY | GYR_DRDY)))
   {
#ifdef _TEST_IMU_DATA
      data_ready_callback(raw_data, BURST_READ_LEN);
#else
      uint8_t* calib_data = raw_data + BNO055_CALIB_STAT_ADDR - BURST_READ_BASE_ADDR;
      int16_t* linear_accel_data = (int16_t*)(raw_data + BNO055_LINEAR_ACCEL_DATA_X_LSB_ADDR - BURST_READ_BASE_ADDR);
      data_ready_callback(calib_data, linear_accel_data);
#endif
   }

   // Reset the interrupt trigger bits
   i2c_write8(BNO055_SYS_TRIGGER_ADDR, 0xC0);
}

static uint8_t i2c_read8(uint8_t reg_number)
{
   // Repeat the transfer until it succeeds or requires a device reset
   static uint32_t read_buffer;
   uint32_t retries_remaining = 5;
   am_hal_iom_transfer_t read_transaction = {
      .uPeerInfo.ui32I2CDevAddr     = IMU_I2C_ADDRESS,
      .ui32InstrLen                 = 1,
      .ui64Instr                    = reg_number,
      .eDirection                   = AM_HAL_IOM_RX,
      .ui32NumBytes                 = 1,
      .pui32TxBuffer                = NULL,
      .pui32RxBuffer                = &read_buffer,
      .bContinue                    = false,
      .ui8RepeatCount               = 0,
      .ui8Priority                  = 1,
      .ui32PauseCondition           = 0,
      .ui32StatusSetClr             = 0
   };
   while (retries_remaining-- && (am_hal_iom_blocking_transfer(i2c_handle, &read_transaction) != AM_HAL_STATUS_SUCCESS));
   return ((uint8_t*)&read_buffer)[0];
}

static void i2c_read(uint8_t reg_number, uint8_t *read_buffer, uint32_t buffer_length, bool non_blocking)
{
   // Repeat the transfer until it succeeds or requires a device reset
   am_hal_iom_transfer_t read_transaction = {
      .uPeerInfo.ui32I2CDevAddr     = IMU_I2C_ADDRESS,
      .ui32InstrLen                 = 1,
      .ui64Instr                    = reg_number,
      .eDirection                   = AM_HAL_IOM_RX,
      .ui32NumBytes                 = buffer_length,
      .pui32TxBuffer                = NULL,
      .pui32RxBuffer                = (uint32_t*)read_buffer,
      .bContinue                    = false,
      .ui8RepeatCount               = 0,
      .ui8Priority                  = 1,
      .ui32PauseCondition           = 0,
      .ui32StatusSetClr             = 0
   };
   if (non_blocking)
      am_hal_iom_nonblocking_transfer(i2c_handle, &read_transaction, i2c_read_complete, NULL);
   else
   {
      uint32_t retries_remaining = 5;
      while (retries_remaining-- && (am_hal_iom_blocking_transfer(i2c_handle, &read_transaction) != AM_HAL_STATUS_SUCCESS));
   }
}


// Interrupt Service Routines ------------------------------------------------------------------------------------------

static void imu_isr(void *args)
{
   // Initiate an IMU burst read
   i2c_read(BURST_READ_BASE_ADDR, (uint8_t*)imu_buffer, BURST_READ_LEN, true);
}

void imu_iom_isr(void)
{
   // Handle an IMU read interrupt
   static uint32_t status;
   AM_CRITICAL_BEGIN
   am_hal_iom_interrupt_status_get(i2c_handle, false, &status);
   am_hal_iom_interrupt_clear(i2c_handle, status);
   AM_CRITICAL_END
   am_hal_iom_interrupt_service(i2c_handle, status);
}


// IMU Chip-Specific API Functions -------------------------------------------------------------------------------------

static void imu_set_op_mode(bno055_opmode_t op_mode)
{
   // Set the indicated mode and delay to allow it to take effect
   i2c_write8(BNO055_OPR_MODE_ADDR, op_mode);
   am_util_delay_ms(30);
}

static bno055_opmode_t imu_get_op_mode(void)
{
   return (bno055_opmode_t)i2c_read8(BNO055_OPR_MODE_ADDR);
}

static void set_use_external_crystal(void)
{
   // Set the IMU to use an external crystal clock source
   i2c_write8(BNO055_SYS_TRIGGER_ADDR, 0xC0);
   am_util_delay_ms(1000);
}

static void enter_suspend_mode(void)
{
   // Switch to configuration mode and suspend
   imu_set_op_mode(OPERATION_MODE_CONFIG);
   i2c_write8(BNO055_PWR_MODE_ADDR, 0x02);
}

static void enable_motion_interrupts(void)
{
   // Set up interrupts for motion and non-motion events
   i2c_write8(BNO055_PAGE_ID_ADDR, 1);
   i2c_write8(ACC_CONFIG, 0b00000000); //operation mode/bandwidth/g range
   i2c_write8(ACC_AM_THRE, 0b00001010);
   i2c_write8(ACC_NM_THRE, 0b00001010);
   i2c_write8(ACC_NM_SET, 0b00001001);
   i2c_write8(ACC_INT_SET, 0b00011111); //axis and duration for triggering motion interrupt
   i2c_write8(INT_MSK, ACC_NM | ACC_AM);
   i2c_write8(INT_EN, ACC_NM | ACC_AM);
   i2c_write8(BNO055_PAGE_ID_ADDR, 0);
}

static void disable_motion_interrupts(void)
{
   // Disable interrupts for motion and non-motion events
   i2c_write8(BNO055_PAGE_ID_ADDR, 1);
   i2c_write8(ACC_INT_SET, 0);
   uint8_t int_msk = i2c_read8(INT_MSK) & ~(ACC_NM | ACC_AM);
   uint8_t int_en = i2c_read8(INT_EN) & ~(ACC_NM | ACC_AM);
   i2c_write8(INT_MSK, int_msk);
   i2c_write8(INT_EN, int_en);
   i2c_write8(BNO055_PAGE_ID_ADDR, 0);
}

static void enable_data_ready_interrupts(void)
{
   i2c_write8(BNO055_PAGE_ID_ADDR, 1);
   uint8_t int_msk = i2c_read8(INT_MSK) | (ACC_BSX_DRDY | MAG_DRDY | GYR_DRDY);
   uint8_t int_en = i2c_read8(INT_EN) | (ACC_BSX_DRDY | MAG_DRDY | GYR_DRDY);
   i2c_write8(INT_MSK, int_msk);
   i2c_write8(INT_EN, int_en);
   i2c_write8(BNO055_PAGE_ID_ADDR, 0);
}

static void disable_data_ready_interrupts(void)
{
   i2c_write8(BNO055_PAGE_ID_ADDR, 1);
   uint8_t int_msk = i2c_read8(INT_MSK) & ~(ACC_BSX_DRDY | MAG_DRDY | GYR_DRDY);
   uint8_t int_en = i2c_read8(INT_EN) & ~(ACC_BSX_DRDY | MAG_DRDY | GYR_DRDY);
   i2c_write8(INT_MSK, int_msk);
   i2c_write8(INT_EN, int_en);
   i2c_write8(BNO055_PAGE_ID_ADDR, 0);
}


// Math Helper Functions -----------------------------------------------------------------------------------------------

void quaternion_to_euler(bno055_quaternion_t quaternion, bno055_euler_t *euler)
{
   int32_t sqw = quaternion.w * quaternion.w;
   int32_t sqx = quaternion.x * quaternion.x;
   int32_t sqy = quaternion.y * quaternion.y;
   int32_t sqz = quaternion.z * quaternion.z;

   euler->yaw = atan2(2.0*(quaternion.x*quaternion.y + quaternion.z*quaternion.w),(sqx - sqy - sqz + sqw));
   euler->pitch = asin((double)(-2*(quaternion.x*quaternion.z - quaternion.y*quaternion.w))/(double)(sqx + sqy + sqz + sqw));
   euler->roll = atan2(2.0*(quaternion.y*quaternion.z + quaternion.x*quaternion.w),(-sqx - sqy + sqz + sqw));
}


// Public API Functions ------------------------------------------------------------------------------------------------

void imu_init(void)
{
   // Initialize static variables
   previously_in_motion = false;
   motion_change_callback = NULL;
   data_ready_callback = NULL;

   // Create an I2C configuration structure
   static uint32_t command_queue_buffer[128];
   const am_hal_iom_config_t i2c_config =
   {
      .eInterfaceMode = AM_HAL_IOM_I2C_MODE,
      .ui32ClockFreq = AM_HAL_IOM_100KHZ,
      .eSpiMode = 0,
      .pNBTxnBuf = command_queue_buffer,
      .ui32NBTxnBufLength = sizeof(command_queue_buffer) / sizeof(uint32_t)
   };

   // Configure and assert the RESET pin
   configASSERT0(am_hal_gpio_pinconfig(PIN_IMU_RESET, am_hal_gpio_pincfg_output));
   am_hal_gpio_output_set(PIN_IMU_RESET);

   // Initialize the I2C module and enable all relevant I2C pins
   am_hal_gpio_pincfg_t scl_config = g_AM_BSP_GPIO_IOM0_SCL;
   am_hal_gpio_pincfg_t sda_config = g_AM_BSP_GPIO_IOM0_SDA;
   scl_config.GP.cfg_b.uFuncSel = PIN_IMU_I2C_SCL_FUNCTION;
   sda_config.GP.cfg_b.uFuncSel = PIN_IMU_I2C_SDA_FUNCTION;
   scl_config.GP.cfg_b.ePullup =  AM_HAL_GPIO_PIN_PULLUP_6K;
   configASSERT0(am_hal_iom_initialize(IMU_I2C_NUMBER, &i2c_handle));
   configASSERT0(am_hal_gpio_pinconfig(PIN_IMU_I2C_SCL, scl_config));
   configASSERT0(am_hal_gpio_pinconfig(PIN_IMU_I2C_SDA, sda_config));
   am_hal_iom_power_ctrl(i2c_handle, AM_HAL_SYSCTRL_WAKE, false);
   am_hal_iom_configure(i2c_handle, &i2c_config);
   am_hal_iom_enable(i2c_handle);

   // Ensure that the device is accessible and enter configuration mode
   while (i2c_read8(BNO055_CHIP_ID_ADDR) != BNO055_ID)
      am_util_delay_ms(100);
   i2c_write8(BNO055_PAGE_ID_ADDR, 0);
   imu_set_op_mode(OPERATION_MODE_CONFIG);

   // Set up an external crystal and the sensor output units
   set_use_external_crystal();
   i2c_write8(BNO055_UNIT_SEL_ADDR, 0);

   // Set device to use the low-power mode
   i2c_write8(BNO055_PWR_MODE_ADDR, POWER_MODE_LOWPOWER);
   am_util_delay_ms(30);

   // Set up incoming interrupts from the IMU
   disable_motion_interrupts();
   disable_data_ready_interrupts();
   uint32_t imu_interrupt_pin = PIN_IMU_INTERRUPT;
   configASSERT0(am_hal_gpio_pinconfig(PIN_IMU_INTERRUPT, am_hal_gpio_pincfg_input));
   configASSERT0(am_hal_gpio_interrupt_control(AM_HAL_GPIO_INT_CHANNEL_0, AM_HAL_GPIO_INT_CTRL_INDV_ENABLE, &imu_interrupt_pin));
   configASSERT0(am_hal_gpio_interrupt_register(AM_HAL_GPIO_INT_CHANNEL_0, PIN_IMU_INTERRUPT, imu_isr, NULL));
   NVIC_SetPriority(GPIO0_001F_IRQn + GPIO_NUM2IDX(PIN_IMU_INTERRUPT), NVIC_configKERNEL_INTERRUPT_PRIORITY - 1);
   NVIC_SetPriority(IOMSTR0_IRQn + IMU_I2C_NUMBER, NVIC_configKERNEL_INTERRUPT_PRIORITY - 2);
   NVIC_EnableIRQ(GPIO0_001F_IRQn + GPIO_NUM2IDX(PIN_IMU_INTERRUPT));
   NVIC_EnableIRQ(IOMSTR0_IRQn + IMU_I2C_NUMBER);
}

void imu_deinit(void)
{
   // Disable interrupts and put the device into suspend mode
   disable_motion_interrupts();
   disable_data_ready_interrupts();
   enter_suspend_mode();

   // Disable all IMU-based interrupts
   uint32_t imu_interrupt_pin = PIN_IMU_INTERRUPT;
   NVIC_DisableIRQ(GPIO0_001F_IRQn + GPIO_NUM2IDX(PIN_IMU_INTERRUPT));
   am_hal_gpio_interrupt_register(AM_HAL_GPIO_INT_CHANNEL_0, imu_interrupt_pin, NULL, NULL);
   am_hal_gpio_interrupt_control(AM_HAL_GPIO_INT_CHANNEL_0, AM_HAL_GPIO_INT_CTRL_INDV_DISABLE, &imu_interrupt_pin);

   // Disable all I2C communications
   while (am_hal_iom_disable(i2c_handle) != AM_HAL_STATUS_SUCCESS);
   am_hal_iom_uninitialize(i2c_handle);
}

void imu_register_motion_change_callback(motion_change_callback_t callback)
{
   // Set up IMU motion-based interrupts
   motion_change_callback = callback;
   enable_motion_interrupts();
}

void imu_register_data_ready_callback(data_ready_callback_t callback)
{
   // Set up IMU data-ready interrupts
   data_ready_callback = callback;
   enable_data_ready_interrupts();
}

void imu_set_fusion_mode(bno055_opmode_t fusion_mode)
{
   imu_set_op_mode(OPERATION_MODE_CONFIG);
   imu_set_op_mode(fusion_mode);
}

void imu_set_power_mode(bno055_powermode_t power_mode)
{
   bno055_opmode_t saved_mode = imu_get_op_mode();
   imu_set_op_mode(OPERATION_MODE_CONFIG);
   i2c_write8(BNO055_PWR_MODE_ADDR, power_mode);
   imu_set_op_mode(saved_mode);
}

void imu_read_accel_data(bno055_acc_t *acc)
{
   static int16_t accel_data[3];
   i2c_read(BNO055_ACCEL_DATA_X_LSB_ADDR, (uint8_t*)accel_data, sizeof(accel_data), false);
   acc->x = accel_data[0];
   acc->y = accel_data[1];
   acc->z = accel_data[2];
}

void imu_read_linear_accel_data(bno055_acc_t *acc)
{
   static int16_t accel_data[3];
   i2c_read(BNO055_LINEAR_ACCEL_DATA_X_LSB_ADDR, (uint8_t*)accel_data, sizeof(accel_data), false);
   acc->x = accel_data[0];
   acc->y = accel_data[1];
   acc->z = accel_data[2];
}

void imu_read_gravity_accel_data(bno055_acc_t *acc)
{
   static int16_t accel_data[3];
   i2c_read(BNO055_GRAVITY_DATA_X_LSB_ADDR, (uint8_t*)accel_data, sizeof(accel_data), false);
   acc->x = accel_data[0];
   acc->y = accel_data[1];
   acc->z = accel_data[2];
}

void imu_read_euler_data(bno055_euler_t *euler)
{
   static int16_t euler_data[3];
   i2c_read(BNO055_EULER_H_LSB_ADDR, (uint8_t*)euler_data, sizeof(euler_data), false);
   euler->yaw = (euler_data[0]/16.0);
   euler->roll = (euler_data[1]/16.0);
   euler->pitch = (euler_data[2]/16.0);
}

void imu_read_quaternion_data(bno055_quaternion_t *quaternion)
{
   static int16_t quaternion_data[4];
   i2c_read(BNO055_QUATERNION_DATA_W_LSB_ADDR, (uint8_t*)quaternion_data, sizeof(quaternion_data), false);
   //temporary fix of MSB sign bit flipping problem
   for (uint8_t i = 0; i < 4; i++)
   {
      if (quaternion_data[i] > 16384)
         quaternion_data[i] -= 32768;
      else if (quaternion_data[i] < -16384)
         quaternion_data[i] += 32768;
   }
   quaternion->w = quaternion_data[0];
   quaternion->x = quaternion_data[1];
   quaternion->y = quaternion_data[2];
   quaternion->z = quaternion_data[3];
}

void imu_read_gyro_data(bno055_gyro_t *gyro)
{
   static int16_t gyro_data[3];
   i2c_read(BNO055_GYRO_DATA_X_LSB_ADDR, (uint8_t*)gyro_data, sizeof(gyro_data), false);
   gyro->x = gyro_data[0];
   gyro->y = gyro_data[1];
   gyro->z = gyro_data[2];
}

void imu_read_temp(int8_t *temp)
{
   static int8_t temp_data;
   i2c_read(BNO055_TEMP_ADDR, (uint8_t*)&temp_data, 1, false);
   *temp = (int8_t)temp_data;
}

void imu_read_fw_version(uint8_t *msb, uint8_t *lsb)
{
   *msb = i2c_read8(BNO055_SW_REV_ID_MSB_ADDR);
   *lsb = i2c_read8(BNO055_SW_REV_ID_LSB_ADDR);
}

void imu_read_calibration_status(bno55_calib_status_t *status)
{
   uint8_t reg_value = i2c_read8(BNO055_CALIB_STAT_ADDR);

   status->mag = reg_value & 0x03;
   status->accel = (reg_value >> 2) & 0x03;
   status->gyro = (reg_value >> 4) & 0x03;
   status->sys = (reg_value >> 6) & 0x03;
}

void imu_read_calibration_offsets(bno055_calib_offsets_t *offsets)
{
   static int16_t calib_data[11];
   bno055_opmode_t saved_mode = imu_get_op_mode();
   //calibration values are only availble in config mode
   imu_set_op_mode(OPERATION_MODE_CONFIG);
   //read the 11 offset values
   i2c_read(ACCEL_OFFSET_X_LSB_ADDR, (uint8_t*)calib_data, sizeof(calib_data), false);
   //revert to the previous mode
   imu_set_op_mode(saved_mode);

   offsets->accel_offset_x = calib_data[0];
   offsets->accel_offset_y = calib_data[1];
   offsets->accel_offset_z = calib_data[2];

   offsets->mag_offset_x = calib_data[3];
   offsets->mag_offset_y = calib_data[4];
   offsets->mag_offset_z = calib_data[5];

   offsets->gyro_offset_x = calib_data[6];
   offsets->gyro_offset_y = calib_data[7];
   offsets->gyro_offset_z = calib_data[8];

   offsets->accel_radius = calib_data[9];
   offsets->mag_radius = calib_data[10];
}

void imu_read_axis_remap(bno055_axis_remap_t *remap)
{
   uint8_t reg_axis_map_config = i2c_read8(BNO055_AXIS_MAP_CONFIG_ADDR);
   remap->x_remap_val = reg_axis_map_config & 0x03;
   remap->y_remap_val = (reg_axis_map_config >> 2) & 0x03;
   remap->z_remap_val = (reg_axis_map_config >> 4) & 0x03;

   uint8_t reg_axis_map_sign = i2c_read8(BNO055_AXIS_MAP_SIGN_ADDR);
   remap->x_remap_sign = reg_axis_map_sign & 0x04;
   remap->y_remap_sign = reg_axis_map_sign & 0x02;
   remap->z_remap_sign = reg_axis_map_sign & 0x01;
}

bool imu_set_axis_remap(bno055_axis_remap_t remap)
{
   uint8_t reg_axis_map_config = remap.x_remap_val | (remap.y_remap_val << 2) | (remap.z_remap_val << 4);
   uint8_t reg_axis_map_sign = (remap.x_remap_sign << 2) | (remap.y_remap_sign << 1) | (remap.z_remap_sign);

   bno055_opmode_t saved_mode = imu_get_op_mode();
   imu_set_op_mode(OPERATION_MODE_CONFIG);
   i2c_write8(BNO055_AXIS_MAP_CONFIG_ADDR, reg_axis_map_config);
   i2c_write8(BNO055_AXIS_MAP_SIGN_ADDR, reg_axis_map_sign);
   imu_set_op_mode(saved_mode);

   //test whether the set is successful
   bno055_axis_remap_t remap_verification = {0};
   imu_read_axis_remap(&remap_verification);
   return ((remap_verification.x_remap_val == remap.x_remap_val) && (remap_verification.y_remap_val == remap.y_remap_val) && (remap_verification.z_remap_val == remap.z_remap_val) &&
           (remap_verification.x_remap_sign == remap.x_remap_sign) && (remap_verification.y_remap_sign == remap.y_remap_sign) && (remap_verification.z_remap_sign == remap.z_remap_sign));
}

bool imu_read_in_motion(void)
{
   return previously_in_motion;
}

uint8_t imu_pick_data_from_raw(const uint8_t **const picked, const uint8_t *raw_data, bno055_data_type_t data_type)
{
   switch (data_type) {
      case GYRO_DATA:
         *picked = raw_data + BNO055_GYRO_DATA_X_LSB_ADDR - BURST_READ_BASE_ADDR;
         return GYRO_DATA_LEN;
      case LACC_DATA:
         *picked = raw_data + BNO055_LINEAR_ACCEL_DATA_X_LSB_ADDR - BURST_READ_BASE_ADDR;
         return LACC_DATA_LEN;
      case GACC_DATA:
         *picked = raw_data + BNO055_GRAVITY_DATA_X_LSB_ADDR - BURST_READ_BASE_ADDR;
         return GACC_DATA_LEN;
      case QUAT_DATA:
         *picked = raw_data + BNO055_QUATERNION_DATA_W_LSB_ADDR - BURST_READ_BASE_ADDR;
         return QUAT_DATA_LEN;
      case STAT_DATA:
         *picked = raw_data + BNO055_CALIB_STAT_ADDR - BURST_READ_BASE_ADDR;
         return STAT_DATA_LEN;
      default:
         break;
   }
   return 0;
}

uint8_t imu_copy_data_from_raw(uint8_t *picked, const uint8_t *raw_data, bno055_data_type_t data_type)
{
   const uint8_t *data_ptr;
   uint8_t data_len = imu_pick_data_from_raw(&data_ptr, raw_data, data_type);
   memcpy(picked, data_ptr, data_len);
   return data_len;
}
