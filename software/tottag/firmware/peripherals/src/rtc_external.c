// Header inclusions ---------------------------------------------------------------------------------------------------

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wredundant-decls"

#include <string.h>
#include "ble_config.h"
#include "nrf_delay.h"
#include "nrfx_gpiote.h"
#include "rtc.h"
#include "rtc_external.h"

#pragma GCC diagnostic pop


#if (BOARD_V >= 0x0F)

// Real-time clock state variables -------------------------------------------------------------------------------------

static const nrf_drv_spi_t *_spi_instance = NULL;
static nrf_drv_spi_config_t _spi_config = NRF_DRV_SPI_DEFAULT_CONFIG;
static ab1815_control_t _ctrl_config = { 0 };
static ab1815_int_config_t _int_config = { 0 };
static ab1815_alarm_callback *_interrupt_callback = NULL;
static uint8_t _ab1815_read_write_buf[257] = { 0 };


// Helper functions ----------------------------------------------------------------------------------------------------

#ifdef FORCE_RTC_RESET
static int ab1815_num_init_retries = 0;
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

uint8_t ab1815_init(void)
{
   // Setup interrupt pins
   nrf_gpio_cfg_input(CARRIER_RTC_INT, NRF_GPIO_PIN_PULLUP);
   nrfx_gpiote_out_config_t rtc_wdi_pin_config = NRFX_GPIOTE_CONFIG_OUT_SIMPLE(0);
   nrfx_gpiote_out_init(CARRIER_RTC_WDI, &rtc_wdi_pin_config);

   // Wait until the chip becomes available
   bool success = ab1815_wait_for_ready(1000);
#ifdef FORCE_RTC_RESET
   if (!success && (++ab1815_num_init_retries == 3))
   {
      // Attempt to perform a software reset on the chip
      ab1815_reset();
      ab1815_num_init_retries = 0;
      ab1815_set_watchdog(1, 2, 0);
      success = ab1815_wait_for_ready(1000);
   }
#endif

   // Disable the RTC chip watchdog
   ab1815_clear_watchdog();
   return success;
}

uint8_t ab1815_init_time(void)
{
#ifdef FORCE_RTC_RESET
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdate-time"
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

   return ab1815_set_time(comp_time);
#pragma GCC diagnostic pop
#else
   return 1;
#endif
}

void ab1815_reset(void)
{
   // Write a software reset request to the configuration register
   ab1815_wait_for_ready(1000);
   uint8_t buf[2] = { 0x80 | AB1815_CONFIGURATION_KEY, AB1815_CONF_KEY_SR };
   APP_ERROR_CHECK(nrf_drv_spi_transfer(_spi_instance, buf, 2, NULL, 0));
}

static uint8_t ab1815_read_reg(uint8_t reg, uint8_t *read_buf, size_t len)
{
   // Wait until the RTC chip becomes available
   if (!ab1815_wait_for_ready(1000))
      return 0;

   // Read from the requested register
   APP_ERROR_CHECK(nrf_drv_spi_transfer(_spi_instance, &reg, 1, _ab1815_read_write_buf, len + 1));
   memcpy(read_buf, _ab1815_read_write_buf + 1, len);
   return 1;
}

static uint8_t ab1815_write_reg(uint8_t reg, uint8_t *write_buf, size_t len)
{
   // Wait until the RTC chip becomes available
   if (!ab1815_wait_for_ready(1000))
      return 0;

   // Write to the requested register
   _ab1815_read_write_buf[0] = 0x80 | reg;
   memcpy(_ab1815_read_write_buf + 1, write_buf, len);
   APP_ERROR_CHECK(nrf_drv_spi_transfer(_spi_instance, _ab1815_read_write_buf, len + 1, NULL, 0));
   return 1;
}

static void interrupt_handler(nrfx_gpiote_pin_t pin, nrf_gpiote_polarity_t action)
{
   // Read and clear interrupts
   if (pin == CARRIER_RTC_INT)
   {
      uint8_t status = 0;
      ab1815_read_reg(AB1815_STATUS, &status, 1);
      if ((status & 0x4) && _interrupt_callback)
         _interrupt_callback();
   }
}

