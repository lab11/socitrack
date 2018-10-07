#include <stddef.h>
#include <string.h>
#include <stm32f0xx_gpio.h>

#include "timer.h"

#include "firmware.h"
#include "board.h"
#include "dw1000.h"
#include "host_interface.h"
#include "rangetest_common.h"
#include "rangetest_tag.h"
#include "rangetest_anchor.h"
#include "SEGGER_RTT.h"

// All of the configuration passed to us by the host for how this application should operate.
static rangetest_config_t _config;
// Our local reference to the timer for all of the high-level applicationcode.
static stm_timer_t* _app_timer;

// Configure the RF channels to use. This is just a mapping from 0..2 to
// the actual RF channel numbers the DW1000 uses.
static const uint8_t channel_index_to_channel_rf_number[NUM_RANGING_CHANNELS] = {
	1, 2, 3
};

// Buffer of anchor IDs and ranges to the anchor.
// Long enough to hold an anchor id followed by the range, plus the number of ranges

static void *_scratchspace_ptr;

// This sets the settings for this node and initializes the node.
void rangetest_configure (oneway_config_t* config, stm_timer_t* app_timer, void *app_scratchspace) {
	_scratchspace_ptr = app_scratchspace;

	// Save the settings
	_config.my_role     = config->my_role;
	_config.update_rate = config->update_rate;

	// Make sure the DW1000 is awake before trying to do anything.
	dw1000_wakeup();

	// Now init based on role
	if (_config.my_role == TAG) {
		rangetest_tag_init(_scratchspace_ptr);
		debug_msg("Initialized as TAG\n");
	} else if (_config.my_role == ANCHOR) {
		rangetest_anchor_init(_scratchspace_ptr);
		debug_msg("Initialized as ANCHOR\n");
	}
}

// Kick off the application
void rangetest_start () {
	dw1000_err_e err;

#if (BOARD_V == SQUAREPOINT)
	// Turn off all LEDs
	GPIO_WriteBit(STM_LED_RED_PORT,   STM_LED_RED_PIN,   Bit_SET);
	GPIO_WriteBit(STM_LED_BLUE_PORT,  STM_LED_BLUE_PIN,  Bit_SET);
	GPIO_WriteBit(STM_LED_GREEN_PORT, STM_LED_GREEN_PIN, Bit_SET);
#endif

	if (_config.my_role == ANCHOR) {
		// Start the anchor state machine. The app doesn't have to do anything for this, it just runs.
		err = rangetest_anchor_start();
		if (err == DW1000_WAKEUP_ERR) {
			debug_msg("ERROR: Could not wake up DecaWave\n");
		}
	} else if (_config.my_role == TAG) {

	}
}

// Update the Antenna and Channel settings to correspond with the settings
// for the given subsequence number.
//
// role:       anchor or tag
// subseq_num: where in the sequence we are
void rangetest_set_ranging_broadcast_settings (dw1000_role_e role, uint8_t subseq_num) {
	// Stop the transceiver on the anchor. Don't know why.
	//if (role == ANCHOR) {
		dwt_forcetrxoff();
	//}

	// Change the channel depending on what subsequence number we're at
    dw1000_update_channel(1);

	// Change what antenna we're listening/sending on
    //dw1000_choose_antenna(0);
}

// SIMPLE TEST ---------------------------------------------------------------------------------------------------------

dwt_config_t simpletest_config = {
	1,                        /* Channel number. */
	DWT_PRF_64M,              /* Pulse repetition frequency. */
	DW1000_PREAMBLE_LENGTH,   /* Preamble length. Used in TX only. */
	DW1000_PAC_SIZE,          /* Preamble acquisition chunk size. Used in RX only. */
	9,                        /* TX preamble code. Used in TX only. */
	9,                        /* RX preamble code. Used in RX only. */
	0,                        /* 0 to use standard SFD, 1 to use non-standard SFD. */
	DW1000_DATA_RATE,         /* Data rate. */
	DWT_PHRMODE_STD,          /* PHY header mode. */
	DW1000_SFD_TO,            /* SFD timeout (preamble length + 1 + SFD length - PAC size). Used in RX only. */
	DW1000_SMART_PWR_EN
};

// This function will call the most basic Rx / Tx example, without making use of any other callbacks or initializations
// This sets the settings for this node and initializes the node.
void simpletest_configure (oneway_config_t* config, stm_timer_t* app_timer, void *app_scratchspace) {
    _scratchspace_ptr = app_scratchspace;

    // Save the settings
    _config.my_role     = config->my_role;
    _config.update_rate = config->update_rate;

    // Make sure the DW1000 is awake before trying to do anything.
    dw1000_wakeup();

    // These functions will never return
    if (_config.my_role == TAG) {
        simpletest_tag_start();
        debug_msg("Initialized as TAG\n");
    } else if (_config.my_role == ANCHOR) {
        simpletest_anchor_start();
        debug_msg("Initialized as ANCHOR\n");
    }
}

