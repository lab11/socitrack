// Header inclusions ---------------------------------------------------------------------------------------------------

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wredundant-decls"

#include "ble_config.h"
#include "nrfx_atomic.h"
#include "nrfx_rtc.h"
#include "rtc.h"
#include "sd_card.h"

#pragma GCC diagnostic pop


// Static RTC state variables ------------------------------------------------------------------------------------------

static const nrfx_rtc_t _rtc_instance = NRFX_RTC_INSTANCE(2);
static nrfx_atomic_u32_t _rtc_sync_time, _rtc_sync_rtc_counter;


// Private helper functions --------------------------------------------------------------------------------------------

static void rtc_handler(nrfx_rtc_int_type_t int_type) {}
static uint32_t rtc_to_s(uint32_t ticks) { return ticks / 8; }


// Public RTC functionality --------------------------------------------------------------------------------------------

void rtc_init(void)
{
   // Initialize the RTC instance
   uint32_t dummy_val;
   nrfx_rtc_config_t rtc_config = NRFX_RTC_DEFAULT_CONFIG;
   rtc_config.prescaler = 4095;  // Ticks at 8 Hz; PRESCALER is 12bit register (2^12 - 1 = 4095)
   _rtc_sync_time = _rtc_sync_rtc_counter = 0;
   nrfx_rtc_counter_clear(&_rtc_instance);
   APP_ERROR_CHECK(nrfx_rtc_init(&_rtc_instance, &rtc_config, rtc_handler));
   nrfx_rtc_int_disable(&_rtc_instance, &dummy_val);
   nrfx_rtc_overflow_disable(&_rtc_instance);
   nrfx_rtc_tick_disable(&_rtc_instance);
   nrfx_rtc_enable(&_rtc_instance);
}

uint32_t rtc_get_current_time(void)
{
   // Check for an RTC overflow
   uint32_t current_rtc_counter = nrfx_rtc_counter_get(&_rtc_instance);
   if (current_rtc_counter < _rtc_sync_rtc_counter)
   {
      _rtc_sync_time += rtc_to_s(current_rtc_counter + 0x00FFFFFF - _rtc_sync_rtc_counter);
      _rtc_sync_rtc_counter = current_rtc_counter;
   }

   // Return the current application time
   return _rtc_sync_time + rtc_to_s(current_rtc_counter - _rtc_sync_rtc_counter);
}

void rtc_set_current_time(uint32_t epoch)
{
   // Set the current application time
   _rtc_sync_time = epoch;
   _rtc_sync_rtc_counter = nrfx_rtc_counter_get(&_rtc_instance);
}
