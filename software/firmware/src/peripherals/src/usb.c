// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "app_tasks.h"
#include "logging.h"
#include "system.h"
#include "timers.h"
#include "usb.h"


#if REVISION_ID > REVISION_N

// Static Global Variables ---------------------------------------------------------------------------------------------

static volatile uint32_t cable_connected;
static bool booted_on_usb;


// Private Helper Functions --------------------------------------------------------------------------------------------

#ifdef __USE_FREERTOS__

static void usb_cable_connected_deferred(void *unused, uint32_t unused_value)
{
   // Record why the log ends here then restart into USB mode through the storage task
   storage_write_charging_status(BATTERY_PLUGGED);
   if (!storage_flush_and_shutdown())
      system_reset(true);
}

#endif  // #ifdef __USE_FREERTOS__

static void usb_cable_callback(void *pin_number)
{
   uint32_t detected = 0;
   am_hal_gpio_state_read(PIN_USB_DETECT, AM_HAL_GPIO_INPUT_READ, &detected);
   if (cable_connected)
   {
      if (booted_on_usb && !detected)
         system_reset(true);
   }
   else
   {
      cable_connected = detected;
      if (detected)
      {
#ifdef __USE_FREERTOS__
         // Tell the main task to reboot
         BaseType_t higher_priority_task_woken = pdFALSE;
         if ((xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) && (xTimerPendFunctionCallFromISR(usb_cable_connected_deferred, NULL, 0, &higher_priority_task_woken) == pdPASS))
         {
            portYIELD_FROM_ISR(higher_priority_task_woken);
            return;
         }
#endif  // #ifdef __USE_FREERTOS__
         system_reset(true);
      }
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
   uint32_t pin_number = PIN_USB_DETECT, detected = 0;
   am_hal_gpio_state_read(PIN_USB_DETECT, AM_HAL_GPIO_INPUT_READ, &detected);
   cable_connected = detected;
   booted_on_usb = detected;
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
