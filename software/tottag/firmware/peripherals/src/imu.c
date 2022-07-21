// Header inclusions ---------------------------------------------------------------------------------------------------

#include "imu.h"
#include "nrfx_spi.h"


#if (BOARD_V < 0x11)  // Accelerometer forwarding for older boards -----------------------------------------------------

#include "accelerometer.h"

bool imu_init(imu_data_callback callback) { return accelerometer_init(callback); }
void imu_handle_incoming_data(uint32_t timestamp) { accelerometer_handle_incoming_data(timestamp); }


#else  // True IMU functionality for newer boards ----------------------------------------------------------------------

// Static IMU state variables ------------------------------------------------------------------------------------------

static nrfx_spi_t _imu_spi_instance = NRFX_SPI_INSTANCE(IMU_SPI_BUS_IDX);
static nrfx_spi_config_t _imu_spi_config = NRFX_SPI_DEFAULT_CONFIG;
static nrfx_atomic_flag_t _imu_data_ready, _imu_motion_changed;
static uint8_t _imu_read_write_buf[257] = { 0 };
static imu_data_callback _data_callback = NULL;
static float x_data[1], y_data[1], z_data[1];
static imudev_ctx_t imu_context = { 0 };


// LSM6DSOX-specific IMU functionality ---------------------------------------------------------------------------------

static int32_t platform_read(void *handle, uint8_t reg, uint8_t *read_buf, uint16_t len)
{
   // Use SPI directly
   uint8_t read_register = reg | 0x80;
   nrfx_spi_xfer_desc_t read_transfer = NRFX_SPI_XFER_TRX(&read_register, 1, _imu_read_write_buf, 1 + len);
   nrfx_err_t err_code = nrfx_spi_xfer(&_imu_spi_instance, &read_transfer, 0);
   memcpy(read_buf, &_imu_read_write_buf[1], len);
   return (int32_t)err_code;
}

static int32_t platform_write(void *handle, uint8_t reg, const uint8_t *write_buf, uint16_t len)
{
   // Use SPI directly
   _imu_read_write_buf[0] = reg & 0x7F;
   memcpy(&_imu_read_write_buf[1], write_buf, len);
   nrfx_spi_xfer_desc_t write_transfer = NRFX_SPI_XFER_TX(_imu_read_write_buf, 1 + len);
   nrfx_err_t err_code = nrfx_spi_xfer(&_imu_spi_instance, &write_transfer, 0);
   return (int32_t)err_code;
}

static void imu_interrupt_handler(nrfx_gpiote_pin_t pin, nrf_gpiote_polarity_t action)
{
   // Set the "data ready" flag for the IMU
   if (pin == IMU_INT1)
      nrfx_atomic_flag_set(&_imu_motion_changed);
   else if (pin == IMU_INT2)
      nrfx_atomic_flag_set(&_imu_data_ready);
}

static nrfx_err_t imu_read_accelerometer_data(float* x, float* y, float* z)
{
   return NRFX_ERROR_NOT_SUPPORTED;
}

static bool imu_in_motion(void)
{
   static lsm6dsox_all_sources_t all_source;
   lsm6dsox_all_sources_get(&imu_context, &all_source);
   return !all_source.sleep_state;
}


// Public IMU API functions --------------------------------------------------------------------------------------------

