#include <stddef.h>
#include <string.h>

#include "deca_device_api.h"
#include "deca_regs.h"
#include "dw1000.h"

#include "timer.h"
#include "delay.h"
#include "SEGGER_RTT.h"

#include "firmware.h"

#include "app_standard_common.h"
#include "app_standard_resp.h"

// APPLICATION STATE ---------------------------------------------------------------------------------------------------

standard_resp_scratchspace_struct *sr_scratch;

// STATIC FUNCTIONS ----------------------------------------------------------------------------------------------------

static void ranging_listening_window_setup();

// ---------------------------------------------------------------------------------------------------------------------

void standard_resp_init (standard_resp_scratchspace_struct *app_scratchspace) {

	// Save scratchspace
	sr_scratch = app_scratchspace;
	
	// Initialize this app's scratchspace
	sr_scratch->pp_anc_final_pkt = (struct pp_anc_final) {
		.ieee154_header_unicast = {
			.frameCtrl = {
				0x61, // FCF[0]: data frame, ack request, panid compression
				0xCC  // FCF[1]: ext source, ext destination
			},
			.seqNum = 0,
			.panID = {
				MODULE_PANID & 0xFF,
				MODULE_PANID >> 8,
			},
			.destAddr = { 0 },    // (blank for now)
			.sourceAddr = { 0 },  // (blank for now)
		},
		.message_type  = MSG_TYPE_PP_NOSLOTS_ANC_FINAL,
		.final_antenna = 0,
		.dw_time_sent  = 0,
		.TOAs          = { 0 },
	};

	// Load our EUI into the outgoing packet
	uint8_t * eui_array = standard_get_EUI();
	memcpy(sr_scratch->pp_anc_final_pkt.ieee154_header_unicast.sourceAddr, eui_array, EUI_LEN);

	// Need a timer
	if (sr_scratch->resp_timer == NULL) {
		sr_scratch->resp_timer = timer_init();
	}

	// Init the PRNG for determining when to respond to the tag
	raninit(&(sr_scratch->prng_state), eui_array[0] << 8 | eui_array[1]);

	// Reset our state because nothing should be in progress if we call init()
	sr_scratch->state = RSTATE_IDLE;
}

// Tell the anchor to start its job of being an anchor
dw1000_err_e standard_resp_start () {
	dw1000_err_e err;

	// Make sure the DW1000 is awake.
	err = dw1000_wakeup();
	if (err == DW1000_WAKEUP_SUCCESS) {
		// We did wake the chip, so reconfigure it properly
		// Put back the ANCHOR settings.
		standard_resp_init((void*)sr_scratch);
	} else if (err) {
		// Chip did not seem to wakeup. This is not good, so we have
		// to reset the application.
		return err;
	}

	// Also we start over in case the anchor was doing anything before
	sr_scratch->state = RSTATE_IDLE;

	standard_set_resp_active(TRUE);

	// Choose to wait in the first default position.
	// This could change to wait in any of the first NUM_CHANNEL-1 positions.
	standard_set_ranging_broadcast_subsequence_settings(TRUE, 0);

	// Obviously we want to be able to receive packets
	dwt_rxenable(0);

	sr_scratch->final_ack_received = FALSE;

	return DW1000_NO_ERR;
}

// Tell the anchor to stop ranging with TAGs.
// This cancels whatever the anchor was doing.
void standard_resp_stop () {
	// Put the anchor in SLEEP state. This is useful in case we need to
	// re-init some stuff after the anchor comes back alive.
	sr_scratch->state = RSTATE_IDLE;

	standard_set_resp_active(FALSE);

	// Stop the timer in case it was in use
	timer_stop(sr_scratch->resp_timer);

	// Put the DW1000 in SLEEP mode.
	dw1000_sleep();
}

// This is called by the periodic timer that tracks the tag's periodic
// broadcast ranging poll messages. This is responsible for setting the
// antenna and channel properties for the anchor.
static void ranging_broadcast_subsequence_task () {
	// When this timer is called it is time to start a new subsequence
	// slot, so we must increment our counter
	sr_scratch->ranging_broadcast_ss_num++;

	// Check if we are done listening for packets from the TAG. If we get
	// a packet on the last subsequence we won't get here, but if we
	// don't get that packet we need this check.
	if (sr_scratch->ranging_broadcast_ss_num > sr_scratch->ranging_operation_config.reply_after_subsequence) {
		ranging_listening_window_setup();

	} else {
		// Update the anchor listening settings
		standard_set_ranging_broadcast_subsequence_settings(TRUE, sr_scratch->ranging_broadcast_ss_num);

		// And re-enable RX. The set_broadcast_settings function disables tx and rx.
		dwt_rxenable(0);
	}
}

