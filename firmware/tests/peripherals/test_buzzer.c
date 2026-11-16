#include "buzzer.h"
#include "system.h"

int main(void)
{
   // Set up system hardware
   setup_hardware();
   buzzer_init();
   system_enable_interrupts(true);

   // Play a different buzzer indication every 10 seconds
   while (true)
   {
      for (int i = 0; i < 5; ++i)
      {
         switch (i)
         {
            case 0:
               buzzer_indicate_plugged_in();
               break;
            case 1:
               buzzer_indicate_unplugged();
               break;
            case 2:
               buzzer_indicate_invalid_rtc_time();
               break;
            case 3:
               buzzer_indicate_error();
               break;
            case 4:
               buzzer_indicate_location();
               break;
            default:
               break;
         }
         am_hal_delay_us(10000000);
      }
   }

   // Should never reach this point
   return 0;
}
