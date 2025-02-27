#include "app_tasks.h"
#include "button.h"
#include "logging.h"
#include "ranging.h"
#include "rtc.h"
#include "scheduler.h"
#include "system.h"

// Static Global Variables ---------------------------------------------------------------------------------------------

static StaticTask_t ranging_task_tcb, test_task_tcb;
static TaskHandle_t ranging_task_handle, test_task_handle;
static StackType_t ranging_task_stack[configMINIMAL_STACK_SIZE], test_task_stack[configMINIMAL_STACK_SIZE];
static uint8_t uid[EUI_LEN], test_state;
static volatile bool is_ranging;

#if REVISION_ID == REVISION_APOLLO4_EVB
static void button_pressed(void *button_number)
{
   // Notify the Test Task that a button was pressed
   BaseType_t xHigherPriorityTaskWoken = pdFALSE;
   vTaskNotifyGiveFromISR(test_task_handle, &xHigherPriorityTaskWoken);
   portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
#else
static const schedule_role_t desired_role = ROLE_MASTER;
#endif

void TestTask(void *uid)
{
   // Initiate interrupt-based button press detection
#if REVISION_ID == REVISION_APOLLO4_EVB
   button_press_register_callback(PIN_BUTTON_1, button_pressed);
   button_press_register_callback(PIN_BUTTON_2, button_pressed);

   // Loop forever
   while (true)
   {
      // Sleep until a button is pressed
      if (ulTaskNotifyTake(pdTRUE, portMAX_DELAY) == pdTRUE)
      {
         // Change to a new testing state
         test_state = (test_state + 1) % 3;
         switch (test_state)
         {
            case 0:
               if (is_ranging)
                  scheduler_stop();
               while (is_ranging);
               break;
            case 1:
            {
               if (is_ranging)
                  scheduler_stop();
               while (is_ranging);
               xTaskNotify(ranging_task_handle, ROLE_PARTICIPANT, eSetValueWithOverwrite);
               break;
            }
            case 2:
            {
               if (is_ranging)
                  scheduler_stop();
               while (is_ranging);
               xTaskNotify(ranging_task_handle, ROLE_MASTER, eSetValueWithOverwrite);
               break;
            }
            default:
               break;
         }
      }
   }
#else
   if (desired_role == ROLE_MASTER)
      xTaskNotify(ranging_task_handle, ROLE_MASTER, eSetValueWithOverwrite);
   else
      xTaskNotify(ranging_task_handle, ROLE_PARTICIPANT, eSetValueWithOverwrite);
   vTaskDelay(1000);
#endif
}

void RangeTask(void *uid)
{
   // Initialize the ranging scheduler
   test_state = 0;
   is_ranging = false;
   uint32_t desired_role_bits;
   scheduler_init(uid);

   // Loop forever
   while (true)
   {
      // Sleep until time to start ranging with the indicated role
      if (xTaskNotifyWait(pdFALSE, 0xffffffff, &desired_role_bits, portMAX_DELAY) == pdTRUE)
      {
         is_ranging = true;
         scheduler_run((schedule_role_t)desired_role_bits);
      }
      is_ranging = false;
   }
}

int main(void)
{
   // Set up the system hardware
   setup_hardware();
   system_read_UID(uid, sizeof(uid));
   buttons_init();
   rtc_init();

   // Initialize the ranging radio and put it into deep sleep
   ranging_radio_init(uid);
   ranging_radio_sleep(true);

   // Create the ranging tasks and start the task scheduler
   ranging_task_handle = xTaskCreateStatic(RangeTask, "RangeTask", configMINIMAL_STACK_SIZE, uid, 5, ranging_task_stack, &ranging_task_tcb);
   test_task_handle = xTaskCreateStatic(TestTask, "TestTask", configMINIMAL_STACK_SIZE, NULL, 4, test_task_stack, &test_task_tcb);
   vTaskStartScheduler();
   return 0;
}
