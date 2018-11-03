#include <string.h>
#include <stddef.h>

#include "dw1000.h"

#include "timer.h"
#include "delay.h"
#include "SEGGER_RTT.h"

#include "host_interface.h"
#include "firmware.h"
#include "module_conf.h"

#include "app_standard_init.h"
#include "app_standard_resp.h"

// APPLICATION STATE ---------------------------------------------------------------------------------------------------

standard_init_scratchspace_struct *si_scratch;

// STATIC FUNCTIONS ----------------------------------------------------------------------------------------------------

static void send_poll ();
static void ranging_broadcast_subsequence_task ();
static void calculate_ranges ();
static void report_range ();

//----------------------------------------------------------------------------------------------------------------------

// Do the TAG-specific init calls.
// We trust that the DW1000 is not in SLEEP mode when this is called.
void standard_initiator_init (standard_init_scratchspace_struct *app_scratchspace) {

    // Save scratchspace
	si_scratch = app_scratchspace;

	// Initialize important variables inside scratchspace
	si_scratch->pp_tag_poll_pkt = (struct pp_tag_poll) {
		.header = { // 802.15.4 HEADER
			{
				0x41, // FCF[0]: data frame, panid compression
				0xC8  // FCF[1]: ext source address, compressed destination
			},
			0,        // Sequence number
			{
				MODULE_PANID & 0xFF, // PAN ID
				MODULE_PANID >> 8
			},
			{
				0xFF, // Destination address: broadcast
				0xFF
			},
			{ 0 }     // Source (blank for now)
		},
		// PACKET BODY
		.message_type = MSG_TYPE_PP_NOSLOTS_TAG_POLL,  // Message type
		.subsequence  = 0,                             // Sub Sequence number
		.footer = {
            { 0 }
		}
	};

	// Make sure the SPI speed is slow for this function
	/*dw1000_spi_slow();

	// FIXME: Is the double receiver buffer necessary / useful?
	//dwt_setdblrxbuffmode(TRUE);//FALSE);
	dwt_enableautoack(DW1000_ACK_RESPONSE_TIME);

	// Make SPI fast now that everything has been setup
	dw1000_spi_fast();*/

    // Put source EUI in the pp_tag_poll packet
    memcpy(si_scratch->pp_tag_poll_pkt.header.sourceAddr, standard_get_EUI(), EUI_LEN);

	// Create a timer for use when sending ranging broadcast packets
	if (si_scratch->init_timer == NULL) {
		si_scratch->init_timer = timer_init();
	}

	// Reset our state because nothing should be in progress if we call init()
	si_scratch->state = ISTATE_IDLE;
}

// This starts a ranging event by causing the tag to send a series of ranging broadcasts.
dw1000_err_e standard_init_start_ranging_event () {
	dw1000_err_e err;

	if (si_scratch->state != ISTATE_IDLE) {

	    if (si_scratch->state == ISTATE_TRANSITION_TO_ANC_FINAL) {
	        debug_msg("WARNING: State ISTATE_TRANSITION_TO_ANC_FINAL suggests no anchor replied...\n");
	        si_scratch->state = ISTATE_IDLE;
	    } else {
            // Cannot start a ranging event if we are currently busy with one.
            debug_msg("ERROR: Not in IDLE state, but in state ");
            debug_msg_int(si_scratch->state);
            debug_msg("\n");
            return DW1000_BUSY;
        }
	}

    //debug_msg("Start ranging event...\r\n");
    standard_set_resp_active(FALSE);
    standard_set_init_active(TRUE);

	// Make sure the DW1000 is awake. If it is, this will just return.
	// If the chip had to awoken, it will return with DW1000_WAKEUP_SUCCESS.
	err = dw1000_wakeup();
	if (err == DW1000_WAKEUP_SUCCESS) {
		// We woke the chip from sleep, so we need to reset the init params.
		// In theory, this isn't necessary, but things seem to work
		// better this way.

		dwt_rxreset();

		// Put back the TAG settings.
		standard_initiator_init((void*)si_scratch);

	} else if (err) {
		// Chip did not seem to wakeup. This is not good, so we have
		// to reset the application.
		debug_msg("ERROR: Couldn't wake up DW!\n");
		return err;
	}

	// Move to the broadcast state
	si_scratch->state = ISTATE_BROADCASTS;

	// Clear state that we keep for each ranging event
	memset(si_scratch->ranging_broadcast_ss_send_times, 0, sizeof(si_scratch->ranging_broadcast_ss_send_times));
	si_scratch->ranging_broadcast_ss_num = 0;

	// Start a timer that will kick off the broadcast ranging events
	timer_start(si_scratch->init_timer, RANGING_BROADCASTS_PERIOD_US, ranging_broadcast_subsequence_task);

	//debug_msg("Started ranging...\n");

	return DW1000_NO_ERR;
}

