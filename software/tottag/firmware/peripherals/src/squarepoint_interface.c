// Header inclusions ---------------------------------------------------------------------------------------------------

#include <string.h>
#include "ble_config.h"
#include "boards.h"
#include "nrf_delay.h"
#include "nrfx_twi.h"
#include "squarepoint_interface.h"


// Application connectivity variables ----------------------------------------------------------------------------------

static nrfx_atomic_flag_t *_squarepoint_interrupt_thrown = NULL;
static squarepoint_interface_data_callback _data_callback = NULL;


// SquarePoint interface state -----------------------------------------------------------------------------------------

static const nrfx_twi_t _twi_instance = NRFX_TWI_INSTANCE(1);
static bool _twi_initialized = false;
static uint8_t _twi_rx_buf[256];


// Private helper functions --------------------------------------------------------------------------------------------

static void twi_bus_clear(void)
{
   // Generate a STOP condition on the TWI bus
   nrf_gpio_cfg(CARRIER_I2C_SCL, NRF_GPIO_PIN_DIR_INPUT, NRF_GPIO_PIN_INPUT_CONNECT, NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_S0D1, NRF_GPIO_PIN_NOSENSE);
   nrf_gpio_cfg(CARRIER_I2C_SDA, NRF_GPIO_PIN_DIR_INPUT, NRF_GPIO_PIN_INPUT_CONNECT, NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_S0D1, NRF_GPIO_PIN_NOSENSE);
   nrf_gpio_pin_set(CARRIER_I2C_SCL);
   nrf_gpio_pin_set(CARRIER_I2C_SDA);

   // Temporarily set both TWI lines to be outputs
   nrf_gpio_cfg(CARRIER_I2C_SCL, NRF_GPIO_PIN_DIR_OUTPUT, NRF_GPIO_PIN_INPUT_CONNECT, NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_S0D1, NRF_GPIO_PIN_NOSENSE);
   nrf_gpio_cfg(CARRIER_I2C_SDA, NRF_GPIO_PIN_DIR_OUTPUT, NRF_GPIO_PIN_INPUT_CONNECT, NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_S0D1, NRF_GPIO_PIN_NOSENSE);

   // Attempt to clock through the problem
   nrf_delay_us(4);
   for (int i = 0; i < 9; ++i)
   {
      // If the SDA line contains a logic 0, the problem has been resolved
      if (nrf_gpio_pin_read(CARRIER_I2C_SDA))
      {
         if (i == 0)
            return;
         else
            break;
      }

      // Generate a clock pulse
      nrf_gpio_pin_clear(CARRIER_I2C_SCL);
      nrf_delay_us(4);
      nrf_gpio_pin_set(CARRIER_I2C_SCL);
      nrf_delay_us(4);
   }

   // Generate a STOP condition on the TWI bus
   nrf_gpio_pin_clear(CARRIER_I2C_SDA);
   nrf_delay_us(4);
   nrf_gpio_pin_set(CARRIER_I2C_SDA);
}

static void squarepoint_interrupt_handler(nrfx_gpiote_pin_t pin, nrf_gpiote_polarity_t action)
{
   // Verify that the interrupt is from the SquarePoint module
   if (pin != CARRIER_INTERRUPT_MODULE)
      return;

   // Read the length of the incoming packet and ensure that it is valid
   uint8_t cmd = SQUAREPOINT_CMD_READ_PACKET_LENGTH;
   if ((nrfx_twi_tx(&_twi_instance, SQUAREPOINT_ADDRESS, &cmd, sizeof(cmd), false) != NRFX_SUCCESS) ||
         (nrfx_twi_rx(&_twi_instance, SQUAREPOINT_ADDRESS, _twi_rx_buf, 1) != NRFX_SUCCESS) ||
         !_twi_rx_buf[0] || (_twi_rx_buf[0] == 0xFF))
      return;

   // Read the rest of the incoming packet
   cmd = SQUAREPOINT_CMD_READ_PACKET;
   if ((nrfx_twi_tx(&_twi_instance, SQUAREPOINT_ADDRESS, &cmd, sizeof(cmd), false) == NRFX_SUCCESS) &&
         (nrfx_twi_rx(&_twi_instance, SQUAREPOINT_ADDRESS, _twi_rx_buf + 1, _twi_rx_buf[0]) == NRFX_SUCCESS))
      nrfx_atomic_flag_set(_squarepoint_interrupt_thrown);
   else
      printf("ERROR: Failed reading SquarePoint packet of length %i\n", _twi_rx_buf[0]);
}

