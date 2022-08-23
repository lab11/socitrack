// Header inclusions ---------------------------------------------------------------------------------------------------

#include "nrf_delay.h"
#include "nrfx_spi.h"
#include "rtc.h"
#include "rtc_external.h"


// Real-time clock state variables -------------------------------------------------------------------------------------

static nrfx_spi_t _rtc_spi_instance = NRFX_SPI_INSTANCE(RTC_SPI_BUS_IDX);
static nrfx_spi_config_t _rtc_spi_config = NRFX_SPI_DEFAULT_CONFIG;
static ab1815_control_t _ctrl_config = { 0 };
static ab1815_int_config_t _int_config = { 0 };
static uint8_t _ab1815_write_buf[257] = { 0 };


// Helper functions ----------------------------------------------------------------------------------------------------

#ifdef FORCE_RTC_RESET
static int ascii_to_int(char c) { return (c == ' ') ? 0 : (int)(c - '0'); }
static int month_to_int(const char *c)
{
   // Read 3 chars and select month
   char month[] = { c[0], c[1], c[2], '\0' };
   if (strcmp(month, "Jan") == 0)
      return 0;
   else if (strcmp(month, "Feb") == 0)
      return 1;
   else if (strcmp(month, "Mar") == 0)
      return 2;
   else if (strcmp(month, "Apr") == 0)
      return 3;
   else if (strcmp(month, "May") == 0)
      return 4;
   else if (strcmp(month, "Jun") == 0)
      return 5;
   else if (strcmp(month, "Jul") == 0)
      return 6;
   else if (strcmp(month, "Aug") == 0)
      return 7;
   else if (strcmp(month, "Sep") == 0)
      return 8;
   else if (strcmp(month, "Oct") == 0)
      return 9;
   else if (strcmp(month, "Nov") == 0)
      return 10;
   else if (strcmp(month, "Dec") == 0)
      return 11;
   else
      return 0;
}
#endif // #ifdef FORCE_RTC_RESET

inline int get_tens(int x) { return x / 10; }
inline int get_ones(int x) { return x % 10; }

static void ab1815_form_time_buffer(const struct tm *time_struct, uint8_t *buf)
{
   buf[0] = 0;
   buf[1] = (uint8_t)(((get_tens(time_struct->tm_sec) & 0x00000007) << 4) | (get_ones(time_struct->tm_sec) & 0x0000000F));
   buf[2] = (uint8_t)(((get_tens(time_struct->tm_min) & 0x00000007) << 4) | (get_ones(time_struct->tm_min) & 0x0000000F));
   buf[3] = (uint8_t)(((get_tens(time_struct->tm_hour) & 0x00000003) << 4) | (get_ones(time_struct->tm_hour) & 0x0000000F));
   buf[4] = (uint8_t)(((get_tens(time_struct->tm_mday) & 0x00000003) << 4) | (get_ones(time_struct->tm_mday) & 0x0000000F));
   buf[5] = (uint8_t)(((get_tens(time_struct->tm_mon + 1) & 0x00000001) << 4) | (get_ones(time_struct->tm_mon + 1) & 0x0000000F));
   buf[6] = (uint8_t)(((get_tens(time_struct->tm_year % 100) & 0x0000000F) << 4) | (get_ones(time_struct->tm_year % 100) & 0x0000000F));
   buf[7] = (uint8_t)(time_struct->tm_wday & 0x00000007);
}

static bool ab1815_wait_for_ready(uint16_t timeout_ms)
{
   // Wait until the RTC_INT line is asserted
   uint16_t counter = 0, step_size_ms = 10;
   while (!nrfx_gpiote_in_is_set(RTC_INT) && (counter < timeout_ms))
   {
      nrf_delay_ms(step_size_ms);
      counter += step_size_ms;
   }

   // Return whether the RTC chip became available in the allotted time
   if (counter < timeout_ms)
   {
      nrf_delay_ms(100);
      return true;
   }
   else
   {
      printf("WARNING: RTC not ready yet, but timeout of %u ms expired!\n", timeout_ms);
      return false;
   }
}

static void ab1815_init(void)
{
   // Setup interrupt pins
   nrf_gpio_cfg_input(RTC_INT, NRF_GPIO_PIN_PULLUP);
   nrfx_gpiote_out_config_t rtc_wdi_pin_config = NRFX_GPIOTE_CONFIG_OUT_SIMPLE(0);
   nrfx_gpiote_out_init(RTC_WDI, &rtc_wdi_pin_config);
}

