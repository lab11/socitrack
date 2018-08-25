#include "nrf_drv_twi.h"
#include "sdk_errors.h"
#include "app_util_platform.h"
#include "nrf_drv_config.h"
#include "nrf_gpiote.h"
#include "nrfx_gpiote.h"
#include "nrf_drv_gpiote.h"
#include "nrf_delay.h"

#include "boards.h"
#include "led.h"

#include "module_interface.h"

uint8_t response[256];

#ifndef TWI_INSTANCE_NR
#define TWI_INSTANCE_NR 1
#endif

nrf_drv_twi_t twi_instance = NRF_DRV_TWI_INSTANCE(TWI_INSTANCE_NR);

// Save the callback that we use to signal the main application that we
// received data over I2C.
module_interface_data_cb_f _data_callback = NULL;


void module_interrupt_handler (nrf_drv_gpiote_pin_t pin, nrf_gpiote_polarity_t action) {

	/*debug_msg("Detected interrupt on pin: ");
	debug_msg_int(pin);
	debug_msg("\r\n");*/

	// verify interrupt is from module
	if (pin == CARRIER_INTERRUPT_MODULE) {

		// Ask whats up over I2C
		uint32_t ret;
		uint8_t cmd = MODULE_CMD_READ_INTERRUPT;
		ret = nrf_drv_twi_tx(&twi_instance, MODULE_ADDRESS, &cmd, 1, false);
		if (ret != NRF_SUCCESS) {
		    debug_msg("ERROR: ");
            debug_msg_int(ret);
            return;
        }

		//debug_msg("Sent CMD_READ_INTERRUPT\r\n");

		// Figure out the length of what we need to receive by
		// checking the first byte of the response.
		uint8_t len = 0;
		ret = nrf_drv_twi_rx(&twi_instance, MODULE_ADDRESS, &len, 1, true);
		if (ret != NRF_SUCCESS) return;

		// Read the rest of the packet
		if (len == 0) {
			// some error?
		} else {
			ret = nrf_drv_twi_rx(&twi_instance, MODULE_ADDRESS, response, len, false);
			if (ret != NRF_SUCCESS) return;
		}

		// Send back the I2C data
		debug_msg("Received I2C response of length ");
		debug_msg_int(len / 10);
		debug_msg_int(len % 10);
		debug_msg("\r\n");

		_data_callback(response, len);
	}
}

ret_code_t module_hw_init () {
	nrf_drv_twi_config_t twi_config;
	ret_code_t ret;

	// Initialize the I2C module
	twi_config.scl                = CARRIER_I2C_SCL;
	twi_config.sda                = CARRIER_I2C_SDA;
	twi_config.frequency          = NRF_DRV_TWI_FREQ_400K;
	twi_config.interrupt_priority = APP_IRQ_PRIORITY_HIGH;

	ret = nrf_drv_twi_init(&twi_instance, &twi_config, NULL);
	if (ret != NRF_SUCCESS) return ret;
	nrf_drv_twi_enable(&twi_instance);

	// Initialize the GPIO interrupt from the device
	ret = nrf_drv_gpiote_init();
	if (ret != NRF_SUCCESS) return ret;

	nrf_drv_gpiote_in_config_t in_config = NRFX_GPIOTE_CONFIG_IN_SENSE_LOTOHI(1);
	ret = nrf_drv_gpiote_in_init(CARRIER_INTERRUPT_MODULE, &in_config, module_interrupt_handler);
	if (ret != NRF_SUCCESS) return ret;

	nrf_drv_gpiote_in_event_enable(CARRIER_INTERRUPT_MODULE, true);
	return NRF_SUCCESS;
}

ret_code_t module_init (module_interface_data_cb_f cb) {
	_data_callback = cb;
	ret_code_t ret;

	// Wait for 500 ms to make sure the module module is ready
	nrf_delay_us(500000);

	// Now try to read the info byte to make sure we have I2C connection
	{
		uint16_t id;
		uint8_t version;
		ret = module_get_info(&id, &version);
		if (ret != NRF_SUCCESS) {
		    debug_msg("ERROR: Failed to contact STM with error code ");
		    debug_msg_int(ret);
		    debug_msg("!\r\n");
		    return ret;
		}
		if (id != MODULE_ID) return NRF_ERROR_INVALID_DATA;
	}

	return NRF_SUCCESS;
}