// Put the TAG into sleep mode
void standard_init_stop () {
	// Put the tag in the idle mode. It will eventually go to sleep as well,
	// but we just need to know it's idle
	si_scratch->state = ISTATE_IDLE;

	standard_set_init_active(FALSE);

	// Stop the timer in case it was in use
	timer_stop(si_scratch->init_timer);
	timer_free(si_scratch->init_timer);

	// Deschedule the tag's LWB slot since we're done
	//glossy_deschedule();

	// FIXME: Use the DW1000 library to put the chip to sleep
	//dw1000_sleep();

	//debug_msg("Put DW1000 into sleep...\r\n");
}

// Called after the TAG has transmitted a packet.
void init_txcallback (const dwt_cb_data_t *txd) {

	if (txd->status & SYS_STATUS_TXFRS) {
		// Packet was sent successfully
		//debug_msg("TAG transmitted a packet\n");

		// Check which state we are in to decide what to do.
		// We use TX_callback because it will get called after we have sent
		// all of the broadcast packets. (Now of course we will get this
		// callback multiple times, but that is ok.)
		if (si_scratch->state == ISTATE_TRANSITION_TO_ANC_FINAL) {
			// At this point we have sent all of our ranging broadcasts.
			// Now we move to listening for responses from anchors.

			// Init some state
			si_scratch->anchor_response_count = 0;

			//debug_msg("Finished ranging. Waiting for responses from anchors...\n");

		} else {
			// We don't need to do anything on TX done for any other states
		}

	} else {
		// Some error occurred, don't just keep trying to send packets.
		debug_msg("ERROR: Failed in sending packet!\n");
		timer_stop(si_scratch->init_timer);
	}

}

