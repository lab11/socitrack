#include <stddef.h>
#include <string.h>

#include "dw1000.h"

#include "timer.h"
#include "delay.h"
#include "SEGGER_RTT.h"

#include "firmware.h"
#include "module_conf.h"

#include "app_standard_common.h"
#include "app_standard_init.h"
#include "app_standard_resp.h"

// APPLICATION STATE ---------------------------------------------------------------------------------------------------

standard_resp_scratchspace_struct *sr_scratch;

// STATIC FUNCTIONS ----------------------------------------------------------------------------------------------------

static void standard_resp_responding_task();
static void standard_resp_send_response();

// Helper functions
static uint8_t  get_final_antenna();
static void 	write_anc_final_to_buffer();
static uint16_t get_anc_final_packet_length(struct pp_anc_final * packet);

// ---------------------------------------------------------------------------------------------------------------------

void standard_resp_init (standard_resp_scratchspace_struct *app_scratchspace) {

	// Save scratchspace
	sr_scratch = app_scratchspace;
	
	// Initialize this app's scratchspace
	sr_scratch->pp_anc_final_pkt = (struct pp_anc_final) {
		.header = {
			.frameCtrl = {
				0x41, // FCF[0]: data frame, ack request, panid compression
				0xC8  // FCF[1]: ext source, ext destination
			},
			.seqNum = 0,
			.panID = {
				MODULE_PANID & 0xFF,
				MODULE_PANID >> 8,
			},
			.destAddr = {
					0xFF, // Destination address: broadcast
					0xFF
			},
			.sourceAddr = { 0 },  // (blank for now)
		},
		.message_type  		  = MSG_TYPE_PP_NOSLOTS_ANC_FINAL,
		.final_antenna 		  = 0,
		.dw_time_sent  		  = 0,
		.init_response_length = 0
	};

	memset(sr_scratch->pp_anc_final_pkt.init_responses, 0, sizeof(sr_scratch->pp_anc_final_pkt.init_responses));

	// Load our EUI into the outgoing packet
	uint8_t * eui_array = standard_get_EUI();
	memcpy(sr_scratch->pp_anc_final_pkt.header.sourceAddr, eui_array, EUI_LEN);

	// Clear our buffer
	memset(sr_scratch->pp_anc_final_pkt_buffer, 0, sizeof(sr_scratch->pp_anc_final_pkt_buffer));

	// Clear state
	memset(sr_scratch->resp_antenna_recv_num,   0, sizeof(sr_scratch->resp_antenna_recv_num));

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
dw1000_err_e standard_resp_start (bool delayed_rx) {
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
	if (delayed_rx) {
	    // We need to have a gap after the Initiator sent its polls so that the responders can successfully iterate their buffers; in this gap, we do NOT listen but simply wait
        uint32_t rx_delay_time = (dwt_readsystimestamphi32() + DW_DELAY_FROM_US(RANGING_BROADCASTS_GAP_US)) & 0xFFFFFFFE;
        dwt_setdelayedtrxtime(rx_delay_time);
        dwt_rxenable(DWT_START_RX_DELAYED);
	} else {
        dwt_rxenable(0);
    }

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
	timer_free(sr_scratch->resp_timer);

	// Put the DW1000 in SLEEP mode.
	//dw1000_sleep();
}

void standard_resp_sleep () {

    sr_scratch->state = RSTATE_SLEEP;

    timer_stop(sr_scratch->resp_timer);
}

void standard_resp_continue () {

    sr_scratch->state = RSTATE_IDLE;
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
	if (sr_scratch->ranging_broadcast_ss_num > (NUM_RANGING_BROADCASTS - 1) ) {

		// The current initiator is done with ranging
		sr_scratch->state = RSTATE_PENDING;

		// Stop iterating through timing channels
		timer_stop(sr_scratch->resp_timer);

		// Prepare for next initiators
		sr_scratch->pp_anc_final_pkt.init_response_length++;

		// Now, we either wait for the next initiator or will start responding with our packet when its our turn
		standard_resp_start(TRUE);

	} else {
		// Update the anchor listening settings
		standard_set_ranging_broadcast_subsequence_settings(TRUE, sr_scratch->ranging_broadcast_ss_num);

		// And re-enable RX. The set_broadcast_settings function disables tx and rx.
		dwt_rxenable(0);
	}
}

// The node is scheduled as a responder in this slot; let's listen for other responses and then send our own one
void standard_resp_trigger_response (uint8_t slot_nr) {

    sr_scratch->resp_window_timeslot = slot_nr;
    sr_scratch->resp_window_nr       = 0;

    /*debug_msg("Triggered response window, responding in slot ");
    debug_msg_uint(slot_nr);
    debug_msg("\n");*/

    if (slot_nr == 0) {
    	// Directly transmit
    	standard_resp_send_response();
    } else {
		// We wait for our message slot inside the LWB slot
		sr_scratch->resp_window_nr = 0;
		timer_start(sr_scratch->resp_timer, LWB_SLOT_US / LWB_RESPONSES_PER_SLOT, standard_resp_responding_task);
	}
}

static void standard_resp_responding_task () {

    debug_msg("Triggered response task with window nr ");
    debug_msg_uint(sr_scratch->resp_window_nr);
    debug_msg("\n");

    if (sr_scratch->resp_window_nr == sr_scratch->resp_window_timeslot) {
        // Our slot
        standard_resp_send_response();

		timer_stop(sr_scratch->resp_timer);
    }

    sr_scratch->resp_window_nr++;
}

// Called for transmitting responses to the tag.
static void standard_resp_send_response () {

	// Update our state to the TX response state
	sr_scratch->state = RSTATE_RESPONDING;

	standard_set_init_active(FALSE);
	standard_set_resp_active(TRUE);

	//debug_msg("Prepare to respond to INIT...\r\n");

	// Determine which antenna we are going to use for the response.
	sr_scratch->pp_anc_final_pkt.final_antenna = get_final_antenna();

	/*debug_msg("Sending response to initiator ");
	for (uint8_t i = 0; i < sr_scratch->pp_anc_final_pkt.init_response_length; i++) {
		debug_msg_uint(sr_scratch->pp_anc_final_pkt.init_responses[i].init_eui[0]);
		debug_msg(" ");
	}
	debug_msg("\n");*/

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

	// Turn off Rx and switch to Tx
	dwt_forcetrxoff();

	// Setup the channel and antenna settings
	standard_set_ranging_response_settings( FALSE,
											sr_scratch->pp_anc_final_pkt.final_antenna);

	// Prepare the outgoing packet to send back to the tags with our TOAs.
	sr_scratch->pp_anc_final_pkt.header.seqNum = ranval(&(sr_scratch->prng_state)) & 0xFF;

	const uint16_t frame_len = get_anc_final_packet_length(&sr_scratch->pp_anc_final_pkt);
	dwt_writetxfctrl(frame_len, 0, MSG_TYPE_RANGING);
    debug_print_tx(frame_len);

	// Leave enough time to copy packet
	// TODO: Verify that frames are not too long and have sufficient time to be transmitted and copied
	uint32_t delay_time = dwt_readsystimestamphi32() + DW_DELAY_FROM_US(RANGING_RESPONSE_PADDING_US + dw1000_preamble_time_in_us());
	delay_time &= 0xFFFFFFFE;

	// Set the packet to be transmitted later.
	dw1000_setdelayedtrxtime(delay_time);

	// Record the outgoing time in the packet.
	sr_scratch->pp_anc_final_pkt.dw_time_sent = (((uint64_t) delay_time) << 8) + dw1000_gettimestampoverflow() + standard_get_txdelay_from_ranging_response_channel(RANGING_RESPONSE_CHANNEL_INDEX, sr_scratch->pp_anc_final_pkt.final_antenna);

    // Write our packet to the buffer
    write_anc_final_to_buffer();

	// Send the response packet
    //dwt_setrxaftertxdelay(1);
    // TODO: handle if starttx errors. I'm not sure what to do about it, other than just wait for the next slot.
	dwt_starttx(DWT_START_TX_DELAYED);
	dwt_settxantennadelay(DW1000_ANTENNA_DELAY_TX);

    // Set transmit buffer
    uint16_t num_transactions = frame_len / MAX_SPI_TRANSACTION_BYTES;

    if (frame_len % MAX_SPI_TRANSACTION_BYTES) {
        num_transactions++;
    }

    for (uint16_t i = 0; i < num_transactions; i++) {
        uint16_t write_bytes = MAX_SPI_TRANSACTION_BYTES;

        if (i == (num_transactions - 1)) {
            write_bytes = frame_len - (i * MAX_SPI_TRANSACTION_BYTES);
        }

        dwt_writetxdata(write_bytes, sr_scratch->pp_anc_final_pkt_buffer + (i * MAX_SPI_TRANSACTION_BYTES), i * MAX_SPI_TRANSACTION_BYTES);
    }

	// Done with our response; Go back to IDLE
	sr_scratch->state = RSTATE_IDLE;

    // Clear state for next round
    sr_scratch->pp_anc_final_pkt.init_response_length = 0;
    memset(sr_scratch->resp_antenna_recv_num, 0, sizeof(sr_scratch->resp_antenna_recv_num));

}


// Called after a packet is transmitted. We don't need this so it is
// just empty.
void resp_txcallback (const dwt_cb_data_t *txd) {

	if (txd->status & SYS_STATUS_TXFRS) {
		// Packet was sent successfully
		//debug_msg("ANCHOR transmitted a packet\n");

		// As we sent our single packet, we switch back to INIT mode to catch the rest of the packets
		if (standard_is_init_enabled()) {
		    standard_set_resp_active(FALSE);
		    standard_set_init_active(TRUE);
		}

	} else {
		// Some error occurred, don't just keep trying to send packets.
		debug_msg("ERROR: Failed in sending packet!\n");
	}

}

// Called when the radio has received a packet.
void resp_rxcallback (const dwt_cb_data_t *rxd, uint8_t * buf, uint64_t dw_rx_timestamp) {

	timer_disable_interrupt(sr_scratch->resp_timer);

	//debug_msg("Received DW1000 packet\r\n");

	if (rxd->status & SYS_STATUS_RXFCG) {

        // Clear the flags first
        clear_frame_event();

		// If LWB counter is above expected value, prevent a timeout as we successfully receive a (unknown) packet and other nodes are still scheduled
		glossy_reset_counter_offset();

		// We process based on the first byte in the packet
		uint8_t message_type = buf[offsetof(struct pp_tag_poll, message_type)];

		if (message_type == MSG_TYPE_PP_NOSLOTS_TAG_POLL) {
			// This is one of the broadcast ranging packets from the tag
			struct pp_tag_poll* rx_poll_pkt = (struct pp_tag_poll*) buf;

			/*debug_msg("Received Poll message ");
			debug_msg_int(rx_poll_pkt->subsequence);
			debug_msg(": ");
			debug_msg_uint((uint32_t)(dw_rx_timestamp >> 32));
			debug_msg(" ");
			debug_msg_uint((uint32_t)(dw_rx_timestamp & 0xFFFFFFFF));
			debug_msg("\n");*/

			// Decide what to do with this packet
			if ( (sr_scratch->state == RSTATE_IDLE) || (sr_scratch->state == RSTATE_PENDING) ) {
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
					uint8_t idx = sr_scratch->pp_anc_final_pkt.init_response_length;

					// Clear memory for this new tag ranging event
					memset(sr_scratch->pp_anc_final_pkt.init_responses[idx].TOAs, 0, sizeof(sr_scratch->pp_anc_final_pkt.init_responses[idx].TOAs));

					// Record the EUI of the tag so that we don't get mixed up
					memcpy(sr_scratch->pp_anc_final_pkt.init_responses[idx].init_eui, rx_poll_pkt->header.sourceAddr, PROTOCOL_EUI_LEN);

					// Record which ranging subsequence the tag is on
					sr_scratch->ranging_broadcast_ss_num = rx_poll_pkt->subsequence;

					// Record the timestamp. Need to subtract off the TX+RX delay from each recorded timestamp.
					sr_scratch->pp_anc_final_pkt.init_responses[idx].first_rxd_toa = dw_rx_timestamp - standard_get_rxdelay_from_subsequence(TRUE, sr_scratch->ranging_broadcast_ss_num);
					sr_scratch->pp_anc_final_pkt.init_responses[idx].first_rxd_idx = sr_scratch->ranging_broadcast_ss_num;

					sr_scratch->pp_anc_final_pkt.init_responses[idx].TOAs[sr_scratch->ranging_broadcast_ss_num] = (dw_rx_timestamp - standard_get_rxdelay_from_subsequence(TRUE, sr_scratch->ranging_broadcast_ss_num)) & 0xFFFF;

					// Update the statistics we keep about which antenna receives the most packets from the tag
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

				uint8_t idx = sr_scratch->pp_anc_final_pkt.init_response_length;

				// First check if this is from the same tag
				if (memcmp(sr_scratch->pp_anc_final_pkt.init_responses[idx].init_eui, rx_poll_pkt->header.sourceAddr, PROTOCOL_EUI_LEN) == 0) {
					// Same tag

					if (rx_poll_pkt->subsequence == sr_scratch->ranging_broadcast_ss_num) {
						// This is the packet we were expecting from the tag.
						// Record the TOA, and adjust it with the calibration value.
						sr_scratch->pp_anc_final_pkt.init_responses[idx].TOAs[sr_scratch->ranging_broadcast_ss_num] =
							(dw_rx_timestamp - standard_get_rxdelay_from_subsequence(TRUE, sr_scratch->ranging_broadcast_ss_num)) & 0xFFFF;
						sr_scratch->pp_anc_final_pkt.init_responses[idx].last_rxd_toa = dw_rx_timestamp - standard_get_rxdelay_from_subsequence(TRUE, sr_scratch->ranging_broadcast_ss_num);
						sr_scratch->pp_anc_final_pkt.init_responses[idx].last_rxd_idx = sr_scratch->ranging_broadcast_ss_num;

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

					debug_msg("WARNING: Expected poll from ");
					debug_msg_uint(sr_scratch->pp_anc_final_pkt.init_responses[idx].init_eui[0]);
					debug_msg(", but received one from ");
					debug_msg_uint(rx_poll_pkt->header.sourceAddr[0]);
					debug_msg("\n");

					dwt_rxenable(0);
				}
			} else {
				// We are in some other state, not sure what that means
				debug_msg("WARNING: Wrong state ");
				debug_msg_int(sr_scratch->state);
				debug_msg("\n");
			}

		} else if (message_type == MSG_TYPE_PP_NOSLOTS_ANC_FINAL) {

            debug_msg("WARNING: Received FINAL message as responder!\n");

            // We do want to enter RX mode again, however
            dwt_rxenable(0);

        } else {

	        debug_msg("WARNING: Received unknown message of type: ");
	        debug_msg_uint(message_type);
	        debug_msg("\n");

            // We do want to enter RX mode again, however
            dwt_rxenable(0);
		}

	} else {
		// If an RX error has occurred, we're gonna need to setup the receiver again
		// (because dwt_rxreset within dwt_isr smashes everything without regard)
		if ( (rxd->status & SYS_STATUS_ALL_RX_ERR) ||
		     (rxd->status & SYS_STATUS_ALL_RX_TO )   ) {
			/*debug_msg("WARNING: Rx error for RESP, status: ");
			debug_msg_uint((uint32_t)rxd->status);
			debug_msg("\n");*/

			standard_set_ranging_broadcast_subsequence_settings(TRUE, sr_scratch->ranging_broadcast_ss_num);
			dwt_rxenable(0);
		} else {
			// Some other unknown error, not sure what to do
			debug_msg("ERROR: Unknown Rx issue!\n");
			dwt_rxenable(0);
		}
	}

	timer_enable_interrupt(sr_scratch->resp_timer);
}


// Helper functions ----------------------------------------------------------------------------------------------------

static uint8_t get_final_antenna() {

	uint8_t max_packets  = 0;
	uint8_t index_first  = 0;


	// Find the best one
	for (uint8_t i = 0; i < NUM_ANTENNAS; i++) {

		if (sr_scratch->resp_antenna_recv_num[i] > max_packets) {
			max_packets = sr_scratch->resp_antenna_recv_num[i];
			index_first  = i;
		}
	}

	return index_first;

    /* We are currently choosing a fitting option on the receiver (INIT) side; we could however also do the same thing on the sender side as written below
    uint8_t index_second = 0;

    // If we do not have received any requests, just reply with the "maximum" one, which will be the default
    if (sr_scratch->pp_anc_final_pkt.init_response_length == 0) {
        return index_first;
    }

    // Otherwise, test that at least for the first response, we did actually observe the packet for the channel on which we respond
    // If this is not the case, the initiator will not be able to calculate the distances and must throw the packet away
    if ( (sr_scratch->pp_anc_final_pkt.init_responses[0].TOAs[index_first] > 0) ||
         (NUM_ANTENNAS == 1)                                                      ) {
        debug_msg("Responding on best channel ");
        debug_msg_uint(index_first);
        debug_msg("\n");

        return index_first;
    } else {
        debug_msg("WARNING: Did not observe packet for best antenna, switching to second-best option...\n");
    }

    // As we have not observed that packet, we will have to use the second-best option
    max_packets = 0;
    for (uint8_t i = 0; i < NUM_ANTENNAS; i++) {

        if ( (sr_scratch->resp_antenna_recv_num[i] > max_packets) &&
                (i != index_first)                                  ){
            max_packets = sr_scratch->resp_antenna_recv_num[i];
            index_second  = i;
        }
    }

    if ( (sr_scratch->pp_anc_final_pkt.init_responses[0].TOAs[index_second] > 0) ||
         (NUM_ANTENNAS == 2)													   ) {
        debug_msg("Responding on second-best channel ");
        debug_msg_uint(index_second);
        debug_msg("\n");

        return index_second;
    } else {
        debug_msg("WARNING: Did not observe packet for second-best antenna, switching to last option...\n");
    }

    // Lets hope that we at least got a single one of those packets by sending on the last remaining option
    uint8_t index_third = (uint8_t)NUM_ANTENNAS - (index_first + index_second);
    debug_msg("Responding on last channel ");
    debug_msg_uint(index_third);
    debug_msg("\n");

    return index_third;*/
}

static void write_anc_final_to_buffer() {

	uint8_t * dest = sr_scratch->pp_anc_final_pkt_buffer;

	// Clear buffer
	memset(dest, 0, sizeof(sr_scratch->pp_anc_final_pkt_buffer));

	// Fill buffer:
	uint16_t offset = 0;

	// Header + Constant part
	uint16_t header_length = sizeof(struct ieee154_header_broadcast) + MSG_PP_ANC_FINAL_PAYLOAD_DEFAULT_LENGTH;

	memcpy(dest + offset, &sr_scratch->pp_anc_final_pkt, header_length);
	offset += header_length;

	// Responses
	uint16_t responses_length = sr_scratch->pp_anc_final_pkt.init_response_length * sizeof(struct pp_anc_final_init_response);

	memcpy(dest + offset, sr_scratch->pp_anc_final_pkt.init_responses, responses_length);
	offset += responses_length;

	// Footer
	memcpy(dest + offset, &sr_scratch->pp_anc_final_pkt.footer, sizeof(struct ieee154_footer));
}

static uint16_t get_anc_final_packet_length(struct pp_anc_final * packet) {

	uint16_t packet_length  = sizeof(struct ieee154_header_broadcast) + MSG_PP_ANC_FINAL_PAYLOAD_DEFAULT_LENGTH + sizeof(struct ieee154_footer);
			 packet_length += packet->init_response_length * sizeof(struct pp_anc_final_init_response);

	/*debug_msg("Anchor response packet size: ");
	debug_msg_uint(packet_length);
	debug_msg("\n");*/

	return packet_length;
}