ret_code_t module_get_info (uint16_t* id, uint8_t* version) {
	uint8_t buf_cmd[1] = {MODULE_CMD_INFO};
	uint8_t buf_resp[3];
	ret_code_t ret;

	// Send outgoing command that indicates we want the device info string
	ret = nrf_drv_twi_tx(&twi_instance, MODULE_ADDRESS, buf_cmd, 1, false);
	if (ret != NRF_SUCCESS) return ret;

	// Read back the 3 byte payload
	ret = nrf_drv_twi_rx(&twi_instance, MODULE_ADDRESS, buf_resp, 3, false);
	if (ret != NRF_SUCCESS) return ret;

	*id = (uint16_t)( (uint16_t)buf_resp[0] << (uint8_t)8) | buf_resp[1];
	*version = buf_resp[2];

	return NRF_SUCCESS;
}

ret_code_t module_start_ranging (bool periodic, uint8_t rate) {
	uint8_t buf_cmd[4];
	ret_code_t ret;

	buf_cmd[0] = MODULE_CMD_CONFIG;

	// TAG for now
	buf_cmd[1] = 0;

	// TAG options
	buf_cmd[2] = 0;
	if (periodic) {
		// leave 0
	} else {
		buf_cmd[2] |= 0x2;
	}

	// Use sleep mode on the TAG
	buf_cmd[2] |= 0x08;

	// And rate
	buf_cmd[3] = rate;

	ret = nrf_drv_twi_tx(&twi_instance, MODULE_ADDRESS, buf_cmd, 4, false);
	if (ret != NRF_SUCCESS) return ret;

	return NRF_SUCCESS;
}

// Tell the attached module to become an anchor.
ret_code_t module_start_anchor (bool is_glossy_master) {
	uint8_t buf_cmd[4];
	ret_code_t ret;

	buf_cmd[0] = MODULE_CMD_CONFIG;

	// Make ANCHOR
	if(is_glossy_master)
		buf_cmd[1] = (uint8_t)0x01 | (uint8_t)0x20;
	else
		buf_cmd[1] = 0x01;

	// // TAG options
	// buf_cmd[2] = 0;
	// if (periodic) {
	// 	// leave 0
	// } else {
	// 	buf_cmd[2] |= 0x2;
	// }

	// // And rate
	// buf_cmd[3] = rate;

	ret = nrf_drv_twi_tx(&twi_instance, MODULE_ADDRESS, buf_cmd, 2, false);
	if (ret != NRF_SUCCESS) return ret;

	return NRF_SUCCESS;
}

// Tell the attached module that it should enter the calibration
// mode.
ret_code_t module_start_calibration (uint8_t index) {
	uint8_t buf_cmd[4];
	ret_code_t ret;

	buf_cmd[0] = MODULE_CMD_CONFIG;

	// Make TAG in CALIBRATION
	buf_cmd[1] = 0x04;

	// Set the index of the node in calibration
	buf_cmd[2] = index;

	ret = nrf_drv_twi_tx(&twi_instance, MODULE_ADDRESS, buf_cmd, 3, false);
	if (ret != NRF_SUCCESS) return ret;

	return NRF_SUCCESS;
}

// Read the stored calibration values into a buffer (must be
// at least 18 bytes long).
ret_code_t module_get_calibration (uint8_t* calib_buf) {
	uint8_t buf_cmd[1] = {MODULE_CMD_READ_CALIBRATION};
	ret_code_t ret;

	// Send outgoing command that indicates we want the device info string
	ret = nrf_drv_twi_tx(&twi_instance, MODULE_ADDRESS, buf_cmd, 1, false);
	if (ret != NRF_SUCCESS) return ret;

	// Read back the 18 bytes of calibration values
	ret = nrf_drv_twi_rx(&twi_instance, MODULE_ADDRESS, calib_buf, 18, false);
	if (ret != NRF_SUCCESS) return ret;

	return NRF_SUCCESS;
}

// Stop the module and put it in sleep mode
ret_code_t module_sleep () {
	uint8_t buf_cmd[1] = {MODULE_CMD_SLEEP};
	ret_code_t ret;

	ret = nrf_drv_twi_tx(&twi_instance, MODULE_ADDRESS, buf_cmd, 1, false);
	if (ret != NRF_SUCCESS) return ret;

	return NRF_SUCCESS;
}

// Restart the module. This should only be called if the module was
// once running and then was stopped. If the module was never configured,
// this won't do anything.
ret_code_t module_resume () {
	uint8_t buf_cmd[1] = {MODULE_CMD_RESUME};
	ret_code_t ret;

	ret = nrf_drv_twi_tx(&twi_instance, MODULE_ADDRESS, buf_cmd, 1, false);
	if (ret != NRF_SUCCESS) return ret;

	return NRF_SUCCESS;
}




