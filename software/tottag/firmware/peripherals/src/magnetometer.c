// Header inclusions ---------------------------------------------------------------------------------------------------

#include <string.h>
#include "ble_config.h"
#include "magnetometer.h"
#include "nrf_delay.h"


// LIS3MDL-specific magnetometer functionality -------------------------------------------------------------------------

#if (BOARD_V >= 0x11)

static read_reg_ptr lis3mdl_read_reg;
static write_reg_ptr lis3mdl_write_reg;

float lis3mdl_from_fs4_to_gauss(int16_t lsb) { return ((float)lsb / 6842.0f); }
float lis3mdl_from_fs8_to_gauss(int16_t lsb) { return ((float)lsb / 3421.0f); }
float lis3mdl_from_fs12_to_gauss(int16_t lsb) { return ((float)lsb / 2281.0f); }
float lis3mdl_from_fs16_to_gauss(int16_t lsb) { return ((float)lsb / 1711.0f); }
float lis3mdl_from_lsb_to_celsius(int16_t lsb) { return ((float)lsb / 8.0f ) + ( 25.0f ); }

nrfx_err_t lis3mdl_data_rate_set(lis3mdl_om_t val)
{
   lis3mdl_ctrl_reg1_t ctrl_reg1;
   lis3mdl_ctrl_reg4_t ctrl_reg4;
   nrfx_err_t ret = lis3mdl_read_reg(LIS3MDL_CTRL_REG1, (uint8_t*)&ctrl_reg1, 1);
   if (ret == NRFX_SUCCESS)
   {
      ctrl_reg1.om = (uint8_t)val;
      ret = lis3mdl_write_reg(LIS3MDL_CTRL_REG1, (uint8_t*)&ctrl_reg1, 1);
   }
   if (ret == NRFX_SUCCESS)        // set mode also for z axis, ctrl_reg4 -> omz
      ret = lis3mdl_read_reg(LIS3MDL_CTRL_REG4, (uint8_t*)&ctrl_reg4, 1);
   if (ret == NRFX_SUCCESS)
   {
      ctrl_reg4.omz = (uint8_t)(((uint8_t) val >> 4) & 0x03U);
      ret = lis3mdl_write_reg(LIS3MDL_CTRL_REG4, (uint8_t*)&ctrl_reg4, 1);
   }
   return ret;
}

