#include <stddef.h>
#include <string.h>

#include "timer.h"
#include "SEGGER_RTT.h"

#include "firmware.h"
#include "dw1000.h"

#include "app_standard_common.h"
#include "app_standard_init.h"
#include "app_standard_resp.h"

// APPLICATION STATE ---------------------------------------------------------------------------------------------------

// All of the configuration passed to us by the host for how this application should operate.
static module_config_t _config;
// Our local reference to the timer for all of the high-level application code.
static stm_timer_t* _app_timer;

// Configure the RF channels to use. This is just a mapping from 0..2 to
// the actual RF channel numbers the DW1000 uses.
static const uint8_t channel_index_to_channel_rf_number[NUM_RANGING_CHANNELS] = {
	1, 4, 3
};

union app_scratchspace {
	standard_init_scratchspace_struct si_scratch;
	standard_resp_scratchspace_struct sr_scratch;
} _app_scratchspace;

// STATIC FUNCTIONS ----------------------------------------------------------------------------------------------------

static void common_txcallback(const dwt_cb_data_t *txd);
static void common_rxcallback(const dwt_cb_data_t *rxd);

// ---------------------------------------------------------------------------------------------------------------------

// This sets the settings for this node and initializes the node.
void standard_configure (module_config_t* config, stm_timer_t* app_timer) {

	// Save the settings
	memcpy(&_config, config, sizeof(module_config_t));

	// Save the application timer for use by this application
	//_app_timer = app_timer;

	// Set scratchspace to known zeros
	memset(&_app_scratchspace, 0, sizeof(_app_scratchspace));

	// Make sure the DW1000 is awake before trying to do anything.
	dw1000_wakeup();

	// Oneway ranging requires glossy synchronization, so let's enable that now
	glossy_init(_config.my_glossy_role);

	// Now init based on role
	_config.init_active = FALSE;
	_config.resp_active = FALSE;

	switch (_config.my_role) {
		case APP_ROLE_INIT_NORESP: {
			_config.init_enabled = TRUE;
			_config.resp_enabled = FALSE;

			debug_msg("Initialized as TAG\n");
			break;
		}
		case APP_ROLE_INIT_RESP: {
			_config.init_enabled = TRUE;
			_config.resp_enabled = TRUE;

			debug_msg("Initialized as HYBRID\n");
			break;
		}
		case APP_ROLE_NOINIT_RESP: {
			_config.init_enabled = FALSE;
			_config.resp_enabled = TRUE;

			debug_msg("Initialized as ANCHOR\n");
			break;
		}
		case APP_ROLE_NOINIT_NORESP: {
			_config.init_enabled = FALSE;
			_config.resp_enabled = FALSE;

			debug_msg("Initialized as SUPPORT\n");
			break;
		}
		default: {
			debug_msg("ERROR: Unknown role!\n");
			return;
		}
	}

	// Initialize code for INIT
	if (_config.init_enabled) {
		standard_initiator_init(&_app_scratchspace.si_scratch);
	}
	// Initialize code for RESP
	if (_config.resp_enabled) {
		standard_resp_init(&_app_scratchspace.sr_scratch);
	}
}

// Kick off the application
void standard_start () {
	dw1000_err_e err;

	// Set correct DW settings for both roles
	// Make sure the SPI speed is slow for this function
	dw1000_spi_slow();

	// Setup callbacks to this ANCHOR
	dwt_setcallbacks(common_txcallback, common_rxcallback, common_rxcallback, common_rxcallback);

	// Make sure the radio starts off
	dwt_forcetrxoff();

	// Set the anchor so it only receives data and ack packets
	dwt_enableframefilter(DWT_FF_DATA_EN | DWT_FF_ACK_EN);

	// Don't use these
	dwt_setrxtimeout(FALSE);

	// Set EUI
    dw1000_read_eui(_config.my_EUI);

	// Make SPI fast now that everything has been setup
	dw1000_spi_fast();


	// Initiators will be enabled directly through Glossy

	if (_config.resp_enabled) {
		// Start the state machine for responders. The app doesn't have to do anything for this, it just runs.

		err = standard_resp_start();
		if (err == DW1000_WAKEUP_ERR) {
			debug_msg("ERROR: Did not wakeup!\n");
			module_reset();
		}
	}
}

// Stop the standard application
void standard_stop () {
	if (_config.init_enabled) {
		standard_init_stop();
	}

	if (_config.resp_enabled) {
		standard_resp_stop();
	}
}

