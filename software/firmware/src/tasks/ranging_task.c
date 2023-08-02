// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "app_tasks.h"
#include "rtc.h"
#include "scheduler.h"


// Static Global Variables ---------------------------------------------------------------------------------------------

static TaskHandle_t ranging_task_handle;
static volatile bool is_ranging = false;


// Public API Functions ------------------------------------------------------------------------------------------------

void ranging_begin(schedule_role_t role)
{
   // Notify the ranging task to start with the indicated role
   is_ranging = true;
   scheduler_prepare();
   xTaskNotify(ranging_task_handle, role, eSetValueWithOverwrite);
}

void ranging_end(void)
{
   // Force ranging task to end
   if (is_ranging)
      scheduler_stop();
}

bool ranging_active(void)
{
   // Return whether actively ranging
   return is_ranging;
}

void ranging_schedule_device(const uint8_t *device_id)
{
   // Instruct ranging scheduler to add device
   scheduler_add_device(device_id[0]);
}

void RangingTask(void *uid)
{
   // Store the ranging task handle and initialize the ranging scheduler
   ranging_task_handle = xTaskGetCurrentTaskHandle();
   uint32_t desired_role_bits = 0;
   scheduler_init(uid);

   // Loop forever
   while (true)
   {
      // Sleep until time to start ranging with the indicated role
      if ((xTaskNotifyWait(pdFALSE, 0xffffffff, &desired_role_bits, portMAX_DELAY) == pdTRUE) && uid)
         scheduler_run((schedule_role_t)desired_role_bits, rtc_get_timestamp());

      // Notify the application that network connectivity has been lost
      is_ranging = false;
      app_notify(APP_NOTIFY_NETWORK_LOST, false);
   }
}
