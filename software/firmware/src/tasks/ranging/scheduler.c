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
static uint8_t empty_round_timeout, eui[EUI_LEN], read_buffer[128];
static uint8_t ranging_results[MAX_COMPRESSED_RANGE_DATA_LENGTH];
static uint32_t last_round_stimer, search_started_stimer;
static volatile schedule_role_t current_role = ROLE_IDLE;
static volatile scheduler_phase_t ranging_phase;
static volatile bool is_running;


// Private Helper Functions --------------------------------------------------------------------------------------------

static void idle_until_next_round(void)
{
   // Nothing further happens this round and the next one is a free-running timer away
   ranging_radio_sleep(true);
   ranging_phase = UNSCHEDULED_TIME_PHASE;
}

static void begin_schedule_phase(void)
{
   // Publish the phase with no window in which a radio interrupt could advance it and be overwritten
   AM_CRITICAL_BEGIN
   ranging_phase = schedule_phase_begin();
   AM_CRITICAL_END
}

#if !defined(_TEST_RANGING_TASK) && !defined(_TEST_NO_STORAGE)
static uint32_t schedule_reference_age_ms(void)
{
   // How long ago the master's timestamp for this round was sampled
   const uint32_t elapsed = am_hal_stimer_counter_get() - schedule_phase_get_reference_stimer();
   return (elapsed < RANGING_MS_TO_STIMER(SCHEDULING_INTERVAL_US / 1000u)) ? ((elapsed * 1000u) / RANGING_STIMER_HZ) : 0u;
}
#endif

static uint32_t round_elapsed_us(void)
{
   // How far into the round this device actually is
   const uint64_t now = (uint64_t)dwt_readsystimestamphi32() << 8;
   return DWT_TO_US((now - schedule_phase_get_reference_time_full()) & 0xFFFFFFFFFFULL);
}

static void arm_wakeup_timer(uint32_t elapsed_us)
{
   // Wake RADIO_WAKEUP_SAFETY_DELAY_US before the next round's reference instant
   const uint32_t target_us = SCHEDULING_INTERVAL_US - RADIO_WAKEUP_SAFETY_DELAY_US;
   const uint32_t remaining_us = (elapsed_us < target_us) ? (target_us - elapsed_us) : RADIO_WAKEUP_SAFETY_DELAY_US;
   wakeup_timer_config.ui32Compare0 = (uint32_t)(((uint64_t)RADIO_WAKEUP_TIMER_TICK_RATE_HZ * remaining_us) / 1000000u);
   am_hal_timer_config(RADIO_WAKEUP_TIMER_NUMBER, &wakeup_timer_config);
   am_hal_timer_clear(RADIO_WAKEUP_TIMER_NUMBER);
}

