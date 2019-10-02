#include "nrf_drv_gpiote.h"
#include "nrf_spi_mngr.h"

#include "app_error.h"

#include "rtc_ab1815.h"
#include "boards.h"

// Helpers
static uint8_t ascii_to_i(char c);
static uint8_t month_to_i(const char * c);

static const nrf_drv_spi_t* spi_instance;
static ab1815_control_t ctrl_config;
static ab1815_int_config_t int_config;
static nrf_drv_spi_config_t spi_config = NRF_DRV_SPI_DEFAULT_CONFIG;
static ab1815_alarm_callback* interrupt_callback;

void ab1815_init(const nrf_drv_spi_t* instance) {
  spi_instance = instance;

  spi_config.sck_pin    = SPI_SCLK;
  spi_config.miso_pin   = SPI_MISO;
  spi_config.mosi_pin   = SPI_MOSI;
  spi_config.ss_pin     = RTC_CS;
  spi_config.frequency  = NRF_DRV_SPI_FREQ_2M;
  spi_config.mode       = NRF_DRV_SPI_MODE_0;
  spi_config.bit_order  = NRF_DRV_SPI_BIT_ORDER_MSB_FIRST;

  nrf_gpio_cfg_input(CARRIER_RTC_INT,  NRF_GPIO_PIN_NOPULL);
  nrf_gpio_cfg_output(RTC_WDI);
  nrf_gpio_pin_set(RTC_WDI);
}

void ab1815_init_time(void) {

  // Check whether RTC is already running; if so, DONT overwrite
  struct timeval curr_time = ab1815_get_time_unix();

  //printf("DEBUG: Unix time is %08llx\n", curr_time.tv_sec);

  if (curr_time.tv_sec < TIMESTAMP_UNIX_PAST) {

      char _date[] = __DATE__; // the format is "Jan  1 2000"
      char _time[] = __TIME__; // the format is "00:00:00"

      ab1815_time_t comp_time;
      comp_time.hundredths = 0;
      comp_time.seconds    = ascii_to_i(_time[6]) * 10 + ascii_to_i(_time[7]);
      comp_time.minutes    = ascii_to_i(_time[3]) * 10 + ascii_to_i(_time[4]);
      comp_time.hours      = ascii_to_i(_time[0]) * 10 + ascii_to_i(_time[1]);

      comp_time.date       = ascii_to_i(_date[4]) * 10 + ascii_to_i(_date[5]);
      comp_time.months     = month_to_i(&_date[0]);
      comp_time.years      = ascii_to_i(_date[9]) * 10 + ascii_to_i(_date[10]);

      ab1815_set_time(comp_time);
  }
}

void  ab1815_read_reg(uint8_t reg, uint8_t* read_buf, size_t len){
  if (len > 256) return;
  uint8_t readreg = reg;
  uint8_t buf[257];

  nrf_drv_spi_uninit(spi_instance);
  nrf_drv_spi_init(spi_instance, &spi_config, NULL, NULL);
  nrf_drv_spi_transfer(spi_instance, &readreg, 1, buf, len+1);

  memcpy(read_buf, buf+1, len);
}

void ab1815_write_reg(uint8_t reg, uint8_t* write_buf, size_t len){
  if (len > 256) return;
  uint8_t buf[257];
  buf[0] = 0x80 | reg;
  memcpy(buf+1, write_buf, len);

  nrf_drv_spi_uninit(spi_instance);
  nrf_drv_spi_init(spi_instance, &spi_config, NULL, NULL);
  nrf_drv_spi_transfer(spi_instance, buf, len+1, NULL, 0);
}

static void interrupt_handler(nrf_drv_gpiote_pin_t pin, nrf_gpiote_polarity_t action) {
  // read and clear interrupts
  uint8_t status = 0;
  ab1815_read_reg(AB1815_STATUS, &status, 1);

  if (status & 0x4 && interrupt_callback) {
    // call user callback
    interrupt_callback();
  }
}


void ab1815_set_config(ab1815_control_t config) {
  uint8_t write;

  // Control1
  write =  config.stop << 7 | config.hour_12 << 6 | config.OUTB << 5 |
           config.OUT << 4  | config.rst_pol << 3 | config.auto_rst << 2 |
           0x2 | config.write_rtc;
  ab1815_write_reg(AB1815_CONTROL1, &write, 1);

  // Control2
  write =  config.psw_nirq2_function << 2 | config.fout_nirq_function;
  ab1815_write_reg(AB1815_CONTROL2, &write, 1);

  // Store current configuration
  ctrl_config = config;
}

