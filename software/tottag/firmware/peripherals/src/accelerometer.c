// Header inclusions ---------------------------------------------------------------------------------------------------

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wredundant-decls"

#include <string.h>
#include "accelerometer.h"
#include "ble_config.h"
#include "nrf_delay.h"
#include "nrfx_gpiote.h"

#pragma GCC diagnostic pop


// Static accelerometer state variables --------------------------------------------------------------------------------

#if (BOARD_V < 0x11)

#define DISABLE_ACCEL_DATA_READING

static nrf_drv_spi_t _spi_instance = NRF_DRV_SPI_INSTANCE(IMU_SPI_BUS_IDX);
static nrf_drv_spi_config_t _spi_config = NRF_DRV_SPI_DEFAULT_CONFIG;
static lis2dw12_config_t _accelerometer_config = { .odr = lis2dw12_odr_200, .mode = lis2dw12_low_power,
      .lp_mode = lis2dw12_lp_1, .cs_nopull = 0, .bdu = 1, .auto_increment = 1, .i2c_disable = 1, .int_active_low = 0,
      .on_demand = 1, .bandwidth = lis2dw12_bw_odr_4, .fs = lis2dw12_fs_2g, .high_pass = 1, .low_noise  = 0 };
static lis2dw12_int_config_t _accelerometer_int_config = { .int2_sleep_change = 1 };
static lis2dw12_wakeup_config_t _accelerometer_wake_config = { .sleep_enable = true, .threshold = 0x01,
      .wake_duration = 1, .sleep_duration = 1 };
static nrfx_atomic_flag_t _accelerometer_data_ready, _accelerometer_motion_changed;
static float _acc_sensitivity_scalar = 1.0f;
static float x_data[ACC_NUM_RESULTS_PER_READ], y_data[ACC_NUM_RESULTS_PER_READ], z_data[ACC_NUM_RESULTS_PER_READ];
static uint8_t _acc_xyz[(ACC_NUM_RESULTS_PER_READ*3*sizeof(int16_t)) + 1] = { 0 };
static uint8_t _lis2dw12_read_write_buf[257] = { 0 }, _lsb_empty_bits = 0;
static imu_data_callback _data_callback = NULL;


// LIS2DW12-specific accelerometer functionality -----------------------------------------------------------------------

static nrfx_err_t lis2dw12_read_reg(uint8_t reg, uint8_t* read_buf, size_t len)
{
   // Use SPI directly
   uint8_t readreg = reg | LIS2DW12_SPI_READ;
   nrfx_err_t err_code = nrf_drv_spi_transfer(&_spi_instance, &readreg, 1, _lis2dw12_read_write_buf, len + 1);
   memcpy(read_buf, _lis2dw12_read_write_buf+1, len);
   return err_code;
}

static nrfx_err_t lis2dw12_write_reg(uint8_t reg, uint8_t* write_buf, size_t len)
{
   // Use SPI directly
   _lis2dw12_read_write_buf[0] = reg & (uint8_t)LIS2DW12_SPI_WRITE;
   memcpy(_lis2dw12_read_write_buf + 1, write_buf, len);
   nrfx_err_t err_code = nrf_drv_spi_transfer(&_spi_instance, _lis2dw12_read_write_buf, len + 1, NULL, 0);
   return err_code;
}

static void lis2dw12_config(void)
{
   // Setup the LIS2DW12 control registers with the specified configuration values
   uint8_t buf[6] = { 0 };
   buf[0] = _accelerometer_config.odr << 4 | (_accelerometer_config.mode & 0x3) << 2 | (_accelerometer_config.lp_mode & 0x3);
   buf[1] = _accelerometer_config.cs_nopull << 4 | _accelerometer_config.bdu << 3 | _accelerometer_config.auto_increment << 2 | _accelerometer_config.i2c_disable << 1 | _accelerometer_config.sim;
   buf[2] = _accelerometer_config.int_active_low << 3 | _accelerometer_config.on_demand << 1;
   buf[5] = _accelerometer_config.bandwidth << 6 | _accelerometer_config.fs << 4 | _accelerometer_config.high_pass << 3 | _accelerometer_config.low_noise << 2;

   // Write the configuration and set the number of empty LSB bits
   lis2dw12_write_reg(LIS2DW12_CTRL1, buf, sizeof(buf));
   _lsb_empty_bits = (((_accelerometer_config.lp_mode == lis2dw12_lp_1) && (_accelerometer_config.mode == lis2dw12_low_power)) ? 4 : 2);

   // Determine the sensitivity scalar given the current configuration
   _acc_sensitivity_scalar = 0.244f;
   if ((_accelerometer_config.lp_mode == lis2dw12_lp_1) && (_accelerometer_config.mode == lis2dw12_low_power)) // 12-bit
      switch (_accelerometer_config.fs)
      {
         case lis2dw12_fs_4g:
            _acc_sensitivity_scalar = 1.952f;
            break;
         case lis2dw12_fs_8g:
            _acc_sensitivity_scalar = 3.904f;
            break;
         case lis2dw12_fs_16g:
            _acc_sensitivity_scalar = 7.808f;
            break;
         case lis2dw12_fs_2g:
         default:
            _acc_sensitivity_scalar = 0.976f;
            break;
      }
   else                                                                                                        // 14-bit
      switch (_accelerometer_config.fs)
      {
         case lis2dw12_fs_4g:
            _acc_sensitivity_scalar = 0.488f;
            break;
         case lis2dw12_fs_8g:
            _acc_sensitivity_scalar = 0.976f;
            break;
         case lis2dw12_fs_16g:
            _acc_sensitivity_scalar = 1.952f;
            break;
         case lis2dw12_fs_2g:
         default:
            _acc_sensitivity_scalar = 0.244f;
            break;
      }
}

