// Header inclusions ---------------------------------------------------------------------------------------------------

#include "nrf_delay.h"
#include "nrfx_spi.h"
#include "rtc.h"
#include "rtc_external.h"
#include "sd_card.h"


// Real-time clock state variables -------------------------------------------------------------------------------------

static nrfx_spi_t _rtc_spi_instance = NRFX_SPI_INSTANCE(RTC_SPI_BUS_IDX);
static nrfx_spi_config_t _rtc_spi_config = NRFX_SPI_DEFAULT_CONFIG;
static ab1815_control_t _ctrl_config = { 0 };
static ab1815_int_config_t _int_config = { 0 };
static uint8_t _ab1815_read_write_buf[257] = { 0 };


// Helper functions ----------------------------------------------------------------------------------------------------

#ifdef FORCE_RTC_RESET
static uint8_t ascii_to_i(char c) { return (c == ' ') ? 0 : (uint8_t)(c - '0'); }
static uint8_t month_to_i(const char *c)
{
   // Read 3 chars and select month
   char month[] = { c[0], c[1], c[2], '\0' };
   if (strcmp(month, "Jan") == 0)
      return 1;
   else if (strcmp(month, "Feb") == 0)
      return 2;
   else if (strcmp(month, "Mar") == 0)
      return 3;
   else if (strcmp(month, "Apr") == 0)
      return 4;
   else if (strcmp(month, "May") == 0)
      return 5;
   else if (strcmp(month, "Jun") == 0)
      return 6;
   else if (strcmp(month, "Jul") == 0)
      return 7;
   else if (strcmp(month, "Aug") == 0)
      return 8;
   else if (strcmp(month, "Sep") == 0)
      return 9;
   else if (strcmp(month, "Oct") == 0)
      return 10;
   else if (strcmp(month, "Nov") == 0)
      return 11;
   else if (strcmp(month, "Dec") == 0)
      return 12;
   else
      return 0;
}
#endif // #ifdef FORCE_RTC_RESET

inline uint8_t get_tens(uint8_t x) { return (x / 10) % 10; }
inline uint8_t get_ones(uint8_t x) { return x % 10; }

static void ab1815_form_time_buffer(ab1815_time_t time, uint8_t *buf)
{
   buf[0] = (get_tens(time.hundredths) & 0xF) << 4 | (get_ones(time.hundredths) & 0xF);
   buf[1] = (get_tens(time.seconds) & 0x7) << 4 | (get_ones(time.seconds) & 0xF);
   buf[2] = (get_tens(time.minutes) & 0x7) << 4 | (get_ones(time.minutes) & 0xF);
   buf[3] = (get_tens(time.hours) & 0x3) << 4 | (get_ones(time.hours) & 0xF);
   buf[4] = (get_tens(time.date) & 0x3) << 4 | (get_ones(time.date) & 0xF);
   buf[5] = (get_tens(time.months) & 0x1) << 4 | (get_ones(time.months) & 0xF);
   buf[6] = (get_tens(time.years) & 0xF) << 4 | (get_ones(time.years) & 0xF);
   buf[7] = time.weekday & 0x7;
}

static ab1815_time_t tm_to_ab1815(struct tm *t)
{
   ab1815_time_t time;
   time.hundredths = 0;
   time.seconds = t->tm_sec;
   time.minutes = t->tm_min;
   time.hours = t->tm_hour;
   time.date = t->tm_mday;
   time.months = t->tm_mon + 1;
   time.years = t->tm_year - 100;
   time.weekday = t->tm_wday;
   return time;
}

static ab1815_time_t unix_to_ab1815(struct timeval tv)
{
   ab1815_time_t time = tm_to_ab1815(gmtime((time_t*)&(tv.tv_sec)));
   time.hundredths = tv.tv_usec / 10000;
   return time;
}

static struct timeval ab1815_to_unix(ab1815_time_t time)
{
   struct tm t;
   struct timeval unix_time;

   t.tm_sec = time.seconds;
   t.tm_min = time.minutes;
   t.tm_hour = time.hours;
   t.tm_mday = time.date;
   t.tm_mon = time.months - 1;
   t.tm_year = time.years + 100;
   t.tm_wday = time.weekday;
   unix_time.tv_sec = mktime(&t);
   unix_time.tv_usec = time.hundredths * 10000;

