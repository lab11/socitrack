// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "bluetooth.h"
#include "computation_phase.h"
#include "deca_interface.h"
#include "logging.h"
#include "ranging_phase.h"
#include "schedule_phase.h"
#include "scheduler.h"
#include "status_phase.h"
#include "subscription_phase.h"
#include "system.h"


// Static Global Variables ---------------------------------------------------------------------------------------------

static TaskHandle_t notification_handle;
static am_hal_timer_config_t wakeup_timer_config;
static uint8_t empty_round_timeout, eui[EUI_LEN];
static uint8_t ranging_results[MAX_COMPRESSED_RANGE_DATA_LENGTH];
static uint8_t read_buffer[128], device_eui, reception_timeout;
static volatile schedule_role_t current_role = ROLE_IDLE;
static volatile scheduler_phase_t ranging_phase;
static volatile bool is_running;


// Private Helper Functions --------------------------------------------------------------------------------------------

static void fix_network_errors(uint8_t num_ranging_results)
{
   // Have the Scheduler Phase handle any new device timeouts
   uint8_t num_devices = 0;
   const uint8_t *device_list = status_phase_get_detected_devices(&num_devices);
   for (uint8_t i = 0; i < num_devices; ++i)
      schedule_phase_update_device_presence(device_list[i]);
   schedule_phase_handle_device_timeouts();

   // Check if we are still synchronized with the network
   empty_round_timeout = (!num_devices && !num_ranging_results) ? (empty_round_timeout + 1) : 0;
   if (empty_round_timeout >= MAX_EMPTY_ROUNDS_BEFORE_STATE_CHANGE)
   {
      print("WARNING: No network traffic received\n");
#ifndef _TEST_RANGING_TASK
      is_running = false;
#else
      empty_round_timeout = 0;
#endif
   }
}

static void handle_range_computation_phase(void)
{
   // Put the radio into deep-sleep mode and handle role-specific tasks
   ranging_radio_sleep(true);
   switch (current_role)
   {
      case ROLE_MASTER:
      {
         // Carry out the ranging algorithm and fix any detected network errors
         compute_ranges(ranging_results);
         fix_network_errors(ranging_results[0]);
         bluetooth_write_range_results(ranging_results, 1 + ((uint16_t)ranging_results[0] * COMPRESSED_RANGE_DATUM_LENGTH));
#ifndef _TEST_RANGING_TASK
#ifndef _TEST_BLE_RANGING_TASK
         if (ranging_results[0])
            storage_write_ranging_data(schedule_phase_get_timestamp(), ranging_results, 1 + ((uint32_t)ranging_results[0] * COMPRESSED_RANGE_DATUM_LENGTH));
#endif
#endif
         print_ranges(schedule_phase_get_timestamp(), ranging_results, 1 + ((uint32_t)ranging_results[0] * COMPRESSED_RANGE_DATUM_LENGTH));
         break;
      }
      case ROLE_PARTICIPANT:
      {
         // Set a timer to wake the radio before the next round
         const uint32_t remaing_time_us = 1000000 - RADIO_WAKEUP_SAFETY_DELAY_US - SCHEDULE_BROADCAST_PERIOD_US - SUBSCRIPTION_BROADCAST_PERIOD_US - ranging_phase_get_duration() - (schedule_phase_get_num_devices() * RANGE_STATUS_BROADCAST_PERIOD_US);
         wakeup_timer_config.ui32Compare0 = (uint32_t)((float)RADIO_WAKEUP_TIMER_TICK_RATE_HZ / (1000000.0f / remaing_time_us));
         am_hal_timer_config(RADIO_WAKEUP_TIMER_NUMBER, &wakeup_timer_config);
         am_hal_timer_clear(RADIO_WAKEUP_TIMER_NUMBER);

         // Carry out the ranging algorithm and fix any detected network errors
         compute_ranges(ranging_results);
         bluetooth_write_range_results(ranging_results, 1 + ((uint16_t)ranging_results[0] * COMPRESSED_RANGE_DATUM_LENGTH));
#ifndef _TEST_RANGING_TASK
#ifndef _TEST_BLE_RANGING_TASK
         if (ranging_results[0])
            storage_write_ranging_data(schedule_phase_get_timestamp(), ranging_results, 1 + ((uint32_t)ranging_results[0] * COMPRESSED_RANGE_DATUM_LENGTH));
#endif
#endif
         print_ranges(schedule_phase_get_timestamp(), ranging_results, 1 + ((uint32_t)ranging_results[0] * COMPRESSED_RANGE_DATUM_LENGTH));
         break;
      }
      default:
      {
         // Set a timer to wake the radio before the next round
         const uint32_t remaing_time_us = 1000000 - RADIO_WAKEUP_SAFETY_DELAY_US - SCHEDULE_BROADCAST_PERIOD_US - SUBSCRIPTION_BROADCAST_PERIOD_US;
         wakeup_timer_config.ui32Compare0 = (uint32_t)((float)RADIO_WAKEUP_TIMER_TICK_RATE_HZ / (1000000.0f / remaing_time_us));
         am_hal_timer_config(RADIO_WAKEUP_TIMER_NUMBER, &wakeup_timer_config);
         am_hal_timer_clear(RADIO_WAKEUP_TIMER_NUMBER);
         break;
      }
   }
   ranging_phase = UNSCHEDULED_TIME_PHASE;
}


