#include <stddef.h>
#include <string.h>

#include "dw1000.h"

#include "timer.h"
#include "SEGGER_RTT.h"

#include "firmware.h"
#include "module_conf.h"

#include "app_standard_common.h"
#include "app_standard_init.h"
#include "app_standard_resp.h"

// APPLICATION STATE ---------------------------------------------------------------------------------------------------

// All of the configuration passed to us by the host for how this application should operate.
static module_config_t _config;

// Configure the RF channels to use. This is just a mapping from 0..2 to
// the actual RF channel numbers the DW1000 uses.
static const uint8_t channel_index_to_channel_rf_number[NUM_RANGING_CHANNELS] = {
	1, 4, 3
};

typedef struct  {
    standard_init_scratchspace_struct si_scratch;	// Storage for INIT
    standard_resp_scratchspace_struct sr_scratch;	// Storage for RESP
} standard_scratchspace_struct;

// Scratchspace for both INIT and RESP
static standard_scratchspace_struct _app_scratchspace;

// STATIC FUNCTIONS ----------------------------------------------------------------------------------------------------

static void common_txcallback(const dwt_cb_data_t *txd);
static void common_rxcallback(const dwt_cb_data_t *rxd);

// ---------------------------------------------------------------------------------------------------------------------

// This sets the settings for this node and initializes the node.
void standard_configure (module_config_t* config) {

	// Save the settings
	memcpy(&_config, config, sizeof(module_config_t));

	// Set scratchspace to known zeros
	memset(&_app_scratchspace, 0, sizeof(_app_scratchspace));

	// Make sure the DW1000 is awake before trying to do anything.
	dw1000_wakeup();

    // Set EUI
    dw1000_read_eui(_config.my_EUI);

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

    // Ranging requires glossy synchronization, so let's enable that now
    // This uses TIM17 as a timer
    glossy_init(_config.my_glossy_role, _config.my_glossy_master_EUI[0]);

	// Get a second timer (TIM16) for both INIT and RESP; they will never be using it simultaneously
	stm_timer_t * ranging_timer = timer_init();
	_app_scratchspace.si_scratch.init_timer = ranging_timer;
	_app_scratchspace.sr_scratch.resp_timer = ranging_timer;

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

	// Setup general callbacks
	dwt_setcallbacks(common_txcallback, common_rxcallback, common_rxcallback, common_rxcallback);

	// Make sure the radio starts off
	dwt_forcetrxoff();

	// Set the anchor so it only receives data and ack packets
	dwt_enableframefilter(DWT_FF_DATA_EN | DWT_FF_ACK_EN);

	// Don't use these
	dwt_setdblrxbuffmode(FALSE);
	dwt_setrxtimeout(FALSE);

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

	// Start Glossy
	glossy_start();
}

// Stop the standard application
void standard_stop () {

	// Turn off the DecaWave from reception
	dwt_forcetrxoff();

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
	uint8_t tag_offset = tag_antenna_index    * NUM_RANGING_CHANNELS * NUM_RANGING_CHANNELS;
	uint8_t base_offset = anc_offset + tag_offset + channel_index;

	return base_offset;
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
void standard_set_ranging_response_settings (bool init_active,
                                             uint8_t antenna_num) {

    if (init_active) {
        dwt_forcetrxoff();
    }

	// Choose channel
	dw1000_update_channel(channel_index_to_channel_rf_number[RANGING_RESPONSE_CHANNEL_INDEX]);

	// Change what antenna we're listening/sending on
	dw1000_choose_antenna(antenna_num);
}

// Get the subsequence slot number that a particular set of settings
// (anchor antenna index, tag antenna index, channel) were used to send
// a broadcast poll message. The tag antenna index and channel are derived
// from the settings used in the listening window.
uint8_t standard_get_ss_index_from_settings (uint8_t anchor_antenna_index,
                                             uint8_t channel_index) {
	// FIXME: need something more rigorous than setting 0 here; we can also try whether we have obtained the other two packets with the same receiver antenna
	uint8_t tag_antenna_index = 0;

	return antenna_and_channel_to_subsequence_number(tag_antenna_index,
	                                                 anchor_antenna_index,
	                                                 channel_index % NUM_RANGING_CHANNELS);
}

// Get the TX delay for this node, given the channel value
uint64_t standard_get_txdelay_from_subsequence (bool resp_active, uint8_t subseq_num) {
	// Need to get channel and antenna to call the dw1000 function
	uint8_t channel_index = subsequence_number_to_channel_index(subseq_num);
	uint8_t antenna_index = standard_subsequence_number_to_antenna(resp_active, subseq_num);
	return dw1000_get_tx_delay(channel_index, antenna_index);
}

// Get the RX delay for this node, given the channel value
uint64_t standard_get_rxdelay_from_subsequence (bool resp_active, uint8_t subseq_num) {
	// Need to get channel and antenna to call the dw1000 function
	uint8_t channel_index = subsequence_number_to_channel_index(subseq_num);
	uint8_t antenna_index = standard_subsequence_number_to_antenna(resp_active, subseq_num);
	return dw1000_get_rx_delay(channel_index, antenna_index);
}

uint64_t standard_get_txdelay_from_ranging_response_channel (uint8_t channel_index, uint8_t antenna_index){
	return dw1000_get_tx_delay(channel_index % NUM_RANGING_CHANNELS, antenna_index);
}

uint64_t standard_get_rxdelay_from_ranging_response_channel (uint8_t channel_index, uint8_t antenna_index){
	return dw1000_get_rx_delay(channel_index % NUM_RANGING_CHANNELS, antenna_index);
}

/******************************************************************************/
// TX / RX functions
/******************************************************************************/

void debug_print_tx(uint32_t length) {
    /*debug_msg("Tx -> length ");
    debug_msg_uint(length);
    debug_msg("\n");*/
}

void clear_frame_event() {

    // Clear SYS_STATUS_RXFCG and SYS_STATUS_TXFRS
    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXFCG | SYS_STATUS_TXFRS);
}

