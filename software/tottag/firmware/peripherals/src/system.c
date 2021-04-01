// Header inclusions ---------------------------------------------------------------------------------------------------

#include "ble_config.h"
#include "system.h"

typedef struct gpio_pin_def_t { uint8_t a, b; } gpio_pin_def_t;

void initialize_gpio(void)
{
   // Initialize the GPIO subsystem
   if (!nrfx_gpiote_is_init())
      nrfx_gpiote_init();

   // Set all unused pins to INPUT ANALOG
   gpio_pin_def_t unused_pins[] = UNUSED_GPIO_PINS;
   uint32_t num_unused_pins = sizeof(unused_pins) / sizeof(gpio_pin_def_t);
   for (uint32_t i = 0; i < num_unused_pins; ++i)
      nrf_gpio_cfg_default(NRF_GPIO_PIN_MAP(unused_pins[i].a, unused_pins[i].b));
}
