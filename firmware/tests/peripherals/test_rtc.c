#include "logging.h"
#include "rtc.h"
#include "system.h"

int device_start_time;
uint32_t get_experiment_time(int32_t offset) { return (uint32_t)(rtc_get_timestamp_diff_ms(device_start_time) + offset); }

int main(void)
{
   // Set up system hardware
   setup_hardware();
   rtc_init();
#ifdef SET_RTC
   rtc_set_time_to_compile_time();
   int i = 0;
#endif

   // Output timestamp every second
   device_start_time = rtc_get_timestamp();
   while (true)
   {
      uint32_t timestamp = rtc_get_timestamp();
	  uint32_t diff = get_experiment_time(0);
      print("Current Timestamp: %u, Time Diff in milliseconds: %u\n", timestamp, diff);
#ifdef SET_RTC
      if (i++ == 15)
      {
         i = 0;
         print("Setting timestamp 15 seconds earlier...%s\n",
         rtc_set_time_from_timestamp(timestamp - 15) ? "SUCCESS" : "FAILURE");
      }
#endif
      am_hal_delay_us(1000000);
   }

   // Should never reach this point
   return 0;
}
