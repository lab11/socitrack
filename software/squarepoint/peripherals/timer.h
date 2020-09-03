#ifndef __TIMER_H
#define __TIMER_H

#include "stm32f0xx.h"
#include "system.h"

// Number of supported timers
#define TIMER_NUMBER 4
#define DELAY_TIMER 0
#define I2C_TIMER 1
#define SCHEDULER_TIMER 2
#define RANGING_TIMER 3

typedef struct
{
   uint8_t index;
   TIM_TypeDef *tim_ptr;
   NVIC_InitTypeDef nvic_init;
   TIM_TimeBaseInitTypeDef tim_init;
   uint32_t timer_clock;
   uint32_t divider;
} stm_timer_t;

typedef void (*timer_callback)();

// NOTE: These timers are peculiar in that they fire
// immediately then at the periodic interval.

stm_timer_t* timer_init(uint8_t index);
void timer_disable_interrupt(stm_timer_t *t);
void timer_enable_interrupt(stm_timer_t *t);
void timer_start(stm_timer_t *t, uint32_t us_period, timer_callback cb, bool callback_immediate);
void timer_reset(stm_timer_t *t, uint32_t val_us);
uint32_t timer_value_us(stm_timer_t *t);
void timer_stop(stm_timer_t *t);
void timer_free(stm_timer_t *t);
bool timer_is_valid(stm_timer_t *t);

// Only used for interrupt handling
void scheduler_timer_fired();
void ranging_timer_fired();

#endif
