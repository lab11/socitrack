// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "logging.h"
#include "system.h"
#include "usb.h"


#if REVISION_ID > REVISION_N

// Static Global Variables ---------------------------------------------------------------------------------------------

static uint32_t cable_connected;


// Private Helper Functions --------------------------------------------------------------------------------------------

static void usb_cable_callback(void *pin_number)
{
   // Only care about a connection change when a USB cable is plugged in
   if (!cable_connected)
   {
      am_hal_gpio_state_read(PIN_USB_DETECT, AM_HAL_GPIO_INPUT_READ, &cable_connected);
      if (cable_connected)
         system_reset(true);
   }
}


// Public API Functions ------------------------------------------------------------------------------------------------

void usb_init(void)
{
   // Initialize all USB GPIOs
   am_hal_gpio_pincfg_t usb_power_config = AM_HAL_GPIO_PINCFG_OUTPUT;
   am_hal_gpio_pincfg_t cable_detect_config = AM_HAL_GPIO_PINCFG_INPUT;
   configASSERT0(am_hal_gpio_pinconfig(PIN_USB_DETECT, cable_detect_config));
   configASSERT0(am_hal_gpio_pinconfig(PIN_USB_ENABLE1, usb_power_config));
   configASSERT0(am_hal_gpio_pinconfig(PIN_USB_ENABLE2, usb_power_config));
   am_hal_gpio_state_write(PIN_USB_ENABLE1, AM_HAL_GPIO_OUTPUT_CLEAR);
   am_hal_gpio_state_write(PIN_USB_ENABLE2, AM_HAL_GPIO_OUTPUT_CLEAR);

   // Set initial cable connection status and enable cable detection interrupts
   uint32_t pin_number = PIN_USB_DETECT;
   am_hal_gpio_state_read(PIN_USB_DETECT, AM_HAL_GPIO_INPUT_READ, &cable_connected);
   cable_detect_config.GP.cfg_b.eIntDir = cable_connected ? AM_HAL_GPIO_PIN_INTDIR_HI2LO : AM_HAL_GPIO_PIN_INTDIR_LO2HI;
   configASSERT0(am_hal_gpio_pinconfig(PIN_USB_DETECT, cable_detect_config));
   configASSERT0(am_hal_gpio_interrupt_register(AM_HAL_GPIO_INT_CHANNEL_0, PIN_USB_DETECT, usb_cable_callback, (void*)pin_number));
   configASSERT0(am_hal_gpio_interrupt_control(AM_HAL_GPIO_INT_CHANNEL_0, AM_HAL_GPIO_INT_CTRL_INDV_ENABLE, &pin_number));
   NVIC_SetPriority(GPIO0_001F_IRQn + GPIO_NUM2IDX(PIN_USB_DETECT), NVIC_configKERNEL_INTERRUPT_PRIORITY);
   NVIC_EnableIRQ(GPIO0_001F_IRQn + GPIO_NUM2IDX(PIN_USB_DETECT));

   // Power up USB if cable connected
   NVIC_SetPriority(USB0_IRQn, NVIC_configMAX_SYSCALL_INTERRUPT_PRIORITY);
   if (cable_connected)
   {
      am_hal_gpio_state_write(PIN_USB_ENABLE1, AM_HAL_GPIO_OUTPUT_SET);
      am_hal_gpio_state_write(PIN_USB_ENABLE2, AM_HAL_GPIO_OUTPUT_SET);
   }
}

bool usb_cable_connected(void)
{
   // Return current USB cable connection status
   return cable_connected;
}

#else

void usb_init(void) {}
bool usb_cable_connected(void) { return false; }

#endif  // #if REVISION_ID > REVISION_N
