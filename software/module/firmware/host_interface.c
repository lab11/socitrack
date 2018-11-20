#include <stdio.h>
#include <string.h>

#include "stm32f0xx_i2c_cpal.h"
#include "stm32f0xx_i2c_cpal_hal.h"

#include "dw1000.h"

#include "SEGGER_RTT.h"

#include "board.h"
#include "firmware.h"
#include "host_interface.h"

#include "app_standard_common.h"
#include "app_calibration.h"
#include "glossy.h"


// STATE ---------------------------------------------------------------------------------------------------------------

#define BUFFER_SIZE 128
uint8_t rxBuffer[BUFFER_SIZE];
uint8_t txBuffer[BUFFER_SIZE];
uint8_t pending_tx = 0;


/* CPAL local transfer structures */
CPAL_TransferTypeDef rxStructure;
CPAL_TransferTypeDef txStructure;

// Just pre-set the INFO response packet.
// Last byte is the version. Set to 1 for now
uint8_t INFO_PKT[3] = {0xb0, 0x1a, 1};
// If we are not ready.
uint8_t NULL_PKT[3] = {0xaa, 0xaa, 0};

// Keep track of why we interrupted the host
interrupt_reason_e _interrupt_reason;
uint8_t* _interrupt_buffer;
uint8_t  _interrupt_buffer_len;

extern I2C_TypeDef* CPAL_I2C_DEVICE[];


// FUNCTIONS -----------------------------------------------------------------------------------------------------------

uint32_t host_interface_init () {
	uint32_t ret;

	// Enabled the Interrupt pin
	GPIO_InitTypeDef  GPIO_InitStructure;
	RCC_AHBPeriphClockCmd(INTERRUPT_CLK, ENABLE);

	GPIO_InitStructure.GPIO_Pin   = INTERRUPT_PIN;
	GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_OUT;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_NOPULL;
	GPIO_Init(INTERRUPT_PORT, &GPIO_InitStructure);
	INTERRUPT_PORT->BRR = INTERRUPT_PIN; // clear

	// Start CPAL communication configuration
	// Initialize local Reception structures
	rxStructure.wNumData = BUFFER_SIZE;   /* Maximum Number of data to be received */
	rxStructure.pbBuffer = rxBuffer;      /* Common Rx buffer for all received data */
	rxStructure.wAddr1   = 0;             /* Not needed */
	rxStructure.wAddr2   = 0;             /* Not needed */

	// Initialize local Transmission structures
	txStructure.wNumData = BUFFER_SIZE;   /* Maximum Number of data to be sent */
	txStructure.pbBuffer = txBuffer;      /* Common Tx buffer for all received data */
	txStructure.wAddr1   = (I2C_OWN_ADDRESS << 1); /* The own board address */
	txStructure.wAddr2   = 0;             /* Not needed */

	// Set SYSCLK as I2C clock source
	// RCC_I2CCLKConfig(RCC_I2C1CLK_SYSCLK);
	RCC_I2CCLKConfig(RCC_I2C1CLK_HSI);

	// Configure the device structure
	CPAL_I2C_StructInit(&I2C1_DevStructure);      /* Set all fields to default values */
	I2C1_DevStructure.CPAL_Dev       = 0;
	I2C1_DevStructure.CPAL_Direction = CPAL_DIRECTION_TXRX;
	I2C1_DevStructure.CPAL_Mode      = CPAL_MODE_SLAVE;
	I2C1_DevStructure.CPAL_State     = CPAL_STATE_READY;
	I2C1_DevStructure.wCPAL_Timeout  = 6;
	I2C1_DevStructure.wCPAL_Options  =  CPAL_OPT_NO_MEM_ADDR | CPAL_OPT_I2C_WAKEUP_STOP;
	// I2C1_DevStructure.wCPAL_Options =  0;
	I2C1_DevStructure.CPAL_ProgModel = CPAL_PROGMODEL_INTERRUPT;
	I2C1_DevStructure.pCPAL_I2C_Struct->I2C_Timing = I2C_TIMING;
	I2C1_DevStructure.pCPAL_I2C_Struct->I2C_OwnAddress1 = (I2C_OWN_ADDRESS << 1);
	I2C1_DevStructure.pCPAL_TransferRx = &rxStructure;
	I2C1_DevStructure.pCPAL_TransferTx = &txStructure;

	// Initialize CPAL device with the selected parameters
	ret = CPAL_I2C_Init(&I2C1_DevStructure);

	// See if this takes care of issues when STM is busy and can't respond
	// right away. It's also possible this was already configured.
	__CPAL_I2C_HAL_DISABLE_NOSTRETCH(0);

	return ret;
}