// Called at the beginning of each listening window for transmitting to
// the tag.
static void ranging_listening_window_task () {
	// Check if we are done transmitting to the tag.
	// Ideally we never get here, as an ack from the tag will cause us to stop
	// cycling through listening windows and put us back into a ready state.
	if (sr_scratch->ranging_listening_window_num == NUM_RANGING_CHANNELS) {
		// Go back to IDLE
		sr_scratch->state = RSTATE_IDLE;
		// Stop the timer for the window
		timer_stop(sr_scratch->resp_timer);

		// Restart being an anchor
		standard_resp_start();

	} else {

		if(!sr_scratch->final_ack_received){

			//debug_msg("Sending response to Tag\n");
			debug_msg("Number of packets: Antenna 1 - ");
			debug_msg_int(sr_scratch->resp_antenna_recv_num[0]);
			debug_msg("; Antenna 2 - ");
			debug_msg_int(sr_scratch->resp_antenna_recv_num[1]);
			debug_msg("; Antenna 3 - ");
			debug_msg_int(sr_scratch->resp_antenna_recv_num[2]);
			debug_msg("\n");

            /*debug_msg("; First index: ");
            debug_msg_int(sr_scratch->pp_anc_final_pkt.first_rxd_idx);
            debug_msg("; last index: ");
            debug_msg_int(sr_scratch->pp_anc_final_pkt.last_rxd_idx);
            debug_msg("\n");*/

			dwt_forcetrxoff();
	
			// Setup the channel and antenna settings
			standard_set_ranging_listening_window_settings(FALSE,
			                                             sr_scratch->ranging_listening_window_num,
			                                             sr_scratch->pp_anc_final_pkt.final_antenna);
	
			// Prepare the outgoing packet to send back to the
			// tag with our TOAs.
			sr_scratch->pp_anc_final_pkt.ieee154_header_unicast.seqNum = ranval(&(sr_scratch->prng_state)) & 0xFF;
			const uint16_t frame_len = sizeof(struct pp_anc_final);

			dwt_writetxfctrl(frame_len, 0, MSG_TYPE_RANGING);
	
			// Pick a slot to respond in. Generate a random number and mod it
			// by the number of slots
			uint32_t slot_time = ranval(&(sr_scratch->prng_state)) % (sr_scratch->ranging_operation_config.resp_reply_window_in_us -
			                                                           dw1000_packet_data_time_in_us(frame_len) -
			                                                           dw1000_preamble_time_in_us());
	
			dwt_setrxaftertxdelay(1);
	
			// Come up with the time to send this packet back to the
			// tag based on the slot we picked.
			uint32_t delay_time = dwt_readsystimestamphi32() +
				DW_DELAY_FROM_US(RANGING_LISTENING_WINDOW_PADDING_US + dw1000_preamble_time_in_us() + slot_time);
	
			delay_time &= 0xFFFFFFFE;
	
			// Set the packet to be transmitted later.
			dw1000_setdelayedtrxtime(delay_time);
	
			// Record the outgoing time in the packet. Do not take calibration into
			// account here, as that is done on all of the RX timestamps.
			sr_scratch->pp_anc_final_pkt.dw_time_sent = (((uint64_t) delay_time) << 8) + dw1000_gettimestampoverflow() + standard_get_txdelay_from_ranging_listening_window(sr_scratch->ranging_listening_window_num);
	
			// Send the response packet
			// TODO: handle if starttx errors. I'm not sure what to do about it, other than just wait for the next slot.
			dwt_starttx(DWT_START_TX_DELAYED | DWT_RESPONSE_EXPECTED);
			dwt_settxantennadelay(DW1000_ANTENNA_DELAY_TX);
			dwt_writetxdata(frame_len, (uint8_t*) &(sr_scratch->pp_anc_final_pkt), 0);
		}

		sr_scratch->ranging_listening_window_num++;
	}
}

