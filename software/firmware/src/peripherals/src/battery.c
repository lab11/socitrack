// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "battery.h"


// TODO: Zero-CPU Brownout Detection -----------------------------------------------------------------------------------

// The Apollo4 voltage comparator (VCOMP) runs continuously in deep sleep with no CPU or ADC involvement,
// making it the only way to get advance warning of battery collapse without paying standby current for a
// free-running ADC. It compares a selectable input against an internal DAC reference and raises VCOMP_IRQn
// when the input falls below it.
//
// GATED OFF ON ALL CURRENT BOARDS. VCOMP's external inputs are CMPIN0 (GPIO 10) and CMPIN1 (GPIO 11) only.
// The battery divider is wired to GPIO 18, whose pinmux has no comparator function, so VCOMP cannot see the
// battery on revM/N/O/P. GPIO 10 is already the ranging-radio MISO; GPIO 11 is unused on every revision.
//
// TO ENABLE ON A FUTURE BOARD REVISION:
//   1. Route the existing PIN_BATTERY_VOLTAGE divider node to GPIO 11 in addition to GPIO 18.
//   2. Add "#define PIN_BATTERY_VOLTAGE_COMPARATOR 11" to that revision's pinout.h.
// Everything below then compiles in automatically. Optionally override BATTERY_COMPARATOR_LEVEL to move the
// trip point; see the voltage table below.
//
// Trip point math: the comparator sees V_batt * LOWER / (UPPER + LOWER), so a DAC level L corresponds to a
// battery voltage of L * (UPPER + LOWER) / LOWER. With the revP divider (510k / 187k, ratio 0.2683):
//     LVLSEL_0P77V -> 2870 mV     LVLSEL_0P97V -> 3616 mV     LVLSEL_1P16V -> 4324 mV
// 0.97 V is the only level that lands usefully between BATTERY_EMPTY (3500) and BATTERY_CRITICAL (3680).

#ifdef PIN_BATTERY_VOLTAGE_COMPARATOR

#if PIN_BATTERY_VOLTAGE_COMPARATOR == 10
#define BATTERY_COMPARATOR_FUNCTION                 AM_HAL_PIN_10_CMPIN0
#define BATTERY_COMPARATOR_PSEL                     VCOMP_CFG_PSEL_VEXT1
#elif PIN_BATTERY_VOLTAGE_COMPARATOR == 11
#define BATTERY_COMPARATOR_FUNCTION                 AM_HAL_PIN_11_CMPIN1
#define BATTERY_COMPARATOR_PSEL                     VCOMP_CFG_PSEL_VEXT2
#else
#error "PIN_BATTERY_VOLTAGE_COMPARATOR must be GPIO 10 (CMPIN0) or GPIO 11 (CMPIN1)"
#endif

#ifndef BATTERY_COMPARATOR_LEVEL
#define BATTERY_COMPARATOR_LEVEL                    VCOMP_CFG_LVLSEL_0P97V
#endif

#endif  // #ifdef PIN_BATTERY_VOLTAGE_COMPARATOR


// Static Global Variables ---------------------------------------------------------------------------------------------

#define BATTERY_ADC_SLOT                            0

#define BATTERY_STIMER_HZ                           32768u
#define BATTERY_MS_TO_STIMER(ms)                    (((uint32_t)(ms) * BATTERY_STIMER_HZ) / 1000u)

typedef struct
{
   int8_t reported;              // last state handed to the callback; -1 until the first observation
   bool accepted_valid;          // whether accepted_at holds a meaningful value yet
   uint32_t accepted_at;         // free-running STIMER count at the last accepted change
} charger_signal_t;

static void *adc_handle;
static SemaphoreHandle_t adc_mutex;
static StaticSemaphore_t adc_mutex_buffer;
static uint32_t last_valid_voltage_mV;
static battery_event_callback_t event_callback;
static volatile uint32_t battery_voltage_code, suppressed_edge_count;
static volatile charger_signal_t plugged_signal, charging_signal;
static volatile bool conversion_complete;


// Private Helper Functions --------------------------------------------------------------------------------------------

