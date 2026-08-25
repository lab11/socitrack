// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "battery.h"
#include "bluetooth.h"
#include "button.h"
#include "buzzer.h"
#include "imu.h"
#include "led.h"
#include "logging.h"
#include "nandlog.h"
#include "ranging.h"
#include "rtc.h"
#include "scheduler.h"
#include "system.h"


// Static Global Variables ---------------------------------------------------------------------------------------------

#define DIAGNOSTIC_SCRATCH_MAGIC                    0x5D1A0000
#define DIAGNOSTIC_SCRATCH_MAGIC_MASK               0xFFFF0000

#define WATCHDOG_STIMER_HZ                          32768u
#define WATCHDOG_MS_TO_STIMER(ms)                   ((((uint32_t)(ms) / 1000u) * WATCHDOG_STIMER_HZ) + ((((uint32_t)(ms) % 1000u) * WATCHDOG_STIMER_HZ) / 1000u))

#define WATCHDOG_INTERRUPT_PERIOD_MS                (WATCHDOG_INTERRUPT_TICKS * WATCHDOG_TICK_S * 1000)

extern uint8_t _uid_base_address;
static volatile uint32_t watchdog_last_checkin[WATCHDOG_NUM_TASKS];
static volatile bool watchdog_registered[WATCHDOG_NUM_TASKS];
static volatile bool watchdog_enabled = false;
static volatile uint32_t watchdog_armed_at;
static uint16_t boot_reset_status = 0;

__attribute__((unused))
static const char *const watchdog_task_names[WATCHDOG_NUM_TASKS] = { "TimeAlignedTask", "StorageTask", "AppTask", "BLETask", "RangingTask" };


// Private Helper Functions --------------------------------------------------------------------------------------------

static inline uint32_t watchdog_now(void)
{
   // A tick-independent clock that wraps every ~36 hours
   return am_hal_stimer_counter_get();
}

static reset_diagnostic_t watchdog_find_stalled_tasks(void)
{
   // Report which registered tasks have gone quiet for longer than they are allowed to
   uint32_t num_stalled = 0;
   reset_diagnostic_t first_stalled = RESET_DIAGNOSTIC_NONE;
   const uint32_t now = watchdog_now(), deadline = WATCHDOG_MS_TO_STIMER(WATCHDOG_CHECKIN_DEADLINE_MS);
   for (uint32_t task = 0; task < WATCHDOG_NUM_TASKS; ++task)
      if (watchdog_registered[task] && ((now - watchdog_last_checkin[task]) > deadline))
      {
         if (!num_stalled)
            first_stalled = (reset_diagnostic_t)(RESET_DIAGNOSTIC_STALL_TIME_ALIGNED + task);
         ++num_stalled;
      }
   return (num_stalled > 1) ? RESET_DIAGNOSTIC_STALL_MULTIPLE : first_stalled;
}

static void watchdog_evaluate_and_pet(void)
{
   // Pet the watchdog from whichever task last checked in
   if ((watchdog_now() - watchdog_armed_at) < WATCHDOG_MS_TO_STIMER(WATCHDOG_STARTUP_GRACE_MS))
   {
      am_hal_wdt_restart(AM_HAL_WDT_MCU);        // still starting up; nothing is expected of anyone yet
      return;
   }

   // Only pet if all registered tasks have checked in within their allowed time
   const reset_diagnostic_t stalled = watchdog_find_stalled_tasks();
   if (stalled == RESET_DIAGNOSTIC_NONE)
      am_hal_wdt_restart(AM_HAL_WDT_MCU);
   else
      system_record_diagnostic(stalled);         // decline to pet, and name the cause while we still can
}


// Ambiq Interrupt Service Routines and MCU Functions ------------------------------------------------------------------

void _close(void) {}
void _lseek(void) {}
void _read(void) {}
void _write(void) {}
void _fstat(void) {}
void _getpid(void) {}
void _isatty(void) {}
void _kill(void) {}

void am_gpio0_001f_isr(void)
{
   uint32_t status = 0;
   AM_CRITICAL_BEGIN
   am_hal_gpio_interrupt_irq_status_get(GPIO0_001F_IRQn, false, &status);
   am_hal_gpio_interrupt_irq_clear(GPIO0_001F_IRQn, status);
   AM_CRITICAL_END
   am_hal_gpio_interrupt_service(GPIO0_001F_IRQn, status);
}

