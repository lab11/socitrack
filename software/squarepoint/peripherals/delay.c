#include "delay.h"
#include "stddef.h"
#include "timer.h"

static stm_timer_t *_delay_timer = NULL;

void uDelay(uint32_t u)
{
   if (!_delay_timer)
      _delay_timer = timer_init(DELAY_TIMER);
   timer_start(_delay_timer, UINT16_MAX, NULL, FALSE);
   volatile uint32_t start = _delay_timer->tim_ptr->CNT;
   while ((_delay_timer->tim_ptr->CNT - start) <= u);
   timer_stop(_delay_timer);
}

void mDelay(uint32_t m)
{
   if (!_delay_timer)
      _delay_timer = timer_init(DELAY_TIMER);
   uint32_t u = m * 1000;
   timer_start(_delay_timer, UINT16_MAX, NULL, FALSE);
   volatile uint32_t start = _delay_timer->tim_ptr->CNT;
   while ((_delay_timer->tim_ptr->CNT - start) <= u);
   timer_stop(_delay_timer);
}