bool imu_init(imu_data_callback callback)
{
   // Configure the magnetometer input pins as INPUT ANALOG no-ops
   nrf_gpio_cfg_default(MAGNETOMETER_INT);
   nrf_gpio_cfg_default(MAGNETOMETER_DRDY);

   // Setup SPI parameters
   _data_callback = callback;
   _imu_spi_config.sck_pin = IMU_SPI_SCLK;
   _imu_spi_config.miso_pin = IMU_SPI_MISO;
   _imu_spi_config.mosi_pin = IMU_SPI_MOSI;
   _imu_spi_config.ss_pin = IMU_SPI_CS;
   _imu_spi_config.frequency = NRF_SPI_FREQ_4M;
   _imu_spi_config.mode = NRF_SPI_MODE_0;
   _imu_data_ready = _imu_motion_changed = false;
   nrfx_spi_init(&_imu_spi_instance, &_imu_spi_config, NULL, NULL);

   // Setup the IMU read/write context
   imu_context.write_reg = platform_write;
   imu_context.read_reg = platform_read;
   imu_context.handle = NULL;

   // Check the IMU ID
   uint8_t dummy;
   lsm6dsox_device_id_get(&imu_context, &dummy);
   if (dummy == LSM6DSOX_ID)
   {
      // Restore the default chip configuration and disable the I3C interface
      lsm6dsox_reset_set(&imu_context, PROPERTY_ENABLE);
      do { lsm6dsox_reset_get(&imu_context, &dummy); } while (dummy);
      lsm6dsox_i3c_disable_set(&imu_context, LSM6DSOX_I3C_DISABLE);

      // Configure the LSM6DSOX output range and data rate
      lsm6dsox_xl_full_scale_set(&imu_context, LSM6DSOX_2g);
      lsm6dsox_gy_full_scale_set(&imu_context, LSM6DSOX_250dps);
      lsm6dsox_block_data_update_set(&imu_context, PROPERTY_ENABLE);
      lsm6dsox_xl_data_rate_set(&imu_context, LSM6DSOX_XL_ODR_52Hz);
      lsm6dsox_gy_data_rate_set(&imu_context, LSM6DSOX_GY_ODR_12Hz5);

      // Configure the LSM6DSOX power modes
      lsm6dsox_xl_power_mode_set(&imu_context, LSM6DSOX_LOW_NORMAL_POWER_MD);
      lsm6dsox_gy_power_mode_set(&imu_context, LSM6DSOX_GY_NORMAL);

      // Set the duration for activity/inactivity detection to:
      //   [80.0 ms (0x01 * 1 / ODR_XL), 9.85 s (MAX(16, 0x01 * 512) / ODR_XL)]
      lsm6dsox_wkup_dur_set(&imu_context, 0x01);
      lsm6dsox_act_sleep_dur_set(&imu_context, 0x01);
      lsm6dsox_wkup_threshold_set(&imu_context, 0x02);
      lsm6dsox_act_mode_set(&imu_context, LSM6DSOX_XL_12Hz5_GY_PD);

      // Enable generation of activity-change interrupts on INT1 pin
      nrfx_gpiote_in_config_t int1_gpio_config = NRFX_GPIOTE_CONFIG_IN_SENSE_TOGGLE(0);
      nrfx_gpiote_in_init(IMU_INT1, &int1_gpio_config, imu_interrupt_handler);
      nrfx_gpiote_in_event_enable(IMU_INT1, 1);
      lsm6dsox_pin_int1_route_t int1_route = { 0 };
      int1_route.sleep_change = PROPERTY_ENABLE;
      lsm6dsox_pin_int1_route_set(&imu_context, int1_route);
   }

   // Reset the SPI driver
   nrfx_spi_uninit(&_imu_spi_instance);
   nrfx_gpiote_out_clear(IMU_SPI_SCLK);
   return true;
}

void imu_handle_incoming_data(uint32_t timestamp)
{
   bool data_ready = nrfx_atomic_flag_clear_fetch(&_imu_data_ready);
   if (nrfx_atomic_flag_clear_fetch(&_imu_motion_changed) || data_ready)
   {
      // Read the IMU data
      nrfx_spi_uninit(&_imu_spi_instance);
      nrfx_spi_init(&_imu_spi_instance, &_imu_spi_config, NULL, NULL);
      bool in_motion = imu_in_motion();
      if (data_ready)
         imu_read_accelerometer_data(x_data, y_data, z_data);
      nrfx_spi_uninit(&_imu_spi_instance);
      nrfx_gpiote_out_clear(IMU_SPI_SCLK);

      // Fire the IMU callback with the retrieved data
      if (data_ready)
         _data_callback(in_motion, timestamp, x_data, y_data, z_data);
      else
         _data_callback(in_motion, timestamp, NULL, NULL, NULL);
   }
}

#endif  // #if (BOARD_V < 0x11)
