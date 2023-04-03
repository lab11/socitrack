#include "led.h"
#include "system.h"

int main(void)
{
   // Set up system hardware
   setup_hardware();
   leds_init();

   // Light up a different LED every 2 seconds
   led_color_t last_color = LED_ALL;
   while (true)
   {
      led_off(last_color);
      last_color = (last_color == LED_ALL) ? 0 : (last_color + 1);
      led_on(last_color);
      am_hal_delay_us(2000000);
   }

   // Should never reach this point
   return 0;
}