void am_gpio0_203f_isr(void)
{
   uint32_t status = 0;
   AM_CRITICAL_BEGIN
   am_hal_gpio_interrupt_irq_status_get(GPIO0_203F_IRQn, false, &status);
   am_hal_gpio_interrupt_irq_clear(GPIO0_203F_IRQn, status);
   AM_CRITICAL_END
   am_hal_gpio_interrupt_service(GPIO0_203F_IRQn, status);
}

void am_gpio0_405f_isr(void)
{
   uint32_t status = 0;
   AM_CRITICAL_BEGIN
   am_hal_gpio_interrupt_irq_status_get(GPIO0_405F_IRQn, false, &status);
   am_hal_gpio_interrupt_irq_clear(GPIO0_405F_IRQn, status);
   AM_CRITICAL_END
   am_hal_gpio_interrupt_service(GPIO0_405F_IRQn, status);
}

void am_gpio0_607f_isr(void)
{
   uint32_t status = 0;
   AM_CRITICAL_BEGIN
   am_hal_gpio_interrupt_irq_status_get(GPIO0_607F_IRQn, false, &status);
   am_hal_gpio_interrupt_irq_clear(GPIO0_607F_IRQn, status);
   AM_CRITICAL_END
   am_hal_gpio_interrupt_service(GPIO0_607F_IRQn, status);
}

void am_rtc_isr(void)
{
   am_hal_rtc_alarm_repeat_e repeat_interval;
   AM_CRITICAL_BEGIN
   am_hal_rtc_alarm_get(NULL, &repeat_interval);
   am_hal_rtc_interrupt_clear(AM_HAL_RTC_INT_ALM);
   AM_CRITICAL_END
}

void am_watchdog_isr(void)
{
   // Retained deliberately even though field data says it never runs
   AM_CRITICAL_BEGIN
   am_hal_wdt_interrupt_clear(AM_HAL_WDT_MCU, AM_HAL_WDT_INTERRUPT_MCU);
   AM_CRITICAL_END
   if (watchdog_enabled)
      watchdog_evaluate_and_pet();
}

uint32_t am_freertos_sleep(uint32_t idleTime)
{
   am_hal_sysctrl_sleep(AM_HAL_SYSCTRL_SLEEP_DEEP);
   return 0;
}

void am_freertos_wakeup(uint32_t idleTime) { return; }


// Helpful Debugging Functions and Macros ------------------------------------------------------------------------------

typedef struct __attribute__((packed)) ContextStateFrame
{ uint32_t r0, r1, r2, r3, r12, lr, return_address, xpsr; } sContextStateFrame;

#define HARDFAULT_HANDLING_ASM(_x)               \
  __asm volatile(                                \
      "tst lr, #4 \n"                            \
      "ite eq \n"                                \
      "mrseq r0, msp \n"                         \
      "mrsne r0, psp \n"                         \
      "b system_hard_fault_handler \n"           )

static void system_fault_reset(reset_diagnostic_t diagnostic)
{
   system_record_diagnostic(diagnostic);
#ifdef AM_DEBUG_PRINTF
   if (CoreDebug->DHCSR & (1 << 0))
      __asm("bkpt 1");
#endif
   NVIC_SystemReset();
   while (true) {}
}

__attribute__((optimize("O0")))
void system_hard_fault_handler(sContextStateFrame *frame)
{
   system_fault_reset(RESET_DIAGNOSTIC_HARD_FAULT);
}

void HardFault_Handler(void) { HARDFAULT_HANDLING_ASM(); }


// FreeRTOS Debugging Functions ----------------------------------------------------------------------------------------

void vApplicationMallocFailedHook(void)
{
   system_fault_reset(RESET_DIAGNOSTIC_MALLOC_FAILED);
}

void vApplicationStackOverflowHook(TaskHandle_t pxTask, char *pcTaskName)
{
   print("ERROR: Stack overflow in task %s\n", pcTaskName ? pcTaskName : "<unknown>");
   system_fault_reset(RESET_DIAGNOSTIC_STACK_OVERFLOW);
}

