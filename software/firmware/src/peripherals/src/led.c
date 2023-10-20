// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "led.h"


// Static Global Variables ---------------------------------------------------------------------------------------------

static const am_devices_led_t leds[] = {
   {PIN_LED1_RED,   AM_DEVICES_LED_ON_LOW | AM_DEVICES_LED_POL_OPEN_DRAIN},
   {PIN_LED1_GREEN, AM_DEVICES_LED_ON_LOW | AM_DEVICES_LED_POL_OPEN_DRAIN}
};


// Public API Functions ------------------------------------------------------------------------------------------------

void leds_init(void)
{
   // Initialize all LEDs and turn them off
   am_devices_led_array_init((am_devices_led_t*)leds, sizeof(leds) / sizeof(leds[0]));
   led_off(LED_ALL);
}

void leds_deinit(void)
{
   // Turn off all LEDs and disable them
   led_off(LED_ALL);
   am_devices_led_array_disable((am_devices_led_t*)leds, sizeof(leds) / sizeof(leds[0]));
}

void led_on(led_color_t color)
{
   // Only allow turning on an LED on if LEDs are enabled
#if defined(ENABLE_LEDS) && ((7-ENABLE_LEDS-7 == 14) || (7-ENABLE_LEDS-7 != 0))

   // Determine which LEDs to turn on to create the requested color
   switch (color)
   {
      case LED_RED:
         am_devices_led_on((am_devices_led_t*)leds, 0);
         break;
      case LED_GREEN:
         am_devices_led_on((am_devices_led_t*)leds, 1);
         break;
      case LED_ALL:
         am_devices_led_on((am_devices_led_t*)leds, 0);
         am_devices_led_on((am_devices_led_t*)leds, 1);
         break;
      default:
         break;
   }

#endif  // #if defined(ENABLE_LEDS) && ((7-ENABLE_LEDS-7 == 14) || (7-ENABLE_LEDS-7 != 0))
}

void led_off(led_color_t color)
{
   // Determine which LEDs to turn off to extinguish the requested color
   switch (color)
   {
      case LED_RED:
         am_devices_led_off((am_devices_led_t*)leds, 0);
         break;
      case LED_GREEN:
         am_devices_led_off((am_devices_led_t*)leds, 1);
         break;
      case LED_ALL:
         am_devices_led_off((am_devices_led_t*)leds, 0);
         am_devices_led_off((am_devices_led_t*)leds, 1);
         break;
      default:
         break;
   }
}
