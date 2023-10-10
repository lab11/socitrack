#include "logging.h"
#include "rtc.h"
#include "system.h"

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
   while (true)
   {
      uint32_t timestamp = rtc_get_timestamp();
      print("Current Timestamp: %u\n", timestamp);
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
