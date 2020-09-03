#include "board.h"
#include "led.h"

void led_on(led_color_t color)
{
#if (BOARD_V == SQUAREPOINT)
#ifndef STM_DISABLE_LEDS
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
      case YELLOW:
      case ORANGE:
         GPIO_WriteBit(STM_LED_RED_PORT, STM_LED_RED_PIN, LED_ON);
         GPIO_WriteBit(STM_LED_BLUE_PORT, STM_LED_BLUE_PIN, LED_OFF);
         GPIO_WriteBit(STM_LED_GREEN_PORT, STM_LED_GREEN_PIN, LED_ON);
         break;
      default:
         break;
   }
#endif
#endif
}

void led_off(void)
{
#if (BOARD_V == SQUAREPOINT)
#ifndef STM_DISABLE_LEDS
   GPIO_WriteBit(STM_LED_RED_PORT, STM_LED_RED_PIN, LED_OFF);
   GPIO_WriteBit(STM_LED_BLUE_PORT, STM_LED_BLUE_PIN, LED_OFF);
   GPIO_WriteBit(STM_LED_GREEN_PORT, STM_LED_GREEN_PIN, LED_OFF);
#endif
#endif
}
