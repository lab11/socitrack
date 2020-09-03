#ifndef __LED_H
#define __LED_H

typedef enum { RED, BLUE, GREEN, ORANGE } led_color_t;

void led_on(led_color_t color);
void led_off(void);

#endif