static void lis2dw12_interrupt_config(void)
{
   // Setup the LIS2DW12 interrupt control registers with the specified values
   uint8_t buf[2] = { 0 };
   buf[0]  = _accelerometer_int_config.int1_6d << 7 | _accelerometer_int_config.int1_sngl_tap << 6 |
         _accelerometer_int_config.int1_wakeup << 5 | _accelerometer_int_config.int1_free_fall << 4 |
         _accelerometer_int_config.int1_dbl_tap << 3 | _accelerometer_int_config.int1_fifo_full << 2 |
         _accelerometer_int_config.int1_fifo_thresh << 1 | _accelerometer_int_config.int1_data_ready;
   buf[1]  = _accelerometer_int_config.int2_sleep_state << 7 | _accelerometer_int_config.int2_sleep_change << 6 |
         _accelerometer_int_config.int2_boot << 5 | _accelerometer_int_config.int2_data_ready << 4 |
         _accelerometer_int_config.int2_fifo_over << 3 | _accelerometer_int_config.int2_fifo_full << 2 |
         _accelerometer_int_config.int2_fifo_thresh << 1 | _accelerometer_int_config.int2_data_ready;
   lis2dw12_write_reg(LIS2DW12_CTRL4_INT1, buf, 2);
}

static void lis2dw12_interrupt_enable(bool enable)
{
   uint8_t int_enable = enable << 5;
   lis2dw12_write_reg(LIS2DW12_CTRL7, &int_enable, 1);
}

static void lis2dw12_wakeup_config(void)
{
   // Setup the LIS2DW12 wakeup control registers with the specified values
   uint8_t wake_ths_byte = _accelerometer_wake_config.sleep_enable << 6 | (_accelerometer_wake_config.threshold & 0x3f);
   uint8_t wake_dur_byte = (_accelerometer_wake_config.wake_duration & 0x3) << 5 | (_accelerometer_wake_config.sleep_duration & 0xf);
   lis2dw12_write_reg(LIS2DW12_WAKE_UP_THS, &wake_ths_byte, 1);
   lis2dw12_write_reg(LIS2DW12_WAKE_UP_DUR, &wake_dur_byte, 1);
}

static void lis2dw12_fifo_config(lis2dw12_fifo_config_t config)
{
   uint8_t fifo_byte = config.mode << 5 | (config.thresh & 0x1f);
   lis2dw12_write_reg(LIS2DW12_FIFO_CTRL, &fifo_byte, 1);
}

static void lis2dw12_reset(void)
{
   // Check that SoftReset value is reset
   uint8_t reset_byte = 0x40;
   for (int retries = 0; (retries < 10); ++retries)
   {
      // Reset byte in register CTRL2
      lis2dw12_write_reg(LIS2DW12_CTRL2, &reset_byte, 1);
      nrf_delay_ms(10);
      lis2dw12_read_reg(LIS2DW12_CTRL2, &reset_byte, 1);
   }
}

static void lis2dw12_fifo_reset(void)
{
   // To reset FIFO content, Bypass mode should be written and then FIFO mode should be restarted
   lis2dw12_fifo_config_t fifo_config = { .mode = lis2dw12_fifo_bypass, .thresh = 0 };
   lis2dw12_fifo_config(fifo_config);
#ifndef DISABLE_ACCEL_DATA_READING
   fifo_config.mode = lis2dw12_fifo_continuous;
   fifo_config.thresh = 31;
   lis2dw12_fifo_config(fifo_config);
#endif
}

static bool lis2dw12_is_stationary(void)
{
   uint8_t status_byte;
   lis2dw12_read_reg(LIS2DW12_STATUS, &status_byte, 1);
   return ((status_byte >> 5) & 0x1);
}

static void accelerometer_interrupt_handler(nrfx_gpiote_pin_t pin, nrf_gpiote_polarity_t action)
{
   // Set the "data ready" flag for the accelerometer
   if (pin == IMU_INT1)
      nrfx_atomic_flag_set(&_accelerometer_data_ready);
   else if (pin == IMU_INT2)
      nrfx_atomic_flag_set(&_accelerometer_motion_changed);
}

