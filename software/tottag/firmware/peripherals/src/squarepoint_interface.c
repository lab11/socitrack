// Header inclusions ---------------------------------------------------------------------------------------------------

#include "nrf_delay.h"
#include "nrfx_twi.h"
#include "squarepoint_interface.h"


// Application connectivity variables ----------------------------------------------------------------------------------

static nrfx_atomic_u32_t _squarepoint_interrupt_count = 0;
static nrfx_atomic_flag_t _squarepoint_initialized = false;
static squarepoint_interface_data_callback _data_callback = NULL;


// SquarePoint interface state -----------------------------------------------------------------------------------------

#define MAX_NUM_SQUAREPOINT_INTERRUPTS 4

static const nrfx_twi_t _twi_instance = NRFX_TWI_INSTANCE(1);
static uint8_t _twi_tx_buf[32], _twi_rx_bufs[MAX_NUM_SQUAREPOINT_INTERRUPTS][256], _twi_rx_lens[MAX_NUM_SQUAREPOINT_INTERRUPTS];
static nrfx_twi_xfer_desc_t _tx_data_description = { .type = NRFX_TWI_XFER_TX, .address = SQUAREPOINT_ADDRESS,
                                                     .p_primary_buf = _twi_tx_buf, .p_secondary_buf = NULL,
                                                     .primary_length = 1, .secondary_length = 0 };
static nrfx_twi_xfer_desc_t _rx_data_descriptions[MAX_NUM_SQUAREPOINT_INTERRUPTS];
static nrfx_twi_xfer_desc_t _rx_length_descriptions[MAX_NUM_SQUAREPOINT_INTERRUPTS];


// Private helper functions --------------------------------------------------------------------------------------------

static void twi_bus_clear(void)
{
   // Generate a STOP condition on the TWI bus
   nrf_gpio_cfg(STM_I2C_SCL, NRF_GPIO_PIN_DIR_INPUT, NRF_GPIO_PIN_INPUT_CONNECT, NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_S0D1, NRF_GPIO_PIN_NOSENSE);
   nrf_gpio_cfg(STM_I2C_SDA, NRF_GPIO_PIN_DIR_INPUT, NRF_GPIO_PIN_INPUT_CONNECT, NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_S0D1, NRF_GPIO_PIN_NOSENSE);
   nrf_gpio_pin_set(STM_I2C_SCL);
   nrf_gpio_pin_set(STM_I2C_SDA);

   // Temporarily set both TWI lines to be outputs
   nrf_gpio_cfg(STM_I2C_SCL, NRF_GPIO_PIN_DIR_OUTPUT, NRF_GPIO_PIN_INPUT_CONNECT, NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_S0D1, NRF_GPIO_PIN_NOSENSE);
   nrf_gpio_cfg(STM_I2C_SDA, NRF_GPIO_PIN_DIR_OUTPUT, NRF_GPIO_PIN_INPUT_CONNECT, NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_S0D1, NRF_GPIO_PIN_NOSENSE);

   // Attempt to clock through the problem
   nrf_delay_us(4);
   for (int i = 0; i < 9; ++i)
   {
      // If the SDA line contains a logic 0, the problem has been resolved
      if (nrf_gpio_pin_read(STM_I2C_SDA))
      {
         if (i == 0)
            return;
         else
            break;
      }

      // Generate a clock pulse
      nrf_gpio_pin_clear(STM_I2C_SCL);
      nrf_delay_us(4);
      nrf_gpio_pin_set(STM_I2C_SCL);
      nrf_delay_us(4);
   }

   // Generate a STOP condition on the TWI bus
   nrf_gpio_pin_clear(STM_I2C_SDA);
   nrf_delay_us(4);
   nrf_gpio_pin_set(STM_I2C_SDA);
}