static void interrupt_host_set () {
	GPIO_WriteBit(INTERRUPT_PORT, INTERRUPT_PIN, Bit_SET);
}

static void interrupt_host_clear () {
	GPIO_WriteBit(INTERRUPT_PORT, INTERRUPT_PIN, Bit_RESET);
}

// Send the ranges to the host
void host_interface_notify_ranges (uint8_t* anchor_ids_ranges, uint8_t len) {

	// TODO: this should be in an atomic block

	// Save the relevant state for when the host asks for it
	_interrupt_reason = HOST_IFACE_INTERRUPT_RANGES;
	_interrupt_buffer = anchor_ids_ranges;
	_interrupt_buffer_len = len;

	// Let the host know it should ask
	interrupt_host_set();
}

// Send the raw ranges to the host for analysis
void host_interface_notify_ranges_raw (uint8_t* range_measurements) {

	// TODO: this should be in an atomic block

	// Save the relevant state for when the host asks for it
	_interrupt_reason = HOST_IFACE_INTERRUPT_RANGES_RAW;
	_interrupt_buffer = range_measurements;
	_interrupt_buffer_len = NUM_RANGING_BROADCASTS * sizeof(int);

	// Already copy here, as packet is too long to respond in the interrupt afterwards
    memcpy(txBuffer + 2, _interrupt_buffer, _interrupt_buffer_len);

	// Let the host know it should ask
	interrupt_host_set();
}

void host_interface_notify_calibration (uint8_t* calibration_data, uint8_t len) {

	// TODO: this should be in an atomic block

	// Save the relevant state for when the host asks for it
	_interrupt_reason = HOST_IFACE_INTERRUPT_CALIBRATION;
	_interrupt_buffer = calibration_data;
	_interrupt_buffer_len = len;

	// Let the host know it should ask
	interrupt_host_set();
}

void host_interface_notify_master_change (uint8_t* master_eui, uint8_t len) {

	// Save the relevant state for when the host asks for it
	_interrupt_reason = HOST_IFACE_INTERRUPT_MASTER_EUI;
	_interrupt_buffer = master_eui;
	_interrupt_buffer_len = len;

	// Let the host know it should ask
	interrupt_host_set();

}

// Doesn't block, but waits for an I2C master to initiate a WRITE.
uint32_t host_interface_wait () {
	uint32_t ret;

	// Setup the buffer to receive the contents of the WRITE in
	rxStructure.wNumData = BUFFER_SIZE;     // Maximum Number of data to be received
	rxStructure.pbBuffer = rxBuffer;        // Common Rx buffer for all received data

	// Device is ready, not clear if this is needed
	I2C1_DevStructure.CPAL_State = CPAL_STATE_READY;

	// Now wait for something to happen in slave mode.
	// Start waiting for data to be received in slave mode.
	ret = CPAL_I2C_Read(&I2C1_DevStructure);

	return ret;
}

// Wait for a READ from the master. Setup the buffers
uint32_t host_interface_respond (uint8_t length, bool fixed_length) {
	uint32_t ret;

	if (length > BUFFER_SIZE) {
		return CPAL_FAIL;
	}

	// Setup outgoing data
	if (pending_tx) {
		// From a previous response, we still have left overs; skip 'length' field and send the rest of the buffer
		txStructure.pbBuffer = txBuffer + 1;
	} else {
		txStructure.pbBuffer = txBuffer;
	}

	if (fixed_length) {
        txStructure.wNumData = length;
        pending_tx = 0;
	} else {
	    txStructure.wNumData = 1; // Only send 'length' field, second READ will get rest
	    pending_tx = length - (uint8_t)1; // Subtract 'length' field
	}

	// Device is ready, not clear if this is needed
	I2C1_DevStructure.CPAL_State = CPAL_STATE_READY;

	// Now wait for something to happen in slave mode.
	// Start waiting for data to be received in slave mode.
	ret = CPAL_I2C_Write(&I2C1_DevStructure);

	return ret;
}

