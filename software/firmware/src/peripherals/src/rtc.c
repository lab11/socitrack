// Header Inclusions ---------------------------------------------------------------------------------------------------

#include <time.h>
#include "rtc.h"


// Static Global Variables ---------------------------------------------------------------------------------------------

static const char *month_strings[] = { "Invalid Entry", "January", "February", "March", "April", "May", "June", "July",
                                       "August", "September", "October", "November", "December", "Invalid Month" };
static const char *day_strings[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };


// Private Helper Functions --------------------------------------------------------------------------------------------

static int to_val(const char *ascii_string)
{
   int val = ascii_string[1] - '0';
   val += (ascii_string[0] == ' ') ? 0 : ((ascii_string[0] - '0') * 10);
   return val;
}

static int month_to_index(const char *month_string)
{
   for (int i = 1; i < 12; ++i)
      if (am_util_string_strnicmp(month_strings[i], month_string, 3) == 0)
         return i;
   return 12;
}

static int day_to_index(const char *day_string)
{
   for (int i = 0; i < 7; ++i)
      if (am_util_string_strnicmp(day_strings[i], day_string, 3) == 0)
         return i;
   return 7;
}

static uint32_t to_unix_timestamp(const am_hal_rtc_time_t *time)
{
   struct tm time_struct = {
      .tm_sec = time->ui32Second,
      .tm_min = time->ui32Minute,
      .tm_hour = time->ui32Hour,
      .tm_mday = time->ui32DayOfMonth,
      .tm_mon = time->ui32Month - 1,
      .tm_year = time->ui32Year + 100,
      .tm_wday = time->ui32Weekday,
      .tm_yday = -1,
      .tm_isdst = -1
   };
   time_t timestamp = mktime(&time_struct);
   return (timestamp == (time_t)(-1)) ? 0 : (uint32_t)timestamp;
}

static am_hal_rtc_time_t to_rtc_time(uint32_t unix_timestamp)
{
   time_t timestamp = (time_t)unix_timestamp;
   struct tm *unix_time = gmtime(&timestamp);
   am_hal_rtc_time_t new_rtc_time = {
      .ui32ReadError = 0,
      .ui32CenturyEnable = 0,
      .ui32Century = 0,
      .ui32Hour = unix_time->tm_hour,
      .ui32Minute = unix_time->tm_min,
      .ui32Second = unix_time->tm_sec,
      .ui32Hundredths = 0,
      .ui32Weekday = unix_time->tm_wday,
      .ui32DayOfMonth = unix_time->tm_mday,
      .ui32Month = unix_time->tm_mon + 1,
      .ui32Year = unix_time->tm_year - 100
   };
   return new_rtc_time;
}


// Public API Functions ------------------------------------------------------------------------------------------------

void rtc_init(void)
{
   // Enable the XT clock for the RTC
   configASSERT0(am_hal_clkgen_control(AM_HAL_CLKGEN_CONTROL_RTC_SEL_XTAL, NULL));
   am_hal_rtc_osc_select(AM_HAL_RTC_OSC_XT);

   // Enable the RTC
   configASSERT0(am_hal_rtc_osc_enable());
}

void rtc_set_time_to_compile_time(void)
{
   const char _datetime[] = _DATETIME;  // Format: "Tue Jan  1 00:00:00 UTC 2000"
   am_hal_rtc_time_t new_rtc_time = {
      .ui32ReadError = 0,
      .ui32CenturyEnable = 0,
      .ui32Century = 0,
      .ui32Hour = to_val(&_datetime[11]),
      .ui32Minute = to_val(&_datetime[14]),
      .ui32Second = to_val(&_datetime[17]),
      .ui32Hundredths = 0,
      .ui32Weekday = day_to_index(&_datetime[0]),
      .ui32DayOfMonth = to_val(&_datetime[8]),
      .ui32Month = month_to_index(&_datetime[4]),
      .ui32Year = to_val(&_datetime[26])
   };
   configASSERT0(am_hal_rtc_time_set(&new_rtc_time));
}

bool rtc_set_time_from_timestamp(uint32_t timestamp)
{
   am_hal_rtc_time_t new_rtc_time = to_rtc_time(timestamp);
   return (am_hal_rtc_time_set(&new_rtc_time) == AM_HAL_STATUS_SUCCESS);
}

void rtc_set_wakeup_timestamp(uint32_t timestamp)
{
   am_hal_rtc_time_t wakeup_time = to_rtc_time(timestamp);
   am_hal_rtc_alarm_set(&wakeup_time, RTC_RTCCTL_RPT_YEAR);
   am_hal_rtc_interrupt_clear(AM_HAL_RTC_INT_ALM);
   am_hal_rtc_interrupt_enable(AM_HAL_RTC_INT_ALM);
   NVIC_SetPriority(RTC_IRQn, AM_IRQ_PRIORITY_DEFAULT);
   NVIC_EnableIRQ(RTC_IRQn);
}

uint32_t rtc_get_timestamp(void)
{
   static am_hal_rtc_time_t rtc_time;
   return (am_hal_rtc_time_get(&rtc_time) == AM_HAL_STATUS_SUCCESS) ? to_unix_timestamp(&rtc_time) : 0;
}

bool rtc_is_valid(void)
{
   static am_hal_rtc_time_t rtc_time;
   return (am_hal_rtc_time_get(&rtc_time) == AM_HAL_STATUS_SUCCESS) && (rtc_time.ui32Year > 22) && (rtc_time.ui32Year < 40);
}
