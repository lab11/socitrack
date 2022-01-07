#include "board.h"
#include "delay.h"
#include "dw1000.h"
#include "led.h"
#include "SEGGER_RTT.h"
#include "SEGGER_RTT_Conf.h"
#include "stm32f0xx_dbgmcu.h"
#include "stm32f0xx_gpio.h"
#include "stm32f0xx_iwdg.h"
#include "stm32f0xx_pwr.h"
#include "stm32f0xx_rcc.h"
#include "stm32f0xx_tim.h"
#include "stm32f0xx_usart.h"
#include "stm32f0xx_wwdg.h"
#include "system.h"

// Local state variables -----------------------------------------------------------------------------------------------

static EXTI_InitTypeDef _EXTI_InitStructure = { 0 };
static NVIC_InitTypeDef _NVIC_InitStructure = { 0 };

// Private helper functions --------------------------------------------------------------------------------------------

static uint8_t init_dw1000(void)
{
   // Loop forever trying to initialize the DW1000
   uint8_t err = dw1000_init();
   for (uint8_t tries = 1, resets = 0; err != DW1000_NO_ERR; ++tries)
   {
      // DW1000 completely unreachable, force reset whole device using watchdog
      if (resets >= DW1000_NUM_RESETS_BEFORE_DEVICE_RESET)
         return 1;

      // Assume that DW1000 is sleeping and try to force awake
      if (tries > (DW1000_NUM_CONTACT_TRIES_BEFORE_RESET / 2))
         dw1000_force_wakeup();

      // No DW1000 response, attempt a hard reset
      if (tries == DW1000_NUM_CONTACT_TRIES_BEFORE_RESET)
      {
         debug_msg("FATAL: DW1000 is not responding...forcing a reset!\n");
         dw1000_reset_hard(TRUE);
         tries = 0;
         ++resets;
      }

      // Give DW1000 10ms to wake up then retry initialization
      mDelay(10);
      err = dw1000_init();
   }
   return 0;
}

static void disable_all_gpios(void)
{
   // Enable all GPIO clocks
   RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOA | RCC_AHBPeriph_GPIOB | RCC_AHBPeriph_GPIOC | RCC_AHBPeriph_GPIOD | RCC_AHBPeriph_GPIOE | RCC_AHBPeriph_GPIOF, ENABLE);

   // Set all GPIOs to ANALOG INPUT
   GPIO_InitTypeDef allGPIOs;
   GPIO_StructInit(&allGPIOs);
   allGPIOs.GPIO_Mode = GPIO_Mode_AN;
   allGPIOs.GPIO_Speed = GPIO_Speed_Level_1;

   // Push configuration to all GPIOs
   GPIO_Init(GPIOB, &allGPIOs);
   GPIO_Init(GPIOC, &allGPIOs);
   GPIO_Init(GPIOD, &allGPIOs);
   GPIO_Init(GPIOE, &allGPIOs);
   GPIO_Init(GPIOF, &allGPIOs);
   allGPIOs.GPIO_Pin &= ~(STM_SWDIO_PIN | STM_SWDCLK_PIN);
   GPIO_Init(GPIOA, &allGPIOs);

   // Disable all GPIO clocks (except GPIOA which contains debugging and wakeup pins)
   RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOB | RCC_AHBPeriph_GPIOC | RCC_AHBPeriph_GPIOD | RCC_AHBPeriph_GPIOE | RCC_AHBPeriph_GPIOF, DISABLE);
}

// Public functions ----------------------------------------------------------------------------------------------------

uint8_t hw_init(void)
{
   // Disable all GPIO pins, and enable PWR, Watchdog, and TIM14 clocks
   disable_all_gpios();
   RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR | RCC_APB1Periph_TIM14 | RCC_APB1Periph_WWDG, ENABLE);
   if (RCC_GetFlagStatus(RCC_FLAG_WWDGRST) || RCC_GetFlagStatus(RCC_FLAG_IWDGRST) || RCC_GetFlagStatus(RCC_FLAG_SFTRST))
      RCC_ClearFlag();

   // Initialize LEDs
   GPIO_InitTypeDef GPIO_InitStructure_B;
   RCC_AHBPeriphClockCmd(STM_GPIO3_CLK, ENABLE);
   GPIO_InitStructure_B.GPIO_Pin = STM_GPIO3_PIN | STM_LED_RED_PIN | STM_LED_BLUE_PIN | STM_LED_GREEN_PIN;
   GPIO_InitStructure_B.GPIO_Mode = GPIO_Mode_OUT;
   GPIO_InitStructure_B.GPIO_OType = GPIO_OType_PP;
   GPIO_InitStructure_B.GPIO_Speed = GPIO_Speed_2MHz;
   GPIO_InitStructure_B.GPIO_PuPd = GPIO_PuPd_NOPULL;
   GPIO_Init(STM_GPIO3_PORT, &GPIO_InitStructure_B);

   // Signal initialization by turning on the RED LED
   led_on(RED);

    // Initialize SEGGER RTT debugging output