void vAssertCalled(const char * const pcFileName, unsigned long ulLine)
{
   print("ERROR: Assertion failed at %s:%lu\n", pcFileName ? pcFileName : "<unknown>", ulLine);
   system_fault_reset(RESET_DIAGNOSTIC_ASSERT);
}

void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize)
{
   static StaticTask_t xIdleTaskTCB;
   static StackType_t uxIdleTaskStack[256];
   *ppxIdleTaskTCBBuffer = &xIdleTaskTCB;
   *ppxIdleTaskStackBuffer = uxIdleTaskStack;
   *pulIdleTaskStackSize = 256;
}

void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer, StackType_t **ppxTimerTaskStackBuffer, uint32_t *pulTimerTaskStackSize)
{
   static StaticTask_t xTimerTaskTCB;
   static StackType_t uxTimerTaskStack[configTIMER_TASK_STACK_DEPTH];
   *ppxTimerTaskTCBBuffer = &xTimerTaskTCB;
   *ppxTimerTaskStackBuffer = uxTimerTaskStack;
   *pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}


// Public API Functions ------------------------------------------------------------------------------------------------

void setup_hardware(void)
{
   // Read the hardware reset reason and add whatever the previous boot managed to record about why it rebooted
   am_hal_reset_status_t reset_reason;
   am_hal_reset_status_get(&reset_reason);
   boot_reset_status = (uint16_t)(reset_reason.eStatus & ((1u << RESET_DIAGNOSTIC_SHIFT) - 1u));
   const uint32_t scratch = MCUCTRL->SCRATCH0;
   if ((scratch & DIAGNOSTIC_SCRATCH_MAGIC_MASK) == DIAGNOSTIC_SCRATCH_MAGIC)
      boot_reset_status |= (uint16_t)((scratch & RESET_DIAGNOSTIC_MASK) << RESET_DIAGNOSTIC_SHIFT);

   // Re-arm the diagnostic breadcrumb rather than clearing it
   MCUCTRL->SCRATCH0 = DIAGNOSTIC_SCRATCH_MAGIC | RESET_DIAGNOSTIC_NOTHING_RECORDED;

   // Enable the floating point module
   am_hal_sysctrl_fpu_enable();
   am_hal_sysctrl_fpu_stacking_enable(true);

   // Configure the board to operate in low-power mode
   am_hal_pwrctrl_low_power_init();
   am_hal_pwrctrl_control(AM_HAL_PWRCTRL_CONTROL_SIMOBUCK_INIT, NULL);
#ifndef AM_DEBUG_PRINTF
   am_hal_pwrctrl_control(AM_HAL_PWRCTRL_CONTROL_CRYPTO_POWERDOWN, NULL);
#endif

   // Configure only the necessary memory
   am_hal_pwrctrl_dsp_memory_config_t dsp_mem_config =
   {
      .bEnableICache = false,
      .bRetainCache = false,
      .bEnableRAM = false,
      .bActiveRAM = false,
      .bRetainRAM = false
   };
   am_hal_pwrctrl_mcu_memory_config_t mcu_mem_config =
   {
      .eCacheCfg    = AM_HAL_PWRCTRL_CACHE_NONE,
      .bRetainCache = false,
      .eDTCMCfg     = AM_HAL_PWRCTRL_DTCM_384K,
      .eRetainDTCM  = AM_HAL_PWRCTRL_DTCM_384K,
      .bEnableNVM0  = true,
      .bRetainNVM0  = false
   };
   am_hal_pwrctrl_sram_memcfg_t sram_mem_config =
   {
      .eSRAMCfg           = AM_HAL_PWRCTRL_SRAM_NONE,
      .eActiveWithMCU     = AM_HAL_PWRCTRL_SRAM_NONE,
      .eActiveWithGFX     = AM_HAL_PWRCTRL_SRAM_NONE,
      .eActiveWithDISP    = AM_HAL_PWRCTRL_SRAM_NONE,
      .eActiveWithDSP     = AM_HAL_PWRCTRL_SRAM_NONE,
      .eSRAMRetain        = AM_HAL_PWRCTRL_SRAM_NONE
   };
   am_hal_pwrctrl_dsp_memory_config(AM_HAL_DSP0, &dsp_mem_config);
   am_hal_pwrctrl_dsp_memory_config(AM_HAL_DSP1, &dsp_mem_config);
   am_hal_pwrctrl_mcu_memory_config(&mcu_mem_config);
   am_hal_pwrctrl_sram_config(&sram_mem_config);
   am_hal_cachectrl_disable();

   // Set up printing to the console
   logging_init();
   print_reset_reason(&reset_reason);
}