nrfx_err_t lis3mdl_data_rate_get(lis3mdl_om_t *val)
{
   lis3mdl_ctrl_reg1_t ctrl_reg1;
   /* z axis, ctrl_reg4 -> omz is aligned with x/y axis ctrl_reg1 -> om*/
   nrfx_err_t ret = lis3mdl_read_reg(LIS3MDL_CTRL_REG1, (uint8_t*)&ctrl_reg1, 1);
   switch (ctrl_reg1.om)
   {
      case LIS3MDL_LP_Hz625:
         *val = LIS3MDL_LP_Hz625;
         break;
      case LIS3MDL_LP_1kHz:
         *val = LIS3MDL_LP_1kHz;
         break;
      case LIS3MDL_MP_560Hz:
         *val = LIS3MDL_MP_560Hz;
         break;
      case LIS3MDL_HP_300Hz:
         *val = LIS3MDL_HP_300Hz;
         break;
      case LIS3MDL_UHP_155Hz:
         *val = LIS3MDL_UHP_155Hz;
         break;
      case LIS3MDL_LP_1Hz25:
         *val = LIS3MDL_LP_1Hz25;
         break;
      case LIS3MDL_LP_2Hz5:
         *val = LIS3MDL_LP_2Hz5;
         break;
      case LIS3MDL_LP_5Hz:
         *val = LIS3MDL_LP_5Hz;
         break;
      case LIS3MDL_LP_10Hz:
         *val = LIS3MDL_LP_10Hz;
         break;
      case LIS3MDL_LP_20Hz:
         *val = LIS3MDL_LP_20Hz;
         break;
      case LIS3MDL_LP_40Hz:
         *val = LIS3MDL_LP_40Hz;
         break;
      case LIS3MDL_LP_80Hz:
         *val = LIS3MDL_LP_80Hz;
         break;
      case LIS3MDL_MP_1Hz25:
         *val = LIS3MDL_MP_1Hz25;
         break;
      case LIS3MDL_MP_2Hz5:
         *val = LIS3MDL_MP_2Hz5;
         break;
      case LIS3MDL_MP_5Hz:
         *val = LIS3MDL_MP_5Hz;
         break;
      case LIS3MDL_MP_10Hz:
         *val = LIS3MDL_MP_10Hz;
         break;
      case LIS3MDL_MP_20Hz:
         *val = LIS3MDL_MP_20Hz;
         break;
      case LIS3MDL_MP_40Hz:
         *val = LIS3MDL_MP_40Hz;
         break;
      case LIS3MDL_MP_80Hz:
         *val = LIS3MDL_MP_80Hz;
         break;
      case LIS3MDL_HP_1Hz25:
         *val = LIS3MDL_HP_1Hz25;
         break;
      case LIS3MDL_HP_2Hz5:
         *val = LIS3MDL_HP_2Hz5;
         break;
      case LIS3MDL_HP_5Hz:
         *val = LIS3MDL_HP_5Hz;
         break;
      case LIS3MDL_HP_10Hz:
         *val = LIS3MDL_HP_10Hz;
         break;
      case LIS3MDL_HP_20Hz:
         *val = LIS3MDL_HP_20Hz;
         break;
      case LIS3MDL_HP_40Hz:
         *val = LIS3MDL_HP_40Hz;
         break;
      case LIS3MDL_HP_80Hz:
         *val = LIS3MDL_HP_80Hz;
         break;
      case LIS3MDL_UHP_1Hz25:
         *val = LIS3MDL_UHP_1Hz25;
         break;
      case LIS3MDL_UHP_2Hz5:
         *val = LIS3MDL_UHP_2Hz5;
         break;
      case LIS3MDL_UHP_5Hz:
         *val = LIS3MDL_UHP_5Hz;
         break;
      case LIS3MDL_UHP_10Hz:
         *val = LIS3MDL_UHP_10Hz;
         break;
      case LIS3MDL_UHP_20Hz:
         *val = LIS3MDL_UHP_20Hz;
         break;
      case LIS3MDL_UHP_40Hz:
         *val = LIS3MDL_UHP_40Hz;
         break;
      case LIS3MDL_UHP_80Hz:
         *val = LIS3MDL_UHP_80Hz;
         break;
      default:
         *val = LIS3MDL_UHP_80Hz;
         break;
   }
   return ret;
}

nrfx_err_t lis3mdl_temperature_meas_set(uint8_t val)
{
   lis3mdl_ctrl_reg1_t ctrl_reg1;
   nrfx_err_t ret = lis3mdl_read_reg(LIS3MDL_CTRL_REG1, (uint8_t*)&ctrl_reg1, 1);
   if (ret == NRFX_SUCCESS)
   {
      ctrl_reg1.temp_en = val;
      ret = lis3mdl_write_reg(LIS3MDL_CTRL_REG1, (uint8_t*)&ctrl_reg1, 1);
   }
   return ret;
}

nrfx_err_t lis3mdl_temperature_meas_get(uint8_t *val)
{
   lis3mdl_ctrl_reg1_t ctrl_reg1;
   nrfx_err_t ret = lis3mdl_read_reg(LIS3MDL_CTRL_REG1, (uint8_t*)&ctrl_reg1, 1);
   *val = (uint8_t)ctrl_reg1.temp_en;
   return ret;
}

nrfx_err_t lis3mdl_full_scale_set(lis3mdl_fs_t val)
{
   lis3mdl_ctrl_reg2_t ctrl_reg2;
   nrfx_err_t ret = lis3mdl_read_reg(LIS3MDL_CTRL_REG2, (uint8_t*)&ctrl_reg2, 1);
   if (ret == NRFX_SUCCESS)
   {
      ctrl_reg2.fs = (uint8_t)val;
      ret = lis3mdl_write_reg(LIS3MDL_CTRL_REG2, (uint8_t*)&ctrl_reg2, 1);
   }
   return ret;
}

