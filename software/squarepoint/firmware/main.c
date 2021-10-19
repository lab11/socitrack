// Includes ------------------------------------------------------------------------------------------------------------

#include "atomic.h"
#include "board.h"
#include "calibration.h"
#include "configuration.h"
#include "delay.h"
#include "host_interface.h"
#include "led.h"
#include "scheduler.h"
#include "SEGGER_RTT.h"
#include "stm32f0xx_pwr.h"
#include "timer.h"

// OS state ------------------------------------------------------------------------------------------------------------

static volatile bool _running_successfully = TRUE, _force_reset = FALSE;
static volatile uint32_t _interrupts_awaiting_service[NUM_INTERRUPT_SOURCES] = { FALSE }, _watchdog_counter = 0;
static volatile app_status_t _app_status = APP_NOT_INITIALIZED;
static volatile app_mode_t _app_mode = APP_UNSPECIFIED;

void WWDG_IRQHandler(void)
{
   // Watchdog interrupt, reset the running success flag
   if (_running_successfully)
   {
      _running_successfully = FALSE;
      _watchdog_counter = 0;
   }

   // Reset the watchdog timer if the app is stopped or less than 2 seconds have passed
   if (!_force_reset && ((++_watchdog_counter < 50) || (_app_status == APP_STOPPED)))
   {
      WWDG_SetCounter(0x7F);
      WWDG_ClearFlag();
      IWDG_ReloadCounter();
   }
}

// Application and OS functions ----------------------------------------------------------------------------------------

// Configure this module as the correct application
bool module_configure(app_mode_t app_mode, void *config)
{
   // Do not reconfigure if nothing has changed
   if ((_app_status == APP_RUNNING) && (app_mode == _app_mode) && (_app_mode == APP_STANDARD) && scheduler_verify_config((app_config_t*)config))
      return TRUE;

   // Check if this application is already running
   bool resume = FALSE, success = FALSE;
   if (_app_status == APP_RUNNING)
   {
      resume = TRUE;
      module_stop();
   }

   // Configure the correct corresponding application
   _app_mode = app_mode;
   switch (_app_mode)
   {
      case APP_STANDARD:
         success = scheduler_configure((app_config_t*)config);
         break;
      case APP_CALIBRATION:
         success = calibration_configure((calibration_config_t*)config);
         break;
      default:
         break;
   }

   // Restart application if already previously running
   if (resume && success)
      success = module_start();
   return success;
}

// Start the application
bool module_start(void)
{
   // Do not start if already running
   if (_app_status == APP_RUNNING)
      return TRUE;

   // Start the correct corresponding application
   bool success = FALSE;
   switch (_app_mode)
   {
      case APP_STANDARD:
         success = scheduler_start();
         break;
      case APP_CALIBRATION:
         success = calibration_start();
         break;
      default:
         break;
   }

   // Return whether the application started successfully
   if (success)
   {
      // Enable the Independent Watchdog Timer
      _app_status = APP_RUNNING;
      IWDG_Enable();
   }
   return success;
}

// Stop the application
void module_stop(void)
{
   // Do not stop if not already running
   if (_app_status != APP_RUNNING)
      return;
   _app_status = APP_STOPPED;

   // Stop the correct corresponding application
   switch (_app_mode)
   {
      case APP_STANDARD:
         scheduler_stop();
         break;
      case APP_CALIBRATION:
         calibration_stop();
         break;
      default:
         break;
   }
}

void module_reset(void) { _force_reset = TRUE; }
void module_set_stopped(void) { _app_status = APP_STOPPED; }
bool module_ready(void) { return (_app_status != APP_NOT_INITIALIZED); }
void reset_watchdog(void) { _running_successfully = TRUE; }
void mark_interrupt(interrupt_source_t src) { atomic_set(_interrupts_awaiting_service + src); }

int main(void)
{
   // Initialize all hardware
   _force_reset = hw_init();

   // Initialize the application
   _app_status = APP_INITIALIZING;
#ifdef CALIBRATION
   debug_msg("INFO: Calibration mode active!\n");
   calibration_config config = { 1 };
   module_configure(APP_CALIBRATION, &config);
#elif BYPASS_HOST_INTERFACE
   app_config_t config;
   config.device_role = HYBRID;
   config.scheduler_role = PARTICIPANT;
   config.hybrids_perform_all_rangings = HYBRIDS_PERFORM_ALL_RANGINGS;
   config.radio_sleep_while_passive = STM_RADIO_SLEEP_WHILE_PASSIVE;
   config.radio_wakeup_from_host = ALLOW_HOST_TO_WAKEUP_RADIO;
   config.startup_timestamp = 0;
   module_configure(APP_STANDARD, &config);
#endif // CALIBRATION

   // Initialize the I2C host controller communications interface
#ifndef BYPASS_HOST_INTERFACE
   debug_msg("INFO: Connecting to the host interface...\n");
   _force_reset = _force_reset || host_interface_init();
   if (!_force_reset)
   {
      reset_watchdog();
      host_interface_notify_stopped();
   }
   else
      debug_msg("ERROR: Unable to connect to the host interface\n");
#else
   debug_msg("INFO: Bypassing the host interface!\n");
   module_start();
#endif // BYPASS_HOST_INTERFACE

   // Signal normal operation by turning LED BLUE
   led_off();
   led_on(BLUE);

   // Main loop
   bool interrupt_triggered = FALSE;
   while (TRUE)
   {
      // Ensure that we are listening for incoming data from the host
      host_interface_wait();

      // Sleep until an interrupt is signaled
      PWR_EnterSleepMode(PWR_SLEEPEntry_WFI);

      // Fire the appropriate callbacks based on any outstanding interrupts to be serviced
      do
      {
         interrupt_triggered = FALSE;
         if (atomic_read_clear(_interrupts_awaiting_service + INTERRUPT_DW1000))             // DW1000 Timer
         {
            interrupt_triggered = TRUE;
            dw1000_interrupt_fired();
         }
         if (atomic_read_clear(_interrupts_awaiting_service + INTERRUPT_RANGING))            // Ranging Timer
         {
            interrupt_triggered = TRUE;
            ranging_timer_fired();
         }
         if (atomic_read_clear(_interrupts_awaiting_service + INTERRUPT_I2C_RX))             // I2C Data RX
         {
            interrupt_triggered = TRUE;
            host_interface_rx_fired();
         }
         if (atomic_read_clear(_interrupts_awaiting_service + INTERRUPT_I2C_ERROR))          // I2C Data Timeout
         {
            interrupt_triggered = TRUE;
            host_interface_error_occurred();
         }
      } while (interrupt_triggered);

      // Put the board into STOP mode to be awoken when a network is in range
      if (_watchdog_counter == 100)
      {
         hw_stop_chip();
         _watchdog_counter = 0;
         _app_status = APP_INITIALIZING;
         _force_reset = !hw_restart_chip();
      }
   }

   return 0;
}
