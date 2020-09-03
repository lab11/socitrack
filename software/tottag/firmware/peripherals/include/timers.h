#ifndef __TIMERS_HEADER_H
#define __TIMERS_HEADER_H

// Header inclusions ---------------------------------------------------------------------------------------------------

#include "nrfx_errors.h"


// Timer typedefs ------------------------------------------------------------------------------------------------------

typedef void (*timer_handler_callback)(void*);


// Public timer functionality ------------------------------------------------------------------------------------------

void timers_init(timer_handler_callback callback, nrfx_atomic_flag_t* wakeup_triggered_flag);
nrfx_err_t watchdog_timer_start(uint16_t watchdog_frequency_ms);
nrfx_err_t wakeup_timer_start(uint16_t wakeup_delay_ms);

#endif // #ifndef __TIMERS_HEADER_H
