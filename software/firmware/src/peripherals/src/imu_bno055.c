// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "imu.h"
#include "math.h"
#include "system.h"
#include "logging.h"


// IMU Definitions -----------------------------------------------------------------------------------------------------

// ISR Definitions
#define imu_iom_isr         am_iom_isr1(IMU_I2C_NUMBER)
#define am_iom_isr1(n)      am_iom_isr(n)
#define am_iom_isr(n)       am_iomaster ## n ## _isr

#define BNO055_ID           0xA0

#define ACC_DATA_LEN        6
#define GYRO_DATA_LEN       6
#define LACC_DATA_LEN       6
#define GACC_DATA_LEN       6
#define QUAT_DATA_LEN       8
#define STAT_DATA_LEN       1

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

typedef enum
{
   GYRO_DATA,
   ACC_DATA,
   LACC_DATA,
   GACC_DATA,
   QUAT_DATA,
   STAT_DATA,
} bno055_data_type_t;

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


// Static Global Variables ---------------------------------------------------------------------------------------------

static void *i2c_handle;
static volatile bool previously_in_motion;
static uint32_t imu_buffer[BURST_READ_LEN];
static motion_change_callback_t motion_change_callback;
static data_ready_callback_t data_ready_callback;
static bool imu_is_initialized;


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
   if (data_ready_callback)
   {
      imu_data_type_t data_types = IMU_UNKNOWN;
      if (interrupt_status & ACC_BSX_DRDY)
         data_types |= IMU_LINEAR_ACCELEROMETER | IMU_ACCELEROMETER;
      if (interrupt_status & MAG_DRDY)
         data_types |= IMU_MAGNETOMETER;
      if (interrupt_status & GYR_DRDY)
         data_types |= IMU_GYROSCOPE;
      data_ready_callback(data_types);
   }
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
   i2c_write8(BNO055_PWR_MODE_ADDR, POWER_MODE_SUSPEND);
}

static void enable_motion_interrupts(void)
{
   // Set up interrupts for motion and non-motion events
   i2c_write8(BNO055_PAGE_ID_ADDR, 1);
   i2c_write8(ACC_CONFIG, 0b00000000); //operation mode/bandwidth/g range
   i2c_write8(ACC_AM_THRE, 0b00000001);
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
   if (imu_is_initialized)
      return;
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
   am_util_delay_ms(30);

   // Set device to use low-power mode
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

   print("INFO: IMU Initialized\n");
   imu_is_initialized = true;
}

void imu_deinit(void)
{
   // The app will crash if you try to deinit an already deinited IMU
   if (!imu_is_initialized)
      return;
   // Disable interrupts and put the device into suspend mode
   imu_set_op_mode(OPERATION_MODE_CONFIG);
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
   print("INFO: IMU Deinited\n");
   imu_is_initialized = false;
}

bool imu_calibrate_sensors(imu_calibration_data_t calibration_type) { return false; }

bool imu_store_current_calibration(void) { return false; }

void imu_enable_data_outputs(imu_data_type_t data_types, uint32_t report_interval_us)
{
   // Set the fusion mode to 9-DOF if anything other than motion detection is requested
   imu_set_op_mode(OPERATION_MODE_CONFIG);
   if (((data_types & IMU_LINEAR_ACCELEROMETER) || (data_types & IMU_ACCELEROMETER)) && (data_types & IMU_GYROSCOPE) && (data_types & IMU_MAGNETOMETER))
   {
      i2c_write8(BNO055_PWR_MODE_ADDR, POWER_MODE_NORMAL);
      imu_set_op_mode(OPERATION_MODE_NDOF);
   }
   else if ((data_types & ~IMU_MOTION_DETECT))
   {
      i2c_write8(BNO055_PWR_MODE_ADDR, POWER_MODE_LOWPOWER);
      if (((data_types & IMU_LINEAR_ACCELEROMETER) || (data_types & IMU_ACCELEROMETER)) && (data_types & IMU_GYROSCOPE))
         imu_set_op_mode(OPERATION_MODE_ACCGYRO);
      else if (((data_types & IMU_LINEAR_ACCELEROMETER) || (data_types & IMU_ACCELEROMETER)) && (data_types & IMU_MAGNETOMETER))
         imu_set_op_mode(OPERATION_MODE_ACCMAG);
      else if ((data_types & IMU_GYROSCOPE) && (data_types & IMU_MAGNETOMETER))
         imu_set_op_mode(OPERATION_MODE_MAGGYRO);
      else if ((data_types & IMU_LINEAR_ACCELEROMETER) || (data_types & IMU_ACCELEROMETER))
         imu_set_op_mode(OPERATION_MODE_ACCONLY);
      else if ((data_types & IMU_MAGNETOMETER))
         imu_set_op_mode(OPERATION_MODE_MAGONLY);
      else if ((data_types & IMU_GYROSCOPE))
         imu_set_op_mode(OPERATION_MODE_GYRONLY);
   }
   else if ((data_types & IMU_MOTION_DETECT))
   {
      i2c_write8(BNO055_PWR_MODE_ADDR, POWER_MODE_LOWPOWER);
      imu_set_op_mode(OPERATION_MODE_ACCONLY);
   }
   else
      i2c_write8(BNO055_PWR_MODE_ADDR, POWER_MODE_LOWPOWER);
}

