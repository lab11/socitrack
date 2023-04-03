// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "button.h"


// Static Global Variables ---------------------------------------------------------------------------------------------

static const am_devices_button_t buttons[] = {
#if REVISION_ID == REVISION_APOLLO4_EVB
   AM_DEVICES_BUTTON(PIN_BUTTON_1, AM_DEVICES_BUTTON_NORMAL_HIGH),
   AM_DEVICES_BUTTON(PIN_BUTTON_2, AM_DEVICES_BUTTON_NORMAL_HIGH)
#endif
};
static const uint32_t num_buttons = sizeof(buttons) / sizeof(buttons[0]);


// Public API Functions ------------------------------------------------------------------------------------------------

void buttons_init(void)
{
   // Initialize all button GPIOs and enable interrupts
   am_hal_gpio_pincfg_t button_config = AM_HAL_GPIO_PINCFG_INPUT;
   button_config.GP.cfg_b.ePullup = AM_HAL_GPIO_PIN_PULLUP_100K;
   for (uint32_t i = 0; i < num_buttons; ++i)
   {
      configASSERT0(am_hal_gpio_pinconfig(buttons[i].ui32GPIONumber, button_config));
      NVIC_SetPriority(GPIO0_001F_IRQn + GPIO_NUM2IDX(buttons[i].ui32GPIONumber), AM_IRQ_PRIORITY_DEFAULT);
      NVIC_EnableIRQ(GPIO0_001F_IRQn + GPIO_NUM2IDX(buttons[i].ui32GPIONumber));
   }
}

void buttons_deinit(void)
{
   // Disable all button-based interrupts
   for (uint32_t i = 0; i < num_buttons; ++i)
   {
      NVIC_DisableIRQ(GPIO0_001F_IRQn + GPIO_NUM2IDX(buttons[i].ui32GPIONumber));
      am_hal_gpio_interrupt_register(AM_HAL_GPIO_INT_CHANNEL_0, buttons[i].ui32GPIONumber, NULL, (void*)buttons[i].ui32GPIONumber);
      am_hal_gpio_interrupt_control(AM_HAL_GPIO_INT_CHANNEL_0, AM_HAL_GPIO_INT_CTRL_INDV_DISABLE, (void*)&buttons[i].ui32GPIONumber);
   }
}

void button_press_register_callback(uint32_t button_number, button_press_callback_t callback)
{
   // Register button press callback and set up the corresponding interrupt
   for (uint32_t i = 0; i < num_buttons; ++i)
      if (buttons[i].ui32GPIONumber == button_number)
      {
         configASSERT0(am_hal_gpio_interrupt_register(AM_HAL_GPIO_INT_CHANNEL_0, buttons[i].ui32GPIONumber, callback, (void*)button_number));
         configASSERT0(am_hal_gpio_interrupt_control(AM_HAL_GPIO_INT_CHANNEL_0, AM_HAL_GPIO_INT_CTRL_INDV_ENABLE, &button_number));
      }
}

bool button_is_pressed(uint32_t button_number)
{
   // Manually check whether a button is currently pressed
   static uint32_t button_state;
   am_hal_gpio_state_read(button_number, AM_HAL_GPIO_INPUT_READ, &button_state);
   for (int i = 0; i < num_buttons; ++i)
      if ((buttons[i].ui32GPIONumber == button_number) && (buttons[i].ui32Polarity != button_state))
         return true;
   return false;
}