static bool signal_change_accepted(volatile charger_signal_t *signal, bool state)
{
   // Report only a genuine change and only once per de-bounce window
   const uint32_t now = am_hal_stimer_counter_get();
   bool accepted = false;
   AM_CRITICAL_BEGIN
   if (signal->reported != (int8_t)state)
   {
      if (signal->accepted_valid && ((now - signal->accepted_at) < BATTERY_MS_TO_STIMER(BATTERY_EVENT_DEBOUNCE_MS)))
      {
         // Deferred, not dropped: the latch is left alone so this change is still outstanding
         ++suppressed_edge_count;
      }
      else
      {
         signal->reported = (int8_t)state;
         signal->accepted_at = now;
         signal->accepted_valid = true;
         accepted = true;
      }
   }
   AM_CRITICAL_END
   return accepted;
}

static inline bool locking_applies(void)
{
   // Returns whether the current context is one in which the ADC mutex should be used
   return (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) && !xPortIsInsideInterrupt();
}

static bool acquire_adc(void)
{
   // Check whether locking applies here
   if (!locking_applies())
      return true;

   // Create the ADC mutex if it doesn't exist yet
   if (!adc_mutex)
   {
      taskENTER_CRITICAL();
      if (!adc_mutex)
         adc_mutex = xSemaphoreCreateMutexStatic(&adc_mutex_buffer);
      taskEXIT_CRITICAL();
   }

   // Acquire the ADC mutex if it exists, returning false on timeout
   if (!adc_mutex)
      return false;
   return xSemaphoreTake(adc_mutex, pdMS_TO_TICKS(BATTERY_ADC_TIMEOUT_MS)) == pdPASS;
}

static void release_adc(void)
{
   // Release the ADC mutex if it exists and locking applies here
   if (adc_mutex && locking_applies())
      xSemaphoreGive(adc_mutex);
}

static void shut_down_adc(void)
{
   // Disable the ADC and put it into Deep Sleep mode
   am_hal_adc_interrupt_disable(adc_handle, AM_HAL_ADC_INT_CNVCMP);
   am_hal_adc_power_control(adc_handle, AM_HAL_SYSCTRL_DEEPSLEEP, true);
   NVIC_DisableIRQ(ADC_IRQn);
}

static void signal_charge_complete(bool charge_complete)
{
   // Update the charge complete signal on the wireless charger to HIGH or HIGH IMPEDANCE
   if (charge_complete)
      am_hal_gpio_output_tristate_enable(PIN_BATTERY_CHARGE_COMPLETE_SIGNAL);
   else
      am_hal_gpio_output_tristate_disable(PIN_BATTERY_CHARGE_COMPLETE_SIGNAL);
}

static void plugged_in_status_changed(void *args)
{
   // Retrieve the current plugged-in status of the charger
   const bool is_plugged_in = battery_monitor_is_plugged_in();

   // Toggle the interrupt direction (dual-edge interrupts not available due to errata)
   am_hal_gpio_pincfg_t pin_config = AM_HAL_GPIO_PINCFG_INPUT;
   pin_config.GP.cfg_b.ePullup = AM_HAL_GPIO_PIN_PULLUP_100K;
   pin_config.GP.cfg_b.eIntDir = is_plugged_in ? AM_HAL_GPIO_PIN_INTDIR_LO2HI : AM_HAL_GPIO_PIN_INTDIR_HI2LO;
   am_hal_gpio_pinconfig(PIN_BATTERY_INPUT_POWER_GOOD, pin_config);

   // Update the "charge complete signal" for the wireless charger
   signal_charge_complete(is_plugged_in && !battery_monitor_is_charging());

   // Report only a genuine, de-bounced change
   if (signal_change_accepted(&plugged_signal, is_plugged_in) && event_callback)
      event_callback(is_plugged_in ? BATTERY_PLUGGED : BATTERY_UNPLUGGED);
}

