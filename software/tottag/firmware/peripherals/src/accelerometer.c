// Header inclusions ---------------------------------------------------------------------------------------------------

#include <string.h>
#include "accelerometer.h"
#include "ble_config.h"
#include "nrf_delay.h"


// Static accelerometer state variables --------------------------------------------------------------------------------

#if (BOARD_V < 0x11)

static const nrfx_spim_t* _spi_instance = NULL;
static nrfx_spim_config_t _spi_config = NRFX_SPIM_DEFAULT_CONFIG;
static lis2dw12_config_t _accelerometer_config = { .odr = lis2dw12_odr_200, .mode = lis2dw12_low_power,
      .lp_mode = lis2dw12_lp_1, .cs_nopull = 0, .bdu = 1, .auto_increment = 1, .i2c_disable = 1, .int_active_low = 0,
      .on_demand = 1, .bandwidth = lis2dw12_bw_odr_2,.fs = lis2dw12_fs_4g, .high_pass = 1, .low_noise  = 1 };
static lis2dw12_int_config_t _accelerometer_int_config = { 0 };
static lis2dw12_wakeup_config_t _accelerometer_wake_config = { .sleep_enable = true, .threshold = 0x05,
      .wake_duration = 0, .sleep_duration = 1 };
static nrfx_atomic_flag_t* _accelerometer_data_ready = NULL;
static float _acc_sensitivity_scalar = 1.0f;
static uint8_t _acc_xyz[(ACC_NUM_RESULTS_PER_READ*3*sizeof(int16_t)) + 1] = { 0 };
static uint8_t _lis2dw12_read_buf[257] = { 0 }, _lis2dw12_write_buf[257] = { 0 }, _lsb_empty_bits = 0;


// LIS2DW12-specific accelerometer functionality -----------------------------------------------------------------------

static nrfx_err_t lis2dw12_read_reg(uint8_t reg, uint8_t* read_buf, size_t len)
{
   // Use SPI directly
   if (len > (sizeof(_lis2dw12_read_buf) - 1))
      return NRFX_ERROR_NO_MEM;
   uint8_t readreg = reg | LIS2DW12_SPI_READ;
   nrfx_spim_uninit(_spi_instance);
   nrfx_err_t err_code = nrfx_spim_init(_spi_instance, &_spi_config, NULL, NULL);
   if (err_code != NRFX_SUCCESS)
      return err_code;
   nrfx_spim_xfer_desc_t xfer_desc = NRFX_SPIM_XFER_TRX(&readreg, 1, _lis2dw12_read_buf, sizeof(_lis2dw12_read_buf));
   err_code = nrfx_spim_xfer(_spi_instance, &xfer_desc, 0);
   if (err_code != NRFX_SUCCESS)
      return err_code;
   memcpy(read_buf, _lis2dw12_read_buf+1, len);
   return err_code;
}

static nrfx_err_t lis2dw12_write_reg(uint8_t reg, uint8_t* write_buf, size_t len)
{
   // Use SPI directly
   if (len > (sizeof(_lis2dw12_write_buf) - 1))
      return NRFX_ERROR_NO_MEM;
   _lis2dw12_write_buf[0] = reg & (uint8_t)LIS2DW12_SPI_WRITE;
   memcpy(_lis2dw12_write_buf + 1, write_buf, len);
   nrfx_spim_uninit(_spi_instance);
   nrfx_err_t err_code = nrfx_spim_init(_spi_instance, &_spi_config, NULL, NULL);
   if (err_code != NRFX_SUCCESS)
      return err_code;
   nrfx_spim_xfer_desc_t xfer_desc = NRFX_SPIM_XFER_TX(_lis2dw12_write_buf, len + 1);
   return nrfx_spim_xfer(_spi_instance, &xfer_desc, 0);
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
   // Reset byte in register CTRL2
   uint8_t reset_byte = 1 << 6;
   lis2dw12_write_reg(LIS2DW12_CTRL2, &reset_byte, 1);

   // Check that SoftReset value is reset
   while (((reset_byte >> 6) & 0x01) == 1)
   {
      // Allow accelerometer time to reset
      nrf_delay_ms(10);
      lis2dw12_read_reg(LIS2DW12_CTRL2, &reset_byte, 1);
   }
}

static void lis2dw12_fifo_reset(void)
{
   // To reset FIFO content, Bypass mode should be written and then FIFO mode should be restarted
   lis2dw12_fifo_config_t fifo_config;
   fifo_config.mode = lis2dw12_fifo_bypass;
   lis2dw12_fifo_config(fifo_config);
   fifo_config.mode = lis2dw12_fifo_continuous;
   fifo_config.thresh = 31;
   lis2dw12_fifo_config(fifo_config);
}