static void common_txcallback(const dwt_cb_data_t *txd) {

	//debug_msg("Tx -> done\n"); // datalength is NOT valid for Tx callbacks

	// Handle GLOSSY
	if (glossy_process_txcallback()) {
		// We already handled the packet
		return;
	}

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
		debug_msg("ERROR: Invalid active state during Tx: INIT ");
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

	// Get the message type for the different package types; with the current implementation, they should all have the same offset
	uint8_t message_type_sync   = buf[offsetof(struct pp_sched_flood,  message_type)];
    uint8_t message_type_signal = buf[offsetof(struct pp_signal_flood, message_type)];
    uint8_t message_type_poll   = buf[offsetof(struct pp_tag_poll,     message_type)];
    uint8_t message_type_final  = buf[offsetof(struct pp_anc_final,    message_type)];

	/*debug_msg("Rx -> length: ");
	debug_msg_uint(rxd->datalength);
	debug_msg("; type: ");
	debug_msg_uint(message_type_sync);
	debug_msg("\n");*/

	if(message_type_sync == MSG_TYPE_PP_GLOSSY_SYNC || message_type_signal == MSG_TYPE_PP_GLOSSY_SIGNAL)
	{
	    // Verify that the frame is actually correctly received
	    if (rxd->status & SYS_STATUS_RXFCG) {

	        // Clear flag again
	        clear_frame_event();
	    } else if ( (rxd->status & SYS_STATUS_ALL_RX_ERR) ||
                    (rxd->status & SYS_STATUS_ALL_RX_TO )   ) {

            /*debug_msg("WARNING: Rx error for GLOSSY, status: ");
            debug_msg_int((uint32_t) rxd->status);
            debug_msg("\n");*/

            standard_set_ranging_response_settings(TRUE, 0);
            dwt_rxenable(0);
            return;
        }

		// Handle Glossy packet; same for all roles - neither INIT nor RESP should be active currently
		glossy_process_rxcallback(dw_rx_timestamp - standard_get_rxdelay_from_subsequence(FALSE, 0), buf);
	}
	else if (message_type_poll == MSG_TYPE_PP_NOSLOTS_TAG_POLL || message_type_final == MSG_TYPE_PP_NOSLOTS_ANC_FINAL)
	{
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
			debug_msg("ERROR: Invalid active state during Rx: INIT ");
			debug_msg_int(_config.init_active);
			debug_msg(" , RESP ");
			debug_msg_int(_config.resp_active);
			debug_msg("\n");
		}

	}
	else
    {
	    /*debug_msg("WARNING: Received invalid packet of type ");
	    debug_msg_uint(message_type_sync);
	    debug_msg(" with status ");
	    debug_msg_uint(rxd->status);
	    debug_msg("\n");*/

	    // Frame received incorrectly - Reed Solomon cannot correct it, so we just have to drop the packet
        standard_set_ranging_response_settings(TRUE, 0);
	    dwt_rxenable(0);
    }
}


/******************************************************************************/
// Helper functions
/******************************************************************************/

void helper_print_EUI(uint8_t * address, uint8_t len) {

    for (uint8_t i = 0; i < len; i++) {
    	debug_msg_hex(address[i] >> 0x04);
        debug_msg_hex(address[i] &  0x0F);

        if (i < (len - 1))
        	debug_msg(":");
    }
}