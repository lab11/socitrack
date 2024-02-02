// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "battery.h"
#include "bluetooth.h"
#include "button.h"
#include "buzzer.h"
#include "imu.h"
#include "led.h"
#include "logging.h"
#include "ranging.h"
#include "rtc.h"
#include "scheduler.h"
#include "storage.h"
#include "system.h"


// Static Global Variables ---------------------------------------------------------------------------------------------

extern uint8_t _uid_base_address;


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
   static uint32_t status;
   AM_CRITICAL_BEGIN
   am_hal_gpio_interrupt_irq_status_get(GPIO0_001F_IRQn, false, &status);
   am_hal_gpio_interrupt_irq_clear(GPIO0_001F_IRQn, status);
   AM_CRITICAL_END
   am_hal_gpio_interrupt_service(GPIO0_001F_IRQn, status);
}

void am_gpio0_203f_isr(void)
{
   static uint32_t status;
   AM_CRITICAL_BEGIN
   am_hal_gpio_interrupt_irq_status_get(GPIO0_203F_IRQn, false, &status);
   am_hal_gpio_interrupt_irq_clear(GPIO0_203F_IRQn, status);
   AM_CRITICAL_END
   am_hal_gpio_interrupt_service(GPIO0_203F_IRQn, status);
}

void am_gpio0_405f_isr(void)
{
   static uint32_t status;
   AM_CRITICAL_BEGIN
   am_hal_gpio_interrupt_irq_status_get(GPIO0_405F_IRQn, false, &status);
   am_hal_gpio_interrupt_irq_clear(GPIO0_405F_IRQn, status);
   AM_CRITICAL_END
   am_hal_gpio_interrupt_service(GPIO0_405F_IRQn, status);
}

void am_gpio0_607f_isr(void)
{
   static uint32_t status;
   AM_CRITICAL_BEGIN
   am_hal_gpio_interrupt_irq_status_get(GPIO0_607F_IRQn, false, &status);
   am_hal_gpio_interrupt_irq_clear(GPIO0_607F_IRQn, status);
   AM_CRITICAL_END
   am_hal_gpio_interrupt_service(GPIO0_607F_IRQn, status);
}

void am_rtc_isr(void)
{
   static am_hal_rtc_alarm_repeat_e repeat_interval;
   AM_CRITICAL_BEGIN
   am_hal_rtc_alarm_get(NULL, &repeat_interval);
   am_hal_rtc_interrupt_clear(AM_HAL_RTC_INT_ALM);
   AM_CRITICAL_END
   if (repeat_interval == AM_HAL_RTC_ALM_RPT_SEC)
      scheduler_rtc_isr();
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

__attribute__((optimize("O0")))
void system_hard_fault_handler(sContextStateFrame *frame)
{
#ifdef DEBUGGING
   do {
      if (CoreDebug->DHCSR & (1 << 0))
         __asm("bkpt 1");
   } while (0);
#else
   NVIC_SystemReset();
   while (true) {}
#endif
}

void HardFault_Handler(void) { HARDFAULT_HANDLING_ASM(); }


// FreeRTOS Debugging Functions ----------------------------------------------------------------------------------------

void vApplicationMallocFailedHook(void)
{
   while (1)
      __asm("BKPT #0\n");
}

void vApplicationStackOverflowHook(TaskHandle_t pxTask, char *pcTaskName)
{
   while (1)
      __asm("BKPT #0\n");
}

void vAssertCalled(const char * const pcFileName, unsigned long ulLine)
{
   volatile uint32_t ulSetToNonZeroInDebuggerToContinue = 0;
   taskENTER_CRITICAL();
   {
      // You can step out of this function to debug the assertion by using
      // the debugger to set ulSetToNonZeroInDebuggerToContinue to a non-zero value.
      while (ulSetToNonZeroInDebuggerToContinue == 0)
         portNOP();
   }
   taskEXIT_CRITICAL();
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
   // Read the hardware reset reason
   am_hal_reset_status_t reset_reason;
   am_hal_reset_status_get(&reset_reason);

   // Enable the floating point module
   am_hal_sysctrl_fpu_enable();
   am_hal_sysctrl_fpu_stacking_enable(true);

   // Configure the board to operate in low-power mode
   am_hal_pwrctrl_low_power_init();
   am_hal_pwrctrl_control(AM_HAL_PWRCTRL_CONTROL_SIMOBUCK_INIT, NULL);

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
   if (!immediate)
   {
      storage_flush_and_shutdown();
      vTaskDelay(portMAX_DELAY);
   }
   else
#endif
   am_hal_reset_control(AM_HAL_RESET_CONTROL_SWPOR, NULL);
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
   // Turn off all peripherals
   print("WARNING: Powering off...\n");
   battery_monitor_deinit();
   bluetooth_deinit();
   buttons_deinit();
   buzzer_deinit();
   imu_deinit();
   leds_deinit();
   ranging_radio_deinit();
   storage_deinit();
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