   return unix_time;
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

static bool ab1815_init(void)
{
   // Setup interrupt pins and wait until the chip becomes available
   nrf_gpio_cfg_input(RTC_INT, NRF_GPIO_PIN_PULLUP);
   nrfx_gpiote_out_config_t rtc_wdi_pin_config = NRFX_GPIOTE_CONFIG_OUT_SIMPLE(0);
   nrfx_gpiote_out_init(RTC_WDI, &rtc_wdi_pin_config);
   return ab1815_wait_for_ready(1000);
}

static bool ab1815_read_reg(uint8_t reg, uint8_t *read_buf, size_t len)
{
   // Read from the requested register
   uint8_t read_register = reg & 0x7F;
   nrfx_spi_xfer_desc_t read_transfer = NRFX_SPI_XFER_TRX(&read_register, 1, _ab1815_read_write_buf, 1 + len);
   nrfx_err_t err_code = nrfx_spi_xfer(&_rtc_spi_instance, &read_transfer, 0);
   memcpy(read_buf, &_ab1815_read_write_buf[1], len);
   return (err_code == NRFX_SUCCESS);
}

static bool ab1815_write_reg(uint8_t reg, uint8_t *write_buf, size_t len)
{
   // Write to the requested register
   _ab1815_read_write_buf[0] = reg | 0x80;
   memcpy(&_ab1815_read_write_buf[1], write_buf, len);
   nrfx_spi_xfer_desc_t write_transfer = NRFX_SPI_XFER_TX(_ab1815_read_write_buf, 1 + len);
   return (nrfx_spi_xfer(&_rtc_spi_instance, &write_transfer, 0) == NRFX_SUCCESS);
}

static bool ab1815_get_time(ab1815_time_t *time)
{
   uint8_t read[10] = { 0 };
   if (!ab1815_read_reg(AB1815_HUND, read, 8))
      return false;

   time->hundredths = 10 * ((read[0] & 0xF0) >> 4) + (read[0] & 0xF);
   time->seconds = 10 * ((read[1] & 0x70) >> 4) + (read[1] & 0xF);
   time->minutes = 10 * ((read[2] & 0x70) >> 4) + (read[2] & 0xF);
   time->hours = 10 * ((read[3] & 0x30) >> 4) + (read[3] & 0xF);
   time->date = 10 * ((read[4] & 0x30) >> 4) + (read[4] & 0xF);
   time->months = 10 * ((read[5] & 0x10) >> 4) + (read[5] & 0xF);
   time->years = 10 * ((read[6] & 0xF0) >> 4) + (read[6] & 0xF);
   time->weekday = read[7] & 0x7;
   return true;
}

static bool ab1815_set_config(void)
{
   // Control1
   uint8_t write = _ctrl_config.stop << 7 | _ctrl_config.hour_12 << 6 |     _ctrl_config.OUTB << 5 |
                   _ctrl_config.OUT << 4  | _ctrl_config.rst_pol << 3 | _ctrl_config.auto_rst << 2 | 0x2 | _ctrl_config.write_rtc;
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
   return ab1815_write_reg(AB1815_BATMODE, &write, 1);
}

#ifdef FORCE_RTC_RESET
static struct timeval ab1815_get_time_unix(void)
{
   struct timeval tv = { 0 };
   ab1815_time_t time = { 0 };
   if (ab1815_get_time(&time))
      tv = ab1815_to_unix(time);
   return tv;
}
#endif

static bool ab1815_set_time(ab1815_time_t time)
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
   ab1815_form_time_buffer(time, write);
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

   ab1815_time_t comp_time;
   comp_time.hundredths = 0;
   comp_time.seconds = ascii_to_i(_datetime[17]) * 10 + ascii_to_i(_datetime[18]);
   comp_time.minutes = ascii_to_i(_datetime[14]) * 10 + ascii_to_i(_datetime[15]);
   comp_time.hours = ascii_to_i(_datetime[11]) * 10 + ascii_to_i(_datetime[12]);