void system_reset(bool immediate)
{
#ifdef __USE_FREERTOS__
   // A graceful reset hands the job to the storage task so buffered records reach flash first
   if (!immediate && storage_flush_and_shutdown())
   {
      vTaskDelay(portMAX_DELAY);
      return;
   }
#endif
   am_util_delay_ms(1000);
   am_hal_reset_control(AM_HAL_RESET_CONTROL_SWPOR, NULL);
}

uint16_t system_get_reset_reason(void)
{
   // Raw am_hal_reset_status_e bits latched at boot, plus the firmware's own diagnostic in the top 4
   return boot_reset_status;
}

void system_record_diagnostic(reset_diagnostic_t diagnostic)
{
   // A real diagnostic replaces the boot marker but never an earlier real diagnostic
   const uint32_t held = MCUCTRL->SCRATCH0;
   const bool tagged = (held & DIAGNOSTIC_SCRATCH_MAGIC_MASK) == DIAGNOSTIC_SCRATCH_MAGIC;
   if (!tagged || ((held & RESET_DIAGNOSTIC_MASK) == RESET_DIAGNOSTIC_NOTHING_RECORDED))
      MCUCTRL->SCRATCH0 = DIAGNOSTIC_SCRATCH_MAGIC | ((uint32_t)diagnostic & RESET_DIAGNOSTIC_MASK);
}

void system_watchdog_register(watchdog_task_t task)
{
   // A task is not monitored until it says it is ready to be
   if (task < WATCHDOG_NUM_TASKS)
   {
      watchdog_last_checkin[task] = watchdog_now();
      watchdog_registered[task] = true;
   }
}

void system_watchdog_enable(void)
{
   // Nothing needs to be registered yet; the startup grace period covers the gap
   am_hal_wdt_config_t watchdog_config = {
      .eClockSource      = AM_HAL_WDT_1_16HZ,
      .bInterruptEnable  = true,
      .ui32InterruptValue = WATCHDOG_INTERRUPT_TICKS,
      .bResetEnable      = true,
      .ui32ResetValue    = WATCHDOG_RESET_TICKS,
      .bAlertOnDSPReset  = false
   };
   am_hal_wdt_config(AM_HAL_WDT_MCU, &watchdog_config);
   am_hal_reset_configure(AM_HAL_RESET_WDT_RESET_ENABLE);

   // Route the pre-reset interrupt
   NVIC_SetPriority(WDT_IRQn, WATCHDOG_ISR_PRIORITY);
   NVIC_EnableIRQ(WDT_IRQn);

   // Never lock the watchdog
   watchdog_armed_at = watchdog_now();
   for (uint32_t task = 0; task < WATCHDOG_NUM_TASKS; ++task)
      watchdog_last_checkin[task] = watchdog_armed_at;
   watchdog_enabled = true;
   am_hal_wdt_start(AM_HAL_WDT_MCU, false);
   print("INFO: Watchdog armed -- WDT->CFG = 0x%08X (clksel %u, intval %u, resval %u); health checked every %u s, reset %u s after a declined pet\n",
         (uint32_t)WDT->CFG, (uint32_t)WDT->CFG_b.CLKSEL, (uint32_t)WDT->CFG_b.INTVAL, (uint32_t)WDT->CFG_b.RESVAL,
         (uint32_t)(WATCHDOG_INTERRUPT_TICKS * WATCHDOG_TICK_S), (uint32_t)((WATCHDOG_RESET_TICKS - WATCHDOG_INTERRUPT_TICKS) * WATCHDOG_TICK_S));
}