static bool ab1815_read_reg(uint8_t reg, uint8_t *read_buf, size_t len)
{
   // Read from the requested register
   uint8_t read_register = reg & 0x7F;
   nrfx_spi_xfer_desc_t read_transfer = NRFX_SPI_XFER_TRX(&read_register, 1, read_buf, 1 + len);
   nrfx_err_t err_code = nrfx_spi_xfer(&_rtc_spi_instance, &read_transfer, 0);
   return (err_code == NRFX_SUCCESS);
}

static bool ab1815_write_reg(uint8_t reg, uint8_t *write_buf, size_t len)
{
   // Write to the requested register
   _ab1815_write_buf[0] = reg | 0x80;
   memcpy(&_ab1815_write_buf[1], write_buf, len);
   nrfx_spi_xfer_desc_t write_transfer = NRFX_SPI_XFER_TX(_ab1815_write_buf, 1 + len);
   return (nrfx_spi_xfer(&_rtc_spi_instance, &write_transfer, 0) == NRFX_SUCCESS);
}

static uint32_t ab1815_get_time(void)
{
   uint32_t unix_time = 0;
   uint8_t read[10] = { 0 }, retries_remaining = 3;
   while (!unix_time && retries_remaining--)
   {
      if (ab1815_read_reg(AB1815_HUND, read, 8))
      {
         struct tm time_struct = {
               .tm_sec = (10 * (int)((read[2] >> 4) & 0x07)) + (int)(read[2] & 0x0F),
               .tm_min = (10 * (int)((read[3] >> 4) & 0x07)) + (int)(read[3] & 0x0F),
               .tm_hour = (10 * (int)((read[4] >> 4) & 0x03)) + (int)(read[4] & 0x0F),
               .tm_mday = (10 * (int)((read[5] >> 4) & 0x03)) + (int)(read[5] & 0x0F),
               .tm_mon = (10 * (int)((read[6] >> 4) & 0x01)) + (int)(read[6] & 0x0F) - 1,
               .tm_year = (10 * (int)((read[7] >> 4) & 0x0F)) + (int)(read[7] & 0x0F) + 100,
               .tm_wday = -1,
               .tm_yday = -1,
               .tm_isdst = -1
         };
         if ((time_struct.tm_sec >= 0) && (time_struct.tm_sec <= 60) &&
             (time_struct.tm_min >= 0) && (time_struct.tm_min < 60) &&
             (time_struct.tm_hour >= 0) && (time_struct.tm_hour < 24) &&
             (time_struct.tm_mday > 0) && (time_struct.tm_mday < 32) &&
             (time_struct.tm_mon >= 0) && (time_struct.tm_mon < 12) &&
             (time_struct.tm_year > 120) && (time_struct.tm_year < 200))
            unix_time = (uint32_t)mktime(&time_struct);
      }
   }
   return unix_time;
}

static bool ab1815_set_config(void)
{
   // Control1
   uint8_t write = _ctrl_config.stop << 7 | _ctrl_config.hour_12 << 6 | _ctrl_config.OUTB     << 5 |
                   _ctrl_config.OUT  << 4 | _ctrl_config.rst_pol << 3 | _ctrl_config.auto_rst << 2 |
                   _ctrl_config.write_rtc;
   if (!ab1815_write_reg(AB1815_CONTROL1, &write, 1))
      return false;

   // Control2
   write = _ctrl_config.psw_nirq2_function << 2 | _ctrl_config.fout_nirq_function;
   if (!ab1815_write_reg(AB1815_CONTROL2, &write, 1))
      return false;

   // Output Control
   write = AB1815_CONF_KEY_REG;
   if (!ab1815_write_reg(AB1815_CONFIGURATION_KEY, &write, 1))
      return false;
   write = (0x1 << 5) | (0x1 << 4);
   if (!ab1815_write_reg(AB1815_OUTPUT_CTRL, &write, 1))
      return false;

   // Batmode - disable IO if main battery is removed
   write = AB1815_CONF_KEY_REG;
   if (!ab1815_write_reg(AB1815_CONFIGURATION_KEY, &write, 1))
      return false;
   write = 0;
   if (!ab1815_write_reg(AB1815_BATMODE, &write, 1))
      return false;

   // Disable the Countdown Timer
   write = 0;
   if (!ab1815_write_reg(AB1815_COUNTDOWN_CTRL, &write, 1))
      return false;
   write = 0xFF;
   return ab1815_write_reg(AB1815_COUNTDOWN_TIMER, &write, 1);
}