// Prepare to transmit a response to the TAG.
// TODO: check to see if we should even bother. Did we get enough packets?
static void ranging_listening_window_setup () {
	// Stop iterating through timing channels
	timer_stop(sr_scratch->resp_timer);

	// We no longer need to receive and need to instead start transmitting.
	dwt_forcetrxoff();

	// Update our state to the TX response state
	sr_scratch->state = RSTATE_RESPONDING;
	// Set the listening window index
	sr_scratch->ranging_listening_window_num = 0;

	//debug_msg("Prepare to respond to TAG...\r\n");

	// Determine which antenna we are going to use for
	// the response.
	uint8_t max_packets = 0;
	uint8_t max_index = 0;
	for (uint8_t i=0; i<NUM_ANTENNAS; i++) {
		if (sr_scratch->resp_antenna_recv_num[i] > max_packets) {
			max_packets = sr_scratch->resp_antenna_recv_num[i];
			max_index = i;
		}
	}
	sr_scratch->pp_anc_final_pkt.final_antenna = max_index;

	// Now we need to setup a timer to iterate through
	// the response windows so we can send a packet
	// back to the tag
	timer_start(sr_scratch->resp_timer,
	            sr_scratch->ranging_operation_config.resp_reply_window_in_us + RANGING_LISTENING_WINDOW_PADDING_US*2,
	            ranging_listening_window_task);
}


// Called after a packet is transmitted. We don't need this so it is
// just empty.
void resp_txcallback (const dwt_cb_data_t *txd) {

	if (txd->status & SYS_STATUS_TXFRS) {
		// Packet was sent successfully
		//debug_msg("ANCHOR transmitted a packet\n");

	} else {
		// Some error occurred, don't just keep trying to send packets.
		debug_msg("ERROR: Failed in sending packet!\n");
	}

}