void ab1815_get_config(ab1815_control_t* config) {
  uint8_t read;

  ab1815_read_reg(AB1815_CONTROL1, &read, 1);

  config->stop      = (read & 0x80) >> 7;
  config->hour_12   = (read & 0x40) >> 6;
  config->OUTB      = (read & 0x20) >> 5;
  config->OUT       = (read & 0x10) >> 4;
  config->rst_pol   = (read & 0x08) >> 3;
  config->auto_rst  = (read & 0x04) >> 2;
  config->write_rtc = read & 0x01;

  ab1815_read_reg(AB1815_CONTROL2, &read, 1);

  config->psw_nirq2_function = (read & 0x4C) >> 2;
  config->fout_nirq_function = (read & 0x03);
}

void ab1815_set_int_config(ab1815_int_config_t config) {
  uint8_t write;
  write =  config.century_en << 7 | config.int_mode << 5 |
           config.bat_low_en << 4 | config.timer_en << 3 | config.alarm_en << 2 |
           config.xt2_en << 1     | config.xt1_en;
  int_config = config;

  ab1815_write_reg(AB1815_INT_MASK, &write, 1);
}

void ab1815_get_int_config(ab1815_int_config_t* config) {
    uint8_t read;

    ab1815_read_reg(AB1815_INT_MASK, &read, 1);

    config->century_en = (read & 0x80) >> 7;
    config->int_mode   = (read & 0x60) >> 5;
    config->bat_low_en = (read & 0x10) >> 4;
    config->timer_en   = (read & 0x08) >> 3;
    config->alarm_en   = (read & 0x04) >> 2;
    config->xt2_en     = (read & 0x02) >> 2;
    config->xt1_en     = (read & 0x01);
}

inline uint8_t get_tens(uint8_t x) {
  return (x / 10) % 10;
}

inline uint8_t get_ones(uint8_t x) {
  return x % 10;
}

void ab1815_form_time_buffer(ab1815_time_t time, uint8_t* buf) {
  APP_ERROR_CHECK_BOOL(time.hundredths < 100 && time.hundredths >= 0);
  APP_ERROR_CHECK_BOOL(time.seconds < 60 && time.seconds>= 0);
  APP_ERROR_CHECK_BOOL(time.minutes < 60 && time.minutes >= 0);
  APP_ERROR_CHECK_BOOL(time.hours < 24 && time.hours >= 0);
  if (time.date == 0) time.date = 1;
  if (time.months == 0) time.months = 1;
  APP_ERROR_CHECK_BOOL(time.date <= 31 && time.date >= 1);
  APP_ERROR_CHECK_BOOL(time.months <= 12 && time.months >= 1);
  APP_ERROR_CHECK_BOOL(time.years < 100 && time.date >= 0);
  APP_ERROR_CHECK_BOOL(time.weekday < 7 && time.weekday >= 0);

  buf[0] = (get_tens(time.hundredths) & 0xF) << 4 | (get_ones(time.hundredths) & 0xF);
  buf[1] = (get_tens(time.seconds)    & 0x7) << 4 | (get_ones(time.seconds)    & 0xF);
  buf[2] = (get_tens(time.minutes)    & 0x7) << 4 | (get_ones(time.minutes)    & 0xF);
  buf[3] = (get_tens(time.hours)      & 0x3) << 4 | (get_ones(time.hours)      & 0xF);
  buf[4] = (get_tens(time.date)       & 0x3) << 4 | (get_ones(time.date)       & 0xF);
  buf[5] = (get_tens(time.months)     & 0x1) << 4 | (get_ones(time.months)     & 0xF);
  buf[6] = (get_tens(time.years)      & 0xF) << 4 | (get_ones(time.years)      & 0xF);
  buf[7] = time.weekday & 0x7;
}

void ab1815_set_time(ab1815_time_t time) {
  uint8_t write[8];

  // Ensure rtc write bit is enabled
  if (ctrl_config.write_rtc != 1) {
    ctrl_config.write_rtc = 1;
    ab1815_set_config(ctrl_config);
  }

  ab1815_form_time_buffer(time, write);

  ab1815_write_reg(AB1815_HUND, write, 8);
}

void ab1815_get_time(ab1815_time_t* time) {
  uint8_t read[10];

  ab1815_read_reg(AB1815_HUND, read, 8);

  time->hundredths = 10 * ((read[0] & 0xF0) >> 4) + (read[0] & 0xF);
  time->seconds    = 10 * ((read[1] & 0x70) >> 4) + (read[1] & 0xF);
  time->minutes    = 10 * ((read[2] & 0x70) >> 4) + (read[2] & 0xF);
  time->hours      = 10 * ((read[3] & 0x30) >> 4) + (read[3] & 0xF);
  time->date       = 10 * ((read[4] & 0x30) >> 4) + (read[4] & 0xF);
  time->months     = 10 * ((read[5] & 0x10) >> 4) + (read[5] & 0xF);
  time->years      = 10 * ((read[6] & 0xF0) >> 4) + (read[6] & 0xF);
  time->weekday    = read[7] & 0x7;
}

struct timeval ab1815_get_time_unix(void) {
  ab1815_time_t time;

