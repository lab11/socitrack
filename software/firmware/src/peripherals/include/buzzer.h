#ifndef __BUZZER_HEADER_H__
#define __BUZZER_HEADER_H__

// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "app_config.h"


// Public API Functions ------------------------------------------------------------------------------------------------

void buzzer_init(void);
void buzzer_deinit(void);
void buzzer_indicate_plugged_in(void);
void buzzer_indicate_unplugged(void);
void buzzer_indicate_invalid_rtc_time(void);
void buzzer_indicate_error(void);
void buzzer_indicate_location(void);

#endif  // ifndef __BUZZER_HEADER_H__
