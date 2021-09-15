// Datasheet: https://abracon.com/Support/AppsManuals/Precisiontiming/AB18XX-Application-Manual.pdf
#ifndef __RTC_EXTERNAL_HEADER_H
#define __RTC_EXTERNAL_HEADER_H

// Header inclusions ---------------------------------------------------------------------------------------------------

#include "nrf_drv_spi.h"
#include "time.h"


// Time and date registers ---------------------------------------------------------------------------------------------

#define AB1815_HUND               0x00
#define AB1815_SEC                0x01
#define AB1815_MIN                0x02
#define AB1815_HOUR               0x03
#define AB1815_DATE               0x04
#define AB1815_MONTH              0x05
#define AB1815_YEARS              0x06
#define AB1815_WEEKDAY            0x07


// Alarm registers -----------------------------------------------------------------------------------------------------

#define AB1815_ALARM_HUND         0x08
#define AB1815_ALARM_SEC          0x09
#define AB1815_ALARM_MIN          0x0A
#define AB1815_ALARM_HOUR         0x0B
#define AB1815_ALARM_DATE         0x0C
#define AB1815_ALARM_MONTH        0x0D
#define AB1815_ALARM_WEEKDAY      0x0E


// Config registers ----------------------------------------------------------------------------------------------------

#define AB1815_STATUS             0x0F
#define AB1815_CONTROL1           0x10
#define AB1815_CONTROL2           0x11
#define AB1815_INT_MASK           0x12
#define AB1815_SQW                0x13


// Calibration registers -----------------------------------------------------------------------------------------------

#define AB1815_CALIB_XT           0x14
#define AB1815_CALIB_RC_UP        0x15
#define AB1815_CALIB_RC_LO        0x16


// Sleep control register ----------------------------------------------------------------------------------------------

#define AB1815_SLEEP              0x17


// Timer registers -----------------------------------------------------------------------------------------------------

#define AB1815_COUNTDOWN_CTRL     0x18
#define AB1815_COUNTDOWN_TIMER    0x19
#define AB1815_TIMER_INIT_VAL     0x1A
#define AB1815_WATCHDOG_TIMER     0x1B


// Oscillator registers ------------------------------------------------------------------------------------------------

#define AB1815_OSCILLATOR_CTRL    0x1C
#define AB1815_OSCILLATOR_STATUS  0x1D


// Miscellaneous registers ---------------------------------------------------------------------------------------------

#define AB1815_CONFIGURATION_KEY  0x1F
#define AB1815_TRICKLE            0x20
#define AB1815_AFCTRL             0x26
#define AB1815_BATMODE            0x27
#define AB1815_OUTPUT_CTRL        0x30


// Configuration key ---------------------------------------------------------------------------------------------------

#define AB1815_CONF_KEY_OSC       0xA1
#define AB1815_CONF_KEY_SR        0x3C
#define AB1815_CONF_KEY_REG       0x9D


// RTC typedefs and definitions ----------------------------------------------------------------------------------------

typedef void ab1815_alarm_callback(void);

typedef enum { _16HZ = 0, _4HZ, _1HZ, _1_4HZ } ab1815_watch_clock_freq;

typedef enum { HUNDREDTH_MATCH = 0x7, ONCE_PER_MINUTE = 0x6, ONCE_PER_HOUR = 0x5, ONCE_PER_DAY = 0x4, ONCE_PER_WEEK = 0x3, ONCE_PER_MONTH = 0x2, ONCE_PER_YEAR = 0x1, DISABLED = 0x0 } ab1815_alarm_repeat;

/* Control1: 0x10
 * STOP:  1 = stops the clocking system
 * 12/24: 0 = 24h mode, 1 = 12h mode
 * OUTB:  may be driven on the nIRQ2 pin
 * OUT:   may be driven on the nIRQ  pin
 * RSP:   reset polarity; 1 = nRST high, 0 = nRST low
 * ARST:  auto reset enable
 * PWR2:  1 = enable powering other system devices through nIRQ2
 * WRTC:  write RTC (write to counter registers)
 *
 * Control2: 0x11
 * PSW/IRQ2:  control functionality of nIRQ2
 * FOUT/nIRQ: control functionality of nIRQ
 */
typedef struct
{
   bool stop;
   bool hour_12;
   bool OUTB;
   bool OUT;
   bool rst_pol;
   bool auto_rst;
   bool write_rtc;
   uint8_t psw_nirq2_function;
   uint8_t fout_nirq_function;
} ab1815_control_t;

/* Interrupt Mask: 0x12
 * CEB: 0 = CB will not update, 1 = will toggle
 * IM:  Interrupt mode (toggle duration)
 * BLIE: 1 = battery low detection enabled
 * TIE:  1 = timer interrupt enabled
 * AIE:  1 = alarm interrupt enabled
 * EX2E: 1 = XT2 interrupt enabled
 * EX1E: 1 = XT1 interrupt enabled
 */
typedef struct
{
   bool century_en;
   uint8_t int_mode;
   bool bat_low_en;
   bool timer_en;
   bool alarm_en;
   bool xt2_en;
   bool xt1_en;
} ab1815_int_config_t;

typedef struct
{
   uint8_t hundredths;
   uint8_t seconds;
   uint8_t minutes;
   uint8_t hours;
   uint8_t date;
   uint8_t months;
   uint8_t years;
   uint8_t weekday;
} ab1815_time_t;


// Public AB1815 RTC API -----------------------------------------------------------------------------------------------

uint8_t ab1815_init(void);
uint8_t ab1815_init_time(void);
void ab1815_reset(void);
uint8_t ab1815_set_config(ab1815_control_t config);
uint8_t ab1815_get_config(ab1815_control_t *config);
uint8_t ab1815_set_int_config(ab1815_int_config_t config);
uint8_t ab1815_get_int_config(ab1815_int_config_t *config);
uint8_t ab1815_use_xt_oscillator(void);
uint8_t ab1815_get_status(void);
ab1815_time_t unix_to_ab1815(struct timeval tv);
ab1815_time_t tm_to_ab1815(struct tm *t);
struct timeval ab1815_to_unix(ab1815_time_t time);
uint8_t ab1815_set_time(ab1815_time_t time);
uint8_t ab1815_get_time(ab1815_time_t *time);
uint8_t ab1815_set_timestamp(uint32_t unix_timestamp);
struct timeval ab1815_get_time_unix(void);
uint8_t ab1815_enable_trickle_charger(void);
uint8_t ab1815_set_alarm(ab1815_time_t time, ab1815_alarm_repeat repeat, ab1815_alarm_callback *cb);
uint8_t ab1815_set_watchdog(bool reset, uint8_t clock_cycles, uint8_t clock_frequency);
void ab1815_tickle_watchdog(void);
uint8_t ab1815_clear_watchdog(void);
uint8_t ab1815_ready(void);
uint8_t ab1815_wait_for_ready(uint16_t timeout_ms);
void ab1815_printTime(ab1815_time_t time);

// Public initialization function
uint8_t rtc_external_init(const nrf_drv_spi_t* spi_instance);

#endif // #ifndef __RTC_EXTERNAL_HEADER_H