// Called when the tag receives a packet.
void init_rxcallback (const dwt_cb_data_t* rxd, uint8_t * buf, uint64_t dw_rx_timestamp) {

	if (rxd->status & SYS_STATUS_RXFCG) {
		// Everything went right when receiving this packet.
		// We have to process it to ensure that it is a packet we are expecting to get.

		// Clear the flags first
        clear_frame_event();

		// Get the message_type
		uint8_t message_type = buf[offsetof(struct pp_anc_final, message_type)];

		if (message_type == MSG_TYPE_PP_NOSLOTS_ANC_FINAL) {
			// This is what we were looking for, an ANC_FINAL packet
			struct pp_anc_final* anc_final;

			if (si_scratch->anchor_response_count >= MAX_NUM_ANCHOR_RESPONSES) {
				// Nowhere to store this, so we have to ignore this
				dwt_rxenable(0);
				return;
			}

			// Continue parsing the received packet
			anc_final = (struct pp_anc_final*) buf;

			// Check that we haven't already received a packet from this anchor.
			// The anchors should check for an ACK and not retransmit, but that
			// could still fail.
			bool anc_already_found = FALSE;
			for (uint8_t i=0; i<si_scratch->anchor_response_count; i++) {
				if (memcmp(si_scratch->anchor_responses[i].anchor_addr, anc_final->header.sourceAddr, PROTOCOL_EUI_LEN) == 0) {
					anc_already_found = TRUE;
					break;
				}
			}

			// Check that anchor actually has responses for us
			int resp_idx = -1;
			for (uint8_t i=0; i < anc_final->init_response_length; i++) {
			    if (memcmp(standard_get_EUI(), anc_final->init_responses[i].init_eui, PROTOCOL_EUI_LEN) == 0) {
			        resp_idx = i;
			        break;
			    }
			}

			// Only save this response if we haven't already seen this anchor
			if (!anc_already_found && (resp_idx >= 0)) {

                /*debug_msg("Received an Anchor response packet from ");
                helper_print_EUI(anc_final->header.sourceAddr);
                debug_msg("\r\n");

                debug_msg("First index: ");
                debug_msg_int(anc_final->init_responses[resp_idx].first_rxd_idx);
                debug_msg("; last index: ");
                debug_msg_int(anc_final->init_responses[resp_idx].last_rxd_idx);
                debug_msg("; length of packet: ");
                debug_msg_uint(rxd->datalength);
                debug_msg("\n");*/

				// Save the anchor address
				memcpy(si_scratch->anchor_responses[si_scratch->anchor_response_count].anchor_addr, anc_final->header.sourceAddr, PROTOCOL_EUI_LEN);

				// Save the anchor's list of when it received the tag broadcasts
				si_scratch->anchor_responses[si_scratch->anchor_response_count].tag_poll_first_TOA = anc_final->init_responses[resp_idx].first_rxd_toa;
				si_scratch->anchor_responses[si_scratch->anchor_response_count].tag_poll_first_idx = anc_final->init_responses[resp_idx].first_rxd_idx;
				si_scratch->anchor_responses[si_scratch->anchor_response_count].tag_poll_last_TOA = anc_final->init_responses[resp_idx].last_rxd_toa;
				si_scratch->anchor_responses[si_scratch->anchor_response_count].tag_poll_last_idx = anc_final->init_responses[resp_idx].last_rxd_idx;
				memcpy(si_scratch->anchor_responses[si_scratch->anchor_response_count].tag_poll_TOAs, anc_final->init_responses[resp_idx].TOAs, sizeof(anc_final->init_responses[resp_idx].TOAs));

				// Save the antenna the anchor chose to use when responding to us
				si_scratch->anchor_responses[si_scratch->anchor_response_count].anchor_final_antenna_index = anc_final->final_antenna;

				// Save when the anchor sent the packet we just received
				si_scratch->anchor_responses[si_scratch->anchor_response_count].anc_final_tx_timestamp = anc_final->dw_time_sent;

				// Save when we received the packet.
				// We have already handled the calibration values so we don't need to here.
				si_scratch->anchor_responses[si_scratch->anchor_response_count].anc_final_rx_timestamp = dw_rx_timestamp - standard_get_rxdelay_from_ranging_response_channel(RANGING_RESPONSE_CHANNEL_INDEX, anc_final->final_antenna);

				// Increment the number of anchors heard from
				si_scratch->anchor_response_count++;
			}

			// Reenable Rx
			dwt_rxenable(0);

		} else {
			// Reenable Rx
			dwt_rxenable(0);
		}

	} else {
		// Packet was NOT received correctly. Need to do some re-configuring
		// as things get blown out when this happens. (Because dwt_rxreset
		// within dwt_isr smashes everything without regard.)
		if ( (rxd->status & SYS_STATUS_ALL_RX_ERR) ||
		     (rxd->status & SYS_STATUS_ALL_RX_TO )   ) {
            /*debug_msg("WARNING: Rx error for INIT, status: ");
            debug_msg_int((uint32_t)rxd->status);
            debug_msg("\n");*/

			standard_set_ranging_response_settings(TRUE, 0);
			dwt_rxenable(0);
		} else {
			debug_msg("ERROR: Unknown error!");
			dwt_rxenable(0);
		}
	}

}