static nrfx_err_t accelerometer_read_data(float* x, float* y, float* z)
{
#if (BOARD_V < 0x11)

   // Read 32 samples from the accelerometer FIFO
   uint8_t readreg = LIS2DW12_OUT_X_L | LIS2DW12_SPI_READ;
   nrfx_err_t err_code = nrf_drv_spi_transfer(&_spi_instance, &readreg, 1, _acc_xyz, sizeof(_acc_xyz));
   if (err_code != NRFX_SUCCESS)
      return err_code;

   // Convert each sample to milli-g's
   for (size_t i = 0, j = 1; i < ACC_NUM_RESULTS_PER_READ; ++i, j += 6)
   {
      if (x) x[i] = ((int16_t)(_acc_xyz[j+0] | ((int16_t)_acc_xyz[j+1] << 8)) >> _lsb_empty_bits) * _acc_sensitivity_scalar;
      if (y) y[i] = ((int16_t)(_acc_xyz[j+2] | ((int16_t)_acc_xyz[j+3] << 8)) >> _lsb_empty_bits) * _acc_sensitivity_scalar;
      if (z) z[i] = ((int16_t)(_acc_xyz[j+4] | ((int16_t)_acc_xyz[j+5] << 8)) >> _lsb_empty_bits) * _acc_sensitivity_scalar;
   }
   return NRFX_SUCCESS;

#else

   return NRFX_ERROR_INTERNAL;

#endif  // #if (BOARD_V < 0x11)
}

static bool accelerometer_in_motion(void)
{
#if (BOARD_V < 0x11)

   // Read the stationary detection register
   return !lis2dw12_is_stationary();

#endif
   return false;
}

#endif  // #if (BOARD_V < 0x11)


// Public Accelerometer API functions ----------------------------------------------------------------------------------

bool accelerometer_init(imu_data_callback callback)
{
#if (BOARD_V < 0x11)

   // Initialize SPI communications
   _data_callback = callback;
   _spi_config.sck_pin = IMU_SPI_SCLK;
   _spi_config.miso_pin = IMU_SPI_MISO;
   _spi_config.mosi_pin = IMU_SPI_MOSI;
   _spi_config.ss_pin = IMU_SPI_CS;
   _spi_config.frequency = NRF_DRV_SPI_FREQ_4M;
   _spi_config.mode = NRF_DRV_SPI_MODE_3;
   nrf_drv_spi_init(&_spi_instance, &_spi_config, NULL, NULL);
   _accelerometer_data_ready = _accelerometer_motion_changed = false;

   // Turn on and configure the accelerometer
#ifndef DISABLE_ACCEL_DATA_READING
   _accelerometer_int_config.int1_fifo_full = 1;
#endif
   lis2dw12_reset();
   lis2dw12_config();
   lis2dw12_interrupt_config();
   lis2dw12_wakeup_config();

   // Initialize the accelerometer interrupt pins
#ifndef DISABLE_ACCEL_DATA_READING
   nrfx_gpiote_in_config_t int1_gpio_config = NRFX_GPIOTE_CONFIG_IN_SENSE_LOTOHI(1);
   nrfx_gpiote_in_init(IMU_INT1, &int1_gpio_config, accelerometer_interrupt_handler);
   nrfx_gpiote_in_event_enable(IMU_INT1, 1);
#endif
   nrfx_gpiote_in_config_t int2_gpio_config = NRFX_GPIOTE_CONFIG_IN_SENSE_LOTOHI(1);
   nrfx_gpiote_in_init(IMU_INT2, &int2_gpio_config, accelerometer_interrupt_handler);
   nrfx_gpiote_in_event_enable(IMU_INT2, 1);
   lis2dw12_interrupt_enable(true);

   // Reset FIFO
   lis2dw12_fifo_reset();
   nrf_drv_spi_uninit(&_spi_instance);
   nrfx_gpiote_out_clear(IMU_SPI_SCLK);
   return true;

#else
   return false;
#endif  // #if (BOARD_V < 0x11)
}

void accelerometer_handle_incoming_data(uint32_t timestamp)
{
#if (BOARD_V < 0x11)

   bool data_ready = nrfx_atomic_flag_clear_fetch(&_accelerometer_data_ready);
   if (nrfx_atomic_flag_clear_fetch(&_accelerometer_motion_changed) || data_ready)
   {
      // Read the accelerometer data
      nrf_drv_spi_uninit(&_spi_instance);
      nrf_drv_spi_init(&_spi_instance, &_spi_config, NULL, NULL);
      bool in_motion = accelerometer_in_motion();
      if (data_ready)
         accelerometer_read_data(x_data, y_data, z_data);
      nrf_drv_spi_uninit(&_spi_instance);
      nrfx_gpiote_out_clear(IMU_SPI_SCLK);

      // Fire the IMU callback with the retrieved data
      if (data_ready)
         _data_callback(in_motion, timestamp, x_data, y_data, z_data);
      else
         _data_callback(in_motion, timestamp, NULL, NULL, NULL);
   }

#endif  // #if (BOARD_V < 0x11)
}
