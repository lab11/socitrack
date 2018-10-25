#include <string.h>

#include "stm32f0xx.h"
#include "stm32f0xx_tim.h"
#include "stm32f0xx_rcc.h"
#include "stm32f0xx_misc.h"

#include "timer.h"
#include "firmware.h"


static uint8_t used_timers = 0;
static uint8_t timer_in_use[TIMER_NUMBER] = { 0 };

static timer_callback timer_callbacks[TIMER_NUMBER];

// Predefine all of the timer structures
stm_timer_t timers[TIMER_NUMBER] = {
	{
		0, // index
		TIM17,
		{ // NVIC Init
			TIM17_IRQn, // Channel
			0x01,       // Priority
			ENABLE      // Enable or disable
		},
		{ // TIM init
			0,                  // Prescalar
			TIM_CounterMode_Up, // Counter Mode
			0,                  // Period
			TIM_CKD_DIV1,       // ClockDivision
			0                   // Repetition Counter
		},
		RCC_APB2Periph_TIM17,
		1
	},
	{
		1, // index
		TIM16,
		{ // NVIC Init
			TIM16_IRQn, // Channel
			0x01,       // Priority
			ENABLE      // Enable or disable
		},
		{ // TIM init
			0,                  // Prescalar
			TIM_CounterMode_Up, // Counter Mode
			0,                  // Period
			TIM_CKD_DIV1,       // ClockDivision
			0                   // Repetition Counter
		},
		RCC_APB2Periph_TIM16,
		1
	}
};

/******************************************************************************/
// API Functions
/******************************************************************************/

// Give the caller a pointer to a valid timer configuration struct.
stm_timer_t* timer_init () {
	if (used_timers >= TIMER_NUMBER) {
		return NULL;
	}

	for (int i = 0; i < TIMER_NUMBER; i++) {

	    if (timer_in_use[i] == 0) {
	        timer_in_use[i] = 1;
            used_timers++;
	        return &timers[i];
	    }
	}

	// ERROR Did not find any free timers
	return NULL;
}

// Start a particular timer running
void timer_start (stm_timer_t* t, uint32_t us_period, timer_callback cb) {
#if (BOARD_V == 1)
	uint32_t prescalar = (SystemCoreClock/500000)-1;
#else
	uint32_t prescalar = (SystemCoreClock/1000000)-1;
#endif
	// Save the callback
	timer_callbacks[t->index] = cb;

	// Enable the clock
	RCC_APB2PeriphClockCmd(t->timer_clock , ENABLE);

	// Setup the interrupt
	t->nvic_init.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&t->nvic_init);

	// Need this to fit in 16 bits
	t->divider = 1;
	while (us_period > 65535) {
		us_period = us_period >> 1;
		prescalar = prescalar << 1;
		t->divider = t->divider << 1;
	}

	// Setup the actual timer
	t->tim_init.TIM_Period    = us_period;
	t->tim_init.TIM_Prescaler = prescalar;
	TIM_TimeBaseInit(t->tim_ptr, &t->tim_init);

	// Enable the interrupt
	TIM_ITConfig(t->tim_ptr, TIM_IT_Update , ENABLE);
	TIM_ClearITPendingBit(t->tim_ptr, TIM_IT_Update);

	// Enable the timer
	TIM_Cmd(t->tim_ptr, ENABLE);
}

void timer_disable_interrupt(stm_timer_t* t){
	TIM_ITConfig(t->tim_ptr, TIM_IT_Update, DISABLE);
}

void timer_enable_interrupt(stm_timer_t* t){
	TIM_ITConfig(t->tim_ptr, TIM_IT_Update, ENABLE);
}

void timer_reset (stm_timer_t* t, uint32_t val_us){

	val_us /= t->divider;
	//val_us /= t->tim_init.TIM_Prescaler;
	TIM_SetCounter(t->tim_ptr, val_us);

	// Clear the interrupt pending bit for good measure
	TIM_ClearITPendingBit(t->tim_ptr, TIM_IT_Update);
}

// Disable everything that timer_start enabled
void timer_stop (stm_timer_t* t) {
	// Disable the timer
	TIM_Cmd(t->tim_ptr, DISABLE);

	// Disable the interrupt
	TIM_ITConfig(t->tim_ptr, TIM_IT_Update , DISABLE);

	// Disable the interrupt channel
	t->nvic_init.NVIC_IRQChannelCmd = DISABLE;
	NVIC_Init(&t->nvic_init);

	// Disable the clock
	RCC_APB2PeriphClockCmd(t->timer_clock , DISABLE);

	// Remove the callback
	timer_callbacks[t->index] = NULL;
}

// Make timer available again for call to timer_init
void timer_free(stm_timer_t* t) {

    bool matched = FALSE;

    for (int i = 0; i < TIMER_NUMBER; i++) {

        if (t == &timers[i]) {

            if (timer_in_use[i] == 1) {
                timer_in_use[i] = 0;
				used_timers--;
                matched = TRUE;
            } else {
                // This timer might have already been freed by another function (e.g. INIT and RESP both using the same one)
            }
        }
    }
}

// Test whether stored timer pointer is valid
bool timer_is_valid(stm_timer_t* t) {

	for (int i = 0; i < TIMER_NUMBER; i++) {

		if (t == &timers[i]) {
			return TRUE;
		}
	}

	return FALSE;
}

/******************************************************************************/
// Interrupt handling
/******************************************************************************/

// Call the timer callback from main thread context
void timer_17_fired () {
	if (timer_callbacks[0] != NULL) {
		timer_callbacks[0]();
	}
}

// Call the timer callback from main thread context
void timer_16_fired () {
	if (timer_callbacks[1] != NULL) {
		timer_callbacks[1]();
	}
}

// Raw interrupt handlers from vector table
void TIM17_IRQHandler(void) {
	if (TIM_GetITStatus(TIM17, TIM_IT_Update) != RESET) {

		// Notify main loop that we got a timer interrupt
		// We save the index of the callback in our interrupt slot
		// so that when the main thread gets back to us we know what
		// to call.
		mark_interrupt(INTERRUPT_TIMER_17);

		// Clear Timer interrupt pending bit
		TIM_ClearITPendingBit(TIM17, TIM_IT_Update);
	}
}

void TIM16_IRQHandler(void) {
	if (TIM_GetITStatus(TIM16, TIM_IT_Update) != RESET) {

		// Notify main loop that we got a timer interrupt
		// We save the index of the callback in our interrupt slot
		// so that when the main thread gets back to us we know what
		// to call.
		mark_interrupt(INTERRUPT_TIMER_16);

		// Clear Timer interrupt pending bit
		TIM_ClearITPendingBit(TIM16, TIM_IT_Update);
	}
}
