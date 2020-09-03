#ifndef __LED_HEADER_H
#define __LED_HEADER_H

// LED Color typedefs --------------------------------------------------------------------------------------------------

typedef enum { RED, BLUE, GREEN, ORANGE, PURPLE } led_color_t;


// LED functionality ---------------------------------------------------------------------------------------------------

void leds_init(void);
void leds_disable(void);
void leds_enable(void);
void led_on(led_color_t color);
void led_off(void);

#endif // #ifndef __LED_HEADER_H
