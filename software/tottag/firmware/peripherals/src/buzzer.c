// Header inclusions ---------------------------------------------------------------------------------------------------

#include "ble_config.h"
#include "buzzer.h"
#include "nrf_delay.h"
#include "nrfx_gpiote.h"
#include "nrfx_pwm.h"


// Static buzzer state variables ---------------------------------------------------------------------------------------

#define BUZZER_CLOCK_HZ 1000000

#if (BOARD_V >= 0x10)
static nrfx_pwm_t pwm_instance = NRFX_PWM_INSTANCE(0);
static nrfx_pwm_config_t pwm_config = NRFX_PWM_DEFAULT_CONFIG;
static nrf_pwm_values_common_t pwm_duties[] = { 0x8000, 0 };
static nrf_pwm_sequence_t pwm_sequence = { .values.p_common = pwm_duties, .length = NRF_PWM_VALUES_LENGTH(pwm_duties), .repeats = 0, .end_delay = 0 };
#endif


// Private helper functions --------------------------------------------------------------------------------------------

static void buzzer_indicate_plugged(void)
{
   // Tone Frequency = base_clock / top_value
#if (BOARD_V >= 0x10)
#ifdef ENABLE_SOUNDS
   const uint32_t duration_ms = 100;
   uint32_t tone_frequency_hz = 880;
   nrfx_pwm_uninit(&pwm_instance);
   pwm_config.top_value = BUZZER_CLOCK_HZ / tone_frequency_hz;
   nrfx_pwm_init(&pwm_instance, &pwm_config, NULL);
   nrfx_pwm_simple_playback(&pwm_instance, &pwm_sequence, (duration_ms * BUZZER_CLOCK_HZ / 2000) / pwm_config.top_value, NRFX_PWM_FLAG_STOP);

   nrf_delay_ms(duration_ms);
   tone_frequency_hz = 1110;
   nrfx_pwm_uninit(&pwm_instance);
   pwm_config.top_value = BUZZER_CLOCK_HZ / tone_frequency_hz;
   nrfx_pwm_init(&pwm_instance, &pwm_config, NULL);
   nrfx_pwm_simple_playback(&pwm_instance, &pwm_sequence, (duration_ms * BUZZER_CLOCK_HZ / 2000) / pwm_config.top_value, NRFX_PWM_FLAG_STOP);

   nrf_delay_ms(duration_ms);
   tone_frequency_hz = 1320;
   nrfx_pwm_uninit(&pwm_instance);
   pwm_config.top_value = BUZZER_CLOCK_HZ / tone_frequency_hz;
   nrfx_pwm_init(&pwm_instance, &pwm_config, NULL);
   nrfx_pwm_simple_playback(&pwm_instance, &pwm_sequence, (duration_ms * BUZZER_CLOCK_HZ / 2000) / pwm_config.top_value, NRFX_PWM_FLAG_STOP);

   nrf_delay_ms(duration_ms);
   tone_frequency_hz = 1760;
   nrfx_pwm_uninit(&pwm_instance);
   pwm_config.top_value = BUZZER_CLOCK_HZ / tone_frequency_hz;
   nrfx_pwm_init(&pwm_instance, &pwm_config, NULL);
   nrfx_pwm_simple_playback(&pwm_instance, &pwm_sequence, (duration_ms * BUZZER_CLOCK_HZ / 2000) / pwm_config.top_value, NRFX_PWM_FLAG_STOP);

   nrf_delay_ms(duration_ms + 10);
   nrfx_gpiote_out_set(BUZZER_DRIVER);
#endif
#endif
}