static void charging_status_changed(void *args)
{
   // Retrieve the current charging statuses of the charger
   const bool is_charging = battery_monitor_is_charging();

   // Toggle the interrupt direction (dual-edge interrupts not available due to errata)
   am_hal_gpio_pincfg_t pin_config = AM_HAL_GPIO_PINCFG_INPUT;
   pin_config.GP.cfg_b.ePullup = AM_HAL_GPIO_PIN_PULLUP_100K;
   pin_config.GP.cfg_b.eIntDir = is_charging ? AM_HAL_GPIO_PIN_INTDIR_LO2HI : AM_HAL_GPIO_PIN_INTDIR_HI2LO;
   am_hal_gpio_pinconfig(PIN_BATTERY_CHARGING_STATUS, pin_config);

   // Update the "charge complete signal" for the wireless charger
   signal_charge_complete(battery_monitor_is_plugged_in() && !is_charging);

   // Report only a genuine, de-bounced change
   if (signal_change_accepted(&charging_signal, is_charging) && event_callback)
      event_callback(is_charging ? BATTERY_CHARGING : BATTERY_NOT_CHARGING);
}

#ifdef PIN_BATTERY_VOLTAGE_COMPARATOR

static void brownout_detection_init(void)
{
   // Route the battery divider tap to the comparator input
   am_hal_gpio_pincfg_t comparator_pin_config = AM_HAL_GPIO_PINCFG_DEFAULT;
   comparator_pin_config.GP.cfg_b.uFuncSel = BATTERY_COMPARATOR_FUNCTION;
   comparator_pin_config.GP.cfg_b.eGPInput = AM_HAL_GPIO_PIN_INPUT_NONE;
   comparator_pin_config.GP.cfg_b.ePullup = AM_HAL_GPIO_PIN_PULLUP_NONE;
   configASSERT0(am_hal_gpio_pinconfig(PIN_BATTERY_VOLTAGE_COMPARATOR, comparator_pin_config));

   // Power up the comparator and select the divider tap against the internal DAC reference
   VCOMP->PWDKEY = VCOMP_PWDKEY_PWDKEY_Key;
   VCOMP->CFG_b.PSEL = BATTERY_COMPARATOR_PSEL;
   VCOMP->CFG_b.NSEL = VCOMP_CFG_NSEL_DAC;
   VCOMP->CFG_b.LVLSEL = BATTERY_COMPARATOR_LEVEL;

   // Discard any comparison made while the reference was still settling, then enable falling-edge interrupts
   VCOMP->INTCLR = VCOMP_INTCLR_OUTLOW_Msk | VCOMP_INTCLR_OUTHI_Msk;
   VCOMP->INTEN_b.OUTLOW = 1;
   NVIC_SetPriority(VCOMP_IRQn, NVIC_configKERNEL_INTERRUPT_PRIORITY);
   NVIC_EnableIRQ(VCOMP_IRQn);
}

static void brownout_detection_deinit(void)
{
   // Disable the interrupt and power the comparator back down
   NVIC_DisableIRQ(VCOMP_IRQn);
   VCOMP->INTEN_b.OUTLOW = 0;
   VCOMP->INTCLR = VCOMP_INTCLR_OUTLOW_Msk | VCOMP_INTCLR_OUTHI_Msk;
   VCOMP->PWDKEY = 0;
}

void am_vcomp_isr(void)
{
   // Clear the interrupt and disable further ones
   VCOMP->INTCLR = VCOMP_INTCLR_OUTLOW_Msk;
   VCOMP->INTEN_b.OUTLOW = 0;
   NVIC_DisableIRQ(VCOMP_IRQn);

   // Hand off to the application, which flushes buffered log data and shuts down
   if (event_callback)
      event_callback(BATTERY_CRITICAL_VOLTAGE);
}

#endif  // #ifdef PIN_BATTERY_VOLTAGE_COMPARATOR

void am_adc_isr(void)
{
   // Clear the ADC interrupt
   static uint32_t status;
   am_hal_adc_interrupt_status(adc_handle, &status, true);
   am_hal_adc_interrupt_clear(adc_handle, status);

   // Read all values from the ADC FIFO
   static am_hal_adc_sample_t sample;
   while (AM_HAL_ADC_FIFO_COUNT(ADC->FIFO))
   {
      uint32_t samples_to_read = 1;
      am_hal_daxi_control(AM_HAL_DAXI_CONTROL_INVALIDATE, NULL);
      am_hal_adc_samples_read(adc_handle, true, NULL, &samples_to_read, &sample);
      if (sample.ui32Slot == BATTERY_ADC_SLOT)
         battery_voltage_code = AM_HAL_ADC_FIFO_SAMPLE(sample.ui32Sample);
   }

   // Set the conversion complete flag
   conversion_complete = true;
}


