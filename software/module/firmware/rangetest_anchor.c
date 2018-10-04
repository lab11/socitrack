#include <stddef.h>
#include <string.h>

#include "deca_device_api.h"
#include "deca_regs.h"

#include "rangetest_common.h"
#include "rangetest_anchor.h"
#include "dw1000.h"
#include "timer.h"
#include "delay.h"
#include "firmware.h"
#include "board.h"
#include "SEGGER_RTT.h"

static void anchor_txcallback (const dwt_callback_data_t *txd);
static void anchor_rxcallback (const dwt_callback_data_t *rxd);


void rangetest_anchor_init(void *app_scratchspace) {
	
	test_oa_scratch = (rangetest_anchor_scratchspace_struct*) app_scratchspace;

	// Make sure the SPI speed is slow for this function
	dw1000_spi_slow();

	// Setup callbacks to this ANCHOR
	dwt_setcallbacks(anchor_txcallback, anchor_rxcallback);

	// Make sure the radio starts off
	dwt_forcetrxoff();

	// Set the anchor so it only receives data and ack packets
	dwt_enableframefilter(DWT_FF_DATA_EN | DWT_FF_ACK_EN);

	// // Set the ID and PAN ID for this anchor
	uint8_t eui_array[8];
	dw1000_read_eui(eui_array);

	// Automatically go back to receive
	dwt_setautorxreenable(TRUE);

	// Don't use these
	dwt_setdblrxbuffmode(FALSE);
	dwt_setrxtimeout(FALSE);

	// Make SPI fast now that everything has been setup
	dw1000_spi_fast();

	// Reset our state because nothing should be in progress if we call init()
	test_oa_scratch->state = TEST_ASTATE_IDLE;
}

// Tell the anchor to start its job of being an anchor
dw1000_err_e rangetest_anchor_start () {
	dw1000_err_e err;

	// Make sure the DW1000 is awake.
	err = dw1000_wakeup();
	if (err == DW1000_WAKEUP_SUCCESS) {
		// We did wake the chip, so reconfigure it properly
		// Put back the ANCHOR settings.
		rangetest_anchor_init((void*)test_oa_scratch);
	} else if (err) {
		// Chip did not seem to wakeup. This is not good, so we have
		// to reset the application.
		return err;
	}

	// Also we start over in case the anchor was doing anything before
	test_oa_scratch->state = TEST_ASTATE_IDLE;

	// Choose to wait in the first default position.
	// This could change to wait in any of the first NUM_CHANNEL-1 positions.
	rangetest_set_ranging_broadcast_settings(ANCHOR, 0);

	// Obviously we want to be able to receive packets
	dwt_rxenable(0);

	return DW1000_NO_ERR;
}

// This is called whenever we received a packet
static void ranging_broadcast_received_task () {
	// When this timer is called it is time to start a new subsequence
	// slot, so we must increment our counter
	test_oa_scratch->ranging_broadcast_ss_num++;

	// Update the anchor listening settings
	rangetest_set_ranging_broadcast_settings(ANCHOR, test_oa_scratch->ranging_broadcast_ss_num);

	// Toggle GREEN, turn off BLUE
    GPIO_WriteBit(STM_LED_BLUE_PORT,  STM_LED_BLUE_PIN,  Bit_SET);
	if (GPIO_ReadOutputDataBit(STM_LED_GREEN_PORT, STM_LED_GREEN_PIN)) {
		GPIO_WriteBit(STM_LED_GREEN_PORT, STM_LED_GREEN_PIN, Bit_RESET);
	} else {
		GPIO_WriteBit(STM_LED_GREEN_PORT, STM_LED_GREEN_PIN, Bit_SET);
	}

	// And re-enable RX. The set_broadcast_settings function disables tx and rx.
	dwt_rxenable(0);
}

// Called after a packet is transmitted. We don't need this so it is
// just empty.
static void anchor_txcallback (const dwt_callback_data_t *txd) {

    debug_msg("ANCHOR transmitted a packet\n");

}

// Called when the radio has received a packet.
static void anchor_rxcallback (const dwt_callback_data_t *rxd) {

	debug_msg("Received DW1000 packet\r\n");

	if (rxd->event == DWT_SIG_RX_OKAY) {

		// First check to see if this is an acknowledgement...
		// If so, we can stop sending ranging responses
		if((rxd->fctrl[0] & 0x03) == 0x02){  //This bit says whether this was an ack or not
			uint8_t cur_seq_num;
			dwt_readrxdata(&cur_seq_num, 1, 2);

		} else {

			// Read in parameters of this packet reception
			uint8_t  buf[RANGETEST_ANCHOR_MAX_RX_PKT_LEN];
			uint64_t dw_rx_timestamp;
			uint8_t  message_type;

			// Get the received time of this packet first
			dw_rx_timestamp = dw1000_readrxtimestamp();

			// Get the actual packet bytes
			dwt_readrxdata(buf, MIN(RANGETEST_ANCHOR_MAX_RX_PKT_LEN, rxd->datalength), 0);

			// We process based on the first byte in the packet. How very active
			// message like...
			message_type = buf[offsetof(struct rangetest_packet, message_type)];

			if (message_type == MSG_TYPE_PP_NOSLOTS_TAG_POLL) {
				// This is one of the broadcast ranging packets from the tag
				struct rangetest_packet* rx_poll_pkt = (struct rangetest_packet*) buf;

                debug_msg("Received Poll message ");
                debug_msg_int(rx_poll_pkt->subsequence);
                debug_msg(": ");
                debug_msg_int((uint32_t)(dw_rx_timestamp >> 32));
                debug_msg(" ");
				debug_msg_int((uint32_t)(dw_rx_timestamp & 0xFFFFFFFF));
				debug_msg("\n");

                // Receive analysis about packet
                /*debug_msg("Estimated path loss: ");
                debug_msg_int((int)dw1000_estimatepathloss());
                debug_msg("\n");*/

				// Setup for next packet
				ranging_broadcast_received_task();

			} else {
				// Prepare for next packet
				dwt_rxenable(0);
			}
		}

	} else {
		// If an RX error has occurred, we're gonna need to setup the receiver again
		// (because dwt_rxreset within dwt_isr smashes everything without regard)
		if (rxd->event == DWT_SIG_RX_PHR_ERROR ||
			rxd->event == DWT_SIG_RX_ERROR ||
			rxd->event == DWT_SIG_RX_SYNCLOSS ||
			rxd->event == DWT_SIG_RX_SFDTIMEOUT ||
			rxd->event == DWT_SIG_RX_PTOTIMEOUT) {
			rangetest_set_ranging_broadcast_settings(ANCHOR, test_oa_scratch->ranging_broadcast_ss_num);
		} else {
			// Some other unknown error, not sure what to do
		}
	}

}