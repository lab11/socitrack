#include <string.h>
#include <stddef.h>
#include <stm32f0xx_gpio.h>

#include "deca_device_api.h"
#include "deca_regs.h"

#include "timer.h"
#include "delay.h"
#include "dw1000.h"
#include "app_test_init.h"
#include "firmware.h"
#include "board.h"
#include "SEGGER_RTT.h"

// Functions
static void send_poll ();
static void ranging_broadcast_send_task ();
static void tag_txcallback (const dwt_cb_data_t *txd);
static void tag_rxcallback (const dwt_cb_data_t *rxd);

// Do the TAG-specific init calls.
// We trust that the DW1000 is not in SLEEP mode when this is called.
void rangetest_tag_init (test_init_scratchspace_struct *app_scratchspace) {

	test_ot_scratch = app_scratchspace;

	// Initialize important variables inside scratchspace
	test_ot_scratch->pp_tag_poll_pkt = (struct rangetest_packet) {
		{ // 802.15.4 HEADER
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
		MSG_TYPE_PP_NOSLOTS_TAG_POLL,  // Message type
		0,                             // Sub Sequence number
		NUM_RANGING_BROADCASTS-1,
		RANGING_LISTENING_WINDOW_US,
		RANGING_LISTENING_SLOT_US,
		{ { 0 } }
	};

	// Make sure the SPI speed is slow for this function
	dw1000_spi_slow();

	// Setup callbacks to this TAG
	dwt_setcallbacks(tag_txcallback, tag_rxcallback, tag_rxcallback, tag_rxcallback);

    // Make sure the radio starts off
    dwt_forcetrxoff();

	// Allow data and ack frames
	dwt_enableframefilter(DWT_FF_DATA_EN | DWT_FF_ACK_EN);

	// Setup parameters of how the radio should work
	dwt_setdblrxbuffmode(TRUE);//FALSE);
	dwt_enableautoack(DW1000_ACK_RESPONSE_TIME);

	// Put source EUI in the packet
	dw1000_read_eui(test_ot_scratch->pp_tag_poll_pkt.header.sourceAddr);

	// Create a timer for use when sending ranging broadcast packets
	if (test_ot_scratch->tag_timer == NULL) {
		test_ot_scratch->tag_timer = timer_init();
	}

	// Make SPI fast now that everything has been setup
	dw1000_spi_fast();

	// Reset our state because nothing should be in progress if we call init()
	test_ot_scratch->state = TEST_ISTATE_IDLE;

	// Start timer
	rangetest_tag_start_ranging_event();
}

// This starts a ranging event by causing the tag to send a series of
// ranging broadcasts.
dw1000_err_e rangetest_tag_start_ranging_event () {
	dw1000_err_e err;

	//debug_msg("Start ranging event...\r\n");

	if (test_ot_scratch->state != TEST_ISTATE_IDLE) {
		// Cannot start a ranging event if we are currently busy with one.

		debug_msg("ERROR: Not in IDLE state, but in state ");
		debug_msg_int(test_ot_scratch->state);
		debug_msg("\n");
		return DW1000_BUSY;
	}

	// Make sure the DW1000 is awake. If it is, this will just return.
	// If the chip had to awoken, it will return with DW1000_WAKEUP_SUCCESS.
	err = dw1000_wakeup();
	if (err == DW1000_WAKEUP_SUCCESS) {
		// We woke the chip from sleep, so we need to reset the init params.
		// In theory, this isn't necessary, but things seem to work
		// better this way.

		dwt_rxreset();

		// Put back the TAG settings.
		rangetest_tag_init((void*)test_ot_scratch);

	} else if (err) {
		// Chip did not seem to wakeup. This is not good, so we have
		// to reset the application.
		debug_msg("ERROR: Couldn't wake up DW!\n");
		return err;
	}

	// Move to the broadcast state
	test_ot_scratch->state = TEST_ISTATE_BROADCASTS;

	// Clear state that we keep for each ranging event
	test_ot_scratch->ranging_broadcast_ss_num = 0;

	// Start a timer that will kick off the broadcast ranging events
	timer_start(test_ot_scratch->tag_timer, GLOSSY_UPDATE_INTERVAL_US, ranging_broadcast_send_task);

	//debug_msg("Started ranging...\n");

	return DW1000_NO_ERR;
}

// Called after the TAG has transmitted a packet.
static void tag_txcallback (const dwt_cb_data_t *txd) {

	if (txd->status & SYS_STATUS_TXFRS) {
		// Packet was sent successfully
        debug_msg("TAG transmitted a packet\n");

	} else {
		// Some error occurred, don't just keep trying to send packets.
		timer_stop(test_ot_scratch->tag_timer);

		debug_msg("ERROR: Failed in sending packet!\n");
	}

}

// Called when the tag receives a packet.
static void tag_rxcallback (const dwt_cb_data_t* rxd) {

	if (rxd->status & SYS_STATUS_ALL_RX_GOOD) {
		// Everything went right when receiving this packet.
		// We have to process it to ensure that it is a packet we are expecting
		// to get.

		/*uint64_t dw_rx_timestamp;
		uint8_t  buf[RANGETEST_TAG_MAX_RX_PKT_LEN];
		uint8_t  message_type;

		// Get the received time of this packet first
		dw_rx_timestamp = dw1000_readrxtimestamp();

		// Get the actual packet bytes
		dwt_readrxdata(buf, MIN(RANGETEST_TAG_MAX_RX_PKT_LEN, rxd->datalength), 0);
		message_type = buf[offsetof(struct pp_anc_final, message_type)];*/

	} else {
		// Packet was NOT received correctly. Need to do some re-configuring
		// as things get blown out when this happens. (Because dwt_rxreset
		// within dwt_isr smashes everything without regard.)
		if (rxd->status & SYS_STATUS_ALL_RX_ERR ||
			rxd->status & SYS_STATUS_ALL_RX_TO) {
		    debug_msg("WARNING: Rx error, status: ");
		    debug_msg_int((uint32_t)rxd->status);
		    debug_msg("\n");

			rangetest_set_ranging_broadcast_settings (APP_ROLE_INIT_NORESP, test_ot_scratch->ranging_broadcast_ss_num);
		}
	}

	// Reenable Rx
	dwt_rxenable(0);

}

// Send one of the ranging broadcast packets.
// After it sends the last of the subsequence this function automatically
// puts the DW1000 in RX mode.
static void send_poll () {
	int err;

	debug_msg("Sending poll ");
	debug_msg_int(test_ot_scratch->ranging_broadcast_ss_num);
	debug_msg("\n");

	// Record the packet length to send to DW1000
	uint16_t tx_len = sizeof(struct rangetest_packet);

	// Setup what needs to change in the outgoing packet
	test_ot_scratch->pp_tag_poll_pkt.header.seqNum++;
	test_ot_scratch->pp_tag_poll_pkt.subsequence = test_ot_scratch->ranging_broadcast_ss_num;

	// Make sure we're out of RX mode before attempting to transmit
	dwt_forcetrxoff();

	// Tell the DW1000 about the packet
	dwt_writetxfctrl(tx_len, 0, MSG_TYPE_RANGING);

	// Setup the time the packet will go out at, and save that timestamp
	uint32_t delay_time = dwt_readsystimestamphi32() + DW_DELAY_FROM_PKT_LEN(tx_len);
	delay_time &= 0xFFFFFFFE; //Make sure last bit is zero
	dw1000_setdelayedtrxtime(delay_time);

	// Write the data
	dwt_writetxdata(tx_len, (uint8_t*) &(test_ot_scratch->pp_tag_poll_pkt), 0);


	err = dwt_starttx(DWT_START_TX_DELAYED);

	// MP bug - TX antenna delay needs reprogramming as it is not preserved
	dwt_settxantennadelay(DW1000_ANTENNA_DELAY_TX);

	if (err != DWT_SUCCESS) {
		// This likely means our delay was too short when sending this packet
		if (test_ot_scratch->ranging_broadcast_ss_num == NUM_RANGING_BROADCASTS-1) {
			// If this occurs in the last round, it however also means that the anchor didnt reply with packets correctly
			debug_msg("ERROR: Delay too short for the last packet, setup took too long!\n");
		} else {
			debug_msg("ERROR: Delay too short, packet could not be sent!\n");
		}
	} else {

#if (BOARD_V == SQUAREPOINT)
#ifndef STM_DISABLE_LEDS
		// Toggle GREEN, turn off BLUE
		GPIO_WriteBit(STM_LED_BLUE_PORT,  STM_LED_BLUE_PIN,  Bit_SET);
		if (GPIO_ReadOutputDataBit(STM_LED_GREEN_PORT, STM_LED_GREEN_PIN)) {
			GPIO_WriteBit(STM_LED_GREEN_PORT, STM_LED_GREEN_PIN, Bit_RESET);
		} else {
			GPIO_WriteBit(STM_LED_GREEN_PORT, STM_LED_GREEN_PIN, Bit_SET);
		}
#endif
#endif
	}

}

// This is called for each broadcast ranging subsequence interval where
// the tag sends broadcast packets.
static void ranging_broadcast_send_task () {

	// Go ahead and setup and send a ranging broadcast
	rangetest_set_ranging_broadcast_settings(APP_ROLE_INIT_NORESP, test_ot_scratch->ranging_broadcast_ss_num);

	// Actually send the packet
	send_poll();
	test_ot_scratch->ranging_broadcast_ss_num += 1;
}

// SIMPLE TEST ---------------------------------------------------------------------------------------------------------

/* The frame sent in this example is an 802.15.4e standard blink. It is a 12-byte frame composed of the following fields:
 *     - byte 0: frame type (0xC5 for a blink).
 *     - byte 1: sequence number, incremented for each new frame.
 *     - byte 2 -> 9: device ID, see NOTE 1 below.
 *     - byte 10/11: frame check-sum, automatically set by DW1000.  */
static uint8 simpletest_tx_msg[] = {0xC5, 0, 'D', 'E', 'C', 'A', 'W', 'A', 'V', 'E', 0, 0};
/* Index to access to sequence number of the blink frame in the tx_msg array. */
#define SIMPLETEST_BLINK_FRAME_SN_IDX 1

/* Inter-frame delay period, in milliseconds. */
#define SIMPLETEST_TX_DELAY_MS 1000

dw1000_err_e simpletest_tag_start(void) {

	dw1000_spi_slow();
	if (dwt_initialise(DWT_LOADUCODE) == DWT_ERROR) {
		debug_msg("ERROR: Simpletest init failed!\n");
	}
	dw1000_spi_fast();

	/* Configure DW1000 */
	dwt_configure(&simpletest_config);

#if (BOARD_V == SQUAREPOINT)
#ifndef STM_DISABLE_LEDS
	// Turn off RED
	GPIO_WriteBit(STM_LED_RED_PORT, STM_LED_RED_PIN, Bit_SET);
#endif
#endif

	/* Loop forever sending frames periodically. */
	while(1)
	{
		/* Write frame data to DW1000 and prepare transmission.*/
		dwt_writetxdata(sizeof(simpletest_tx_msg), simpletest_tx_msg, 0); /* Zero offset in TX buffer. */
		dwt_writetxfctrl(sizeof(simpletest_tx_msg), 0, 0); /* Zero offset in TX buffer, no ranging. */

		/* Start transmission. */
		dwt_starttx(DWT_START_TX_IMMEDIATE);

		/* Poll DW1000 until TX frame sent event set.
         * STATUS register is 5 bytes long but, as the event we are looking at is in the first byte of the register, we can use this simplest API
         * function to access it.*/
		while (!(dwt_read32bitreg(SYS_STATUS_ID) & SYS_STATUS_TXFRS)) { };

		/* Clear TX frame sent event. */
		dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS);

		debug_msg("Sent message ");
		debug_msg_uint(simpletest_tx_msg[SIMPLETEST_BLINK_FRAME_SN_IDX]);
		debug_msg("\n");

#if (BOARD_V == SQUAREPOINT)
#ifndef STM_DISABLE_LEDS
		// Toggle GREEN, turn off BLUE
		GPIO_WriteBit(STM_LED_BLUE_PORT,  STM_LED_BLUE_PIN,  Bit_SET);
		if (GPIO_ReadOutputDataBit(STM_LED_GREEN_PORT, STM_LED_GREEN_PIN)) {
			GPIO_WriteBit(STM_LED_GREEN_PORT, STM_LED_GREEN_PIN, Bit_RESET);
		} else {
			GPIO_WriteBit(STM_LED_GREEN_PORT, STM_LED_GREEN_PIN, Bit_SET);
		}
#endif
#endif

		/* Execute a delay between transmissions. */
		deca_sleep(SIMPLETEST_TX_DELAY_MS);

		/* Increment the blink frame sequence number (modulo 256). */
		simpletest_tx_msg[SIMPLETEST_BLINK_FRAME_SN_IDX]++;
	}
}