static bool ab1815_set_time(const struct tm *time_struct)
{
   // Ensure that the RTC write bit is enabled
   if (_ctrl_config.write_rtc != 1)
   {
      _ctrl_config.write_rtc = 1;
      if (!ab1815_set_config())
      {
         _ctrl_config.write_rtc = 0;
         return false;
      }
   }

   uint8_t write[8] = { 0 };
   ab1815_form_time_buffer(time_struct, write);
   if (!ab1815_write_reg(AB1815_HUND, write, 8))
      return false;

   // Ensure that the RTC write bit is disabled to prevent unintended access
   _ctrl_config.write_rtc = 0;
   if (!ab1815_set_config())
      _ctrl_config.write_rtc = 1;
   return true;
}

static bool ab1815_init_time(void)
{
#ifdef FORCE_RTC_RESET

   const char _datetime[] = _DATETIME;  // the format is "Tue Jan  1 00:00:00 UTC 2000"
   printf("INFO: Forcing RTC reset to %s\n", _datetime);
   struct tm comp_time = {
         .tm_sec = (ascii_to_int(_datetime[17]) * 10) + ascii_to_int(_datetime[18]),
         .tm_min = (ascii_to_int(_datetime[14]) * 10) + ascii_to_int(_datetime[15]),
         .tm_hour = (ascii_to_int(_datetime[11]) * 10) + ascii_to_int(_datetime[12]),
         .tm_mday = (ascii_to_int(_datetime[8]) * 10) + ascii_to_int(_datetime[9]),
         .tm_mon = month_to_int(&_datetime[4]),
         .tm_year = (ascii_to_int(_datetime[24]) * 1000) + (ascii_to_int(_datetime[25]) * 100) +
                    (ascii_to_int(_datetime[26]) * 10) + ascii_to_int(_datetime[27]) - 1900,
         .tm_wday = -1,
         .tm_yday = -1,
         .tm_isdst = -1
   };

   uint8_t time_properly_set = 0, num_retries = 3;
   uint32_t set_time = (uint32_t)mktime(&comp_time);
   while (!time_properly_set && num_retries--)
   {
      ab1815_set_time(&comp_time);
      uint32_t retrieved_time = ab1815_get_time();
      time_properly_set = (set_time <= retrieved_time) && ((retrieved_time - 60) <= set_time);
   }
   printf("%s: RTC clock was %s set to the current datetime\n", time_properly_set ? "INFO" : "ERROR", time_properly_set ? "properly" : "unable to be");
   return time_properly_set;

#else
   return 1;
#endif
}

static bool ab1815_set_int_config(void)
{
   uint8_t write = _int_config.century_en << 7 | _int_config.int_mode << 5 | _int_config.bat_low_en << 4 |
                   _int_config.timer_en   << 3 | _int_config.alarm_en << 2 | _int_config.xt2_en     << 1 |
                   _int_config.xt1_en;
   return ab1815_write_reg(AB1815_INT_MASK, &write, 1);
}

static bool ab1815_use_xt_oscillator(void)
{
   // Clear all current status bits
   uint8_t write = 0;
   if (!ab1815_write_reg(AB1815_OSCILLATOR_STATUS, &write, 1))
      return false;
   if (!ab1815_write_reg(AB1815_CALIB_XT, &write, 1))
      return false;

   // Enable writing to the Autocalibration Filter Register and enable the AF filter
   write = AB1815_CONF_KEY_REG;
   if (!ab1815_write_reg(AB1815_CONFIGURATION_KEY, &write, 1))
      return false;
   write = 0xA0;
   if (!ab1815_write_reg(AB1815_AFCTRL, &write, 1))
      return false;

   // Enable writing to the Oscillator Control Register, enable XT oscillator, and enable failover to RC oscillator
   write = AB1815_CONF_KEY_OSC;
   if (!ab1815_write_reg(AB1815_CONFIGURATION_KEY, &write, 1))
      return false;
   write = 0x48;
   return ab1815_write_reg(AB1815_OSCILLATOR_CTRL, &write, 1);
}

static bool ab1815_enable_trickle_charger(void)
{
   // Trickle - needs to be enabled first
   uint8_t write = AB1815_CONF_KEY_REG;
   if (!ab1815_write_reg(AB1815_CONFIGURATION_KEY, &write, 1))
      return false;
   write = (0b1010 << 4) | (0b01 << 2) | (0b01);
   return ab1815_write_reg(AB1815_TRICKLE, &write, 1);
}

static bool ab1815_clear_watchdog(void)
{
   uint8_t buf = 0;
   return ab1815_write_reg(AB1815_WATCHDOG_TIMER, &buf, 1);
}