#ifdef DEBUG_OUTPUT_RTT
   SEGGER_RTT_Init();
   debug_msg("\n----------------------------------------------\n");
   debug_msg("INFO: Initialized RTT...\n");
#endif

   // Set up the Independent Watchdog Timer
   IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
   IWDG_SetPrescaler(IWDG_Prescaler_256); // set clock to (40kHz/256) = 156.25Hz = 6.4ms
   IWDG_SetReload(313); // reload every 2 seconds
   IWDG_ReloadCounter();

   // Set up and enable the Windowed Watchdog Timer
   NVIC_InitTypeDef nvic_init = { WWDG_IRQn, 0x00, ENABLE };
   NVIC_Init(&nvic_init);
   WWDG_SetPrescaler(WWDG_Prescaler_8); // set clock to (48MHz/4096)/8 = 1464.84375Hz = 682.666us
   WWDG_SetWindowValue(0x7F);
   WWDG_Enable(0x7F);
   WWDG_ClearFlag();
   WWDG_EnableIT();

   // Initialize External Interrupt Pin variables
   _EXTI_InitStructure.EXTI_Line = EXT_INTERRUPT_EXTI_LINE;
   _EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
   _EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;
   _NVIC_InitStructure.NVIC_IRQChannel = EXT_INTERRUPT_EXTI_IRQn;
   _NVIC_InitStructure.NVIC_IRQChannelPriority = 0x00;

   // Set up the DW1000 radio
   debug_msg("INFO: Configuring the DW1000...\n");
   return init_dw1000();
}

void hw_stop_chip(void)
{
   // Indicate that the chip is disabled by turning off the LED
   debug_msg("INFO: Disabling chip until there is something to do...\n");
   led_off();

   // Force the DW1000 radio into DEEP SLEEP mode
   dw1000_force_deepsleep();

   // Set the External Interrupt Pin to be an interruptible input
   RCC_APB2PeriphClockCmd(RCC_APB2Periph_SYSCFG, ENABLE);
   SYSCFG_EXTILineConfig(EXT_INTERRUPT_EXTI_PORT, EXT_INTERRUPT_EXTI_PIN);
   _NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
   _EXTI_InitStructure.EXTI_LineCmd = ENABLE;
   EXTI_Init(&_EXTI_InitStructure);
   NVIC_Init(&_NVIC_InitStructure);

   // Put the chip into STOP or STANDBY mode
#if EXT_WAKEUP_PIN_ENABLED
   PWR->CR |= PWR_CR_CWUF;
   PWR_WakeUpPinCmd(PWR_WakeUpPin_1, ENABLE);
   PWR_EnterSTANDBYMode();
#else
   PWR_EnterSTOPMode(PWR_Regulator_LowPower, PWR_STOPEntry_WFI);
#endif
}

bool hw_restart_chip(void)
{
   // Indicate that the chip is re-enabled by turning on the LED
   debug_msg("INFO: Chip has been re-enabled!\n");
   led_on(BLUE);

   // Re-initalize the system clocks and PLL
   SystemInit();

   // Reset the External Interrupt Pin to be a simple input
   RCC_APB2PeriphClockCmd(RCC_APB2Periph_SYSCFG, ENABLE);
   _NVIC_InitStructure.NVIC_IRQChannelCmd = DISABLE;
   _EXTI_InitStructure.EXTI_LineCmd = DISABLE;
   NVIC_Init(&_NVIC_InitStructure);
   EXTI_Init(&_EXTI_InitStructure);

   // Wake up the DW1000 radio from DEEP SLEEP mode
   dw1000_reset_hard(TRUE);
   return (init_dw1000() == DW1000_NO_ERR);
}