// The whole DW1000 reset, so we need to get this app running again
void standard_reset () {
	// Start by initing based on role
	if (_config.init_enabled) {
		standard_initiator_init(&_app_scratchspace.si_scratch);
	}

	if (_config.resp_enabled) {
		standard_resp_init(&_app_scratchspace.sr_scratch);
	}
}

// Function to perform an on-demand ranging event
void standard_do_range () {
	dw1000_err_e err;

	// If we are not a tag, or we are not
	// in on-demand ranging mode, don't do anything.
	if (_config.my_role 	!= APP_ROLE_INIT_NORESP ||
	    _config.update_mode != MODULE_UPDATE_MODE_DEMAND) {
		return;
	}

	// TODO: this does return an error if we are already ranging.
	err = standard_init_start_ranging_event();
	if (err == DW1000_WAKEUP_ERR) {
		module_reset();
	}
}

// Return a pointer to the application configuration settings
module_config_t* standard_get_config () {
	return &_config;
}

module_role_e standard_get_role() {
	return _config.my_role;
}

void standard_set_role(module_role_e role) {
	_config.my_role = role;
}

bool standard_is_init_enabled() {
	return _config.init_enabled;
}

bool standard_is_init_active() {
	return _config.init_active;
}

void standard_set_init_active(bool init_active) {
	_config.init_active = init_active;
}

bool standard_is_resp_enabled() {
	return _config.resp_enabled;
}

bool standard_is_resp_active() {
	return _config.resp_active;
}

void standard_set_resp_active(bool resp_active) {
	_config.resp_active = resp_active;
}

uint8_t * standard_get_EUI() {
	return _config.my_EUI;
}

/******************************************************************************/
// Ranging Protocol Algorithm Functions
/******************************************************************************/

// Break this out into two functions.
// (Mostly needed for calibration purposes.)
static uint8_t subsequence_number_to_channel_index (uint8_t subseq_num) {
	return subseq_num % NUM_RANGING_CHANNELS;;
}

// Return the RF channel to use for a given subsequence number
static uint8_t subsequence_number_to_channel (uint8_t subseq_num) {
	// ALGORITHM
	// We iterate through the channels as fast as possible. We do this to
	// find anchors that may not be listening on the first channel as quickly
	// as possible so that they can join the sequence as early as possible. This
	// increases the number of successful packet transmissions and increases
	// ranging accuracy.
	uint8_t channel_index = subsequence_number_to_channel_index(subseq_num);
	return channel_index_to_channel_rf_number[channel_index];
}

// Return the Antenna index to use for a given subsequence number
uint8_t standard_subsequence_number_to_antenna (bool resp_active, uint8_t subseq_num) {

	// ALGORITHM
	// We must rotate the anchor and tag antennas differently so the same
	// ones don't always overlap. This should also be different from the
	// channel sequence. This math is a little weird but somehow works out,
	// even if NUM_RANGING_CHANNELS != NUM_ANTENNAS.
	if (resp_active) {
		return (subseq_num / NUM_RANGING_CHANNELS) % NUM_ANTENNAS;
	} else {
		return 	((subseq_num / NUM_RANGING_CHANNELS) / NUM_RANGING_CHANNELS) % NUM_ANTENNAS;
	}
}

// Go the opposite way and return the ss number based on the antenna used.
// Returns the LAST valid slot that matches the sequence.
static uint8_t antenna_and_channel_to_subsequence_number (uint8_t tag_antenna_index,
                                                          uint8_t anchor_antenna_index,
                                                          uint8_t channel_index) {
	uint8_t anc_offset = anchor_antenna_index * NUM_RANGING_CHANNELS;
	uint8_t tag_offset = tag_antenna_index * NUM_RANGING_CHANNELS * NUM_RANGING_CHANNELS;
	uint8_t base_offset = anc_offset + tag_offset + channel_index;

	return base_offset;
}

// Return the RF channel to use when the anchors respond to the tag
static uint8_t listening_window_number_to_channel (uint8_t window_num) {
	return channel_index_to_channel_rf_number[window_num % NUM_RANGING_CHANNELS];
}



// Update the Antenna and Channel settings to correspond with the settings
// for the given subsequence number.
//
// role:       anchor or tag
// subseq_num: where in the sequence we are
void standard_set_ranging_broadcast_subsequence_settings (bool resp_active,
                                                        uint8_t subseq_num) {
	// Stop the transceiver on the anchor.
	if (resp_active) {
		dwt_forcetrxoff();
	}

	// Change the channel depending on what subsequence number we're at
	dw1000_update_channel(subsequence_number_to_channel(subseq_num));

	// Change what antenna we're listening/sending on
	dw1000_choose_antenna(standard_subsequence_number_to_antenna(resp_active, subseq_num));
}

