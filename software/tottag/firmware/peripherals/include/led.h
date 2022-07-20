#ifndef __LED_HEADER_H
#define __LED_HEADER_H

// Header inclusions ---------------------------------------------------------------------------------------------------

#include "ble_config.h"


// LED Color typedefs and definitions ----------------------------------------------------------------------------------

typedef enum { RED, BLUE, GREEN, ORANGE, PURPLE } led_color_t;

#ifndef LEDS_ACTIVE_LOW
#define LEDS_ACTIVE_LOW 1
#endif


// LED functionality ---------------------------------------------------------------------------------------------------

void leds_init(void);
void leds_disable(void);
void leds_enable(void);
void led_on(led_color_t color);
void led_off(void);

#endif // #ifndef __LED_HEADER_H