void system_watchdog_disable(void)
{
   // Idempotent, because the paths that need the watchdog off are also reachable before it was ever armed
   if (!watchdog_enabled)
      return;
   watchdog_enabled = false;
   for (uint32_t task = 0; task < WATCHDOG_NUM_TASKS; ++task)
      watchdog_registered[task] = false;
   am_hal_wdt_stop(AM_HAL_WDT_MCU);
   NVIC_DisableIRQ(WDT_IRQn);
   am_hal_wdt_interrupt_disable(AM_HAL_WDT_MCU, AM_HAL_WDT_INTERRUPT_MCU);
   am_hal_reset_configure(AM_HAL_RESET_WDT_RESET_DISABLE);
   print("WARNING: Watchdog disabled\n");
}

void system_watchdog_pet(watchdog_task_t task)
{
   // Record this task's check-in, then decide on behalf of all of them whether the dog gets pet
   if (!watchdog_enabled || (task >= WATCHDOG_NUM_TASKS))
      return;
   watchdog_last_checkin[task] = watchdog_now();
   watchdog_evaluate_and_pet();
}

void system_enable_interrupts(bool enabled)
{
   // Enable or disable all system interrupts
   if (enabled)
      am_hal_interrupt_master_enable();
   else
      am_hal_interrupt_master_disable();
}

void system_enter_power_off_mode(uint32_t wake_on_gpio, uint32_t wake_on_timestamp)
{
   // Stop the watchdog before sleeping
   system_watchdog_disable();

   // Turn off all peripherals
   print("WARNING: Powering off...\n");
   battery_monitor_deinit();
   bluetooth_deinit();
   buttons_deinit();
   buzzer_deinit();
   imu_deinit();
   leds_deinit();
   ranging_radio_deinit();
   nandlog_deinit();
   logging_disable();

   // Power down the crypto module followed by all peripherals
   am_hal_pwrctrl_control(AM_HAL_PWRCTRL_CONTROL_CRYPTO_POWERDOWN, NULL);
   am_hal_pwrctrl_control(AM_HAL_PWRCTRL_CONTROL_DIS_PERIPHS_ALL, NULL);

   // Optionally allow a change on a GPIO pin to wake up the device
   if (wake_on_gpio)
   {
      am_hal_gpio_pincfg_t input_pin_config = AM_HAL_GPIO_PINCFG_INPUT;
      input_pin_config.GP.cfg_b.ePullup = AM_HAL_GPIO_PIN_PULLUP_100K;
      input_pin_config.GP.cfg_b.eIntDir = AM_HAL_GPIO_PIN_INTDIR_HI2LO;
      uint32_t wakeup_pin = wake_on_gpio, interrupt_status;
      am_hal_gpio_pinconfig(wakeup_pin, input_pin_config);
      AM_CRITICAL_BEGIN
      am_hal_gpio_interrupt_irq_status_get(GPIO0_001F_IRQn, false, &interrupt_status);
      am_hal_gpio_interrupt_irq_clear(GPIO0_001F_IRQn, interrupt_status);
      AM_CRITICAL_END
      am_hal_gpio_interrupt_control(AM_HAL_GPIO_INT_CHANNEL_0, AM_HAL_GPIO_INT_CTRL_INDV_ENABLE, &wakeup_pin);
      NVIC_SetPriority(GPIO0_001F_IRQn + GPIO_NUM2IDX(wakeup_pin), NVIC_configKERNEL_INTERRUPT_PRIORITY);
      NVIC_EnableIRQ(GPIO0_001F_IRQn + GPIO_NUM2IDX(wakeup_pin));
   }

   // Optionally, configure the RTC to wake the device at a specific timestamp
   if (wake_on_timestamp)
      rtc_set_wakeup_timestamp(wake_on_timestamp);

   // Enable interrupts and enter Deep Sleep mode
   am_hal_interrupt_master_enable();
   am_hal_sysctrl_sleep(AM_HAL_SYSCTRL_SLEEP_DEEP);
}

void system_read_UID(uint8_t *uid, uint32_t uid_length)
{
   // Copy UID from flash memory location into the specified buffer
   uint8_t *_uid = &_uid_base_address;
   for (uint32_t i = 0; i < uid_length; ++i)
      uid[i] = _uid[i];
}