static void squarepoint_interrupt_handler(nrfx_gpiote_pin_t pin, nrf_gpiote_polarity_t action)
{
   // Verify that the interrupt is from the SquarePoint module
   if (pin == STM_INTERRUPT)
   {
      // Read the length of the incoming packet and ensure that it is valid
      _tx_data_description.primary_length = 1;
      _twi_tx_buf[0] = SQUAREPOINT_CMD_READ_PACKET_LENGTH;
      uint32_t idx = (uint32_t)MIN(MAX_NUM_SQUAREPOINT_INTERRUPTS - 1, nrfx_atomic_u32_fetch(&_squarepoint_interrupt_count));
      if ((nrfx_twi_xfer(&_twi_instance, &_tx_data_description, NRFX_TWI_FLAG_NO_XFER_EVT_HANDLER) != NRFX_SUCCESS) ||
          (nrfx_twi_xfer(&_twi_instance, &_rx_length_descriptions[idx], NRFX_TWI_FLAG_NO_XFER_EVT_HANDLER) != NRFX_SUCCESS) ||
          !_twi_rx_lens[idx] || (_twi_rx_lens[idx] == 0xFF))
      {
         log_printf("ERROR: Failed reading SquarePoint packet length\n");
         return;
      }

      // Read the rest of the incoming packet
      _twi_tx_buf[0] = SQUAREPOINT_CMD_READ_PACKET;
      _rx_data_descriptions[idx].primary_length = _twi_rx_lens[idx];
      if ((nrfx_twi_xfer(&_twi_instance, &_tx_data_description, NRFX_TWI_FLAG_NO_XFER_EVT_HANDLER) == NRFX_SUCCESS) &&
          (nrfx_twi_xfer(&_twi_instance, &_rx_data_descriptions[idx], NRFX_TWI_FLAG_NO_XFER_EVT_HANDLER) == NRFX_SUCCESS))
         nrfx_atomic_u32_store(&_squarepoint_interrupt_count, idx + 1);
      else
         log_printf("ERROR: Failed reading SquarePoint packet of length %i\n", _twi_rx_lens[idx]);
   }
}

static nrfx_err_t twi_hw_init(void)
{
   // Attempt to unlock the I2C bus in case it is stuck in an unexpected state
   twi_bus_clear();

   // Configure the TWI Master peripheral
   nrfx_twi_config_t twi_config = NRFX_TWI_DEFAULT_CONFIG;
   twi_config.scl = STM_I2C_SCL;
   twi_config.sda = STM_I2C_SDA;
   twi_config.frequency = NRF_TWI_FREQ_400K;
   twi_config.interrupt_priority = APP_IRQ_PRIORITY_HIGHEST;
   nrfx_twi_init(&_twi_instance, &twi_config, NULL, NULL);
   nrfx_twi_enable(&_twi_instance);

   // Setup an interrupt handler to detect when SquarePoint has data to send
   nrfx_gpiote_in_config_t int_config = NRFX_GPIOTE_CONFIG_IN_SENSE_LOTOHI(1);
   int_config.pull = NRF_GPIO_PIN_PULLDOWN;
   nrfx_err_t err_code = nrfx_gpiote_in_init(STM_INTERRUPT, &int_config, squarepoint_interrupt_handler);
   if (err_code == NRFX_SUCCESS)
      nrfx_gpiote_in_event_enable(STM_INTERRUPT, true);
   else
   {
      log_printf("ERROR: Unable to initialize SquarePoint interrupt GPIO pin; Error code = %lu\n", err_code);
      nrfx_twi_uninit(&_twi_instance);
   }
   return err_code;
}

static void twi_hw_uninit(void)
{
   // Uninitialize the interrupt handler and the TWI Master peripheral
   nrfx_gpiote_in_uninit(STM_INTERRUPT);
   nrfx_twi_uninit(&_twi_instance);
}

