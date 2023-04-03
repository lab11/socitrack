// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "bluetooth.h"
#include "computation_phase.h"
#include "deca_interface.h"
#include "logging.h"
#include "ranging_phase.h"
#include "schedule_phase.h"
#include "scheduler.h"
#include "status_phase.h"
#include "system.h"


// Static Global Variables ---------------------------------------------------------------------------------------------

static scheduler_phase_t ranging_phase;
static TaskHandle_t notification_handle = 0;
static am_hal_timer_config_t wakeup_timer_config;
static uint8_t ranging_results[MAX_COMPRESSED_RANGE_DATA_LENGTH];
static uint8_t read_buffer[768], device_eui, schedule_reception_timeout;
static uint8_t empty_round_timeout, eui[EUI_LEN];
static volatile bool is_running;


// Private Helper Functions --------------------------------------------------------------------------------------------

static bool fix_network_errors(uint8_t num_ranging_results)
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
      return false;
   }
   return true;
}

static void handle_range_computation_phase(bool is_master)
{
   // Put the radio into deep-sleep mode and set a timer to wake it before the next round
   ranging_radio_sleep(true);
   if (!is_master)
   {
      const uint32_t remaing_time_us = 1000000 - RADIO_WAKEUP_SAFETY_DELAY_US - SCHEDULE_BROADCAST_PERIOD_US - (ranging_phase_get_time_slices() * RANGING_ITERATION_INTERVAL_US) - (schedule_phase_get_num_devices() * RANGE_STATUS_BROADCAST_PERIOD_US);
      wakeup_timer_config.ui32Compare0 = (uint32_t)((float)RADIO_WAKEUP_TIMER_TICK_RATE_HZ / (1000000.0f / remaing_time_us));
      am_hal_timer_config(RADIO_WAKEUP_TIMER_NUMBER, &wakeup_timer_config);
      am_hal_timer_clear(RADIO_WAKEUP_TIMER_NUMBER);
   }

   // Carry out the ranging algorithm and fix any detected network errors
   compute_ranges(ranging_results);
   if (!is_master || fix_network_errors(ranging_results[0]))
   {
      bluetooth_write_range_results(ranging_results, 1 + ((uint16_t)ranging_results[0] * COMPRESSED_RANGE_DATUM_LENGTH));
#ifndef _TEST_RANGING_TASK
      storage_write_ranging_data(schedule_phase_get_timestamp(), ranging_results, 1 + ((uint32_t)ranging_results[0] * COMPRESSED_RANGE_DATUM_LENGTH));
#else
      print_ranges(schedule_phase_get_timestamp(), ranging_results, 1 + ((uint32_t)ranging_results[0] * COMPRESSED_RANGE_DATUM_LENGTH));
#endif
   }
   ranging_phase = UNSCHEDULED_TIME_PHASE;
}


// Interrupt Service Routines and Callbacks ----------------------------------------------------------------------------

void am_rtc_isr(void)
{
   // Notify the main task to handle the interrupt
   BaseType_t xHigherPriorityTaskWoken = pdFALSE;
   am_hal_rtc_interrupt_clear(AM_HAL_RTC_INT_ALM);
   if (notification_handle)
   {
      xTaskNotifyFromISR(notification_handle, RANGING_NEW_ROUND_START, eSetBits, &xHigherPriorityTaskWoken);
      portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
   }
}

