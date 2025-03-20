#include "app_tasks.h"
#include "logging.h"
#include "rtc.h"
#include "system.h"
#include "usb.h"

int main(void)
{
   // Set up system hardware
   setup_hardware();
   rtc_init();
   usb_init();

   // Create the USB task and start the task scheduler
   static StaticTask_t usb_task_tcb, usb_cdc_task_tcb;
   static StackType_t usb_task_stack[configMINIMAL_STACK_SIZE], usb_cdc_task_stack[2*configMINIMAL_STACK_SIZE];
   xTaskCreateStatic(UsbTask, "UsbTask", configMINIMAL_STACK_SIZE, NULL, configMAX_PRIORITIES-1, usb_task_stack, &usb_task_tcb);
   xTaskCreateStatic(UsbCdcTask, "UsbCdcTask", 2 * configMINIMAL_STACK_SIZE, NULL, configMAX_PRIORITIES-2, usb_cdc_task_stack, &usb_cdc_task_tcb);
   vTaskStartScheduler();

   // Should never reach this point
   return 0;
}