uint8_t ab1815_set_config(ab1815_control_t config)
{
   // Store current configuration
   _ctrl_config = config;

   // Control1
   uint8_t write = config.stop << 7 | config.hour_12 << 6 | config.OUTB << 5 | config.OUT << 4 | config.rst_pol << 3 | config.auto_rst << 2 | 0x2 | config.write_rtc;
   if (!ab1815_write_reg(AB1815_CONTROL1, &write, 1))
      return 0;

   // Control2
   write = config.psw_nirq2_function << 2 | config.fout_nirq_function;
   if (!ab1815_write_reg(AB1815_CONTROL2, &write, 1))
      return 0;

   // Output Control
   write = AB1815_CONF_KEY_REG;
   if (!ab1815_write_reg(AB1815_CONFIGURATION_KEY, &write, 1))
      return 0;
   write = (0x1 << 5) | (0x1 << 4);
   if (!ab1815_write_reg(AB1815_OUTPUT_CTRL, &write, 1))
      return 0;

   // Batmode - disable IO if main battery is removed
   write = AB1815_CONF_KEY_REG;
   if (!ab1815_write_reg(AB1815_CONFIGURATION_KEY, &write, 1))
      return 0;
   write = 0;
   return ab1815_write_reg(AB1815_BATMODE, &write, 1);
}

uint8_t ab1815_get_config(ab1815_control_t *config)
{
   uint8_t read = 0;
   if (!ab1815_read_reg(AB1815_CONTROL1, &read, 1))
      return 0;

   config->stop = (read & 0x80) >> 7;
   config->hour_12 = (read & 0x40) >> 6;
   config->OUTB = (read & 0x20) >> 5;
   config->OUT = (read & 0x10) >> 4;
   config->rst_pol = (read & 0x08) >> 3;
   config->auto_rst = (read & 0x04) >> 2;
   config->write_rtc = read & 0x01;

   if (!ab1815_read_reg(AB1815_CONTROL2, &read, 1))
      return 0;

   config->psw_nirq2_function = (read & 0x4C) >> 2;
   config->fout_nirq_function = (read & 0x03);
   return 1;
}

uint8_t ab1815_set_int_config(ab1815_int_config_t config)
{
   _int_config = config;
   uint8_t write = config.century_en << 7 | config.int_mode << 5 | config.bat_low_en << 4 | config.timer_en << 3 | config.alarm_en << 2 | config.xt2_en << 1 | config.xt1_en;
   return ab1815_write_reg(AB1815_INT_MASK, &write, 1);
}

uint8_t ab1815_get_int_config(ab1815_int_config_t *config)
{
   uint8_t read = 0;
   if (!ab1815_read_reg(AB1815_INT_MASK, &read, 1))
      return 0;

   config->century_en = (read & 0x80) >> 7;
   config->int_mode = (read & 0x60) >> 5;
   config->bat_low_en = (read & 0x10) >> 4;
   config->timer_en = (read & 0x08) >> 3;
   config->alarm_en = (read & 0x04) >> 2;
   config->xt2_en = (read & 0x02) >> 2;
   config->xt1_en = (read & 0x01);
   return 1;
}

uint8_t ab1815_use_xt_oscillator(void)
{
   // Clear all current status bits
   uint8_t write = 0;
   if (!ab1815_write_reg(AB1815_OSCILLATOR_STATUS, &write, 1))
      return 0;
   if (!ab1815_write_reg(AB1815_CALIB_XT, &write, 1))
      return 0;

   // Enable writing to the Autocalibration Filter Register and enable the AF filter
   write = AB1815_CONF_KEY_REG;
   if (!ab1815_write_reg(AB1815_CONFIGURATION_KEY, &write, 1))
      return 0;
   write = 0xA0;
   if (!ab1815_write_reg(AB1815_AFCTRL, &write, 1))
      return 0;

   // Enable writing to the Oscillator Control Register, enable XT oscillator, and enable failover to RC oscillator
   write = AB1815_CONF_KEY_OSC;
   if (!ab1815_write_reg(AB1815_CONFIGURATION_KEY, &write, 1))
      return 0;
   write = 0x48;
   return ab1815_write_reg(AB1815_OSCILLATOR_CTRL, &write, 1);
}

