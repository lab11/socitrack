#include <string.h>

#include "stm32f0xx.h"
#include "stm32f0xx_tim.h"
#include "stm32f0xx_rcc.h"
#include "stm32f0xx_misc.h"

#include "configuration.h"
#include "timer.h"

static uint8_t timer_in_use[TIMER_NUMBER] = { 0 };
static timer_callback timer_callbacks[TIMER_NUMBER];

// Predefine all of the timer structures
stm_timer_t timers[TIMER_NUMBER] = {
   {
      0,                        // Index
      TIM14,
      {                         // NVIC Init
         TIM14_IRQn,            // Channel
         0x01,                  // Priority
         DISABLE                // Enable or disable
      },
      {                         // TIM init
         0,                     // Prescalar
         TIM_CounterMode_Up,    // Counter Mode
         0,                     // Period
         TIM_CKD_DIV1,          // ClockDivision
         0                      // Repetition Counter
      },
      RCC_APB1Periph_TIM14,
      1
   },
   {
      1,                        // Index
      TIM15,
      {                         // NVIC Init
         TIM15_IRQn,            // Channel
         0x01,                  // Priority
         DISABLE                // Enable or disable
      },
      {                         // TIM init
         0,                     // Prescalar
         TIM_CounterMode_Up,    // Counter Mode
         0,                     // Period
         TIM_CKD_DIV1,          // ClockDivision
         0                      // Repetition Counter
      },
      RCC_APB2Periph_TIM15,
      1
   },
   {
      2,                        // Index
      TIM17,
      {                         // NVIC Init
         TIM17_IRQn,            // Channel
         0x01,                  // Priority
         ENABLE                 // Enable or disable
      },
      {                         // TIM init
         0,                     // Prescalar
         TIM_CounterMode_Up,    // Counter Mode
         0,                     // Period
         TIM_CKD_DIV1,          // ClockDivision
         0                      // Repetition Counter
      },
      RCC_APB2Periph_TIM17,
      1
   },
   {
      3,                        // index
      TIM16,
      {                         // NVIC Init
         TIM16_IRQn,            // Channel
         0x01,                  // Priority
         ENABLE                 // Enable or disable
      },
      {                         // TIM init
         0,                     // Prescalar
         TIM_CounterMode_Up,    // Counter Mode
         0,                     // Period
         TIM_CKD_DIV1,          // ClockDivision
         0                      // Repetition Counter
      },
      RCC_APB2Periph_TIM16,
      1
   }
};

/******************************************************************************/
// API Functions
/******************************************************************************/

// Give the caller a pointer to a valid timer configuration struct.
stm_timer_t* timer_init(uint8_t index)
{
   // Return the specified timer if not already in use
   if ((index < TIMER_NUMBER) && !timer_in_use[index])
   {
      timer_in_use[index] = 1;
      return &timers[index];
   }
   return NULL;
}

// Start a particular timer running
void timer_start(stm_timer_t *t, uint32_t us_period, timer_callback cb, bool callback_immediate)
{
#if (BOARD_V == 1)
   uint32_t prescalar = (SystemCoreClock / 500000) - 1;
#else
   uint32_t prescalar = (SystemCoreClock / 1000000) - 1;
#endif
   // Save the callback
   timer_callbacks[t->index] = cb;

   // Enable the clock and interrupts
   if (t->index == DELAY_TIMER)
   {
      RCC_APB1PeriphClockCmd(t->timer_clock, ENABLE);
      t->nvic_init.NVIC_IRQChannelCmd = DISABLE;
   }
   else
   {
      RCC_APB2PeriphClockCmd(t->timer_clock, ENABLE);
      t->nvic_init.NVIC_IRQChannelCmd = ENABLE;
   }
   NVIC_Init(&t->nvic_init);

   // Need this to fit in 16 bits
   t->divider = 1;
   while (us_period > UINT16_MAX)
   {
      us_period = us_period >> 1;
      prescalar = prescalar << 1;
      t->divider = t->divider << 1;
   }

   // Setup the actual timer
   t->tim_init.TIM_Period = us_period - 1;
   t->tim_init.TIM_Prescaler = prescalar;
   TIM_TimeBaseInit(t->tim_ptr, &t->tim_init);
   TIM_SetCounter(t->tim_ptr, 0);

   // Enable the interrupt
   if (t->index != DELAY_TIMER)
   {
      TIM_ITConfig(t->tim_ptr, TIM_IT_Update, ENABLE);
      if (callback_immediate)
         t->tim_ptr->SR = (uint16_t)TIM_IT_Update;
      else
         TIM_ClearITPendingBit(t->tim_ptr, TIM_IT_Update);
   }

   // Enable the timer
   TIM_Cmd(t->tim_ptr, ENABLE);
}