// Interrupt Service Routines and Callbacks ----------------------------------------------------------------------------

void am_timer02_isr(void)
{
   // Notify the main task to handle the interrupt
   BaseType_t xHigherPriorityTaskWoken = pdFALSE;
   am_hal_timer_interrupt_clear(AM_HAL_TIMER_MASK(RADIO_WAKEUP_TIMER_NUMBER, AM_HAL_TIMER_COMPARE_BOTH));
   xTaskNotifyFromISR(notification_handle, RANGING_NEW_ROUND_START, eSetBits, &xHigherPriorityTaskWoken);
   portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void scheduler_rtc_isr(void)
{
   // Notify the main task to handle the interrupt
   BaseType_t xHigherPriorityTaskWoken = pdFALSE;
   xTaskNotifyFromISR(notification_handle, RANGING_NEW_ROUND_START, eSetBits, &xHigherPriorityTaskWoken);
   portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

static void tx_callback(const dwt_cb_data_t *txData)
{
   // Allow the scheduling protocol to handle the interrupt
   ranging_phase = schedule_phase_tx_complete();

   // Determine if the main task needs to be woken up to handle the current ranging phase
   if ((ranging_phase == RADIO_ERROR) || (ranging_phase == RANGE_COMPUTATION_PHASE))
   {
      BaseType_t xHigherPriorityTaskWoken = pdFALSE;
      xTaskNotifyFromISR(notification_handle, RANGING_TX_COMPLETE, eSetBits, &xHigherPriorityTaskWoken);
      portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
   }
}

static void rx_callback(const dwt_cb_data_t *rxData)
{
   // Read the received data packet and allow the scheduling protocol to handle it
   dwt_readrxdata(read_buffer, rxData->datalength, 0);
   ranging_phase = schedule_phase_rx_complete((schedule_packet_t*)read_buffer);

   // Determine if the main task needs to be woken up to handle the current ranging phase
   if ((ranging_phase == RANGE_COMPUTATION_PHASE) || (ranging_phase == MESSAGE_COLLISION) || (ranging_phase == RADIO_ERROR))
   {
      BaseType_t xHigherPriorityTaskWoken = pdFALSE;
      xTaskNotifyFromISR(notification_handle, RANGING_RX_COMPLETE, eSetBits, &xHigherPriorityTaskWoken);
      portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
   }
}

static void rx_timeout_callback(const dwt_cb_data_t *rxData)
{
   // Allow the scheduling protocol to handle the interrupt
   ranging_phase = schedule_phase_rx_error();

   // Determine if the main task needs to be woken up to handle the current ranging phase
   if ((ranging_phase == RANGING_ERROR) || (ranging_phase == RADIO_ERROR) || (ranging_phase == RANGE_COMPUTATION_PHASE))
   {
      BaseType_t xHigherPriorityTaskWoken = pdFALSE;
      xTaskNotifyFromISR(notification_handle, RANGING_RX_TIMEOUT, eSetBits, &xHigherPriorityTaskWoken);
      portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
   }
}


// Public API Functions ------------------------------------------------------------------------------------------------

void scheduler_init(uint8_t *uid)
{
   // Store the device EUI
   if (uid)
   {
      memcpy(eui, uid, EUI_LEN);
      device_eui = eui[0];

      // Set the DW3000 callback configuration
      ranging_radio_register_callbacks(tx_callback, rx_callback, rx_timeout_callback, rx_timeout_callback);
   }
   is_running = false;
}

schedule_role_t scheduler_get_current_role(void)
{
   return current_role;
}

void scheduler_run(schedule_role_t role, uint32_t timestamp)
{
   // Ensure that the role is a valid ranging role
   if ((role != ROLE_MASTER) && (role != ROLE_PARTICIPANT))
      return;

   // Wake up the DW3000 ranging radio and set it to the correct channel
   ranging_radio_wakeup();
   ranging_radio_choose_channel(RADIO_XMIT_CHANNEL);

   // Initialize all static ranging variables
   notification_handle = xTaskGetCurrentTaskHandle();
   memset(ranging_results, 0, sizeof(ranging_results));
   reception_timeout = empty_round_timeout = 0;
   ranging_phase = UNSCHEDULED_TIME_PHASE;

   // Initialize the Schedule, Ranging, Status, and Subscription phases
   schedule_phase_initialize(eui, role == ROLE_MASTER, timestamp - 1);
   ranging_phase_initialize(eui);
   status_phase_initialize(eui);
   subscription_phase_initialize(eui);

   // Initialize the scheduler or wakeup timers based on the device role
   is_running = true;
   if (role == ROLE_MASTER)
   {
      // Initialize the scheduler timer
      current_role = ROLE_MASTER;
      am_hal_rtc_time_t scheduler_interval = {
         .ui32ReadError = 0, .ui32CenturyEnable = 0, .ui32Weekday = 0, .ui32Century = 0, .ui32Year = 0,
         .ui32Month = 0, .ui32DayOfMonth = 0, .ui32Hour = 0, .ui32Minute = 0, .ui32Second = 1, .ui32Hundredths = 0 };
      am_hal_rtc_alarm_set(&scheduler_interval, AM_HAL_RTC_ALM_RPT_SEC);
      am_hal_rtc_interrupt_enable(AM_HAL_RTC_INT_ALM);
      NVIC_SetPriority(RTC_IRQn, NVIC_configKERNEL_INTERRUPT_PRIORITY - 1);
      NVIC_EnableIRQ(RTC_IRQn);
   }
   else
   {
      // Initialize the radio wakeup timer
      current_role = ROLE_IDLE;
      am_hal_timer_default_config_set(&wakeup_timer_config);
      am_hal_timer_interrupt_enable(AM_HAL_TIMER_MASK(RADIO_WAKEUP_TIMER_NUMBER, AM_HAL_TIMER_COMPARE0));
      NVIC_SetPriority(TIMER0_IRQn + RADIO_WAKEUP_TIMER_NUMBER, NVIC_configKERNEL_INTERRUPT_PRIORITY - 1);
      NVIC_EnableIRQ(TIMER0_IRQn + RADIO_WAKEUP_TIMER_NUMBER);
      print("INFO: Searching for an existing network\n");
      ranging_phase = schedule_phase_begin();
   }

   // Notify the application that network connectivity has been established
   app_notify(APP_NOTIFY_NETWORK_CONNECTED, false);

   // Loop forever waiting for actions to wake us up
   uint32_t pending_actions = 0;
   while (is_running)
      if (xTaskNotifyWait(pdFALSE, 0xffffffff, &pending_actions, portMAX_DELAY) == pdTRUE)
      {
         // Handle any pending actions
         if ((pending_actions & RANGING_NEW_ROUND_START) != 0)
         {
            // Wake up the radio and wait until all schedule updating tasks have completed
            ranging_radio_wakeup();
            ranging_phase = schedule_phase_begin();
         }

         // Carry out logic based on the current reported phase of the ranging protocol
         switch (ranging_phase)
         {
            case RANGE_COMPUTATION_PHASE:
               reception_timeout = 0;
               if (ranging_phase_was_scheduled() && (current_role == ROLE_IDLE))
               {
                  // Notify the application that our network role has changed
                  current_role = ROLE_PARTICIPANT;
                  app_notify(APP_NOTIFY_VERIFY_CONFIGURATION, false);
               }
               handle_range_computation_phase();
               break;
            case RADIO_ERROR:
               if (current_role == ROLE_MASTER)
                  ranging_phase = UNSCHEDULED_TIME_PHASE;
               else
                  ranging_phase = schedule_phase_begin();
               break;
            case RANGING_ERROR:
               if (current_role == ROLE_MASTER)
                  ranging_phase = UNSCHEDULED_TIME_PHASE;
               else if (++reception_timeout >= NETWORK_SEARCH_TIME_SECONDS)
               {
                     // Stop the ranging task if no network was detected after a period of time
                     print("WARNING: Timed out searching for an existing network\n");
#ifndef _TEST_RANGING_TASK
                     is_running = false;
#else
                     ranging_phase = schedule_phase_begin();
                     reception_timeout = 0;
#endif
               }
               else
                  ranging_phase = schedule_phase_begin();
               break;
            case MESSAGE_COLLISION:
               print("WARNING: Stopping ranging due to possible network collision\n");
#ifndef _TEST_RANGING_TASK
               is_running = false;
#else
               ranging_phase = schedule_phase_begin();
               reception_timeout = 0;
#endif
               break;
            default:
               break;
         }
      }

   // Disable all ranging timers and interrupts
   const am_hal_rtc_time_t scheduler_interval = {
      .ui32ReadError = 0, .ui32CenturyEnable = 0, .ui32Weekday = 0, .ui32Century = 0, .ui32Year = 0,
      .ui32Month = 0, .ui32DayOfMonth = 0, .ui32Hour = 0, .ui32Minute = 0, .ui32Second = 0, .ui32Hundredths = 0 };
   am_hal_rtc_alarm_set((am_hal_rtc_time_t*)&scheduler_interval, AM_HAL_RTC_ALM_RPT_DIS);
   am_hal_timer_interrupt_disable(AM_HAL_TIMER_MASK(RADIO_WAKEUP_TIMER_NUMBER, AM_HAL_TIMER_COMPARE_BOTH));
   am_hal_rtc_interrupt_disable(AM_HAL_RTC_INT_ALM);
   NVIC_DisableIRQ(TIMER0_IRQn + RADIO_WAKEUP_TIMER_NUMBER);
   NVIC_DisableIRQ(RTC_IRQn);

   // Put the DW3000 radio into deep sleep mode
   ranging_radio_sleep(true);

   // Notify the application that network connectivity has been lost
   current_role = ROLE_IDLE;
   app_notify(APP_NOTIFY_NETWORK_LOST, false);
}

void scheduler_stop(void)
{
   // Notify the scheduling task that it is time to stop
   is_running = false;
   xTaskNotify(notification_handle, RANGING_STOP, eSetBits);
}
