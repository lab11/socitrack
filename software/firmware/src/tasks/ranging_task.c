// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "app_tasks.h"
#include "bluetooth.h"
#include "rtc.h"
#include "scheduler.h"


// Static Global Variables ---------------------------------------------------------------------------------------------

static TaskHandle_t ranging_task_handle;
static volatile bool is_ranging;


// Public API Functions ------------------------------------------------------------------------------------------------

void ranging_begin(schedule_role_t role)
{
   // Notify the ranging task to start with the indicated role
   is_ranging = true;
   xTaskNotify(ranging_task_handle, role, eSetValueWithOverwrite);
}

bool ranging_active(void)
{
   // Return whether actively ranging
   return is_ranging;
}

void RangingTask(void *uid)
{
   // Store the ranging task handle and initialize the ranging scheduler
   ranging_task_handle = xTaskGetCurrentTaskHandle();
   uint32_t desired_role_bits = 0;
   scheduler_init(uid);
   is_ranging = false;

   // Loop forever
   while (true)
   {
      // Sleep until time to start ranging with the indicated role
      if ((xTaskNotifyWait(pdFALSE, 0xffffffff, &desired_role_bits, portMAX_DELAY) == pdTRUE) && uid)
      {
         bluetooth_set_current_ranging_role(desired_role_bits);
         scheduler_run((schedule_role_t)desired_role_bits, rtc_get_timestamp());
         bluetooth_set_current_ranging_role(ROLE_IDLE);
      }

      // Notify the application that network connectivity has been lost
      is_ranging = false;
      app_notify(APP_NOTIFY_NETWORK_LOST, false);
   }
}