// Public API Functions ------------------------------------------------------------------------------------------------

void battery_monitor_init(void)
{
   // Start both charger latches undefined so the first observation of each is always reported
   plugged_signal = (charger_signal_t){ .reported = -1, .accepted_valid = false, .accepted_at = 0 };
   charging_signal = (charger_signal_t){ .reported = -1, .accepted_valid = false, .accepted_at = 0 };
   suppressed_edge_count = 0;

   // Define the ADC configuration structures
   am_hal_adc_config_t adc_config =
   {
      .eClock = AM_HAL_ADC_CLKSEL_HFRC_24MHZ,
      .ePolarity = AM_HAL_ADC_TRIGPOL_RISING,
      .eTrigger = AM_HAL_ADC_TRIGSEL_SOFTWARE,
      .eClockMode = AM_HAL_ADC_CLKMODE_LOW_POWER,
      .ePowerMode = AM_HAL_ADC_LPMODE1,
      .eRepeat = AM_HAL_ADC_SINGLE_SCAN,
      .eRepeatTrigger = AM_HAL_ADC_RPTTRIGSEL_INT
   };
   am_hal_adc_slot_config_t unused_slot_config =
   {
      .bEnabled = false,
      .bWindowCompare = false,
      .eChannel = AM_HAL_ADC_SLOT_CHSEL_SE0,
      .ePrecisionMode = AM_HAL_ADC_SLOT_12BIT,
      .ui32TrkCyc = AM_HAL_ADC_MIN_TRKCYC,
      .eMeasToAvg = AM_HAL_ADC_SLOT_AVG_1
   };
   am_hal_adc_slot_config_t used_slot_config =
   {
      .bEnabled = true,
      .bWindowCompare = false,
      .eChannel = PIN_BATTERY_VOLTAGE_ADC_CHANNEL,
      .ePrecisionMode = AM_HAL_ADC_SLOT_12BIT,
      .ui32TrkCyc = AM_HAL_ADC_MIN_TRKCYC,
      .eMeasToAvg = AM_HAL_ADC_SLOT_AVG_1
   };

   // Initialize static variables
   conversion_complete = false;
   battery_voltage_code = 0;
   event_callback = NULL;

   // Initialize the charging, plugged-in status, and voltage pins
   am_hal_gpio_pincfg_t input_pin_config = AM_HAL_GPIO_PINCFG_INPUT;
   am_hal_gpio_pincfg_t voltage_pin_config = AM_HAL_GPIO_PINCFG_INPUT;
   input_pin_config.GP.cfg_b.ePullup = AM_HAL_GPIO_PIN_PULLUP_100K;
   input_pin_config.GP.cfg_b.eIntDir = AM_HAL_GPIO_PIN_INTDIR_BOTH;
   voltage_pin_config.GP.cfg_b.uFuncSel = PIN_BATTERY_VOLTAGE_FUNCTION;
   configASSERT0(am_hal_gpio_pinconfig(PIN_BATTERY_CHARGE_COMPLETE_SIGNAL, am_hal_gpio_pincfg_tristate));
   configASSERT0(am_hal_gpio_pinconfig(PIN_BATTERY_INPUT_POWER_GOOD, input_pin_config));
   configASSERT0(am_hal_gpio_pinconfig(PIN_BATTERY_CHARGING_STATUS, input_pin_config));
   configASSERT0(am_hal_gpio_pinconfig(PIN_BATTERY_VOLTAGE, voltage_pin_config));
   am_hal_gpio_output_set(PIN_BATTERY_CHARGE_COMPLETE_SIGNAL);
   plugged_in_status_changed(NULL);
   charging_status_changed(NULL);

   // Set the status pins to generate an interrupt on toggle
   uint32_t charging_status_pin = PIN_BATTERY_CHARGING_STATUS, input_power_good_pin = PIN_BATTERY_INPUT_POWER_GOOD;
   configASSERT0(am_hal_gpio_interrupt_register(AM_HAL_GPIO_INT_CHANNEL_0, PIN_BATTERY_CHARGING_STATUS, charging_status_changed, NULL));
   configASSERT0(am_hal_gpio_interrupt_register(AM_HAL_GPIO_INT_CHANNEL_0, PIN_BATTERY_INPUT_POWER_GOOD, plugged_in_status_changed, NULL));
   configASSERT0(am_hal_gpio_interrupt_control(AM_HAL_GPIO_INT_CHANNEL_0, AM_HAL_GPIO_INT_CTRL_INDV_ENABLE, &charging_status_pin));
   configASSERT0(am_hal_gpio_interrupt_control(AM_HAL_GPIO_INT_CHANNEL_0, AM_HAL_GPIO_INT_CTRL_INDV_ENABLE, &input_power_good_pin));
   NVIC_SetPriority(GPIO0_001F_IRQn + GPIO_NUM2IDX(PIN_BATTERY_INPUT_POWER_GOOD), NVIC_configKERNEL_INTERRUPT_PRIORITY);
   NVIC_SetPriority(GPIO0_001F_IRQn + GPIO_NUM2IDX(PIN_BATTERY_CHARGING_STATUS), NVIC_configKERNEL_INTERRUPT_PRIORITY);
   NVIC_EnableIRQ(GPIO0_001F_IRQn + GPIO_NUM2IDX(PIN_BATTERY_INPUT_POWER_GOOD));
   NVIC_EnableIRQ(GPIO0_001F_IRQn + GPIO_NUM2IDX(PIN_BATTERY_CHARGING_STATUS));

   // Initialize and configure the ADC
   configASSERT0(am_hal_adc_initialize(0, &adc_handle));
   configASSERT0(am_hal_adc_power_control(adc_handle, AM_HAL_SYSCTRL_WAKE, false));
   configASSERT0(am_hal_adc_configure(adc_handle, &adc_config));

   // Configure all ADC conversion slots
   for (int slot = 0; slot < AM_HAL_ADC_MAX_SLOTS; ++slot)
      if (slot == BATTERY_ADC_SLOT)
         am_hal_adc_configure_slot(adc_handle, slot, &used_slot_config);
      else
         am_hal_adc_configure_slot(adc_handle, slot, &unused_slot_config);

   // Put the ADC into Deep Sleep mode
   configASSERT0(am_hal_adc_power_control(adc_handle, AM_HAL_SYSCTRL_DEEPSLEEP, true));

   // Enable zero-CPU brownout detection
#ifdef PIN_BATTERY_VOLTAGE_COMPARATOR
   brownout_detection_init();
#endif
}