static void fix_network_errors(uint8_t num_ranging_results)
{
   // Presence is the union of what every device reachable from here reported
   const uint16_t present = status_phase_get_present_slots();
   const uint32_t num_scheduled = schedule_phase_get_num_devices();
   uint32_t num_devices = 0;
   for (uint32_t slot = 1; slot < num_scheduled; ++slot)
      if (present & (1u << slot))
      {
         schedule_phase_update_device_presence(schedule_phase_get_addr_from_slot((uint8_t)slot));
         ++num_devices;
      }
   schedule_phase_handle_device_timeouts();

   // Nominate the closest device heard this round to transmit last in the next one
   uint8_t nearest_slot = 0;
   int16_t nearest_mm = INT16_MAX;
   for (uint8_t i = 0; i < ranging_results[0]; ++i)
   {
      const uint32_t offset = 1 + ((uint32_t)i * COMPRESSED_RANGE_DATUM_LENGTH);
      int16_t range_mm = 0;
      memcpy(&range_mm, &ranging_results[offset + 1], sizeof(range_mm));
      const uint8_t slot = schedule_phase_get_slot_from_addr(ranging_results[offset]);
      if ((slot != UNSCHEDULED_SLOT) && slot && (range_mm < nearest_mm))
      {
         nearest_mm = range_mm;
         nearest_slot = slot;
      }
   }
   schedule_phase_set_master_nearest(nearest_slot);

   // A request from a device the master cannot hear arrives by way of one that can
   const uint8_t pending_subscriber = status_phase_get_pending_subscriber();
   if (pending_subscriber)
      schedule_phase_add_device(pending_subscriber);

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
   // Read the radio clock before the radio goes away then arm the next wake-up from it
   const uint32_t elapsed_us = round_elapsed_us();
   ranging_radio_sleep(true);
   if (current_role != ROLE_MASTER)
      arm_wakeup_timer(elapsed_us);
   switch (ranging_phase_was_scheduled() ? current_role : ROLE_IDLE)
   {
      case ROLE_MASTER:
      {
         // Carry out the ranging algorithm and fix any detected network errors
         compute_ranges(ranging_results);
         fix_network_errors(ranging_results[0]);
         const uint32_t data_timestamp = schedule_phase_get_timestamp();
         bluetooth_write_range_results(ranging_results, 1 + ((uint16_t)ranging_results[0] * COMPRESSED_RANGE_DATUM_LENGTH));
#ifndef _TEST_RANGING_TASK
#ifndef _TEST_NO_STORAGE
         if (ranging_results[0])
            storage_write_ranging_data(data_timestamp, ranging_results, 1 + ((uint32_t)ranging_results[0] * COMPRESSED_RANGE_DATUM_LENGTH), app_get_time_offset());
#endif
#endif
         print_ranges(app_experiment_time_to_rtc_time(data_timestamp), data_timestamp % 1000, ranging_results, 1 + ((uint32_t)ranging_results[0] * COMPRESSED_RANGE_DATUM_LENGTH));
         break;
      }
      case ROLE_PARTICIPANT:
      {
         // Carry out the ranging algorithm and fix any detected network errors
         compute_ranges(ranging_results);
         const uint32_t data_timestamp = schedule_phase_get_timestamp();
         bluetooth_write_range_results(ranging_results, 1 + ((uint16_t)ranging_results[0] * COMPRESSED_RANGE_DATUM_LENGTH));
#ifndef _TEST_RANGING_TASK
#ifndef _TEST_NO_STORAGE
         if (ranging_results[0])
         {
            // Wind back the local clock to match the master's timestamp for this round
            const int64_t local_at_reference = (int64_t)app_get_experiment_time(0) - (int64_t)schedule_reference_age_ms();
            storage_write_ranging_data(data_timestamp, ranging_results, 1 + ((uint32_t)ranging_results[0] * COMPRESSED_RANGE_DATUM_LENGTH), (int32_t)((int64_t)data_timestamp - local_at_reference));
         }
#endif
#endif
         print_ranges(app_experiment_time_to_rtc_time(data_timestamp), data_timestamp % 1000, ranging_results, 1 + ((uint32_t)ranging_results[0] * COMPRESSED_RANGE_DATUM_LENGTH));
         break;
      }
      default:
         break;
   }
   ranging_radio_note_network_size((uint8_t)schedule_phase_get_num_devices());
   ranging_phase = UNSCHEDULED_TIME_PHASE;
   last_round_stimer = am_hal_stimer_counter_get();
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

static void handle_rx_failure(uint32_t notification_reason)
{
   // Allow the scheduling protocol to handle the interrupt
   ranging_phase = schedule_phase_rx_error();

   // Determine if the main task needs to be woken up to handle the current ranging phase
   if ((ranging_phase == RANGING_ERROR) || (ranging_phase == RADIO_ERROR) || (ranging_phase == RANGE_COMPUTATION_PHASE))
   {
      BaseType_t xHigherPriorityTaskWoken = pdFALSE;
      xTaskNotifyFromISR(notification_handle, notification_reason, eSetBits, &xHigherPriorityTaskWoken);
      portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
   }
}

static void rx_timeout_callback(const dwt_cb_data_t *rxData)
{
   // A listening window that expired with nothing in it
   handle_rx_failure(RANGING_RX_TIMEOUT);
}

static void rx_error_callback(const dwt_cb_data_t *rxData)
{
   // A frame that arrived and could not be decoded
   handle_rx_failure(RANGING_RX_ERROR);
}


// Public API Functions ------------------------------------------------------------------------------------------------

void scheduler_init(experiment_details_t *details)
{
   // Store the device EUI and experiment details
   if (details)
   {
      schedule_phase_store_experiment_details(details);
      system_read_UID(eui, sizeof(eui));

      // Set the DW3000 callback configuration
      ranging_radio_register_callbacks(tx_callback, rx_callback, rx_timeout_callback, rx_error_callback);
   }
   is_running = false;
}

schedule_role_t scheduler_get_current_role(void)
{
   return current_role;
}

void scheduler_run(schedule_role_t role)
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
   last_round_stimer = search_started_stimer = am_hal_stimer_counter_get();
   empty_round_timeout = 0;
   ranging_phase = UNSCHEDULED_TIME_PHASE;

   // Initialize the Schedule, Ranging, Status, and Subscription phases
   schedule_phase_initialize(eui, role == ROLE_MASTER);
   ranging_phase_initialize(eui);
   status_phase_initialize(eui);
   subscription_phase_initialize(eui);

   // Initialize the wakeup timer based on the device role
   is_running = true;
   if (role == ROLE_MASTER)
   {
      // Initialize the scheduler timer
      current_role = ROLE_MASTER;
      am_hal_timer_default_config_set(&wakeup_timer_config);
      wakeup_timer_config.eFunction = AM_HAL_TIMER_FN_UPCOUNT;
      wakeup_timer_config.eInputClock = AM_HAL_TIMER_CLOCK_XT;
      wakeup_timer_config.ui32Compare0 = (uint32_t)(((uint64_t)RADIO_WAKEUP_TIMER_TICK_RATE_HZ * SCHEDULING_INTERVAL_US) / 1000000u);
      am_hal_timer_config(RADIO_WAKEUP_TIMER_NUMBER, &wakeup_timer_config);
      am_hal_timer_interrupt_enable(AM_HAL_TIMER_MASK(RADIO_WAKEUP_TIMER_NUMBER, AM_HAL_TIMER_COMPARE0));
      NVIC_SetPriority(TIMER0_IRQn + RADIO_WAKEUP_TIMER_NUMBER, NVIC_configKERNEL_INTERRUPT_PRIORITY - 1);
      NVIC_EnableIRQ(TIMER0_IRQn + RADIO_WAKEUP_TIMER_NUMBER);
      am_hal_timer_clear(RADIO_WAKEUP_TIMER_NUMBER);
      ranging_radio_sleep(true);
   }
   else
   {
      // Initialize the radio wakeup timer
      current_role = ROLE_IDLE;
      am_hal_timer_default_config_set(&wakeup_timer_config);
      wakeup_timer_config.eFunction = AM_HAL_TIMER_FN_UPCOUNT;
      wakeup_timer_config.eInputClock = AM_HAL_TIMER_CLOCK_XT;
      am_hal_timer_interrupt_enable(AM_HAL_TIMER_MASK(RADIO_WAKEUP_TIMER_NUMBER, AM_HAL_TIMER_COMPARE0));
      NVIC_SetPriority(TIMER0_IRQn + RADIO_WAKEUP_TIMER_NUMBER, NVIC_configKERNEL_INTERRUPT_PRIORITY - 1);
      NVIC_EnableIRQ(TIMER0_IRQn + RADIO_WAKEUP_TIMER_NUMBER);
      print("INFO: Searching for an existing network\n");
      begin_schedule_phase();
   }

   // Notify the application that network connectivity has been established
   app_notify(APP_NOTIFY_NETWORK_CONNECTED);

   // Loop forever waiting for actions to wake us up
   uint32_t pending_actions = 0;
   const TickType_t wait_ticks = pdMS_TO_TICKS(RANGING_ROUND_STALL_TIMEOUT_MS);
   while (is_running)
   {
      system_watchdog_pet(WATCHDOG_TASK_RANGING);
      if (xTaskNotifyWait(pdFALSE, 0xffffffff, &pending_actions, wait_ticks) == pdTRUE)
      {
         // Handle any pending actions
         if ((pending_actions & RANGING_NEW_ROUND_START))
         {
            // Wake up the radio and wait until all schedule updating tasks have completed
            ranging_radio_wakeup();
            begin_schedule_phase();
         }
         else if ((pending_actions & RANGING_STOP))
            continue;

         // Carry out logic based on the current reported phase of the ranging protocol
         switch (ranging_phase)
         {
            case RANGE_COMPUTATION_PHASE:
               search_started_stimer = am_hal_stimer_counter_get();
               if (ranging_phase_was_scheduled() && (current_role == ROLE_IDLE))
               {
                  // Notify the application that our network role has changed
                  current_role = ROLE_PARTICIPANT;
                  app_notify(APP_NOTIFY_VERIFY_CONFIGURATION);
               }
               handle_range_computation_phase();
               break;
            case RADIO_ERROR:
               if (current_role == ROLE_MASTER)
                  idle_until_next_round();
               else
                  begin_schedule_phase();
               break;
            case RANGING_ERROR:
               if (current_role == ROLE_MASTER)
                  idle_until_next_round();
               else if ((am_hal_stimer_counter_get() - search_started_stimer) >= NETWORK_SEARCH_TIMEOUT_STIMER)
               {
                     // Stop the ranging task if no network was detected after a period of time
                     print("WARNING: Timed out searching for an existing network\n");
#ifndef _TEST_RANGING_TASK
                     is_running = false;
#else
                     begin_schedule_phase();
                     search_started_stimer = am_hal_stimer_counter_get();
#endif
               }
               else
                  begin_schedule_phase();
               break;
            case MESSAGE_COLLISION:
               print("WARNING: Stopping ranging due to possible network collision\n");
#ifndef _TEST_RANGING_TASK
               is_running = false;
#else
               begin_schedule_phase();
               search_started_stimer = am_hal_stimer_counter_get();
#endif
               break;
            default:
               break;
         }
      }
      else if ((current_role != ROLE_MASTER) && ((am_hal_stimer_counter_get() - last_round_stimer) > RANGING_ROUND_STALL_STIMER))
      {
         // A participant's rounds are restarted only by its own wake-up timer
         print("WARNING: No ranging round completed in %u ms...restarting the Schedule Phase\n", (uint32_t)RANGING_ROUND_STALL_TIMEOUT_MS);
         last_round_stimer = am_hal_stimer_counter_get();
         ranging_radio_wakeup();
         begin_schedule_phase();
      }
   }

   // Disable all ranging timers and interrupts
   const am_hal_rtc_time_t scheduler_interval = {
      .ui32ReadError = 0, .ui32Weekday = 0, .ui32CenturyBit = RTC_CTRUP_CB_2000, .ui32Year = 0,
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
   app_notify(APP_NOTIFY_NETWORK_LOST);
}

void scheduler_stop(void)
{
   // Notify the scheduling task that it is time to stop
   is_running = false;
   xTaskNotify(notification_handle, RANGING_STOP, eSetBits);
}