static nrfx_err_t squarepoint_get_info(uint16_t *id, uint8_t *version, const uint8_t *eui)
{
   // Requesting a device info string from the SquarePoint module
   uint8_t rx_buf[3];
   _twi_tx_buf[0] = SQUAREPOINT_CMD_INFO;
   _tx_data_description.primary_length = 7;
   _twi_tx_buf[1] = eui[0]; _twi_tx_buf[2] = eui[1]; _twi_tx_buf[3] = eui[2];
   _twi_tx_buf[4] = eui[3]; _twi_tx_buf[5] = eui[4]; _twi_tx_buf[6] = eui[5];
   nrfx_twi_xfer_desc_t rx_description = { .type = NRFX_TWI_XFER_RX, .address = SQUAREPOINT_ADDRESS,
                                           .p_primary_buf = rx_buf, .p_secondary_buf = NULL,
                                           .primary_length = 3, .secondary_length = 0 };
   nrfx_err_t err_code = nrfx_twi_xfer(&_twi_instance, &_tx_data_description, NRFX_TWI_FLAG_NO_XFER_EVT_HANDLER);
   if (err_code == NRFX_SUCCESS)
   {
      err_code = nrfx_twi_xfer(&_twi_instance, &rx_description, NRFX_TWI_FLAG_NO_XFER_EVT_HANDLER);
      if ((err_code != NRFX_SUCCESS) && (err_code != NRFX_ERROR_DRV_TWI_ERR_OVERRUN))
         log_printf("WARNING: Problem receiving SQUAREPOINT_CMD_INFO response; Error code = %lu\n", err_code);
   }
   else
      log_printf("WARNING: Problem sending SQUAREPOINT_CMD_INFO request; Error code = %lu\n", err_code);
   if ((err_code == NRFX_SUCCESS) || (err_code == NRFX_ERROR_DRV_TWI_ERR_OVERRUN))
   {
      *id = (uint16_t)((uint16_t)rx_buf[0] << (uint8_t)8) | rx_buf[1];
      *version = rx_buf[2];
   }
   else
      *id = *version = 0;
   return err_code;
}


// Public SquarePoint Interface API ------------------------------------------------------------------------------------

nrfx_err_t squarepoint_init(squarepoint_interface_data_callback callback, const uint8_t* eui)
{
   // Set up the SquarePoint wakeup pin
#if (BOARD_V >= 0x11)
   nrfx_gpiote_out_config_t stm_wakeup_pin_config = NRFX_GPIOTE_CONFIG_OUT_SIMPLE(0);
   APP_ERROR_CHECK(nrfx_gpiote_out_init(STM_WAKEUP, &stm_wakeup_pin_config));
#endif

   // Initialize all RX data descriptions
   if (!nrfx_atomic_flag_set_fetch(&_squarepoint_initialized))
      for (int i = 0; i < MAX_NUM_SQUAREPOINT_INTERRUPTS; ++i)
      {
         nrfx_twi_xfer_desc_t data_desc = { .type = NRFX_TWI_XFER_RX, .address = SQUAREPOINT_ADDRESS,
                                            .p_primary_buf = _twi_rx_bufs[i], .p_secondary_buf = NULL,
                                            .primary_length = 1, .secondary_length = 0 };
         nrfx_twi_xfer_desc_t len_desc = { .type = NRFX_TWI_XFER_RX, .address = SQUAREPOINT_ADDRESS,
                                           .p_primary_buf = &_twi_rx_lens[i], .p_secondary_buf = NULL,
                                           .primary_length = 1, .secondary_length = 0 };
         _rx_data_descriptions[i] = data_desc;
         _rx_length_descriptions[i] = len_desc;

      }

   // Determine if the SquarePoint module is already awake
   uint16_t id = 0;
   uint8_t version = 0;
   if (squarepoint_get_info(&id, &version, eui) != NRFX_SUCCESS)
   {
      // Reverse the direction of the interrupt line to wake up SquarePoint
      log_printf("INFO: Attempting to wake-up SquarePoint module and re-initiate communications\n");
      nrfx_gpiote_in_uninit(STM_INTERRUPT);
      nrfx_gpiote_out_config_t out_config = NRFX_GPIOTE_CONFIG_OUT_SIMPLE(1);
      nrfx_gpiote_out_init(STM_INTERRUPT, &out_config);
      twi_hw_uninit();
      nrf_delay_ms(50);

      // Ensure that the TWI peripheral hardware is initialized
      if (twi_hw_init() || squarepoint_get_info(&id, &version, eui))
      {
         log_printf("ERROR: Unable to initialize the SquarePoint TWI communications module\n");
         return NRFX_ERROR_INTERNAL;
      }
   }

   // Setup connections to the application
   _data_callback = callback;

   // Try to read information from SquarePoint to validate the TWI connection
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
   _twi_tx_buf[0] = SQUAREPOINT_CMD_START;
   _tx_data_description.primary_length = 6;
   _twi_tx_buf[1] = device_role | (scheduler_role << 3) | (SQUAREPOINT_RUNTIME_MODE_STANDARD << 6);
   memcpy(_twi_tx_buf + 2, &current_time, sizeof(current_time));
   return nrfx_twi_xfer(&_twi_instance, &_tx_data_description, NRFX_TWI_FLAG_NO_XFER_EVT_HANDLER);
}

