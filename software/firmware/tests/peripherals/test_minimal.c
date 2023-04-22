#include "logging.h"
#include "system.h"

int main(void)
{
   // Set up system hardware
   setup_hardware();
   
   // Set up the output pin
   configASSERT0(am_hal_gpio_pinconfig(25, am_hal_gpio_pincfg_output));
   am_hal_gpio_output_clear(25);
   
   while (true)
   {
	  am_hal_gpio_output_set(25);
      am_hal_delay_us(500000);
	  am_hal_gpio_output_clear(25);
      am_hal_delay_us(500000);  
   }
   // Should never reach this point
   return 0;
}