static void buzzer_indicate_unplugged(void)
{
   // Tone Frequency = base_clock / top_value
#if (BOARD_V >= 0x10)
#ifdef ENABLE_SOUNDS
   const uint32_t duration_ms = 100;
   uint32_t tone_frequency_hz = 1760;
   nrfx_pwm_uninit(&pwm_instance);
   pwm_config.top_value = BUZZER_CLOCK_HZ / tone_frequency_hz;
   nrfx_pwm_init(&pwm_instance, &pwm_config, NULL);
   nrfx_pwm_simple_playback(&pwm_instance, &pwm_sequence, (duration_ms * BUZZER_CLOCK_HZ / 2000) / pwm_config.top_value, NRFX_PWM_FLAG_STOP);

   nrf_delay_ms(duration_ms);
   tone_frequency_hz = 1320;
   nrfx_pwm_uninit(&pwm_instance);
   pwm_config.top_value = BUZZER_CLOCK_HZ / tone_frequency_hz;
   nrfx_pwm_init(&pwm_instance, &pwm_config, NULL);
   nrfx_pwm_simple_playback(&pwm_instance, &pwm_sequence, (duration_ms * BUZZER_CLOCK_HZ / 2000) / pwm_config.top_value, NRFX_PWM_FLAG_STOP);

   nrf_delay_ms(duration_ms);
   tone_frequency_hz = 1110;
   nrfx_pwm_uninit(&pwm_instance);
   pwm_config.top_value = BUZZER_CLOCK_HZ / tone_frequency_hz;
   nrfx_pwm_init(&pwm_instance, &pwm_config, NULL);
   nrfx_pwm_simple_playback(&pwm_instance, &pwm_sequence, (duration_ms * BUZZER_CLOCK_HZ / 2000) / pwm_config.top_value, NRFX_PWM_FLAG_STOP);

   nrf_delay_ms(duration_ms);
   tone_frequency_hz = 880;
   nrfx_pwm_uninit(&pwm_instance);
   pwm_config.top_value = BUZZER_CLOCK_HZ / tone_frequency_hz;
   nrfx_pwm_init(&pwm_instance, &pwm_config, NULL);
   nrfx_pwm_simple_playback(&pwm_instance, &pwm_sequence, (duration_ms * BUZZER_CLOCK_HZ / 2000) / pwm_config.top_value, NRFX_PWM_FLAG_STOP);

   nrf_delay_ms(duration_ms + 10);
   nrfx_gpiote_out_set(BUZZER_DRIVER);
#endif
#endif
}


// Buzzer functionality ------------------------------------------------------------------------------------------------

void buzzer_init(void)
{
   // Configure buzzer GPIO as output, pulled high by default
   nrfx_gpiote_out_config_t buzzer_gpio_config = NRFX_GPIOTE_CONFIG_OUT_SIMPLE(1);
   nrfx_gpiote_out_init(BUZZER_DRIVER, &buzzer_gpio_config);

   // Configure PWM signal generator on buzzer pin
#if (BOARD_V >= 0x10)
   pwm_config.base_clock = NRF_PWM_CLK_1MHz;
   pwm_config.output_pins[0] = BUZZER_DRIVER;
   pwm_config.output_pins[1] = pwm_config.output_pins[2] = pwm_config.output_pins[3] = NRFX_PWM_PIN_NOT_USED;
   APP_ERROR_CHECK(nrfx_pwm_init(&pwm_instance, &pwm_config, NULL));
   nrfx_gpiote_out_set(BUZZER_DRIVER);
#endif
}

void buzzer_indicate_plugged_status(bool plugged_in)
{
   if (plugged_in)
      buzzer_indicate_plugged();
   else
      buzzer_indicate_unplugged();
}

void buzzer_indicate_invalid_rtc_time(void)
{
   // Tone Frequency = base_clock / top_value
#if (BOARD_V >= 0x10)
#ifdef ENABLE_SOUNDS
   uint32_t duration_ms = 100;
   uint32_t tone_frequency_hz = 1047;
   nrfx_pwm_uninit(&pwm_instance);
   pwm_config.top_value = BUZZER_CLOCK_HZ / tone_frequency_hz;
   nrfx_pwm_init(&pwm_instance, &pwm_config, NULL);
   nrfx_pwm_simple_playback(&pwm_instance, &pwm_sequence, (duration_ms * BUZZER_CLOCK_HZ / 2000) / pwm_config.top_value, NRFX_PWM_FLAG_STOP);

   duration_ms = 200;
   nrf_delay_ms(duration_ms);
   nrfx_pwm_uninit(&pwm_instance);
   pwm_config.top_value = BUZZER_CLOCK_HZ / tone_frequency_hz;
   nrfx_pwm_init(&pwm_instance, &pwm_config, NULL);
   nrfx_pwm_simple_playback(&pwm_instance, &pwm_sequence, (duration_ms * BUZZER_CLOCK_HZ / 2000) / pwm_config.top_value, NRFX_PWM_FLAG_STOP);

   nrf_delay_ms(duration_ms);
   duration_ms = 100;
   tone_frequency_hz = 830;
   nrfx_pwm_uninit(&pwm_instance);
   pwm_config.top_value = BUZZER_CLOCK_HZ / tone_frequency_hz;
   nrfx_pwm_init(&pwm_instance, &pwm_config, NULL);
   nrfx_pwm_simple_playback(&pwm_instance, &pwm_sequence, (duration_ms * BUZZER_CLOCK_HZ / 2000) / pwm_config.top_value, NRFX_PWM_FLAG_STOP);

   duration_ms = 200;
   nrf_delay_ms(duration_ms);
   tone_frequency_hz = 880;
   nrfx_pwm_uninit(&pwm_instance);
   pwm_config.top_value = BUZZER_CLOCK_HZ / tone_frequency_hz;
   nrfx_pwm_init(&pwm_instance, &pwm_config, NULL);
   nrfx_pwm_simple_playback(&pwm_instance, &pwm_sequence, (duration_ms * BUZZER_CLOCK_HZ / 2000) / pwm_config.top_value, NRFX_PWM_FLAG_STOP);

   nrf_delay_ms(duration_ms);
   tone_frequency_hz = 698;
   nrfx_pwm_uninit(&pwm_instance);
   pwm_config.top_value = BUZZER_CLOCK_HZ / tone_frequency_hz;
   nrfx_pwm_init(&pwm_instance, &pwm_config, NULL);
   nrfx_pwm_simple_playback(&pwm_instance, &pwm_sequence, (duration_ms * BUZZER_CLOCK_HZ / 2000) / pwm_config.top_value, NRFX_PWM_FLAG_STOP);

   nrf_delay_ms(duration_ms * 2);
   nrfx_gpiote_out_set(BUZZER_DRIVER);
#endif
#endif
}

