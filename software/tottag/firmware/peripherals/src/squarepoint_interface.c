// Header inclusions ---------------------------------------------------------------------------------------------------

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wredundant-decls"
#include <string.h>
#include "ble_config.h"
#include "nrf_delay.h"
#include "nrfx_gpiote.h"
#include "nrfx_twi.h"
#include "sd_card.h"
#include "squarepoint_interface.h"
#pragma GCC diagnostic pop


// Application connectivity variables ----------------------------------------------------------------------------------

static nrfx_atomic_flag_t *_squarepoint_interrupt_thrown = NULL;
static squarepoint_interface_data_callback _data_callback = NULL;


// SquarePoint interface state -----------------------------------------------------------------------------------------

static const nrfx_twi_t _twi_instance = NRFX_TWI_INSTANCE(1);
static bool _twi_initialized = false;
static uint8_t _twi_rx_buf[256], _twi_rx_length;
static const nrfx_twi_xfer_desc_t _rx_length_description = { .type = NRFX_TWI_XFER_RX, .address = SQUAREPOINT_ADDRESS,
                                                             .p_primary_buf = &_twi_rx_length, .p_secondary_buf = NULL,
                                                             .primary_length = 1, .secondary_length = 0 };
static nrfx_twi_xfer_desc_t _rx_data_description = { .type = NRFX_TWI_XFER_RX, .address = SQUAREPOINT_ADDRESS,
                                                     .p_primary_buf = _twi_rx_buf, .p_secondary_buf = NULL,
                                                     .primary_length = 1, .secondary_length = 0 };


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
   if (pin != STM_INTERRUPT)
      return;

   // Read the length of the incoming packet and ensure that it is valid
   _twi_rx_buf[0] = SQUAREPOINT_CMD_READ_PACKET_LENGTH;
   nrfx_twi_xfer_desc_t tx_description = { .type = NRFX_TWI_XFER_TX, .address = SQUAREPOINT_ADDRESS,
                                           .p_primary_buf = _twi_rx_buf, .p_secondary_buf = NULL,
                                           .primary_length = 1, .secondary_length = 0 };
   if ((nrfx_twi_xfer(&_twi_instance, &tx_description, NRFX_TWI_FLAG_NO_XFER_EVT_HANDLER) != NRFX_SUCCESS) ||
       (nrfx_twi_xfer(&_twi_instance, &_rx_length_description, NRFX_TWI_FLAG_NO_XFER_EVT_HANDLER) != NRFX_SUCCESS) ||
       !_twi_rx_length || (_twi_rx_length == 0xFF))
   {
      log_printf("ERROR: Failed reading SquarePoint packet length\n");
      return;
   }

   // Read the rest of the incoming packet
   _twi_rx_buf[0] = SQUAREPOINT_CMD_READ_PACKET;
   _rx_data_description.primary_length = _twi_rx_length;
   if ((nrfx_twi_xfer(&_twi_instance, &tx_description, NRFX_TWI_FLAG_NO_XFER_EVT_HANDLER) == NRFX_SUCCESS) &&
       (nrfx_twi_xfer(&_twi_instance, &_rx_data_description, NRFX_TWI_FLAG_NO_XFER_EVT_HANDLER) == NRFX_SUCCESS))
      nrfx_atomic_flag_set(_squarepoint_interrupt_thrown);
   else
      log_printf("ERROR: Failed reading SquarePoint packet of length %i\n", _twi_rx_length);
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
   nrfx_err_t err_code = nrfx_gpiote_in_init(STM_INTERRUPT, &int_config, squarepoint_interrupt_handler);
   if (err_code == NRFX_SUCCESS)
   {
      nrfx_gpiote_in_event_enable(STM_INTERRUPT, true);
      _twi_initialized = true;
   }
   else
      nrfx_twi_uninit(&_twi_instance);
   return err_code;
}

static void twi_hw_uninit(void)
{
   // Uninitialize the interrupt handler and the TWI Master peripheral
   nrfx_gpiote_in_uninit(STM_INTERRUPT);
   nrfx_twi_uninit(&_twi_instance);
   _twi_initialized = false;
}