static bool lis2dw12_is_stationary(void)
{
   uint8_t status_byte;
   lis2dw12_read_reg(LIS2DW12_STATUS, &status_byte, 1);
   return ((status_byte >> 5) & 0x1);
}

static void accelerometer_fifo_full_handler(nrfx_gpiote_pin_t pin, nrf_gpiote_polarity_t action)
{
   // Set the "data ready" flag for the accelerometer
   if (pin == CARRIER_IMU_INT2)
      nrfx_atomic_flag_set(_accelerometer_data_ready);
}

#endif  // #if (BOARD_V < 0x11)


// Public Accelerometer API functions ----------------------------------------------------------------------------------

bool accelerometer_init(const nrfx_spim_t* spi_instance, nrfx_atomic_flag_t* data_ready)
{
#if (BOARD_V < 0x11)

   // Setup SPI parameters
   _spi_instance = spi_instance;
   _spi_config.sck_pin = IMU_SPI_SCLK;
   _spi_config.miso_pin = IMU_SPI_MISO;
   _spi_config.mosi_pin = IMU_SPI_MOSI;
   _spi_config.ss_pin = CARRIER_CS_IMU;
   _spi_config.frequency = NRF_SPIM_FREQ_4M;
   _spi_config.mode = NRF_SPIM_MODE_3;
   _spi_config.bit_order = NRF_SPIM_BIT_ORDER_MSB_FIRST;

   // Initialize SPI for the accelerometer
   nrfx_spim_uninit(_spi_instance);
   APP_ERROR_CHECK(nrfx_spim_init(_spi_instance, &_spi_config, NULL, NULL));

   // Turn on and configure the accelerometer
   _accelerometer_int_config.int1_wakeup = true;
   _accelerometer_int_config.int2_fifo_full = true;
   lis2dw12_reset();
   lis2dw12_config();
   lis2dw12_interrupt_config();
   lis2dw12_wakeup_config();

   // Initialize the accelerometer interrupt pin
   _accelerometer_data_ready = data_ready;
   nrfx_gpiote_in_config_t int_gpio_config = NRFX_GPIOTE_CONFIG_IN_SENSE_LOTOHI(1);
   nrfx_gpiote_in_init(CARRIER_IMU_INT2, &int_gpio_config, accelerometer_fifo_full_handler);
   nrfx_gpiote_in_event_enable(CARRIER_IMU_INT2, 1);
   lis2dw12_interrupt_enable(true);

   // Reset FIFO
   lis2dw12_fifo_reset();
   return true;

#else
   return false;
#endif  // #if (BOARD_V < 0x11)
}

nrfx_err_t accelerometer_read_data(float* x_data, float* y_data, float* z_data)
{
#if (BOARD_V < 0x11)

   // Read 32 samples from the accelerometer FIFO
   uint8_t readreg = LIS2DW12_OUT_X_L | LIS2DW12_SPI_READ;
   nrfx_spim_uninit(_spi_instance);
   nrfx_err_t err_code = nrfx_spim_init(_spi_instance, &_spi_config, NULL, NULL);
   if (err_code != NRFX_SUCCESS)
      return err_code;
   nrfx_spim_xfer_desc_t xfer_desc = NRFX_SPIM_XFER_TRX(&readreg, 1, _acc_xyz, sizeof(_acc_xyz));
   err_code = nrfx_spim_xfer(_spi_instance, &xfer_desc, 0);
   if (err_code != NRFX_SUCCESS)
      return err_code;

   // Only process accelerometer data if we are in motion
   if (lis2dw12_is_stationary())
      return NRFX_ERROR_INVALID_STATE;

   // Convert each sample to milli-g's
   for (size_t i = 0, j = 1; i < ACC_NUM_RESULTS_PER_READ; ++i, j += 6)
   {
      if (x_data) x_data[i] = ((int16_t)(_acc_xyz[j+0] | ((int16_t)_acc_xyz[j+1] << 8)) >> _lsb_empty_bits) * _acc_sensitivity_scalar;
      if (y_data) y_data[i] = ((int16_t)(_acc_xyz[j+2] | ((int16_t)_acc_xyz[j+3] << 8)) >> _lsb_empty_bits) * _acc_sensitivity_scalar;
      if (z_data) z_data[i] = ((int16_t)(_acc_xyz[j+4] | ((int16_t)_acc_xyz[j+5] << 8)) >> _lsb_empty_bits) * _acc_sensitivity_scalar;
   }
   return NRFX_SUCCESS;

#else

   return NRFX_ERROR_INTERNAL;

#endif  // #if (BOARD_V < 0x11)
}
