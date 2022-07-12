// Header inclusions ---------------------------------------------------------------------------------------------------

#include <string.h>
#include "ble_config.h"
#include "imu.h"
#include "nrf_delay.h"
#include "nrf_drv_spi.h"
#include "nrfx_gpiote.h"


#if (BOARD_V < 0x11)  // Accelerometer forwarding for older boards -----------------------------------------------------

#include "accelerometer.h"

bool imu_init(imu_data_callback callback) { return accelerometer_init(callback); }
void imu_handle_incoming_data(uint32_t timestamp) { accelerometer_handle_incoming_data(timestamp); }


#else  // True IMU functionality for newer boards ----------------------------------------------------------------------


// Static IMU state variables ------------------------------------------------------------------------------------------

static nrf_drv_spi_t _spi_instance = NRF_DRV_SPI_INSTANCE(IMU_SPI_BUS_IDX);
static nrf_drv_spi_config_t _spi_config = NRF_DRV_SPI_DEFAULT_CONFIG;
static nrfx_atomic_flag_t _imu_data_ready, _imu_motion_changed;
static uint8_t _lsm6dsox_write_buf[257] = { 0 };
static imu_data_callback _data_callback = NULL;
static float x_data[1], y_data[1], z_data[1];


// LSM6DSOX-specific IMU functionality ---------------------------------------------------------------------------------

static nrfx_err_t lsm6dsox_read_reg(uint8_t reg, uint8_t *data, uint16_t len)
{
   // Use SPI directly
   reg |= LSM6DSOX_SPI_READ;
   nrfx_err_t err_code = nrf_drv_spi_transfer(&_spi_instance, &reg, 1, NULL, 0);
   if (err_code == NRFX_SUCCESS)
      err_code = nrf_drv_spi_transfer(&_spi_instance, NULL, 0, data, len);
   return err_code;
}

static nrfx_err_t lsm6dsox_write_reg(uint8_t reg, uint8_t *data, uint16_t len)
{
   // Use SPI directly
   _lsm6dsox_write_buf[0] = reg & (uint8_t)LSM6DSOX_SPI_WRITE;
   memcpy(_lsm6dsox_write_buf + 1, data, len);
   return nrf_drv_spi_transfer(&_spi_instance, _lsm6dsox_write_buf, len + 1, NULL, 0);
}

static float lsm6dsox_from_fs2_to_mg(int16_t lsb) { return ((float)lsb) * 0.061f; }
static float lsm6dsox_from_fs4_to_mg(int16_t lsb) { return ((float)lsb) * 0.122f; }
static float lsm6dsox_from_fs8_to_mg(int16_t lsb) { return ((float)lsb) * 0.244f; }
static float lsm6dsox_from_fs16_to_mg(int16_t lsb) { return ((float)lsb) * 0.488f; }
static float lsm6dsox_from_fs125_to_mdps(int16_t lsb) { return ((float)lsb) * 4.375f; }
static float lsm6dsox_from_fs500_to_mdps(int16_t lsb) { return ((float)lsb) * 17.50f; }
static float lsm6dsox_from_fs250_to_mdps(int16_t lsb) { return ((float)lsb) * 8.750f; }
static float lsm6dsox_from_fs1000_to_mdps(int16_t lsb) { return ((float)lsb) * 35.0f; }
static float lsm6dsox_from_fs2000_to_mdps(int16_t lsb) { return ((float)lsb) * 70.0f; }
static float lsm6dsox_from_lsb_to_celsius(int16_t lsb) { return (((float)lsb / 256.0f) + 25.0f); }
static float lsm6dsox_from_lsb_to_nsec(int16_t lsb) { return ((float)lsb * 25000.0f); }