// Called when the I2C interface receives a WRITE message on the bus.
// Based on what was received, either act or setup a response
void host_interface_rx_fired () {
	uint8_t opcode;

	// First byte of every correct WRITE packet is the opcode of the
	// packet.
	opcode = rxBuffer[0];
	switch (opcode) {

		/**********************************************************************/
		// Configure the module. This can be called multiple times to change the setup.
		/**********************************************************************/
		case HOST_CMD_CONFIG: {

            debug_msg("Op code 2: Config\r\n");
			// Just go back to waiting for a WRITE after a config message
			host_interface_wait();

			// This packet configures the module and
			// is what kicks off using it.
			uint8_t config_main   = rxBuffer[1];
			uint8_t config_master = rxBuffer[2];
			module_application_e my_app;
			module_role_e 		 my_role;
			glossy_role_e		 my_glossy_role;
			uint8_t				 my_master_eui;

			// Check if this module should be an anchor or tag
			my_role =        (config_main & HOST_PKT_CONFIG_MAIN_ROLE_MASK)   >> HOST_PKT_CONFIG_MAIN_ROLE_SHIFT;

			// Check if this module should act as a glossy master of slave
			my_glossy_role = (config_main & HOST_PKT_CONFIG_MAIN_GLOSSY_MASK) >> HOST_PKT_CONFIG_MAIN_GLOSSY_SHIFT;

			// Check which application we should run
			my_app =         (config_main & HOST_PKT_CONFIG_MAIN_APP_MASK)    >> HOST_PKT_CONFIG_MAIN_APP_SHIFT;

			// Receive Master EUI
			my_master_eui =	  config_master;

            /*debug_msg("Role: ");
            debug_msg_int(my_role);
            debug_msg("; Glossy Role: ");
            debug_msg_int(my_glossy_role);
            debug_msg("; App: ");
            debug_msg_int(my_app);*/

			// Now that we know what this module is going to be, we can
			// interpret the remainder of the packet.
			if (my_app == APP_STANDARD) {
				// Run the base normal ranging application

				module_config_t module_config;
				module_config.my_role				  = my_role;
				module_config.my_glossy_role		  = my_glossy_role;
				module_config.my_glossy_master_EUI[0] = my_master_eui;

				// Now that we know how we should operate,
				// call the main tag function to get things rollin'.
				module_configure_app(my_app, &module_config);
				module_start();

				// Shut down for Power testing of nRF (also: turn off glossy_init() and disable debug output)
				/*#include <stm32f0xx_pwr.h>
				module_stop();
				PWR_EnterSTOPMode(PWR_Regulator_LowPower, PWR_STOPEntry_WFI);*/

			} else if (my_app == APP_CALIBRATION) {
				// Run the calibration application to find the TX and RX delays in the node

				calibration_config_t cal_config;
				cal_config.index = rxBuffer[2];

				module_configure_app(my_app, &cal_config);
				module_start();

			} else {
				// Did not receive a known app code
				debug_msg("ERROR: Unknown app ");
				debug_msg_uint(my_app);
				debug_msg("!\n");
			}

			break;
		}


		/**********************************************************************/
		// Tell the module that it should take a range/location measurement
		/**********************************************************************/
		case HOST_CMD_DO_RANGE:

            debug_msg("Op code 4: Do range\r\n");
			// Just need to go back to waiting for the host to write more
			// after getting a sleep command
			host_interface_wait();

			// Tell the application to perform a range
			module_tag_do_range();
			break;

		/**********************************************************************/
		// Put the module to sleep.
		/**********************************************************************/
		case HOST_CMD_SLEEP:

            debug_msg("Op code 5: Sleep\r\n");
			// Just need to go back to waiting for the host to write more
			// after getting a sleep command
			host_interface_wait();

			// Tell the application to stop the dw1000 chip
			module_stop();
			break;

		/**********************************************************************/
		// Resume the application.
		/**********************************************************************/
		case HOST_CMD_RESUME:

            debug_msg("Op code 6: Resume\r\n");
			// Keep listening for the next command.
			host_interface_wait();

			// And we just have to start the application.
			module_start();
			break;

		/**********************************************************************/
		// Set epoch timestamp
		/**********************************************************************/
		case HOST_CMD_SET_TIME:

			debug_msg("Op code 9: Set Time\r\n");
			// Just go back to waiting for a WRITE after a configuration message
			host_interface_wait();

			// Set the internal time
			uint32_t curr_epoch = (rxBuffer[1] << 3*8) + (rxBuffer[2] << 2*8) + (rxBuffer[3] << 1*8) + rxBuffer[4];
			glossy_set_epoch_time(curr_epoch);
			break;

		/**********************************************************************/
		// These are handled from the interrupt context.
		/**********************************************************************/
		case HOST_CMD_INFO:
		case HOST_CMD_READ_INTERRUPT:
		case HOST_CMD_READ_CALIBRATION:
			break;


		default:
			break;
	}


}

// Called after a READ message from the master.
// We don't need to do anything after the master reads from us, except
// to go back to waiting for a WRITE.
void host_interface_tx_fired () {

	//debug_msg("Data sent\r\n");
}

// Called after timeout
void host_interface_timeout_fired () {
}


/**
  * @brief  User callback that manages the Timeout error
  * @param  pDevInitStruct
  * @retval None.
  */
