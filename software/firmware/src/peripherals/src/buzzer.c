// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "buzzer.h"


// Static Global Variables ---------------------------------------------------------------------------------------------

static am_hal_timer_config_t timer_config;
static volatile const uint16_t *current_frequency, *current_duration;
static volatile uint32_t interrupt_counter_index, interrupt_counter_max;
static uint32_t buzzer_clock_hz;


// Private Helper Functions --------------------------------------------------------------------------------------------

#if REVISION_ID == REVISION_I

static uint32_t am_hal_timer_output_config(uint32_t ui32PadNum, uint32_t eOutputType)
{
    uint32_t ui32CfgShf = ui32PadNum % 4 * 8;
    uint32_t ui32OutcfgMsk = 0x7F << ui32CfgShf;
    uint32_t ui32OutcfgFnc = eOutputType << ui32CfgShf;

    AM_CRITICAL_BEGIN

    uint32_t volatile *outcfg = &(TIMER->OUTCFG0) + (ui32PadNum >> 2);
    uint32_t ui32OutcfgValue = *outcfg;
    ui32OutcfgValue &= ~ui32OutcfgMsk;
    ui32OutcfgValue |=  ui32OutcfgFnc;
    *outcfg = ui32OutcfgValue;

    AM_CRITICAL_END
    return AM_HAL_STATUS_SUCCESS;
}

#endif  // #if REVISION_ID == REVISION_I

static void continue_current_sequence(void)
{
   // Check whether the end of the sequence has been reached
   if (*current_frequency > 0)
   {
      // Check whether the current sequence value should be silence
      if (*current_frequency == 1)
      {
         timer_config.ui32Compare0 = (*current_duration * buzzer_clock_hz) / 1000;
         timer_config.ui32Compare1 = timer_config.ui32Compare0 + 1;
      }
      else
      {
         timer_config.ui32Compare0 = buzzer_clock_hz / *current_frequency;
         timer_config.ui32Compare1 = timer_config.ui32Compare0 / 2;
      }

      // Set the duration and expected number of interrupt services
      interrupt_counter_index = 0;
      interrupt_counter_max = (*current_duration * *current_frequency) / 1000;
      am_hal_timer_config(BUZZER_TIMER_NUMBER, &timer_config);

      // Move on to the next value in the sequence
      ++current_frequency;
      ++current_duration;
      am_hal_timer_clear(BUZZER_TIMER_NUMBER);
   }
   else
   {
      // Stop all buzzer functionality
      am_hal_timer_stop(BUZZER_TIMER_NUMBER);
      am_hal_timer_disable(BUZZER_TIMER_NUMBER);
      am_hal_gpio_output_clear(PIN_BUZZER_DRIVER);
      current_frequency = current_duration = NULL;
   }
}

void am_timer00_isr(void)
{
   // Clear the timer interrupt and check if it is time to move to the next value in the PWM sequence
   am_hal_timer_interrupt_clear(AM_HAL_TIMER_MASK(BUZZER_TIMER_NUMBER, AM_HAL_TIMER_COMPARE0));
   if (++interrupt_counter_index >= interrupt_counter_max)
      continue_current_sequence();
}


// Public API Functions ------------------------------------------------------------------------------------------------

