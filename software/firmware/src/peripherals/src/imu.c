// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "imu.h"


// Static Global Variables ---------------------------------------------------------------------------------------------

static void *i2c_handle;
static bool previously_in_motion;
static motion_change_callback_t motion_change_callback;


// Chip-Specific Register Definitions ----------------------------------------------------------------------------------

#define BNO055_ID 0xA0

typedef enum {
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
   ACC_NM_SET = 0x16
} bno055_reg_t;

typedef enum {
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

typedef enum {
   POWER_MODE_NORMAL = 0X00,
   POWER_MODE_LOWPOWER = 0X01,
   POWER_MODE_SUSPEND = 0X02
} bno055_powermode_t;


// Private Helper Functions --------------------------------------------------------------------------------------------

static void i2c_write8(uint8_t reg_number, uint8_t reg_value)
{
   // Repeat the transfer until it succeeds
   uint32_t bodyBuffer = reg_value;
   am_hal_iom_transfer_t write_transaction = {
      .uPeerInfo.ui32I2CDevAddr     = IMU_I2C_ADDRESS,
      .ui32InstrLen                 = 1,
      .ui64Instr                    = reg_number,
      .eDirection                   = AM_HAL_IOM_TX,
      .ui32NumBytes                 = 1,
      .pui32TxBuffer                = &bodyBuffer,
      .pui32RxBuffer                = NULL,
      .bContinue                    = false,
      .ui8RepeatCount               = 0,
      .ui8Priority                  = 1,
      .ui32PauseCondition           = 0,
      .ui32StatusSetClr             = 0
   };
   while (am_hal_iom_blocking_transfer(i2c_handle, &write_transaction) != AM_HAL_STATUS_SUCCESS);
}

static uint8_t i2c_read8(uint8_t reg_number)
{
   // Repeat the transfer until it succeeds
   static uint32_t readBuffer;
   am_hal_iom_transfer_t read_transaction = {
      .uPeerInfo.ui32I2CDevAddr     = IMU_I2C_ADDRESS,
      .ui32InstrLen                 = 1,
      .ui64Instr                    = reg_number,
      .eDirection                   = AM_HAL_IOM_RX,
      .ui32NumBytes                 = 1,
      .pui32TxBuffer                = NULL,
      .pui32RxBuffer                = &readBuffer,
      .bContinue                    = false,
      .ui8RepeatCount               = 0,
      .ui8Priority                  = 1,
      .ui32PauseCondition           = 0,
      .ui32StatusSetClr             = 0
   };
   while (am_hal_iom_blocking_transfer(i2c_handle, &read_transaction) != AM_HAL_STATUS_SUCCESS);
   return ((uint8_t*)&readBuffer)[0];
}

static void i2c_read(uint8_t reg_number, uint8_t *read_buffer, uint32_t buffer_length)
{
   // Repeat the transfer until it succeeds
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
   while (am_hal_iom_blocking_transfer(i2c_handle, &read_transaction) != AM_HAL_STATUS_SUCCESS);
}

static void imu_isr(void *args)
{
   // Read the device motion status and trigger the registered callback
   bool in_motion = i2c_read8(BNO055_INTR_STAT_ADDR) & 0x40;
   i2c_write8(BNO055_SYS_TRIGGER_ADDR, 0xC0);
   if (in_motion != previously_in_motion)
      motion_change_callback(in_motion);
   previously_in_motion = in_motion;
}


// IMU Chip-Specific API Functions -------------------------------------------------------------------------------------

static void set_mode(bno055_opmode_t mode)
{
   // Set the indicated mode and delay to allow it to take effect
   i2c_write8(BNO055_OPR_MODE_ADDR, mode);
   am_util_delay_ms(30);
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
  set_mode(OPERATION_MODE_CONFIG);
  i2c_write8(BNO055_PWR_MODE_ADDR, 0x02);
}

static void disable_motion_interrupts(void)
{
   // Disable all interrupts
   i2c_write8(BNO055_PAGE_ID_ADDR, 1);
   i2c_write8(ACC_INT_SET, 0);
   i2c_write8(INT_MSK, 0);
   i2c_write8(INT_EN, 0);
   i2c_write8(BNO055_PAGE_ID_ADDR, 0);
}

static void enable_motion_interrupts(void)
{
   // Set up interrupts for motion and non-motion events
   i2c_write8(BNO055_PAGE_ID_ADDR, 1);
   i2c_write8(ACC_CONFIG, 0b00000000);
   i2c_write8(ACC_AM_THRE, 0b00001010);
   i2c_write8(ACC_NM_THRE, 0b00001010);
   i2c_write8(ACC_NM_SET, 0b00001001);
   i2c_write8(ACC_INT_SET, 0b00011111);
   i2c_write8(INT_MSK, 0xC0);
   i2c_write8(INT_EN, 0xC0);
   i2c_write8(BNO055_PAGE_ID_ADDR, 0);
}


// Public API Functions ------------------------------------------------------------------------------------------------

void imu_init(void)
{
   // Initialize static variables
   previously_in_motion = false;
   motion_change_callback = NULL;

   // Create an I2C configuration structure
   const am_hal_iom_config_t i2c_config =
   {
      .eInterfaceMode = AM_HAL_IOM_I2C_MODE,
      .ui32ClockFreq = AM_HAL_IOM_400KHZ,
      .eSpiMode = 0,
      .pNBTxnBuf = NULL,
      .ui32NBTxnBufLength = 0
   };

   // Configure and assert the RESET pin
   configASSERT0(am_hal_gpio_pinconfig(PIN_IMU_RESET, am_hal_gpio_pincfg_output));
   am_hal_gpio_output_set(PIN_IMU_RESET);

   // Initialize the I2C module and enable all relevant I2C pins
   am_hal_gpio_pincfg_t scl_config = g_AM_BSP_GPIO_IOM0_SCL;
   am_hal_gpio_pincfg_t sda_config = g_AM_BSP_GPIO_IOM0_SDA;
   scl_config.GP.cfg_b.uFuncSel = PIN_IMU_I2C_SCL_FUNCTION;
   sda_config.GP.cfg_b.uFuncSel = PIN_IMU_I2C_SDA_FUNCTION;
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
   set_mode(OPERATION_MODE_CONFIG);

   // Set up an external crystal and the sensor output units
   set_use_external_crystal();
   i2c_write8(BNO055_UNIT_SEL_ADDR, 0);

   // Set device to use the low-power mode
   i2c_write8(BNO055_PWR_MODE_ADDR, POWER_MODE_LOWPOWER);
   am_util_delay_ms(30);

   // Set up incoming interrupts from the IMU
   disable_motion_interrupts();
   uint32_t imu_interrupt_pin = PIN_IMU_INTERRUPT;
   configASSERT0(am_hal_gpio_pinconfig(PIN_IMU_INTERRUPT, am_hal_gpio_pincfg_input));
   configASSERT0(am_hal_gpio_interrupt_control(AM_HAL_GPIO_INT_CHANNEL_0, AM_HAL_GPIO_INT_CTRL_INDV_ENABLE, &imu_interrupt_pin));
   configASSERT0(am_hal_gpio_interrupt_register(AM_HAL_GPIO_INT_CHANNEL_0, PIN_IMU_INTERRUPT, imu_isr, NULL));
   NVIC_SetPriority(GPIO0_001F_IRQn + GPIO_NUM2IDX(PIN_IMU_INTERRUPT), NVIC_configKERNEL_INTERRUPT_PRIORITY - 1);
   NVIC_EnableIRQ(GPIO0_001F_IRQn + GPIO_NUM2IDX(PIN_IMU_INTERRUPT));
}

void imu_deinit(void)
{
   // Disable interrupts and put the device into suspend mode
   disable_motion_interrupts();
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
   set_mode(OPERATION_MODE_CONFIG);
   enable_motion_interrupts();
   set_mode(OPERATION_MODE_ACCONLY);
}

void imu_read_accel_data(int16_t *x, int16_t *y, int16_t *z)
{
   static int16_t accel_data[3];
   i2c_read(BNO055_ACCEL_DATA_X_LSB_ADDR, (uint8_t*)accel_data, sizeof(accel_data));
   *x = (int16_t)(accel_data[0] << 2) / 4;
   *y = (int16_t)(accel_data[1] << 2) / 4;
   *z = (int16_t)(accel_data[2] << 2) / 4;
}