void battery_monitor_deinit(void)
{
   // Disable zero-CPU brownout detection
#ifdef PIN_BATTERY_VOLTAGE_COMPARATOR
   brownout_detection_deinit();
#endif

   // Deinitialize the ADC module
   am_hal_adc_power_control(adc_handle, AM_HAL_SYSCTRL_WAKE, true);
   am_hal_adc_deinitialize(adc_handle);

   // Disable all battery-based interrupts
   NVIC_DisableIRQ(GPIO0_001F_IRQn + GPIO_NUM2IDX(PIN_BATTERY_INPUT_POWER_GOOD));
   NVIC_DisableIRQ(GPIO0_001F_IRQn + GPIO_NUM2IDX(PIN_BATTERY_CHARGING_STATUS));
   uint32_t charging_status_pin = PIN_BATTERY_CHARGING_STATUS, input_power_good_pin = PIN_BATTERY_INPUT_POWER_GOOD;
   am_hal_gpio_interrupt_register(AM_HAL_GPIO_INT_CHANNEL_0, charging_status_pin, NULL, NULL);
   am_hal_gpio_interrupt_register(AM_HAL_GPIO_INT_CHANNEL_0, input_power_good_pin, NULL, NULL);
   configASSERT0(am_hal_gpio_interrupt_control(AM_HAL_GPIO_INT_CHANNEL_0, AM_HAL_GPIO_INT_CTRL_INDV_DISABLE, &charging_status_pin));
   configASSERT0(am_hal_gpio_interrupt_control(AM_HAL_GPIO_INT_CHANNEL_0, AM_HAL_GPIO_INT_CTRL_INDV_DISABLE, &input_power_good_pin));
}