void buzzer_indicate_error(void)
{
   // Tone Frequency = base_clock / top_value
#if (BOARD_V >= 0x10)
#ifdef ENABLE_SOUNDS
   const uint32_t duration_ms = 300;
   uint32_t tone_frequency_hz = 880;
   nrfx_pwm_uninit(&pwm_instance);
   pwm_config.top_value = BUZZER_CLOCK_HZ / tone_frequency_hz;
   nrfx_pwm_init(&pwm_instance, &pwm_config, NULL);
   nrfx_pwm_simple_playback(&pwm_instance, &pwm_sequence, (duration_ms * BUZZER_CLOCK_HZ / 2000) / pwm_config.top_value, NRFX_PWM_FLAG_STOP);

   nrf_delay_ms(duration_ms + 100);
   tone_frequency_hz = 587;
   nrfx_pwm_uninit(&pwm_instance);
   pwm_config.top_value = BUZZER_CLOCK_HZ / tone_frequency_hz;
   nrfx_pwm_init(&pwm_instance, &pwm_config, NULL);
   nrfx_pwm_simple_playback(&pwm_instance, &pwm_sequence, (2 * duration_ms * BUZZER_CLOCK_HZ / 2000) / pwm_config.top_value, NRFX_PWM_FLAG_STOP);

   nrf_delay_ms((2 * duration_ms) + 10);
   nrfx_gpiote_out_set(BUZZER_DRIVER);
#endif
#endif
}

void buzzer_indicate_low_battery(void)
{
#if (BOARD_V >= 0x10)
#ifdef ENABLE_SOUNDS
#ifdef ENABLE_LOW_BATTERY_SOUNDS
   uint32_t duration_ms = 150;
   uint32_t tone_frequency_hz = 1760;
   nrfx_pwm_uninit(&pwm_instance);
   pwm_config.top_value = BUZZER_CLOCK_HZ / tone_frequency_hz;
   nrfx_pwm_init(&pwm_instance, &pwm_config, NULL);
   nrfx_pwm_simple_playback(&pwm_instance, &pwm_sequence, (duration_ms * BUZZER_CLOCK_HZ / 2000) / pwm_config.top_value, NRFX_PWM_FLAG_STOP);
   nrf_delay_ms(2 * duration_ms);
   nrfx_pwm_simple_playback(&pwm_instance, &pwm_sequence, (duration_ms * BUZZER_CLOCK_HZ / 2000) / pwm_config.top_value, NRFX_PWM_FLAG_STOP);

   nrf_delay_ms(6 * duration_ms);
   nrfx_pwm_simple_playback(&pwm_instance, &pwm_sequence, (duration_ms * BUZZER_CLOCK_HZ / 2000) / pwm_config.top_value, NRFX_PWM_FLAG_STOP);
   nrf_delay_ms(2 * duration_ms);
   nrfx_pwm_simple_playback(&pwm_instance, &pwm_sequence, (duration_ms * BUZZER_CLOCK_HZ / 2000) / pwm_config.top_value, NRFX_PWM_FLAG_STOP);

   nrf_delay_ms(2 * duration_ms);
   nrfx_gpiote_out_set(BUZZER_DRIVER);
#endif
#endif
#endif
}
