#ifndef __LED_HEADER_H__
#define __LED_HEADER_H__

// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "app_config.h"


// Peripheral Type Definitions -----------------------------------------------------------------------------------------

typedef enum { LED_RED, LED_GREEN, LED_ALL } led_color_t;


// Public API Functions ------------------------------------------------------------------------------------------------

void leds_init(void);
void leds_deinit(void);
void led_on(led_color_t color);
void led_off(led_color_t color);

#endif  // #ifndef __LED_HEADER_H__