void battery_register_event_callback(battery_event_callback_t callback)
{
   event_callback = callback;
}

void battery_monitor_poll_charger_state(void)
{
   // Flush any deferred change from the de-bounce window
   const bool is_plugged_in = battery_monitor_is_plugged_in();
   if (signal_change_accepted(&plugged_signal, is_plugged_in) && event_callback)
      event_callback(is_plugged_in ? BATTERY_PLUGGED : BATTERY_UNPLUGGED);

   const bool is_charging = battery_monitor_is_charging();
   if (signal_change_accepted(&charging_signal, is_charging) && event_callback)
      event_callback(is_charging ? BATTERY_CHARGING : BATTERY_NOT_CHARGING);
}

uint32_t battery_monitor_get_suppressed_edge_count(void)
{
   return suppressed_edge_count;
}

bool battery_monitor_has_brownout_detection(void)
{
   // Reports whether this board can deliver BATTERY_CRITICAL_VOLTAGE ahead of a collapse
#ifdef PIN_BATTERY_VOLTAGE_COMPARATOR
   return true;
#else
   return false;
#endif
}

uint32_t battery_monitor_get_level_mV(void)
{
   // A failed read reports the last good reading rather than zero
   if (!acquire_adc())
      return last_valid_voltage_mV;

   // Wake up the ADC
   battery_voltage_code = 0;
   conversion_complete = false;
   if (am_hal_adc_power_control(adc_handle, AM_HAL_SYSCTRL_WAKE, true) != AM_HAL_STATUS_SUCCESS)
   {
      release_adc();
      return last_valid_voltage_mV;
   }

   // Enable interrupts upon completion of an ADC conversion
   am_hal_adc_interrupt_enable(adc_handle, AM_HAL_ADC_INT_CNVCMP);
   NVIC_SetPriority(ADC_IRQn, NVIC_configMAX_SYSCALL_INTERRUPT_PRIORITY + 2);
   NVIC_EnableIRQ(ADC_IRQn);

   // Enable the ADC
   if ((am_hal_adc_enable(adc_handle) != AM_HAL_STATUS_SUCCESS) || am_hal_adc_sw_trigger(adc_handle))
   {
      shut_down_adc();
      release_adc();
      return last_valid_voltage_mV;
   }

   // Wait until the conversion has completed
   const uint32_t poll_interval_us = 200;
   const uint32_t polls = (1000 * BATTERY_ADC_TIMEOUT_MS) / poll_interval_us;
   const bool can_yield = (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) && !xPortIsInsideInterrupt();
   for (uint32_t poll = 0; !conversion_complete && (poll < polls); ++poll)
   {
      if (can_yield && ((poll % 50) == 49))
         vTaskDelay(1);
      else
         am_hal_delay_us(poll_interval_us);
   }

   // Disable the ADC
   const bool converted = conversion_complete;
   const uint32_t code = battery_voltage_code;
   shut_down_adc();
   release_adc();
   if (!converted)
      return last_valid_voltage_mV;

   // Calculate and return the battery voltage
   last_valid_voltage_mV = (code * AM_HAL_ADC_VREFMV / 4096) * (VOLTAGE_DIVIDER_UPPER + VOLTAGE_DIVIDER_LOWER) / VOLTAGE_DIVIDER_LOWER;
   return last_valid_voltage_mV;
}

bool battery_monitor_is_plugged_in(void)
{
   // Return the current plugged-in status of the battery
   uint32_t status = 0;
   return (am_hal_gpio_state_read(PIN_BATTERY_INPUT_POWER_GOOD, AM_HAL_GPIO_INPUT_READ, &status) == AM_HAL_STATUS_SUCCESS) && !status;
}

bool battery_monitor_is_charging(void)
{
   // Return the current charging status of the battery
   uint32_t status = 0;
   return (am_hal_gpio_state_read(PIN_BATTERY_CHARGING_STATUS, AM_HAL_GPIO_INPUT_READ, &status) == AM_HAL_STATUS_SUCCESS) && !status;
}
