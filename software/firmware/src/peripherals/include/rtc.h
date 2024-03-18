#ifndef __RTC_HEADER_H__
#define __RTC_HEADER_H__

// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "app_config.h"


// Public API Functions ------------------------------------------------------------------------------------------------

void rtc_init(void);
void rtc_set_time_to_compile_time(void);
bool rtc_set_time_from_timestamp(uint32_t timestamp);
void rtc_set_wakeup_timestamp(uint32_t timestamp);
uint32_t rtc_get_timestamp(void);
uint32_t rtc_get_timestamp_diff_ms(uint32_t starting_timestamp);
uint32_t rtc_get_time_of_day(void);
bool rtc_is_valid(void);

#endif  // #ifndef __RTC_HEADER_H__
