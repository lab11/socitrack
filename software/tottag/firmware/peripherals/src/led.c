// Header inclusions ---------------------------------------------------------------------------------------------------

#include "ble_config.h"
#include "led.h"
#include "nrfx_gpiote.h"

#ifndef LEDS_ACTIVE_LOW
#define LEDS_ACTIVE_LOW 1
#endif


// Static LED state ----------------------------------------------------------------------------------------------------

static bool _leds_enabled = false;


// LED functionality ---------------------------------------------------------------------------------------------------

void leds_init(void)
{
   // Configure all LED pins
   nrfx_gpiote_out_config_t led_pin_config = NRFX_GPIOTE_CONFIG_OUT_SIMPLE(LEDS_ACTIVE_LOW);
   nrfx_gpiote_out_init(LED_RED, &led_pin_config);
   nrfx_gpiote_out_init(LED_BLUE, &led_pin_config);
   nrfx_gpiote_out_init(LED_GREEN, &led_pin_config);
#ifdef ENABLE_LEDS
   _leds_enabled = true;
#endif
}

void leds_disable(void)
{
   _leds_enabled = false;
}

void leds_enable(void)
{
#ifdef ENABLE_LEDS
   _leds_enabled = true;
#endif
}

void led_on(led_color_t color)
{
#ifdef ENABLE_LEDS
   if (_leds_enabled)
   {
#if LEDS_ACTIVE_LOW
      switch (color)
      {
         case RED:
            nrfx_gpiote_out_clear(LED_RED);
            nrfx_gpiote_out_set(LED_BLUE);
            nrfx_gpiote_out_set(LED_GREEN);
            break;
         case BLUE:
            nrfx_gpiote_out_set(LED_RED);
            nrfx_gpiote_out_clear(LED_BLUE);
            nrfx_gpiote_out_set(LED_GREEN);
            break;
         case GREEN:
            nrfx_gpiote_out_set(LED_RED);
            nrfx_gpiote_out_set(LED_BLUE);
            nrfx_gpiote_out_clear(LED_GREEN);
            break;
         case ORANGE:
            nrfx_gpiote_out_clear(LED_RED);
            nrfx_gpiote_out_set(LED_BLUE);
            nrfx_gpiote_out_clear(LED_GREEN);
            break;
         case PURPLE:
            nrfx_gpiote_out_clear(LED_RED);
            nrfx_gpiote_out_clear(LED_BLUE);
            nrfx_gpiote_out_set(LED_GREEN);
            break;
         default:
            break;
      }
#else
      switch (color)
      {
         case RED:
            nrfx_gpiote_out_set(LED_RED);
            nrfx_gpiote_out_clear(LED_BLUE);
            nrfx_gpiote_out_clear(LED_GREEN);
            break;
         case BLUE:
            nrfx_gpiote_out_clear(LED_RED);
            nrfx_gpiote_out_set(LED_BLUE);
            nrfx_gpiote_out_clear(LED_GREEN);
            break;
         case GREEN:
            nrfx_gpiote_out_clear(LED_RED);
            nrfx_gpiote_out_clear(LED_BLUE);
            nrfx_gpiote_out_set(LED_GREEN);
            break;
         case ORANGE:
            nrfx_gpiote_out_set(LED_RED);
            nrfx_gpiote_out_clear(LED_BLUE);
            nrfx_gpiote_out_set(LED_GREEN);
            break;
         case PURPLE:
            nrfx_gpiote_out_set(LED_RED);
            nrfx_gpiote_out_set(LED_BLUE);
            nrfx_gpiote_out_clear(LED_GREEN);
            break;
         default:
            break;
      }
#endif
   }
#endif
}

void led_off(void)
{
#ifdef ENABLE_LEDS
   if (_leds_enabled)
   {
#if LEDS_ACTIVE_LOW
      nrfx_gpiote_out_set(LED_RED);
      nrfx_gpiote_out_set(LED_BLUE);
      nrfx_gpiote_out_set(LED_GREEN);
#else
      nrfx_gpiote_out_clear(LED_RED);
      nrfx_gpiote_out_clear(LED_BLUE);
      nrfx_gpiote_out_clear(LED_GREEN);
#endif
   }
#endif
}