// Send one of the ranging broadcast packets.
// After it sends the last of the subsequence this function automatically
// puts the DW1000 in RX mode.
static void send_poll () {
	int err;

	/*debug_msg("Sending poll ");
	debug_msg_int(si_scratch->ranging_broadcast_ss_num);
	debug_msg("\n");*/

	// Record the packet length to send to DW1000
	uint16_t tx_len = sizeof(struct pp_tag_poll);

	// Vary reply window length depending on baudrate and preamble length
	//si_scratch->pp_tag_poll_pkt.anchor_reply_window_in_us = RANGING_LISTENING_WINDOW_US + dw1000_preamble_time_in_us() + dw1000_packet_data_time_in_us(sizeof(struct pp_anc_final));

	// Setup what needs to change in the outgoing packet
	si_scratch->pp_tag_poll_pkt.header.seqNum++;
	si_scratch->pp_tag_poll_pkt.subsequence = si_scratch->ranging_broadcast_ss_num;

	// Make sure we're out of RX mode before attempting to transmit
	dwt_forcetrxoff();

	// Tell the DW1000 about the packet
	dwt_writetxfctrl(tx_len, 0, MSG_TYPE_RANGING);
	debug_print_tx(tx_len);

	// Setup the time the packet will go out at, and save that timestamp
	uint32_t delay_time = dwt_readsystimestamphi32() + DW_DELAY_FROM_PKT_LEN(tx_len);
	delay_time &= 0xFFFFFFFE; //Make sure last bit is zero
	dw1000_setdelayedtrxtime(delay_time);

	// Take the TX+RX delay into account here by adding it to the time stamp
	// of each outgoing packet.
	si_scratch->ranging_broadcast_ss_send_times[si_scratch->ranging_broadcast_ss_num] =
		(((uint64_t) delay_time) << 8) + dw1000_gettimestampoverflow() + standard_get_txdelay_from_subsequence(FALSE, si_scratch->ranging_broadcast_ss_num);

	// Write the data
	dwt_writetxdata(tx_len, (uint8_t*) &(si_scratch->pp_tag_poll_pkt), 0);

	// Start the transmission
	if (si_scratch->ranging_broadcast_ss_num == NUM_RANGING_BROADCASTS-1) {
		// This is the last broadcast ranging packet, so we want to transition
		// to RX mode after this packet to receive the responses from the anchors.
		dwt_setrxaftertxdelay(1); // us
		err = dwt_starttx(DWT_START_TX_DELAYED | DWT_RESPONSE_EXPECTED);
	} else {
		err = dwt_starttx(DWT_START_TX_DELAYED);
	}

	// MP bug - TX antenna delay needs reprogramming as it is not preserved
	dwt_settxantennadelay(DW1000_ANTENNA_DELAY_TX);

	if (err != DWT_SUCCESS) {
		// This likely means our delay was too short when sending this packet
		if (si_scratch->ranging_broadcast_ss_num == NUM_RANGING_BROADCASTS-1) {
			// If this occurs in the last round, it however also means that the anchor didnt reply with packets correctly
			debug_msg("ERROR: Delay too short for the last packet, setup took too long!\n");
		} else {
			debug_msg("ERROR: Delay too short, packet could not be sent!\n");
		}
	}

}

// This is called for each broadcast ranging subsequence interval where
// the tag sends broadcast packets.
static void ranging_broadcast_subsequence_task () {

	if (si_scratch->ranging_broadcast_ss_num == NUM_RANGING_BROADCASTS-1) {
		// This is our last packet to send. Stop the timer so we don't generate
		// more packets.
		timer_stop(si_scratch->init_timer);

		// Also update the state to say that we are moving to RX mode
		// to listen for responses from the anchor
		si_scratch->state = ISTATE_TRANSITION_TO_ANC_FINAL;

	}

	// Go ahead and setup and send a ranging broadcast
	standard_set_ranging_broadcast_subsequence_settings(FALSE, si_scratch->ranging_broadcast_ss_num);

	// Actually send the packet
	send_poll();
	si_scratch->ranging_broadcast_ss_num += 1;
}

void standard_init_start_response_listening(uint8_t nr_slots) {

    //debug_msg("Listening for responses\n");

	standard_set_resp_active(FALSE);
	standard_set_init_active(TRUE);

	// Change state
	si_scratch->state = ISTATE_LISTENING;

	// Set the correct listening settings
	standard_set_ranging_response_settings(TRUE, 0);

	// Make SURE we're in RX mode!
	dwt_rxenable(0);

    // Turn off Rx mode inside an only partly used LWB response slot (e.g. when we use a new one with only a single responder, turn of Rx after that last one)
    // Note: nr_slots == 0 -> we do not set the timer, as it is not required to stop reception (no partly used slot)
    if ( (nr_slots > 0) && (si_scratch->init_timer != NULL) ) {
    	si_scratch->response_listening_timer_active = TRUE;
        timer_start(si_scratch->init_timer, LWB_SLOT_US / LWB_RESPONSES_PER_SLOT * nr_slots, standard_init_stop_response_listening);
    }

}