uint32_t CPAL_TIMEOUT_UserCallback(CPAL_InitTypeDef* pDevInitStruct) {
	// Handle this interrupt on the main thread
	mark_interrupt(INTERRUPT_I2C_TIMEOUT);

	return CPAL_PASS;
}


/**
  * @brief  Manages the End of Rx transfer event.
  * @param  pDevInitStruct
  * @retval None
  */
void CPAL_I2C_RXTC_UserCallback(CPAL_InitTypeDef* pDevInitStruct) {
	uint8_t opcode;

	// Mark this interrupt for the main thread
	mark_interrupt(INTERRUPT_I2C_RX);

	// We need to do some of the handling for the I2C here, because if
	// we wait to handle it on the main thread sometimes there is too much
	// delay and the I2C stops working.


	// First byte of every correct WRITE packet is the opcode of the
	// packet.
	opcode = rxBuffer[0];
	switch (opcode) {
		/**********************************************************************/
		// Return the INFO array
		/**********************************************************************/
		case HOST_CMD_INFO:

            //debug_msg("Op code 1: Info\r\n");
			// Check what status the main application is in. If it has contacted
			// the DW1000, then it will be ready and we return the correct
			// info string. If it is not ready, we return the null string
			// that says that I2C is working but that we are not ready for
			// prime time yet.
			if (module_ready()) {
				// Info packet is a good way to check that I2C is working.
				memcpy(txBuffer, INFO_PKT, 3);
			} else {
				memcpy(txBuffer, NULL_PKT, 3);
			}
			host_interface_respond(3, TRUE);
			break;

		/**********************************************************************/
		// Ask the host why it asserted the interrupt line.
		/**********************************************************************/
		case HOST_CMD_READ_INTERRUPT: {

            //debug_msg("Op code 3: Interrupt\r\n");
			// Clear interrupt
			interrupt_host_clear();

			/*debug_msg("Interrupt buffer len: ");
			debug_msg_int(_interrupt_buffer_len);
			debug_msg("; Interrupt reason: ");
			debug_msg_int(_interrupt_reason);
			debug_msg("\n");*/

			// Prepare a packet to send back to the host
			txBuffer[0] = 1 + _interrupt_buffer_len;
			txBuffer[1] = _interrupt_reason;

			// Copy data to txBuffer; for long packets, this can take too long, which is why it has to be done before triggering the interrupt
			if (_interrupt_reason != HOST_IFACE_INTERRUPT_RANGES_RAW) {
                memcpy(txBuffer + 2, _interrupt_buffer, _interrupt_buffer_len);
            }

			host_interface_respond(txBuffer[0]+1, FALSE);

			break;
		}

		/**********************************************************************/
		// Respond with the stored calibration values
		/**********************************************************************/
		case HOST_CMD_READ_CALIBRATION: {

            //debug_msg("Op code 8: Calibration\r\n");
			// Copy the raw values from the stored array
			memcpy(txBuffer, dw1000_get_txrx_delay_raw(), 12);
			host_interface_respond(12, TRUE);
			break;
		}

		/**********************************************************************/
		// All of the following do not require a response and can be handled
		// on the main thread.
		/**********************************************************************/
		case HOST_CMD_CONFIG:
		case HOST_CMD_DO_RANGE:
		case HOST_CMD_SLEEP:
		case HOST_CMD_RESUME:

			// Just go back to waiting for a WRITE after a config message
			host_interface_wait();

			// Handle the rest on the main thread
			break;

		default:
			break;
	}
}

/**
  * @brief  Manages the End of Tx transfer event.
  * @param  pDevInitStruct
  * @retval None
  */
void CPAL_I2C_TXTC_UserCallback(CPAL_InitTypeDef* pDevInitStruct) {
	mark_interrupt(INTERRUPT_I2C_TX);

    // We need to do some of the handling for the I2C here, because if
    // we wait to handle it on the main thread sometimes there is too much
    // delay and the I2C stops working.

    if (pending_tx) {
        // If we sent a first message containing only the length, we now prepare to transmit the rest in an immediate second transmission
        host_interface_respond(pending_tx, TRUE);
    } else {
        // No more pending messages
        host_interface_wait();
    }
}

/**
  * @brief  User callback that manages the I2C device errors.
  * @note   Make sure that the define USE_SINGLE_ERROR_CALLBACK is uncommented in
  *         the cpal_conf.h file, otherwise this callback will not be functional.
  * @param  pDevInitStruct.
  * @param  DeviceError.
  * @retval None
  */
void CPAL_I2C_ERR_UserCallback(CPAL_DevTypeDef pDevInstance, uint32_t DeviceError) {

}
