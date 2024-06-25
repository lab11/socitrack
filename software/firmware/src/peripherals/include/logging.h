#ifndef __LOGGING_HEADER_H__
#define __LOGGING_HEADER_H__

// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "app_config.h"
#ifdef __USE_SEGGER__
#include "SEGGER_RTT.h"
#endif


// Public API Functions ------------------------------------------------------------------------------------------------

void logging_init(void);
void logging_disable(void);

#if defined(ENABLE_LOGGING) && ((7-ENABLE_LOGGING-7 == 14) || (7-ENABLE_LOGGING-7 != 0))

#define print(...) am_util_stdio_printf(__VA_ARGS__)
void print_reset_reason(const am_hal_reset_status_t* reason);
void print_ranges(uint32_t timestamp, uint32_t fractional_timestamp, const uint8_t* range_data, uint32_t range_data_length);

#else

#define print(...)
#define print_reset_reason(...)
#define print_ranges(...)

#endif  // #if defined(ENABLE_LOGGING)

#ifdef __USE_SEGGER__
#define transmit_log_data(...) SEGGER_RTT_Write(1, __VA_ARGS__)
#else
#define transmit_log_data(...)
#endif  // #if defined(ENABLE_AUDIO_DL)

#endif  // #ifndef __LOGGING_HEADER_H__