   comp_time.date = ascii_to_i(_datetime[8]) * 10 + ascii_to_i(_datetime[9]);
   comp_time.months = month_to_i(&_datetime[4]);
   comp_time.years = ascii_to_i(_datetime[26]) * 10 + ascii_to_i(_datetime[27]);
   comp_time.weekday = 0;  // default

   uint8_t time_properly_set = 0, num_retries = 10;
   struct timeval set_time = ab1815_to_unix(comp_time);
   while (!time_properly_set && --num_retries)
   {
      ab1815_set_time(comp_time);
      struct timeval retrieved_time = ab1815_get_time_unix();
      time_properly_set = (retrieved_time.tv_sec <= (set_time.tv_sec + 120)) &&
                          (retrieved_time.tv_sec >= (set_time.tv_sec - 60));
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
                   _int_config.timer_en << 3   | _int_config.alarm_en << 2 |     _int_config.xt2_en << 1 | _int_config.xt1_en;
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

static void ab1815_printTime(ab1815_time_t time)
{
   printf("INFO: RTC time is %02u:%02u:%02u, 20%02u/%02u/%02u\n", time.hours, time.minutes, time.seconds, time.years, time.months, time.date);
}

bool rtc_external_init(void)
{
   // Initialize the chip
   bool success = ab1815_init();

   // Define the RTC configuration and settings
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
   success = (success && (nrfx_spi_init(&_rtc_spi_instance, &_rtc_spi_config, NULL, NULL) == NRFX_SUCCESS));

   // Set configurations
   success = (success && ab1815_clear_watchdog());
   success = (success && ab1815_set_config());
   success = (success && ab1815_set_int_config());
   success = (success && ab1815_enable_trickle_charger());
   success = (success && ab1815_use_xt_oscillator());

   // Initialize time
   success = (success && ab1815_init_time());

   // Set nRF time from the RTC
   ab1815_time_t time = { 0 };
   success = (success && ab1815_get_time(&time));
   if (success)
   {
      rtc_set_current_time(ab1815_to_unix(time).tv_sec);
      ab1815_printTime(time);
   }

   // De-initialize SPI communications
   nrfx_spi_uninit(&_rtc_spi_instance);
   nrfx_gpiote_out_clear(RTC_SPI_SCLK);
   return success;
}

bool rtc_external_set_timestamp(uint32_t unix_timestamp)
{
   // Set the internal RTC time and temporarily block the SD card from using the SPI bus
   bool success = false;
   rtc_set_current_time(unix_timestamp);
   struct timeval tv = { .tv_sec = unix_timestamp, .tv_usec = 0 };
   ab1815_time_t new_time = unix_to_ab1815(tv);
   if (!sd_card_revoke_spi_access(true))
      return success;

   // Initialize RTC chip communications and store the timestamp
   if (nrfx_spi_init(&_rtc_spi_instance, &_rtc_spi_config, NULL, NULL) == NRFX_SUCCESS)
   {
      success = ab1815_set_time(new_time);
      nrfx_spi_uninit(&_rtc_spi_instance);
      nrfx_gpiote_out_clear(RTC_SPI_SCLK);
   }

   // Restore SD card access to the SPI bus
   sd_card_revoke_spi_access(false);
   return success;
}

uint32_t rtc_external_sync_to_internal(void)
{
   // Temporarily block the SD card from using the SPI bus
   uint32_t timestamp = 0;
   ab1815_time_t time = { 0 };
   if (!sd_card_revoke_spi_access(true))
      return timestamp;

   // Initialize RTC chip communications and retrieve the current timestamp
   if (nrfx_spi_init(&_rtc_spi_instance, &_rtc_spi_config, NULL, NULL) == NRFX_SUCCESS)
   {
      if (ab1815_get_time(&time))
         timestamp = ab1815_to_unix(time).tv_sec;
      nrfx_spi_uninit(&_rtc_spi_instance);
      nrfx_gpiote_out_clear(RTC_SPI_SCLK);
   }

   // Restore SD card access to the SPI bus and sync the internal timestamp
   sd_card_revoke_spi_access(false);
   if ((timestamp > MINIMUM_VALID_TIMESTAMP) && (timestamp < MAXIMUM_VALID_TIMESTAMP))
   {
      rtc_set_current_time(timestamp);
      ab1815_printTime(time);
   }
   else
      timestamp = 0;
   return timestamp;
}