// This is called after the broadcasts have been sent in order to receive
// the responses from the anchors.
void standard_init_stop_response_listening () {

	if (si_scratch->response_listening_timer_active) {
		// First time the timer is triggerd (directly after it is set, i.e. when counter == 0), we do NOT yet stop
		// Setting timer to false will result in successful stopping listening when the timer is triggered after the configured number of slots
		si_scratch->response_listening_timer_active = FALSE;
		return;
	}

    // Stop the radio
    dwt_forcetrxoff();

    // End active stage of init; we do NOT want to enable Rx afterwards, as this is done by LWB
    standard_set_init_active(FALSE);

    // Turn off timer in case we used it to stop reception earlier and safe energy
    if (si_scratch->init_timer != NULL) {
        timer_stop(si_scratch->init_timer);
    }

    // This function finishes up this ranging event.
    report_range();

}

// Record ranges that the tag found.
void standard_set_ranges (int32_t* ranges_millimeters, anchor_responses_t* anchor_responses) {
	uint8_t buffer_index = 1;
	uint8_t num_anchor_ranges = 0;

	// Iterate through all ranges and copy the correct data into the ranges buffer.
	for (uint8_t i=0; i<MAX_NUM_ANCHOR_RESPONSES; i++) {
		if (ranges_millimeters[i] != INT32_MAX) {
			// This is a valid range
			memcpy(si_scratch->anchor_ids_ranges + buffer_index, anchor_responses[i].anchor_addr, PROTOCOL_EUI_LEN);
			buffer_index += PROTOCOL_EUI_LEN;
			memcpy(si_scratch->anchor_ids_ranges + buffer_index, &ranges_millimeters[i], sizeof(int32_t));
			buffer_index += sizeof(int32_t);
			num_anchor_ranges++;

			/*debug_msg("Range to anchor ");
			debug_msg_hex(anchor_responses[i].anchor_addr[0] >> 4);
			debug_msg_hex(anchor_responses[i].anchor_addr[0] & 0x0F);
			debug_msg(": ");

			if (ranges_millimeters[i] == STANDARD_INIT_RANGE_ERROR_NO_OFFSET) {
			    debug_msg("ERROR_NO_OFFSET");
			} else if (ranges_millimeters[i] == STANDARD_INIT_RANGE_ERROR_TOO_FEW_RANGES) {
			    debug_msg("ERROR_TOO_FEW_RANGES");
			} else if (ranges_millimeters[i] == STANDARD_INIT_RANGE_ERROR_MISC) {
			    debug_msg("ERROR_MISC");
			} else {
                debug_msg_uint((uint32_t) ranges_millimeters[i]);
            }
            debug_msg("\n");*/
		}
	}

	// Set the first byte as the number of ranges
	si_scratch->anchor_ids_ranges[0] = num_anchor_ranges;

	// Now let the host know so it can do something with the ranges.
	host_interface_notify_ranges(si_scratch->anchor_ids_ranges, (num_anchor_ranges*(PROTOCOL_EUI_LEN+sizeof(int32_t)))+1);
}