void imu_register_motion_change_callback(motion_change_callback_t callback)
{
   // Set up IMU motion-based interrupts
   motion_change_callback = callback;
   imu_clear_interrupts();
   enable_motion_interrupts();
}

void imu_register_data_ready_callback(data_ready_callback_t callback)
{
   // Set up IMU data-ready interrupts
   data_ready_callback = callback;
   imu_clear_interrupts();
   enable_data_ready_interrupts();
}

void imu_read_accel_data(int16_t *x, int16_t *y, int16_t *z, uint8_t *accuracy)
{
   uint8_t *raw_data = (uint8_t*)imu_buffer;
   int16_t* accel_data = (int16_t*)(raw_data + BNO055_ACCEL_DATA_X_LSB_ADDR - BURST_READ_BASE_ADDR);
   *x = accel_data[0];
   *y = accel_data[1];
   *z = accel_data[2];
   *accuracy = 0;
}

void imu_read_linear_accel_data(int16_t *x, int16_t *y, int16_t *z, uint8_t *accuracy)
{
   uint8_t *raw_data = (uint8_t*)imu_buffer;
   int16_t* accel_data = (int16_t*)(raw_data + BNO055_LINEAR_ACCEL_DATA_X_LSB_ADDR - BURST_READ_BASE_ADDR);
   *x = accel_data[0];
   *y = accel_data[1];
   *z = accel_data[2];
   *accuracy = 0;
}

void imu_read_gravity_data(int16_t *x, int16_t *y, int16_t *z, uint8_t *accuracy)
{
   uint8_t *raw_data = (uint8_t*)imu_buffer;
   int16_t* accel_data = (int16_t*)(raw_data + BNO055_GRAVITY_DATA_X_LSB_ADDR - BURST_READ_BASE_ADDR);
   *x = accel_data[0];
   *y = accel_data[1];
   *z = accel_data[2];
   *accuracy = 0;
}

void imu_read_quaternion_data(int16_t *w, int16_t *x, int16_t *y, int16_t *z, int16_t *radian_accuracy, uint8_t *accuracy)
{
   uint8_t *raw_data = (uint8_t*)imu_buffer;
   int16_t* quaternion_data = (int16_t*)(raw_data + BNO055_QUATERNION_DATA_X_LSB_ADDR - BURST_READ_BASE_ADDR);
   //temporary fix of MSB sign bit flipping problem
   for (uint8_t i = 0; i < 4; i++)
   {
      if (quaternion_data[i] > 16384)
         quaternion_data[i] -= 32768;
      else if (quaternion_data[i] < -16384)
         quaternion_data[i] += 32768;
   }
   *w = quaternion_data[0];
   *x = quaternion_data[1];
   *y = quaternion_data[2];
   *z = quaternion_data[3];
   *radian_accuracy = 0;
   *accuracy = 0;
}

void imu_read_gyro_data(int16_t *x, int16_t *y, int16_t *z, uint8_t *accuracy)
{
   uint8_t *raw_data = (uint8_t*)imu_buffer;
   int16_t* gyro_data = (int16_t*)(raw_data + BNO055_GYRO_DATA_X_LSB_ADDR - BURST_READ_BASE_ADDR);
   *x = gyro_data[0];
   *y = gyro_data[1];
   *z = gyro_data[2];
   *accuracy = 0;
}

void imu_read_magnetometer_data(int16_t *x, int16_t *y, int16_t *z, uint8_t *accuracy)
{
   uint8_t *raw_data = (uint8_t*)imu_buffer;
   int16_t* mag_data = (int16_t*)(raw_data + BNO055_MAG_DATA_X_LSB_ADDR - BURST_READ_BASE_ADDR);
   *x = mag_data[0];
   *y = mag_data[1];
   *z = mag_data[2];
   *accuracy = 0;
}

uint16_t imu_read_step_count() { return 0; }

bool imu_read_in_motion(void)
{
   return previously_in_motion;
}

void imu_convert_q_format_to_float(imu_data_type_t data_type, int16_t q_x, int16_t q_y, int16_t q_z, int16_t q_w_optional, int16_t q_accuracy_optional, float *x, float *y, float *z, float *w, float *accuracy)
{
   *x = q_x;
   *y = q_y;
   *z = q_z;
   if (w)
      *w = q_w_optional;
   if (accuracy)
      *accuracy = q_accuracy_optional;
}

void imu_clear_interrupts(void)
{
   // Reset the interrupt trigger bits
   i2c_write8(BNO055_SYS_TRIGGER_ADDR, 0xC0);
}