nrfx_err_t lis3mdl_full_scale_get(lis3mdl_fs_t *val)
{
   lis3mdl_ctrl_reg2_t ctrl_reg2;
   nrfx_err_t ret = lis3mdl_read_reg(LIS3MDL_CTRL_REG2, (uint8_t*)&ctrl_reg2, 1);
   switch (ctrl_reg2.fs)
   {
      case LIS3MDL_4_GAUSS:
         *val = LIS3MDL_4_GAUSS;
         break;
      case LIS3MDL_8_GAUSS:
         *val = LIS3MDL_8_GAUSS;
         break;
      case LIS3MDL_12_GAUSS:
         *val = LIS3MDL_12_GAUSS;
         break;
      case LIS3MDL_16_GAUSS:
         *val = LIS3MDL_16_GAUSS;
         break;
      default:
         *val = LIS3MDL_4_GAUSS;
         break;
   }
   return ret;
}

nrfx_err_t lis3mdl_operating_mode_set(lis3mdl_md_t val)
{
   lis3mdl_ctrl_reg3_t ctrl_reg3;
   nrfx_err_t ret = lis3mdl_read_reg(LIS3MDL_CTRL_REG3, (uint8_t*)&ctrl_reg3, 1);
   if (ret == NRFX_SUCCESS)
   {
      ctrl_reg3.md = (uint8_t)val;
      ret = lis3mdl_write_reg(LIS3MDL_CTRL_REG3, (uint8_t*)&ctrl_reg3, 1);
   }
   return ret;
}

nrfx_err_t lis3mdl_operating_mode_get(lis3mdl_md_t *val)
{
   lis3mdl_ctrl_reg3_t ctrl_reg3;
   nrfx_err_t ret = lis3mdl_read_reg(LIS3MDL_CTRL_REG3, (uint8_t*)&ctrl_reg3, 1);
   switch (ctrl_reg3.md)
   {
      case LIS3MDL_CONTINUOUS_MODE:
         *val = LIS3MDL_CONTINUOUS_MODE;
         break;
      case LIS3MDL_SINGLE_TRIGGER:
         *val = LIS3MDL_SINGLE_TRIGGER;
         break;
      case LIS3MDL_POWER_DOWN:
         *val = LIS3MDL_POWER_DOWN;
         break;
      default:
         *val = LIS3MDL_POWER_DOWN;
         break;
   }
   return ret;
}

nrfx_err_t lis3mdl_fast_low_power_set(uint8_t val)
{
   lis3mdl_ctrl_reg3_t ctrl_reg3;
   nrfx_err_t ret = lis3mdl_read_reg(LIS3MDL_CTRL_REG3, (uint8_t*)&ctrl_reg3, 1);
   if (ret == NRFX_SUCCESS)
   {
      ctrl_reg3.lp = val;
      ret = lis3mdl_write_reg(LIS3MDL_CTRL_REG3, (uint8_t*)&ctrl_reg3, 1);
   }
   return ret;
}

nrfx_err_t lis3mdl_fast_low_power_get(uint8_t *val)
{
   lis3mdl_ctrl_reg3_t ctrl_reg3;
   nrfx_err_t ret = lis3mdl_read_reg(LIS3MDL_CTRL_REG3, (uint8_t*)&ctrl_reg3, 1);
   *val = (uint8_t)ctrl_reg3.lp;
   return ret;
}

nrfx_err_t lis3mdl_block_data_update_set(uint8_t val)
{
   lis3mdl_ctrl_reg5_t ctrl_reg5;
   nrfx_err_t ret = lis3mdl_read_reg(LIS3MDL_CTRL_REG5, (uint8_t*)&ctrl_reg5, 1);
   if (ret == NRFX_SUCCESS)
   {
      ctrl_reg5.bdu = val;
      ret = lis3mdl_write_reg(LIS3MDL_CTRL_REG5, (uint8_t*)&ctrl_reg5, 1);
   }
   return ret;
}