uint8_t ab1815_get_status(void)
{
   uint8_t read = 0;
   return ab1815_read_reg(AB1815_STATUS, &read, 1);
}

inline uint8_t get_tens(uint8_t x)
{
   return (x / 10) % 10;
}

inline uint8_t get_ones(uint8_t x)
{
   return x % 10;
}

static void ab1815_form_time_buffer(ab1815_time_t time, uint8_t *buf)
{
   APP_ERROR_CHECK_BOOL(time.hundredths < 100 && time.hundredths >= 0);
   APP_ERROR_CHECK_BOOL(time.seconds < 60 && time.seconds >= 0);
   APP_ERROR_CHECK_BOOL(time.minutes < 60 && time.minutes >= 0);
   APP_ERROR_CHECK_BOOL(time.hours < 24 && time.hours >= 0);
   if (time.date == 0)
      time.date = 1;
   if (time.months == 0)
      time.months = 1;
   APP_ERROR_CHECK_BOOL(time.date <= 31 && time.date >= 1);
   APP_ERROR_CHECK_BOOL(time.months <= 12 && time.months >= 1);
   APP_ERROR_CHECK_BOOL(time.years < 100 && time.date >= 0);
   APP_ERROR_CHECK_BOOL(time.weekday < 7 && time.weekday >= 0);

   buf[0] = (get_tens(time.hundredths) & 0xF) << 4 | (get_ones(time.hundredths) & 0xF);
   buf[1] = (get_tens(time.seconds) & 0x7) << 4 | (get_ones(time.seconds) & 0xF);
   buf[2] = (get_tens(time.minutes) & 0x7) << 4 | (get_ones(time.minutes) & 0xF);
   buf[3] = (get_tens(time.hours) & 0x3) << 4 | (get_ones(time.hours) & 0xF);
   buf[4] = (get_tens(time.date) & 0x3) << 4 | (get_ones(time.date) & 0xF);
   buf[5] = (get_tens(time.months) & 0x1) << 4 | (get_ones(time.months) & 0xF);
   buf[6] = (get_tens(time.years) & 0xF) << 4 | (get_ones(time.years) & 0xF);
   buf[7] = time.weekday & 0x7;
}

uint8_t ab1815_set_time(ab1815_time_t time)
{
   // Ensure RTC write bit is enabled
   uint8_t write[8] = { 0 };
   if (_ctrl_config.write_rtc != 1)
   {
      _ctrl_config.write_rtc = 1;
      if (!ab1815_set_config(_ctrl_config))
         return 0;
   }

   ab1815_form_time_buffer(time, write);

   //printf("DEBUG: Packet Tx - %02x %02x %02x %02x %02x %02x %02x %02x\n", write[0], write[1], write[2], write[3], write[4], write[5], write[6], write[7]);

   if (!ab1815_write_reg(AB1815_HUND, write, 8))
      return 0;

   // Ensure RTC write bit is disabled to prevent unintended access
   if (_ctrl_config.write_rtc != 0)
   {
      _ctrl_config.write_rtc = 0;
      if (!ab1815_set_config(_ctrl_config))
         return 0;
   }
   return 1;
}