static nrfx_err_t lsm6dsox_mem_bank_set(lsm6dsox_reg_access_t val)
{
   lsm6dsox_func_cfg_access_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_FUNC_CFG_ACCESS, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.reg_access = (uint8_t)val;
      ret = lsm6dsox_write_reg(LSM6DSOX_FUNC_CFG_ACCESS, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_mem_bank_get(lsm6dsox_reg_access_t *val)
{
   lsm6dsox_func_cfg_access_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_FUNC_CFG_ACCESS, (uint8_t*)&reg, 1);
   switch (reg.reg_access)
   {
      case LSM6DSOX_USER_BANK:
         *val = LSM6DSOX_USER_BANK;
         break;
      case LSM6DSOX_SENSOR_HUB_BANK:
         *val = LSM6DSOX_SENSOR_HUB_BANK;
         break;
      case LSM6DSOX_EMBEDDED_FUNC_BANK:
         *val = LSM6DSOX_EMBEDDED_FUNC_BANK;
         break;
      default:
         *val = LSM6DSOX_USER_BANK;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_mlc_status_get(lsm6dsox_mlc_status_mainpage_t *val)
{
   return lsm6dsox_read_reg(LSM6DSOX_MLC_STATUS_MAINPAGE, (uint8_t*)val, 1);
}

static nrfx_err_t lsm6dsox_mlc_data_rate_set(lsm6dsox_mlc_odr_t val)
{
   lsm6dsox_emb_func_odr_cfg_c_t reg;
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_EMBEDDED_FUNC_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_EMB_FUNC_ODR_CFG_C, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.mlc_odr = (uint8_t)val;
      ret = lsm6dsox_write_reg(LSM6DSOX_EMB_FUNC_ODR_CFG_C, (uint8_t*)&reg, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   return ret;
}

static nrfx_err_t lsm6dsox_mlc_data_rate_get(lsm6dsox_mlc_odr_t *val)
{
   lsm6dsox_emb_func_odr_cfg_c_t reg;
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_EMBEDDED_FUNC_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_EMB_FUNC_ODR_CFG_C, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      switch (reg.mlc_odr)
      {
         case LSM6DSOX_ODR_PRGS_12Hz5:
            *val = LSM6DSOX_ODR_PRGS_12Hz5;
            break;
         case LSM6DSOX_ODR_PRGS_26Hz:
            *val = LSM6DSOX_ODR_PRGS_26Hz;
            break;
         case LSM6DSOX_ODR_PRGS_52Hz:
            *val = LSM6DSOX_ODR_PRGS_52Hz;
            break;
         case LSM6DSOX_ODR_PRGS_104Hz:
            *val = LSM6DSOX_ODR_PRGS_104Hz;
            break;
         default:
            *val = LSM6DSOX_ODR_PRGS_12Hz5;
            break;
      }
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_sh_read_data_raw_get(lsm6dsox_emb_sh_read_t *val, uint8_t len)
{
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_SENSOR_HUB_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_SENSOR_HUB_1, (uint8_t*)val, len);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   return ret;
}

static nrfx_err_t lsm6dsox_fsm_out_get(lsm6dsox_fsm_out_t *val)
{
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_EMBEDDED_FUNC_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_FSM_OUTS1, (uint8_t*)val, 16);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   return ret;
}

static nrfx_err_t lsm6dsox_fsm_data_rate_set(lsm6dsox_fsm_odr_t val)
{
   lsm6dsox_emb_func_odr_cfg_b_t reg;
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_EMBEDDED_FUNC_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_EMB_FUNC_ODR_CFG_B, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.not_used_01 = 3; /* set default values */
      reg.not_used_02 = 2; /* set default values */
      reg.fsm_odr = (uint8_t)val;
      ret = lsm6dsox_write_reg(LSM6DSOX_EMB_FUNC_ODR_CFG_B, (uint8_t*)&reg, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   return ret;
}

static nrfx_err_t lsm6dsox_fsm_data_rate_get(lsm6dsox_fsm_odr_t *val)
{
   lsm6dsox_emb_func_odr_cfg_b_t reg;
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_EMBEDDED_FUNC_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_EMB_FUNC_ODR_CFG_B, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      switch (reg.fsm_odr)
      {
         case LSM6DSOX_ODR_FSM_12Hz5:
            *val = LSM6DSOX_ODR_FSM_12Hz5;
            break;
         case LSM6DSOX_ODR_FSM_26Hz:
            *val = LSM6DSOX_ODR_FSM_26Hz;
            break;
         case LSM6DSOX_ODR_FSM_52Hz:
            *val = LSM6DSOX_ODR_FSM_52Hz;
            break;
         case LSM6DSOX_ODR_FSM_104Hz:
            *val = LSM6DSOX_ODR_FSM_104Hz;
            break;
         default:
            *val = LSM6DSOX_ODR_FSM_12Hz5;
            break;
      }
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_fsm_init_set(uint8_t val)
{
   lsm6dsox_emb_func_init_b_t reg;
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_EMBEDDED_FUNC_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_EMB_FUNC_INIT_B, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.fsm_init = val;
      ret = lsm6dsox_write_reg(LSM6DSOX_EMB_FUNC_INIT_B, (uint8_t*)&reg, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   return ret;
}

static nrfx_err_t lsm6dsox_fsm_init_get(uint8_t *val)
{
   lsm6dsox_emb_func_init_b_t reg;
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_EMBEDDED_FUNC_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_EMB_FUNC_INIT_B, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      *val = reg.fsm_init;
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_fsm_enable_set(lsm6dsox_emb_fsm_enable_t *val)
{
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_EMBEDDED_FUNC_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_write_reg(LSM6DSOX_FSM_ENABLE_A, (uint8_t*)&val->fsm_enable_a, 1);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_write_reg(LSM6DSOX_FSM_ENABLE_B, (uint8_t*)&val->fsm_enable_b, 1);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   return ret;
}

static nrfx_err_t lsm6dsox_fsm_enable_get(lsm6dsox_emb_fsm_enable_t *val)
{
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_EMBEDDED_FUNC_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_FSM_ENABLE_A, (uint8_t*)val, 2);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   return ret;
}

static nrfx_err_t lsm6dsox_embedded_sens_set(lsm6dsox_emb_sens_t *val)
{
   lsm6dsox_emb_func_en_a_t emb_func_en_a;
   lsm6dsox_emb_func_en_b_t emb_func_en_b;
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_EMBEDDED_FUNC_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_EMB_FUNC_EN_A, (uint8_t*)&emb_func_en_a, 1);
   if (ret == NRFX_SUCCESS)
   {
      ret = lsm6dsox_read_reg(LSM6DSOX_EMB_FUNC_EN_B, (uint8_t*)&emb_func_en_b, 1);
      emb_func_en_b.mlc_en = val->mlc;
      emb_func_en_b.fsm_en = val->fsm;
      emb_func_en_a.tilt_en = val->tilt;
      emb_func_en_a.pedo_en = val->step;
      emb_func_en_a.sign_motion_en = val->sig_mot;
      emb_func_en_b.fifo_compr_en = val->fifo_compr;
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_write_reg(LSM6DSOX_EMB_FUNC_EN_A, (uint8_t*)&emb_func_en_a, 1);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_write_reg(LSM6DSOX_EMB_FUNC_EN_B, (uint8_t*)&emb_func_en_b, 1);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   return ret;
}

static nrfx_err_t lsm6dsox_embedded_sens_get(lsm6dsox_emb_sens_t *emb_sens)
{
   lsm6dsox_emb_func_en_a_t emb_func_en_a;
   lsm6dsox_emb_func_en_b_t emb_func_en_b;
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_EMBEDDED_FUNC_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_EMB_FUNC_EN_A, (uint8_t*)&emb_func_en_a, 1);
   if (ret == NRFX_SUCCESS)
   {
      ret = lsm6dsox_read_reg(LSM6DSOX_EMB_FUNC_EN_B, (uint8_t*)&emb_func_en_b, 1);
      emb_sens->mlc = emb_func_en_b.mlc_en;
      emb_sens->fsm = emb_func_en_b.fsm_en;
      emb_sens->tilt = emb_func_en_a.tilt_en;
      emb_sens->step = emb_func_en_a.pedo_en;
      emb_sens->sig_mot = emb_func_en_a.sign_motion_en;
      emb_sens->fifo_compr = emb_func_en_b.fifo_compr_en;
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   return ret;
}

static nrfx_err_t lsm6dsox_embedded_sens_off(void)
{
   lsm6dsox_emb_func_en_a_t emb_func_en_a;
   lsm6dsox_emb_func_en_b_t emb_func_en_b;
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_EMBEDDED_FUNC_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_EMB_FUNC_EN_A, (uint8_t*)&emb_func_en_a, 1);
   if (ret == NRFX_SUCCESS)
   {
      ret = lsm6dsox_read_reg(LSM6DSOX_EMB_FUNC_EN_B, (uint8_t*)&emb_func_en_b, 1);
      emb_func_en_b.mlc_en = PROPERTY_DISABLE;
      emb_func_en_b.fsm_en = PROPERTY_DISABLE;
      emb_func_en_a.tilt_en = PROPERTY_DISABLE;
      emb_func_en_a.pedo_en = PROPERTY_DISABLE;
      emb_func_en_a.sign_motion_en = PROPERTY_DISABLE;
      emb_func_en_b.fifo_compr_en = PROPERTY_DISABLE;
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_write_reg(LSM6DSOX_EMB_FUNC_EN_A, (uint8_t*)&emb_func_en_a, 1);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_write_reg(LSM6DSOX_EMB_FUNC_EN_B, (uint8_t*)&emb_func_en_b, 1);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   return ret;
}

static nrfx_err_t lsm6dsox_xl_full_scale_set(lsm6dsox_fs_xl_t val)
{
   lsm6dsox_ctrl1_xl_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL1_XL, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.fs_xl = (uint8_t)val;
      ret = lsm6dsox_write_reg(LSM6DSOX_CTRL1_XL, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_xl_full_scale_get(lsm6dsox_fs_xl_t *val)
{
   lsm6dsox_ctrl1_xl_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL1_XL, (uint8_t*)&reg, 1);
   switch (reg.fs_xl)
   {
      case LSM6DSOX_2g:
         *val = LSM6DSOX_2g;
         break;
      case LSM6DSOX_16g:
         *val = LSM6DSOX_16g;
         break;
      case LSM6DSOX_4g:
         *val = LSM6DSOX_4g;
         break;
      case LSM6DSOX_8g:
         *val = LSM6DSOX_8g;
         break;
      default:
         *val = LSM6DSOX_2g;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_xl_data_rate_set(lsm6dsox_odr_xl_t val)
{
   lsm6dsox_odr_xl_t odr_xl = val;
   lsm6dsox_emb_fsm_enable_t fsm_enable;
   lsm6dsox_fsm_odr_t fsm_odr;
   lsm6dsox_emb_sens_t emb_sens;
   lsm6dsox_mlc_odr_t mlc_odr;
   lsm6dsox_ctrl1_xl_t reg;

   // Check the Finite State Machine data rate constraints
   nrfx_err_t ret = lsm6dsox_fsm_enable_get(&fsm_enable);
   if (ret == NRFX_SUCCESS)
   {
      if ((fsm_enable.fsm_enable_a.fsm1_en | fsm_enable.fsm_enable_a.fsm2_en | fsm_enable.fsm_enable_a.fsm3_en | fsm_enable.fsm_enable_a.fsm4_en |
            fsm_enable.fsm_enable_a.fsm5_en | fsm_enable.fsm_enable_a.fsm6_en | fsm_enable.fsm_enable_a.fsm7_en | fsm_enable.fsm_enable_a.fsm8_en |
            fsm_enable.fsm_enable_b.fsm9_en | fsm_enable.fsm_enable_b.fsm10_en | fsm_enable.fsm_enable_b.fsm11_en | fsm_enable.fsm_enable_b.fsm12_en |
            fsm_enable.fsm_enable_b.fsm13_en | fsm_enable.fsm_enable_b.fsm14_en | fsm_enable.fsm_enable_b.fsm15_en | fsm_enable.fsm_enable_b.fsm16_en) == PROPERTY_ENABLE)
      {
         ret = lsm6dsox_fsm_data_rate_get(&fsm_odr);
         if (ret == NRFX_SUCCESS)
         {
            switch (fsm_odr)
            {
               case LSM6DSOX_ODR_FSM_12Hz5:
                  if (val == LSM6DSOX_XL_ODR_OFF)
                     odr_xl = LSM6DSOX_XL_ODR_12Hz5;
                  else
                     odr_xl = val;
                  break;
               case LSM6DSOX_ODR_FSM_26Hz:
                  if (val == LSM6DSOX_XL_ODR_OFF)
                     odr_xl = LSM6DSOX_XL_ODR_26Hz;
                  else if (val == LSM6DSOX_XL_ODR_12Hz5)
                     odr_xl = LSM6DSOX_XL_ODR_26Hz;
                  else
                     odr_xl = val;
                  break;
               case LSM6DSOX_ODR_FSM_52Hz:
                  if (val == LSM6DSOX_XL_ODR_OFF)
                     odr_xl = LSM6DSOX_XL_ODR_52Hz;
                  else if (val == LSM6DSOX_XL_ODR_12Hz5)
                     odr_xl = LSM6DSOX_XL_ODR_52Hz;
                  else if (val == LSM6DSOX_XL_ODR_26Hz)
                     odr_xl = LSM6DSOX_XL_ODR_52Hz;
                  else
                     odr_xl = val;
                  break;
               case LSM6DSOX_ODR_FSM_104Hz:
                  if (val == LSM6DSOX_XL_ODR_OFF)
                     odr_xl = LSM6DSOX_XL_ODR_104Hz;
                  else if (val == LSM6DSOX_XL_ODR_12Hz5)
                     odr_xl = LSM6DSOX_XL_ODR_104Hz;
                  else if (val == LSM6DSOX_XL_ODR_26Hz)
                     odr_xl = LSM6DSOX_XL_ODR_104Hz;
                  else if (val == LSM6DSOX_XL_ODR_52Hz)
                     odr_xl = LSM6DSOX_XL_ODR_104Hz;
                  else
                     odr_xl = val;
                  break;
               default:
                  odr_xl = val;
                  break;
            }
         }
      }
   }

   // Check the Machine Learning Core data rate constraints
   emb_sens.mlc = PROPERTY_DISABLE;
   if (ret == NRFX_SUCCESS)
   {
      lsm6dsox_embedded_sens_get(&emb_sens);
      if ( emb_sens.mlc == PROPERTY_ENABLE )
      {
         ret = lsm6dsox_mlc_data_rate_get(&mlc_odr);
         if (ret == NRFX_SUCCESS)
         {
            switch (mlc_odr)
            {
               case LSM6DSOX_ODR_PRGS_12Hz5:
                  if (val == LSM6DSOX_XL_ODR_OFF)
                     odr_xl = LSM6DSOX_XL_ODR_12Hz5;
                  else
                     odr_xl = val;
                  break;
               case LSM6DSOX_ODR_PRGS_26Hz:
                  if (val == LSM6DSOX_XL_ODR_OFF)
                     odr_xl = LSM6DSOX_XL_ODR_26Hz;
                  else if (val == LSM6DSOX_XL_ODR_12Hz5)
                     odr_xl = LSM6DSOX_XL_ODR_26Hz;
                  else
                     odr_xl = val;
                  break;
               case LSM6DSOX_ODR_PRGS_52Hz:
                  if (val == LSM6DSOX_XL_ODR_OFF)
                     odr_xl = LSM6DSOX_XL_ODR_52Hz;
                  else if (val == LSM6DSOX_XL_ODR_12Hz5)
                     odr_xl = LSM6DSOX_XL_ODR_52Hz;
                  else if (val == LSM6DSOX_XL_ODR_26Hz)
                     odr_xl = LSM6DSOX_XL_ODR_52Hz;
                  else
                     odr_xl = val;
                  break;
               case LSM6DSOX_ODR_PRGS_104Hz:
                  if (val == LSM6DSOX_XL_ODR_OFF)
                     odr_xl = LSM6DSOX_XL_ODR_104Hz;
                  else if (val == LSM6DSOX_XL_ODR_12Hz5)
                     odr_xl = LSM6DSOX_XL_ODR_104Hz;
                  else if (val == LSM6DSOX_XL_ODR_26Hz)
                     odr_xl = LSM6DSOX_XL_ODR_104Hz;
                  else if (val == LSM6DSOX_XL_ODR_52Hz)
                     odr_xl = LSM6DSOX_XL_ODR_104Hz;
                  else
                     odr_xl = val;
                  break;
               default:
                  odr_xl = val;
                  break;
            }
         }
      }
   }

   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_CTRL1_XL, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.odr_xl = (uint8_t) odr_xl;
      ret = lsm6dsox_write_reg(LSM6DSOX_CTRL1_XL, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_xl_data_rate_get(lsm6dsox_odr_xl_t *val)
{
   lsm6dsox_ctrl1_xl_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL1_XL, (uint8_t*)&reg, 1);
   switch (reg.odr_xl)
   {
      case LSM6DSOX_XL_ODR_OFF:
         *val = LSM6DSOX_XL_ODR_OFF;
         break;
      case LSM6DSOX_XL_ODR_12Hz5:
         *val = LSM6DSOX_XL_ODR_12Hz5;
         break;
      case LSM6DSOX_XL_ODR_26Hz:
         *val = LSM6DSOX_XL_ODR_26Hz;
         break;
      case LSM6DSOX_XL_ODR_52Hz:
         *val = LSM6DSOX_XL_ODR_52Hz;
         break;
      case LSM6DSOX_XL_ODR_104Hz:
         *val = LSM6DSOX_XL_ODR_104Hz;
         break;
      case LSM6DSOX_XL_ODR_208Hz:
         *val = LSM6DSOX_XL_ODR_208Hz;
         break;
      case LSM6DSOX_XL_ODR_417Hz:
         *val = LSM6DSOX_XL_ODR_417Hz;
         break;
      case LSM6DSOX_XL_ODR_833Hz:
         *val = LSM6DSOX_XL_ODR_833Hz;
         break;
      case LSM6DSOX_XL_ODR_1667Hz:
         *val = LSM6DSOX_XL_ODR_1667Hz;
         break;
      case LSM6DSOX_XL_ODR_3333Hz:
         *val = LSM6DSOX_XL_ODR_3333Hz;
         break;
      case LSM6DSOX_XL_ODR_6667Hz:
         *val = LSM6DSOX_XL_ODR_6667Hz;
         break;
      case LSM6DSOX_XL_ODR_1Hz6:
         *val = LSM6DSOX_XL_ODR_1Hz6;
         break;
      default:
         *val = LSM6DSOX_XL_ODR_OFF;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_gy_full_scale_set(lsm6dsox_fs_g_t val)
{
   lsm6dsox_ctrl2_g_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL2_G, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.fs_g = (uint8_t) val;
      ret = lsm6dsox_write_reg(LSM6DSOX_CTRL2_G, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_gy_full_scale_get(lsm6dsox_fs_g_t *val)
{
   lsm6dsox_ctrl2_g_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL2_G, (uint8_t*)&reg, 1);

   switch (reg.fs_g) {
      case LSM6DSOX_250dps:
         *val = LSM6DSOX_250dps;
         break;
      case LSM6DSOX_125dps:
         *val = LSM6DSOX_125dps;
         break;
      case LSM6DSOX_500dps:
         *val = LSM6DSOX_500dps;
         break;
      case LSM6DSOX_1000dps:
         *val = LSM6DSOX_1000dps;
         break;
      case LSM6DSOX_2000dps:
         *val = LSM6DSOX_2000dps;
         break;
      default:
         *val = LSM6DSOX_250dps;
         break;
   }

   return ret;
}

static nrfx_err_t lsm6dsox_gy_data_rate_set(lsm6dsox_odr_g_t val)
{
   lsm6dsox_odr_g_t odr_gy = val;
   lsm6dsox_emb_fsm_enable_t fsm_enable;
   lsm6dsox_fsm_odr_t fsm_odr;
   lsm6dsox_emb_sens_t emb_sens;
   lsm6dsox_mlc_odr_t mlc_odr;
   lsm6dsox_ctrl2_g_t reg;

   // Check the Finite State Machine data rate constraints
   nrfx_err_t ret = lsm6dsox_fsm_enable_get(&fsm_enable);
   if (ret == NRFX_SUCCESS)
   {
      if ((fsm_enable.fsm_enable_a.fsm1_en | fsm_enable.fsm_enable_a.fsm2_en | fsm_enable.fsm_enable_a.fsm3_en | fsm_enable.fsm_enable_a.fsm4_en |
            fsm_enable.fsm_enable_a.fsm5_en | fsm_enable.fsm_enable_a.fsm6_en | fsm_enable.fsm_enable_a.fsm7_en | fsm_enable.fsm_enable_a.fsm8_en |
            fsm_enable.fsm_enable_b.fsm9_en | fsm_enable.fsm_enable_b.fsm10_en | fsm_enable.fsm_enable_b.fsm11_en | fsm_enable.fsm_enable_b.fsm12_en |
            fsm_enable.fsm_enable_b.fsm13_en | fsm_enable.fsm_enable_b.fsm14_en | fsm_enable.fsm_enable_b.fsm15_en | fsm_enable.fsm_enable_b.fsm16_en) == PROPERTY_ENABLE)
      {
         ret = lsm6dsox_fsm_data_rate_get(&fsm_odr);
         if (ret == NRFX_SUCCESS)
         {
            switch (fsm_odr)
            {
               case LSM6DSOX_ODR_FSM_12Hz5:
                  if (val == LSM6DSOX_GY_ODR_OFF)
                     odr_gy = LSM6DSOX_GY_ODR_12Hz5;
                  else
                     odr_gy = val;
                  break;
               case LSM6DSOX_ODR_FSM_26Hz:
                  if (val == LSM6DSOX_GY_ODR_OFF)
                     odr_gy = LSM6DSOX_GY_ODR_26Hz;
                  else if (val == LSM6DSOX_GY_ODR_12Hz5)
                     odr_gy = LSM6DSOX_GY_ODR_26Hz;
                  else
                     odr_gy = val;
                  break;
               case LSM6DSOX_ODR_FSM_52Hz:
                  if (val == LSM6DSOX_GY_ODR_OFF)
                     odr_gy = LSM6DSOX_GY_ODR_52Hz;
                  else if (val == LSM6DSOX_GY_ODR_12Hz5)
                     odr_gy = LSM6DSOX_GY_ODR_52Hz;
                  else if (val == LSM6DSOX_GY_ODR_26Hz)
                     odr_gy = LSM6DSOX_GY_ODR_52Hz;
                  else
                     odr_gy = val;
                  break;
               case LSM6DSOX_ODR_FSM_104Hz:
                  if (val == LSM6DSOX_GY_ODR_OFF)
                     odr_gy = LSM6DSOX_GY_ODR_104Hz;
                  else if (val == LSM6DSOX_GY_ODR_12Hz5)
                     odr_gy = LSM6DSOX_GY_ODR_104Hz;
                  else if (val == LSM6DSOX_GY_ODR_26Hz)
                     odr_gy = LSM6DSOX_GY_ODR_104Hz;
                  else if (val == LSM6DSOX_GY_ODR_52Hz)
                     odr_gy = LSM6DSOX_GY_ODR_104Hz;
                  else
                     odr_gy = val;
                  break;
               default:
                  odr_gy = val;
                  break;
            }
         }
      }
   }

   // Check the Machine Learning Core data rate constraints
   emb_sens.mlc = PROPERTY_DISABLE;
   if (ret == NRFX_SUCCESS)
   {
      ret = lsm6dsox_embedded_sens_get(&emb_sens);
      if ( emb_sens.mlc == PROPERTY_ENABLE )
      {
         ret = lsm6dsox_mlc_data_rate_get(&mlc_odr);
         if (ret == NRFX_SUCCESS)
         {
            switch (mlc_odr)
            {
               case LSM6DSOX_ODR_PRGS_12Hz5:
                  if (val == LSM6DSOX_GY_ODR_OFF)
                     odr_gy = LSM6DSOX_GY_ODR_12Hz5;
                  else
                     odr_gy = val;
                  break;
               case LSM6DSOX_ODR_PRGS_26Hz:
                  if (val == LSM6DSOX_GY_ODR_OFF)
                     odr_gy = LSM6DSOX_GY_ODR_26Hz;
                  else if (val == LSM6DSOX_GY_ODR_12Hz5)
                     odr_gy = LSM6DSOX_GY_ODR_26Hz;
                  else
                     odr_gy = val;
                  break;
               case LSM6DSOX_ODR_PRGS_52Hz:
                  if (val == LSM6DSOX_GY_ODR_OFF)
                     odr_gy = LSM6DSOX_GY_ODR_52Hz;
                  else if (val == LSM6DSOX_GY_ODR_12Hz5)
                     odr_gy = LSM6DSOX_GY_ODR_52Hz;
                  else if (val == LSM6DSOX_GY_ODR_26Hz)
                     odr_gy = LSM6DSOX_GY_ODR_52Hz;
                  else
                     odr_gy = val;
                  break;
               case LSM6DSOX_ODR_PRGS_104Hz:
                  if (val == LSM6DSOX_GY_ODR_OFF)
                     odr_gy = LSM6DSOX_GY_ODR_104Hz;
                  else if (val == LSM6DSOX_GY_ODR_12Hz5)
                     odr_gy = LSM6DSOX_GY_ODR_104Hz;
                  else if (val == LSM6DSOX_GY_ODR_26Hz)
                     odr_gy = LSM6DSOX_GY_ODR_104Hz;
                  else if (val == LSM6DSOX_GY_ODR_52Hz)
                     odr_gy = LSM6DSOX_GY_ODR_104Hz;
                  else
                     odr_gy = val;
                  break;
               default:
                  odr_gy = val;
                  break;
            }
         }
      }
   }

   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_CTRL2_G, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.odr_g = (uint8_t) odr_gy;
      ret = lsm6dsox_write_reg(LSM6DSOX_CTRL2_G, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_gy_data_rate_get(lsm6dsox_odr_g_t *val)
{
   lsm6dsox_ctrl2_g_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL2_G, (uint8_t*)&reg, 1);
   switch (reg.odr_g)
   {
      case LSM6DSOX_GY_ODR_OFF:
         *val = LSM6DSOX_GY_ODR_OFF;
         break;
      case LSM6DSOX_GY_ODR_12Hz5:
         *val = LSM6DSOX_GY_ODR_12Hz5;
         break;
      case LSM6DSOX_GY_ODR_26Hz:
         *val = LSM6DSOX_GY_ODR_26Hz;
         break;
      case LSM6DSOX_GY_ODR_52Hz:
         *val = LSM6DSOX_GY_ODR_52Hz;
         break;
      case LSM6DSOX_GY_ODR_104Hz:
         *val = LSM6DSOX_GY_ODR_104Hz;
         break;
      case LSM6DSOX_GY_ODR_208Hz:
         *val = LSM6DSOX_GY_ODR_208Hz;
         break;
      case LSM6DSOX_GY_ODR_417Hz:
         *val = LSM6DSOX_GY_ODR_417Hz;
         break;
      case LSM6DSOX_GY_ODR_833Hz:
         *val = LSM6DSOX_GY_ODR_833Hz;
         break;
      case LSM6DSOX_GY_ODR_1667Hz:
         *val = LSM6DSOX_GY_ODR_1667Hz;
         break;
      case LSM6DSOX_GY_ODR_3333Hz:
         *val = LSM6DSOX_GY_ODR_3333Hz;
         break;
      case LSM6DSOX_GY_ODR_6667Hz:
         *val = LSM6DSOX_GY_ODR_6667Hz;
         break;
      default:
         *val = LSM6DSOX_GY_ODR_OFF;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_block_data_update_set(uint8_t val)
{
   lsm6dsox_ctrl3_c_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL3_C, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.bdu = val;
      ret = lsm6dsox_write_reg(LSM6DSOX_CTRL3_C, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_block_data_update_get(uint8_t *val)
{
   lsm6dsox_ctrl3_c_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL3_C, (uint8_t*)&reg, 1);
   *val = reg.bdu;
   return ret;
}

static nrfx_err_t lsm6dsox_xl_offset_weight_set(lsm6dsox_usr_off_w_t val)
{
   lsm6dsox_ctrl6_c_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL6_C, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.usr_off_w = (uint8_t)val;
      ret = lsm6dsox_write_reg(LSM6DSOX_CTRL6_C, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_xl_offset_weight_get(lsm6dsox_usr_off_w_t *val)
{
   lsm6dsox_ctrl6_c_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL6_C, (uint8_t*)&reg, 1);
   switch (reg.usr_off_w)
   {
      case LSM6DSOX_LSb_1mg:
         *val = LSM6DSOX_LSb_1mg;
         break;
      case LSM6DSOX_LSb_16mg:
         *val = LSM6DSOX_LSb_16mg;
         break;
      default:
         *val = LSM6DSOX_LSb_1mg;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_xl_power_mode_set(lsm6dsox_xl_hm_mode_t val)
{
   lsm6dsox_ctrl5_c_t ctrl5_c;
   lsm6dsox_ctrl6_c_t ctrl6_c;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL5_C, (uint8_t*)&ctrl5_c, 1);
   if (ret == NRFX_SUCCESS)
   {
      ctrl5_c.xl_ulp_en = ((uint8_t)val & 0x02U) >> 1;
      ret = lsm6dsox_write_reg(LSM6DSOX_CTRL5_C, (uint8_t*)&ctrl5_c, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_CTRL6_C, (uint8_t*)&ctrl6_c, 1);
   if (ret == NRFX_SUCCESS)
   {
      ctrl6_c.xl_hm_mode = (uint8_t)val & 0x01U;
      ret = lsm6dsox_write_reg(LSM6DSOX_CTRL6_C, (uint8_t*)&ctrl6_c, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_xl_power_mode_get(lsm6dsox_xl_hm_mode_t *val)
{
   lsm6dsox_ctrl5_c_t ctrl5_c;
   lsm6dsox_ctrl6_c_t ctrl6_c;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL5_C, (uint8_t*)&ctrl5_c, 1);
   if (ret == NRFX_SUCCESS)
   {
      ret = lsm6dsox_read_reg(LSM6DSOX_CTRL6_C, (uint8_t*)&ctrl6_c, 1);
      switch ((ctrl5_c.xl_ulp_en << 1) | ctrl6_c.xl_hm_mode)
      {
         case LSM6DSOX_HIGH_PERFORMANCE_MD:
            *val = LSM6DSOX_HIGH_PERFORMANCE_MD;
            break;
         case LSM6DSOX_LOW_NORMAL_POWER_MD:
            *val = LSM6DSOX_LOW_NORMAL_POWER_MD;
            break;
         case LSM6DSOX_ULTRA_LOW_POWER_MD:
            *val = LSM6DSOX_ULTRA_LOW_POWER_MD;
            break;
         default:
            *val = LSM6DSOX_HIGH_PERFORMANCE_MD;
            break;
      }
   }
   return ret;
}

static nrfx_err_t lsm6dsox_gy_power_mode_set(lsm6dsox_g_hm_mode_t val)
{
   lsm6dsox_ctrl7_g_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL7_G, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.g_hm_mode = (uint8_t)val;
      ret = lsm6dsox_write_reg(LSM6DSOX_CTRL7_G, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_gy_power_mode_get(lsm6dsox_g_hm_mode_t *val)
{
   lsm6dsox_ctrl7_g_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL7_G, (uint8_t*)&reg, 1);
   switch (reg.g_hm_mode)
   {
      case LSM6DSOX_GY_HIGH_PERFORMANCE:
         *val = LSM6DSOX_GY_HIGH_PERFORMANCE;
         break;
      case LSM6DSOX_GY_NORMAL:
         *val = LSM6DSOX_GY_NORMAL;
         break;
      default:
         *val = LSM6DSOX_GY_HIGH_PERFORMANCE;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_status_reg_get(lsm6dsox_status_reg_t *val)
{
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_STATUS_REG, (uint8_t*)val, 1);
   return ret;
}

static nrfx_err_t lsm6dsox_xl_flag_data_ready_get(uint8_t *val)
{
   lsm6dsox_status_reg_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_STATUS_REG, (uint8_t*)&reg, 1);
   *val = reg.xlda;
   return ret;
}

static nrfx_err_t lsm6dsox_gy_flag_data_ready_get(uint8_t *val)
{
   lsm6dsox_status_reg_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_STATUS_REG, (uint8_t*)&reg, 1);
   *val = reg.gda;
   return ret;
}

static nrfx_err_t lsm6dsox_temp_flag_data_ready_get(uint8_t *val)
{
   lsm6dsox_status_reg_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_STATUS_REG, (uint8_t*)&reg, 1);
   *val = reg.tda;
   return ret;
}

static nrfx_err_t lsm6dsox_xl_usr_offset_x_set(uint8_t *buff)
{
   nrfx_err_t ret = lsm6dsox_write_reg(LSM6DSOX_X_OFS_USR, buff, 1);
   return ret;
}

static nrfx_err_t lsm6dsox_xl_usr_offset_x_get(uint8_t *buff)
{
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_X_OFS_USR, buff, 1);
   return ret;
}

static nrfx_err_t lsm6dsox_xl_usr_offset_y_set(uint8_t *buff)
{
   nrfx_err_t ret = lsm6dsox_write_reg(LSM6DSOX_Y_OFS_USR, buff, 1);
   return ret;
}

static nrfx_err_t lsm6dsox_xl_usr_offset_y_get(uint8_t *buff)
{
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_Y_OFS_USR, buff, 1);
   return ret;
}

static nrfx_err_t lsm6dsox_xl_usr_offset_z_set(uint8_t *buff)
{
   nrfx_err_t ret = lsm6dsox_write_reg(LSM6DSOX_Z_OFS_USR, buff, 1);
   return ret;
}

static nrfx_err_t lsm6dsox_xl_usr_offset_z_get(uint8_t *buff)
{
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_Z_OFS_USR, buff, 1);
   return ret;
}

static nrfx_err_t lsm6dsox_xl_usr_offset_set(uint8_t val)
{
   lsm6dsox_ctrl7_g_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL7_G, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.usr_off_on_out = val;
      ret = lsm6dsox_write_reg(LSM6DSOX_CTRL7_G, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_xl_usr_offset_get(uint8_t *val)
{
   lsm6dsox_ctrl7_g_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL7_G, (uint8_t*)&reg, 1);
   *val = reg.usr_off_on_out;
   return ret;
}

static nrfx_err_t lsm6dsox_timestamp_rst(void)
{
   uint8_t rst_val = 0xAA;
   return lsm6dsox_write_reg(LSM6DSOX_TIMESTAMP2, &rst_val, 1);
}

static nrfx_err_t lsm6dsox_timestamp_set(uint8_t val)
{
   lsm6dsox_ctrl10_c_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL10_C, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.timestamp_en = val;
      ret = lsm6dsox_write_reg(LSM6DSOX_CTRL10_C, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_timestamp_get(uint8_t *val)
{
   lsm6dsox_ctrl10_c_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL10_C, (uint8_t*)&reg, 1);
   *val = reg.timestamp_en;
   return ret;
}

static nrfx_err_t lsm6dsox_timestamp_raw_get(uint32_t *val)
{
   uint8_t buff[4];
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_TIMESTAMP0, buff, 4);
   *val = buff[3];
   *val = (*val * 256U) +  buff[2];
   *val = (*val * 256U) +  buff[1];
   *val = (*val * 256U) +  buff[0];
   return ret;
}

static nrfx_err_t lsm6dsox_rounding_mode_set(lsm6dsox_rounding_t val)
{
   lsm6dsox_ctrl5_c_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL5_C, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.rounding = (uint8_t)val;
      ret = lsm6dsox_write_reg(LSM6DSOX_CTRL5_C, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_rounding_mode_get(lsm6dsox_rounding_t *val)
{
   lsm6dsox_ctrl5_c_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL5_C, (uint8_t*)&reg, 1);
   switch (reg.rounding)
   {
      case LSM6DSOX_NO_ROUND:
         *val = LSM6DSOX_NO_ROUND;
         break;
      case LSM6DSOX_ROUND_XL:
         *val = LSM6DSOX_ROUND_XL;
         break;
      case LSM6DSOX_ROUND_GY:
         *val = LSM6DSOX_ROUND_GY;
         break;
      case LSM6DSOX_ROUND_GY_XL:
         *val = LSM6DSOX_ROUND_GY_XL;
         break;
      default:
         *val = LSM6DSOX_NO_ROUND;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_rounding_on_status_set(lsm6dsox_rounding_status_t val)
{
   lsm6dsox_ctrl5_c_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL5_C, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.rounding_status = (uint8_t)val;
      ret = lsm6dsox_write_reg(LSM6DSOX_CTRL5_C, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_rounding_on_status_get(lsm6dsox_rounding_status_t *val)
{
   lsm6dsox_ctrl5_c_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL5_C, (uint8_t*)&reg, 1);
   switch (reg.rounding_status)
   {
      case LSM6DSOX_STAT_RND_DISABLE:
         *val = LSM6DSOX_STAT_RND_DISABLE;
         break;
      case LSM6DSOX_STAT_RND_ENABLE:
         *val = LSM6DSOX_STAT_RND_ENABLE;
         break;
      default:
         *val = LSM6DSOX_STAT_RND_DISABLE;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_temperature_raw_get(int16_t *val)
{
   uint8_t buff[2];
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_OUT_TEMP_L, buff, 2);
   *val = (int16_t)buff[1];
   *val = (*val * 256) + (int16_t)buff[0];
   return ret;
}

static nrfx_err_t lsm6dsox_angular_rate_raw_get(int16_t *val)
{
   uint8_t buff[6];
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_OUTX_L_G, buff, 6);
   val[0] = (int16_t)buff[1];
   val[0] = (val[0] * 256) + (int16_t)buff[0];
   val[1] = (int16_t)buff[3];
   val[1] = (val[1] * 256) + (int16_t)buff[2];
   val[2] = (int16_t)buff[5];
   val[2] = (val[2] * 256) + (int16_t)buff[4];
   return ret;
}

static nrfx_err_t lsm6dsox_acceleration_raw_get(int16_t *val)
{
   uint8_t buff[6];
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_OUTX_L_A, buff, 6);
   val[0] = (int16_t)buff[1];
   val[0] = (val[0] * 256) + (int16_t)buff[0];
   val[1] = (int16_t)buff[3];
   val[1] = (val[1] * 256) + (int16_t)buff[2];
   val[2] = (int16_t)buff[5];
   val[2] = (val[2] * 256) + (int16_t)buff[4];
   return ret;
}

static nrfx_err_t lsm6dsox_fifo_out_raw_get(uint8_t *buff)
{
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_FIFO_DATA_OUT_X_L, buff, 6);
   return ret;
}

static nrfx_err_t lsm6dsox_ois_angular_rate_raw_get(int16_t *val)
{
   uint8_t buff[6];
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_UI_OUTX_L_G_OIS, buff, 6);
   val[0] = (int16_t)buff[1];
   val[0] = (val[0] * 256) + (int16_t)buff[0];
   val[1] = (int16_t)buff[3];
   val[1] = (val[1] * 256) + (int16_t)buff[2];
   val[2] = (int16_t)buff[5];
   val[2] = (val[2] * 256) + (int16_t)buff[4];
   return ret;
}

static nrfx_err_t lsm6dsox_ois_acceleration_raw_get(int16_t *val)
{
   uint8_t buff[6];
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_UI_OUTX_L_A_OIS, buff, 6);
   val[0] = (int16_t)buff[1];
   val[0] = (val[0] * 256) + (int16_t)buff[0];
   val[1] = (int16_t)buff[3];
   val[1] = (val[1] * 256) + (int16_t)buff[2];
   val[2] = (int16_t)buff[5];
   val[2] = (val[2] * 256) + (int16_t)buff[4];
   return ret;
}

static nrfx_err_t lsm6dsox_aux_temperature_raw_get(int16_t *val)
{
   uint8_t buff[2];
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_SPI2_OUT_TEMP_L, buff, 2);
   *val = (int16_t)buff[1];
   *val = (*val * 256) + (int16_t)buff[0];
   return ret;
}

static nrfx_err_t lsm6dsox_aux_ois_angular_rate_raw_get(int16_t *val)
{
   uint8_t buff[6];
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_SPI2_OUTX_L_G_OIS, buff, 6);
   val[0] = (int16_t)buff[1];
   val[0] = (val[0] * 256) + (int16_t)buff[0];
   val[1] = (int16_t)buff[3];
   val[1] = (val[1] * 256) + (int16_t)buff[2];
   val[2] = (int16_t)buff[5];
   val[2] = (val[2] * 256) + (int16_t)buff[4];
   return ret;
}

static nrfx_err_t lsm6dsox_aux_ois_acceleration_raw_get(int16_t *val)
{
   uint8_t buff[6];
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_SPI2_OUTX_L_A_OIS, buff, 6);
   val[0] = (int16_t)buff[1];
   val[0] = (val[0] * 256) + (int16_t)buff[0];
   val[1] = (int16_t)buff[3];
   val[1] = (val[1] * 256) + (int16_t)buff[2];
   val[2] = (int16_t)buff[5];
   val[2] = (val[2] * 256) + (int16_t)buff[4];
   return ret;
}

static nrfx_err_t lsm6dsox_number_of_steps_get(uint16_t *val)
{
   uint8_t buff[2];
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_EMBEDDED_FUNC_BANK);
   if (ret == NRFX_SUCCESS)
   {
      ret = lsm6dsox_read_reg(LSM6DSOX_STEP_COUNTER_L, buff, 2);
      *val = buff[1];
      *val = (*val * 256U) +  buff[0];
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   return ret;
}

static nrfx_err_t lsm6dsox_steps_reset(void)
{
   lsm6dsox_emb_func_src_t reg;
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_EMBEDDED_FUNC_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_EMB_FUNC_SRC, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.pedo_rst_step = PROPERTY_ENABLE;
      ret = lsm6dsox_write_reg(LSM6DSOX_EMB_FUNC_SRC, (uint8_t*)&reg, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   return ret;
}

static nrfx_err_t lsm6dsox_mlc_out_get(uint8_t *buff)
{
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_EMBEDDED_FUNC_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_MLC0_SRC, buff, 8);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   return ret;
}

static nrfx_err_t lsm6dsox_odr_cal_reg_set(uint8_t val)
{
   lsm6dsox_internal_freq_fine_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_INTERNAL_FREQ_FINE, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.freq_fine = val;
      ret = lsm6dsox_write_reg(LSM6DSOX_INTERNAL_FREQ_FINE, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_odr_cal_reg_get(uint8_t *val)
{
   lsm6dsox_internal_freq_fine_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_INTERNAL_FREQ_FINE, (uint8_t*)&reg, 1);
   *val = reg.freq_fine;
   return ret;
}

static nrfx_err_t lsm6dsox_ln_pg_write_byte(uint16_t address, uint8_t *val)
{
   lsm6dsox_page_rw_t page_rw;
   lsm6dsox_page_sel_t page_sel;
   lsm6dsox_page_address_t page_address;
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_EMBEDDED_FUNC_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_PAGE_RW, (uint8_t*)&page_rw, 1);
   if (ret == NRFX_SUCCESS)
   {
      page_rw.page_rw = 0x02; /* page_write enable */
      ret = lsm6dsox_write_reg(LSM6DSOX_PAGE_RW, (uint8_t*)&page_rw, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_PAGE_SEL, (uint8_t*)&page_sel, 1);
   if (ret == NRFX_SUCCESS)
   {
      page_sel.page_sel = ((uint8_t)(address >> 8) & 0x0FU);
      page_sel.not_used_01 = 1;
      ret = lsm6dsox_write_reg(LSM6DSOX_PAGE_SEL, (uint8_t*)&page_sel, 1);
   }
   if (ret == NRFX_SUCCESS)
   {
      page_address.page_addr = (uint8_t)address & 0xFFU;
      ret = lsm6dsox_write_reg(LSM6DSOX_PAGE_ADDRESS, (uint8_t*)&page_address, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_write_reg(LSM6DSOX_PAGE_VALUE, val, 1);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_PAGE_RW, (uint8_t*)&page_rw, 1);
   if (ret == NRFX_SUCCESS)
   {
      page_rw.page_rw = 0x00; /* page_write disable */
      ret = lsm6dsox_write_reg(LSM6DSOX_PAGE_RW, (uint8_t*)&page_rw, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   return ret;
}

static nrfx_err_t lsm6dsox_ln_pg_write(uint16_t address, uint8_t *buf, uint8_t len)
{
   lsm6dsox_page_rw_t page_rw;
   lsm6dsox_page_sel_t page_sel;
   lsm6dsox_page_address_t  page_address;
   uint8_t msb, lsb;
   uint8_t i ;
   msb = ((uint8_t)(address >> 8) & 0x0FU);
   lsb = (uint8_t)address & 0xFFU;
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_EMBEDDED_FUNC_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_PAGE_RW, (uint8_t*)&page_rw, 1);
   if (ret == NRFX_SUCCESS)
   {
      page_rw.page_rw = 0x02; /* page_write enable*/
      ret = lsm6dsox_write_reg(LSM6DSOX_PAGE_RW, (uint8_t*)&page_rw, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_PAGE_SEL, (uint8_t*)&page_sel, 1);
   if (ret == NRFX_SUCCESS)
   {
      page_sel.page_sel = msb;
      page_sel.not_used_01 = 1;
      ret = lsm6dsox_write_reg(LSM6DSOX_PAGE_SEL, (uint8_t*)&page_sel, 1);
   }
   if (ret == NRFX_SUCCESS)
   {
      page_address.page_addr = lsb;
      ret = lsm6dsox_write_reg(LSM6DSOX_PAGE_ADDRESS, (uint8_t*)&page_address, 1);
   }
   if (ret == NRFX_SUCCESS)
   {
      for (i = 0; ((i < len) && (ret == NRFX_SUCCESS)); i++)
      {
         ret = lsm6dsox_write_reg(LSM6DSOX_PAGE_VALUE, &buf[i], 1);
         lsb++;

         /* Check if page wrap */
         if ((lsb == 0x00U) && (ret == NRFX_SUCCESS))
         {
            msb++;
            ret = lsm6dsox_read_reg(LSM6DSOX_PAGE_SEL, (uint8_t*)&page_sel, 1);
            if (ret == NRFX_SUCCESS)
            {
               page_sel.page_sel = msb;
               page_sel.not_used_01 = 1;
               ret = lsm6dsox_write_reg(LSM6DSOX_PAGE_SEL, (uint8_t*)&page_sel, 1);
            }
         }
      }
   }

   page_sel.page_sel = 0;
   page_sel.not_used_01 = 1;
   ret = lsm6dsox_write_reg(LSM6DSOX_PAGE_SEL, (uint8_t*)&page_sel, 1);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_PAGE_RW, (uint8_t*)&page_rw, 1);
   if (ret == NRFX_SUCCESS)
   {
      page_rw.page_rw = 0x00; /* page_write disable */
      ret = lsm6dsox_write_reg(LSM6DSOX_PAGE_RW, (uint8_t*)&page_rw, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   return ret;
}

static nrfx_err_t lsm6dsox_ln_pg_read_byte(uint16_t address, uint8_t *val)
{
   lsm6dsox_page_rw_t page_rw;
   lsm6dsox_page_sel_t page_sel;
   lsm6dsox_page_address_t  page_address;
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_EMBEDDED_FUNC_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_PAGE_RW, (uint8_t*)&page_rw, 1);
   if (ret == NRFX_SUCCESS)
   {
      page_rw.page_rw = 0x01; /* page_read enable*/
      ret = lsm6dsox_write_reg(LSM6DSOX_PAGE_RW, (uint8_t*)&page_rw, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_PAGE_SEL, (uint8_t*)&page_sel, 1);
   if (ret == NRFX_SUCCESS)
   {
      page_sel.page_sel = ((uint8_t)(address >> 8) & 0x0FU);
      page_sel.not_used_01 = 1;
      ret = lsm6dsox_write_reg(LSM6DSOX_PAGE_SEL, (uint8_t*)&page_sel, 1);
   }
   if (ret == NRFX_SUCCESS)
   {
      page_address.page_addr = (uint8_t)address & 0x00FFU;
      ret = lsm6dsox_write_reg(LSM6DSOX_PAGE_ADDRESS, (uint8_t*)&page_address, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_PAGE_VALUE, val, 1);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_PAGE_RW, (uint8_t*)&page_rw, 1);
   if (ret == NRFX_SUCCESS)
   {
      page_rw.page_rw = 0x00; /* page_read disable */
      ret = lsm6dsox_write_reg(LSM6DSOX_PAGE_RW, (uint8_t*)&page_rw, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   return ret;
}

static nrfx_err_t lsm6dsox_data_ready_mode_set(lsm6dsox_dataready_pulsed_t val)
{
   lsm6dsox_counter_bdr_reg1_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_COUNTER_BDR_REG1, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.dataready_pulsed = (uint8_t)val;
      ret = lsm6dsox_write_reg(LSM6DSOX_COUNTER_BDR_REG1, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_data_ready_mode_get(lsm6dsox_dataready_pulsed_t *val)
{
   lsm6dsox_counter_bdr_reg1_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_COUNTER_BDR_REG1, (uint8_t*)&reg, 1);
   switch (reg.dataready_pulsed)
   {
      case LSM6DSOX_DRDY_LATCHED:
         *val = LSM6DSOX_DRDY_LATCHED;
         break;
      case LSM6DSOX_DRDY_PULSED:
         *val = LSM6DSOX_DRDY_PULSED;
         break;
      default:
         *val = LSM6DSOX_DRDY_LATCHED;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_device_id_get(uint8_t *buff)
{
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_WHO_AM_I, buff, 1);
   return ret;
}

static nrfx_err_t lsm6dsox_reset_set(uint8_t val)
{
   lsm6dsox_ctrl3_c_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL3_C, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.sw_reset = val;
      ret = lsm6dsox_write_reg(LSM6DSOX_CTRL3_C, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_reset_get(uint8_t *val)
{
   lsm6dsox_ctrl3_c_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL3_C, (uint8_t*)&reg, 1);
   *val = reg.sw_reset;
   return ret;
}

static nrfx_err_t lsm6dsox_auto_increment_set(uint8_t val)
{
   lsm6dsox_ctrl3_c_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL3_C, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.if_inc = val;
      ret = lsm6dsox_write_reg(LSM6DSOX_CTRL3_C, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_auto_increment_get(uint8_t *val)
{
   lsm6dsox_ctrl3_c_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL3_C, (uint8_t*)&reg, 1);
   *val = reg.if_inc;
   return ret;
}

static nrfx_err_t lsm6dsox_boot_set(uint8_t val)
{
   lsm6dsox_ctrl3_c_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL3_C, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.boot = val;
      ret = lsm6dsox_write_reg(LSM6DSOX_CTRL3_C, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_boot_get(uint8_t *val)
{
   lsm6dsox_ctrl3_c_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL3_C, (uint8_t*)&reg, 1);
   *val = reg.boot;
   return ret;
}

static nrfx_err_t lsm6dsox_xl_self_test_set(lsm6dsox_st_xl_t val)
{
   lsm6dsox_ctrl5_c_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL5_C, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.st_xl = (uint8_t)val;
      ret = lsm6dsox_write_reg(LSM6DSOX_CTRL5_C, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_xl_self_test_get(lsm6dsox_st_xl_t *val)
{
   lsm6dsox_ctrl5_c_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL5_C, (uint8_t*)&reg, 1);
   switch (reg.st_xl)
   {
      case LSM6DSOX_XL_ST_DISABLE:
         *val = LSM6DSOX_XL_ST_DISABLE;
         break;
      case LSM6DSOX_XL_ST_POSITIVE:
         *val = LSM6DSOX_XL_ST_POSITIVE;
         break;
      case LSM6DSOX_XL_ST_NEGATIVE:
         *val = LSM6DSOX_XL_ST_NEGATIVE;
         break;
      default:
         *val = LSM6DSOX_XL_ST_DISABLE;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_gy_self_test_set(lsm6dsox_st_g_t val)
{
   lsm6dsox_ctrl5_c_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL5_C, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.st_g = (uint8_t)val;
      ret = lsm6dsox_write_reg(LSM6DSOX_CTRL5_C, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_gy_self_test_get(lsm6dsox_st_g_t *val)
{
   lsm6dsox_ctrl5_c_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL5_C, (uint8_t*)&reg, 1);
   switch (reg.st_g)
   {
      case LSM6DSOX_GY_ST_DISABLE:
         *val = LSM6DSOX_GY_ST_DISABLE;
         break;
      case LSM6DSOX_GY_ST_POSITIVE:
         *val = LSM6DSOX_GY_ST_POSITIVE;
         break;
      case LSM6DSOX_GY_ST_NEGATIVE:
         *val = LSM6DSOX_GY_ST_NEGATIVE;
         break;
      default:
         *val = LSM6DSOX_GY_ST_DISABLE;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_xl_filter_lp2_set(uint8_t val)
{
   lsm6dsox_ctrl1_xl_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL1_XL, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.lpf2_xl_en = val;
      ret = lsm6dsox_write_reg(LSM6DSOX_CTRL1_XL, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_xl_filter_lp2_get(uint8_t *val)
{
   lsm6dsox_ctrl1_xl_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL1_XL, (uint8_t*)&reg, 1);
   *val = reg.lpf2_xl_en;
   return ret;
}

static nrfx_err_t lsm6dsox_gy_filter_lp1_set(uint8_t val)
{
   lsm6dsox_ctrl4_c_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL4_C, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.lpf1_sel_g = val;
      ret = lsm6dsox_write_reg(LSM6DSOX_CTRL4_C, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_gy_filter_lp1_get(uint8_t *val)
{
   lsm6dsox_ctrl4_c_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL4_C, (uint8_t*)&reg, 1);
   *val = reg.lpf1_sel_g;
   return ret;
}

static nrfx_err_t lsm6dsox_filter_settling_mask_set(uint8_t val)
{
   lsm6dsox_ctrl4_c_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL4_C, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.drdy_mask = val;
      ret = lsm6dsox_write_reg(LSM6DSOX_CTRL4_C, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_filter_settling_mask_get(uint8_t *val)
{
   lsm6dsox_ctrl4_c_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL4_C, (uint8_t*)&reg, 1);
   *val = reg.drdy_mask;
   return ret;
}

static nrfx_err_t lsm6dsox_gy_lp1_bandwidth_set(lsm6dsox_ftype_t val)
{
   lsm6dsox_ctrl6_c_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL6_C, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.ftype = (uint8_t)val;
      ret = lsm6dsox_write_reg(LSM6DSOX_CTRL6_C, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_gy_lp1_bandwidth_get(lsm6dsox_ftype_t *val)
{
   lsm6dsox_ctrl6_c_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL6_C, (uint8_t*)&reg, 1);
   switch (reg.ftype)
   {
      case LSM6DSOX_ULTRA_LIGHT:
         *val = LSM6DSOX_ULTRA_LIGHT;
         break;
      case LSM6DSOX_VERY_LIGHT:
         *val = LSM6DSOX_VERY_LIGHT;
         break;
      case LSM6DSOX_LIGHT:
         *val = LSM6DSOX_LIGHT;
         break;
      case LSM6DSOX_MEDIUM:
         *val = LSM6DSOX_MEDIUM;
         break;
      case LSM6DSOX_STRONG:
         *val = LSM6DSOX_STRONG;
         break;
      case LSM6DSOX_VERY_STRONG:
         *val = LSM6DSOX_VERY_STRONG;
         break;
      case LSM6DSOX_AGGRESSIVE:
         *val = LSM6DSOX_AGGRESSIVE;
         break;
      case LSM6DSOX_XTREME:
         *val = LSM6DSOX_XTREME;
         break;
      default:
         *val = LSM6DSOX_ULTRA_LIGHT;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_xl_lp2_on_6d_set(uint8_t val)
{
   lsm6dsox_ctrl8_xl_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL8_XL, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.low_pass_on_6d = val;
      ret = lsm6dsox_write_reg(LSM6DSOX_CTRL8_XL, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_xl_lp2_on_6d_get(uint8_t *val)
{
   lsm6dsox_ctrl8_xl_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL8_XL, (uint8_t*)&reg, 1);
   *val = reg.low_pass_on_6d;
   return ret;
}

static nrfx_err_t lsm6dsox_xl_hp_path_on_out_set(lsm6dsox_hp_slope_xl_en_t val)
{
   lsm6dsox_ctrl8_xl_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL8_XL, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.hp_slope_xl_en = ((uint8_t)val & 0x10U) >> 4;
      reg.hp_ref_mode_xl = ((uint8_t)val & 0x20U) >> 5;
      reg.hpcf_xl = (uint8_t)val & 0x07U;
      ret = lsm6dsox_write_reg(LSM6DSOX_CTRL8_XL, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_xl_hp_path_on_out_get(lsm6dsox_hp_slope_xl_en_t *val)
{
   lsm6dsox_ctrl8_xl_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL8_XL, (uint8_t*)&reg, 1);
   switch ((reg.hp_ref_mode_xl << 5) | (reg.hp_slope_xl_en << 4) | reg.hpcf_xl)
   {
      case LSM6DSOX_HP_PATH_DISABLE_ON_OUT:
         *val = LSM6DSOX_HP_PATH_DISABLE_ON_OUT;
         break;
      case LSM6DSOX_SLOPE_ODR_DIV_4:
         *val = LSM6DSOX_SLOPE_ODR_DIV_4;
         break;
      case LSM6DSOX_HP_ODR_DIV_10:
         *val = LSM6DSOX_HP_ODR_DIV_10;
         break;
      case LSM6DSOX_HP_ODR_DIV_20:
         *val = LSM6DSOX_HP_ODR_DIV_20;
         break;
      case LSM6DSOX_HP_ODR_DIV_45:
         *val = LSM6DSOX_HP_ODR_DIV_45;
         break;
      case LSM6DSOX_HP_ODR_DIV_100:
         *val = LSM6DSOX_HP_ODR_DIV_100;
         break;
      case LSM6DSOX_HP_ODR_DIV_200:
         *val = LSM6DSOX_HP_ODR_DIV_200;
         break;
      case LSM6DSOX_HP_ODR_DIV_400:
         *val = LSM6DSOX_HP_ODR_DIV_400;
         break;
      case LSM6DSOX_HP_ODR_DIV_800:
         *val = LSM6DSOX_HP_ODR_DIV_800;
         break;
      case LSM6DSOX_HP_REF_MD_ODR_DIV_10:
         *val = LSM6DSOX_HP_REF_MD_ODR_DIV_10;
         break;
      case LSM6DSOX_HP_REF_MD_ODR_DIV_20:
         *val = LSM6DSOX_HP_REF_MD_ODR_DIV_20;
         break;
      case LSM6DSOX_HP_REF_MD_ODR_DIV_45:
         *val = LSM6DSOX_HP_REF_MD_ODR_DIV_45;
         break;
      case LSM6DSOX_HP_REF_MD_ODR_DIV_100:
         *val = LSM6DSOX_HP_REF_MD_ODR_DIV_100;
         break;
      case LSM6DSOX_HP_REF_MD_ODR_DIV_200:
         *val = LSM6DSOX_HP_REF_MD_ODR_DIV_200;
         break;
      case LSM6DSOX_HP_REF_MD_ODR_DIV_400:
         *val = LSM6DSOX_HP_REF_MD_ODR_DIV_400;
         break;
      case LSM6DSOX_HP_REF_MD_ODR_DIV_800:
         *val = LSM6DSOX_HP_REF_MD_ODR_DIV_800;
         break;
      case LSM6DSOX_LP_ODR_DIV_10:
         *val = LSM6DSOX_LP_ODR_DIV_10;
         break;
      case LSM6DSOX_LP_ODR_DIV_20:
         *val = LSM6DSOX_LP_ODR_DIV_20;
         break;
      case LSM6DSOX_LP_ODR_DIV_45:
         *val = LSM6DSOX_LP_ODR_DIV_45;
         break;
      case LSM6DSOX_LP_ODR_DIV_100:
         *val = LSM6DSOX_LP_ODR_DIV_100;
         break;
      case LSM6DSOX_LP_ODR_DIV_200:
         *val = LSM6DSOX_LP_ODR_DIV_200;
         break;
      case LSM6DSOX_LP_ODR_DIV_400:
         *val = LSM6DSOX_LP_ODR_DIV_400;
         break;
      case LSM6DSOX_LP_ODR_DIV_800:
         *val = LSM6DSOX_LP_ODR_DIV_800;
         break;
      default:
         *val = LSM6DSOX_HP_PATH_DISABLE_ON_OUT;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_xl_fast_settling_set(uint8_t val)
{
   lsm6dsox_ctrl8_xl_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL8_XL, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.fastsettl_mode_xl = val;
      ret = lsm6dsox_write_reg(LSM6DSOX_CTRL8_XL, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_xl_fast_settling_get(uint8_t *val)
{
   lsm6dsox_ctrl8_xl_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL8_XL, (uint8_t*)&reg, 1);
   *val = reg.fastsettl_mode_xl;
   return ret;
}

static nrfx_err_t lsm6dsox_xl_hp_path_internal_set(lsm6dsox_slope_fds_t val)
{
   lsm6dsox_tap_cfg0_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_TAP_CFG0, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.slope_fds = (uint8_t)val;
      ret = lsm6dsox_write_reg(LSM6DSOX_TAP_CFG0, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_xl_hp_path_internal_get(lsm6dsox_slope_fds_t *val)
{
   lsm6dsox_tap_cfg0_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_TAP_CFG0, (uint8_t*)&reg, 1);
   switch (reg.slope_fds)
   {
      case LSM6DSOX_USE_SLOPE:
         *val = LSM6DSOX_USE_SLOPE;
         break;
      case LSM6DSOX_USE_HPF:
         *val = LSM6DSOX_USE_HPF;
         break;
      default:
         *val = LSM6DSOX_USE_SLOPE;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_gy_hp_path_internal_set(lsm6dsox_hpm_g_t val)
{
   lsm6dsox_ctrl7_g_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL7_G, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.hp_en_g = ((uint8_t)val & 0x80U) >> 7;
      reg.hpm_g = (uint8_t)val & 0x03U;
      ret = lsm6dsox_write_reg(LSM6DSOX_CTRL7_G, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_gy_hp_path_internal_get(lsm6dsox_hpm_g_t *val)
{
   lsm6dsox_ctrl7_g_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL7_G, (uint8_t*)&reg, 1);
   switch ((reg.hp_en_g << 7) + reg.hpm_g)
   {
      case LSM6DSOX_HP_FILTER_NONE:
         *val = LSM6DSOX_HP_FILTER_NONE;
         break;
      case LSM6DSOX_HP_FILTER_16mHz:
         *val = LSM6DSOX_HP_FILTER_16mHz;
         break;
      case LSM6DSOX_HP_FILTER_65mHz:
         *val = LSM6DSOX_HP_FILTER_65mHz;
         break;
      case LSM6DSOX_HP_FILTER_260mHz:
         *val = LSM6DSOX_HP_FILTER_260mHz;
         break;
      case LSM6DSOX_HP_FILTER_1Hz04:
         *val = LSM6DSOX_HP_FILTER_1Hz04;
         break;
      default:
         *val = LSM6DSOX_HP_FILTER_NONE;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_ois_mode_set(lsm6dsox_spi2_read_en_t val)
{
   lsm6dsox_func_cfg_access_t func_cfg_access;
   lsm6dsox_ui_int_ois_t ui_int_ois;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_UI_INT_OIS, (uint8_t*)&ui_int_ois, 1);
   if (ret == NRFX_SUCCESS)
   {
      ui_int_ois.spi2_read_en = ((uint8_t)val & 0x01U);
      ret = lsm6dsox_write_reg(LSM6DSOX_UI_INT_OIS, (uint8_t*)&ui_int_ois, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_FUNC_CFG_ACCESS, (uint8_t*)&func_cfg_access, 1);
   if (ret == NRFX_SUCCESS)
   {
      func_cfg_access.ois_ctrl_from_ui = ( ((uint8_t)val & 0x02U) >> 1 );
      ret = lsm6dsox_write_reg(LSM6DSOX_FUNC_CFG_ACCESS, (uint8_t*)&func_cfg_access, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_ois_mode_get(lsm6dsox_spi2_read_en_t *val)
{
   lsm6dsox_func_cfg_access_t func_cfg_access;
   lsm6dsox_ui_int_ois_t ui_int_ois;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_UI_INT_OIS, (uint8_t*)&ui_int_ois, 1);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_FUNC_CFG_ACCESS, (uint8_t*)&func_cfg_access, 1);
   switch ((func_cfg_access.ois_ctrl_from_ui << 1) + ui_int_ois.spi2_read_en)
   {
      case LSM6DSOX_OIS_CTRL_AUX_DATA_UI:
         *val = LSM6DSOX_OIS_CTRL_AUX_DATA_UI;
         break;
      case LSM6DSOX_OIS_CTRL_AUX_DATA_UI_AUX:
         *val = LSM6DSOX_OIS_CTRL_AUX_DATA_UI_AUX;
         break;
      case LSM6DSOX_OIS_CTRL_UI_AUX_DATA_UI:
         *val = LSM6DSOX_OIS_CTRL_UI_AUX_DATA_UI;
         break;
      case LSM6DSOX_OIS_CTRL_UI_AUX_DATA_UI_AUX:
         *val = LSM6DSOX_OIS_CTRL_UI_AUX_DATA_UI_AUX;
         break;
      default:
         *val = LSM6DSOX_OIS_CTRL_AUX_DATA_UI;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_aux_sdo_ocs_mode_set(lsm6dsox_ois_pu_dis_t val)
{
   lsm6dsox_pin_ctrl_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_PIN_CTRL, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.ois_pu_dis = (uint8_t)val;
      ret = lsm6dsox_write_reg(LSM6DSOX_PIN_CTRL, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_aux_sdo_ocs_mode_get(lsm6dsox_ois_pu_dis_t *val)
{
   lsm6dsox_pin_ctrl_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_PIN_CTRL, (uint8_t*)&reg, 1);
   switch (reg.ois_pu_dis)
   {
      case LSM6DSOX_AUX_PULL_UP_DISC:
         *val = LSM6DSOX_AUX_PULL_UP_DISC;
         break;
      case LSM6DSOX_AUX_PULL_UP_CONNECT:
         *val = LSM6DSOX_AUX_PULL_UP_CONNECT;
         break;
      default:
         *val = LSM6DSOX_AUX_PULL_UP_DISC;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_aux_pw_on_ctrl_set(lsm6dsox_ois_on_t val)
{
   lsm6dsox_ctrl7_g_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL7_G, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.ois_on_en = (uint8_t)val & 0x01U;
      reg.ois_on = (uint8_t)val & 0x01U;
      ret = lsm6dsox_write_reg(LSM6DSOX_CTRL7_G, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_aux_pw_on_ctrl_get(lsm6dsox_ois_on_t *val)
{
   lsm6dsox_ctrl7_g_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL7_G, (uint8_t*)&reg, 1);
   switch (reg.ois_on)
   {
      case LSM6DSOX_AUX_ON:
         *val = LSM6DSOX_AUX_ON;
         break;
      case LSM6DSOX_AUX_ON_BY_AUX_INTERFACE:
         *val = LSM6DSOX_AUX_ON_BY_AUX_INTERFACE;
         break;
      default:
         *val = LSM6DSOX_AUX_ON;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_aux_xl_fs_mode_set(lsm6dsox_xl_fs_mode_t val)
{
   lsm6dsox_ctrl8_xl_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL8_XL, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.xl_fs_mode = (uint8_t)val;
      ret = lsm6dsox_write_reg(LSM6DSOX_CTRL8_XL, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_aux_xl_fs_mode_get(lsm6dsox_xl_fs_mode_t *val)
{
   lsm6dsox_ctrl8_xl_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL8_XL, (uint8_t*)&reg, 1);
   switch (reg.xl_fs_mode)
   {
      case LSM6DSOX_USE_SAME_XL_FS:
         *val = LSM6DSOX_USE_SAME_XL_FS;
         break;
      case LSM6DSOX_USE_DIFFERENT_XL_FS:
         *val = LSM6DSOX_USE_DIFFERENT_XL_FS;
         break;
      default:
         *val = LSM6DSOX_USE_SAME_XL_FS;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_aux_status_reg_get(lsm6dsox_spi2_status_reg_ois_t *val)
{
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_SPI2_STATUS_REG_OIS, (uint8_t*)val, 1);
   return ret;
}

static nrfx_err_t lsm6dsox_aux_xl_flag_data_ready_get(uint8_t *val)
{
   lsm6dsox_spi2_status_reg_ois_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_SPI2_STATUS_REG_OIS, (uint8_t*)&reg, 1);
   *val = reg.xlda;
   return ret;
}

static nrfx_err_t lsm6dsox_aux_gy_flag_data_ready_get(uint8_t *val)
{
   lsm6dsox_spi2_status_reg_ois_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_SPI2_STATUS_REG_OIS, (uint8_t*)&reg, 1);
   *val = reg.gda;
   return ret;
}

static nrfx_err_t lsm6dsox_aux_gy_flag_settling_get(uint8_t *val)
{
   lsm6dsox_spi2_status_reg_ois_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_SPI2_STATUS_REG_OIS, (uint8_t*)&reg, 1);
   *val = reg.gyro_settling;
   return ret;
}

static nrfx_err_t lsm6dsox_aux_den_polarity_set(lsm6dsox_den_lh_ois_t val)
{
   lsm6dsox_ui_int_ois_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_UI_INT_OIS, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.den_lh_ois = (uint8_t)val;
      ret = lsm6dsox_write_reg(LSM6DSOX_UI_INT_OIS, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_aux_den_polarity_get(lsm6dsox_den_lh_ois_t *val)
{
   lsm6dsox_ui_int_ois_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_UI_INT_OIS, (uint8_t*)&reg, 1);
   switch (reg.den_lh_ois)
   {
      case LSM6DSOX_AUX_DEN_ACTIVE_LOW:
         *val = LSM6DSOX_AUX_DEN_ACTIVE_LOW;
         break;
      case LSM6DSOX_AUX_DEN_ACTIVE_HIGH:
         *val = LSM6DSOX_AUX_DEN_ACTIVE_HIGH;
         break;
      default:
         *val = LSM6DSOX_AUX_DEN_ACTIVE_LOW;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_aux_den_mode_set(lsm6dsox_lvl2_ois_t val)
{
   lsm6dsox_ui_ctrl1_ois_t ctrl1_ois;
   lsm6dsox_ui_int_ois_t int_ois;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_UI_INT_OIS, (uint8_t*)&int_ois, 1);
   if (ret == NRFX_SUCCESS)
   {
      int_ois.lvl2_ois = (uint8_t)val & 0x01U;
      ret = lsm6dsox_write_reg(LSM6DSOX_UI_INT_OIS, (uint8_t*)&int_ois, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_UI_CTRL1_OIS, (uint8_t*)&ctrl1_ois, 1);
   if (ret == NRFX_SUCCESS)
   {
      ctrl1_ois.lvl1_ois = ((uint8_t)val & 0x02U) >> 1;
      ret = lsm6dsox_write_reg(LSM6DSOX_UI_CTRL1_OIS, (uint8_t*)&ctrl1_ois, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_aux_den_mode_get(lsm6dsox_lvl2_ois_t *val)
{
   lsm6dsox_ui_ctrl1_ois_t ctrl1_ois;
   lsm6dsox_ui_int_ois_t int_ois;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_UI_INT_OIS, (uint8_t*)&int_ois, 1);
   if (ret == NRFX_SUCCESS)
   {
      ret = lsm6dsox_read_reg(LSM6DSOX_UI_CTRL1_OIS, (uint8_t*)&ctrl1_ois, 1);
      switch ((ctrl1_ois.lvl1_ois << 1) + int_ois.lvl2_ois)
      {
         case LSM6DSOX_AUX_DEN_DISABLE:
            *val = LSM6DSOX_AUX_DEN_DISABLE;
            break;
         case LSM6DSOX_AUX_DEN_LEVEL_LATCH:
            *val = LSM6DSOX_AUX_DEN_LEVEL_LATCH;
            break;
         case LSM6DSOX_AUX_DEN_LEVEL_TRIG:
            *val = LSM6DSOX_AUX_DEN_LEVEL_TRIG;
            break;
         default:
            *val = LSM6DSOX_AUX_DEN_DISABLE;
            break;
      }
   }
   return ret;
}

static nrfx_err_t lsm6dsox_aux_drdy_on_int2_set(uint8_t val)
{
   lsm6dsox_ui_int_ois_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_UI_INT_OIS, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.int2_drdy_ois = val;
      ret = lsm6dsox_write_reg(LSM6DSOX_UI_INT_OIS, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_aux_drdy_on_int2_get(uint8_t *val)
{
   lsm6dsox_ui_int_ois_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_UI_INT_OIS, (uint8_t*)&reg, 1);
   *val = reg.int2_drdy_ois;
   return ret;
}

static nrfx_err_t lsm6dsox_aux_mode_set(lsm6dsox_ois_en_spi2_t val)
{
   lsm6dsox_ui_ctrl1_ois_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_UI_CTRL1_OIS, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.ois_en_spi2 = (uint8_t)val & 0x01U;
      reg.mode4_en = ((uint8_t)val & 0x02U) >> 1;
      ret = lsm6dsox_write_reg(LSM6DSOX_UI_CTRL1_OIS, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_aux_mode_get(lsm6dsox_ois_en_spi2_t *val)
{
   lsm6dsox_ui_ctrl1_ois_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_UI_CTRL1_OIS, (uint8_t*)&reg, 1);
   switch ((reg.mode4_en << 1) | reg.ois_en_spi2)
   {
      case LSM6DSOX_AUX_DISABLE:
         *val = LSM6DSOX_AUX_DISABLE;
         break;
      case LSM6DSOX_MODE_3_GY:
         *val = LSM6DSOX_MODE_3_GY;
         break;
      case LSM6DSOX_MODE_4_GY_XL:
         *val = LSM6DSOX_MODE_4_GY_XL;
         break;
      default:
         *val = LSM6DSOX_AUX_DISABLE;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_aux_gy_full_scale_set(lsm6dsox_fs_g_ois_t val)
{
   lsm6dsox_ui_ctrl1_ois_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_UI_CTRL1_OIS, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.fs_g_ois = (uint8_t)val;
      ret = lsm6dsox_write_reg(LSM6DSOX_UI_CTRL1_OIS, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_aux_gy_full_scale_get(lsm6dsox_fs_g_ois_t *val)
{
   lsm6dsox_ui_ctrl1_ois_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_UI_CTRL1_OIS, (uint8_t*)&reg, 1);
   switch (reg.fs_g_ois)
   {
      case LSM6DSOX_250dps_AUX:
         *val = LSM6DSOX_250dps_AUX;
         break;
      case LSM6DSOX_125dps_AUX:
         *val = LSM6DSOX_125dps_AUX;
         break;
      case LSM6DSOX_500dps_AUX:
         *val = LSM6DSOX_500dps_AUX;
         break;
      case LSM6DSOX_1000dps_AUX:
         *val = LSM6DSOX_1000dps_AUX;
         break;
      case LSM6DSOX_2000dps_AUX:
         *val = LSM6DSOX_2000dps_AUX;
         break;
      default:
         *val = LSM6DSOX_250dps_AUX;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_aux_spi_mode_set(lsm6dsox_sim_ois_t val)
{
   lsm6dsox_ui_ctrl1_ois_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_UI_CTRL1_OIS, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.sim_ois = (uint8_t)val;
      ret = lsm6dsox_write_reg(LSM6DSOX_UI_CTRL1_OIS, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_aux_spi_mode_get(lsm6dsox_sim_ois_t *val)
{
   lsm6dsox_ui_ctrl1_ois_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_UI_CTRL1_OIS, (uint8_t*)&reg, 1);
   switch (reg.sim_ois)
   {
      case LSM6DSOX_AUX_SPI_4_WIRE:
         *val = LSM6DSOX_AUX_SPI_4_WIRE;
         break;
      case LSM6DSOX_AUX_SPI_3_WIRE:
         *val = LSM6DSOX_AUX_SPI_3_WIRE;
         break;
      default:
         *val = LSM6DSOX_AUX_SPI_4_WIRE;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_aux_gy_lp1_bandwidth_set(lsm6dsox_ftype_ois_t val)
{
   lsm6dsox_ui_ctrl2_ois_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_UI_CTRL2_OIS, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.ftype_ois = (uint8_t)val;
      ret = lsm6dsox_write_reg(LSM6DSOX_UI_CTRL2_OIS, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_aux_gy_lp1_bandwidth_get(lsm6dsox_ftype_ois_t *val)
{
   lsm6dsox_ui_ctrl2_ois_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_UI_CTRL2_OIS, (uint8_t*)&reg, 1);
   switch (reg.ftype_ois)
   {
      case LSM6DSOX_351Hz39:
         *val = LSM6DSOX_351Hz39;
         break;
      case LSM6DSOX_236Hz63:
         *val = LSM6DSOX_236Hz63;
         break;
      case LSM6DSOX_172Hz70:
         *val = LSM6DSOX_172Hz70;
         break;
      case LSM6DSOX_937Hz91:
         *val = LSM6DSOX_937Hz91;
         break;
      default:
         *val = LSM6DSOX_351Hz39;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_aux_gy_hp_bandwidth_set(lsm6dsox_hpm_ois_t val)
{
   lsm6dsox_ui_ctrl2_ois_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_UI_CTRL2_OIS, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.hpm_ois = (uint8_t)val & 0x03U;
      reg.hp_en_ois = ((uint8_t)val & 0x10U) >> 4;
      ret = lsm6dsox_write_reg(LSM6DSOX_UI_CTRL2_OIS, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_aux_gy_hp_bandwidth_get(lsm6dsox_hpm_ois_t *val)
{
   lsm6dsox_ui_ctrl2_ois_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_UI_CTRL2_OIS, (uint8_t*)&reg, 1);
   switch ((reg.hp_en_ois << 4) | reg.hpm_ois)
   {
      case LSM6DSOX_AUX_HP_DISABLE:
         *val = LSM6DSOX_AUX_HP_DISABLE;
         break;
      case LSM6DSOX_AUX_HP_Hz016:
         *val = LSM6DSOX_AUX_HP_Hz016;
         break;
      case LSM6DSOX_AUX_HP_Hz065:
         *val = LSM6DSOX_AUX_HP_Hz065;
         break;
      case LSM6DSOX_AUX_HP_Hz260:
         *val = LSM6DSOX_AUX_HP_Hz260;
         break;
      case LSM6DSOX_AUX_HP_1Hz040:
         *val = LSM6DSOX_AUX_HP_1Hz040;
         break;
      default:
         *val = LSM6DSOX_AUX_HP_DISABLE;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_aux_gy_clamp_set(lsm6dsox_st_ois_clampdis_t val)
{
   lsm6dsox_ui_ctrl3_ois_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_UI_CTRL3_OIS, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.st_ois_clampdis = (uint8_t)val;
      ret = lsm6dsox_write_reg(LSM6DSOX_UI_CTRL3_OIS, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_aux_gy_clamp_get(lsm6dsox_st_ois_clampdis_t *val)
{
   lsm6dsox_ui_ctrl3_ois_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_UI_CTRL3_OIS, (uint8_t*)&reg, 1);
   switch (reg.st_ois_clampdis)
   {
      case LSM6DSOX_ENABLE_CLAMP:
         *val = LSM6DSOX_ENABLE_CLAMP;
         break;
      case LSM6DSOX_DISABLE_CLAMP:
         *val = LSM6DSOX_DISABLE_CLAMP;
         break;
      default:
         *val = LSM6DSOX_ENABLE_CLAMP;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_aux_xl_bandwidth_set(lsm6dsox_filter_xl_conf_ois_t val)
{
   lsm6dsox_ui_ctrl3_ois_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_UI_CTRL3_OIS, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.filter_xl_conf_ois = (uint8_t)val;
      ret = lsm6dsox_write_reg(LSM6DSOX_UI_CTRL3_OIS, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_aux_xl_bandwidth_get(lsm6dsox_filter_xl_conf_ois_t *val)
{
   lsm6dsox_ui_ctrl3_ois_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_UI_CTRL3_OIS, (uint8_t*)&reg, 1);
   switch (reg.filter_xl_conf_ois)
   {
      case LSM6DSOX_289Hz:
         *val = LSM6DSOX_289Hz;
         break;
      case LSM6DSOX_258Hz:
         *val = LSM6DSOX_258Hz;
         break;
      case LSM6DSOX_120Hz:
         *val = LSM6DSOX_120Hz;
         break;
      case LSM6DSOX_65Hz2:
         *val = LSM6DSOX_65Hz2;
         break;
      case LSM6DSOX_33Hz2:
         *val = LSM6DSOX_33Hz2;
         break;
      case LSM6DSOX_16Hz6:
         *val = LSM6DSOX_16Hz6;
         break;
      case LSM6DSOX_8Hz30:
         *val = LSM6DSOX_8Hz30;
         break;
      case LSM6DSOX_4Hz15:
         *val = LSM6DSOX_4Hz15;
         break;
      default:
         *val = LSM6DSOX_289Hz;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_aux_xl_full_scale_set(lsm6dsox_fs_xl_ois_t val)
{
   lsm6dsox_ui_ctrl3_ois_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_UI_CTRL3_OIS, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.fs_xl_ois = (uint8_t)val;
      ret = lsm6dsox_write_reg(LSM6DSOX_UI_CTRL3_OIS, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_aux_xl_full_scale_get(lsm6dsox_fs_xl_ois_t *val)
{
   lsm6dsox_ui_ctrl3_ois_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_UI_CTRL3_OIS, (uint8_t*)&reg, 1);
   switch (reg.fs_xl_ois)
   {
      case LSM6DSOX_AUX_2g:
         *val = LSM6DSOX_AUX_2g;
         break;
      case LSM6DSOX_AUX_16g:
         *val = LSM6DSOX_AUX_16g;
         break;
      case LSM6DSOX_AUX_4g:
         *val = LSM6DSOX_AUX_4g;
         break;
      case LSM6DSOX_AUX_8g:
         *val = LSM6DSOX_AUX_8g;
         break;
      default:
         *val = LSM6DSOX_AUX_2g;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_sdo_sa0_mode_set(lsm6dsox_sdo_pu_en_t val)
{
   lsm6dsox_pin_ctrl_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_PIN_CTRL, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.sdo_pu_en = (uint8_t)val;
      ret = lsm6dsox_write_reg(LSM6DSOX_PIN_CTRL, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_sdo_sa0_mode_get(lsm6dsox_sdo_pu_en_t *val)
{
   lsm6dsox_pin_ctrl_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_PIN_CTRL, (uint8_t*)&reg, 1);
   switch (reg.sdo_pu_en)
   {
      case LSM6DSOX_PULL_UP_DISC:
         *val = LSM6DSOX_PULL_UP_DISC;
         break;
      case LSM6DSOX_PULL_UP_CONNECT:
         *val = LSM6DSOX_PULL_UP_CONNECT;
         break;
      default:
         *val = LSM6DSOX_PULL_UP_DISC;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_spi_mode_set(lsm6dsox_sim_t val)
{
   lsm6dsox_ctrl3_c_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL3_C, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.sim = (uint8_t)val;
      ret = lsm6dsox_write_reg(LSM6DSOX_CTRL3_C, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_spi_mode_get(lsm6dsox_sim_t *val)
{
   lsm6dsox_ctrl3_c_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL3_C, (uint8_t*)&reg, 1);
   switch (reg.sim)
   {
      case LSM6DSOX_SPI_4_WIRE:
         *val = LSM6DSOX_SPI_4_WIRE;
         break;
      case LSM6DSOX_SPI_3_WIRE:
         *val = LSM6DSOX_SPI_3_WIRE;
         break;
      default:
         *val = LSM6DSOX_SPI_4_WIRE;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_i2c_interface_set(lsm6dsox_i2c_disable_t val)
{
   lsm6dsox_ctrl4_c_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL4_C, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.i2c_disable = (uint8_t)val;
      ret = lsm6dsox_write_reg(LSM6DSOX_CTRL4_C, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_i2c_interface_get(lsm6dsox_i2c_disable_t *val)
{
   lsm6dsox_ctrl4_c_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL4_C, (uint8_t*)&reg, 1);
   switch (reg.i2c_disable)
   {
      case LSM6DSOX_I2C_ENABLE:
         *val = LSM6DSOX_I2C_ENABLE;
         break;
      case LSM6DSOX_I2C_DISABLE:
         *val = LSM6DSOX_I2C_DISABLE;
         break;
      default:
         *val = LSM6DSOX_I2C_ENABLE;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_i3c_disable_set(lsm6dsox_i3c_disable_t val)
{
   lsm6dsox_i3c_bus_avb_t i3c_bus_avb;
   lsm6dsox_ctrl9_xl_t ctrl9_xl;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL9_XL, (uint8_t*)&ctrl9_xl, 1);
   if (ret == NRFX_SUCCESS)
   {
      ctrl9_xl.i3c_disable = ((uint8_t)val & 0x80U) >> 7;
      ret = lsm6dsox_write_reg(LSM6DSOX_CTRL9_XL, (uint8_t*)&ctrl9_xl, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_I3C_BUS_AVB, (uint8_t*)&i3c_bus_avb, 1);
   if (ret == NRFX_SUCCESS)
   {
      i3c_bus_avb.i3c_bus_avb_sel = (uint8_t)val & 0x03U;
      ret = lsm6dsox_write_reg(LSM6DSOX_I3C_BUS_AVB, (uint8_t*)&i3c_bus_avb, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_i3c_disable_get(lsm6dsox_i3c_disable_t *val)
{
   lsm6dsox_ctrl9_xl_t ctrl9_xl;
   lsm6dsox_i3c_bus_avb_t i3c_bus_avb;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL9_XL, (uint8_t*)&ctrl9_xl, 1);
   if (ret == NRFX_SUCCESS)
   {
      ret = lsm6dsox_read_reg(LSM6DSOX_I3C_BUS_AVB, (uint8_t*)&i3c_bus_avb, 1);
      switch ((ctrl9_xl.i3c_disable << 7) | i3c_bus_avb.i3c_bus_avb_sel)
      {
         case LSM6DSOX_I3C_DISABLE:
            *val = LSM6DSOX_I3C_DISABLE;
            break;
         case LSM6DSOX_I3C_ENABLE_T_50us:
            *val = LSM6DSOX_I3C_ENABLE_T_50us;
            break;
         case LSM6DSOX_I3C_ENABLE_T_2us:
            *val = LSM6DSOX_I3C_ENABLE_T_2us;
            break;
         case LSM6DSOX_I3C_ENABLE_T_1ms:
            *val = LSM6DSOX_I3C_ENABLE_T_1ms;
            break;
         case LSM6DSOX_I3C_ENABLE_T_25ms:
            *val = LSM6DSOX_I3C_ENABLE_T_25ms;
            break;
         default:
            *val = LSM6DSOX_I3C_DISABLE;
            break;
      }
   }
   return ret;
}

static nrfx_err_t lsm6dsox_pin_mode_set(lsm6dsox_pp_od_t val)
{
   lsm6dsox_i3c_bus_avb_t i3c_bus_avb;
   lsm6dsox_ctrl3_c_t ctrl3_c;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL3_C, (uint8_t*)&ctrl3_c, 1);
   if (ret == NRFX_SUCCESS)
   {
      ctrl3_c.pp_od = (uint8_t)val;
      ret = lsm6dsox_write_reg(LSM6DSOX_CTRL3_C, (uint8_t*)&ctrl3_c, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_I3C_BUS_AVB, (uint8_t*)&i3c_bus_avb, 1);
   if (ret == NRFX_SUCCESS)
   {
      i3c_bus_avb.pd_dis_int1 = ((uint8_t)val & 0x02U) >> 1;
      ret = lsm6dsox_write_reg(LSM6DSOX_I3C_BUS_AVB, (uint8_t*)&i3c_bus_avb, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_pin_mode_get(lsm6dsox_pp_od_t *val)
{
   lsm6dsox_i3c_bus_avb_t i3c_bus_avb;
   lsm6dsox_ctrl3_c_t ctrl3_c;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL3_C, (uint8_t*)&ctrl3_c, 1);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_I3C_BUS_AVB, (uint8_t*)&i3c_bus_avb, 1);
   switch ((i3c_bus_avb.pd_dis_int1 << 1) + ctrl3_c.pp_od)
   {
      case LSM6DSOX_PUSH_PULL:
         *val = LSM6DSOX_PUSH_PULL;
         break;
      case LSM6DSOX_OPEN_DRAIN:
         *val = LSM6DSOX_OPEN_DRAIN;
         break;
      case LSM6DSOX_INT1_NOPULL_DOWN_INT2_PUSH_PULL:
         *val = LSM6DSOX_INT1_NOPULL_DOWN_INT2_PUSH_PULL;
         break;
      case LSM6DSOX_INT1_NOPULL_DOWN_INT2_OPEN_DRAIN:
         *val = LSM6DSOX_INT1_NOPULL_DOWN_INT2_OPEN_DRAIN;
         break;
      default:
         *val = LSM6DSOX_PUSH_PULL;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_pin_polarity_set(lsm6dsox_h_lactive_t val)
{
   lsm6dsox_ctrl3_c_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL3_C, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.h_lactive = (uint8_t)val;
      ret = lsm6dsox_write_reg(LSM6DSOX_CTRL3_C, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_pin_polarity_get(lsm6dsox_h_lactive_t *val)
{
   lsm6dsox_ctrl3_c_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL3_C, (uint8_t*)&reg, 1);
   switch (reg.h_lactive)
   {
      case LSM6DSOX_ACTIVE_HIGH:
         *val = LSM6DSOX_ACTIVE_HIGH;
         break;
      case LSM6DSOX_ACTIVE_LOW:
         *val = LSM6DSOX_ACTIVE_LOW;
         break;
      default:
         *val = LSM6DSOX_ACTIVE_HIGH;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_all_on_int1_set(uint8_t val)
{
   lsm6dsox_ctrl4_c_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL4_C, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.int2_on_int1 = val;
      ret = lsm6dsox_write_reg(LSM6DSOX_CTRL4_C, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_all_on_int1_get(uint8_t *val)
{
   lsm6dsox_ctrl4_c_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL4_C, (uint8_t*)&reg, 1);
   *val = reg.int2_on_int1;
   return ret;
}

static nrfx_err_t lsm6dsox_int_notification_set(lsm6dsox_lir_t val)
{
   lsm6dsox_tap_cfg0_t tap_cfg0;
   lsm6dsox_page_rw_t page_rw;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_TAP_CFG0, (uint8_t*)&tap_cfg0, 1);
   if (ret == NRFX_SUCCESS)
   {
      tap_cfg0.lir = (uint8_t)val & 0x01U;
      tap_cfg0.int_clr_on_read = (uint8_t)val & 0x01U;
      ret = lsm6dsox_write_reg(LSM6DSOX_TAP_CFG0, (uint8_t*)&tap_cfg0, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_EMBEDDED_FUNC_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_PAGE_RW, (uint8_t*)&page_rw, 1);
   if (ret == NRFX_SUCCESS)
   {
      page_rw.emb_func_lir = ((uint8_t)val & 0x02U) >> 1;
      ret = lsm6dsox_write_reg(LSM6DSOX_PAGE_RW, (uint8_t*)&page_rw, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   return ret;
}

static nrfx_err_t lsm6dsox_int_notification_get(lsm6dsox_lir_t *val)
{
   lsm6dsox_tap_cfg0_t tap_cfg0;
   lsm6dsox_page_rw_t page_rw;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_TAP_CFG0, (uint8_t*)&tap_cfg0, 1);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_EMBEDDED_FUNC_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_PAGE_RW, (uint8_t*)&page_rw, 1);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   if (ret == NRFX_SUCCESS)
   {
      switch ((page_rw.emb_func_lir << 1) | tap_cfg0.lir)
      {
         case LSM6DSOX_ALL_INT_PULSED:
            *val = LSM6DSOX_ALL_INT_PULSED;
            break;
         case LSM6DSOX_BASE_LATCHED_EMB_PULSED:
            *val = LSM6DSOX_BASE_LATCHED_EMB_PULSED;
            break;
         case LSM6DSOX_BASE_PULSED_EMB_LATCHED:
            *val = LSM6DSOX_BASE_PULSED_EMB_LATCHED;
            break;
         case LSM6DSOX_ALL_INT_LATCHED:
            *val = LSM6DSOX_ALL_INT_LATCHED;
            break;
         default:
            *val = LSM6DSOX_ALL_INT_PULSED;
            break;
      }
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_EMBEDDED_FUNC_BANK);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_PAGE_RW, (uint8_t*)&page_rw, 1);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   return ret;
}

static nrfx_err_t lsm6dsox_wkup_ths_weight_set(lsm6dsox_wake_ths_w_t val)
{
   lsm6dsox_wake_up_dur_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_WAKE_UP_DUR, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.wake_ths_w = (uint8_t)val;
      ret = lsm6dsox_write_reg(LSM6DSOX_WAKE_UP_DUR, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_wkup_ths_weight_get(lsm6dsox_wake_ths_w_t *val)
{
   lsm6dsox_wake_up_dur_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_WAKE_UP_DUR, (uint8_t*)&reg, 1);
   switch (reg.wake_ths_w)
   {
      case LSM6DSOX_LSb_FS_DIV_64:
         *val = LSM6DSOX_LSb_FS_DIV_64;
         break;
      case LSM6DSOX_LSb_FS_DIV_256:
         *val = LSM6DSOX_LSb_FS_DIV_256;
         break;
      default:
         *val = LSM6DSOX_LSb_FS_DIV_64;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_wkup_threshold_set(uint8_t val)
{
   lsm6dsox_wake_up_ths_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_WAKE_UP_THS, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.wk_ths = val;
      ret = lsm6dsox_write_reg(LSM6DSOX_WAKE_UP_THS, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_wkup_threshold_get(uint8_t *val)
{
   lsm6dsox_wake_up_ths_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_WAKE_UP_THS, (uint8_t*)&reg, 1);
   *val = reg.wk_ths;
   return ret;
}

static nrfx_err_t lsm6dsox_xl_usr_offset_on_wkup_set(uint8_t val)
{
   lsm6dsox_wake_up_ths_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_WAKE_UP_THS, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.usr_off_on_wu = val;
      ret = lsm6dsox_write_reg(LSM6DSOX_WAKE_UP_THS, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_xl_usr_offset_on_wkup_get(uint8_t *val)
{
   lsm6dsox_wake_up_ths_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_WAKE_UP_THS, (uint8_t*)&reg, 1);
   *val = reg.usr_off_on_wu;
   return ret;
}

static nrfx_err_t lsm6dsox_wkup_dur_set(uint8_t val)
{
   lsm6dsox_wake_up_dur_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_WAKE_UP_DUR, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.wake_dur = val;
      ret = lsm6dsox_write_reg(LSM6DSOX_WAKE_UP_DUR, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_wkup_dur_get(uint8_t *val)
{
   lsm6dsox_wake_up_dur_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_WAKE_UP_DUR, (uint8_t*)&reg, 1);
   *val = reg.wake_dur;
   return ret;
}

static nrfx_err_t lsm6dsox_gy_sleep_mode_set(uint8_t val)
{
   lsm6dsox_ctrl4_c_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL4_C, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.sleep_g = val;
      ret = lsm6dsox_write_reg(LSM6DSOX_CTRL4_C, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_gy_sleep_mode_get(uint8_t *val)
{
   lsm6dsox_ctrl4_c_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL4_C, (uint8_t*)&reg, 1);
   *val = reg.sleep_g;
   return ret;
}

static nrfx_err_t lsm6dsox_act_pin_notification_set(lsm6dsox_sleep_status_on_int_t val)
{
   lsm6dsox_tap_cfg0_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_TAP_CFG0, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.sleep_status_on_int = (uint8_t)val;
      ret = lsm6dsox_write_reg(LSM6DSOX_TAP_CFG0, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_act_pin_notification_get(lsm6dsox_sleep_status_on_int_t *val)
{
   lsm6dsox_tap_cfg0_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_TAP_CFG0, (uint8_t*)&reg, 1);
   switch (reg.sleep_status_on_int)
   {
      case LSM6DSOX_DRIVE_SLEEP_CHG_EVENT:
         *val = LSM6DSOX_DRIVE_SLEEP_CHG_EVENT;
         break;
      case LSM6DSOX_DRIVE_SLEEP_STATUS:
         *val = LSM6DSOX_DRIVE_SLEEP_STATUS;
         break;
      default:
         *val = LSM6DSOX_DRIVE_SLEEP_CHG_EVENT;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_act_mode_set(lsm6dsox_inact_en_t val)
{
   lsm6dsox_tap_cfg2_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_TAP_CFG2, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.inact_en = (uint8_t)val;
      ret = lsm6dsox_write_reg(LSM6DSOX_TAP_CFG2, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_act_mode_get(lsm6dsox_inact_en_t *val)
{
   lsm6dsox_tap_cfg2_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_TAP_CFG2, (uint8_t*)&reg, 1);
   switch (reg.inact_en)
   {
      case LSM6DSOX_XL_AND_GY_NOT_AFFECTED:
         *val = LSM6DSOX_XL_AND_GY_NOT_AFFECTED;
         break;
      case LSM6DSOX_XL_12Hz5_GY_NOT_AFFECTED:
         *val = LSM6DSOX_XL_12Hz5_GY_NOT_AFFECTED;
         break;
      case LSM6DSOX_XL_12Hz5_GY_SLEEP:
         *val = LSM6DSOX_XL_12Hz5_GY_SLEEP;
         break;
      case LSM6DSOX_XL_12Hz5_GY_PD:
         *val = LSM6DSOX_XL_12Hz5_GY_PD;
         break;
      default:
         *val = LSM6DSOX_XL_AND_GY_NOT_AFFECTED;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_act_sleep_dur_set(uint8_t val)
{
   lsm6dsox_wake_up_dur_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_WAKE_UP_DUR, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.sleep_dur = val;
      ret = lsm6dsox_write_reg(LSM6DSOX_WAKE_UP_DUR, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_act_sleep_dur_get(uint8_t *val)
{
   lsm6dsox_wake_up_dur_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_WAKE_UP_DUR, (uint8_t*)&reg, 1);
   *val = reg.sleep_dur;
   return ret;
}

static nrfx_err_t lsm6dsox_tap_detection_on_z_set(uint8_t val)
{
   lsm6dsox_tap_cfg0_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_TAP_CFG0, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.tap_z_en = val;
      ret = lsm6dsox_write_reg(LSM6DSOX_TAP_CFG0, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_tap_detection_on_z_get(uint8_t *val)
{
   lsm6dsox_tap_cfg0_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_TAP_CFG0, (uint8_t*)&reg, 1);
   *val = reg.tap_z_en;
   return ret;
}

static nrfx_err_t lsm6dsox_tap_detection_on_y_set(uint8_t val)
{
   lsm6dsox_tap_cfg0_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_TAP_CFG0, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.tap_y_en = val;
      ret = lsm6dsox_write_reg(LSM6DSOX_TAP_CFG0, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_tap_detection_on_y_get(uint8_t *val)
{
   lsm6dsox_tap_cfg0_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_TAP_CFG0, (uint8_t*)&reg, 1);
   *val = reg.tap_y_en;
   return ret;
}

static nrfx_err_t lsm6dsox_tap_detection_on_x_set(uint8_t val)
{
   lsm6dsox_tap_cfg0_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_TAP_CFG0, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.tap_x_en = val;
      ret = lsm6dsox_write_reg(LSM6DSOX_TAP_CFG0, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_tap_detection_on_x_get(uint8_t *val)
{
   lsm6dsox_tap_cfg0_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_TAP_CFG0, (uint8_t*)&reg, 1);
   *val = reg.tap_x_en;
   return ret;
}

static nrfx_err_t lsm6dsox_tap_threshold_x_set(uint8_t val)
{
   lsm6dsox_tap_cfg1_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_TAP_CFG1, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.tap_ths_x = val;
      ret = lsm6dsox_write_reg(LSM6DSOX_TAP_CFG1, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_tap_threshold_x_get(uint8_t *val)
{
   lsm6dsox_tap_cfg1_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_TAP_CFG1, (uint8_t*)&reg, 1);
   *val = reg.tap_ths_x;
   return ret;
}

static nrfx_err_t lsm6dsox_tap_axis_priority_set(lsm6dsox_tap_priority_t val)
{
   lsm6dsox_tap_cfg1_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_TAP_CFG1, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.tap_priority = (uint8_t)val;
      ret = lsm6dsox_write_reg(LSM6DSOX_TAP_CFG1, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_tap_axis_priority_get(lsm6dsox_tap_priority_t *val)
{
   lsm6dsox_tap_cfg1_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_TAP_CFG1, (uint8_t*)&reg, 1);
   switch (reg.tap_priority)
   {
      case LSM6DSOX_XYZ:
         *val = LSM6DSOX_XYZ;
         break;
      case LSM6DSOX_YXZ:
         *val = LSM6DSOX_YXZ;
         break;
      case LSM6DSOX_XZY:
         *val = LSM6DSOX_XZY;
         break;
      case LSM6DSOX_ZYX:
         *val = LSM6DSOX_ZYX;
         break;
      case LSM6DSOX_YZX:
         *val = LSM6DSOX_YZX;
         break;
      case LSM6DSOX_ZXY:
         *val = LSM6DSOX_ZXY;
         break;
      default:
         *val = LSM6DSOX_XYZ;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_tap_threshold_y_set(uint8_t val)
{
   lsm6dsox_tap_cfg2_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_TAP_CFG2, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.tap_ths_y = val;
      ret = lsm6dsox_write_reg(LSM6DSOX_TAP_CFG2, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_tap_threshold_y_get(uint8_t *val)
{
   lsm6dsox_tap_cfg2_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_TAP_CFG2, (uint8_t*)&reg, 1);
   *val = reg.tap_ths_y;
   return ret;
}

static nrfx_err_t lsm6dsox_tap_threshold_z_set(uint8_t val)
{
   lsm6dsox_tap_ths_6d_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_TAP_THS_6D, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.tap_ths_z = val;
      ret = lsm6dsox_write_reg(LSM6DSOX_TAP_THS_6D, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_tap_threshold_z_get(uint8_t *val)
{
   lsm6dsox_tap_ths_6d_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_TAP_THS_6D, (uint8_t*)&reg, 1);
   *val = reg.tap_ths_z;
   return ret;
}

static nrfx_err_t lsm6dsox_tap_shock_set(uint8_t val)
{
   lsm6dsox_int_dur2_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_INT_DUR2, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.shock = val;
      ret = lsm6dsox_write_reg(LSM6DSOX_INT_DUR2, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_tap_shock_get(uint8_t *val)
{
   lsm6dsox_int_dur2_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_INT_DUR2, (uint8_t*)&reg, 1);
   *val = reg.shock;
   return ret;
}

static nrfx_err_t lsm6dsox_tap_quiet_set(uint8_t val)
{
   lsm6dsox_int_dur2_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_INT_DUR2, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.quiet = val;
      ret = lsm6dsox_write_reg(LSM6DSOX_INT_DUR2, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_tap_quiet_get(uint8_t *val)
{
   lsm6dsox_int_dur2_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_INT_DUR2, (uint8_t*)&reg, 1);
   *val = reg.quiet;
   return ret;
}

static nrfx_err_t lsm6dsox_tap_dur_set(uint8_t val)
{
   lsm6dsox_int_dur2_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_INT_DUR2, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.dur = val;
      ret = lsm6dsox_write_reg(LSM6DSOX_INT_DUR2, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_tap_dur_get(uint8_t *val)
{
   lsm6dsox_int_dur2_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_INT_DUR2, (uint8_t*)&reg, 1);
   *val = reg.dur;
   return ret;
}

static nrfx_err_t lsm6dsox_tap_mode_set(lsm6dsox_single_double_tap_t val)
{
   lsm6dsox_wake_up_ths_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_WAKE_UP_THS, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.single_double_tap = (uint8_t)val;
      ret = lsm6dsox_write_reg(LSM6DSOX_WAKE_UP_THS, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_tap_mode_get(lsm6dsox_single_double_tap_t *val)
{
   lsm6dsox_wake_up_ths_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_WAKE_UP_THS, (uint8_t*)&reg, 1);
   switch (reg.single_double_tap)
   {
      case LSM6DSOX_ONLY_SINGLE:
         *val = LSM6DSOX_ONLY_SINGLE;
         break;
      case LSM6DSOX_BOTH_SINGLE_DOUBLE:
         *val = LSM6DSOX_BOTH_SINGLE_DOUBLE;
         break;
      default:
         *val = LSM6DSOX_ONLY_SINGLE;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_6d_threshold_set(lsm6dsox_sixd_ths_t val)
{
   lsm6dsox_tap_ths_6d_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_TAP_THS_6D, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.sixd_ths = (uint8_t)val;
      ret = lsm6dsox_write_reg(LSM6DSOX_TAP_THS_6D, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_6d_threshold_get(lsm6dsox_sixd_ths_t *val)
{
   lsm6dsox_tap_ths_6d_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_TAP_THS_6D, (uint8_t*)&reg, 1);
   switch (reg.sixd_ths)
   {
      case LSM6DSOX_DEG_80:
         *val = LSM6DSOX_DEG_80;
         break;
      case LSM6DSOX_DEG_70:
         *val = LSM6DSOX_DEG_70;
         break;
      case LSM6DSOX_DEG_60:
         *val = LSM6DSOX_DEG_60;
         break;
      case LSM6DSOX_DEG_50:
         *val = LSM6DSOX_DEG_50;
         break;
      default:
         *val = LSM6DSOX_DEG_80;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_4d_mode_set(uint8_t val)
{
   lsm6dsox_tap_ths_6d_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_TAP_THS_6D, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.d4d_en = val;
      ret = lsm6dsox_write_reg(LSM6DSOX_TAP_THS_6D, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_4d_mode_get(uint8_t *val)
{
   lsm6dsox_tap_ths_6d_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_TAP_THS_6D, (uint8_t*)&reg, 1);
   *val = reg.d4d_en;
   return ret;
}

static nrfx_err_t lsm6dsox_ff_threshold_set(lsm6dsox_ff_ths_t val)
{
   lsm6dsox_free_fall_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_FREE_FALL, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.ff_ths = (uint8_t)val;
      ret = lsm6dsox_write_reg(LSM6DSOX_FREE_FALL, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_ff_threshold_get(lsm6dsox_ff_ths_t *val)
{
   lsm6dsox_free_fall_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_FREE_FALL, (uint8_t*)&reg, 1);
   switch (reg.ff_ths)
   {
      case LSM6DSOX_FF_TSH_156mg:
         *val = LSM6DSOX_FF_TSH_156mg;
         break;
      case LSM6DSOX_FF_TSH_219mg:
         *val = LSM6DSOX_FF_TSH_219mg;
         break;
      case LSM6DSOX_FF_TSH_250mg:
         *val = LSM6DSOX_FF_TSH_250mg;
         break;
      case LSM6DSOX_FF_TSH_312mg:
         *val = LSM6DSOX_FF_TSH_312mg;
         break;
      case LSM6DSOX_FF_TSH_344mg:
         *val = LSM6DSOX_FF_TSH_344mg;
         break;
      case LSM6DSOX_FF_TSH_406mg:
         *val = LSM6DSOX_FF_TSH_406mg;
         break;
      case LSM6DSOX_FF_TSH_469mg:
         *val = LSM6DSOX_FF_TSH_469mg;
         break;
      case LSM6DSOX_FF_TSH_500mg:
         *val = LSM6DSOX_FF_TSH_500mg;
         break;
      default:
         *val = LSM6DSOX_FF_TSH_156mg;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_ff_dur_set(uint8_t val)
{
   lsm6dsox_wake_up_dur_t wake_up_dur;
   lsm6dsox_free_fall_t free_fall;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_WAKE_UP_DUR, (uint8_t*)&wake_up_dur, 1);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_FREE_FALL, (uint8_t*)&free_fall, 1);
   if (ret == NRFX_SUCCESS)
   {
      wake_up_dur.ff_dur = ((uint8_t)val & 0x20U) >> 5;
      free_fall.ff_dur = (uint8_t)val & 0x1FU;
      ret = lsm6dsox_write_reg(LSM6DSOX_WAKE_UP_DUR, (uint8_t*)&wake_up_dur, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_write_reg(LSM6DSOX_FREE_FALL, (uint8_t*)&free_fall, 1);
   return ret;
}

static nrfx_err_t lsm6dsox_ff_dur_get(uint8_t *val)
{
   lsm6dsox_wake_up_dur_t wake_up_dur;
   lsm6dsox_free_fall_t free_fall;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_WAKE_UP_DUR, (uint8_t*)&wake_up_dur, 1);
   if (ret == NRFX_SUCCESS)
   {
      ret = lsm6dsox_read_reg(LSM6DSOX_FREE_FALL, (uint8_t*)&free_fall, 1);
      *val = (wake_up_dur.ff_dur << 5) + free_fall.ff_dur;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_fifo_watermark_set(uint16_t val)
{
   lsm6dsox_fifo_ctrl1_t fifo_ctrl1;
   lsm6dsox_fifo_ctrl2_t fifo_ctrl2;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_FIFO_CTRL2, (uint8_t*)&fifo_ctrl2, 1);
   if (ret == NRFX_SUCCESS)
   {
      fifo_ctrl1.wtm = 0x00FFU & (uint8_t)val;
      fifo_ctrl2.wtm = (uint8_t)(( 0x0100U & val ) >> 8);
      ret = lsm6dsox_write_reg(LSM6DSOX_FIFO_CTRL1, (uint8_t*)&fifo_ctrl1, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_write_reg(LSM6DSOX_FIFO_CTRL2, (uint8_t*)&fifo_ctrl2, 1);
   return ret;
}

static nrfx_err_t lsm6dsox_fifo_watermark_get(uint16_t *val)
{
   lsm6dsox_fifo_ctrl1_t fifo_ctrl1;
   lsm6dsox_fifo_ctrl2_t fifo_ctrl2;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_FIFO_CTRL1, (uint8_t*)&fifo_ctrl1, 1);
   if (ret == NRFX_SUCCESS)
   {
      ret = lsm6dsox_read_reg(LSM6DSOX_FIFO_CTRL2, (uint8_t*)&fifo_ctrl2, 1);
      *val = ((uint16_t)fifo_ctrl2.wtm << 8) + (uint16_t)fifo_ctrl1.wtm;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_compression_algo_init_set(uint8_t val)
{
   lsm6dsox_emb_func_init_b_t reg;
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_EMBEDDED_FUNC_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_EMB_FUNC_INIT_B, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.fifo_compr_init = val;
      ret = lsm6dsox_write_reg(LSM6DSOX_EMB_FUNC_INIT_B, (uint8_t*)&reg, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   return ret;
}

static nrfx_err_t lsm6dsox_compression_algo_init_get(uint8_t *val)
{
   lsm6dsox_emb_func_init_b_t reg;
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_EMBEDDED_FUNC_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_EMB_FUNC_INIT_B, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      *val = reg.fifo_compr_init;
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_compression_algo_set(lsm6dsox_uncoptr_rate_t val)
{
   lsm6dsox_fifo_ctrl2_t fifo_ctrl2;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_FIFO_CTRL2, (uint8_t*)&fifo_ctrl2, 1);
   if (ret == NRFX_SUCCESS)
   {
      fifo_ctrl2.fifo_compr_rt_en = ((uint8_t)val & 0x04U) >> 2;
      fifo_ctrl2.uncoptr_rate = (uint8_t)val & 0x03U;
      ret = lsm6dsox_write_reg(LSM6DSOX_FIFO_CTRL2, (uint8_t*)&fifo_ctrl2, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_compression_algo_get(lsm6dsox_uncoptr_rate_t *val)
{
   lsm6dsox_fifo_ctrl2_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_FIFO_CTRL2, (uint8_t*)&reg, 1);
   switch ((reg.fifo_compr_rt_en << 2) | reg.uncoptr_rate)
   {
      case LSM6DSOX_CMP_DISABLE:
         *val = LSM6DSOX_CMP_DISABLE;
         break;
      case LSM6DSOX_CMP_ALWAYS:
         *val = LSM6DSOX_CMP_ALWAYS;
         break;
      case LSM6DSOX_CMP_8_TO_1:
         *val = LSM6DSOX_CMP_8_TO_1;
         break;
      case LSM6DSOX_CMP_16_TO_1:
         *val = LSM6DSOX_CMP_16_TO_1;
         break;
      case LSM6DSOX_CMP_32_TO_1:
         *val = LSM6DSOX_CMP_32_TO_1;
         break;
      default:
         *val = LSM6DSOX_CMP_DISABLE;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_fifo_virtual_sens_odr_chg_set(uint8_t val)
{
   lsm6dsox_fifo_ctrl2_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_FIFO_CTRL2, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.odrchg_en = val;
      ret = lsm6dsox_write_reg(LSM6DSOX_FIFO_CTRL2, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_fifo_virtual_sens_odr_chg_get(uint8_t *val)
{
   lsm6dsox_fifo_ctrl2_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_FIFO_CTRL2, (uint8_t*)&reg, 1);
   *val = reg.odrchg_en;
   return ret;
}

static nrfx_err_t lsm6dsox_compression_algo_real_time_set(uint8_t val)
{
   lsm6dsox_fifo_ctrl2_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_FIFO_CTRL2, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.fifo_compr_rt_en = val;
      ret = lsm6dsox_write_reg(LSM6DSOX_FIFO_CTRL2, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_compression_algo_real_time_get(uint8_t *val)
{
   lsm6dsox_fifo_ctrl2_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_FIFO_CTRL2, (uint8_t*)&reg, 1);
   *val = reg.fifo_compr_rt_en;
   return ret;
}

static nrfx_err_t lsm6dsox_fifo_stop_on_wtm_set(uint8_t val)
{
   lsm6dsox_fifo_ctrl2_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_FIFO_CTRL2, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.stop_on_wtm = val;
      ret = lsm6dsox_write_reg(LSM6DSOX_FIFO_CTRL2, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_fifo_stop_on_wtm_get(uint8_t *val)
{
   lsm6dsox_fifo_ctrl2_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_FIFO_CTRL2, (uint8_t*)&reg, 1);
   *val = reg.stop_on_wtm;
   return ret;
}

static nrfx_err_t lsm6dsox_fifo_xl_batch_set(lsm6dsox_bdr_xl_t val)
{
   lsm6dsox_fifo_ctrl3_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_FIFO_CTRL3, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.bdr_xl = (uint8_t)val;
      ret = lsm6dsox_write_reg(LSM6DSOX_FIFO_CTRL3, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_fifo_xl_batch_get(lsm6dsox_bdr_xl_t *val)
{
   lsm6dsox_fifo_ctrl3_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_FIFO_CTRL3, (uint8_t*)&reg, 1);
   switch (reg.bdr_xl)
   {
      case LSM6DSOX_XL_NOT_BATCHED:
         *val = LSM6DSOX_XL_NOT_BATCHED;
         break;
      case LSM6DSOX_XL_BATCHED_AT_12Hz5:
         *val = LSM6DSOX_XL_BATCHED_AT_12Hz5;
         break;
      case LSM6DSOX_XL_BATCHED_AT_26Hz:
         *val = LSM6DSOX_XL_BATCHED_AT_26Hz;
         break;
      case LSM6DSOX_XL_BATCHED_AT_52Hz:
         *val = LSM6DSOX_XL_BATCHED_AT_52Hz;
         break;
      case LSM6DSOX_XL_BATCHED_AT_104Hz:
         *val = LSM6DSOX_XL_BATCHED_AT_104Hz;
         break;
      case LSM6DSOX_XL_BATCHED_AT_208Hz:
         *val = LSM6DSOX_XL_BATCHED_AT_208Hz;
         break;
      case LSM6DSOX_XL_BATCHED_AT_417Hz:
         *val = LSM6DSOX_XL_BATCHED_AT_417Hz;
         break;
      case LSM6DSOX_XL_BATCHED_AT_833Hz:
         *val = LSM6DSOX_XL_BATCHED_AT_833Hz;
         break;
      case LSM6DSOX_XL_BATCHED_AT_1667Hz:
         *val = LSM6DSOX_XL_BATCHED_AT_1667Hz;
         break;
      case LSM6DSOX_XL_BATCHED_AT_3333Hz:
         *val = LSM6DSOX_XL_BATCHED_AT_3333Hz;
         break;
      case LSM6DSOX_XL_BATCHED_AT_6667Hz:
         *val = LSM6DSOX_XL_BATCHED_AT_6667Hz;
         break;
      case LSM6DSOX_XL_BATCHED_AT_6Hz5:
         *val = LSM6DSOX_XL_BATCHED_AT_6Hz5;
         break;
      default:
         *val = LSM6DSOX_XL_NOT_BATCHED;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_fifo_gy_batch_set(lsm6dsox_bdr_gy_t val)
{
   lsm6dsox_fifo_ctrl3_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_FIFO_CTRL3, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.bdr_gy = (uint8_t)val;
      ret = lsm6dsox_write_reg(LSM6DSOX_FIFO_CTRL3, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_fifo_gy_batch_get(lsm6dsox_bdr_gy_t *val)
{
   lsm6dsox_fifo_ctrl3_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_FIFO_CTRL3, (uint8_t*)&reg, 1);
   switch (reg.bdr_gy)
   {
      case LSM6DSOX_GY_NOT_BATCHED:
         *val = LSM6DSOX_GY_NOT_BATCHED;
         break;
      case LSM6DSOX_GY_BATCHED_AT_12Hz5:
         *val = LSM6DSOX_GY_BATCHED_AT_12Hz5;
         break;
      case LSM6DSOX_GY_BATCHED_AT_26Hz:
         *val = LSM6DSOX_GY_BATCHED_AT_26Hz;
         break;
      case LSM6DSOX_GY_BATCHED_AT_52Hz:
         *val = LSM6DSOX_GY_BATCHED_AT_52Hz;
         break;
      case LSM6DSOX_GY_BATCHED_AT_104Hz:
         *val = LSM6DSOX_GY_BATCHED_AT_104Hz;
         break;
      case LSM6DSOX_GY_BATCHED_AT_208Hz:
         *val = LSM6DSOX_GY_BATCHED_AT_208Hz;
         break;
      case LSM6DSOX_GY_BATCHED_AT_417Hz:
         *val = LSM6DSOX_GY_BATCHED_AT_417Hz;
         break;
      case LSM6DSOX_GY_BATCHED_AT_833Hz:
         *val = LSM6DSOX_GY_BATCHED_AT_833Hz;
         break;
      case LSM6DSOX_GY_BATCHED_AT_1667Hz:
         *val = LSM6DSOX_GY_BATCHED_AT_1667Hz;
         break;
      case LSM6DSOX_GY_BATCHED_AT_3333Hz:
         *val = LSM6DSOX_GY_BATCHED_AT_3333Hz;
         break;
      case LSM6DSOX_GY_BATCHED_AT_6667Hz:
         *val = LSM6DSOX_GY_BATCHED_AT_6667Hz;
         break;
      case LSM6DSOX_GY_BATCHED_AT_6Hz5:
         *val = LSM6DSOX_GY_BATCHED_AT_6Hz5;
         break;
      default:
         *val = LSM6DSOX_GY_NOT_BATCHED;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_fifo_mode_set(lsm6dsox_fifo_mode_t val)
{
   lsm6dsox_fifo_ctrl4_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_FIFO_CTRL4, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.fifo_mode = (uint8_t)val;
      ret = lsm6dsox_write_reg(LSM6DSOX_FIFO_CTRL4, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_fifo_mode_get(lsm6dsox_fifo_mode_t *val)
{
   lsm6dsox_fifo_ctrl4_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_FIFO_CTRL4, (uint8_t*)&reg, 1);
   switch (reg.fifo_mode)
   {
      case LSM6DSOX_BYPASS_MODE:
         *val = LSM6DSOX_BYPASS_MODE;
         break;
      case LSM6DSOX_FIFO_MODE:
         *val = LSM6DSOX_FIFO_MODE;
         break;
      case LSM6DSOX_STREAM_TO_FIFO_MODE:
         *val = LSM6DSOX_STREAM_TO_FIFO_MODE;
         break;
      case LSM6DSOX_BYPASS_TO_STREAM_MODE:
         *val = LSM6DSOX_BYPASS_TO_STREAM_MODE;
         break;
      case LSM6DSOX_STREAM_MODE:
         *val = LSM6DSOX_STREAM_MODE;
         break;
      case LSM6DSOX_BYPASS_TO_FIFO_MODE:
         *val = LSM6DSOX_BYPASS_TO_FIFO_MODE;
         break;
      default:
         *val = LSM6DSOX_BYPASS_MODE;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_fifo_temp_batch_set(lsm6dsox_odr_t_batch_t val)
{
   lsm6dsox_fifo_ctrl4_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_FIFO_CTRL4, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.odr_t_batch = (uint8_t)val;
      ret = lsm6dsox_write_reg(LSM6DSOX_FIFO_CTRL4, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_fifo_temp_batch_get(lsm6dsox_odr_t_batch_t *val)
{
   lsm6dsox_fifo_ctrl4_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_FIFO_CTRL4, (uint8_t*)&reg, 1);
   switch (reg.odr_t_batch)
   {
      case LSM6DSOX_TEMP_NOT_BATCHED:
         *val = LSM6DSOX_TEMP_NOT_BATCHED;
         break;
      case LSM6DSOX_TEMP_BATCHED_AT_1Hz6:
         *val = LSM6DSOX_TEMP_BATCHED_AT_1Hz6;
         break;
      case LSM6DSOX_TEMP_BATCHED_AT_12Hz5:
         *val = LSM6DSOX_TEMP_BATCHED_AT_12Hz5;
         break;
      case LSM6DSOX_TEMP_BATCHED_AT_52Hz:
         *val = LSM6DSOX_TEMP_BATCHED_AT_52Hz;
         break;
      default:
         *val = LSM6DSOX_TEMP_NOT_BATCHED;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_fifo_timestamp_decimation_set(lsm6dsox_odr_ts_batch_t val)
{
   lsm6dsox_fifo_ctrl4_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_FIFO_CTRL4, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.odr_ts_batch = (uint8_t)val;
      ret = lsm6dsox_write_reg(LSM6DSOX_FIFO_CTRL4, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_fifo_timestamp_decimation_get(lsm6dsox_odr_ts_batch_t *val)
{
   lsm6dsox_fifo_ctrl4_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_FIFO_CTRL4, (uint8_t*)&reg, 1);
   switch (reg.odr_ts_batch)
   {
      case LSM6DSOX_NO_DECIMATION:
         *val = LSM6DSOX_NO_DECIMATION;
         break;
      case LSM6DSOX_DEC_1:
         *val = LSM6DSOX_DEC_1;
         break;
      case LSM6DSOX_DEC_8:
         *val = LSM6DSOX_DEC_8;
         break;
      case LSM6DSOX_DEC_32:
         *val = LSM6DSOX_DEC_32;
         break;
      default:
         *val = LSM6DSOX_NO_DECIMATION;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_fifo_cnt_event_batch_set(lsm6dsox_trig_counter_bdr_t val)
{
   lsm6dsox_counter_bdr_reg1_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_COUNTER_BDR_REG1, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.trig_counter_bdr = (uint8_t)val;
      ret = lsm6dsox_write_reg(LSM6DSOX_COUNTER_BDR_REG1, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_fifo_cnt_event_batch_get(lsm6dsox_trig_counter_bdr_t *val)
{
   lsm6dsox_counter_bdr_reg1_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_COUNTER_BDR_REG1, (uint8_t*)&reg, 1);
   switch (reg.trig_counter_bdr)
   {
      case LSM6DSOX_XL_BATCH_EVENT:
         *val = LSM6DSOX_XL_BATCH_EVENT;
         break;
      case LSM6DSOX_GYRO_BATCH_EVENT:
         *val = LSM6DSOX_GYRO_BATCH_EVENT;
         break;
      default:
         *val = LSM6DSOX_XL_BATCH_EVENT;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_rst_batch_counter_set(uint8_t val)
{
   lsm6dsox_counter_bdr_reg1_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_COUNTER_BDR_REG1, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.rst_counter_bdr = val;
      ret = lsm6dsox_write_reg(LSM6DSOX_COUNTER_BDR_REG1, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_rst_batch_counter_get(uint8_t *val)
{
   lsm6dsox_counter_bdr_reg1_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_COUNTER_BDR_REG1, (uint8_t*)&reg, 1);
   *val = reg.rst_counter_bdr;
   return ret;
}

static nrfx_err_t lsm6dsox_batch_counter_threshold_set(uint16_t val)
{
   lsm6dsox_counter_bdr_reg1_t counter_bdr_reg1;
   lsm6dsox_counter_bdr_reg2_t counter_bdr_reg2;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_COUNTER_BDR_REG1, (uint8_t*)&counter_bdr_reg1, 1);
   if (ret == NRFX_SUCCESS)
   {
      counter_bdr_reg2.cnt_bdr_th = 0x00FFU & (uint8_t)val;
      counter_bdr_reg1.cnt_bdr_th = (uint8_t)(0x0700U & val) >> 8;
      ret = lsm6dsox_write_reg(LSM6DSOX_COUNTER_BDR_REG1, (uint8_t*)&counter_bdr_reg1, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_write_reg(LSM6DSOX_COUNTER_BDR_REG2, (uint8_t*)&counter_bdr_reg2, 1);
   return ret;
}

static nrfx_err_t lsm6dsox_batch_counter_threshold_get(uint16_t *val)
{
   lsm6dsox_counter_bdr_reg1_t counter_bdr_reg1;
   lsm6dsox_counter_bdr_reg2_t counter_bdr_reg2;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_COUNTER_BDR_REG1, (uint8_t*)&counter_bdr_reg1, 1);
   if (ret == NRFX_SUCCESS)
   {
      ret = lsm6dsox_read_reg(LSM6DSOX_COUNTER_BDR_REG2, (uint8_t*)&counter_bdr_reg2, 1);
      *val = ((uint16_t)counter_bdr_reg1.cnt_bdr_th << 8) + (uint16_t)counter_bdr_reg2.cnt_bdr_th;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_fifo_data_level_get(uint16_t *val)
{
   lsm6dsox_fifo_status1_t fifo_status1;
   lsm6dsox_fifo_status2_t fifo_status2;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_FIFO_STATUS1, (uint8_t*)&fifo_status1, 1);
   if (ret == NRFX_SUCCESS)
   {
      ret = lsm6dsox_read_reg(LSM6DSOX_FIFO_STATUS2, (uint8_t*)&fifo_status2, 1);
      *val = ((uint16_t)fifo_status2.diff_fifo << 8) + (uint16_t)fifo_status1.diff_fifo;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_fifo_status_get(lsm6dsox_fifo_status2_t *val)
{
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_FIFO_STATUS2, (uint8_t*)val, 1);
   return ret;
}

static nrfx_err_t lsm6dsox_fifo_full_flag_get(uint8_t *val)
{
   lsm6dsox_fifo_status2_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_FIFO_STATUS2, (uint8_t*)&reg, 1);
   *val = reg.fifo_full_ia;
   return ret;
}

static nrfx_err_t lsm6dsox_fifo_ovr_flag_get(uint8_t *val)
{
   lsm6dsox_fifo_status2_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_FIFO_STATUS2, (uint8_t*)&reg, 1);
   *val = reg.fifo_ovr_ia;
   return ret;
}

static nrfx_err_t lsm6dsox_fifo_wtm_flag_get(uint8_t *val)
{
   lsm6dsox_fifo_status2_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_FIFO_STATUS2, (uint8_t*)&reg, 1);
   *val = reg.fifo_wtm_ia;
   return ret;
}

static nrfx_err_t lsm6dsox_fifo_sensor_tag_get(lsm6dsox_fifo_tag_t *val)
{
   lsm6dsox_fifo_data_out_tag_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_FIFO_DATA_OUT_TAG, (uint8_t*)&reg, 1);
   switch (reg.tag_sensor)
   {
      case LSM6DSOX_GYRO_NC_TAG:
         *val = LSM6DSOX_GYRO_NC_TAG;
         break;
      case LSM6DSOX_XL_NC_TAG:
         *val = LSM6DSOX_XL_NC_TAG;
         break;
      case LSM6DSOX_TEMPERATURE_TAG:
         *val = LSM6DSOX_TEMPERATURE_TAG;
         break;
      case LSM6DSOX_TIMESTAMP_TAG:
         *val = LSM6DSOX_TIMESTAMP_TAG;
         break;
      case LSM6DSOX_CFG_CHANGE_TAG:
         *val = LSM6DSOX_CFG_CHANGE_TAG;
         break;
      case LSM6DSOX_XL_NC_T_2_TAG:
         *val = LSM6DSOX_XL_NC_T_2_TAG;
         break;
      case LSM6DSOX_XL_NC_T_1_TAG:
         *val = LSM6DSOX_XL_NC_T_1_TAG;
         break;
      case LSM6DSOX_XL_2XC_TAG:
         *val = LSM6DSOX_XL_2XC_TAG;
         break;
      case LSM6DSOX_XL_3XC_TAG:
         *val = LSM6DSOX_XL_3XC_TAG;
         break;
      case LSM6DSOX_GYRO_NC_T_2_TAG:
         *val = LSM6DSOX_GYRO_NC_T_2_TAG;
         break;
      case LSM6DSOX_GYRO_NC_T_1_TAG:
         *val = LSM6DSOX_GYRO_NC_T_1_TAG;
         break;
      case LSM6DSOX_GYRO_2XC_TAG:
         *val = LSM6DSOX_GYRO_2XC_TAG;
         break;
      case LSM6DSOX_GYRO_3XC_TAG:
         *val = LSM6DSOX_GYRO_3XC_TAG;
         break;
      case LSM6DSOX_SENSORHUB_SLAVE0_TAG:
         *val = LSM6DSOX_SENSORHUB_SLAVE0_TAG;
         break;
      case LSM6DSOX_SENSORHUB_SLAVE1_TAG:
         *val = LSM6DSOX_SENSORHUB_SLAVE1_TAG;
         break;
      case LSM6DSOX_SENSORHUB_SLAVE2_TAG:
         *val = LSM6DSOX_SENSORHUB_SLAVE2_TAG;
         break;
      case LSM6DSOX_SENSORHUB_SLAVE3_TAG:
         *val = LSM6DSOX_SENSORHUB_SLAVE3_TAG;
         break;
      case LSM6DSOX_STEP_CPUNTER_TAG:
         *val = LSM6DSOX_STEP_CPUNTER_TAG;
         break;
      case LSM6DSOX_GAME_ROTATION_TAG:
         *val = LSM6DSOX_GAME_ROTATION_TAG;
         break;
      case LSM6DSOX_GEOMAG_ROTATION_TAG:
         *val = LSM6DSOX_GEOMAG_ROTATION_TAG;
         break;
      case LSM6DSOX_ROTATION_TAG:
         *val = LSM6DSOX_ROTATION_TAG;
         break;
      case LSM6DSOX_SENSORHUB_NACK_TAG:
         *val = LSM6DSOX_SENSORHUB_NACK_TAG;
         break;
      default:
         *val = LSM6DSOX_GYRO_NC_TAG;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_fifo_pedo_batch_set(uint8_t val)
{
   lsm6dsox_emb_func_fifo_cfg_t reg;
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_EMBEDDED_FUNC_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_EMB_FUNC_FIFO_CFG, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.pedo_fifo_en = val;
      ret = lsm6dsox_write_reg(LSM6DSOX_EMB_FUNC_FIFO_CFG, (uint8_t*)&reg, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   return ret;
}

static nrfx_err_t lsm6dsox_fifo_pedo_batch_get(uint8_t *val)
{
   lsm6dsox_emb_func_fifo_cfg_t reg;
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_EMBEDDED_FUNC_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_EMB_FUNC_FIFO_CFG, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      *val = reg.pedo_fifo_en;
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_sh_batch_slave_0_set(uint8_t val)
{
   lsm6dsox_slv0_config_t reg;
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_SENSOR_HUB_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_SLV0_CONFIG, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.batch_ext_sens_0_en = val;
      ret = lsm6dsox_write_reg(LSM6DSOX_SLV0_CONFIG, (uint8_t*)&reg, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   return ret;
}

static nrfx_err_t lsm6dsox_sh_batch_slave_0_get(uint8_t *val)
{
   lsm6dsox_slv0_config_t reg;
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_SENSOR_HUB_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_SLV0_CONFIG, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      *val = reg.batch_ext_sens_0_en;
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_sh_batch_slave_1_set(uint8_t val)
{
   lsm6dsox_slv1_config_t reg;
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_SENSOR_HUB_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_SLV1_CONFIG, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.batch_ext_sens_1_en = val;
      ret = lsm6dsox_write_reg(LSM6DSOX_SLV1_CONFIG, (uint8_t*)&reg, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   return ret;
}

static nrfx_err_t lsm6dsox_sh_batch_slave_1_get(uint8_t *val)
{
   lsm6dsox_slv1_config_t reg;
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_SENSOR_HUB_BANK);
   if (ret == NRFX_SUCCESS)
   {
      ret = lsm6dsox_read_reg(LSM6DSOX_SLV1_CONFIG, (uint8_t*)&reg, 1);
      *val = reg.batch_ext_sens_1_en;
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   return ret;
}

static nrfx_err_t lsm6dsox_sh_batch_slave_2_set(uint8_t val)
{
   lsm6dsox_slv2_config_t reg;
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_SENSOR_HUB_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_SLV2_CONFIG, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.batch_ext_sens_2_en = val;
      ret = lsm6dsox_write_reg(LSM6DSOX_SLV2_CONFIG, (uint8_t*)&reg, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   return ret;
}

static nrfx_err_t lsm6dsox_sh_batch_slave_2_get(uint8_t *val)
{
   lsm6dsox_slv2_config_t reg;
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_SENSOR_HUB_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_SLV2_CONFIG, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      *val = reg.batch_ext_sens_2_en;
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_sh_batch_slave_3_set(uint8_t val)
{
   lsm6dsox_slv3_config_t reg;
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_SENSOR_HUB_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_SLV3_CONFIG, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.batch_ext_sens_3_en = val;
      ret = lsm6dsox_write_reg(LSM6DSOX_SLV3_CONFIG, (uint8_t*)&reg, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   return ret;
}

static nrfx_err_t lsm6dsox_sh_batch_slave_3_get(uint8_t *val)
{
   lsm6dsox_slv3_config_t reg;
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_SENSOR_HUB_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_SLV3_CONFIG, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      *val = reg.batch_ext_sens_3_en;
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_den_mode_set(lsm6dsox_den_mode_t val)
{
   lsm6dsox_ctrl6_c_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL6_C, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.den_mode = (uint8_t)val;
      ret = lsm6dsox_write_reg(LSM6DSOX_CTRL6_C, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_den_mode_get(lsm6dsox_den_mode_t *val)
{
   lsm6dsox_ctrl6_c_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL6_C, (uint8_t*)&reg, 1);
   switch (reg.den_mode)
   {
      case LSM6DSOX_DEN_DISABLE:
         *val = LSM6DSOX_DEN_DISABLE;
         break;
      case LSM6DSOX_LEVEL_FIFO:
         *val = LSM6DSOX_LEVEL_FIFO;
         break;
      case LSM6DSOX_LEVEL_LETCHED:
         *val = LSM6DSOX_LEVEL_LETCHED;
         break;
      case LSM6DSOX_LEVEL_TRIGGER:
         *val = LSM6DSOX_LEVEL_TRIGGER;
         break;
      case LSM6DSOX_EDGE_TRIGGER:
         *val = LSM6DSOX_EDGE_TRIGGER;
         break;
      default:
         *val = LSM6DSOX_DEN_DISABLE;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_den_polarity_set(lsm6dsox_den_lh_t val)
{
   lsm6dsox_ctrl9_xl_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL9_XL, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.den_lh = (uint8_t)val;
      ret = lsm6dsox_write_reg(LSM6DSOX_CTRL9_XL, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_den_polarity_get(lsm6dsox_den_lh_t *val)
{
   lsm6dsox_ctrl9_xl_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL9_XL, (uint8_t*)&reg, 1);
   switch (reg.den_lh)
   {
      case LSM6DSOX_DEN_ACT_LOW:
         *val = LSM6DSOX_DEN_ACT_LOW;
         break;
      case LSM6DSOX_DEN_ACT_HIGH:
         *val = LSM6DSOX_DEN_ACT_HIGH;
         break;
      default:
         *val = LSM6DSOX_DEN_ACT_LOW;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_den_enable_set(lsm6dsox_den_xl_g_t val)
{
   lsm6dsox_ctrl9_xl_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL9_XL, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.den_xl_g = (uint8_t)val;
      ret = lsm6dsox_write_reg(LSM6DSOX_CTRL9_XL, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_den_enable_get(lsm6dsox_den_xl_g_t *val)
{
   lsm6dsox_ctrl9_xl_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL9_XL, (uint8_t*)&reg, 1);
   switch (reg.den_xl_g)
   {
      case LSM6DSOX_STAMP_IN_GY_DATA:
         *val = LSM6DSOX_STAMP_IN_GY_DATA;
         break;
      case LSM6DSOX_STAMP_IN_XL_DATA:
         *val = LSM6DSOX_STAMP_IN_XL_DATA;
         break;
      case LSM6DSOX_STAMP_IN_GY_XL_DATA:
         *val = LSM6DSOX_STAMP_IN_GY_XL_DATA;
         break;
      default:
         *val = LSM6DSOX_STAMP_IN_GY_DATA;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_den_mark_axis_x_set(uint8_t val)
{
   lsm6dsox_ctrl9_xl_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL9_XL, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.den_z = val;
      ret = lsm6dsox_write_reg(LSM6DSOX_CTRL9_XL, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_den_mark_axis_x_get(uint8_t *val)
{
   lsm6dsox_ctrl9_xl_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL9_XL, (uint8_t*)&reg, 1);
   *val = reg.den_z;
   return ret;
}

static nrfx_err_t lsm6dsox_den_mark_axis_y_set(uint8_t val)
{
   lsm6dsox_ctrl9_xl_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL9_XL, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.den_y = val;
      ret = lsm6dsox_write_reg(LSM6DSOX_CTRL9_XL, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_den_mark_axis_y_get(uint8_t *val)
{
   lsm6dsox_ctrl9_xl_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL9_XL, (uint8_t*)&reg, 1);
   *val = reg.den_y;
   return ret;
}

static nrfx_err_t lsm6dsox_den_mark_axis_z_set(uint8_t val)
{
   lsm6dsox_ctrl9_xl_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL9_XL, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.den_x = val;
      ret = lsm6dsox_write_reg(LSM6DSOX_CTRL9_XL, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_den_mark_axis_z_get(uint8_t *val)
{
   lsm6dsox_ctrl9_xl_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL9_XL, (uint8_t*)&reg, 1);
   *val = reg.den_x;
   return ret;
}

static nrfx_err_t lsm6dsox_pedo_sens_set(lsm6dsox_pedo_md_t val)
{
   lsm6dsox_pedo_cmd_reg_t pedo_cmd_reg;
   nrfx_err_t ret = lsm6dsox_ln_pg_read_byte(LSM6DSOX_PEDO_CMD_REG, (uint8_t*)&pedo_cmd_reg);
   if (ret == NRFX_SUCCESS)
   {
      pedo_cmd_reg.fp_rejection_en = ((uint8_t)val & 0x10U) >> 4;
      pedo_cmd_reg.ad_det_en = ((uint8_t)val & 0x20U) >> 5;
      ret = lsm6dsox_ln_pg_write_byte(LSM6DSOX_PEDO_CMD_REG, (uint8_t*)&pedo_cmd_reg);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_pedo_sens_get(lsm6dsox_pedo_md_t *val)
{
   lsm6dsox_pedo_cmd_reg_t pedo_cmd_reg;
   nrfx_err_t ret = lsm6dsox_ln_pg_read_byte(LSM6DSOX_PEDO_CMD_REG, (uint8_t*)&pedo_cmd_reg);
   switch ((pedo_cmd_reg.ad_det_en << 5) | (pedo_cmd_reg.fp_rejection_en << 4))
   {
      case LSM6DSOX_PEDO_BASE_MODE:
         *val = LSM6DSOX_PEDO_BASE_MODE;
         break;
      case LSM6DSOX_FALSE_STEP_REJ:
         *val = LSM6DSOX_FALSE_STEP_REJ;
         break;
      case LSM6DSOX_FALSE_STEP_REJ_ADV_MODE:
         *val = LSM6DSOX_FALSE_STEP_REJ_ADV_MODE;
         break;
      default:
         *val = LSM6DSOX_PEDO_BASE_MODE;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_pedo_step_detect_get(uint8_t *val)
{
   lsm6dsox_emb_func_status_t reg;
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_EMBEDDED_FUNC_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_EMB_FUNC_STATUS, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      *val = reg.is_step_det;
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_pedo_debounce_steps_set(uint8_t *buff)
{
   nrfx_err_t ret = lsm6dsox_ln_pg_write_byte(LSM6DSOX_PEDO_DEB_STEPS_CONF, buff);
   return ret;
}

static nrfx_err_t lsm6dsox_pedo_debounce_steps_get(uint8_t *buff)
{
   nrfx_err_t ret = lsm6dsox_ln_pg_read_byte(LSM6DSOX_PEDO_DEB_STEPS_CONF, buff);
   return ret;
}

static nrfx_err_t lsm6dsox_pedo_steps_period_set(uint16_t val)
{
   uint8_t buff[2];
   buff[1] = (uint8_t) (val / 256U);
   buff[0] = (uint8_t) (val - (buff[1] * 256U));
   nrfx_err_t ret = lsm6dsox_ln_pg_write_byte(LSM6DSOX_PEDO_SC_DELTAT_L, &buff[0]);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_ln_pg_write_byte(LSM6DSOX_PEDO_SC_DELTAT_H, &buff[1]);
   return ret;
}

static nrfx_err_t lsm6dsox_pedo_steps_period_get(uint16_t *val)
{
   uint8_t buff[2];
   nrfx_err_t ret = lsm6dsox_ln_pg_read_byte(LSM6DSOX_PEDO_SC_DELTAT_L, &buff[0]);
   if (ret == NRFX_SUCCESS)
   {
      ret = lsm6dsox_ln_pg_read_byte(LSM6DSOX_PEDO_SC_DELTAT_H, &buff[1]);
      *val = buff[1];
      *val = (*val * 256U) +  buff[0];
   }
   return ret;
}

static nrfx_err_t lsm6dsox_pedo_int_mode_set(lsm6dsox_carry_count_en_t val)
{
   lsm6dsox_pedo_cmd_reg_t reg;
   nrfx_err_t ret = lsm6dsox_ln_pg_read_byte(LSM6DSOX_PEDO_CMD_REG, (uint8_t*)&reg);
   if (ret == NRFX_SUCCESS)
   {
      reg.carry_count_en = (uint8_t)val;
      ret = lsm6dsox_ln_pg_write_byte(LSM6DSOX_PEDO_CMD_REG, (uint8_t*)&reg);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_pedo_int_mode_get(lsm6dsox_carry_count_en_t *val)
{
   lsm6dsox_pedo_cmd_reg_t reg;
   nrfx_err_t ret = lsm6dsox_ln_pg_read_byte(LSM6DSOX_PEDO_CMD_REG, (uint8_t*)&reg);
   switch (reg.carry_count_en)
   {
      case LSM6DSOX_EVERY_STEP:
         *val = LSM6DSOX_EVERY_STEP;
         break;
      case LSM6DSOX_COUNT_OVERFLOW:
         *val = LSM6DSOX_COUNT_OVERFLOW;
         break;
      default:
         *val = LSM6DSOX_EVERY_STEP;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_motion_flag_data_ready_get(uint8_t *val)
{
   lsm6dsox_emb_func_status_t reg;
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_EMBEDDED_FUNC_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_EMB_FUNC_STATUS, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      *val = reg.is_sigmot;
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_tilt_flag_data_ready_get(uint8_t *val)
{
   lsm6dsox_emb_func_status_t reg;
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_EMBEDDED_FUNC_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_EMB_FUNC_STATUS, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      *val = reg.is_tilt;
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_sh_mag_sensitivity_set(uint16_t val)
{
   uint8_t buff[2];
   buff[1] = (uint8_t) (val / 256U);
   buff[0] = (uint8_t) (val - (buff[1] * 256U));
   nrfx_err_t ret = lsm6dsox_ln_pg_write_byte(LSM6DSOX_MAG_SENSITIVITY_L, &buff[0]);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_ln_pg_write_byte(LSM6DSOX_MAG_SENSITIVITY_H, &buff[1]);
   return ret;
}

static nrfx_err_t lsm6dsox_sh_mag_sensitivity_get(uint16_t *val)
{
   uint8_t buff[2];
   nrfx_err_t ret = lsm6dsox_ln_pg_read_byte(LSM6DSOX_MAG_SENSITIVITY_L, &buff[0]);
   if (ret == NRFX_SUCCESS)
   {
      ret = lsm6dsox_ln_pg_read_byte(LSM6DSOX_MAG_SENSITIVITY_H, &buff[1]);
      *val = buff[1];
      *val = (*val * 256U) +  buff[0];
   }
   return ret;
}

static nrfx_err_t lsm6dsox_mlc_mag_sensitivity_set(uint16_t val)
{
   uint8_t buff[2];
   buff[1] = (uint8_t) (val / 256U);
   buff[0] = (uint8_t) (val - (buff[1] * 256U));
   nrfx_err_t ret = lsm6dsox_ln_pg_write_byte(LSM6DSOX_MLC_MAG_SENSITIVITY_L, &buff[0]);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_ln_pg_write_byte(LSM6DSOX_MLC_MAG_SENSITIVITY_H, &buff[1]);
   return ret;
}

static nrfx_err_t lsm6dsox_mlc_mag_sensitivity_get(uint16_t *val)
{
   uint8_t buff[2];
   nrfx_err_t ret = lsm6dsox_ln_pg_read_byte(LSM6DSOX_MLC_MAG_SENSITIVITY_L, &buff[0]);
   if (ret == NRFX_SUCCESS)
   {
      ret = lsm6dsox_ln_pg_read_byte(LSM6DSOX_MLC_MAG_SENSITIVITY_H, &buff[1]);
      *val = buff[1];
      *val = (*val * 256U) +  buff[0];
   }
   return ret;
}

static nrfx_err_t lsm6dsox_mag_offset_set(int16_t *val)
{
   uint8_t buff[6];
   uint8_t i;
   buff[1] = (uint8_t) ((uint16_t)val[0] / 256U);
   buff[0] = (uint8_t) ((uint16_t)val[0] - (buff[1] * 256U));
   buff[3] = (uint8_t) ((uint16_t)val[1] / 256U);
   buff[2] = (uint8_t) ((uint16_t)val[1] - (buff[3] * 256U));
   buff[5] = (uint8_t) ((uint16_t)val[2] / 256U);
   buff[4] = (uint8_t) ((uint16_t)val[2] - (buff[5] * 256U));
   i = 0x00U;
   nrfx_err_t ret = lsm6dsox_ln_pg_write_byte(LSM6DSOX_MAG_OFFX_L, &buff[i]);
   if (ret == NRFX_SUCCESS)
   {
      i++;
      ret = lsm6dsox_ln_pg_write_byte(LSM6DSOX_MAG_OFFX_H, &buff[i]);
   }
   if (ret == NRFX_SUCCESS)
   {
      i++;
      ret = lsm6dsox_ln_pg_write_byte(LSM6DSOX_MAG_OFFY_L, &buff[i]);
   }
   if (ret == NRFX_SUCCESS)
   {
      i++;
      ret = lsm6dsox_ln_pg_write_byte(LSM6DSOX_MAG_OFFY_H, &buff[i]);
   }
   if (ret == NRFX_SUCCESS)
   {
      i++;
      ret = lsm6dsox_ln_pg_write_byte(LSM6DSOX_MAG_OFFZ_L, &buff[i]);
   }
   if (ret == NRFX_SUCCESS)
   {
      i++;
      ret = lsm6dsox_ln_pg_write_byte(LSM6DSOX_MAG_OFFZ_H, &buff[i]);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_mag_offset_get(int16_t *val)
{
   uint8_t buff[6];
   uint8_t i = 0x00U;
   nrfx_err_t ret = lsm6dsox_ln_pg_read_byte(LSM6DSOX_MAG_OFFX_L, &buff[i]);
   if (ret == NRFX_SUCCESS)
   {
      i++;
      ret = lsm6dsox_ln_pg_read_byte(LSM6DSOX_MAG_OFFX_H, &buff[i]);
   }
   if (ret == NRFX_SUCCESS)
   {
      i++;
      ret = lsm6dsox_ln_pg_read_byte(LSM6DSOX_MAG_OFFY_L, &buff[i]);
   }
   if (ret == NRFX_SUCCESS)
   {
      i++;
      ret = lsm6dsox_ln_pg_read_byte(LSM6DSOX_MAG_OFFY_H, &buff[i]);
   }
   if (ret == NRFX_SUCCESS)
   {
      i++;
      ret = lsm6dsox_ln_pg_read_byte(LSM6DSOX_MAG_OFFZ_L, &buff[i]);
   }
   if (ret == NRFX_SUCCESS)
   {
      i++;
      ret = lsm6dsox_ln_pg_read_byte(LSM6DSOX_MAG_OFFZ_H, &buff[i]);
   }
   val[0] = (int16_t)buff[1];
   val[0] = (val[0] * 256) + (int16_t)buff[0];
   val[1] = (int16_t)buff[3];
   val[1] = (val[1] * 256) + (int16_t)buff[2];
   val[2] = (int16_t)buff[5];
   val[2] = (val[2] * 256) + (int16_t)buff[4];
   return ret;
}

static nrfx_err_t lsm6dsox_mag_soft_iron_set(uint16_t *val)
{
   uint8_t buff[12];
   uint8_t index;
   buff[1] = (uint8_t) (val[0] / 256U);
   buff[0] = (uint8_t) (val[0] - (buff[1] * 256U));
   buff[3] = (uint8_t) (val[1] / 256U);
   buff[2] = (uint8_t) (val[1] - (buff[3] * 256U));
   buff[5] = (uint8_t) (val[2] / 256U);
   buff[4] = (uint8_t) (val[2] - (buff[5] * 256U));
   buff[7] = (uint8_t) (val[3] / 256U);
   buff[6] = (uint8_t) (val[3] - (buff[1] * 256U));
   buff[9] = (uint8_t) (val[4] / 256U);
   buff[8] = (uint8_t) (val[4] - (buff[3] * 256U));
   buff[11] = (uint8_t) (val[5] / 256U);
   buff[10] = (uint8_t) (val[5] - (buff[5] * 256U));
   index = 0x00U;
   nrfx_err_t ret = lsm6dsox_ln_pg_write_byte(LSM6DSOX_MAG_SI_XX_L, &buff[index]);
   if (ret == NRFX_SUCCESS)
   {
      index++;
      ret = lsm6dsox_ln_pg_write_byte(LSM6DSOX_MAG_SI_XX_H, &buff[index]);
   }
   if (ret == NRFX_SUCCESS)
   {
      index++;
      ret = lsm6dsox_ln_pg_write_byte(LSM6DSOX_MAG_SI_XY_L, &buff[index]);
   }
   if (ret == NRFX_SUCCESS)
   {
      index++;
      ret = lsm6dsox_ln_pg_write_byte(LSM6DSOX_MAG_SI_XY_H, &buff[index]);
   }
   if (ret == NRFX_SUCCESS)
   {
      index++;
      ret = lsm6dsox_ln_pg_write_byte(LSM6DSOX_MAG_SI_XZ_L, &buff[index]);
   }
   if (ret == NRFX_SUCCESS)
   {
      index++;
      ret = lsm6dsox_ln_pg_write_byte(LSM6DSOX_MAG_SI_XZ_H, &buff[index]);
   }
   if (ret == NRFX_SUCCESS)
   {
      index++;
      ret = lsm6dsox_ln_pg_write_byte(LSM6DSOX_MAG_SI_YY_L, &buff[index]);
   }
   if (ret == NRFX_SUCCESS)
   {
      index++;
      ret = lsm6dsox_ln_pg_write_byte(LSM6DSOX_MAG_SI_YY_H, &buff[index]);
   }
   if (ret == NRFX_SUCCESS)
   {
      index++;
      ret = lsm6dsox_ln_pg_write_byte(LSM6DSOX_MAG_SI_YZ_L, &buff[index]);
   }
   if (ret == NRFX_SUCCESS)
   {
      index++;
      ret = lsm6dsox_ln_pg_write_byte(LSM6DSOX_MAG_SI_YZ_H, &buff[index]);
   }
   if (ret == NRFX_SUCCESS)
   {
      index++;
      ret = lsm6dsox_ln_pg_write_byte(LSM6DSOX_MAG_SI_ZZ_L, &buff[index]);
   }
   if (ret == NRFX_SUCCESS)
   {
      index++;
      ret = lsm6dsox_ln_pg_write_byte(LSM6DSOX_MAG_SI_ZZ_H, &buff[index]);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_mag_soft_iron_get(uint16_t *val)
{
   uint8_t buff[12];
   uint8_t index = 0x00U;
   nrfx_err_t ret = lsm6dsox_ln_pg_read_byte(LSM6DSOX_MAG_SI_XX_L, &buff[index]);
   if (ret == NRFX_SUCCESS)
   {
      index++;
      ret = lsm6dsox_ln_pg_read_byte(LSM6DSOX_MAG_SI_XX_H, &buff[index]);
   }
   if (ret == NRFX_SUCCESS)
   {
      index++;
      ret = lsm6dsox_ln_pg_read_byte(LSM6DSOX_MAG_SI_XY_L, &buff[index]);
   }
   if (ret == NRFX_SUCCESS)
   {
      index++;
      ret = lsm6dsox_ln_pg_read_byte(LSM6DSOX_MAG_SI_XY_H, &buff[index]);
   }
   if (ret == NRFX_SUCCESS)
   {
      index++;
      ret = lsm6dsox_ln_pg_read_byte(LSM6DSOX_MAG_SI_XZ_L, &buff[index]);
   }
   if (ret == NRFX_SUCCESS)
   {
      index++;
      ret = lsm6dsox_ln_pg_read_byte(LSM6DSOX_MAG_SI_XZ_H, &buff[index]);
   }
   if (ret == NRFX_SUCCESS)
   {
      index++;
      ret = lsm6dsox_ln_pg_read_byte(LSM6DSOX_MAG_SI_YY_L, &buff[index]);
   }
   if (ret == NRFX_SUCCESS)
   {
      index++;
      ret = lsm6dsox_ln_pg_read_byte(LSM6DSOX_MAG_SI_YY_H, &buff[index]);
   }
   if (ret == NRFX_SUCCESS)
   {
      index++;
      ret = lsm6dsox_ln_pg_read_byte(LSM6DSOX_MAG_SI_YZ_L, &buff[index]);
   }
   if (ret == NRFX_SUCCESS)
   {
      index++;
      ret = lsm6dsox_ln_pg_read_byte(LSM6DSOX_MAG_SI_YZ_H, &buff[index]);
   }
   if (ret == NRFX_SUCCESS)
   {
      index++;
      ret = lsm6dsox_ln_pg_read_byte(LSM6DSOX_MAG_SI_ZZ_L, &buff[index]);
   }
   if (ret == NRFX_SUCCESS)
   {
      index++;
      ret = lsm6dsox_ln_pg_read_byte(LSM6DSOX_MAG_SI_ZZ_H, &buff[index]);
   }
   val[0] = buff[1];
   val[0] = (val[0] * 256U) +  buff[0];
   val[1] = buff[3];
   val[1] = (val[1] * 256U) +  buff[2];
   val[2] = buff[5];
   val[2] = (val[2] * 256U) +  buff[4];
   val[3] = buff[7];
   val[3] = (val[3] * 256U) +  buff[6];
   val[4] = buff[9];
   val[4] = (val[4] * 256U) +  buff[8];
   val[5] = buff[11];
   val[6] = (val[5] * 256U) +  buff[10];
   return ret;
}

static nrfx_err_t lsm6dsox_mag_z_orient_set(lsm6dsox_mag_z_axis_t val)
{
   lsm6dsox_mag_cfg_a_t reg;
   nrfx_err_t ret = lsm6dsox_ln_pg_read_byte(LSM6DSOX_MAG_CFG_A, (uint8_t*)&reg);
   if (ret == NRFX_SUCCESS)
   {
      reg.mag_z_axis = (uint8_t) val;
      ret = lsm6dsox_ln_pg_write_byte(LSM6DSOX_MAG_CFG_A, (uint8_t*)&reg);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_mag_z_orient_get(lsm6dsox_mag_z_axis_t *val)
{
   lsm6dsox_mag_cfg_a_t reg;
   nrfx_err_t ret = lsm6dsox_ln_pg_read_byte(LSM6DSOX_MAG_CFG_A, (uint8_t*)&reg);
   switch (reg.mag_z_axis)
   {
      case LSM6DSOX_Z_EQ_Y:
         *val = LSM6DSOX_Z_EQ_Y;
         break;
      case LSM6DSOX_Z_EQ_MIN_Y:
         *val = LSM6DSOX_Z_EQ_MIN_Y;
         break;
      case LSM6DSOX_Z_EQ_X:
         *val = LSM6DSOX_Z_EQ_X;
         break;
      case LSM6DSOX_Z_EQ_MIN_X:
         *val = LSM6DSOX_Z_EQ_MIN_X;
         break;
      case LSM6DSOX_Z_EQ_MIN_Z:
         *val = LSM6DSOX_Z_EQ_MIN_Z;
         break;
      case LSM6DSOX_Z_EQ_Z:
         *val = LSM6DSOX_Z_EQ_Z;
         break;
      default:
         *val = LSM6DSOX_Z_EQ_Y;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_mag_y_orient_set(lsm6dsox_mag_y_axis_t val)
{
   lsm6dsox_mag_cfg_a_t reg;
   nrfx_err_t ret = lsm6dsox_ln_pg_read_byte(LSM6DSOX_MAG_CFG_A, (uint8_t*)&reg);
   if (ret == NRFX_SUCCESS)
   {
      reg.mag_y_axis = (uint8_t)val;
      ret = lsm6dsox_ln_pg_write_byte(LSM6DSOX_MAG_CFG_A, (uint8_t*)&reg);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_mag_y_orient_get(lsm6dsox_mag_y_axis_t *val)
{
   lsm6dsox_mag_cfg_a_t reg;
   nrfx_err_t ret = lsm6dsox_ln_pg_read_byte(LSM6DSOX_MAG_CFG_A, (uint8_t*)&reg);
   switch (reg.mag_y_axis)
   {
      case LSM6DSOX_Y_EQ_Y:
         *val = LSM6DSOX_Y_EQ_Y;
         break;
      case LSM6DSOX_Y_EQ_MIN_Y:
         *val = LSM6DSOX_Y_EQ_MIN_Y;
         break;
      case LSM6DSOX_Y_EQ_X:
         *val = LSM6DSOX_Y_EQ_X;
         break;
      case LSM6DSOX_Y_EQ_MIN_X:
         *val = LSM6DSOX_Y_EQ_MIN_X;
         break;
      case LSM6DSOX_Y_EQ_MIN_Z:
         *val = LSM6DSOX_Y_EQ_MIN_Z;
         break;
      case LSM6DSOX_Y_EQ_Z:
         *val = LSM6DSOX_Y_EQ_Z;
         break;
      default:
         *val = LSM6DSOX_Y_EQ_Y;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_mag_x_orient_set(lsm6dsox_mag_x_axis_t val)
{
   lsm6dsox_mag_cfg_b_t reg;
   nrfx_err_t ret = lsm6dsox_ln_pg_read_byte(LSM6DSOX_MAG_CFG_B, (uint8_t*)&reg);
   if (ret == NRFX_SUCCESS)
   {
      reg.mag_x_axis = (uint8_t)val;
      ret = lsm6dsox_ln_pg_write_byte(LSM6DSOX_MAG_CFG_B, (uint8_t*)&reg);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_mag_x_orient_get(lsm6dsox_mag_x_axis_t *val)
{
   lsm6dsox_mag_cfg_b_t reg;
   nrfx_err_t ret = lsm6dsox_ln_pg_read_byte(LSM6DSOX_MAG_CFG_B, (uint8_t*)&reg);
   switch (reg.mag_x_axis)
   {
      case LSM6DSOX_X_EQ_Y:
         *val = LSM6DSOX_X_EQ_Y;
         break;
      case LSM6DSOX_X_EQ_MIN_Y:
         *val = LSM6DSOX_X_EQ_MIN_Y;
         break;
      case LSM6DSOX_X_EQ_X:
         *val = LSM6DSOX_X_EQ_X;
         break;
      case LSM6DSOX_X_EQ_MIN_X:
         *val = LSM6DSOX_X_EQ_MIN_X;
         break;
      case LSM6DSOX_X_EQ_MIN_Z:
         *val = LSM6DSOX_X_EQ_MIN_Z;
         break;
      case LSM6DSOX_X_EQ_Z:
         *val = LSM6DSOX_X_EQ_Z;
         break;
      default:
         *val = LSM6DSOX_X_EQ_Y;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_long_cnt_flag_data_ready_get(uint8_t *val)
{
   lsm6dsox_emb_func_status_t reg;
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_EMBEDDED_FUNC_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_EMB_FUNC_STATUS, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      *val = reg.is_fsm_lc;
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_long_cnt_set(uint16_t val)
{
   uint8_t buff[2];
   buff[1] = (uint8_t) (val / 256U);
   buff[0] = (uint8_t) (val - (buff[1] * 256U));
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_EMBEDDED_FUNC_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_write_reg(LSM6DSOX_FSM_LONG_COUNTER_L, buff, 2);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   return ret;
}

static nrfx_err_t lsm6dsox_long_cnt_get(uint16_t *val)
{
   uint8_t buff[2];
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_EMBEDDED_FUNC_BANK);
   if (ret == NRFX_SUCCESS)
   {
      ret = lsm6dsox_read_reg(LSM6DSOX_FSM_LONG_COUNTER_L, buff, 2);
      *val = buff[1];
      *val = (*val * 256U) +  buff[0];
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   return ret;
}

static nrfx_err_t lsm6dsox_long_clr_set(lsm6dsox_fsm_lc_clr_t val)
{
   lsm6dsox_fsm_long_counter_clear_t reg;
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_EMBEDDED_FUNC_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_FSM_LONG_COUNTER_CLEAR, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg. fsm_lc_clr = (uint8_t)val;
      ret = lsm6dsox_write_reg(LSM6DSOX_FSM_LONG_COUNTER_CLEAR, (uint8_t*)&reg, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   return ret;
}

static nrfx_err_t lsm6dsox_long_clr_get(lsm6dsox_fsm_lc_clr_t *val)
{
   lsm6dsox_fsm_long_counter_clear_t reg;
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_EMBEDDED_FUNC_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_FSM_LONG_COUNTER_CLEAR, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      switch (reg.fsm_lc_clr)
      {
         case LSM6DSOX_LC_NORMAL:
            *val = LSM6DSOX_LC_NORMAL;
            break;
         case LSM6DSOX_LC_CLEAR:
            *val = LSM6DSOX_LC_CLEAR;
            break;
         case LSM6DSOX_LC_CLEAR_DONE:
            *val = LSM6DSOX_LC_CLEAR_DONE;
            break;
         default:
            *val = LSM6DSOX_LC_NORMAL;
            break;
      }
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   return ret;
}

static nrfx_err_t lsm6dsox_long_cnt_int_value_set(uint16_t val)
{
   uint8_t add_l;
   uint8_t add_h;
   add_h = (uint8_t)( ( val & 0xFF00U ) >> 8 );
   add_l = (uint8_t)( val & 0x00FFU );
   nrfx_err_t ret = lsm6dsox_ln_pg_write_byte(LSM6DSOX_FSM_LC_TIMEOUT_L, &add_l);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_ln_pg_write_byte(LSM6DSOX_FSM_LC_TIMEOUT_H, &add_h);
   return ret;
}

static nrfx_err_t lsm6dsox_long_cnt_int_value_get(uint16_t *val)
{
   uint8_t add_l;
   uint8_t add_h;
   nrfx_err_t ret = lsm6dsox_ln_pg_read_byte(LSM6DSOX_FSM_LC_TIMEOUT_L, &add_l);
   if (ret == NRFX_SUCCESS)
   {
      ret = lsm6dsox_ln_pg_read_byte(LSM6DSOX_FSM_LC_TIMEOUT_H, &add_h);
      *val = add_h;
      *val = *val << 8;
      *val += add_l;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_fsm_number_of_programs_set(uint8_t val)
{
   nrfx_err_t ret = lsm6dsox_ln_pg_write_byte(LSM6DSOX_FSM_PROGRAMS, &val);
   return ret;
}

static nrfx_err_t lsm6dsox_fsm_number_of_programs_get(uint8_t *val)
{
   nrfx_err_t ret = lsm6dsox_ln_pg_read_byte(LSM6DSOX_FSM_PROGRAMS, val);
   return ret;
}

static nrfx_err_t lsm6dsox_fsm_start_address_set(uint16_t val)
{
   uint8_t add_l;
   uint8_t add_h;
   add_h = (uint8_t)( ( val & 0xFF00U ) >> 8 );
   add_l = (uint8_t)( val & 0x00FFU );
   nrfx_err_t ret = lsm6dsox_ln_pg_write_byte(LSM6DSOX_FSM_START_ADD_L, &add_l);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_ln_pg_write_byte(LSM6DSOX_FSM_START_ADD_H, &add_h);
   return ret;
}

static nrfx_err_t lsm6dsox_fsm_start_address_get(uint16_t *val)
{
   uint8_t add_l;
   uint8_t add_h;
   nrfx_err_t ret = lsm6dsox_ln_pg_read_byte(LSM6DSOX_FSM_START_ADD_L, &add_l);
   if (ret == NRFX_SUCCESS)
   {
      ret = lsm6dsox_ln_pg_read_byte(LSM6DSOX_FSM_START_ADD_H, &add_h);
      *val = add_h;
      *val = *val << 8;
      *val += add_l;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_sh_slave_connected_set(lsm6dsox_aux_sens_on_t val)
{
   lsm6dsox_master_config_t reg;
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_SENSOR_HUB_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_MASTER_CONFIG, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.aux_sens_on = (uint8_t)val;
      ret = lsm6dsox_write_reg(LSM6DSOX_MASTER_CONFIG, (uint8_t*)&reg, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   return ret;
}

static nrfx_err_t lsm6dsox_sh_slave_connected_get(lsm6dsox_aux_sens_on_t *val)
{
   lsm6dsox_master_config_t reg;
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_SENSOR_HUB_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_MASTER_CONFIG, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      switch (reg.aux_sens_on)
      {
         case LSM6DSOX_SLV_0:
            *val = LSM6DSOX_SLV_0;
            break;
         case LSM6DSOX_SLV_0_1:
            *val = LSM6DSOX_SLV_0_1;
            break;
         case LSM6DSOX_SLV_0_1_2:
            *val = LSM6DSOX_SLV_0_1_2;
            break;
         case LSM6DSOX_SLV_0_1_2_3:
            *val = LSM6DSOX_SLV_0_1_2_3;
            break;
         default:
            *val = LSM6DSOX_SLV_0;
            break;
      }
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_sh_master_set(uint8_t val)
{
   lsm6dsox_master_config_t reg;
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_SENSOR_HUB_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_MASTER_CONFIG, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.master_on = val;
      ret = lsm6dsox_write_reg(LSM6DSOX_MASTER_CONFIG, (uint8_t*)&reg, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   return ret;
}

static nrfx_err_t lsm6dsox_sh_master_get(uint8_t *val)
{
   lsm6dsox_master_config_t reg;
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_SENSOR_HUB_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_MASTER_CONFIG, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      *val = reg.master_on;
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_sh_pin_mode_set(lsm6dsox_shub_pu_en_t val)
{
   lsm6dsox_master_config_t reg;
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_SENSOR_HUB_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_MASTER_CONFIG, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.shub_pu_en = (uint8_t)val;
      ret = lsm6dsox_write_reg(LSM6DSOX_MASTER_CONFIG, (uint8_t*)&reg, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   return ret;
}

static nrfx_err_t lsm6dsox_sh_pin_mode_get(lsm6dsox_shub_pu_en_t *val)
{
   lsm6dsox_master_config_t reg;
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_SENSOR_HUB_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_MASTER_CONFIG, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      switch (reg.shub_pu_en)
      {
         case LSM6DSOX_EXT_PULL_UP:
            *val = LSM6DSOX_EXT_PULL_UP;
            break;
         case LSM6DSOX_INTERNAL_PULL_UP:
            *val = LSM6DSOX_INTERNAL_PULL_UP;
            break;
         default:
            *val = LSM6DSOX_EXT_PULL_UP;
            break;
      }
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_sh_pass_through_set(uint8_t val)
{
   lsm6dsox_master_config_t reg;
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_SENSOR_HUB_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_MASTER_CONFIG, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.pass_through_mode = val;
      ret = lsm6dsox_write_reg(LSM6DSOX_MASTER_CONFIG, (uint8_t*)&reg, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   return ret;
}

static nrfx_err_t lsm6dsox_sh_pass_through_get(uint8_t *val)
{
   lsm6dsox_master_config_t reg;
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_SENSOR_HUB_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_MASTER_CONFIG, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      *val = reg.pass_through_mode;
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_sh_syncro_mode_set(lsm6dsox_start_config_t val)
{
   lsm6dsox_master_config_t reg;
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_SENSOR_HUB_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_MASTER_CONFIG, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.start_config = (uint8_t)val;
      ret = lsm6dsox_write_reg(LSM6DSOX_MASTER_CONFIG, (uint8_t*)&reg, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   return ret;
}

static nrfx_err_t lsm6dsox_sh_syncro_mode_get(lsm6dsox_start_config_t *val)
{
   lsm6dsox_master_config_t reg;
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_SENSOR_HUB_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_MASTER_CONFIG, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      switch (reg.start_config)
      {
         case LSM6DSOX_EXT_ON_INT2_PIN:
            *val = LSM6DSOX_EXT_ON_INT2_PIN;
            break;
         case LSM6DSOX_XL_GY_DRDY:
            *val = LSM6DSOX_XL_GY_DRDY;
            break;
         default:
            *val = LSM6DSOX_EXT_ON_INT2_PIN;
            break;
      }
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_sh_write_mode_set(lsm6dsox_write_once_t val)
{
   lsm6dsox_master_config_t reg;
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_SENSOR_HUB_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_MASTER_CONFIG, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.write_once = (uint8_t)val;
      ret = lsm6dsox_write_reg(LSM6DSOX_MASTER_CONFIG, (uint8_t*)&reg, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   return ret;
}

static nrfx_err_t lsm6dsox_sh_write_mode_get(lsm6dsox_write_once_t *val)
{
   lsm6dsox_master_config_t reg;
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_SENSOR_HUB_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_MASTER_CONFIG, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      switch (reg.write_once)
      {
         case LSM6DSOX_EACH_SH_CYCLE:
            *val = LSM6DSOX_EACH_SH_CYCLE;
            break;
         case LSM6DSOX_ONLY_FIRST_CYCLE:
            *val = LSM6DSOX_ONLY_FIRST_CYCLE;
            break;
         default:
            *val = LSM6DSOX_EACH_SH_CYCLE;
            break;
      }
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_sh_reset_set(void)
{
   lsm6dsox_master_config_t reg;
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_SENSOR_HUB_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_MASTER_CONFIG, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.rst_master_regs = PROPERTY_ENABLE;
      ret = lsm6dsox_write_reg(LSM6DSOX_MASTER_CONFIG, (uint8_t*)&reg, 1);
   }
   if (ret == NRFX_SUCCESS)
   {
      reg.rst_master_regs = PROPERTY_DISABLE;
      ret = lsm6dsox_write_reg(LSM6DSOX_MASTER_CONFIG, (uint8_t*)&reg, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   return ret;
}

static nrfx_err_t lsm6dsox_sh_reset_get(uint8_t *val)
{
   lsm6dsox_master_config_t reg;
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_SENSOR_HUB_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_MASTER_CONFIG, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      *val = reg.rst_master_regs;
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_sh_data_rate_set(lsm6dsox_shub_odr_t val)
{
   lsm6dsox_slv0_config_t reg;
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_SENSOR_HUB_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_SLV0_CONFIG, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.shub_odr = (uint8_t)val;
      ret = lsm6dsox_write_reg(LSM6DSOX_SLV0_CONFIG, (uint8_t*)&reg, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   return ret;
}

static nrfx_err_t lsm6dsox_sh_data_rate_get(lsm6dsox_shub_odr_t *val)
{
   lsm6dsox_slv0_config_t reg;
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_SENSOR_HUB_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_SLV0_CONFIG, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      switch (reg.shub_odr)
      {
         case LSM6DSOX_SH_ODR_104Hz:
            *val = LSM6DSOX_SH_ODR_104Hz;
            break;
         case LSM6DSOX_SH_ODR_52Hz:
            *val = LSM6DSOX_SH_ODR_52Hz;
            break;
         case LSM6DSOX_SH_ODR_26Hz:
            *val = LSM6DSOX_SH_ODR_26Hz;
            break;
         case LSM6DSOX_SH_ODR_13Hz:
            *val = LSM6DSOX_SH_ODR_13Hz;
            break;
         default:
            *val = LSM6DSOX_SH_ODR_104Hz;
            break;
      }
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_sh_cfg_write(lsm6dsox_sh_cfg_write_t *val)
{
   lsm6dsox_slv0_add_t reg;
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_SENSOR_HUB_BANK);
   if (ret == NRFX_SUCCESS)
   {
      reg.slave0 = val->slv0_add;
      reg.rw_0 = 0;
      ret = lsm6dsox_write_reg(LSM6DSOX_SLV0_ADD, (uint8_t*)&reg, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_write_reg(LSM6DSOX_SLV0_SUBADD, &(val->slv0_subadd), 1);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_write_reg(LSM6DSOX_DATAWRITE_SLV0, &(val->slv0_data), 1);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   return ret;
}

static nrfx_err_t lsm6dsox_sh_slv0_cfg_read(lsm6dsox_sh_cfg_read_t *val)
{
   lsm6dsox_slv0_add_t slv0_add;
   lsm6dsox_slv0_config_t slv0_config;
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_SENSOR_HUB_BANK);
   if (ret == NRFX_SUCCESS)
   {
      slv0_add.slave0 = val->slv_add;
      slv0_add.rw_0 = 1;
      ret = lsm6dsox_write_reg(LSM6DSOX_SLV0_ADD, (uint8_t*)&slv0_add, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_write_reg(LSM6DSOX_SLV0_SUBADD, &(val->slv_subadd), 1);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_SLV0_CONFIG, (uint8_t*)&slv0_config, 1);
   if (ret == NRFX_SUCCESS)
   {
      slv0_config.slave0_numop = val->slv_len;
      ret = lsm6dsox_write_reg(LSM6DSOX_SLV0_CONFIG, (uint8_t*)&slv0_config, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   return ret;
}

static nrfx_err_t lsm6dsox_sh_slv1_cfg_read(lsm6dsox_sh_cfg_read_t *val)
{
   lsm6dsox_slv1_add_t slv1_add;
   lsm6dsox_slv1_config_t slv1_config;
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_SENSOR_HUB_BANK);
   if (ret == NRFX_SUCCESS)
   {
      slv1_add.slave1_add = val->slv_add;
      slv1_add.r_1 = 1;
      ret = lsm6dsox_write_reg(LSM6DSOX_SLV1_ADD, (uint8_t*)&slv1_add, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_write_reg(LSM6DSOX_SLV1_SUBADD, &(val->slv_subadd), 1);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_SLV1_CONFIG, (uint8_t*)&slv1_config, 1);
   if (ret == NRFX_SUCCESS)
   {
      slv1_config.slave1_numop = val->slv_len;
      ret = lsm6dsox_write_reg(LSM6DSOX_SLV1_CONFIG, (uint8_t*)&slv1_config, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   return ret;
}

static nrfx_err_t lsm6dsox_sh_slv2_cfg_read(lsm6dsox_sh_cfg_read_t *val)
{
   lsm6dsox_slv2_add_t slv2_add;
   lsm6dsox_slv2_config_t slv2_config;
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_SENSOR_HUB_BANK);
   if (ret == NRFX_SUCCESS)
   {
      slv2_add.slave2_add = val->slv_add;
      slv2_add.r_2 = 1;
      ret = lsm6dsox_write_reg(LSM6DSOX_SLV2_ADD, (uint8_t*)&slv2_add, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_write_reg(LSM6DSOX_SLV2_SUBADD, &(val->slv_subadd), 1);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_SLV2_CONFIG, (uint8_t*)&slv2_config, 1);
   if (ret == NRFX_SUCCESS)
   {
      slv2_config.slave2_numop = val->slv_len;
      ret = lsm6dsox_write_reg(LSM6DSOX_SLV2_CONFIG, (uint8_t*)&slv2_config, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   return ret;
}

static nrfx_err_t lsm6dsox_sh_slv3_cfg_read(lsm6dsox_sh_cfg_read_t *val)
{
   lsm6dsox_slv3_add_t slv3_add;
   lsm6dsox_slv3_config_t slv3_config;
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_SENSOR_HUB_BANK);
   if (ret == NRFX_SUCCESS)
   {
      slv3_add.slave3_add = val->slv_add;
      slv3_add.r_3 = 1;
      ret = lsm6dsox_write_reg(LSM6DSOX_SLV3_ADD, (uint8_t*)&slv3_add, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_write_reg(LSM6DSOX_SLV3_SUBADD, &(val->slv_subadd), 1);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_SLV3_CONFIG, (uint8_t*)&slv3_config, 1);
   if (ret == NRFX_SUCCESS)
   {
      slv3_config.slave3_numop = val->slv_len;
      ret = lsm6dsox_write_reg(LSM6DSOX_SLV3_CONFIG, (uint8_t*)&slv3_config, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   return ret;
}

static nrfx_err_t lsm6dsox_sh_status_get(lsm6dsox_status_master_t *val)
{
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_SENSOR_HUB_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_STATUS_MASTER, (uint8_t*)val, 1);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   return ret;
}

static nrfx_err_t lsm6dsox_s4s_tph_res_set(lsm6dsox_s4s_tph_res_t val)
{
   lsm6dsox_s4s_tph_l_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_S4S_TPH_L, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.tph_h_sel = (uint8_t)val;
      ret = lsm6dsox_write_reg(LSM6DSOX_S4S_TPH_L, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_s4s_tph_res_get(lsm6dsox_s4s_tph_res_t *val)
{
   lsm6dsox_s4s_tph_l_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_S4S_TPH_L, (uint8_t*)&reg, 1);
   switch (reg.tph_h_sel)
   {
      case LSM6DSOX_S4S_TPH_7bit:
         *val = LSM6DSOX_S4S_TPH_7bit;
         break;
      case LSM6DSOX_S4S_TPH_15bit:
         *val = LSM6DSOX_S4S_TPH_15bit;
         break;
      default:
         *val = LSM6DSOX_S4S_TPH_7bit;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_s4s_tph_val_set(uint16_t val)
{
   lsm6dsox_s4s_tph_l_t s4s_tph_l;
   lsm6dsox_s4s_tph_h_t s4s_tph_h;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_S4S_TPH_L, (uint8_t*)&s4s_tph_l, 1);
   if (ret == NRFX_SUCCESS)
   {
      s4s_tph_l.tph_l = (uint8_t)(val & 0x007FU);
      ret = lsm6dsox_write_reg(LSM6DSOX_S4S_TPH_L, (uint8_t*)&s4s_tph_l, 1);
   }
   if (ret == NRFX_SUCCESS)
   {
      ret = lsm6dsox_read_reg(LSM6DSOX_S4S_TPH_H, (uint8_t*)&s4s_tph_h, 1);
      s4s_tph_h.tph_h = (uint8_t)(val & 0x7F80U) >> 7;
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_write_reg(LSM6DSOX_S4S_TPH_H, (uint8_t*)&s4s_tph_h, 1);
   return ret;
}

static nrfx_err_t lsm6dsox_s4s_tph_val_get(uint16_t *val)
{
   lsm6dsox_s4s_tph_l_t s4s_tph_l;
   lsm6dsox_s4s_tph_h_t s4s_tph_h;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_S4S_TPH_L, (uint8_t*)&s4s_tph_l, 1);
   if (ret == NRFX_SUCCESS)
   {
      ret = lsm6dsox_read_reg(LSM6DSOX_S4S_TPH_H, (uint8_t*)&s4s_tph_h, 1);
      *val = s4s_tph_h.tph_h;
      *val = *val << 7;
      *val += s4s_tph_l.tph_l;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_s4s_res_ratio_set(lsm6dsox_s4s_res_ratio_t val)
{
   lsm6dsox_s4s_rr_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_S4S_RR, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.rr = (uint8_t)val;
      ret = lsm6dsox_write_reg(LSM6DSOX_S4S_RR, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_s4s_res_ratio_get(lsm6dsox_s4s_res_ratio_t *val)
{
   lsm6dsox_s4s_rr_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_S4S_RR, (uint8_t*)&reg, 1);
   switch (reg.rr)
   {
      case LSM6DSOX_S4S_DT_RES_11:
         *val = LSM6DSOX_S4S_DT_RES_11;
         break;
      case LSM6DSOX_S4S_DT_RES_12:
         *val = LSM6DSOX_S4S_DT_RES_12;
         break;
      case LSM6DSOX_S4S_DT_RES_13:
         *val = LSM6DSOX_S4S_DT_RES_13;
         break;
      case LSM6DSOX_S4S_DT_RES_14:
         *val = LSM6DSOX_S4S_DT_RES_14;
         break;
      default:
         *val = LSM6DSOX_S4S_DT_RES_11;
         break;
   }
   return ret;
}

static nrfx_err_t lsm6dsox_s4s_command_set(uint8_t val)
{
   lsm6dsox_s4s_st_cmd_code_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_S4S_ST_CMD_CODE, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.s4s_st_cmd_code = val;
      ret = lsm6dsox_write_reg(LSM6DSOX_S4S_ST_CMD_CODE, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_s4s_command_get(uint8_t *val)
{
   lsm6dsox_s4s_st_cmd_code_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_S4S_ST_CMD_CODE, (uint8_t*)&reg, 1);
   *val = reg.s4s_st_cmd_code;
   return ret;
}

static nrfx_err_t lsm6dsox_s4s_dt_set(uint8_t val)
{
   lsm6dsox_s4s_dt_reg_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_S4S_DT_REG, (uint8_t*)&reg, 1);
   if (ret == NRFX_SUCCESS)
   {
      reg.dt = val;
      ret = lsm6dsox_write_reg(LSM6DSOX_S4S_DT_REG, (uint8_t*)&reg, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_s4s_dt_get(uint8_t *val)
{
   lsm6dsox_s4s_dt_reg_t reg;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_S4S_DT_REG, (uint8_t*)&reg, 1);
   *val = reg.dt;
   return ret;
}

static nrfx_err_t lsm6dsox_init_set(lsm6dsox_init_t val)
{
   lsm6dsox_emb_func_init_a_t emb_func_init_a;
   lsm6dsox_emb_func_init_b_t emb_func_init_b;
   lsm6dsox_ctrl3_c_t ctrl3_c;
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_EMBEDDED_FUNC_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_EMB_FUNC_INIT_B, (uint8_t*)&emb_func_init_b, 1);
   if (ret == NRFX_SUCCESS)
   {
      emb_func_init_b.fifo_compr_init = (uint8_t)val & ((uint8_t)LSM6DSOX_FIFO_COMP >> 2);
      emb_func_init_b.fsm_init = (uint8_t)val & ((uint8_t)LSM6DSOX_FSM >> 3);
      emb_func_init_b.mlc_init = (uint8_t)val & ((uint8_t)LSM6DSOX_MLC >> 4);
      ret = lsm6dsox_write_reg(LSM6DSOX_EMB_FUNC_INIT_B, (uint8_t*)&emb_func_init_b, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_EMB_FUNC_INIT_A, (uint8_t*)&emb_func_init_a, 1);
   if (ret == NRFX_SUCCESS)
   {
      emb_func_init_a.step_det_init = ((uint8_t)val & (uint8_t)LSM6DSOX_PEDO ) >> 5;
      emb_func_init_a.tilt_init = ((uint8_t)val & (uint8_t)LSM6DSOX_TILT ) >> 6;
      emb_func_init_a.sig_mot_init = ((uint8_t)val & (uint8_t)LSM6DSOX_SMOTION ) >> 7;
      ret = lsm6dsox_write_reg(LSM6DSOX_EMB_FUNC_INIT_A, (uint8_t*)&emb_func_init_a, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_CTRL3_C, (uint8_t*)&ctrl3_c, 1);
   if (((val == LSM6DSOX_BOOT) || (val == LSM6DSOX_RESET)) && (ret == NRFX_SUCCESS))
   {
      ctrl3_c.boot = (uint8_t)val & (uint8_t)LSM6DSOX_BOOT;
      ctrl3_c.sw_reset = ( (uint8_t)val & (uint8_t)LSM6DSOX_RESET) >> 1;
      ret = lsm6dsox_write_reg(LSM6DSOX_CTRL3_C, (uint8_t*)&ctrl3_c, 1);
   }
   if ((val == LSM6DSOX_DRV_RDY) && ((ctrl3_c.bdu == PROPERTY_DISABLE) || (ctrl3_c.if_inc == PROPERTY_DISABLE)) && (ret == NRFX_SUCCESS))
   {
      ctrl3_c.bdu = PROPERTY_ENABLE;
      ctrl3_c.if_inc = PROPERTY_ENABLE;
      ret = lsm6dsox_write_reg(LSM6DSOX_CTRL3_C, (uint8_t*)&ctrl3_c, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_pin_conf_set(lsm6dsox_pin_conf_t val)
{
   lsm6dsox_i3c_bus_avb_t i3c_bus_avb;
   lsm6dsox_pin_ctrl_t pin_ctrl;
   lsm6dsox_ctrl3_c_t ctrl3_c;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_PIN_CTRL, (uint8_t*)&pin_ctrl, 1);
   if (ret == NRFX_SUCCESS)
   {
      pin_ctrl.ois_pu_dis = ~val.aux_sdo_ocs_pull_up;
      pin_ctrl.sdo_pu_en  = val.sdo_sa0_pull_up;
      ret = lsm6dsox_write_reg(LSM6DSOX_PIN_CTRL, (uint8_t*)&pin_ctrl, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_CTRL3_C, (uint8_t*)&ctrl3_c, 1);
   if (ret == NRFX_SUCCESS)
   {
      ctrl3_c.pp_od = ~val.int1_int2_push_pull;
      ret = lsm6dsox_write_reg(LSM6DSOX_CTRL3_C, (uint8_t*)&ctrl3_c, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_I3C_BUS_AVB, (uint8_t*)&i3c_bus_avb, 1);
   if (ret == NRFX_SUCCESS)
   {
      i3c_bus_avb.pd_dis_int1 = ~val.int1_pull_down;
      ret = lsm6dsox_write_reg(LSM6DSOX_I3C_BUS_AVB, (uint8_t*)&i3c_bus_avb, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_pin_conf_get(lsm6dsox_pin_conf_t *val)
{
   lsm6dsox_i3c_bus_avb_t i3c_bus_avb;
   lsm6dsox_pin_ctrl_t pin_ctrl;
   lsm6dsox_ctrl3_c_t ctrl3_c;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_PIN_CTRL, (uint8_t*)&pin_ctrl, 1);
   if (ret == NRFX_SUCCESS)
   {
      val->aux_sdo_ocs_pull_up = ~pin_ctrl.ois_pu_dis;
      val->aux_sdo_ocs_pull_up = pin_ctrl.sdo_pu_en;
      ret = lsm6dsox_read_reg(LSM6DSOX_CTRL3_C, (uint8_t*)&ctrl3_c, 1);
   }
   if (ret == NRFX_SUCCESS)
   {
      val->int1_int2_push_pull = ~ctrl3_c.pp_od;
      ret = lsm6dsox_read_reg(LSM6DSOX_I3C_BUS_AVB, (uint8_t*)&i3c_bus_avb, 1);
   }
   if (ret == NRFX_SUCCESS)
      val->int1_pull_down = ~i3c_bus_avb.pd_dis_int1;
   return ret;
}

static nrfx_err_t lsm6dsox_interrupt_mode_set(lsm6dsox_int_mode_t val)
{
   lsm6dsox_tap_cfg0_t tap_cfg0;
   lsm6dsox_page_rw_t page_rw;
   lsm6dsox_ctrl3_c_t ctrl3_c;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL3_C, (uint8_t*)&ctrl3_c, 1);
   if (ret == NRFX_SUCCESS)
   {
      ctrl3_c.h_lactive = val.active_low;
      ret = lsm6dsox_write_reg(LSM6DSOX_CTRL3_C, (uint8_t*)&ctrl3_c, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_TAP_CFG0, (uint8_t*)&tap_cfg0, 1);
   if (ret == NRFX_SUCCESS)
   {
      tap_cfg0.lir = val.base_latched;
      tap_cfg0.int_clr_on_read = val.base_latched | val.emb_latched;
      ret = lsm6dsox_write_reg(LSM6DSOX_TAP_CFG0, (uint8_t*)&tap_cfg0, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_EMBEDDED_FUNC_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_PAGE_RW, (uint8_t*)&page_rw, 1);
   if (ret == NRFX_SUCCESS)
   {
      page_rw.emb_func_lir = val.emb_latched;
      ret = lsm6dsox_write_reg(LSM6DSOX_PAGE_RW, (uint8_t*)&page_rw, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   return ret;
}

static nrfx_err_t lsm6dsox_interrupt_mode_get(lsm6dsox_int_mode_t *val)
{
   lsm6dsox_tap_cfg0_t tap_cfg0;
   lsm6dsox_page_rw_t page_rw;
   lsm6dsox_ctrl3_c_t ctrl3_c;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL3_C, (uint8_t*)&ctrl3_c, 1);
   if (ret == NRFX_SUCCESS)
   {
      ctrl3_c.h_lactive = val->active_low;
      ret = lsm6dsox_read_reg(LSM6DSOX_TAP_CFG0, (uint8_t*)&tap_cfg0, 1);
   }
   if (ret == NRFX_SUCCESS)
   {
      tap_cfg0.lir = val->base_latched;
      tap_cfg0.int_clr_on_read = val->base_latched | val->emb_latched;
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_EMBEDDED_FUNC_BANK);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_PAGE_RW, (uint8_t*)&page_rw, 1);
   if (ret == NRFX_SUCCESS)
   {
      page_rw.emb_func_lir = val->emb_latched;
      ret = lsm6dsox_write_reg(LSM6DSOX_PAGE_RW, (uint8_t*)&page_rw, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   return ret;
}

static nrfx_err_t lsm6dsox_pin_int1_route_set(lsm6dsox_pin_int1_route_t val)
{
   lsm6dsox_emb_func_int1_t   emb_func_int1;
   lsm6dsox_fsm_int1_a_t      fsm_int1_a;
   lsm6dsox_fsm_int1_b_t      fsm_int1_b;
   lsm6dsox_int1_ctrl_t       int1_ctrl;
   lsm6dsox_int2_ctrl_t       int2_ctrl;
   lsm6dsox_mlc_int1_t        mlc_int1;
   lsm6dsox_tap_cfg2_t        tap_cfg2;
   lsm6dsox_md2_cfg_t         md2_cfg;
   lsm6dsox_md1_cfg_t         md1_cfg;
   lsm6dsox_ctrl4_c_t         ctrl4_c;
   int1_ctrl.int1_drdy_xl   = val.drdy_xl;
   int1_ctrl.int1_drdy_g    = val.drdy_g;
   int1_ctrl.int1_boot      = val.boot;
   int1_ctrl.int1_fifo_th   = val.fifo_th;
   int1_ctrl.int1_fifo_ovr  = val.fifo_ovr;
   int1_ctrl.int1_fifo_full = val.fifo_full;
   int1_ctrl.int1_cnt_bdr   = val.fifo_bdr;
   int1_ctrl.den_drdy_flag  = val.den_flag;
   md1_cfg.int1_shub         = val.sh_endop;
   md1_cfg.int1_6d           = val.six_d;
   md1_cfg.int1_double_tap   = val.double_tap;
   md1_cfg.int1_ff           = val.free_fall;
   md1_cfg.int1_wu           = val.wake_up;
   md1_cfg.int1_single_tap   = val.single_tap;
   md1_cfg.int1_sleep_change = val.sleep_change;
   emb_func_int1.int1_step_detector = val.step_detector;
   emb_func_int1.int1_tilt          = val.tilt;
   emb_func_int1.int1_sig_mot       = val.sig_mot;
   emb_func_int1.int1_fsm_lc        = val.fsm_lc;
   fsm_int1_a.int1_fsm1 = val.fsm1;
   fsm_int1_a.int1_fsm2 = val.fsm2;
   fsm_int1_a.int1_fsm3 = val.fsm3;
   fsm_int1_a.int1_fsm4 = val.fsm4;
   fsm_int1_a.int1_fsm5 = val.fsm5;
   fsm_int1_a.int1_fsm6 = val.fsm6;
   fsm_int1_a.int1_fsm7 = val.fsm7;
   fsm_int1_a.int1_fsm8 = val.fsm8;
   fsm_int1_b.int1_fsm9  = val.fsm9 ;
   fsm_int1_b.int1_fsm10 = val.fsm10;
   fsm_int1_b.int1_fsm11 = val.fsm11;
   fsm_int1_b.int1_fsm12 = val.fsm12;
   fsm_int1_b.int1_fsm13 = val.fsm13;
   fsm_int1_b.int1_fsm14 = val.fsm14;
   fsm_int1_b.int1_fsm15 = val.fsm15;
   fsm_int1_b.int1_fsm16 = val.fsm16;
   mlc_int1.int1_mlc1 = val.mlc1;
   mlc_int1.int1_mlc2 = val.mlc2;
   mlc_int1.int1_mlc3 = val.mlc3;
   mlc_int1.int1_mlc4 = val.mlc4;
   mlc_int1.int1_mlc5 = val.mlc5;
   mlc_int1.int1_mlc6 = val.mlc6;
   mlc_int1.int1_mlc7 = val.mlc7;
   mlc_int1.int1_mlc8 = val.mlc8;
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL4_C, (uint8_t*)&ctrl4_c, 1);
   if (ret == NRFX_SUCCESS)
   {
      if ((val.drdy_temp | val.timestamp) != PROPERTY_DISABLE)
         ctrl4_c.int2_on_int1 = PROPERTY_ENABLE;
      else
         ctrl4_c.int2_on_int1 = PROPERTY_DISABLE;
      ret = lsm6dsox_write_reg(LSM6DSOX_CTRL4_C, (uint8_t*)&ctrl4_c, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_EMBEDDED_FUNC_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_write_reg(LSM6DSOX_MLC_INT1, (uint8_t*)&mlc_int1, 1);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_write_reg(LSM6DSOX_EMB_FUNC_INT1, (uint8_t*)&emb_func_int1, 1);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_write_reg(LSM6DSOX_FSM_INT1_A, (uint8_t*)&fsm_int1_a, 1);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_write_reg(LSM6DSOX_FSM_INT1_B, (uint8_t*)&fsm_int1_b, 1);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   if (ret == NRFX_SUCCESS)
   {
      if ((emb_func_int1.int1_fsm_lc | emb_func_int1.int1_sig_mot | emb_func_int1.int1_step_detector | emb_func_int1.int1_tilt
            | fsm_int1_a.int1_fsm1 | fsm_int1_a.int1_fsm2 | fsm_int1_a.int1_fsm3 | fsm_int1_a.int1_fsm4
            | fsm_int1_a.int1_fsm5 | fsm_int1_a.int1_fsm6 | fsm_int1_a.int1_fsm7 | fsm_int1_a.int1_fsm8
            | fsm_int1_b.int1_fsm9 | fsm_int1_b.int1_fsm10 | fsm_int1_b.int1_fsm11 | fsm_int1_b.int1_fsm12
            | fsm_int1_b.int1_fsm13 | fsm_int1_b.int1_fsm14 | fsm_int1_b.int1_fsm15 | fsm_int1_b.int1_fsm16
            | mlc_int1.int1_mlc1 | mlc_int1.int1_mlc2 | mlc_int1.int1_mlc3 | mlc_int1.int1_mlc4
            | mlc_int1.int1_mlc5 | mlc_int1.int1_mlc6 | mlc_int1.int1_mlc7 | mlc_int1.int1_mlc8) != PROPERTY_DISABLE)
         md1_cfg.int1_emb_func = PROPERTY_ENABLE;
      else
         md1_cfg.int1_emb_func = PROPERTY_DISABLE;
      ret = lsm6dsox_write_reg(LSM6DSOX_INT1_CTRL, (uint8_t*)&int1_ctrl, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_write_reg(LSM6DSOX_MD1_CFG, (uint8_t*)&md1_cfg, 1);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_INT2_CTRL, (uint8_t*)&int2_ctrl, 1);
   if (ret == NRFX_SUCCESS)
   {
      int2_ctrl.int2_drdy_temp = val.drdy_temp;
      ret = lsm6dsox_write_reg(LSM6DSOX_INT2_CTRL, (uint8_t*)&int2_ctrl, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_MD2_CFG, (uint8_t*)&md2_cfg, 1);
   if (ret == NRFX_SUCCESS)
   {
      md2_cfg.int2_timestamp = val.timestamp;
      ret = lsm6dsox_write_reg(LSM6DSOX_MD2_CFG, (uint8_t*)&md2_cfg, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_TAP_CFG2, (uint8_t*)&tap_cfg2, 1);
   if (ret == NRFX_SUCCESS)
   {
      tap_cfg2.interrupts_enable = PROPERTY_DISABLE;
      ret = lsm6dsox_write_reg(LSM6DSOX_TAP_CFG2, (uint8_t*)&tap_cfg2, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_pin_int1_route_get(lsm6dsox_pin_int1_route_t *val)
{
   lsm6dsox_emb_func_int1_t   emb_func_int1;
   lsm6dsox_fsm_int1_a_t      fsm_int1_a;
   lsm6dsox_fsm_int1_b_t      fsm_int1_b;
   lsm6dsox_int1_ctrl_t       int1_ctrl;
   lsm6dsox_int2_ctrl_t       int2_ctrl;
   lsm6dsox_mlc_int1_t        mlc_int1;
   lsm6dsox_md2_cfg_t         md2_cfg;
   lsm6dsox_md1_cfg_t         md1_cfg;
   lsm6dsox_ctrl4_c_t         ctrl4_c;
   nrfx_err_t ret = lsm6dsox_mem_bank_set(LSM6DSOX_EMBEDDED_FUNC_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_MLC_INT1, (uint8_t*)&mlc_int1, 1);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_EMB_FUNC_INT1, (uint8_t*)&emb_func_int1, 1);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_FSM_INT1_A, (uint8_t*)&fsm_int1_a, 1);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_FSM_INT1_B, (uint8_t*)&fsm_int1_b, 1);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_mem_bank_set(LSM6DSOX_USER_BANK);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_INT1_CTRL, (uint8_t*)&int1_ctrl, 1);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_MD1_CFG, (uint8_t*)&md1_cfg, 1);
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_CTRL4_C, (uint8_t*)&ctrl4_c, 1);
   if (ctrl4_c.int2_on_int1 == PROPERTY_ENABLE)
   {
      if (ret == NRFX_SUCCESS)
      {
         ret = lsm6dsox_read_reg(LSM6DSOX_INT2_CTRL, (uint8_t*)&int2_ctrl, 1);
         val->drdy_temp = int2_ctrl.int2_drdy_temp;
      }
      if (ret == NRFX_SUCCESS)
      {
         ret = lsm6dsox_read_reg(LSM6DSOX_MD2_CFG, (uint8_t*)&md2_cfg, 1);
         val->timestamp = md2_cfg.int2_timestamp;
      }
   }
   else
   {
      val->drdy_temp = PROPERTY_DISABLE;
      val->timestamp = PROPERTY_DISABLE;
   }

   val->drdy_xl   = int1_ctrl.int1_drdy_xl;
   val->drdy_g    = int1_ctrl.int1_drdy_g;
   val->boot      = int1_ctrl.int1_boot;
   val->fifo_th   = int1_ctrl.int1_fifo_th;
   val->fifo_ovr  = int1_ctrl.int1_fifo_ovr;
   val->fifo_full = int1_ctrl.int1_fifo_full;
   val->fifo_bdr  = int1_ctrl.int1_cnt_bdr;
   val->den_flag  = int1_ctrl.den_drdy_flag;
   val->sh_endop     = md1_cfg.int1_shub;
   val->six_d        = md1_cfg.int1_6d;
   val->double_tap   = md1_cfg.int1_double_tap;
   val->free_fall    = md1_cfg.int1_ff;
   val->wake_up      = md1_cfg.int1_wu;
   val->single_tap   = md1_cfg.int1_single_tap;
   val->sleep_change = md1_cfg.int1_sleep_change;
   val->step_detector = emb_func_int1.int1_step_detector;
   val->tilt          = emb_func_int1.int1_tilt;
   val->sig_mot       = emb_func_int1.int1_sig_mot;
   val->fsm_lc        = emb_func_int1.int1_fsm_lc;
   val->fsm1 = fsm_int1_a.int1_fsm1;
   val->fsm2 = fsm_int1_a.int1_fsm2;
   val->fsm3 = fsm_int1_a.int1_fsm3;
   val->fsm4 = fsm_int1_a.int1_fsm4;
   val->fsm5 = fsm_int1_a.int1_fsm5;
   val->fsm6 = fsm_int1_a.int1_fsm6;
   val->fsm7 = fsm_int1_a.int1_fsm7;
   val->fsm8 = fsm_int1_a.int1_fsm8;
   val->fsm9  = fsm_int1_b.int1_fsm9;
   val->fsm10 = fsm_int1_b.int1_fsm10;
   val->fsm11 = fsm_int1_b.int1_fsm11;
   val->fsm12 = fsm_int1_b.int1_fsm12;
   val->fsm13 = fsm_int1_b.int1_fsm13;
   val->fsm14 = fsm_int1_b.int1_fsm14;
   val->fsm15 = fsm_int1_b.int1_fsm15;
   val->fsm16 = fsm_int1_b.int1_fsm16;
   val->mlc1 = mlc_int1.int1_mlc1;
   val->mlc2 = mlc_int1.int1_mlc2;
   val->mlc3 = mlc_int1.int1_mlc3;
   val->mlc4 = mlc_int1.int1_mlc4;
   val->mlc5 = mlc_int1.int1_mlc5;
   val->mlc6 = mlc_int1.int1_mlc6;
   val->mlc7 = mlc_int1.int1_mlc7;
   val->mlc8 = mlc_int1.int1_mlc8;
   return ret;
}

static nrfx_err_t lsm6dsox_all_sources_get(lsm6dsox_all_sources_t *val)
{
   lsm6dsox_emb_func_status_mainpage_t emb_func_status_mainpage;
   lsm6dsox_status_master_mainpage_t   status_master_mainpage;
   lsm6dsox_fsm_status_a_mainpage_t    fsm_status_a_mainpage;
   lsm6dsox_fsm_status_b_mainpage_t    fsm_status_b_mainpage;
   lsm6dsox_mlc_status_mainpage_t      mlc_status_mainpage;
   lsm6dsox_fifo_status1_t             fifo_status1;
   lsm6dsox_fifo_status2_t             fifo_status2;
   lsm6dsox_all_int_src_t              all_int_src;
   lsm6dsox_wake_up_src_t              wake_up_src;
   lsm6dsox_status_reg_t               status_reg;
   lsm6dsox_tap_src_t                  tap_src;
   lsm6dsox_d6d_src_t                  d6d_src;
   lsm6dsox_ctrl5_c_t                  ctrl5_c;
   uint8_t                             reg[12];
   nrfx_err_t ret = lsm6dsox_read_reg(LSM6DSOX_CTRL5_C, (uint8_t*)&ctrl5_c, 1);
   if (ret == NRFX_SUCCESS)
   {
      ctrl5_c.rounding_status = PROPERTY_ENABLE;
      ret = lsm6dsox_write_reg(LSM6DSOX_CTRL5_C, (uint8_t*)&ctrl5_c, 1);
   }
   if (ret == NRFX_SUCCESS)
      ret = lsm6dsox_read_reg(LSM6DSOX_ALL_INT_SRC, reg, 12);
   if (ret == NRFX_SUCCESS)
   {
      *(uint8_t*)&all_int_src = reg[0];
      *(uint8_t*)&wake_up_src = reg[1];
      *(uint8_t*)&tap_src = reg[2];
      *(uint8_t*)&d6d_src = reg[3];
      *(uint8_t*)&status_reg = reg[4];
      *(uint8_t*)&emb_func_status_mainpage = reg[5];
      *(uint8_t*)&fsm_status_a_mainpage = reg[6];
      *(uint8_t*)&fsm_status_b_mainpage = reg[7];
      *(uint8_t*)&mlc_status_mainpage = reg[8];
      *(uint8_t*)&status_master_mainpage = reg[9];
      *(uint8_t*)&fifo_status1 = reg[10];
      *(uint8_t*)&fifo_status2 = reg[11];
      val->timestamp = all_int_src.timestamp_endcount;
      val->wake_up_z    = wake_up_src.z_wu;
      val->wake_up_y    = wake_up_src.y_wu;
      val->wake_up_x    = wake_up_src.x_wu;
      val->wake_up      = wake_up_src.wu_ia;
      val->sleep_state  = wake_up_src.sleep_state;
      val->free_fall    = wake_up_src.ff_ia;
      val->sleep_change = wake_up_src.sleep_change_ia;
      val->tap_x      = tap_src.x_tap;
      val->tap_y      = tap_src.y_tap;
      val->tap_z      = tap_src.z_tap;
      val->tap_sign   = tap_src.tap_sign;
      val->double_tap = tap_src.double_tap;
      val->single_tap = tap_src.single_tap;
      val->six_d_xl = d6d_src.xl;
      val->six_d_xh = d6d_src.xh;
      val->six_d_yl = d6d_src.yl;
      val->six_d_yh = d6d_src.yh;
      val->six_d_zl = d6d_src.zl;
      val->six_d_zh = d6d_src.zh;
      val->six_d    = d6d_src.d6d_ia;
      val->den_flag = d6d_src.den_drdy;
      val->drdy_xl   = status_reg.xlda;
      val->drdy_g    = status_reg.gda;
      val->drdy_temp = status_reg.tda;
      val->step_detector = emb_func_status_mainpage.is_step_det;
      val->tilt          = emb_func_status_mainpage.is_tilt;
      val->sig_mot       = emb_func_status_mainpage.is_sigmot;
      val->fsm_lc        = emb_func_status_mainpage.is_fsm_lc;
      val->fsm1 = fsm_status_a_mainpage.is_fsm1;
      val->fsm2 = fsm_status_a_mainpage.is_fsm2;
      val->fsm3 = fsm_status_a_mainpage.is_fsm3;
      val->fsm4 = fsm_status_a_mainpage.is_fsm4;
      val->fsm5 = fsm_status_a_mainpage.is_fsm5;
      val->fsm6 = fsm_status_a_mainpage.is_fsm6;
      val->fsm7 = fsm_status_a_mainpage.is_fsm7;
      val->fsm8 = fsm_status_a_mainpage.is_fsm8;
      val->fsm9  = fsm_status_b_mainpage.is_fsm9;
      val->fsm10 = fsm_status_b_mainpage.is_fsm10;
      val->fsm11 = fsm_status_b_mainpage.is_fsm11;
      val->fsm12 = fsm_status_b_mainpage.is_fsm12;
      val->fsm13 = fsm_status_b_mainpage.is_fsm13;
      val->fsm14 = fsm_status_b_mainpage.is_fsm14;
      val->fsm15 = fsm_status_b_mainpage.is_fsm15;
      val->fsm16 = fsm_status_b_mainpage.is_fsm16;
      val->mlc1 = mlc_status_mainpage.is_mlc1;
      val->mlc2 = mlc_status_mainpage.is_mlc2;
      val->mlc3 = mlc_status_mainpage.is_mlc3;
      val->mlc4 = mlc_status_mainpage.is_mlc4;
      val->mlc5 = mlc_status_mainpage.is_mlc5;
      val->mlc6 = mlc_status_mainpage.is_mlc6;
      val->mlc7 = mlc_status_mainpage.is_mlc7;
      val->mlc8 = mlc_status_mainpage.is_mlc8;
      val->sh_endop       = status_master_mainpage.sens_hub_endop;
      val->sh_slave0_nack = status_master_mainpage.slave0_nack;
      val->sh_slave1_nack = status_master_mainpage.slave1_nack;
      val->sh_slave2_nack = status_master_mainpage.slave2_nack;
      val->sh_slave3_nack = status_master_mainpage.slave3_nack;
      val->sh_wr_once     = status_master_mainpage.wr_once_done;
      val->fifo_diff = (256U * fifo_status2.diff_fifo) + fifo_status1.diff_fifo;
      val->fifo_ovr_latched = fifo_status2.over_run_latched;
      val->fifo_bdr         = fifo_status2.counter_bdr_ia;
      val->fifo_full        = fifo_status2.fifo_full_ia;
      val->fifo_ovr         = fifo_status2.fifo_ovr_ia;
      val->fifo_th          = fifo_status2.fifo_wtm_ia;
      ctrl5_c.rounding_status = PROPERTY_DISABLE;
      ret = lsm6dsox_write_reg(LSM6DSOX_CTRL5_C, (uint8_t*)&ctrl5_c, 1);
   }
   return ret;
}

static nrfx_err_t lsm6dsox_read_lis3mdl_reg(uint8_t reg, uint8_t *data, uint16_t len)
{
   // Disable the accelerometer, enable the I2C Master, and enable accelerometer to trigger Sensor Hub operation
   int16_t data_raw_acceleration[3];
   lsm6dsox_sh_cfg_read_t sh_cfg_read;
   lsm6dsox_xl_data_rate_set(LSM6DSOX_XL_ODR_OFF);
   sh_cfg_read.slv_add = (LIS3MDL_I2C_ADD_H & 0xFEU) >> 1; /* 7bit I2C address */
   sh_cfg_read.slv_subadd = reg;
   sh_cfg_read.slv_len = len;
   nrfx_err_t ret = lsm6dsox_sh_slv0_cfg_read(&sh_cfg_read);
   lsm6dsox_sh_slave_connected_set(LSM6DSOX_SLV_0);
   lsm6dsox_sh_master_set(PROPERTY_ENABLE);
   lsm6dsox_xl_data_rate_set(LSM6DSOX_XL_ODR_104Hz);
   lsm6dsox_acceleration_raw_get(data_raw_acceleration);

   // Retrieve Sensor Hub status
   lsm6dsox_status_master_t master_status;
   uint8_t drdy;
   do
   {
      nrf_delay_ms(20);
      lsm6dsox_xl_flag_data_ready_get(&drdy);
   } while (!drdy);
   do
   {
      nrf_delay_ms(20);
      lsm6dsox_sh_status_get(&master_status);
   } while (!master_status.sens_hub_endop);

   // Disable I2C Master and XL trigger
   lsm6dsox_sh_master_set(PROPERTY_DISABLE);
   lsm6dsox_xl_data_rate_set(LSM6DSOX_XL_ODR_OFF);
   lsm6dsox_sh_read_data_raw_get((lsm6dsox_emb_sh_read_t*)data, len);
   return ret;
}

static nrfx_err_t lsm6dsox_write_lis3mdl_reg(uint8_t reg, uint8_t *data, uint16_t len)
{
   // Configure Sensor Hub to read LIS3MDL
   lsm6dsox_sh_cfg_write_t sh_cfg_write;
   sh_cfg_write.slv0_add = (LIS3MDL_I2C_ADD_H & 0xFEU) >> 1; /* 7bit I2C address */
   sh_cfg_write.slv0_subadd = reg;
   sh_cfg_write.slv0_data = *data;
   nrfx_err_t ret = lsm6dsox_sh_cfg_write(&sh_cfg_write);

   // Disable the accelerometer, enable the I2C Master, and enable accelerometer to trigger Sensor Hub operation
   int16_t data_raw_acceleration[3];
   lsm6dsox_xl_data_rate_set(LSM6DSOX_XL_ODR_OFF);
   lsm6dsox_sh_master_set(PROPERTY_ENABLE);
   lsm6dsox_xl_data_rate_set(LSM6DSOX_XL_ODR_104Hz);
   lsm6dsox_acceleration_raw_get(data_raw_acceleration);

   // Retrieve Sensor Hub status
   lsm6dsox_status_master_t master_status;
   uint8_t drdy;
   do
   {
      nrf_delay_ms(20);
      lsm6dsox_xl_flag_data_ready_get(&drdy);
   } while (!drdy);
   do
   {
      nrf_delay_ms(20);
      lsm6dsox_sh_status_get(&master_status);
   } while (!master_status.sens_hub_endop);

   // Disable I2C Master and XL trigger
   lsm6dsox_sh_master_set(PROPERTY_DISABLE);
   lsm6dsox_xl_data_rate_set(LSM6DSOX_XL_ODR_OFF);
   return ret;
}

static nrfx_err_t imu_read_accelerometer_data(float* x, float* y, float* z)
{
   return NRFX_ERROR_NOT_SUPPORTED;
}

static bool imu_in_motion(void)
{
   return false;
}


// Public IMU API functions --------------------------------------------------------------------------------------------

bool imu_init(imu_data_callback callback)
{
   // Configure the magnetometer input pins as INPUT ANALOG no-ops
   nrf_gpio_cfg_default(MAGNETOMETER_INT);
   nrf_gpio_cfg_default(MAGNETOMETER_DRDY);

   // Setup SPI parameters
   _data_callback = callback;
   _spi_config.sck_pin = IMU_SPI_SCLK;
   _spi_config.miso_pin = IMU_SPI_MISO;
   _spi_config.mosi_pin = IMU_SPI_MOSI;
   _spi_config.ss_pin = IMU_SPI_CS;
   _spi_config.frequency = NRF_DRV_SPI_FREQ_4M;
   _spi_config.mode = NRF_DRV_SPI_MODE_3;
   _imu_data_ready = _imu_motion_changed = false;
   nrf_drv_spi_init(&_spi_instance, &_spi_config, NULL, NULL);

   // Check the IMU ID
   uint8_t dummy;
   lsm6dsox_device_id_get(&dummy);
   if (dummy == LSM6DSOX_ID)
   {
      // Restore the default chip configuration and disable the I3C interface
      lsm6dsox_reset_set(PROPERTY_ENABLE);
      do { lsm6dsox_reset_get(&dummy); } while (dummy);
      lsm6dsox_i3c_disable_set(LSM6DSOX_I3C_DISABLE);
      // TODO Some hardware require to enable pull up on master I2C interface, maybe??
      //lsm6dsox_sh_pin_mode_set(LSM6DSOX_INTERNAL_PULL_UP);

      // Verify that the magnetometer is connected to the Sensor Hub
      lis3mdl_set_read_reg_function(lsm6dsox_read_lis3mdl_reg);
      lis3mdl_set_write_reg_function(lsm6dsox_write_lis3mdl_reg);
      lis3mdl_device_id_get(&dummy);
      if (dummy == LIS3MDL_ID)
      {
         // Configure the magnetometer
         lis3mdl_block_data_update_set(PROPERTY_ENABLE);
         lis3mdl_operating_mode_set(LIS3MDL_CONTINUOUS_MODE);
         lis3mdl_data_rate_set(LIS3MDL_LP_20Hz);

         // Configure the IMU FIFO buffer in Stream/Continuous Mode with interrupts on INT1
         lsm6dsox_pin_int1_route_t int1_route;
         lsm6dsox_fifo_watermark_set(15);
         lsm6dsox_fifo_mode_set(LSM6DSOX_STREAM_MODE);
         lsm6dsox_int_notification_set(LSM6DSOX_ALL_INT_LATCHED);
         lsm6dsox_pin_int1_route_get(&int1_route);
         int1_route.fifo_th = PROPERTY_ENABLE;
         lsm6dsox_pin_int1_route_set(int1_route);

         // Enable FIFO batching of Slave0
         lsm6dsox_sh_batch_slave_0_set(PROPERTY_ENABLE);
         lsm6dsox_sh_data_rate_set(LSM6DSOX_SH_ODR_13Hz);
         lsm6dsox_fifo_xl_batch_set(LSM6DSOX_XL_BATCHED_AT_12Hz5);
         lsm6dsox_fifo_gy_batch_set(LSM6DSOX_GY_BATCHED_AT_12Hz5);

         // Prepare Sensor Hub to read data from external Slave0 continuously and store into FIFO
         lsm6dsox_sh_cfg_read_t sh_cfg_read;
         sh_cfg_read.slv_add = (LIS3MDL_I2C_ADD_H & 0xFEU) >> 1;
         sh_cfg_read.slv_subadd = LIS3MDL_OUT_X_L;
         sh_cfg_read.slv_len = 6;
         lsm6dsox_sh_slv0_cfg_read(&sh_cfg_read);
         lsm6dsox_sh_slave_connected_set(LSM6DSOX_SLV_0);
         lsm6dsox_sh_master_set(PROPERTY_ENABLE);

         // Configure LSM6DSOX
         lsm6dsox_xl_full_scale_set(LSM6DSOX_2g);
         lsm6dsox_gy_full_scale_set(LSM6DSOX_2000dps);
         lsm6dsox_block_data_update_set(PROPERTY_ENABLE);
         lsm6dsox_xl_data_rate_set(LSM6DSOX_XL_ODR_12Hz5);
         lsm6dsox_gy_data_rate_set(LSM6DSOX_GY_ODR_12Hz5);
      }
   }

   // De-initialize SPI communications
   nrf_drv_spi_uninit(&_spi_instance);
   nrfx_gpiote_out_clear(IMU_SPI_SCLK);

   // TODO: In some ISR, set _imu_data_ready and _imu_motion_changed

   return true;
}

void imu_handle_incoming_data(uint32_t timestamp)
{
   bool data_ready = nrfx_atomic_flag_clear_fetch(&_imu_data_ready);
   if (nrfx_atomic_flag_clear_fetch(&_imu_motion_changed) || data_ready)
   {
      // Read the accelerometer data
      nrf_drv_spi_uninit(&_spi_instance);
      nrf_drv_spi_init(&_spi_instance, &_spi_config, NULL, NULL);
      bool in_motion = imu_in_motion();
      if (data_ready)
         imu_read_accelerometer_data(x_data, y_data, z_data);
      nrf_drv_spi_uninit(&_spi_instance);
      nrfx_gpiote_out_clear(IMU_SPI_SCLK);

      // Fire the IMU callback with the retrieved data
      if (data_ready)
         _data_callback(in_motion, timestamp, x_data, y_data, z_data);
      else
         _data_callback(in_motion, timestamp, NULL, NULL, NULL);
   }
}

#endif  // #if (BOARD_V < 0x11)