nrfx_err_t lis3mdl_block_data_update_get(uint8_t *val)
{
   lis3mdl_ctrl_reg5_t ctrl_reg5;
   nrfx_err_t ret = lis3mdl_read_reg(LIS3MDL_CTRL_REG5, (uint8_t*)&ctrl_reg5, 1);
   *val = (uint8_t)ctrl_reg5.bdu;
   return ret;
}

nrfx_err_t lis3mdl_high_part_cycle_set(uint8_t val)
{
   lis3mdl_ctrl_reg5_t ctrl_reg5;
   nrfx_err_t ret = lis3mdl_read_reg(LIS3MDL_CTRL_REG5, (uint8_t*)&ctrl_reg5, 1);
   if (ret == NRFX_SUCCESS)
   {
      ctrl_reg5.fast_read = val;
      ret = lis3mdl_write_reg(LIS3MDL_CTRL_REG5, (uint8_t*)&ctrl_reg5, 1);
   }
   return ret;
}

nrfx_err_t lis3mdl_high_part_cycle_get(uint8_t *val)
{
   lis3mdl_ctrl_reg5_t ctrl_reg5;
   nrfx_err_t ret = lis3mdl_read_reg(LIS3MDL_CTRL_REG5, (uint8_t*)&ctrl_reg5, 1);
   *val = (uint8_t)ctrl_reg5.fast_read;
   return ret;
}

nrfx_err_t lis3mdl_mag_data_ready_get(uint8_t *val)
{
   lis3mdl_status_reg_t status_reg;
   nrfx_err_t ret = lis3mdl_read_reg(LIS3MDL_STATUS_REG, (uint8_t*)&status_reg, 1);
   *val = (uint8_t)status_reg.zyxda;
   return ret;
}

nrfx_err_t lis3mdl_mag_data_ovr_get(uint8_t *val)
{
   lis3mdl_status_reg_t status_reg;
   nrfx_err_t ret = lis3mdl_read_reg(LIS3MDL_STATUS_REG, (uint8_t*)&status_reg, 1);
   *val = (uint8_t)status_reg.zyxor;
   return ret;
}

nrfx_err_t lis3mdl_magnetic_raw_get(int16_t *val)
{
   uint8_t buff[6];
   nrfx_err_t ret = lis3mdl_read_reg(LIS3MDL_OUT_X_L, (uint8_t*) buff, 6);
   val[0] = (int16_t)buff[1];
   val[0] = (val[0] * 256) +  (int16_t)buff[0];
   val[1] = (int16_t)buff[3];
   val[1] = (val[1] * 256) +  (int16_t)buff[2];
   val[2] = (int16_t)buff[5];
   val[2] = (val[2] * 256) +  (int16_t)buff[4];
   return ret;
}

nrfx_err_t lis3mdl_temperature_raw_get(int16_t *val)
{
   uint8_t buff[2];
   nrfx_err_t ret = lis3mdl_read_reg(LIS3MDL_TEMP_OUT_L, (uint8_t*) buff, 2);
   *val = (int16_t)buff[1];
   *val = (*val * 256) +  (int16_t)buff[0];
   return ret;
}

nrfx_err_t lis3mdl_device_id_get(uint8_t *buff)
{
   nrfx_err_t ret = lis3mdl_read_reg(LIS3MDL_WHO_AM_I, (uint8_t*) buff, 1);
   return ret;
}

nrfx_err_t lis3mdl_self_test_set(uint8_t val)
{
   lis3mdl_ctrl_reg1_t ctrl_reg1;
   nrfx_err_t ret = lis3mdl_read_reg(LIS3MDL_CTRL_REG1, (uint8_t*)&ctrl_reg1, 1);
   if (ret == NRFX_SUCCESS)
   {
      ctrl_reg1.st = (uint8_t)val;
      ret = lis3mdl_write_reg(LIS3MDL_CTRL_REG1, (uint8_t*)&ctrl_reg1, 1);
   }
   return ret;
}

