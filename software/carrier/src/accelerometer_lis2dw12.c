#include "nrf_spi_mngr.h"
#include "nrf_drv_spi.h"
#include "nrf_gpio.h"
#include "nrf_delay.h"
#include "nrfx_gpiote.h"
#include "app_error.h"

#include "accelerometer_lis2dw12.h"
#include "boards.h"

// Static functions
static void lis2dw12_read_full_fifo_callback(nrf_drv_spi_evt_t const* p_event, void* p_context);
static lis2dw12_read_full_fifo_callback_t fifo_callback;

// Configurations
//static const nrf_spi_mngr_t* spi_instance;
static const nrf_drv_spi_t* spi_instance;
static lis2dw12_config_t ctl_config;
static nrf_drv_spi_config_t spi_config  = NRF_DRV_SPI_DEFAULT_CONFIG;

// Static variables
#define XYZ_DATA_LENGTH (32*3*2)
static uint8_t xyz_buf[1 + XYZ_DATA_LENGTH]; // buffer to hold all fifo data
static int16_t *x_buf, *y_buf, *z_buf;
static uint8_t full_scale = 2;


void  lis2dw12_init(const nrf_drv_spi_t* instance) {
  spi_instance = instance;

  // Set correct SPI configurations
  spi_config.sck_pin      = SPI_SCLK;
  spi_config.miso_pin     = SPI_MISO;
  spi_config.mosi_pin     = SPI_MOSI;
  spi_config.ss_pin       = LI2D_CS;
  spi_config.frequency    = NRF_DRV_SPI_FREQ_4M;
  spi_config.mode         = NRF_DRV_SPI_MODE_3;
  spi_config.bit_order    = NRF_DRV_SPI_BIT_ORDER_MSB_FIRST;
}

void  lis2dw12_config(lis2dw12_config_t config) {
  ctl_config = config;

  switch (config.fs) {
    case lis2dw12_fs_2g:
      full_scale = 2;
      break;
    case lis2dw12_fs_4g:
      full_scale = 4;
      break;
    case lis2dw12_fs_8g:
      full_scale = 8;
      break;
    case lis2dw12_fs_16g:
      full_scale = 16;
      break;
  }

  #define CONFIG_BUF_LENGTH 6
  uint8_t buf[CONFIG_BUF_LENGTH] = {0};
  buf[0] = config.odr << 4 | (config.mode & 0x3) << 2 |
           (config.lp_mode & 0x3);
  buf[1] = config.cs_nopull << 4 | config.bdu << 3 |
           config.auto_increment << 2 | config.i2c_disable << 1 |
           config.sim;
  buf[2] = config.int_active_low << 3 | config.on_demand << 1;
  buf[5] = config.bandwidth << 6 | config.fs << 4 |
           config.high_pass << 3 | config.low_noise << 2;

  // Write configs
  lis2dw12_write_reg(LIS2DW12_CTRL1, buf, CONFIG_BUF_LENGTH);
}

static uint8_t lis2dw12_read_buf[257];

void  lis2dw12_read_reg(uint8_t reg, uint8_t* read_buf, size_t len) {

    //printf("Reading from register\n");

    if (len > 256) return;
    uint8_t readreg = reg | LIS2DW12_SPI_READ;


    // Use SPI Manager: we have to read len + 1, as the first (written) byte is also read again
    /*nrf_spi_mngr_transfer_t config_transfer[] = {
            NRF_SPI_MNGR_TRANSFER( &readreg, 1, lis2dw12_read_buf, len+1),
    };

    ret_code_t error = nrf_spi_mngr_perform(spi_instance, &spi_config, config_transfer, 1, NULL);*/

    // Use SPI directly
    /*nrf_drv_spi_uninit(spi_instance);
    nrf_drv_spi_init(spi_instance, &spi_config, NULL, NULL);*/
    ret_code_t error = nrf_drv_spi_transfer(spi_instance, &readreg, 1, lis2dw12_read_buf, len+1);
    APP_ERROR_CHECK(error);

    memcpy(read_buf, lis2dw12_read_buf+1, len);
}

