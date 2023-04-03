#ifndef __BUTTON_HEADER_H__
#define __BUTTON_HEADER_H__

// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "app_config.h"


// Peripheral Type Definitions -----------------------------------------------------------------------------------------

typedef void (*button_press_callback_t)(void *button_number);


// Public API Functions ------------------------------------------------------------------------------------------------

void buttons_init(void);
void buttons_deinit(void);
void button_press_register_callback(uint32_t button_number, button_press_callback_t callback);
bool button_is_pressed(uint32_t button_number);

#endif  // #ifndef __BUTTON_HEADER_H__
