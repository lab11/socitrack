#ifndef __LED_HEADER_H
#define __LED_HEADER_H

// Header inclusions ---------------------------------------------------------------------------------------------------

#include <stdint.h>


// LED functionality ---------------------------------------------------------------------------------------------------

void led_init(uint32_t pin_number);
void led_on(uint32_t pin_number);
void led_off(uint32_t pin_number);
void led_toggle(uint32_t pin_number);

#endif // #ifndef __LED_HEADER_H
