// Header Inclusions ---------------------------------------------------------------------------------------------------

#include <time.h>
#include "rtc.h"

#ifndef _BUILD_TIMESTAMP
#error "_BUILD_TIMESTAMP must be defined as the build's Unix time (the Makefile passes -D_BUILD_TIMESTAMP=$(date -u +%s)u)"
#endif


// Static Global Variables ---------------------------------------------------------------------------------------------

static volatile uint32_t rtc_stat;


// Private Helper Functions --------------------------------------------------------------------------------------------

// Reentrant calendar conversion rather than through mktime()/gmtime()
static int32_t days_from_civil(uint32_t year, uint32_t month, uint32_t day)
{
   // Shift the year so that a leap day lands at the END of the shifted year, which removes every special case
   int32_t y = (int32_t)year - (month <= 2);
   const int32_t era = (y >= 0 ? y : y - 399) / 400;
   const uint32_t yoe = (uint32_t)(y - era * 400);                                    // [0, 399]
   const uint32_t doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;      // [0, 365]
   const uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;                         // [0, 146096]
   return (era * 146097) + (int32_t)doe - 719468;
}

static void civil_from_days(int32_t days, uint32_t *year, uint32_t *month, uint32_t *day)
{
   days += 719468;
   const int32_t era = (days >= 0 ? days : days - 146096) / 146097;
   const uint32_t doe = (uint32_t)(days - era * 146097);                               // [0, 146096]
   const uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;         // [0, 399]
   const uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);                       // [0, 365]
   const uint32_t mp = (5 * doy + 2) / 153;                                            // [0, 11]
   *day = doy - (153 * mp + 2) / 5 + 1;                                                // [1, 31]
   *month = mp + (mp < 10 ? 3 : -9);                                                   // [1, 12]
   *year = (uint32_t)((int32_t)yoe + (era * 400) + (*month <= 2));
}

static uint32_t to_unix_timestamp(const am_hal_rtc_time_t *time)
{
   // The RTC only ever holds a two-digit year with the century bit pinned to 2000
   if ((time->ui32Month < 1) || (time->ui32Month > 12) || (time->ui32DayOfMonth < 1) || (time->ui32DayOfMonth > 31))
      return 0;
   const int32_t days = days_from_civil(2000 + time->ui32Year, time->ui32Month, time->ui32DayOfMonth);
   if (days < 0)
      return 0;
   return ((uint32_t)days * 86400u) + (3600u * time->ui32Hour) + (60u * time->ui32Minute) + time->ui32Second;
}

static am_hal_rtc_time_t to_rtc_time(uint32_t unix_timestamp)
{
   const int32_t days = (int32_t)(unix_timestamp / 86400u);
   const uint32_t seconds_of_day = unix_timestamp % 86400u;
   uint32_t year = 1970, month = 1, day = 1;
   civil_from_days(days, &year, &month, &day);
   am_hal_rtc_time_t new_rtc_time = {
      .ui32ReadError = 0,
      .ui32CenturyBit = RTC_CTRUP_CB_2000,
      .ui32Hour = seconds_of_day / 3600u,
      .ui32Minute = (seconds_of_day % 3600u) / 60u,
      .ui32Second = seconds_of_day % 60u,
      .ui32Hundredths = 0,
      .ui32Weekday = (uint32_t)(((days % 7) + 7 + 4) % 7),
      .ui32DayOfMonth = day,
      .ui32Month = month,
      .ui32Year = (year >= 2000) ? (year - 2000) : 0
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
   // The same instant rtc_is_valid() measures against, so a clock set here is valid
   am_hal_rtc_time_t new_rtc_time = to_rtc_time(_BUILD_TIMESTAMP);
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
   am_hal_rtc_alarm_set(&wakeup_time, AM_HAL_RTC_ALM_RPT_YR);
   am_hal_rtc_interrupt_clear(AM_HAL_RTC_INT_ALM);
   am_hal_rtc_interrupt_enable(AM_HAL_RTC_INT_ALM);
   NVIC_SetPriority(RTC_IRQn, NVIC_configKERNEL_INTERRUPT_PRIORITY);
   NVIC_EnableIRQ(RTC_IRQn);
}

uint32_t rtc_get_timestamp(void)
{
   rtc_stat = RTC->RTCSTAT;  // Read RTCSTAT to mitigate RTC hanging as per errata
   am_hal_rtc_time_t rtc_time;
   return (am_hal_rtc_time_get(&rtc_time) == AM_HAL_STATUS_SUCCESS) ? to_unix_timestamp(&rtc_time) : 0;
}

uint32_t rtc_get_timestamp_diff_ms(uint32_t starting_timestamp)
{
   rtc_stat = RTC->RTCSTAT;  // Read RTCSTAT to mitigate RTC hanging as per errata
   am_hal_rtc_time_t rtc_time;
   if (am_hal_rtc_time_get(&rtc_time) != AM_HAL_STATUS_SUCCESS)
      return 0;

   // Clamp rather than underflow
   const uint32_t now = to_unix_timestamp(&rtc_time);
   return (now > starting_timestamp) ? ((1000 * (now - starting_timestamp)) + (10 * rtc_time.ui32Hundredths)) : 0;
}

uint32_t rtc_get_time_of_day(void)
{
   rtc_stat = RTC->RTCSTAT;  // Read RTCSTAT to mitigate RTC hanging as per errata
   am_hal_rtc_time_t rtc_time;
   return (am_hal_rtc_time_get(&rtc_time) == AM_HAL_STATUS_SUCCESS) ? ((3600 * rtc_time.ui32Hour) + (60 * rtc_time.ui32Minute) + rtc_time.ui32Second) : 0;
}

bool rtc_is_valid(void)
{
   // Valid once set for as long as it keeps power
   rtc_stat = RTC->RTCSTAT;  // Read RTCSTAT to mitigate RTC hanging as per errata
   am_hal_rtc_time_t rtc_time;
   return (am_hal_rtc_time_get(&rtc_time) == AM_HAL_STATUS_SUCCESS) && (to_unix_timestamp(&rtc_time) >= (uint32_t)_BUILD_TIMESTAMP);
}