nrfx_err_t lis3mdl_self_test_get(uint8_t *val)
{
   lis3mdl_ctrl_reg1_t ctrl_reg1;
   nrfx_err_t ret = lis3mdl_read_reg(LIS3MDL_CTRL_REG1, (uint8_t*)&ctrl_reg1, 1);
   *val = (uint8_t)ctrl_reg1.st;
   return ret;
}

nrfx_err_t lis3mdl_reset_set(uint8_t val)
{
   lis3mdl_ctrl_reg2_t ctrl_reg2;
   nrfx_err_t ret = lis3mdl_read_reg(LIS3MDL_CTRL_REG2, (uint8_t*)&ctrl_reg2, 1);
   if (ret == NRFX_SUCCESS)
   {
      ctrl_reg2.soft_rst = val;
      ret = lis3mdl_write_reg(LIS3MDL_CTRL_REG2, (uint8_t*)&ctrl_reg2, 1);
   }
   return ret;
}

nrfx_err_t lis3mdl_reset_get(uint8_t *val)
{
   lis3mdl_ctrl_reg2_t ctrl_reg2;
   nrfx_err_t ret = lis3mdl_read_reg(LIS3MDL_CTRL_REG2, (uint8_t*)&ctrl_reg2, 1);
   *val = (uint8_t)ctrl_reg2.soft_rst;
   return ret;
}

nrfx_err_t lis3mdl_boot_set(uint8_t val)
{
   lis3mdl_ctrl_reg2_t ctrl_reg2;
   nrfx_err_t ret = lis3mdl_read_reg(LIS3MDL_CTRL_REG2, (uint8_t*)&ctrl_reg2, 1);
   if (ret == NRFX_SUCCESS)
   {
      ctrl_reg2.reboot = val;
      ret = lis3mdl_write_reg(LIS3MDL_CTRL_REG2, (uint8_t*)&ctrl_reg2, 1);
   }
   return ret;
}

nrfx_err_t lis3mdl_boot_get(uint8_t *val)
{
   lis3mdl_ctrl_reg2_t ctrl_reg2;
   nrfx_err_t ret = lis3mdl_read_reg(LIS3MDL_CTRL_REG2, (uint8_t*)&ctrl_reg2, 1);
   *val = (uint8_t)ctrl_reg2.reboot;
   return ret;
}

nrfx_err_t lis3mdl_data_format_set(lis3mdl_ble_t val)
{
   lis3mdl_ctrl_reg4_t ctrl_reg4;
   nrfx_err_t ret = lis3mdl_read_reg(LIS3MDL_CTRL_REG4, (uint8_t*)&ctrl_reg4, 1);
   if (ret == NRFX_SUCCESS)
   {
      ctrl_reg4.ble = (uint8_t)val;
      ret = lis3mdl_write_reg(LIS3MDL_CTRL_REG4, (uint8_t*)&ctrl_reg4, 1);
   }
   return ret;
}

nrfx_err_t lis3mdl_data_format_get(lis3mdl_ble_t *val)
{
   lis3mdl_ctrl_reg4_t ctrl_reg4;
   nrfx_err_t ret = lis3mdl_read_reg(LIS3MDL_CTRL_REG4, (uint8_t*)&ctrl_reg4, 1);
   switch (ctrl_reg4.ble)
   {
      case LIS3MDL_LSB_AT_LOW_ADD:
         *val = LIS3MDL_LSB_AT_LOW_ADD;
         break;
      case LIS3MDL_MSB_AT_LOW_ADD:
         *val = LIS3MDL_MSB_AT_LOW_ADD;
         break;
      default:
         *val = LIS3MDL_LSB_AT_LOW_ADD;
         break;
   }
   return ret;
}

nrfx_err_t lis3mdl_status_get(lis3mdl_status_reg_t *val)
{
   return lis3mdl_read_reg(LIS3MDL_STATUS_REG, (uint8_t*) val, 1);
}

