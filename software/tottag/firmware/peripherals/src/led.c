// Header inclusions ---------------------------------------------------------------------------------------------------

#include "ble_config.h"
#include "led.h"
#include "nrfx_gpiote.h"

#ifndef LEDS_ACTIVE_LOW
#define LEDS_ACTIVE_LOW 1
#endif


// LED functionality ---------------------------------------------------------------------------------------------------

void led_init(uint32_t pin_number)
{
   // Configure the LED pin
   nrfx_gpiote_out_config_t led_pin_config = NRFX_GPIOTE_CONFIG_OUT_SIMPLE(LEDS_ACTIVE_LOW);
   nrfx_gpiote_out_init(pin_number, &led_pin_config);
}

void led_on(uint32_t pin_number)
{
#ifdef ENABLE_LEDS
#if LEDS_ACTIVE_LOW
   nrfx_gpiote_out_clear(pin_number);
#else
   nrfx_gpiote_out_set(pin_number);
#endif
#endif
}

void led_off(uint32_t pin_number)
{
#ifdef ENABLE_LEDS
#if LEDS_ACTIVE_LOW
   nrfx_gpiote_out_set(pin_number);
#else
   nrfx_gpiote_out_clear(pin_number);
#endif
#endif
}

void led_toggle(uint32_t pin_number)
{
#ifdef ENABLE_LEDS
   nrfx_gpiote_out_toggle(pin_number);
#endif
}
