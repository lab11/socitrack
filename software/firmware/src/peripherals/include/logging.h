#ifndef __LOGGING_HEADER_H__
#define __LOGGING_HEADER_H__

// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "app_config.h"

#if (REVISION_ID == REVISION_I) || (REVISION_ID == REVISION_APOLLO4_EVB) || (REVISION_ID == REVISION_IP)
#include "SEGGER_RTT.h"
#endif


// Public API Functions ------------------------------------------------------------------------------------------------

void logging_init(void);
void logging_disable(void);

#if defined(ENABLE_LOGGING) && ((7-ENABLE_LOGGING-7 == 14) || (7-ENABLE_LOGGING-7 != 0))

#if (REVISION_ID == REVISION_I) || (REVISION_ID == REVISION_APOLLO4_EVB) || (REVISION_ID == REVISION_IP)
#define print(...) SEGGER_RTT_printf(0, __VA_ARGS__)
#else
#define print(...) am_util_stdio_printf(__VA_ARGS__)
#endif

void print_reset_reason(const am_hal_reset_status_t* reason);
void print_ranges(uint32_t timestamp, const uint8_t* range_data, uint32_t range_data_length);

#else

#define print(...)
#define print_reset_reason(...)
#define print_ranges(...)

#endif

#endif  // #ifndef __LOGGING_HEADER_H__
