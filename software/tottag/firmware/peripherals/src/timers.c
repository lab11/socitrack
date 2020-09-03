// Header inclusions ---------------------------------------------------------------------------------------------------

#include "ble_config.h"
#include "nrfx_wdt.h"
#include "timers.h"


// Static timer state variables ----------------------------------------------------------------------------------------

APP_TIMER_DEF(watchdog_timer);
APP_TIMER_DEF(wakeup_timer);

static nrfx_wdt_config_t wdt_config = { NRF_WDT_BEHAVIOUR_RUN_SLEEP_HALT, WATCHDOG_HARD_RESET_TIMEOUT_MS };
static nrfx_atomic_flag_t *_wakeup_triggered = NULL;


// Private timer handler functions -------------------------------------------------------------------------------------

static void wakeup_handler(void *p_context)
{
   // Set the wakeup triggered flag
   nrfx_atomic_flag_set(_wakeup_triggered);
}


// Public timer functionality ------------------------------------------------------------------------------------------

void timers_init(timer_handler_callback callback, nrfx_atomic_flag_t* wakeup_triggered_flag)
{
   // Create watchdog and wakeup timers
   APP_ERROR_CHECK(app_timer_init());
   APP_ERROR_CHECK(app_timer_create(&watchdog_timer, APP_TIMER_MODE_REPEATED, callback));
   APP_ERROR_CHECK(app_timer_create(&wakeup_timer, APP_TIMER_MODE_SINGLE_SHOT, wakeup_handler));
   _wakeup_triggered = wakeup_triggered_flag;
}

nrfx_err_t watchdog_timer_start(uint16_t watchdog_frequency_ms)
{
   nrfx_err_t err_code = app_timer_start(watchdog_timer, APP_TIMER_TICKS(watchdog_frequency_ms), NULL);
   if (err_code == NRFX_SUCCESS)
   {
      nrfx_wdt_channel_id channel_id;
      nrfx_wdt_init(&wdt_config, NULL);
      nrfx_wdt_channel_alloc(&channel_id);
      nrfx_wdt_enable();
      nrfx_wdt_feed();
   }
   return err_code;
}

nrfx_err_t wakeup_timer_start(uint16_t wakeup_delay_ms)
{
   return app_timer_start(wakeup_timer, APP_TIMER_TICKS(wakeup_delay_ms), NULL);
}
