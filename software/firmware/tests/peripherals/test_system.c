#include "logging.h"
#include "system.h"

static volatile int i;
static uint8_t uid[EUI_LEN];
static TaskHandle_t rtos_task_handle;

void RtosIdleTask(void *param)
{
   // Put RTOS into Wait Mode forever
   while (true)
      vTaskDelay(portMAX_DELAY);
}

void RtosActiveTask(void *param)
{
   // Ensure that RTOS always has something to keep it awake
   while (true)
      for (i = 0; i < 1000000; ++i)
         ++i;
}

void idle_cpu_power_test(void)
{
   // Put CPU into deep sleep forever
   while (true)
      am_hal_sysctrl_sleep(AM_HAL_SYSCTRL_SLEEP_DEEP);
}

void idle_cpu_with_rtos_power_test(void)
{
   // Test power consumption with FreeRTOS when permanently in tickless idle mode
   configASSERT1(xTaskCreate(RtosIdleTask, "RtosIdleTask", 512, NULL, 3, &rtos_task_handle));
   vTaskStartScheduler();
}

void active_cpu_with_rtos_power_test(void)
{
   // Test power consumption with FreeRTOS when in an active CPU loop
   configASSERT1(xTaskCreate(RtosActiveTask, "RtosActiveTask", 512, NULL, 3, &rtos_task_handle));
   vTaskStartScheduler();
}

int main(void)
{
   // Set up the system hardware and retrieve the Device UID
   setup_hardware();
   system_read_UID(uid, sizeof(uid));
   print("System initialized, UID = %X", uid[0]);
   for (size_t i = 1; i < EUI_LEN; ++i)
      print(":%02X", uid[i]);
   print("\n");

   // Choose which test to run
   idle_cpu_power_test();
   //idle_cpu_with_rtos_power_test();
   //active_cpu_with_rtos_power_test();

   // Should never reach this point
   return 0;
}