static nrfx_err_t squarepoint_get_info(uint16_t *id, uint8_t *version, const uint8_t *eui)
{
   // Requesting a device info string from the SquarePoint module
   _twi_rx_buf[0] = SQUAREPOINT_CMD_INFO;
   _twi_rx_buf[1] = eui[0]; _twi_rx_buf[2] = eui[1]; _twi_rx_buf[3] = eui[2];
   _twi_rx_buf[4] = eui[3]; _twi_rx_buf[5] = eui[4]; _twi_rx_buf[6] = eui[5];
   nrfx_twi_xfer_desc_t tx_description = { .type = NRFX_TWI_XFER_TX, .address = SQUAREPOINT_ADDRESS,
                                           .p_primary_buf = _twi_rx_buf, .p_secondary_buf = NULL,
                                           .primary_length = 7, .secondary_length = 0 },
                        rx_description = { .type = NRFX_TWI_XFER_RX, .address = SQUAREPOINT_ADDRESS,
                                           .p_primary_buf = _twi_rx_buf, .p_secondary_buf = NULL,
                                           .primary_length = 3, .secondary_length = 0 };
   nrfx_err_t err_code = nrfx_twi_xfer(&_twi_instance, &tx_description, NRFX_TWI_FLAG_NO_XFER_EVT_HANDLER);
   if (err_code == NRFX_SUCCESS)
      err_code = nrfx_twi_xfer(&_twi_instance, &rx_description, NRFX_TWI_FLAG_NO_XFER_EVT_HANDLER);
   if (err_code == NRFX_SUCCESS)
   {
      *id = (uint16_t)((uint16_t)_twi_rx_buf[0] << (uint8_t)8) | _twi_rx_buf[1];
      *version = _twi_rx_buf[2];
   }
   else
      *id = *version = 0;
   return err_code;
}


// Public SquarePoint Interface API ------------------------------------------------------------------------------------

nrfx_err_t squarepoint_init(nrfx_atomic_flag_t* incoming_data_flag, squarepoint_interface_data_callback callback, const uint8_t* eui)
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
   if (squarepoint_get_info(&id, &version, eui) != NRFX_SUCCESS)
   {
      twi_hw_uninit();
      return NRFX_ERROR_INTERNAL;
   }
   if (id != SQUAREPOINT_ID)
   {
      log_printf("ERROR: SquarePoint module is not reporting the expected ID [Expected = %uh Reported = %uh]\n", SQUAREPOINT_ID, id);
      return NRFX_ERROR_INVALID_STATE;
   }
   return NRFX_SUCCESS;
}

nrfx_err_t squarepoint_start_application(uint32_t current_time, uint8_t device_role, uint8_t scheduler_role)
{
   // Send an outgoing command requesting that the SquarePoint module start
   log_printf("INFO: Starting SquarePoint module as %s with timestamp %lu\n", (scheduler_role == SCHEDULER) ? "SCHEDULER" : "PARTICIPANT", current_time);
   _twi_rx_buf[0] = SQUAREPOINT_CMD_START;
   _twi_rx_buf[1] = device_role | (scheduler_role << 3) | (SQUAREPOINT_RUNTIME_MODE_STANDARD << 6);
   memcpy(_twi_rx_buf + 2, &current_time, sizeof(current_time));
   nrfx_twi_xfer_desc_t tx_description = { .type = NRFX_TWI_XFER_TX, .address = SQUAREPOINT_ADDRESS,
                                           .p_primary_buf = _twi_rx_buf, .p_secondary_buf = NULL,
                                           .primary_length = 6, .secondary_length = 0 };
   return nrfx_twi_xfer(&_twi_instance, &tx_description, NRFX_TWI_FLAG_NO_XFER_EVT_HANDLER);
}

nrfx_err_t squarepoint_start_calibration(uint8_t index)
{
   // Send an outgoing command requesting that the SquarePoint module enter calibration mode
   _twi_rx_buf[0] = SQUAREPOINT_CMD_START;
   _twi_rx_buf[1] = (SQUAREPOINT_RUNTIME_MODE_CALIBRATION << 6);
   _twi_rx_buf[2] = index;
   nrfx_twi_xfer_desc_t tx_description = { .type = NRFX_TWI_XFER_TX, .address = SQUAREPOINT_ADDRESS,
                                           .p_primary_buf = _twi_rx_buf, .p_secondary_buf = NULL,
                                           .primary_length = 3, .secondary_length = 0 };
   return nrfx_twi_xfer(&_twi_instance, &tx_description, NRFX_TWI_FLAG_NO_XFER_EVT_HANDLER);
}

nrfx_err_t squarepoint_get_calibration(uint8_t *calib_buf)
{
   // Send an outgoing command requesting the SquarePoint calibration info
   _twi_rx_buf[0] = SQUAREPOINT_CMD_READ_CALIBRATION;
   nrfx_twi_xfer_desc_t tx_description = { .type = NRFX_TWI_XFER_TX, .address = SQUAREPOINT_ADDRESS,
                                           .p_primary_buf = _twi_rx_buf, .p_secondary_buf = NULL,
                                           .primary_length = 1, .secondary_length = 0 },
                        rx_description = { .type = NRFX_TWI_XFER_RX, .address = SQUAREPOINT_ADDRESS,
                                           .p_primary_buf = calib_buf, .p_secondary_buf = NULL,
                                           .primary_length = 18, .secondary_length = 0 };
   nrfx_err_t err_code = nrfx_twi_xfer(&_twi_instance, &tx_description, NRFX_TWI_FLAG_NO_XFER_EVT_HANDLER);
   if (err_code == NRFX_SUCCESS)
      err_code = nrfx_twi_xfer(&_twi_instance, &rx_description, NRFX_TWI_FLAG_NO_XFER_EVT_HANDLER);
   return err_code;
}