static uint8_t lis2dw12_write_buf[257];

void lis2dw12_write_reg(uint8_t reg, uint8_t* write_buf, size_t len) {

    //printf("Writing to register\n");

    if (len > 256) return;
    lis2dw12_write_buf[0] = reg & (uint8_t)LIS2DW12_SPI_WRITE;
    memcpy(lis2dw12_write_buf + 1, write_buf, len);


    // Use SPI Manager
    /*nrf_spi_mngr_transfer_t config_transfer[] = {
            NRF_SPI_MNGR_TRANSFER(lis2dw12_write_buf, len+1, NULL, 0),
    };

    ret_code_t error = nrf_spi_mngr_perform(spi_instance, &spi_config, config_transfer, 1, NULL);*/


    // Use SPI directly
    /*nrf_drv_spi_uninit(spi_instance);
    nrf_drv_spi_init(spi_instance, &spi_config, NULL, NULL);*/
    ret_code_t error = nrf_drv_spi_transfer(spi_instance, lis2dw12_write_buf, len+1, NULL, 0);

    APP_ERROR_CHECK(error);
}

void  lis2dw12_interrupt_config(lis2dw12_int_config_t config){

  uint8_t buf[2] = {0};

  buf[0]  = config.int1_6d << 7 | config.int1_sngl_tap << 6 |
                config.int1_wakeup << 5 | config.int1_free_fall << 4 |
                config.int1_dbl_tap << 3 | config.int1_fifo_full << 2 |
                config.int1_fifo_thresh << 1 | config.int1_data_ready;
  buf[1]  = config.int2_sleep_state << 7 | config.int2_sleep_change << 6 |
                config.int2_boot << 5 | config.int2_data_ready << 4 |
                config.int2_fifo_over << 3 | config.int2_fifo_full << 2 |
                config.int2_fifo_thresh << 1 | config.int2_data_ready;

  lis2dw12_write_reg(LIS2DW12_CTRL4_INT1, buf, 2);
}

void  lis2dw12_interrupt_enable(bool enable){
  uint8_t int_enable = enable << 5;

  lis2dw12_write_reg(LIS2DW12_CTRL7, &int_enable, 1);
}

void lis2dw12_wakeup_config(lis2dw12_wakeup_config_t wake_config) {

    uint8_t wake_ths_byte = wake_config.sleep_enable << 6 | (wake_config.threshold & 0x3f);
    uint8_t wake_dur_byte = (wake_config.wake_duration & 0x3) << 5 | (wake_config.sleep_duration & 0xf);

    lis2dw12_write_reg(LIS2DW12_WAKE_UP_THS, &wake_ths_byte, 1);
    lis2dw12_write_reg(LIS2DW12_WAKE_UP_DUR, &wake_dur_byte, 1);
}

void lis2dw12_fifo_config(lis2dw12_fifo_config_t config) {
  uint8_t fifo_byte = config.mode << 5 | (config.thresh & 0x1f);

  lis2dw12_write_reg(LIS2DW12_FIFO_CTRL, &fifo_byte, 1);
}

void  lis2dw12_read_full_fifo(int16_t* x, int16_t* y, int16_t* z, lis2dw12_read_full_fifo_callback_t callback) {

    fifo_callback = callback;
    x_buf = x;
    y_buf = y;
    z_buf = z;

    uint8_t readreg = LIS2DW12_OUT_X_L | LIS2DW12_SPI_READ;

    // Use SPI Manager
    /*nrf_spi_mngr_transfer_t const config_transfer[] = {
        NRF_SPI_MNGR_TRANSFER( &readreg, 1, xyz_buf, sizeof(xyz_buf)),
    };

    ret_code_t error = nrf_spi_mngr_perform(spi_instance, &spi_config, config_transfer, 1, NULL);
    APP_ERROR_CHECK(error);

    // As "perform" is blocking, we can now call the callback
    lis2dw12_read_full_fifo_callback(NULL, NULL);*/

    // Use SPI directly
    nrf_drv_spi_uninit(spi_instance);
    nrf_drv_spi_init(spi_instance, &spi_config, lis2dw12_read_full_fifo_callback, NULL);
    ret_code_t error = nrf_drv_spi_transfer(spi_instance, &readreg, 1, xyz_buf, sizeof(xyz_buf));
    APP_ERROR_CHECK(error);
}