void am_timer02_isr(void)
{
   // Notify the main task to handle the interrupt
   BaseType_t xHigherPriorityTaskWoken = pdFALSE;
   am_hal_timer_interrupt_clear(AM_HAL_TIMER_MASK(RADIO_WAKEUP_TIMER_NUMBER, AM_HAL_TIMER_COMPARE_BOTH));
   xTaskNotifyFromISR(notification_handle, RANGING_NEW_ROUND_START, eSetBits, &xHigherPriorityTaskWoken);
   portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

static void tx_callback(const dwt_cb_data_t *txData)
{
   // Notify the main task to handle the interrupt
   BaseType_t xHigherPriorityTaskWoken = pdFALSE;
   xTaskNotifyFromISR(notification_handle, RANGING_TX_COMPLETE, eSetBits, &xHigherPriorityTaskWoken);
   portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

static void rx_callback(const dwt_cb_data_t *rxData)
{
   // Read packet data and ensure that no errors were encountered
   if (rxData->datalength > sizeof(read_buffer))
   {
      read_buffer[sizeof(ieee154_header_t)] = UNKNOWN_PACKET;
      print("ERROR: Received packet which exceeds maximal length (received %u bytes)!\n", rxData->datalength);
   }

   // Notify the main task to handle the interrupt
   BaseType_t xHigherPriorityTaskWoken = pdFALSE;
   dwt_readrxdata(read_buffer, rxData->datalength, 0);
   xTaskNotifyFromISR(notification_handle, RANGING_RX_COMPLETE, eSetBits, &xHigherPriorityTaskWoken);
   portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

static void rx_timeout_callback(const dwt_cb_data_t *rxData)
{
   // Notify the main task to handle the interrupt
   BaseType_t xHigherPriorityTaskWoken = pdFALSE;
   xTaskNotifyFromISR(notification_handle, RANGING_RX_TIMEOUT, eSetBits, &xHigherPriorityTaskWoken);
   portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}


// Public API Functions ------------------------------------------------------------------------------------------------

void scheduler_init(uint8_t *uid)
{
   // Store the device EUI
   memcpy(eui, uid, EUI_LEN);
   device_eui = eui[0];

   // Set the DW3000 callback configuration
   ranging_radio_register_callbacks(tx_callback, rx_callback, rx_timeout_callback, rx_timeout_callback);
}

void scheduler_run(schedule_role_t role, uint32_t timestamp)
{
   // Wake up the DW3000 ranging radio and set it to the correct channel
   ranging_radio_wakeup();
   ranging_radio_choose_channel(RADIO_XMIT_CHANNEL);

   // Initialize all static ranging variables
   notification_handle = xTaskGetCurrentTaskHandle();
   memset(ranging_results, 0, sizeof(ranging_results));
   schedule_reception_timeout = empty_round_timeout = 0;
   ranging_phase = UNSCHEDULED_TIME_PHASE;

   // Initialize the Schedule, Ranging, and Status phases
   schedule_phase_initialize(eui, role == ROLE_MASTER, timestamp - 1);
   ranging_phase_initialize(eui);
   status_phase_initialize(eui);

   // Initialize the scheduler or wakeup timers based on the device role
   is_running = true;
   if (role == ROLE_MASTER)
   {
      // Initialize the scheduler timer
      am_hal_rtc_time_t scheduler_interval = {
         .ui32ReadError = 0, .ui32CenturyEnable = 0, .ui32Weekday = 0, .ui32Century = 0, .ui32Year = 0,
         .ui32Month = 0, .ui32DayOfMonth = 0, .ui32Hour = 0, .ui32Minute = 0, .ui32Second = 1, .ui32Hundredths = 0 };
      am_hal_rtc_alarm_set(&scheduler_interval, AM_HAL_RTC_ALM_RPT_SEC);
      am_hal_rtc_interrupt_enable(AM_HAL_RTC_INT_ALM);
      NVIC_SetPriority(RTC_IRQn, NVIC_configMAX_SYSCALL_INTERRUPT_PRIORITY + 1);
      NVIC_EnableIRQ(RTC_IRQn);
   }
   else
   {
      // Initialize the radio wakeup timer
      am_hal_timer_default_config_set(&wakeup_timer_config);
      am_hal_timer_interrupt_enable(AM_HAL_TIMER_MASK(RADIO_WAKEUP_TIMER_NUMBER, AM_HAL_TIMER_COMPARE0));
      NVIC_SetPriority(TIMER0_IRQn + RADIO_WAKEUP_TIMER_NUMBER, NVIC_configMAX_SYSCALL_INTERRUPT_PRIORITY + 1);
      NVIC_EnableIRQ(TIMER0_IRQn + RADIO_WAKEUP_TIMER_NUMBER);
      print("INFO: Searching for an existing network\n");
      schedule_phase_begin();
   }

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
            while (ranging_phase == UPDATING_SCHEDULE_PHASE)
               vTaskDelay(1);
            ranging_phase = schedule_phase_begin() ? SCHEDULE_PHASE : RANGING_ERROR;
         }
         if ((pending_actions & RANGING_TX_COMPLETE) != 0)
            ranging_phase = schedule_phase_tx_complete();
         if ((pending_actions & RANGING_RX_COMPLETE) != 0)
            ranging_phase = schedule_phase_rx_complete((schedule_packet_t*)read_buffer);
         if ((pending_actions & RANGING_RX_TIMEOUT) != 0)
            ranging_phase = schedule_phase_rx_error();

         // Carry out logic based on the current reported phase of the ranging protocol
         switch (ranging_phase)
         {
            case RANGING_PHASE:
               schedule_reception_timeout = 0;
               break;
            case RANGE_COMPUTATION_PHASE:
               handle_range_computation_phase(role == ROLE_MASTER);
               break;
            case RANGING_ERROR:
               if (role == ROLE_MASTER)
                  ranging_phase = UNSCHEDULED_TIME_PHASE;
               else if (++schedule_reception_timeout >= NETWORK_SEARCH_TIME_SECONDS)
               {
                     // Stop the ranging task if no network was detected after a period of time
                     print("WARNING: Timed out searching for an existing network\n");
#ifndef _TEST_RANGING_TASK
                     is_running = false;
#else
                     schedule_phase_begin();
                     schedule_reception_timeout = 0;
#endif
               }
               else
                  schedule_phase_begin();
               break;
            case MESSAGE_COLLISION:
               print("WARNING: Stopping ranging due to possible network collision\n");
#ifndef _TEST_RANGING_TASK
               is_running = false;
#else
               schedule_phase_begin();
               schedule_reception_timeout = 0;
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
}

void scheduler_add_device(uint8_t eui)
{
   // Ensure that schedule changes only occur while not actively ranging
   while (ranging_phase != UNSCHEDULED_TIME_PHASE)
      vTaskDelay(pdMS_TO_TICKS(2));
   ranging_phase = UPDATING_SCHEDULE_PHASE;
   schedule_phase_add_device(eui);
   ranging_phase = UNSCHEDULED_TIME_PHASE;
}

void scheduler_stop(void)
{
   // Notify the scheduling task that it is time to stop
   is_running = false;
   xTaskNotify(notification_handle, RANGING_STOP, eSetBits);
}