nrfx_err_t lis3mdl_int_config_set(lis3mdl_int_cfg_t *val)
{
   return lis3mdl_write_reg(LIS3MDL_INT_CFG, (uint8_t*) val, 1);
}

nrfx_err_t lis3mdl_int_config_get(lis3mdl_int_cfg_t *val)
{
   return lis3mdl_read_reg(LIS3MDL_INT_CFG, (uint8_t*) val, 1);
}

nrfx_err_t lis3mdl_int_generation_set(uint8_t val)
{
   lis3mdl_int_cfg_t int_cfg;
   nrfx_err_t ret = lis3mdl_read_reg(LIS3MDL_INT_CFG, (uint8_t*)&int_cfg, 1);
   if (ret == NRFX_SUCCESS)
   {
      int_cfg.ien = val;
      ret = lis3mdl_write_reg(LIS3MDL_INT_CFG, (uint8_t*)&int_cfg, 1);
   }
   return ret;
}

nrfx_err_t lis3mdl_int_generation_get(uint8_t *val)
{
   lis3mdl_int_cfg_t int_cfg;
   nrfx_err_t ret = lis3mdl_read_reg(LIS3MDL_INT_CFG, (uint8_t*)&int_cfg, 1);
   *val = (uint8_t)int_cfg.ien;
   return ret;
}

nrfx_err_t lis3mdl_int_notification_mode_set(lis3mdl_lir_t val)
{
   lis3mdl_int_cfg_t int_cfg;
   nrfx_err_t ret = lis3mdl_read_reg(LIS3MDL_INT_CFG, (uint8_t*)&int_cfg, 1);
   if (ret == NRFX_SUCCESS)
   {
      int_cfg.lir = (uint8_t)val;
      ret = lis3mdl_write_reg(LIS3MDL_INT_CFG, (uint8_t*)&int_cfg, 1);
   }
   return ret;
}

nrfx_err_t lis3mdl_int_notification_mode_get(lis3mdl_lir_t *val)
{
   lis3mdl_int_cfg_t int_cfg;
   nrfx_err_t ret = lis3mdl_read_reg(LIS3MDL_INT_CFG, (uint8_t*)&int_cfg, 1);
   switch (int_cfg.lir)
   {
      case LIS3MDL_INT_PULSED:
         *val = LIS3MDL_INT_PULSED;
         break;
      case LIS3MDL_INT_LATCHED:
         *val = LIS3MDL_INT_LATCHED;
         break;
      default:
         *val = LIS3MDL_INT_PULSED;
         break;
   }
   return ret;
}

nrfx_err_t lis3mdl_int_polarity_set(lis3mdl_iea_t val)
{
   lis3mdl_int_cfg_t int_cfg;
   nrfx_err_t ret = lis3mdl_read_reg(LIS3MDL_INT_CFG, (uint8_t*)&int_cfg, 1);
   if (ret == NRFX_SUCCESS)
   {
      int_cfg.iea = (uint8_t)val;
      ret = lis3mdl_write_reg(LIS3MDL_INT_CFG, (uint8_t*)&int_cfg, 1);
   }
   return ret;
}

nrfx_err_t lis3mdl_int_polarity_get(lis3mdl_iea_t *val)
{
   lis3mdl_int_cfg_t int_cfg;
   nrfx_err_t ret = lis3mdl_read_reg(LIS3MDL_INT_CFG, (uint8_t*)&int_cfg, 1);
   switch (int_cfg.iea)
   {
      case LIS3MDL_ACTIVE_HIGH:
         *val = LIS3MDL_ACTIVE_HIGH;
         break;
      case LIS3MDL_ACTIVE_LOW:
         *val = LIS3MDL_ACTIVE_LOW;
         break;
      default:
         *val = LIS3MDL_ACTIVE_HIGH;
         break;
   }
   return ret;
}

