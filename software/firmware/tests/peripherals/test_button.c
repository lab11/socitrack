#include "button.h"
#include "logging.h"
#include "system.h"

static void button_pressed(void *button_number)
{
   print("Button #%d pressed!\n", ((uint32_t)button_number == PIN_BUTTON_1) ? 1 : 2);
}

int main(void)
{
   // Set up system hardware
   setup_hardware();
   buttons_init();
   system_enable_interrupts(true);

   // Allow for manual detection of button presses for 5 seconds
   print("Manual button press detection started...\n");
   for (int i = 0; i < 1000000; ++i)
   {
      if (button_is_pressed(PIN_BUTTON_1))
         print("Button #1 pressed!\n");
      if (button_is_pressed(PIN_BUTTON_2))
         print("Button #2 pressed!\n");
      am_hal_delay_us(1);
   }

   // Initiate interrupt-based button press detection
   button_press_register_callback(PIN_BUTTON_1, button_pressed);
   button_press_register_callback(PIN_BUTTON_2, button_pressed);
   print("Interrupt-based button press detection started...\n");
   while (true)
      am_hal_sysctrl_sleep(AM_HAL_SYSCTRL_SLEEP_DEEP);

   // Should never reach this point
   return 0;
}