// Once we have heard from all of the anchors, calculate range.
static void report_range () {
	// New state
	si_scratch->state = ISTATE_CALCULATE_RANGE;

	/*debug_msg("Calculating & reporting ranges to ");
	debug_msg_int(si_scratch->anchor_response_count);
	debug_msg(" anchors\r\n");*/

	// Push data out over UART if configured to do so
#ifdef UART_DATA_OFFLOAD
	// Start things off with a packet header
	const uint8_t header[] = {0x80, 0x01, 0x80, 0x01};
	uart_write(4, header);

	// Send the timestamp
	uart_write(sizeof(uint8_t), &(si_scratch->anchor_response_count));

	// Send the send times
	uart_write(NUM_RANGING_BROADCASTS*sizeof(uint64_t), &(si_scratch->ranging_broadcast_ss_send_times));

	for (uint8_t anchor_index=0; anchor_index<si_scratch->anchor_response_count; anchor_index++) {
		// Some timing issues in UART, catch them
		const uint8_t data_header[] = {0x80, 0x80};
		uart_write(2, data_header);

		anchor_responses_t* aresp = &(si_scratch->anchor_responses[anchor_index]);

		uart_write(sizeof(anchor_responses_t), (uint8_t*) aresp);
	}

	//// Offload parameters appropriate for NLOS analysis
	//uint8_t buffer[2];
	//dwt_readfromdevice(RX_TIME_ID, RX_TIME_FP_AMPL1_OFFSET, 2, buffer);
	//uart_write(2, buffer);

	//dwt_readfromdevice(RX_FQUAL_ID, RX_EQUAL_FP_AMPL2_SHIFT/8, 2, buffer);
	//uart_write(2, buffer);

	//dwt_readfromdevice(RX_FQUAL_ID, RX_EQUAL_PP_AMPL3_SHIFT/8, 2, buffer);
	//uart_write(2, buffer);

	//dwt_readfromdevice(RX_FINFO_ID, RX_FINFO_RXPACC_SHIFT/8, 2, buffer);
	//uart_write(2, buffer);

	// Finish things off with a packet footer
	const uint8_t footer[] = {0x80, 0xfe};
	uart_write(2, footer);
#endif

    // Calculate ranges
    calculate_ranges();

	// Decide what we should do with these ranges. We can either report
	// these right back to the host, or we can try to get the anchors
	// to calculate location.
	//module_report_mode_e report_mode = standard_get_config()->report_mode;

	// We're done, so go to idle.
	si_scratch->state = ISTATE_IDLE;

	// Just need to send the ranges back to the host. Send the array
	// of ranges to the main application and let it deal with it.
	// This also returns control to the main application and signals
	// the end of the ranging event.
	standard_set_ranges(si_scratch->ranges_millimeters, si_scratch->anchor_responses);

	// Check if we should try to sleep after the ranging event.
	if (standard_get_config()->sleep_mode) {
		// Call stop() to sleep, it will be woken up automatically on
		// the next call to start_ranging_event().
		standard_init_stop();
	}

}