nrfx_err_t squarepoint_stop(void)
{
   // Send an outgoing command requesting that the SquarePoint module stop
   _twi_rx_buf[0] = SQUAREPOINT_CMD_STOP;
   nrfx_twi_xfer_desc_t tx_description = { .type = NRFX_TWI_XFER_TX, .address = SQUAREPOINT_ADDRESS,
                                           .p_primary_buf = _twi_rx_buf, .p_secondary_buf = NULL,
                                           .primary_length = 1, .secondary_length = 0 };
   return nrfx_twi_xfer(&_twi_instance, &tx_description, NRFX_TWI_FLAG_NO_XFER_EVT_HANDLER);
}

nrfx_err_t squarepoint_set_time(uint32_t epoch)
{
   // Send an outgoing command requesting that the SquarePoint module reset its current time epoch
   _twi_rx_buf[0] = SQUAREPOINT_CMD_SET_TIME;
   _twi_rx_buf[1] = (epoch >> 3 * 8) & 0xFF;
   _twi_rx_buf[2] = (epoch >> 2 * 8) & 0xFF;
   _twi_rx_buf[3] = (epoch >> 1 * 8) & 0xFF;
   _twi_rx_buf[4] = (epoch >> 0 * 8) & 0xFF;
   nrfx_twi_xfer_desc_t tx_description = { .type = NRFX_TWI_XFER_TX, .address = SQUAREPOINT_ADDRESS,
                                           .p_primary_buf = _twi_rx_buf, .p_secondary_buf = NULL,
                                           .primary_length = 5, .secondary_length = 0 };
   return nrfx_twi_xfer(&_twi_instance, &tx_description, NRFX_TWI_FLAG_NO_XFER_EVT_HANDLER);
}

nrfx_err_t squarepoint_wakeup_module(void)
{
   // Ensure that a valid SquarePoint connection exists
   if (!_twi_initialized)
      return NRFX_ERROR_BUSY;

   // Reverse the direction of the module interrupt to wake up SquarePoint
   _twi_initialized = false;
   nrfx_gpiote_in_uninit(STM_INTERRUPT);
   nrfx_gpiote_out_config_t out_config = NRFX_GPIOTE_CONFIG_OUT_SIMPLE(1);
   nrfx_err_t err_code = nrfx_gpiote_out_init(STM_INTERRUPT, &out_config);
   if (err_code == NRFX_SUCCESS)
      nrfx_gpiote_out_clear(STM_INTERRUPT);
   nrfx_gpiote_out_uninit(STM_INTERRUPT);
   nrfx_twi_uninit(&_twi_instance);
   err_code = twi_hw_init();

   // Give the SquarePoint module some time to re-initialize
   nrf_delay_ms(5);
   return err_code;
}

nrfx_err_t squarepoint_wakeup_radio(void)
{
   // Send an outgoing command requesting that the SquarePoint module's radio wakeup
   _twi_rx_buf[0] = SQUAREPOINT_CMD_WAKEUP;
   nrfx_twi_xfer_desc_t tx_description = { .type = NRFX_TWI_XFER_TX, .address = SQUAREPOINT_ADDRESS,
                                           .p_primary_buf = _twi_rx_buf, .p_secondary_buf = NULL,
                                           .primary_length = 1, .secondary_length = 0 };
   return nrfx_twi_xfer(&_twi_instance, &tx_description, NRFX_TWI_FLAG_NO_XFER_EVT_HANDLER);
}

nrfx_err_t squarepoint_ack(void)
{
   // Send an outgoing command responding to a ping request
   _twi_rx_buf[0] = SQUAREPOINT_CMD_ACK;
   nrfx_twi_xfer_desc_t tx_description = { .type = NRFX_TWI_XFER_TX, .address = SQUAREPOINT_ADDRESS,
                                           .p_primary_buf = _twi_rx_buf, .p_secondary_buf = NULL,
                                           .primary_length = 1, .secondary_length = 0 };
   return nrfx_twi_xfer(&_twi_instance, &tx_description, NRFX_TWI_FLAG_NO_XFER_EVT_HANDLER);
}

void squarepoint_handle_incoming_data(void)
{
   // Call the user callback
   if (_data_callback)
      _data_callback(_twi_rx_buf, _twi_rx_length);
}
