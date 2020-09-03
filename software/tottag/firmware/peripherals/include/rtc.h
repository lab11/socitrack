#ifndef __RTC_HEADER_H
#define __RTC_HEADER_H

// Header inclusions ---------------------------------------------------------------------------------------------------

#include <stdint.h>


// Public RTC functionality --------------------------------------------------------------------------------------------

void rtc_init(void);
uint32_t rtc_get_current_time(void);
void rtc_set_current_time(uint32_t epoch);

#endif // #ifndef __RTC_HEADER_H
