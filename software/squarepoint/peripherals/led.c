#include "configuration.h"
#include "led.h"

void led_on(led_color_t color)
{
#if STM_ENABLE_LEDS
   switch (color)
   {
      case RED:
         GPIO_WriteBit(STM_LED_RED_PORT, STM_LED_RED_PIN, LED_ON);
         GPIO_WriteBit(STM_LED_BLUE_PORT, STM_LED_BLUE_PIN, LED_OFF);
         GPIO_WriteBit(STM_LED_GREEN_PORT, STM_LED_GREEN_PIN, LED_OFF);
         break;
      case BLUE:
         GPIO_WriteBit(STM_LED_RED_PORT, STM_LED_RED_PIN, LED_OFF);
         GPIO_WriteBit(STM_LED_BLUE_PORT, STM_LED_BLUE_PIN, LED_ON);
         GPIO_WriteBit(STM_LED_GREEN_PORT, STM_LED_GREEN_PIN, LED_OFF);
         break;
      case GREEN:
         GPIO_WriteBit(STM_LED_RED_PORT, STM_LED_RED_PIN, LED_OFF);
         GPIO_WriteBit(STM_LED_BLUE_PORT, STM_LED_BLUE_PIN, LED_OFF);
         GPIO_WriteBit(STM_LED_GREEN_PORT, STM_LED_GREEN_PIN, LED_ON);
         break;
      case ORANGE:
         GPIO_WriteBit(STM_LED_RED_PORT, STM_LED_RED_PIN, LED_ON);
         GPIO_WriteBit(STM_LED_BLUE_PORT, STM_LED_BLUE_PIN, LED_OFF);
         GPIO_WriteBit(STM_LED_GREEN_PORT, STM_LED_GREEN_PIN, LED_ON);
         break;
      default:
         break;
   }
#endif
}

void led_off(void)
{
   GPIO_WriteBit(STM_LED_RED_PORT, STM_LED_RED_PIN, LED_OFF);
   GPIO_WriteBit(STM_LED_BLUE_PORT, STM_LED_BLUE_PIN, LED_OFF);
   GPIO_WriteBit(STM_LED_GREEN_PORT, STM_LED_GREEN_PIN, LED_OFF);
}