// Called when the radio has received a packet.
void resp_rxcallback (const dwt_cb_data_t *rxd, uint8_t * buf, uint64_t dw_rx_timestamp) {

	timer_disable_interrupt(sr_scratch->resp_timer);

	//debug_msg("Received DW1000 packet\r\n");

	if (rxd->status & SYS_STATUS_ALL_RX_GOOD) {

		// First check to see if this is an acknowledgement...
		// If so, we can stop sending ranging responses
		if((rxd->fctrl[0] & 0x03) == 0x02){  //This bit says whether this was an ack or not
			uint8_t cur_seq_num;
			dwt_readrxdata(&cur_seq_num, 1, 2);

			// Check to see if the sequence number matches the outgoing packet
			if(cur_seq_num == sr_scratch->pp_anc_final_pkt.ieee154_header_unicast.seqNum) {
                sr_scratch->final_ack_received = TRUE;
            }
		} else {

			// We process based on the first byte in the packet
			uint8_t message_type = buf[offsetof(struct pp_tag_poll, message_type)];

			if (message_type == MSG_TYPE_PP_NOSLOTS_TAG_POLL) {
				// This is one of the broadcast ranging packets from the tag
				struct pp_tag_poll* rx_poll_pkt = (struct pp_tag_poll*) buf;

                /*debug_msg("Received Poll message ");
                debug_msg_int(rx_poll_pkt->subsequence);
                debug_msg(": ");
                debug_msg_int((uint32_t)(dw_rx_timestamp >> 32));
                debug_msg(" ");
				debug_msg_int((uint32_t)(dw_rx_timestamp & 0xFFFFFFFF));
				debug_msg("\n");*/

				// Decide what to do with this packet
				if (sr_scratch->state == RSTATE_IDLE) {
					// We are currently not ranging with any tags.

					if (rx_poll_pkt->subsequence < NUM_RANGING_CHANNELS) {

					    /*debug_msg("Discovered new tag with EUI ");
					    debug_msg_int(rx_poll_pkt->header.sourceAddr[0] >> 4);
					    debug_msg_int(rx_poll_pkt->header.sourceAddr[0] & 0x0F);
					    debug_msg("\r\n");*/

						// We are idle and this is one of the first packets
						// that the tag sent. Start listening for this tag's
						// ranging broadcast packets.
						sr_scratch->state = RSTATE_RANGING;

						// Clear memory for this new tag ranging event
						memset(sr_scratch->pp_anc_final_pkt.TOAs, 0, sizeof(sr_scratch->pp_anc_final_pkt.TOAs));
						memset(sr_scratch->resp_antenna_recv_num, 0, sizeof(sr_scratch->resp_antenna_recv_num));

						// Record the EUI of the tag so that we don't get mixed up
						memcpy(sr_scratch->pp_anc_final_pkt.ieee154_header_unicast.destAddr, rx_poll_pkt->header.sourceAddr, 8);
						// Record which ranging subsequence the tag is on
						sr_scratch->ranging_broadcast_ss_num = rx_poll_pkt->subsequence;
						// Record the timestamp. Need to subtract off the TX+RX delay from each recorded
						// timestamp.
						sr_scratch->pp_anc_final_pkt.first_rxd_toa = dw_rx_timestamp - standard_get_rxdelay_from_subsequence(sr_scratch->ranging_broadcast_ss_num);
						sr_scratch->pp_anc_final_pkt.first_rxd_idx = sr_scratch->ranging_broadcast_ss_num;
						sr_scratch->pp_anc_final_pkt.TOAs[sr_scratch->ranging_broadcast_ss_num] =
							(dw_rx_timestamp - standard_get_rxdelay_from_subsequence(sr_scratch->ranging_broadcast_ss_num)) & 0xFFFF;
						// Also record parameters the tag has sent us about how to respond
						// (or other operational parameters).
						sr_scratch->ranging_operation_config.reply_after_subsequence = rx_poll_pkt->reply_after_subsequence;
						sr_scratch->ranging_operation_config.resp_reply_window_in_us = rx_poll_pkt->anchor_reply_window_in_us;
						sr_scratch->ranging_operation_config.resp_reply_slot_time_in_us = rx_poll_pkt->anchor_reply_slot_time_in_us;

						// Update the statistics we keep about which antenna
						// receives the most packets from the tag
						uint8_t recv_antenna_index = standard_subsequence_number_to_antenna(TRUE, rx_poll_pkt->subsequence);
						sr_scratch->resp_antenna_recv_num[recv_antenna_index]++;

						// Now we need to start our own state machine to iterate
						// through the antenna / channel combinations while listening
						// for packets from the same tag.
						timer_start(sr_scratch->resp_timer, RANGING_BROADCASTS_PERIOD_US, ranging_broadcast_subsequence_task);

					} else {
						// We found this tag ranging sequence late. We don't want
						// to use this because we won't get enough range estimates.
						// Just stay idle, but we do need to re-enable RX to
						// keep receiving packets.
						dwt_rxenable(0);
					}

				} else if (sr_scratch->state == RSTATE_RANGING) {
					// We are currently ranging with a tag, waiting for the various
					// ranging broadcast packets.

					// First check if this is from the same tag
					if (memcmp(sr_scratch->pp_anc_final_pkt.ieee154_header_unicast.destAddr, rx_poll_pkt->header.sourceAddr, 8) == 0) {
						// Same tag

						if (rx_poll_pkt->subsequence == sr_scratch->ranging_broadcast_ss_num) {
							// This is the packet we were expecting from the tag.
							// Record the TOA, and adjust it with the calibration value.
							sr_scratch->pp_anc_final_pkt.TOAs[sr_scratch->ranging_broadcast_ss_num] =
								(dw_rx_timestamp - standard_get_rxdelay_from_subsequence(sr_scratch->ranging_broadcast_ss_num)) & 0xFFFF;
							sr_scratch->pp_anc_final_pkt.last_rxd_toa = dw_rx_timestamp - standard_get_rxdelay_from_subsequence(sr_scratch->ranging_broadcast_ss_num);
							sr_scratch->pp_anc_final_pkt.last_rxd_idx = sr_scratch->ranging_broadcast_ss_num;

							// Update the statistics we keep about which antenna
							// receives the most packets from the tag
							uint8_t recv_antenna_index = standard_subsequence_number_to_antenna(TRUE, sr_scratch->ranging_broadcast_ss_num);
							sr_scratch->resp_antenna_recv_num[recv_antenna_index]++;

						} else {
							// Some how we got out of sync with the tag. Ignore the
							// range and catch up.
							sr_scratch->ranging_broadcast_ss_num = rx_poll_pkt->subsequence;
						}

						// Regardless, it's a good idea to immediately call the subsequence task and restart the timer
						timer_reset(sr_scratch->resp_timer, RANGING_BROADCASTS_PERIOD_US - 120); // Magic number calculated from timing

					} else {
						// Not the same tag, ignore
						dwt_rxenable(0);
					}
				} else {
					// We are in some other state, not sure what that means
					debug_msg("WARNING: Wrong state ");
					debug_msg_int(sr_scratch->state);
					debug_msg("\n");
				}

			} else {
				// We do want to enter RX mode again, however
				dwt_rxenable(0);
			}
		}

	} else {
		// If an RX error has occurred, we're gonna need to setup the receiver again
		// (because dwt_rxreset within dwt_isr smashes everything without regard)
		if (rxd->status & SYS_STATUS_ALL_RX_ERR ||
			rxd->status & SYS_STATUS_ALL_RX_TO) {
			debug_msg("ERROR: Rx error, status: ");
			debug_msg_uint((uint32_t)rxd->status);
			debug_msg("\n");

			standard_set_ranging_broadcast_subsequence_settings(TRUE, sr_scratch->ranging_broadcast_ss_num);
		} else {
			// Some other unknown error, not sure what to do
			debug_msg("ERROR: Unknown Rx issue!\n");
			dwt_rxenable(0);
		}
	}

	timer_enable_interrupt(sr_scratch->resp_timer);
}