// Update the Antenna and Channel settings to correspond with the settings
// for the given listening window.
//
// role:       anchor or tag
// window_num: where in the listening window we are
void standard_set_ranging_listening_window_settings (bool init_active,
                                                   uint8_t window_num,
                                                   uint8_t antenna_num) {

    if (init_active) {
        dwt_forcetrxoff();
    }

	// Change the channel depending on what window number we're at
	dw1000_update_channel(listening_window_number_to_channel(window_num));

	// Change what antenna we're listening/sending on
	dw1000_choose_antenna(antenna_num);
}

// Get the subsequence slot number that a particular set of settings
// (anchor antenna index, tag antenna index, channel) were used to send
// a broadcast poll message. The tag antenna index and channel are derived
// from the settings used in the listening window.
uint8_t standard_get_ss_index_from_settings (uint8_t anchor_antenna_index,
                                           uint8_t window_num) {
	// NOTE: need something more rigorous than setting 0 here
	uint8_t tag_antenna_index = 0;
	uint8_t channel_index = window_num % NUM_RANGING_CHANNELS;

	return antenna_and_channel_to_subsequence_number(tag_antenna_index,
	                                                 anchor_antenna_index,
	                                                 channel_index);
}

// Get the TX delay for this node, given the channel value
uint64_t standard_get_txdelay_from_subsequence (uint8_t subseq_num) {
	// Need to get channel and antenna to call the dw1000 function
	uint8_t channel_index = subsequence_number_to_channel_index(subseq_num);
	return dw1000_get_tx_delay(channel_index);
}

// Get the RX delay for this node, given the channel value
uint64_t standard_get_rxdelay_from_subsequence (uint8_t subseq_num) {
	// Need to get channel and antenna to call the dw1000 function
	uint8_t channel_index = subsequence_number_to_channel_index(subseq_num);
	return dw1000_get_rx_delay(channel_index);
}

uint64_t standard_get_txdelay_from_ranging_listening_window (uint8_t window_num){
	return dw1000_get_tx_delay(window_num % NUM_RANGING_CHANNELS);
}

uint64_t standard_get_rxdelay_from_ranging_listening_window (uint8_t window_num){
	return dw1000_get_rx_delay(window_num % NUM_RANGING_CHANNELS);
}

/******************************************************************************/
// TX / RX functions
/******************************************************************************/

static void common_txcallback(const dwt_cb_data_t *txd) {

	// Handle GLOSSY
	glossy_process_txcallback();

	// Switch for active role to call correct callback
	if ( (_config.init_active && !_config.resp_active) || (!_config.init_active && _config.resp_active)) {

		// INIT active
		if (_config.init_active) {
			init_txcallback(txd);
		}

		// RESP active
		if (_config.resp_active) {
			resp_txcallback(txd);
		}

	} else {
		debug_msg("ERROR: Invalid active state: INIT ");
		debug_msg_int(_config.init_active);
		debug_msg(" , RESP ");
		debug_msg_int(_config.resp_active);
		debug_msg("\n");
	}
}

static void common_rxcallback(const dwt_cb_data_t *rxd) {

	// Handle GLOSSY
	uint64_t dw_rx_timestamp;
	uint8_t  buf[MSG_MAX_PACK_LEN];

	// Get the received time of this packet first
	dw_rx_timestamp = dw1000_readrxtimestamp();

	// Get the actual packet bytes
	dwt_readrxdata(buf, MIN(MSG_MAX_PACK_LEN, rxd->datalength), 0);

	uint8_t message_type = buf[offsetof(struct pp_tag_poll, message_type)];
	if(message_type == MSG_TYPE_PP_GLOSSY_SYNC || message_type == MSG_TYPE_PP_GLOSSY_SCHED_REQ)
	{
		// Handle Glossy packet; same for all roles - neither INIT nor RESP should be active currently
		glossy_sync_process(dw_rx_timestamp - standard_get_rxdelay_from_subsequence(0), buf);
	}
	else {

		// Switch for active role to call correct callback
		if ((_config.init_active && !_config.resp_active) || (!_config.init_active && _config.resp_active)) {

			// INIT active
			if (_config.init_active) {
				init_rxcallback(rxd, buf, dw_rx_timestamp);
			}

			// RESP active
			if (_config.resp_active) {
				resp_rxcallback(rxd, buf, dw_rx_timestamp);
			}

		} else {
			debug_msg("ERROR: Invalid active state: INIT ");
			debug_msg_int(_config.init_active);
			debug_msg(" , RESP ");
			debug_msg_int(_config.resp_active);
			debug_msg("\n");
		}

	}
}