void buzzer_init(void)
{
   // Initialize static variables
   buzzer_clock_hz = AM_HAL_CLKGEN_FREQ_MAX_HZ / 256;
   current_frequency = NULL;
   current_duration = NULL;

   // Configure the buzzer driver GPIO, set to low by default
   am_hal_gpio_pincfg_t buzzer_pin_output = AM_HAL_GPIO_PINCFG_OUTPUT;
   buzzer_pin_output.GP.cfg_b.uFuncSel = AM_HAL_PIN_0_CT0;
   configASSERT0(am_hal_gpio_pinconfig(PIN_BUZZER_DRIVER, buzzer_pin_output));
   am_hal_gpio_output_clear(PIN_BUZZER_DRIVER);
   configASSERT0(am_hal_timer_output_config(PIN_BUZZER_DRIVER, 2 * BUZZER_TIMER_NUMBER));

   // Set up the PWM timer configuration
   am_hal_timer_default_config_set(&timer_config);
   timer_config.eInputClock = AM_HAL_TIMER_CLOCK_HFRC_DIV256;
   timer_config.eFunction = AM_HAL_TIMER_FN_PWM;
   am_hal_timer_config(BUZZER_TIMER_NUMBER, &timer_config);

   // Enable interrupts upon PWM timer completion
   am_hal_timer_interrupt_enable(AM_HAL_TIMER_MASK(BUZZER_TIMER_NUMBER, AM_HAL_TIMER_COMPARE0));
   NVIC_SetPriority(TIMER0_IRQn + BUZZER_TIMER_NUMBER, AM_IRQ_PRIORITY_DEFAULT);
   NVIC_EnableIRQ(TIMER0_IRQn + BUZZER_TIMER_NUMBER);
}

void buzzer_deinit(void)
{
   // Disable all buzzer-based interrupts
   NVIC_DisableIRQ(TIMER0_IRQn + BUZZER_TIMER_NUMBER);
   am_hal_timer_interrupt_disable(AM_HAL_TIMER_MASK(BUZZER_TIMER_NUMBER, AM_HAL_TIMER_COMPARE0));
   am_hal_timer_interrupt_clear(AM_HAL_TIMER_MASK(BUZZER_TIMER_NUMBER, AM_HAL_TIMER_COMPARE0));

   // Ensure the buzzer playback timer is stopped
   am_hal_timer_stop(BUZZER_TIMER_NUMBER);
   am_hal_timer_disable(BUZZER_TIMER_NUMBER);
   am_hal_gpio_output_clear(PIN_BUZZER_DRIVER);
}

void buzzer_indicate_plugged_in(void)
{
   const uint16_t plugged_frequencies[] = { 880,   1, 1110,   1, 1320,   1, 1760, 0 };
   const uint16_t plugged_durations[] =   { 100,  10,  100,  10,  100,  10,  100 };
   if (!current_frequency)
   {
      current_frequency = plugged_frequencies;
      current_duration = plugged_durations;
      continue_current_sequence();
   }
}

void buzzer_indicate_unplugged(void)
{
   const uint16_t unplugged_frequencies[] = { 1760,   1, 1320,   1, 1110,   1, 880, 0 };
   const uint16_t unplugged_durations[] =   {  100,  10,  100,  10,  100,  10, 100 };
   if (!current_frequency)
   {
      current_frequency = unplugged_frequencies;
      current_duration = unplugged_durations;
      continue_current_sequence();
   }
}

void buzzer_indicate_invalid_rtc_time(void)
{
   const uint16_t invalid_rtc_frequencies[] = { 760,   1, 760, 0 };
   const uint16_t invalid_rtc_durations[] =   {  100, 100,  400 };
   if (!current_frequency)
   {
      current_frequency = invalid_rtc_frequencies;
      current_duration = invalid_rtc_durations;
      continue_current_sequence();
   }
}

void buzzer_indicate_error(void)
{
   const uint16_t error_frequencies[] = { 880,   1, 587, 0 };
   const uint16_t error_durations[] =   { 300, 100, 500 };
   if (!current_frequency)
   {
      current_frequency = error_frequencies;
      current_duration = error_durations;
      continue_current_sequence();
   }
}

void buzzer_indicate_location(void)
{
   const uint16_t locator_frequencies[] = { 1047,   1, 1047, 830,   1, 880, 698, 0 };
   const uint16_t locator_durations[] =   {  100, 100,  100, 100, 100, 100, 200 };
   if (!current_frequency)
   {
      current_frequency = locator_frequencies;
      current_duration = locator_durations;
      continue_current_sequence();
   }
}