// After getting responses from anchors calculate the range to each anchor.
// These values are stored in si_scratch->ranges_millimeters.
static void calculate_ranges () {
	// Clear array, don't use memset
	for (uint8_t i=0; i<MAX_NUM_ANCHOR_RESPONSES; i++) {
		si_scratch->ranges_millimeters[i] = INT32_MAX;
	}

	// Iterate through all anchors to calculate the range from the tag
	// to each anchor
	for (uint8_t anchor_index=0; anchor_index<si_scratch->anchor_response_count; anchor_index++) {
		anchor_responses_t* aresp = &(si_scratch->anchor_responses[anchor_index]);

		debug_msg("Anchor ID: ");
		helper_print_EUI(aresp->anchor_addr, PROTOCOL_EUI_LEN);
		debug_msg("; First index: ");
		debug_msg_int(aresp->tag_poll_first_idx);
		debug_msg("; last index: ");
		debug_msg_int(aresp->tag_poll_last_idx);
		debug_msg("\n");

		// Since the rxd TOAs are compressed to 16 bits, we first need to decompress them back to 64-bit quantities
		uint64_t tag_poll_TOAs[NUM_RANGING_BROADCASTS];
		memset(tag_poll_TOAs, 0, sizeof(tag_poll_TOAs));

		// Get an estimate of clock offset
		double approx_clock_offset = (double)(aresp->tag_poll_last_TOA - aresp->tag_poll_first_TOA) / (double)(si_scratch->ranging_broadcast_ss_send_times[aresp->tag_poll_last_idx] - si_scratch->ranging_broadcast_ss_send_times[aresp->tag_poll_first_idx]);

		// First put in the TOA values that are known
		tag_poll_TOAs[aresp->tag_poll_first_idx] = aresp->tag_poll_first_TOA;
		tag_poll_TOAs[aresp->tag_poll_last_idx] = aresp->tag_poll_last_TOA;

		// Then interpolate between the two to find the high 48 bits which fit best
		uint8_t ii;
		for(ii=aresp->tag_poll_first_idx+1; ii < aresp->tag_poll_last_idx; ii++){
			uint64_t estimated_TOA = aresp->tag_poll_first_TOA + (approx_clock_offset*(si_scratch->ranging_broadcast_ss_send_times[ii] - si_scratch->ranging_broadcast_ss_send_times[aresp->tag_poll_first_idx]));

			uint64_t actual_TOA = (estimated_TOA & 0xFFFFFFFFFFFF0000ULL) + aresp->tag_poll_TOAs[ii];

			// Make corrections if we're off by more than 0x7FFF
			if(actual_TOA < estimated_TOA - 0x7FFF)
				actual_TOA += 0x10000;
			else if(actual_TOA > estimated_TOA + 0x7FFF)
				actual_TOA -= 0x10000;

			// We're done -- store it...
			tag_poll_TOAs[ii] = actual_TOA;
		}

		// First need to calculate the crystal offset between the anchor and tag.
		// To do this, we need to get the timestamps at the anchor and tag
		// for packets that are repeated. In the current scheme, the first
		// three packets are repeated, where three is the number of channels.
		// If we get multiple matches, we take the average of the clock offsets.
		uint8_t valid_offset_calculations = 0;
		double offset_ratios_sum = 0.0;
		for (uint8_t j=0; j<NUM_RANGING_CHANNELS; j++) {
			uint8_t first_broadcast_index = j;
			uint8_t last_broadcast_index = NUM_RANGING_BROADCASTS - NUM_RANGING_CHANNELS + j;
			uint64_t first_broadcast_send_time = si_scratch->ranging_broadcast_ss_send_times[first_broadcast_index];
			uint64_t first_broadcast_recv_time = tag_poll_TOAs[first_broadcast_index];
			uint64_t last_broadcast_send_time  = si_scratch->ranging_broadcast_ss_send_times[last_broadcast_index];
			uint64_t last_broadcast_recv_time  = tag_poll_TOAs[last_broadcast_index];

			// Now lets check that the anchor actually received both of these
			// packets. If it didn't then this isn't valid.
			if (first_broadcast_recv_time == 0 || last_broadcast_recv_time == 0) {
				// A packet was dropped (or the anchor wasn't listening on the
				// first channel). This isn't useful so we skip it.
				continue;
			}

			// Calculate the "multiplier for the crystal offset between tag and anchor".
			// (last_recv-first_recv) / (last_send-first_send)
			double offset_anchor_over_tag_item = ((double) last_broadcast_recv_time - (double) first_broadcast_recv_time) /
				((double) last_broadcast_send_time - (double) first_broadcast_send_time);

			// Add this to the running sum for the average
			offset_ratios_sum += offset_anchor_over_tag_item;
			valid_offset_calculations++;
		}

		// If we didn't get any matching pairs in the first and last rounds then we have to skip this anchor.
		/*debug_msg("Number of matching pairs: ");
		debug_msg_uint(valid_offset_calculations);
		debug_msg("\n");*/

		if (valid_offset_calculations == 0) {
			si_scratch->ranges_millimeters[anchor_index] = STANDARD_INIT_RANGE_ERROR_NO_OFFSET;
			continue;
		}

		// Calculate the average clock offset multiplier
		double offset_anchor_over_tag = offset_ratios_sum / (double) valid_offset_calculations;

		// Now we need to use the one packet we have from the anchor
		// to calculate a one-way time of flight measurement so that we can
		// account for the time offset between the anchor and tag (i.e. the
		// tag and anchors are not synchronized). We will use this TOF
		// to calculate ranges from all of the other polls the tag sent.
		// To do this, we need to match the anchor_antenna, tag_antenna, and
		// channel between the anchor response and the correct tag poll.
		uint8_t ss_index_matching = standard_get_ss_index_from_settings(aresp->anchor_final_antenna_index, RANGING_RESPONSE_CHANNEL_INDEX);

		// Exit early if the corresponding broadcast wasn't received
		if(tag_poll_TOAs[ss_index_matching] == 0){
			si_scratch->ranges_millimeters[anchor_index] = STANDARD_INIT_RANGE_ERROR_NO_OFFSET;
			continue;
		}

		uint64_t matching_broadcast_send_time = si_scratch->ranging_broadcast_ss_send_times[ss_index_matching];
		uint64_t matching_broadcast_recv_time = tag_poll_TOAs[ss_index_matching];
		uint64_t response_send_time  = aresp->anc_final_tx_timestamp;
		uint64_t response_recv_time  = aresp->anc_final_rx_timestamp;

		double two_way_TOF = (((double) response_recv_time - (double) matching_broadcast_send_time)*offset_anchor_over_tag) -
			((double) response_send_time - (double) matching_broadcast_recv_time);
		double one_way_TOF = two_way_TOF / 2.0;


		// Declare an array for sorting the ranges.
		int distances_millimeters[NUM_RANGING_BROADCASTS] = {0};
		uint8_t num_valid_distances = 0;

		// Next we calculate the TOFs for each of the poll messages the tag sent.
		for (uint8_t broadcast_index=0; broadcast_index<NUM_RANGING_BROADCASTS; broadcast_index++) {
			uint64_t broadcast_send_time = si_scratch->ranging_broadcast_ss_send_times[broadcast_index];
			uint64_t broadcast_recv_time = tag_poll_TOAs[broadcast_index];

			// Check that the anchor actually received the tag broadcast.
			// We use 0 as a sentinel for the anchor not receiving the packet.
			if (broadcast_recv_time == 0) {
				continue;
			}

			// We use the reference packet (that we used to calculate one_way_TOF)
			// to compensate for the unsynchronized clock.
			int64_t broadcast_anchor_offset = (int64_t) broadcast_recv_time - (int64_t) matching_broadcast_recv_time;
			int64_t broadcast_tag_offset = (int64_t) broadcast_send_time - (int64_t) matching_broadcast_send_time;
			double TOF = (double) broadcast_anchor_offset - (((double) broadcast_tag_offset) * offset_anchor_over_tag) + one_way_TOF;

			int distance_millimeters = dwtime_to_millimeters(TOF);

			/*debug_msg("Calculated range: ");
			debug_msg_int(distance_millimeters);
			debug_msg("\n");*/

			// Check that the distance we have at this point is at all reasonable
			if (distance_millimeters >= MIN_VALID_RANGE_MM && distance_millimeters <= MAX_VALID_RANGE_MM) {
				// Add this to our sorted array of distances
				insert_sorted(distances_millimeters, distance_millimeters, num_valid_distances);
				num_valid_distances++;
			}
		}

		// Check to make sure that we got enough ranges from this anchor.
		// If not, we just skip it.
		if (num_valid_distances < MIN_VALID_RANGES_PER_ANCHOR) {
			si_scratch->ranges_millimeters[anchor_index] = STANDARD_INIT_RANGE_ERROR_TOO_FEW_RANGES;
			continue;
		}


		// Now that we have all of the calculated ranges from all of the tag
		// broadcasts we can calculate some percentile range.
		uint8_t bot = (num_valid_distances*RANGE_PERCENTILE_NUMERATOR)/RANGE_PERCENTILE_DENOMENATOR;
		uint8_t top = bot+1;
		// bot represents the whole index of the item at the percentile.
		// Then we are going to use the remainder decimal portion to get
		// a scaled value to add to that base. And we are going to do this
		// without floating point, so buckle up.
		// EXAMPLE: if the 10th percentile would be index 3.4, we do:
		//                  distances[3] + 0.4*(distances[4]-distances[3])
		int32_t result = distances_millimeters[bot] +
			(((distances_millimeters[top]-distances_millimeters[bot]) * ((RANGE_PERCENTILE_NUMERATOR*num_valid_distances)
			 - (bot*RANGE_PERCENTILE_DENOMENATOR))) / RANGE_PERCENTILE_DENOMENATOR);

		// Save the result
		si_scratch->ranges_millimeters[anchor_index] = result;
		// si_scratch->ranges_millimeters[anchor_index] = (int32_t) one_way_TOF;
		// si_scratch->ranges_millimeters[anchor_index] = dm;
		// si_scratch->ranges_millimeters[anchor_index] = distances_millimeters[bot];
		// si_scratch->ranges_millimeters[anchor_index] = si_scratch->ranging_broadcast_ss_send_times[0];
		// si_scratch->ranges_millimeters[anchor_index] = ss_index_matching;
		// si_scratch->ranges_millimeters[anchor_index] = num_valid_distances;
		if (si_scratch->ranges_millimeters[anchor_index] == INT32_MAX) {
			si_scratch->ranges_millimeters[anchor_index] = STANDARD_INIT_RANGE_ERROR_MISC;
		}
	}
}