static void ab1815_printTime(uint32_t timestamp)
{
   time_t current_timestamp = (time_t)timestamp;
   struct tm *current_time = gmtime(&current_timestamp);
   printf("INFO: RTC time is %02u:%02u:%02u, %04u/%02u/%02u\n",
         current_time->tm_hour, current_time->tm_min, current_time->tm_sec,
         current_time->tm_year + 1900, current_time->tm_mon + 1, current_time->tm_mday);
}

bool rtc_external_init(void)
{
   // Initialize the chip and define common RTC settings
   ab1815_init();
   _ctrl_config.OUT = 1;
   _ctrl_config.auto_rst = 1;
   _int_config.int_mode = 0x3;

   // Setup SPI communications
   _rtc_spi_config.ss_pin = RTC_SPI_CS;
   _rtc_spi_config.sck_pin = RTC_SPI_SCLK;
   _rtc_spi_config.miso_pin = RTC_SPI_MISO;
   _rtc_spi_config.mosi_pin = RTC_SPI_MOSI;
   _rtc_spi_config.mode = NRF_SPI_MODE_0;
   _rtc_spi_config.frequency = NRF_SPI_FREQ_1M;
   bool success = (nrfx_spi_init(&_rtc_spi_instance, &_rtc_spi_config, NULL, NULL) == NRFX_SUCCESS);

   // Wait for the RTC chip to become ready and reset its status registers
   uint8_t val = 0;
   ab1815_wait_for_ready(1000);
   ab1815_read_reg(AB1815_STATUS, &val, 1);
   ab1815_read_reg(AB1815_STATUS, &val, 1);

   // Set configurations
   success = (success && ab1815_clear_watchdog());
   success = (success && ab1815_set_config());
   success = (success && ab1815_set_int_config());
   success = (success && ab1815_enable_trickle_charger());
   success = (success && ab1815_use_xt_oscillator());

   // Initialize time
   success = (success && ab1815_init_time());

   // Set nRF time from the RTC
   uint32_t current_timestamp = ab1815_get_time();
   success = (success && (current_timestamp > MINIMUM_VALID_TIMESTAMP) && (current_timestamp < MAXIMUM_VALID_TIMESTAMP));
   if (success)
   {
      rtc_set_current_time(current_timestamp);
      ab1815_printTime(current_timestamp);
   }

   // De-initialize SPI communications
   nrfx_spi_uninit(&_rtc_spi_instance);
   nrfx_gpiote_out_clear(RTC_SPI_SCLK);
   return success;
}

bool rtc_external_set_timestamp(uint32_t unix_timestamp)
{
   // Ensure the timestamp is valid
   bool success = false;
   log_printf("INFO: Setting timestamp from the network: %lu\n", unix_timestamp);
   if ((unix_timestamp > MINIMUM_VALID_TIMESTAMP) && (unix_timestamp < MAXIMUM_VALID_TIMESTAMP))
   {
      // Set the internal RTC time
      time_t timestamp = (time_t)unix_timestamp;
      rtc_set_current_time(unix_timestamp);
      success = true;

      // Initialize RTC chip communications and store the timestamp
      nrfx_spi_uninit(&_rtc_spi_instance);
      if (nrfx_spi_init(&_rtc_spi_instance, &_rtc_spi_config, NULL, NULL) == NRFX_SUCCESS)
      {
         ab1815_set_time(gmtime(&timestamp));
         nrfx_spi_uninit(&_rtc_spi_instance);
         nrfx_gpiote_out_clear(RTC_SPI_SCLK);
         ab1815_printTime(unix_timestamp);
      }
   }
   return success;
}

uint32_t rtc_external_sync_to_internal(void)
{
   // Initialize RTC chip communications and retrieve the current timestamp
   uint32_t timestamp = 0;
   nrfx_spi_uninit(&_rtc_spi_instance);
   if (nrfx_spi_init(&_rtc_spi_instance, &_rtc_spi_config, NULL, NULL) == NRFX_SUCCESS)
   {
      timestamp = ab1815_get_time();
      nrfx_spi_uninit(&_rtc_spi_instance);
      nrfx_gpiote_out_clear(RTC_SPI_SCLK);
   }

   // Set the internal RTC time
   if ((timestamp > MINIMUM_VALID_TIMESTAMP) && (timestamp < MAXIMUM_VALID_TIMESTAMP))
   {
      rtc_set_current_time(timestamp);
      ab1815_printTime(timestamp);
   }
   else
      timestamp = 0;
   return timestamp;
}