nrfx_err_t lis3mdl_int_on_z_ax_set(uint8_t val)
{
   lis3mdl_int_cfg_t int_cfg;
   nrfx_err_t ret = lis3mdl_read_reg(LIS3MDL_INT_CFG, (uint8_t*)&int_cfg, 1);
   if (ret == NRFX_SUCCESS)
   {
      int_cfg.zien = val;
      ret = lis3mdl_write_reg(LIS3MDL_INT_CFG, (uint8_t*)&int_cfg, 1);
   }
   return ret;
}

nrfx_err_t lis3mdl_int_on_z_ax_get(uint8_t *val)
{
   lis3mdl_int_cfg_t int_cfg;
   nrfx_err_t ret = lis3mdl_read_reg(LIS3MDL_INT_CFG, (uint8_t*)&int_cfg, 1);
   *val = (uint8_t)int_cfg.zien;
   return ret;
}

nrfx_err_t lis3mdl_int_on_y_ax_set(uint8_t val)
{
   lis3mdl_int_cfg_t int_cfg;
   nrfx_err_t ret = lis3mdl_read_reg(LIS3MDL_INT_CFG, (uint8_t*)&int_cfg, 1);
   if (ret == NRFX_SUCCESS)
   {
      int_cfg.yien = val;
      ret = lis3mdl_write_reg(LIS3MDL_INT_CFG, (uint8_t*)&int_cfg, 1);
   }
   return ret;
}

nrfx_err_t lis3mdl_int_on_y_ax_get(uint8_t *val)
{
   lis3mdl_int_cfg_t int_cfg;
   nrfx_err_t ret = lis3mdl_read_reg(LIS3MDL_INT_CFG, (uint8_t*)&int_cfg, 1);
   *val = (uint8_t)int_cfg.yien;
   return ret;
}

nrfx_err_t lis3mdl_int_on_x_ax_set(uint8_t val)
{
   lis3mdl_int_cfg_t int_cfg;
   nrfx_err_t ret = lis3mdl_read_reg(LIS3MDL_INT_CFG, (uint8_t*)&int_cfg, 1);
   if (ret == NRFX_SUCCESS)
   {
      int_cfg.xien = val;
      ret = lis3mdl_write_reg(LIS3MDL_INT_CFG, (uint8_t*)&int_cfg, 1);
   }
   return ret;
}

nrfx_err_t lis3mdl_int_on_x_ax_get(uint8_t *val)
{
   lis3mdl_int_cfg_t int_cfg;
   nrfx_err_t ret = lis3mdl_read_reg(LIS3MDL_INT_CFG, (uint8_t*)&int_cfg, 1);
   *val = (uint8_t)int_cfg.xien;
   return ret;
}

nrfx_err_t lis3mdl_int_source_get(lis3mdl_int_src_t *val)
{
   return lis3mdl_read_reg(LIS3MDL_INT_SRC, (uint8_t*) val, 1);
}

nrfx_err_t lis3mdl_interrupt_event_flag_get(uint8_t *val)
{
   lis3mdl_int_src_t int_src;
   nrfx_err_t ret = lis3mdl_read_reg(LIS3MDL_INT_SRC, (uint8_t*)&int_src, 1);
   *val = (uint8_t)int_src.int_;
   return ret;
}

nrfx_err_t lis3mdl_int_mag_over_range_flag_get(uint8_t *val)
{
   lis3mdl_int_src_t int_src;
   nrfx_err_t ret = lis3mdl_read_reg(LIS3MDL_INT_SRC, (uint8_t*)&int_src, 1);
   *val = (uint8_t)int_src.mroi;
   return ret;
}

nrfx_err_t lis3mdl_int_neg_z_flag_get(uint8_t *val)
{
   lis3mdl_int_src_t int_src;
   nrfx_err_t ret = lis3mdl_read_reg(LIS3MDL_INT_SRC, (uint8_t*)&int_src, 1);
   *val = (uint8_t)int_src.nth_z;
   return ret;
}