void timer_disable_interrupt(stm_timer_t *t)
{
   TIM_ITConfig(t->tim_ptr, TIM_IT_Update, DISABLE);
}

void timer_enable_interrupt(stm_timer_t *t)
{
   TIM_ITConfig(t->tim_ptr, TIM_IT_Update, ENABLE);
}

void timer_reset(stm_timer_t *t, uint32_t val_us)
{
   TIM_SetCounter(t->tim_ptr, val_us / t->divider);

   // Clear the interrupt pending bit for good measure
   TIM_ClearITPendingBit(t->tim_ptr, TIM_IT_Update);
}

uint32_t timer_value_us(stm_timer_t *t)
{
   return TIM_GetCounter(t->tim_ptr) * t->divider;
}

// Disable everything that timer_start enabled
void timer_stop(stm_timer_t *t)
{
   // Disable the timer
   TIM_Cmd(t->tim_ptr, DISABLE);

   // Disable the interrupt
   TIM_ITConfig(t->tim_ptr, TIM_IT_Update, DISABLE);

   // Disable the interrupt channel
   t->nvic_init.NVIC_IRQChannelCmd = DISABLE;
   NVIC_Init(&t->nvic_init);

   // Disable the clock
   if (t->index == DELAY_TIMER)
      RCC_APB1PeriphClockCmd(t->timer_clock, DISABLE);
   else
      RCC_APB2PeriphClockCmd(t->timer_clock, DISABLE);

   // Remove the callback
   timer_callbacks[t->index] = NULL;
}

// Make timer available again for call to timer_init
void timer_free(stm_timer_t *t)
{
   for (int i = 0; i < TIMER_NUMBER; ++i)
      if ((t == &timers[i]) && timer_in_use[i])
         timer_in_use[i] = 0;
}

// Test whether stored timer pointer is valid
bool timer_is_valid(stm_timer_t *t)
{
   for (int i = 0; i < TIMER_NUMBER; ++i)
      if (t == &timers[i])
         return TRUE;
   return FALSE;
}

/******************************************************************************/
// Interrupt handling
/******************************************************************************/

// Call the timer callback from main thread context
void scheduler_timer_fired(void)
{
   if (timer_callbacks[SCHEDULER_TIMER])
      timer_callbacks[SCHEDULER_TIMER]();
}

// Call the timer callback from main thread context
void ranging_timer_fired(void)
{
   if (timer_callbacks[RANGING_TIMER])
      timer_callbacks[RANGING_TIMER]();
}

// Raw interrupt handlers from vector table
void TIM17_IRQHandler(void)
{
   if (TIM_GetITStatus(TIM17, TIM_IT_Update) != RESET)
   {
      // Handle scheduling interrupts within the ISR
      if (timer_callbacks[SCHEDULER_TIMER])
         timer_callbacks[SCHEDULER_TIMER]();

      // Clear Timer interrupt pending bit
      TIM_ClearITPendingBit(TIM17, TIM_IT_Update);
   }
}

void TIM16_IRQHandler(void)
{
   if (TIM_GetITStatus(TIM16, TIM_IT_Update) != RESET)
   {
      // Notify main loop that we got a ranging timer interrupt
      mark_interrupt(INTERRUPT_RANGING);

      // Clear Timer interrupt pending bit
      TIM_ClearITPendingBit(TIM16, TIM_IT_Update);
   }
}
