#include "app_tasks.h"
#include "battery.h"
#include "bluetooth.h"
#include "button.h"
#include "buzzer.h"
#include "imu.h"
#include "led.h"
#include "logging.h"
#include "ranging.h"
#include "rtc.h"
#include "storage.h"
#include "system.h"

typedef enum { WAKE_WITH_RTC = 1, WAKE_WITH_BATTERY_STATUS } wake_criteria_t;
static const uint32_t wakeup_pin = PIN_BATTERY_INPUT_POWER_GOOD;
static const wake_criteria_t wake_criteria = WAKE_WITH_RTC;

int main(void)
{
   // Setup all hardware and fetch the device UID
   setup_hardware();
   static uint8_t uid[EUI_LEN];
   system_read_UID(uid, sizeof(uid));

   // Initialize all required peripherals
   battery_monitor_init();
   bluetooth_init(uid);
   buzzer_init();
   imu_init();
   leds_init();
   rtc_init();
   storage_init();
   rtc_set_time_to_compile_time();

   // Initialize the ranging radio and put it into deep sleep
   ranging_radio_init(uid);
   ranging_radio_sleep(true);

   // Flash the red LED for 2 seconds to show that we are awake
   led_on(LED_RED);
   am_hal_delay_us(2000000);
   led_off(LED_RED);

   // Enter power-down mode until awoken 5 seconds in the future (or by a change in charging status)
   system_enter_power_off_mode(wakeup_pin, (wake_criteria == WAKE_WITH_RTC) ? rtc_get_timestamp() + 5 : 0);
   system_reset();

   // Should never reach this point
   return 0;
}
