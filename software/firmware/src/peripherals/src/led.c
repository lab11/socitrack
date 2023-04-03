// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "led.h"


// Static Global Variables ---------------------------------------------------------------------------------------------

static am_devices_led_t leds[] = {
   {PIN_LED1_RED,   AM_DEVICES_LED_ON_LOW | AM_DEVICES_LED_POL_OPEN_DRAIN},
   {PIN_LED1_GREEN, AM_DEVICES_LED_ON_LOW | AM_DEVICES_LED_POL_OPEN_DRAIN},
   {PIN_LED2_RED,   AM_DEVICES_LED_ON_LOW | AM_DEVICES_LED_POL_OPEN_DRAIN},
   {PIN_LED2_YELLOW, AM_DEVICES_LED_ON_LOW | AM_DEVICES_LED_POL_OPEN_DRAIN}
};


// Public API Functions ------------------------------------------------------------------------------------------------

void leds_init(void)
{
   // Initialize all LEDs and turn them off
   am_devices_led_array_init(leds, sizeof(leds) / sizeof(leds[0]));
   led_off(LED_ALL);
}

void leds_deinit(void)
{
   // Turn off all LEDs and disable them
   led_off(LED_ALL);
   am_devices_led_array_disable(leds, sizeof(leds) / sizeof(leds[0]));
}

void led_on(led_color_t color)
{
   // Only allow turning on an LED on if LEDs are enabled
#if defined(ENABLE_LEDS) && ((7-ENABLE_LEDS-7 == 14) || (7-ENABLE_LEDS-7 != 0))

   // Determine which LEDs to turn on to create the requested color
   switch (color)
   {
      case LED_RED:
         am_devices_led_on(leds, 0);
         am_devices_led_off(leds, 1);
         break;
      case LED_GREEN:
         am_devices_led_on(leds, 1);
         am_devices_led_off(leds, 0);
         break;
      case LED_YELLOW:
         am_devices_led_on(leds, 3);
         am_devices_led_off(leds, 2);
         break;
      case LED_BROWN:
         am_devices_led_on(leds, 0);
         am_devices_led_on(leds, 1);
         break;
      case LED_ORANGE:
         am_devices_led_on(leds, 2);
         am_devices_led_on(leds, 3);
         break;
      case LED_ALL:
         am_devices_led_on(leds, 0);
         am_devices_led_on(leds, 2);
         am_devices_led_off(leds, 1);
         am_devices_led_off(leds, 3);
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
         am_devices_led_off(leds, 0);
         am_devices_led_off(leds, 1);
         break;
      case LED_GREEN:
         am_devices_led_off(leds, 0);
         am_devices_led_off(leds, 1);
         break;
      case LED_YELLOW:
         am_devices_led_off(leds, 2);
         am_devices_led_off(leds, 3);
         break;
      case LED_BROWN:
         am_devices_led_off(leds, 0);
         am_devices_led_off(leds, 1);
         break;
      case LED_ORANGE:
         am_devices_led_off(leds, 2);
         am_devices_led_off(leds, 3);
         break;
      case LED_ALL:
         am_devices_led_off(leds, 0);
         am_devices_led_off(leds, 1);
         am_devices_led_off(leds, 2);
         am_devices_led_off(leds, 3);
         break;
      default:
         break;
   }
}