uint8_t ab1815_get_time(ab1815_time_t *time)
{
   uint8_t read[10] = { 0 };
   if (!ab1815_read_reg(AB1815_HUND, read, 8))
      return 0;

   //printf("DEBUG: Packet Rx - %02x %02x %02x %02x %02x %02x %02x %02x\n", read[0], read[1], read[2], read[3], read[4], read[5], read[6], read[7]);

   time->hundredths = 10 * ((read[0] & 0xF0) >> 4) + (read[0] & 0xF);
   time->seconds = 10 * ((read[1] & 0x70) >> 4) + (read[1] & 0xF);
   time->minutes = 10 * ((read[2] & 0x70) >> 4) + (read[2] & 0xF);
   time->hours = 10 * ((read[3] & 0x30) >> 4) + (read[3] & 0xF);
   time->date = 10 * ((read[4] & 0x30) >> 4) + (read[4] & 0xF);
   time->months = 10 * ((read[5] & 0x10) >> 4) + (read[5] & 0xF);
   time->years = 10 * ((read[6] & 0xF0) >> 4) + (read[6] & 0xF);
   time->weekday = read[7] & 0x7;
   return 1;
}

#endif  // #if (BOARD_V >= 0x0F)

uint8_t ab1815_set_timestamp(uint32_t unix_timestamp)
{
#if (BOARD_V >= 0x0F)

   nrf_drv_spi_uninit(_spi_instance);
   nrf_drv_spi_init(_spi_instance, &_spi_config, NULL, NULL);
   rtc_set_current_time(unix_timestamp);
   struct timeval tv = { .tv_sec = unix_timestamp, .tv_usec = 0 };
   ab1815_time_t new_time = unix_to_ab1815(tv);
   return ab1815_set_time(new_time);
   nrf_drv_spi_uninit(_spi_instance);
   nrfx_gpiote_out_clear(RTC_SD_SPI_SCLK);

#else

   return 0;

#endif  // #if (BOARD_V >= 0x0F)
}

#if (BOARD_V >= 0x0F)

struct timeval ab1815_get_time_unix(void)
{
   struct timeval tv = { 0 };
   ab1815_time_t time = { 0 };
   if (ab1815_get_time(&time))
      tv = ab1815_to_unix(time);
   return tv;
}

ab1815_time_t unix_to_ab1815(struct timeval tv)
{
   ab1815_time_t time;
   struct tm *t = gmtime((time_t*)&(tv.tv_sec));
   time.hundredths = tv.tv_usec / 10000;
   time.seconds = t->tm_sec;
   time.minutes = t->tm_min;
   time.hours = t->tm_hour;
   time.date = t->tm_mday;
   time.months = t->tm_mon + 1;
   time.years = t->tm_year - 100;
   time.weekday = t->tm_wday;
   return time;
}

ab1815_time_t tm_to_ab1815(struct tm *t)
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

