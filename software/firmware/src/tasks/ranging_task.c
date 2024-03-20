// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "app_tasks.h"
#include "logging.h"
#include "scheduler.h"
#include "system.h"


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

void RangingTask(void *scheduled_experiment)
{
   // Store the ranging task handle and initialize the ranging scheduler
   static uint32_t desired_role_bits = 0;
   ranging_task_handle = xTaskGetCurrentTaskHandle();
   scheduler_init((experiment_details_t*)scheduled_experiment);
   is_ranging = false;

   // Loop forever
   while (true)
   {
      // Sleep until time to start ranging with the indicated role
      if ((xTaskNotifyWait(pdFALSE, 0xffffffff, &desired_role_bits, portMAX_DELAY) == pdTRUE) && scheduled_experiment)
      {
         print("TotTag Ranging: Starting ranging task as %s\n", (desired_role_bits == ROLE_MASTER) ? "MASTER" : "PARTICIPANT");
         scheduler_run((schedule_role_t)desired_role_bits);
         print("TotTag Ranging: Ranging task has stopped!\n");
         is_ranging = false;
      }
   }
}
