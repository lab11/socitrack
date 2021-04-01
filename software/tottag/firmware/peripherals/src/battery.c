// Header inclusions ---------------------------------------------------------------------------------------------------

#include "battery.h"
#include "ble_config.h"
#include "nrfx_gpiote.h"
#include "sd_card.h"


// Battery monitoring state variables ----------------------------------------------------------------------------------

static nrfx_atomic_flag_t *_battery_status_changed = NULL;


// Private helper functions --------------------------------------------------------------------------------------------

#if (BOARD_V >= 0x10)
static void signal_charge_complete(bool charge_complete)
{
   // Update the charge complete signal to the wireless charger
   if (charge_complete)
   {
      // Set the "charge complete" line to HIGH
      nrfx_gpiote_out_config_t charge_complete_gpio_config = NRFX_GPIOTE_CONFIG_OUT_SIMPLE(1);
      nrfx_gpiote_out_init(CHARGER_COMPLETE_SIGAL, &charge_complete_gpio_config);
   }
   else
   {
      // Set the "charge complete" line to HIGH IMPEDANCE
      nrf_gpio_cfg_input(CHARGER_COMPLETE_SIGAL, NRF_GPIO_PIN_NOPULL);
   }
}

static void plugged_in_status_changed(nrfx_gpiote_pin_t pin, nrf_gpiote_polarity_t action)
{
   // Set the "battery status changed" flag for the battery monitor
   if (pin == CHARGER_INPUT_POWER_GOOD)
      nrfx_atomic_flag_set(_battery_status_changed);

   // Update the "charge complete signal" for the wireless charger
   signal_charge_complete(battery_monitor_is_plugged_in() && !battery_monitor_is_charging());
}

static void charging_status_changed(nrfx_gpiote_pin_t pin, nrf_gpiote_polarity_t action)
{
   // Set the "battery status changed" flag for the battery monitor
   if (pin == CHARGER_CHARGING_STATUS)
      nrfx_atomic_flag_set(_battery_status_changed);

   // Update the "charge complete signal" for the wireless charger
   signal_charge_complete(battery_monitor_is_plugged_in() && !battery_monitor_is_charging());
}
#endif // #if (BOARD_V >= 0x10)


// Battery monitoring functionality ------------------------------------------------------------------------------------

static void saadc_callback(nrfx_saadc_evt_t const * p_event) {}     // This should never be called

void battery_monitor_init(nrfx_atomic_flag_t* battery_status_changed_flag)
{
   // Enable and calibrate the SAADC
   nrfx_saadc_config_t saadc_config = NRFX_SAADC_DEFAULT_CONFIG;
   nrfx_err_t err_code = nrfx_saadc_init(&saadc_config, saadc_callback);
   APP_ERROR_CHECK(err_code);
   err_code = nrfx_saadc_calibrate_offset();
   APP_ERROR_CHECK(err_code);
   while (nrfx_saadc_is_busy())
      sd_app_evt_wait();

   // Configure and enable the SAADC comparison channel
   nrf_saadc_channel_config_t saadc_channel_config = NRFX_SAADC_DEFAULT_CHANNEL_CONFIG_SE(CARRIER_BATTERY_PIN);
   err_code = nrfx_saadc_channel_init(BATTERY_MONITOR_CHANNEL, &saadc_channel_config);
   APP_ERROR_CHECK(err_code);

   // Initialize the charging and plugged-in status interrupt pins
   _battery_status_changed = battery_status_changed_flag;
#if (BOARD_V >= 0x10)
   nrfx_gpiote_in_config_t charging_gpio_config = NRFX_GPIOTE_CONFIG_IN_SENSE_TOGGLE(0);
   charging_gpio_config.pull = NRF_GPIO_PIN_PULLUP;
   nrfx_gpiote_in_init(CHARGER_CHARGING_STATUS, &charging_gpio_config, charging_status_changed);
   nrfx_gpiote_in_event_enable(CHARGER_CHARGING_STATUS, 1);
   nrfx_gpiote_in_config_t plugged_in_gpio_config = NRFX_GPIOTE_CONFIG_IN_SENSE_TOGGLE(0);
   plugged_in_gpio_config.pull = NRF_GPIO_PIN_PULLUP;
   nrfx_gpiote_in_init(CHARGER_INPUT_POWER_GOOD, &plugged_in_gpio_config, plugged_in_status_changed);
   nrfx_gpiote_in_event_enable(CHARGER_INPUT_POWER_GOOD, 1);
#endif
}

uint16_t battery_monitor_get_level_mV(void)
{
   // Get an ADC voltage sample
   nrf_saadc_value_t adc_sample;
   nrfx_err_t err_code = nrfx_saadc_sample_convert(BATTERY_MONITOR_CHANNEL, &adc_sample);
   if (err_code != NRFX_SUCCESS)
   {
      log_printf("WARNING: ADC is busy and cannot sample the battery voltage\n");
      return 0;
   }

   // Convert the ADC sample to a voltage reading in mV
   //   Calculation based on 600mV reference voltage with a gain of 1/6 and 10 bit return value
   uint16_t voltage_sample = (adc_sample * 3600) >> 10;

   // Scale it by the voltage dividing circuit
   return (voltage_sample * (BATTERY_VOLTAGE_DIV_UP + BATTERY_VOLTAGE_DIV_DOWN) / BATTERY_VOLTAGE_DIV_DOWN);
}

bool battery_monitor_is_plugged_in(void)
{
   // Return the current plugged-in status of the battery
#if (BOARD_V >= 0x10)
   return !nrfx_gpiote_in_is_set(CHARGER_INPUT_POWER_GOOD);
#else
   return false;
#endif
}

bool battery_monitor_is_charging(void)
{
   // Return the current charging status of the battery
#if (BOARD_V >= 0x10)
   return !nrfx_gpiote_in_is_set(CHARGER_CHARGING_STATUS);
#else
   return false;
#endif
}