nrfx_err_t squarepoint_start_calibration(uint8_t index)
{
   // Send an outgoing command requesting that the SquarePoint module enter calibration mode
   _twi_tx_buf[0] = SQUAREPOINT_CMD_START;
   _twi_tx_buf[1] = (SQUAREPOINT_RUNTIME_MODE_CALIBRATION << 6);
   _twi_tx_buf[2] = index;
   _tx_data_description.primary_length = 3;
   return nrfx_twi_xfer(&_twi_instance, &_tx_data_description, NRFX_TWI_FLAG_NO_XFER_EVT_HANDLER);
}

nrfx_err_t squarepoint_get_calibration(uint8_t *calib_buf)
{
   // Send an outgoing command requesting the SquarePoint calibration info
   _tx_data_description.primary_length = 1;
   _twi_tx_buf[0] = SQUAREPOINT_CMD_READ_CALIBRATION;
   nrfx_twi_xfer_desc_t rx_description = { .type = NRFX_TWI_XFER_RX, .address = SQUAREPOINT_ADDRESS,
                                           .p_primary_buf = calib_buf, .p_secondary_buf = NULL,
                                           .primary_length = 18, .secondary_length = 0 };
   nrfx_err_t err_code = nrfx_twi_xfer(&_twi_instance, &_tx_data_description, NRFX_TWI_FLAG_NO_XFER_EVT_HANDLER);
   if (err_code == NRFX_SUCCESS)
      err_code = nrfx_twi_xfer(&_twi_instance, &rx_description, NRFX_TWI_FLAG_NO_XFER_EVT_HANDLER);
   return err_code;
}

nrfx_err_t squarepoint_stop(void)
{
   // Send an outgoing command requesting that the SquarePoint module stop
   _twi_tx_buf[0] = SQUAREPOINT_CMD_STOP;
   _tx_data_description.primary_length = 1;
   return nrfx_twi_xfer(&_twi_instance, &_tx_data_description, NRFX_TWI_FLAG_NO_XFER_EVT_HANDLER);
}

nrfx_err_t squarepoint_set_time(uint32_t epoch)
{
   // Send an outgoing command requesting that the SquarePoint module reset its current time epoch
   _twi_tx_buf[0] = SQUAREPOINT_CMD_SET_TIME;
   _twi_tx_buf[1] = (epoch >> 3 * 8) & 0xFF;
   _twi_tx_buf[2] = (epoch >> 2 * 8) & 0xFF;
   _twi_tx_buf[3] = (epoch >> 1 * 8) & 0xFF;
   _twi_tx_buf[4] = (epoch >> 0 * 8) & 0xFF;
   _tx_data_description.primary_length = 5;
   return nrfx_twi_xfer(&_twi_instance, &_tx_data_description, NRFX_TWI_FLAG_NO_XFER_EVT_HANDLER);
}

nrfx_err_t squarepoint_wakeup_radio(void)
{
   // TODO: Utilize the SquarePoint wakeup pin

   // Send an outgoing command requesting that the SquarePoint module's radio wakeup
   _tx_data_description.primary_length = 1;
   _twi_tx_buf[0] = SQUAREPOINT_CMD_WAKEUP;
   return nrfx_twi_xfer(&_twi_instance, &_tx_data_description, NRFX_TWI_FLAG_NO_XFER_EVT_HANDLER);
}

nrfx_err_t squarepoint_ack(void)
{
   // Send an outgoing command responding to a ping request
   _twi_tx_buf[0] = SQUAREPOINT_CMD_ACK;
   _tx_data_description.primary_length = 1;
   return nrfx_twi_xfer(&_twi_instance, &_tx_data_description, NRFX_TWI_FLAG_NO_XFER_EVT_HANDLER);
}

uint32_t squarepoint_handle_incoming_data(uint32_t timestamp)
{
   // Call the user callback for all available data
   for (uint32_t i = 0; i < nrfx_atomic_u32_fetch(&_squarepoint_interrupt_count); ++i)
      if (_data_callback)
         timestamp = _data_callback(_twi_rx_bufs[i], _twi_rx_lens[i], timestamp);
   nrfx_atomic_u32_store(&_squarepoint_interrupt_count, 0);
   return timestamp;
}