static nrfx_err_t twi_hw_init(void)
{
   // Attempt to unlock the I2C bus in case it is stuck in an unexpected state
   twi_bus_clear();

   // Configure the TWI Master peripheral
   nrfx_twi_config_t twi_config = NRFX_TWI_DEFAULT_CONFIG;
   twi_config.scl = CARRIER_I2C_SCL;
   twi_config.sda = CARRIER_I2C_SDA;
   twi_config.frequency = NRF_TWI_FREQ_400K;
   twi_config.interrupt_priority = APP_IRQ_PRIORITY_HIGHEST;
   APP_ERROR_CHECK(nrfx_twi_init(&_twi_instance, &twi_config, NULL, NULL));
   nrfx_twi_enable(&_twi_instance);

   // Setup an interrupt handler to detect when SquarePoint has data to send
   nrfx_gpiote_in_config_t int_config = NRFX_GPIOTE_CONFIG_IN_SENSE_LOTOHI(1);
   nrfx_err_t err_code = nrfx_gpiote_in_init(CARRIER_INTERRUPT_MODULE, &int_config, squarepoint_interrupt_handler);
   if (err_code == NRFX_SUCCESS)
   {
      nrfx_gpiote_in_event_enable(CARRIER_INTERRUPT_MODULE, true);
      _twi_initialized = true;
   }
   else
      nrfx_twi_uninit(&_twi_instance);
   return err_code;
}

static void twi_hw_uninit(void)
{
   // Uninitialize the interrupt handler and the TWI Master peripheral
   nrfx_gpiote_in_uninit(CARRIER_INTERRUPT_MODULE);
   nrfx_twi_uninit(&_twi_instance);
   _twi_initialized = false;
}

static nrfx_err_t squarepoint_get_info(uint16_t *id, uint8_t *version)
{
   // Requesting a device info string from the SquarePoint module
   uint8_t cmd = SQUAREPOINT_CMD_INFO, resp[3] = { 0 };
   nrfx_err_t err_code = nrfx_twi_tx(&_twi_instance, SQUAREPOINT_ADDRESS, &cmd, sizeof(cmd), false);
   if (err_code == NRFX_SUCCESS)
      err_code = nrfx_twi_rx(&_twi_instance, SQUAREPOINT_ADDRESS, resp, sizeof(resp));
   if (err_code == NRFX_SUCCESS)
   {
      *id = (uint16_t)((uint16_t)resp[0] << (uint8_t)8) | resp[1];
      *version = resp[2];
   }
   else
      *id = *version = 0;
   return err_code;
}


// Public SquarePoint Interface API ------------------------------------------------------------------------------------

nrfx_err_t squarepoint_init(nrfx_atomic_flag_t* incoming_data_flag, squarepoint_interface_data_callback callback)
{
   // Ensure that the TWI peripheral hardware is initialized
   if (!_twi_initialized && twi_hw_init())
      return NRFX_ERROR_INTERNAL;

   // Setup connections to the application
   _squarepoint_interrupt_thrown = incoming_data_flag;
   _data_callback = callback;

   // Try to read information from SquarePoint to validate the TWI connection
   uint16_t id = 0;
   uint8_t version = 0;
   if (squarepoint_get_info(&id, &version) != NRFX_SUCCESS)
   {
      twi_hw_uninit();
      return NRFX_ERROR_INTERNAL;
   }
   if (id != SQUAREPOINT_ID)
      return NRFX_ERROR_INVALID_STATE;
   return NRFX_SUCCESS;
}

nrfx_err_t squarepoint_start_application(uint32_t current_time, uint8_t device_role, uint8_t scheduler_role)
{
   // Send an outgoing command requesting that the SquarePoint module start
   printf("INFO: Starting SquarePoint module as %s with timestamp %lu\n", (scheduler_role == SCHEDULER) ? "SCHEDULER" : "PARTICIPANT", current_time);
   uint8_t cmd[6] = { SQUAREPOINT_CMD_START, device_role | (scheduler_role << 3) | (SQUAREPOINT_RUNTIME_MODE_STANDARD << 6), 0, 0, 0, 0 };
   memcpy(cmd + 2, &current_time, sizeof(current_time));
   return nrfx_twi_tx(&_twi_instance, SQUAREPOINT_ADDRESS, cmd, sizeof(cmd), false);
}