  ab1815_get_time(&time);
  return ab1815_to_unix(time);
}

ab1815_time_t unix_to_ab1815(struct timeval tv) {
  ab1815_time_t time;
  struct tm * t;

  t = gmtime((time_t*)&(tv.tv_sec));
  time.hundredths = tv.tv_usec / 10000;
  time.seconds  = t->tm_sec;
  time.minutes  = t->tm_min;
  time.hours    = t->tm_hour;
  time.date     = t->tm_mday;
  time.months   = t->tm_mon + 1;
  time.years    = t->tm_year - 100;
  time.weekday  = t->tm_wday;
  return time;
}

struct timeval ab1815_to_unix(ab1815_time_t time) {
  struct timeval unix_time;
  struct tm t;

  t.tm_sec  = time.seconds;
  t.tm_min  = time.minutes;
  t.tm_hour = time.hours;
  t.tm_mday = time.date;
  t.tm_mon  = time.months - 1;
  t.tm_year = time.years + 100;
  t.tm_wday = time.weekday;
  unix_time.tv_sec  = mktime(&t);
  unix_time.tv_usec = time.hundredths * 10000;

  return unix_time;
}

void ab1815_set_alarm(ab1815_time_t time, ab1815_alarm_repeat repeat, ab1815_alarm_callback* cb) {
  uint8_t buf[8] = {0};
  interrupt_callback = cb;

  // Clear status
  ab1815_read_reg(AB1815_STATUS, buf, 1);

  // GPIOTE listen to IRQ1 pin
  if (!nrf_drv_gpiote_is_init()) {
    nrf_drv_gpiote_init();
  }
  nrf_drv_gpiote_in_config_t gpio_config = GPIOTE_CONFIG_IN_SENSE_HITOLO(0);
  gpio_config.pull = NRF_GPIO_PIN_PULLUP;
  int error = nrf_drv_gpiote_in_init(RTC_IRQ1, &gpio_config, interrupt_handler);
  APP_ERROR_CHECK(error);
  nrf_drv_gpiote_in_event_enable(RTC_IRQ1, 1);

  // Configure alarm time and frequency
  ab1815_form_time_buffer(time, buf);
  ab1815_write_reg(AB1815_ALARM_HUND, buf, 8);
  ab1815_read_reg(AB1815_COUNTDOWN_CTRL, buf, 1);
  buf[0] &= 0xE3;
  buf[0] |= repeat << 2;
  ab1815_write_reg(AB1815_COUNTDOWN_CTRL, buf, 1);

  // Enable alarm interrupt
  ab1815_read_reg(AB1815_INT_MASK, buf, 1);
  buf[0] |= 0x4;
  ab1815_write_reg(AB1815_INT_MASK, buf, 1);
}

void ab1815_set_watchdog(bool reset, uint8_t clock_cycles, uint8_t clock_frequency) {
  uint8_t buf = 0;

  buf |= clock_frequency & 0x3;
  buf |= (clock_cycles << 2) & 0x7C;
  buf |= reset << 7;

  ab1815_write_reg(AB1815_WATCHDOG_TIMER, &buf, 1);
}

void ab1815_tickle_watchdog(void) {
  nrf_gpio_pin_toggle(RTC_WDI);
  nrf_gpio_pin_toggle(RTC_WDI);
}
void ab1815_clear_watchdog(void) {
  uint8_t buf = 0;
  ab1815_write_reg(AB1815_WATCHDOG_TIMER, &buf, 1);
}

void ab1815_printTime(ab1815_time_t time) {
    printf("INFO: Time is %02u:%02u:%02u, 20%02u/%02u/%02u\n", time.hours, time.minutes, time.seconds, time.years, time.months, time.date);
}

// Helpers
static uint8_t ascii_to_i(char c) {

    if (c == ' ')
        return 0;
    else
        return (uint8_t)(c - '0');
}

static uint8_t month_to_i(const char * c) {

    // Read 3 chars and select month
    char month[] = {c[0], c[1], c[2], '\0'};
    if      (strcmp(month,"Jan") == 0)
        return 1;
    else if (strcmp(month,"Feb") == 0)
        return 2;
    else if (strcmp(month,"Mar") == 0)
        return 3;
    else if (strcmp(month,"Apr") == 0)
        return 4;
    else if (strcmp(month,"May") == 0)
        return 5;
    else if (strcmp(month,"Jun") == 0)
        return 6;
    else if (strcmp(month,"Jul") == 0)
        return 7;
    else if (strcmp(month,"Aug") == 0)
        return 8;
    else if (strcmp(month,"Sep") == 0)
        return 9;
    else if (strcmp(month,"Oct") == 0)
        return 10;
    else if (strcmp(month,"Nov") == 0)
        return 11;
    else if (strcmp(month,"Dec") == 0)
        return 12;
    else {
        // Invalid string!
        return 0;
    }
}