// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "battery.h"


// Static Global Variables ---------------------------------------------------------------------------------------------

#define BATTERY_ADC_SLOT 0

static battery_event_callback_t event_callback;
static volatile uint32_t battery_voltage_code;
static volatile bool conversion_complete;
static void *adc_handle;


// Private Helper Functions --------------------------------------------------------------------------------------------

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
   bool is_plugged_in = battery_monitor_is_plugged_in();

   // Toggle the interrupt direction (dual-edge interrupts not available due to errata)
   am_hal_gpio_pincfg_t pin_config = AM_HAL_GPIO_PINCFG_INPUT;
   pin_config.GP.cfg_b.ePullup = AM_HAL_GPIO_PIN_PULLUP_100K;
   pin_config.GP.cfg_b.eIntDir = is_plugged_in ? AM_HAL_GPIO_PIN_INTDIR_LO2HI : AM_HAL_GPIO_PIN_INTDIR_HI2LO;
   am_hal_gpio_pinconfig(PIN_BATTERY_INPUT_POWER_GOOD, pin_config);

   // Update the "charge complete signal" for the wireless charger
   signal_charge_complete(is_plugged_in && !battery_monitor_is_charging());

   // Call a registered battery event callback if applicable
   if (event_callback)
      event_callback(is_plugged_in ? BATTERY_PLUGGED : BATTERY_UNPLUGGED);
}

static void charging_status_changed(void *args)
{
   // Retrieve the current charging statuses of the charger
   bool is_charging = battery_monitor_is_charging();

   // Toggle the interrupt direction (dual-edge interrupts not available due to errata)
   am_hal_gpio_pincfg_t pin_config = AM_HAL_GPIO_PINCFG_INPUT;
   pin_config.GP.cfg_b.ePullup = AM_HAL_GPIO_PIN_PULLUP_100K;
   pin_config.GP.cfg_b.eIntDir = is_charging ? AM_HAL_GPIO_PIN_INTDIR_LO2HI : AM_HAL_GPIO_PIN_INTDIR_HI2LO;
   am_hal_gpio_pinconfig(PIN_BATTERY_CHARGING_STATUS, pin_config);

   // Update the "charge complete signal" for the wireless charger
   signal_charge_complete(battery_monitor_is_plugged_in() && !is_charging);

   // Call a registered battery event callback if applicable
   if (event_callback)
      event_callback(is_charging ? BATTERY_CHARGING : BATTERY_NOT_CHARGING);
}

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
   NVIC_SetPriority(GPIO0_001F_IRQn + GPIO_NUM2IDX(PIN_BATTERY_INPUT_POWER_GOOD), AM_IRQ_PRIORITY_DEFAULT);
   NVIC_SetPriority(GPIO0_001F_IRQn + GPIO_NUM2IDX(PIN_BATTERY_CHARGING_STATUS), AM_IRQ_PRIORITY_DEFAULT);
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
}

void battery_monitor_deinit(void)
{
   // Deinitialize the ADC module
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

uint32_t battery_monitor_get_level_mV(void)
{
   // Wake up the ADC
   conversion_complete = false;
   if (am_hal_adc_power_control(adc_handle, AM_HAL_SYSCTRL_WAKE, true) != AM_HAL_STATUS_SUCCESS)
      return 0;

   // Enable interrupts upon completion of an ADC conversion
   am_hal_adc_interrupt_enable(adc_handle, AM_HAL_ADC_INT_CNVCMP);
   NVIC_SetPriority(ADC_IRQn, AM_IRQ_PRIORITY_DEFAULT);
   NVIC_EnableIRQ(ADC_IRQn);

   // Enable the ADC
   if ((am_hal_adc_enable(adc_handle) != AM_HAL_STATUS_SUCCESS) || am_hal_adc_sw_trigger(adc_handle))
   {
      am_hal_adc_interrupt_disable(adc_handle, AM_HAL_ADC_INT_CNVCMP);
      am_hal_adc_power_control(adc_handle, AM_HAL_SYSCTRL_DEEPSLEEP, true);
      NVIC_DisableIRQ(ADC_IRQn);
      return 0;
   }

   // Wait until the conversion has completed
   while (!conversion_complete)
      am_hal_sysctrl_sleep(AM_HAL_SYSCTRL_SLEEP_NORMAL);

   // Disable the ADC
   am_hal_adc_interrupt_disable(adc_handle, AM_HAL_ADC_INT_CNVCMP);
   am_hal_adc_power_control(adc_handle, AM_HAL_SYSCTRL_DEEPSLEEP, true);
   NVIC_DisableIRQ(ADC_IRQn);

   // Calculate and return the battery voltage
   return (battery_voltage_code * AM_HAL_ADC_VREFMV / 4096) * (VOLTAGE_DIVIDER_UPPER + VOLTAGE_DIVIDER_LOWER) / VOLTAGE_DIVIDER_LOWER;
}

bool battery_monitor_is_plugged_in(void)
{
   // Return the current plugged-in status of the battery
   static uint32_t status;
   if (am_hal_gpio_state_read(PIN_BATTERY_INPUT_POWER_GOOD, AM_HAL_GPIO_INPUT_READ, &status) == AM_HAL_STATUS_SUCCESS)
      return (status == 0);
   else
      return false;
}

bool battery_monitor_is_charging(void)
{
   // Return the current charging status of the battery
   static uint32_t status;
   if (am_hal_gpio_state_read(PIN_BATTERY_CHARGING_STATUS, AM_HAL_GPIO_INPUT_READ, &status) == AM_HAL_STATUS_SUCCESS)
      return (status == 0);
   else
      return false;
}