nrfx_err_t squarepoint_start_calibration(uint8_t index)
{
   // Send an outgoing command requesting that the SquarePoint module enter calibration mode
   uint8_t cmd[3] = { SQUAREPOINT_CMD_START, (SQUAREPOINT_RUNTIME_MODE_CALIBRATION << 6), index };
   return nrfx_twi_tx(&_twi_instance, SQUAREPOINT_ADDRESS, cmd, sizeof(cmd), false);
}

nrfx_err_t squarepoint_get_calibration(uint8_t *calib_buf)
{
   // Send an outgoing command requesting the SquarePoint calibration info
   uint8_t cmd = SQUAREPOINT_CMD_READ_CALIBRATION;
   nrfx_err_t err_code = nrfx_twi_tx(&_twi_instance, SQUAREPOINT_ADDRESS, &cmd, sizeof(cmd), false);
   if (err_code == NRFX_SUCCESS)
      err_code = nrfx_twi_rx(&_twi_instance, SQUAREPOINT_ADDRESS, calib_buf, 18);
   return err_code;
}

nrfx_err_t squarepoint_stop(void)
{
   // Send an outgoing command requesting that the SquarePoint module stop
   uint8_t cmd = SQUAREPOINT_CMD_STOP;
   return nrfx_twi_tx(&_twi_instance, SQUAREPOINT_ADDRESS, &cmd, sizeof(cmd), false);
}

nrfx_err_t squarepoint_set_time(uint32_t epoch)
{
   // Send an outgoing command requesting that the SquarePoint module reset its current time epoch
   uint8_t cmd[5] = { SQUAREPOINT_CMD_SET_TIME, (epoch >> 3 * 8) & 0xFF, (epoch >> 2 * 8) & 0xFF, (epoch >> 1 * 8) & 0xFF, (epoch >> 0 * 8) & 0xFF };
   return nrfx_twi_tx(&_twi_instance, SQUAREPOINT_ADDRESS, cmd, sizeof(cmd), false);
}

nrfx_err_t squarepoint_wakeup_module(void)
{
   // Ensure that a valid SquarePoint connection exists
   if (!_twi_initialized)
      return NRFX_ERROR_BUSY;

   // Reverse the direction of the module interrupt to wake up SquarePoint
   _twi_initialized = false;
   nrfx_gpiote_in_uninit(CARRIER_INTERRUPT_MODULE);
   nrfx_gpiote_out_config_t out_config = NRFX_GPIOTE_CONFIG_OUT_SIMPLE(0);
   nrfx_err_t err_code = nrfx_gpiote_out_init(CARRIER_INTERRUPT_MODULE, &out_config);
   if (err_code == NRFX_SUCCESS)
      nrfx_gpiote_out_set(CARRIER_INTERRUPT_MODULE);
   nrfx_gpiote_out_uninit(CARRIER_INTERRUPT_MODULE);
   nrfx_twi_uninit(&_twi_instance);
   err_code = twi_hw_init();

   // Give the SquarePoint module some time to re-initialize
   nrf_delay_ms(2);
   return err_code;
}

nrfx_err_t squarepoint_wakeup_radio(void)
{
   // Send an outgoing command requesting that the SquarePoint module's radio wakeup
   uint8_t cmd = SQUAREPOINT_CMD_WAKEUP;
   return nrfx_twi_tx(&_twi_instance, SQUAREPOINT_ADDRESS, &cmd, sizeof(cmd), false);
}

nrfx_err_t squarepoint_ack(void)
{
   // Send an outgoing command responding to a ping request
   uint8_t cmd = SQUAREPOINT_CMD_ACK;
   return nrfx_twi_tx(&_twi_instance, SQUAREPOINT_ADDRESS, &cmd, sizeof(cmd), false);
}

void squarepoint_handle_incoming_data(void)
{
   // Call the user callback
   if (_data_callback)
      _data_callback(_twi_rx_buf + 1, _twi_rx_buf[0]);
}