nrfx_err_t lis3mdl_int_neg_y_flag_get(uint8_t *val)
{
   lis3mdl_int_src_t int_src;
   nrfx_err_t ret = lis3mdl_read_reg(LIS3MDL_INT_SRC, (uint8_t*)&int_src, 1);
   *val = (uint8_t)int_src.nth_y;
   return ret;
}

nrfx_err_t lis3mdl_int_neg_x_flag_get(uint8_t *val)
{
   lis3mdl_int_src_t int_src;
   nrfx_err_t ret = lis3mdl_read_reg(LIS3MDL_INT_SRC, (uint8_t*)&int_src, 1);
   *val = (uint8_t)int_src.nth_x;
   return ret;
}

nrfx_err_t lis3mdl_int_pos_z_flag_get(uint8_t *val)
{
   lis3mdl_int_src_t int_src;
   nrfx_err_t ret = lis3mdl_read_reg(LIS3MDL_INT_SRC, (uint8_t*)&int_src, 1);
   *val = (uint8_t)int_src.pth_z;
   return ret;
}

nrfx_err_t lis3mdl_int_pos_y_flag_get(uint8_t *val)
{
   lis3mdl_int_src_t int_src;
   nrfx_err_t ret = lis3mdl_read_reg(LIS3MDL_INT_SRC, (uint8_t*)&int_src, 1);
   *val = (uint8_t)int_src.pth_y;
   return ret;
}

nrfx_err_t lis3mdl_int_pos_x_flag_get(uint8_t *val)
{
   lis3mdl_int_src_t int_src;
   nrfx_err_t ret = lis3mdl_read_reg(LIS3MDL_INT_SRC, (uint8_t*)&int_src, 1);
   *val = (uint8_t)int_src.pth_x;
   return ret;
}

nrfx_err_t lis3mdl_int_threshold_set(uint16_t val)
{
   uint8_t buff[2];
   buff[1] = (uint8_t) (val / 256U);
   buff[0] = (uint8_t) (val - (buff[1] * 256U));
   nrfx_err_t ret = lis3mdl_write_reg(LIS3MDL_INT_THS_L, buff, 2);
   return ret;
}

nrfx_err_t lis3mdl_int_threshold_get(uint16_t *val)
{
   uint8_t buff[2];
   nrfx_err_t ret = lis3mdl_read_reg(LIS3MDL_INT_THS_L, buff, 2);
   *val = buff[1];
   *val = (*val * 256U) +  buff[0];
   return ret;
}

nrfx_err_t lis3mdl_spi_mode_set(lis3mdl_sim_t val)
{
   lis3mdl_ctrl_reg3_t ctrl_reg3;
   nrfx_err_t ret = lis3mdl_read_reg(LIS3MDL_CTRL_REG3, (uint8_t*)&ctrl_reg3, 1);
   if (ret == NRFX_SUCCESS)
   {
      ctrl_reg3.sim = (uint8_t)val;
      ret = lis3mdl_write_reg(LIS3MDL_CTRL_REG3, (uint8_t*)&ctrl_reg3, 1);
   }
   return ret;
}

nrfx_err_t lis3mdl_spi_mode_get(lis3mdl_sim_t *val)
{
   lis3mdl_ctrl_reg3_t ctrl_reg3;
   nrfx_err_t ret = lis3mdl_read_reg(LIS3MDL_CTRL_REG3, (uint8_t*)&ctrl_reg3, 1);
   switch (ctrl_reg3.sim)
   {
      case LIS3MDL_SPI_4_WIRE:
         *val = LIS3MDL_SPI_4_WIRE;
         break;
      case LIS3MDL_SPI_3_WIRE:
         *val = LIS3MDL_SPI_3_WIRE;
         break;
      default:
         *val = LIS3MDL_SPI_4_WIRE;
         break;
   }
   return ret;
}

void lis3mdl_set_read_reg_function(read_reg_ptr read_reg_function) { lis3mdl_read_reg = read_reg_function; }
void lis3mdl_set_write_reg_function(write_reg_ptr write_reg_function) { lis3mdl_write_reg = write_reg_function; }

#endif  // #if (BOARD_V >= 0x11)