//static inline float convert_raw_to_g(int16_t raw) {
//  int32_t divisor = (1 << (8*sizeof(int16_t) - 1));
//  if (raw >= 0) {
//    divisor -= 1;
//  }
//  return (float) raw / (float) divisor * full_scale;
//};

static void lis2dw12_read_full_fifo_callback(nrf_drv_spi_evt_t const* p_event, void* p_context) {

    size_t i, xyz_i = 0;

    for (i = 1; i < 1 + XYZ_DATA_LENGTH; i += 6) {
        x_buf[xyz_i] = /*convert_raw_to_g*/(xyz_buf[i]   | (int16_t) xyz_buf[i+1] << 8);
        y_buf[xyz_i] = /*convert_raw_to_g*/(xyz_buf[i+2] | (int16_t) xyz_buf[i+3] << 8);
        z_buf[xyz_i] = /*convert_raw_to_g*/(xyz_buf[i+4] | (int16_t) xyz_buf[i+5] << 8);
        xyz_i++;
    }

    fifo_callback();
}

void  lis2dw12_reset() {

  // Reset byte in register CTRL2
  uint8_t reset_byte = 1 << 6;

  lis2dw12_write_reg(LIS2DW12_CTRL2, &reset_byte, 1);

  // Check that SoftReset value is reset
  while ( ((reset_byte >> 6) & 0x01) == 1) {

    // Allow Accelerometer time to reset
    nrf_delay_ms(10);

    lis2dw12_read_reg(LIS2DW12_CTRL2, &reset_byte, 1);
  }

}

void lis2dw12_fifo_reset() {

    // To reset FIFO content, Bypass mode should be written and then FIFO mode should be restarted
    lis2dw12_fifo_config_t fifo_config;
    fifo_config.mode = lis2dw12_fifo_bypass;
    lis2dw12_fifo_config(fifo_config);
    fifo_config.mode   = lis2dw12_fifo_continuous;
    fifo_config.thresh = 31;
    lis2dw12_fifo_config(fifo_config);
}

void  lis2dw12_off() {
  lis2dw12_odr_t odr_power_down = lis2dw12_odr_power_down;
  uint8_t off_byte = odr_power_down << 4 | (ctl_config.mode & 0x3) << 2 |
               (ctl_config.lp_mode & 0x3);

  lis2dw12_write_reg(LIS2DW12_CTRL1, &off_byte, 1);
}

void  lis2dw12_on() {
  uint8_t on_byte = ctl_config.odr << 4 | (ctl_config.mode & 0x3) << 2 |
           (ctl_config.lp_mode & 0x3);

  lis2dw12_write_reg(LIS2DW12_CTRL1, &on_byte, 1);
}

lis2dw12_status_t lis2dw12_read_status(void) {
  uint8_t status_byte;
  lis2dw12_read_reg(LIS2DW12_STATUS, &status_byte, 1);

  lis2dw12_status_t status;

  status.fifo_thresh  = (status_byte >> 7) & 0x1;
  status.wakeup       = (status_byte >> 6) & 0x1;
  status.sleep        = (status_byte >> 5) & 0x1;
  status.dbl_tap      = (status_byte >> 4) & 0x1;
  status.sngl_tap     = (status_byte >> 3) & 0x1;
  status._6D          = (status_byte >> 2) & 0x1;
  status.free_fall    = (status_byte >> 1) & 0x1;
  status.data_ready   =  status_byte & 0x1;

  return status;
}
