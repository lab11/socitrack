#include "system.h"
#include "logging.h"

int main(void)
{
   // Set up system hardware
   setup_hardware();

   // Output a different line every second
   print("Starting logging test!\n");
   while (true)
      for (int i = 0; i < 1000; ++i)
      {
         print("Seconds elapsed: %d\n", i);
         am_hal_delay_us(1000000);
      }

   // Should never reach this point
   return 0;
}