struct timeval ab1815_to_unix(ab1815_time_t time)
{
   struct timeval unix_time;
   struct tm t;

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

uint8_t ab1815_enable_trickle_charger(void)
{
   // Trickle - needs to be enabled first
   uint8_t write = AB1815_CONF_KEY_REG;
   if (!ab1815_write_reg(AB1815_CONFIGURATION_KEY, &write, 1))
      return 0;
   write = (0b1010 << 4) | (0b01 << 2) | (0b01);
   return ab1815_write_reg(AB1815_TRICKLE, &write, 1);
}

uint8_t ab1815_set_alarm(ab1815_time_t time, ab1815_alarm_repeat repeat, ab1815_alarm_callback *cb)
{
   uint8_t buf[8] = { 0 };
   _interrupt_callback = cb;

   // Clear status
   if (!ab1815_read_reg(AB1815_STATUS, buf, 1))
      return 0;

   // GPIOTE listen to IRQ1 pin
   nrfx_gpiote_in_config_t gpio_config = NRFX_GPIOTE_RAW_CONFIG_IN_SENSE_HITOLO(0);
   gpio_config.pull = NRF_GPIO_PIN_PULLUP;
   nrfx_err_t error = nrfx_gpiote_in_init(CARRIER_RTC_INT, &gpio_config, interrupt_handler);
   APP_ERROR_CHECK(error);
   nrfx_gpiote_in_event_enable(CARRIER_RTC_INT, 1);

   // Configure alarm time and frequency
   ab1815_form_time_buffer(time, buf);
   if (!ab1815_write_reg(AB1815_ALARM_HUND, buf, 8))
      return 0;
   if (!ab1815_read_reg(AB1815_COUNTDOWN_CTRL, buf, 1))
      return 0;
   buf[0] &= 0xE3;
   buf[0] |= repeat << 2;
   if (!ab1815_write_reg(AB1815_COUNTDOWN_CTRL, buf, 1))
      return 0;

   // Enable alarm interrupt
   if (!ab1815_read_reg(AB1815_INT_MASK, buf, 1))
      return 0;
   buf[0] |= 0x4;
   return ab1815_write_reg(AB1815_INT_MASK, buf, 1);
}

uint8_t ab1815_set_watchdog(bool reset, uint8_t clock_cycles, uint8_t clock_frequency)
{
   uint8_t buf = 0;

   buf |= clock_frequency & 0x3;
   buf |= (clock_cycles << 2) & 0x7C;
   buf |= reset << 7;

   return ab1815_write_reg(AB1815_WATCHDOG_TIMER, &buf, 1);
}

void ab1815_tickle_watchdog(void)
{
   nrfx_gpiote_out_toggle(CARRIER_RTC_WDI);
}

uint8_t ab1815_clear_watchdog(void)
{
   uint8_t buf = 0;
   return ab1815_write_reg(AB1815_WATCHDOG_TIMER, &buf, 1);
}

uint8_t ab1815_ready(void)
{
   return nrfx_gpiote_in_is_set(CARRIER_RTC_INT);
}

uint8_t ab1815_wait_for_ready(uint16_t timeout_ms)
{
   uint16_t counter = 0;
   uint16_t step_size_ms = 10;

   while (!ab1815_ready() && (counter < timeout_ms))
   {
      nrf_delay_ms(step_size_ms);
      counter += step_size_ms;
   }

   if (counter < timeout_ms)
      return 1;
   else
   {
      printf("WARNING: RTC not ready yet, but timeout of %u ms expired!\n", timeout_ms);
      return 0;
   }
}

void ab1815_printTime(ab1815_time_t time)
{
   printf("INFO: Time is %02u:%02u:%02u, 20%02u/%02u/%02u\n", time.hours, time.minutes, time.seconds, time.years, time.months, time.date);
}
#endif  // #if (BOARD_V >= 0x0F)

uint8_t rtc_external_init(const nrf_drv_spi_t* spi_instance)
{
   bool success = true;
#if (BOARD_V >= 0x0F)

   // Setup SPI communications
   _spi_instance = spi_instance;
   _spi_config.sck_pin = RTC_SD_SPI_SCLK;
   _spi_config.miso_pin = RTC_SD_SPI_MISO;
   _spi_config.mosi_pin = RTC_SD_SPI_MOSI;
   _spi_config.ss_pin = RTC_SD_SPI_CS;
   _spi_config.frequency = NRF_DRV_SPI_FREQ_250K;
   _spi_config.mode = NRF_DRV_SPI_MODE_3;
   nrf_drv_spi_init(_spi_instance, &_spi_config, NULL, NULL);

   // Initialize the chip
   success = ab1815_init();

   // Startup RTC
   static ab1815_control_t ctrl_config = {
         .stop      = 0,
         .hour_12   = 0,
         .OUTB      = 0,
         .OUT       = 1,
         .rst_pol   = 0,
         .auto_rst  = 1,
         .write_rtc = 1,
         .psw_nirq2_function = 0,
         .fout_nirq_function = 0
   };
   static ab1815_int_config_t int_config = {
         .century_en = 1,
         .int_mode   = 0x3,
         .bat_low_en = 0,
         .timer_en   = 0,
         .alarm_en   = 0,
         .xt2_en     = 0,
         .xt1_en     = 0
   };

   // Set configurations
   success = (success && ab1815_set_config(ctrl_config));
   success = (success && ab1815_set_int_config(int_config));
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
   nrf_drv_spi_uninit(_spi_instance);
   nrfx_gpiote_out_clear(RTC_SD_SPI_SCLK);

#else
   printf("INFO: Skipping RTC as compiling for older board (Version < revF)\n");
#endif
   return success;
}
