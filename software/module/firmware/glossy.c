
#include <string.h>
#include <stddef.h>
#include <math.h>

#include "stm32f0xx_gpio.h"

#include "dw1000.h"
#include "deca_regs.h"

#include "board.h"
#include "host_interface.h"
#include "timer.h"
#include "prng.h"
#include "SEGGER_RTT.h"

#include "app_standard_common.h"
#include "app_standard_init.h"
#include "app_standard_resp.h"

#include "glossy.h"

// APPLICATION STATE ---------------------------------------------------------------------------------------------------

static glossy_role_e _role;

static stm_timer_t*  _glossy_timer;

static ranctx        _prng_state;

static uint8_t  _last_sync_depth;
static uint64_t _last_sync_timestamp;
static uint32_t _last_sync_epoch;
static uint64_t _last_time_sent;
static uint64_t _glossy_flood_timeslot_corrected_us;
static uint32_t _last_delay_time;
static uint8_t  _xtal_trim;
static uint8_t  _last_xtal_trim;
static bool     _sending_sync;
static uint32_t _lwb_counter;
static uint8_t  _cur_glossy_depth;
static bool     _glossy_currently_flooding;

static bool  	_lwb_in_sync;
static uint8_t  _lwb_master_eui[EUI_LEN];
static bool     _lwb_sched_en;
static bool     _lwb_scheduled_init;        // _lwb_scheduled_X is a boolean variable determining whether the node has been scheduled as X in the current round
static bool     _lwb_scheduled_resp;
static uint32_t _lwb_num_scheduled_init;
static uint32_t _lwb_num_scheduled_resp;    // _lwb_num_scheduled_X is the number of X scheduled for the next round (only relevant for the Glossy master)
static uint32_t _lwb_num_scheduled_hybrid;
static uint32_t _lwb_num_timeslots_total;
static uint32_t _lwb_num_init;              // _lwb_num_X           is the number of X in the current round
static uint32_t _lwb_num_resp;
static uint32_t _lwb_num_timeslots_cont;
static uint32_t _lwb_num_signalling_used;
static uint8_t  _lwb_prev_signalling_type;
static uint8_t  _lwb_prev_signalling_eui[EUI_LEN];
static uint32_t _lwb_timeslot_init;         // _lwb_timeslot_X is the assigned timeslot of the present node in the current schedule
static uint32_t _lwb_timeslot_resp;
static double _clock_offset;

static struct pp_sched_flood  _sync_pkt;
static        uint8_t         _sync_pkt_buffer[sizeof(struct pp_sched_flood)]; // We will copy the bytes into this buffer before sending the packet

static struct pp_signal_flood _signal_pkt;

static uint8_t _init_sched_euis  [PROTOCOL_INIT_SCHED_MAX][PROTOCOL_EUI_LEN];
static uint8_t _resp_sched_euis  [PROTOCOL_RESP_SCHED_MAX][PROTOCOL_EUI_LEN];
static uint8_t _hybrid_sched_euis[PROTOCOL_HYBRID_SCHED_MAX][PROTOCOL_EUI_LEN];
static uint8_t _sched_timeouts   [PROTOCOL_INIT_SCHED_MAX + PROTOCOL_RESP_SCHED_MAX + PROTOCOL_HYBRID_SCHED_MAX];

#ifdef GLOSSY_PER_TEST
static uint32_t _total_syncs_sent;
static uint32_t _total_syncs_received;
#endif

// STATIC FUNCTIONS ----------------------------------------------------------------------------------------------------

static void    prepare_schedule_signal();
static uint8_t   schedule_init(uint8_t * eui);
static uint8_t   schedule_resp(uint8_t * eui);
static uint8_t   schedule_hybrid(uint8_t * eui);
static uint8_t deschedule_init(uint8_t * eui);
static uint8_t deschedule_resp(uint8_t * eui);
static uint8_t deschedule_hybrid(uint8_t * eui);
static int 	   check_if_scheduled(uint8_t * array, uint8_t array_length);

static uint8_t glossy_get_resp_listening_slots_a();
//             glossy_get_resp_listening_slots_b -> public

static void    glossy_lwb_round_task();
static void    lwb_increment_sched_timeout();
static void    lwb_adjust_contention_period();

static void    write_data_to_sync();
static void	   write_sync_to_packet_buffer();
static uint8_t get_sync_packet_length(struct pp_sched_flood * packet);
static void    lwb_send_sync(uint32_t delay_time);

// Helpers
static uint8_t ceil_fraction(uint32_t nominator, uint32_t denominator);
static uint8_t get_master_candidate();
static void	   save_schedule_information(uint8_t* buffer);
static void    restore_schedule_information(uint8_t* buffer);

// ---------------------------------------------------------------------------------------------------------------------


void glossy_init(glossy_role_e role, uint8_t config_master_eui){

    // Load EUI from application
	uint8_t * my_eui = standard_get_EUI();

	// Init Glossy packet: Schedule
	_sync_pkt = (struct pp_sched_flood) {
		.header = {
			.frameCtrl = {
				0x41,
				0xC8
			},
			.seqNum = 0,
			.panID = {
				MODULE_PANID & 0xFF,
				MODULE_PANID >> 8
			},
			.destAddr = {
				0xFF,
				0xFF
			},
			.sourceAddr = { 0 },
		},
		.message_type = MSG_TYPE_PP_GLOSSY_SYNC,
		.round_length = 0,
#ifdef PROTOCOL_ENABLE_GLOBAL_TIMESTAMPS
        .epoch_time = 0,
#endif
        .init_schedule_length = 0,
        .resp_schedule_length = 0,
        //.eui_array            = { 0 },
        .footer = {
                { 0 }
		}
	};

	memset(_sync_pkt.eui_array, 0, sizeof(_sync_pkt.eui_array));

	// Add source address
    memcpy(_sync_pkt.header.sourceAddr, standard_get_EUI(), EUI_LEN);

	// Clear its buffer
	memset(_sync_pkt_buffer, 0, sizeof(_sync_pkt_buffer));

	// Init Glossy packet: Schedule request
	_signal_pkt = (struct pp_signal_flood) {
		.header 	  = _sync_pkt.header,
		.message_type = MSG_TYPE_PP_GLOSSY_SIGNAL,
		.info_type    = SIGNAL_UNDEFINED,
		.device_eui   = { 0 },
		.footer = {
				{ 0 }
		}
	};

	// Seed our random number generator with our EUI
	raninit(&_prng_state, my_eui[0] << 8 | my_eui[1]);

	// Save our role
	_role 					= role;

	// Init to zero
	_glossy_timer           = NULL;
	_last_sync_depth		= 0;
	_last_sync_timestamp    = 0;
	_last_sync_epoch		= 0;
	_last_time_sent			= 0;
	_last_delay_time 		= 0;
	_lwb_in_sync 			= FALSE;
	_sending_sync 			= FALSE;
	_lwb_counter 			= 0;
	_cur_glossy_depth		= 0;
	_glossy_currently_flooding = FALSE;
	_lwb_sched_en 			   = FALSE;
	_lwb_scheduled_init        = FALSE;
	_lwb_scheduled_resp        = FALSE;
	_lwb_num_scheduled_init    = 0;
	_lwb_num_scheduled_resp    = 0;
	_lwb_num_scheduled_hybrid  = 0;
	_lwb_num_timeslots_total   = 0;
	_lwb_num_init              = 0;
	_lwb_num_resp              = 0;
	_lwb_num_timeslots_cont    = PROTOCOL_STANDARD_CONT_LENGTH;
	_lwb_num_signalling_used   = 0;
	_lwb_prev_signalling_type  = SIGNAL_UNDEFINED;
	_lwb_timeslot_init		   = 0;
	_lwb_timeslot_resp		   = 0;
	_clock_offset			   = 0;

	memset(_lwb_master_eui,          0, EUI_LEN);
	memset(_lwb_prev_signalling_eui, 0, EUI_LEN);

	memset(_init_sched_euis,   0, sizeof(_init_sched_euis));
    memset(_resp_sched_euis,   0, sizeof(_resp_sched_euis));
	memset(_hybrid_sched_euis, 0, sizeof(_hybrid_sched_euis));
    memset(_sched_timeouts,    0, sizeof(_sched_timeouts));

	_glossy_flood_timeslot_corrected_us = (uint64_t)(DW_DELAY_FROM_US(GLOSSY_FLOOD_TIMESLOT_US) & 0xFFFFFFFE) << 8;

#ifdef GLOSSY_PER_TEST
	_total_syncs_sent = 0;
#endif

	// Set crystal trim to mid-range
	_last_xtal_trim = 0;
	_xtal_trim 		= 15;
	dwt_setxtaltrim(_xtal_trim);

	// If the anchor, let's kick off a task which unconditionally kicks off sync messages with depth = 0
	if(role == GLOSSY_MASTER){
		_lwb_in_sync = TRUE;
		memcpy(_lwb_master_eui, standard_get_EUI(), EUI_LEN);
		host_interface_notify_master_change(_lwb_master_eui, EUI_LEN);

#ifdef DW1000_USE_OTP
		uint8 ldok = OTP_SF_OPS_KICK | OTP_SF_OPS_SEL_TIGHT;
		dwt_writetodevice(OTP_IF_ID, OTP_SF, 1, &ldok); // set load LDE kick bit
#endif

		_last_time_sent = dwt_readsystimestamphi32() & 0xFFFFFFFE;

		// If Master itself is also ranging, add it as the first initiator in the schedule
		if (standard_is_init_enabled()) {

            // Add to schedule
            schedule_init(standard_get_EUI());

            // Local bookkeeping
		    _lwb_sched_en  		= TRUE;
		    _lwb_scheduled_init = TRUE;
		    _lwb_timeslot_init  = 0;
		}

        // If Master itself is also ranging, add it as the first responder in the schedule
		if (standard_is_resp_enabled()) {

            // Add to schedule
            schedule_resp(standard_get_EUI());

            // Local bookkeeping
            _lwb_sched_en  		= TRUE;
            _lwb_scheduled_resp = TRUE;
            _lwb_timeslot_resp  = 0;
		}
	} else {
		// Store Master EUI
		_lwb_master_eui[0] = config_master_eui;
	}

	// The glossy timer acts to synchronize everyone to a common timebase
    _glossy_timer = timer_init();
}

void glossy_start() {

    // Enable the schedule request
    lwb_set_sched_request(TRUE);

    // Kick-off the Glossy timer
    timer_start(_glossy_timer, LWB_SLOT_US, glossy_lwb_round_task);
}

void glossy_stop() {

	// Disable schedule request
	lwb_set_sched_request(FALSE);

	// Stop the Glossy timer
	timer_stop(_glossy_timer);
	timer_free(_glossy_timer);
}

void glossy_deschedule(){

	// Prevent re-scheduling
	_lwb_sched_en = FALSE;

	if (standard_is_init_enabled() && standard_is_resp_enabled()) {
		_signal_pkt.info_type = DESCHED_REQUEST_HYBRID;
		memcpy(_signal_pkt.device_eui, standard_get_EUI(), PROTOCOL_EUI_LEN);
	} else if (standard_is_init_enabled()) {
		_signal_pkt.info_type = DESCHED_REQUEST_INIT;
		memcpy(_signal_pkt.device_eui, standard_get_EUI(), PROTOCOL_EUI_LEN);
	} else if (standard_is_resp_enabled()) {
		_signal_pkt.info_type = DESCHED_REQUEST_RESP;
		memcpy(_signal_pkt.device_eui, standard_get_EUI(), PROTOCOL_EUI_LEN);
	} else {
		debug_msg("WARNING: Configuration does not allow descheduling!\n");
	}
}

void glossy_enable_reception(uint32_t starttime) {

	// Start receiving
	dwt_forcetrxoff();
	dw1000_update_channel(LWB_CHANNEL);
	dw1000_choose_antenna(LWB_ANTENNA);

	if (starttime > 0) {
		dwt_setdelayedtrxtime(starttime);
		dwt_rxenable(1);
	} else {
		dwt_rxenable(0);
	}
}

void glossy_listen_for_next_sync() {

	// Get the number of DW time units for which we did not receive a new schedule
	uint64_t curr_timestamp = dw1000_correct_timestamp((uint64_t)dwt_readsystimestamphi32() << 8); //32 highest bits, bitshifted to compare to the 40bit timestamp
	uint64_t out_of_sync_dw = (curr_timestamp - _last_sync_timestamp) >> 8; // Subtract and directly shift back again to get the 32bit number

	// Correct the time we turn the receiver back on based on the maximal clock drift
	uint64_t max_clock_drift_dw = (uint64_t)( (DW_CLOCK_DRIFT_MAX_PPM * out_of_sync_dw) / 1e6 );

	// Calculate the number of rounds we have not received anything
	uint64_t out_of_sync_rounds = out_of_sync_dw / GLOSSY_UPDATE_INTERVAL_DW;

	// Turn the receiver on based on the maximal drift
	uint64_t delay_time = (_last_sync_timestamp >> 8) + (out_of_sync_rounds + 1) * GLOSSY_UPDATE_INTERVAL_DW - (max_clock_drift_dw + GLOSSY_SCHEDULE_RECV_SLACK_DW);

	// Only take the 32bit high part of the timestamp and make sure last bit is zero
	delay_time &= 0xFFFFFFFE;

	// FIXME: It might occasionally happen that the out_of_sync_dw is miscalculated (due to DW bugs at Tx/Rx); for reliability reasons, we just start to always listen if we havent heard for more than 10 rounds
	if (out_of_sync_rounds > 10) {
	    delay_time = 0; // Triggers immediate listening
	    debug_msg("WARNING: Out of sync for more than 10 rounds!\n");
	}

	/*debug_msg("Current time: ");
	debug_msg_uint((curr_timestamp >> 40));
	debug_msg(" | ");
	debug_msg_uint((curr_timestamp >> 8) & 0xFFFFFFFF);
	debug_msg("; out of sync: ");
	debug_msg_uint(out_of_sync_dw >> 32);
	debug_msg(" | ");
	debug_msg_uint(out_of_sync_dw & 0xFFFFFFFF);
	debug_msg("; no sync for ");
	debug_msg_uint(out_of_sync_rounds);
	debug_msg(" rounds; delayed timestamp: ");
	debug_msg_uint(delay_time);
	debug_msg("\n");*/

	glossy_enable_reception((uint32_t)delay_time);
}

static void glossy_lwb_round_task() {
	_lwb_counter++;

    /*debug_msg("LWB slot: ");
    debug_msg_int(_lwb_counter);
    debug_msg("\n");*/

    // The different round stages:
	uint8_t lwb_slot_sync 	    = 0;
	uint8_t lwb_slot_init_start = 1;
	uint8_t lwb_slot_resp_start = (uint8_t)( lwb_slot_init_start + _lwb_num_init * LWB_SLOTS_PER_RANGE);
	uint8_t lwb_slot_cont_start = (uint8_t)( lwb_slot_resp_start + ceil_fraction(_lwb_num_resp, LWB_RESPONSES_PER_SLOT));
	uint8_t lwb_slot_last		= (uint8_t)( _lwb_num_timeslots_total - 1 );

	// Debugging information
	/*if (_lwb_counter == 1) {
        debug_msg("Round info: init_start ");
        debug_msg_uint(lwb_slot_init_start);
        debug_msg("; resp_start ");
        debug_msg_uint(lwb_slot_resp_start);
        debug_msg("; cont_start ");
        debug_msg_uint(lwb_slot_cont_start);
        debug_msg("; last ");
        debug_msg_uint(lwb_slot_last);
        debug_msg("\n");
    }*/

	// NOTE: lwb_slot_last != "(GLOSSY_UPDATE_INTERVAL_US/LWB_SLOT_US)-1"
	// lwb_slot_last is the last used slot and will be relevant for contention
	// "(GLOSSY_UPDATE_INTERVAL_US/LWB_SLOT_US)-1" is the slot directly before the distribution of the next sync and will be used for setup

	if(_role == GLOSSY_MASTER) {

		// LWB Slot N-C: During the first timeslot, put ourselves back into RX mode to listen for schedule requests
		if(_lwb_counter == lwb_slot_cont_start){

            if (standard_is_init_active()) {
                // Stop if still listening for responses
                standard_init_stop_response_listening();
            }

		    //debug_msg("Listening for requests...\n");

			dwt_rxenable(0);
#ifdef GLOSSY_ANCHOR_SYNC_TEST
			dw1000_choose_antenna(LWB_ANTENNA);
#endif

		// LWB Slot N-1: Last timeslot is used by the master to schedule the next glossy sync packet
		} else if((  _lwb_counter == ( (GLOSSY_UPDATE_INTERVAL_US/LWB_SLOT_US) - 1) )                                 ||
				  ( (_lwb_counter >  ( (GLOSSY_UPDATE_INTERVAL_US/LWB_SLOT_US) + 1) ) && ( (_lwb_counter % 10) == 0) )  ){

			dwt_forcetrxoff();

		#ifdef GLOSSY_PER_TEST
			_total_syncs_sent++;
			if(_total_syncs_sent >= 10000){
				while(1){
					dwt_write32bitreg(3, _total_syncs_received);
				}
			}
		#endif

			dw1000_update_channel(LWB_CHANNEL);
			dw1000_choose_antenna(LWB_ANTENNA);

			// Trigger timeouts
			lwb_increment_sched_timeout();

			// Adjust contention depending on usage
			lwb_adjust_contention_period();

			// Copy information to sync packet
			write_data_to_sync();

			// Copy information into buffer
			write_sync_to_packet_buffer();

#ifdef PROTOCOL_ENABLE_GLOBAL_TIMESTAMPS
			// Clear epoch time (only transmit once when actually fresh)
			_sync_pkt.epoch_time = 0;
#endif

			_last_time_sent += GLOSSY_UPDATE_INTERVAL_DW;

			// BUG FIX: If the Tx callback is not triggered, the schedule will never be sent anymore; to prevent this, we resent the schedule if the counter is higher than expected
			if ( (_lwb_counter > ( (GLOSSY_UPDATE_INTERVAL_US/LWB_SLOT_US) + 1) ) && ( (_lwb_counter % 10) == 0) ) {
				_last_time_sent = ( dwt_readsystimestamphi32() + GLOSSY_SCHEDULE_RETRY_SLACK_US) & 0xFFFFFFFE;
				debug_msg("WARNING: Did not successfully send the schedule; retrying in round ");
				debug_msg_int(_lwb_counter);
				debug_msg("\n");
			}

			// Enable Tx callback to reset the LWB counter
            _sending_sync = TRUE;

			// Trigger send operation
			lwb_send_sync(_last_time_sent);

			debug_msg("Sent LWB schedule\r\n");

#if (BOARD_V == SQUAREPOINT)
			// Signal that distributing schedule by turning on WHITE (will blink and be turned off after 10ms)
			GPIO_WriteBit(STM_LED_RED_PORT,   STM_LED_RED_PIN,   Bit_RESET);
			GPIO_WriteBit(STM_LED_BLUE_PORT,  STM_LED_BLUE_PIN,  Bit_RESET);
			GPIO_WriteBit(STM_LED_GREEN_PORT, STM_LED_GREEN_PIN, Bit_RESET);
#endif
		// LWB Slot > N: Invalid counter value
		} else if (_lwb_counter > ( (GLOSSY_UPDATE_INTERVAL_US/LWB_SLOT_US) - 1) ) {
			debug_msg("WARNING: LWB counter overshooting, currently at ");
			debug_msg_int(_lwb_counter);
			debug_msg("\n");
		}

	} else if (_role == GLOSSY_SLAVE) {

		// OUT OF SYNC: Force ourselves into RX mode if we still haven't received any sync floods
		if( (!_lwb_in_sync || (_lwb_counter > (GLOSSY_UPDATE_INTERVAL_US/LWB_SLOT_US)) ) && ((_lwb_counter % 10) == 0) ) {

			if (_last_sync_timestamp == 0) {
				// We have never received a sync before
				glossy_enable_reception(0);
			} else {
				// We have received a sync before but have gotten out of sync unfortunately
				glossy_listen_for_next_sync();
			}

			debug_msg("Not in sync with Glossy master (yet); current counter: ");
			debug_msg_uint(_lwb_counter);
			debug_msg("\n");

#if (BOARD_V == SQUAREPOINT)
			// Signal normal operation by turning on BLUE
			if (GPIO_ReadOutputDataBit(STM_LED_BLUE_PORT, STM_LED_BLUE_PIN)) {
				GPIO_WriteBit(STM_LED_GREEN_PORT, STM_LED_GREEN_PIN, Bit_SET);
				GPIO_WriteBit(STM_LED_BLUE_PORT,  STM_LED_BLUE_PIN,  Bit_RESET);
			}
#endif

#ifdef PROTOCOL_ENABLE_TIMEOUT
			// Timeout: if have not heard from Master for more than Timeout, leave network and wait for neighbour discovery
			if (_lwb_counter > GLOSSY_MASTER_TIMEOUT_PERIOD) {

			    debug_msg("WARNING: Timing out, leaving this network...\n");

				// Stop reception
				glossy_stop();

				standard_stop();

				// Deschedule from network
				memset(_lwb_master_eui, 0, sizeof(_lwb_master_eui));
				host_interface_notify_master_change(_lwb_master_eui, EUI_LEN);
			}
#endif

#ifdef PROTOCOL_ENABLE_MASTER_TAKEOVER
			// Timeout: If have not heard from Master for more than given time, try to take over network with highest slave
			if (_lwb_counter > GLOSSY_MASTER_TAKEOVER_PERIOD) {

				debug_msg("WARNING: Timing out, trying network takeover\n");

				// Stop reception
				glossy_stop();

				standard_stop();

				uint8_t master_candidate_eui = get_master_candidate();

				// Inform host
				_lwb_master_eui[0] = master_candidate_eui;
				host_interface_notify_master_change(_lwb_master_eui, EUI_LEN);

				if (standard_get_config()->my_EUI[0] == master_candidate_eui) {
					// This node will try to take over
					debug_msg("INFO: Node will try to take over network\n");

					// Backup scheduling information
					uint8_t eui_copy_buffer[PROTOCOL_INIT_SCHED_MAX + PROTOCOL_RESP_SCHED_MAX + PROTOCOL_HYBRID_SCHED_MAX][PROTOCOL_EUI_LEN] = { 0 };
					save_schedule_information((uint8_t*)eui_copy_buffer);

					// Restart
					glossy_init(GLOSSY_MASTER, master_candidate_eui);

					// Fill in copied information
					restore_schedule_information((uint8_t*)eui_copy_buffer);

					// Let's try and get this new network running
					standard_start();

				} else {
					// Configure new master and switch
					debug_msg("INFO: Node ");
					debug_msg_uint(master_candidate_eui);
					debug_msg(" will take over network\n");

					// Restart
					glossy_init(GLOSSY_SLAVE, master_candidate_eui);

					standard_start();
				}

			}
#endif

            // There is nothing else to do in this round; as we are not in sync, we do not want to use the slot for ranging
            return;

		} else if (_lwb_in_sync) {

			// IN SYNC: Figure out what to do for the given slot

			// LWB Slot N-C: Contention slots - We decide when to send in the first
			if (_lwb_counter == lwb_slot_cont_start) {

                if (standard_is_init_active()) {
                    // Stop if still listening for responses
                    standard_init_stop_response_listening();
                }

                // Setup for contention
				dw1000_update_channel(LWB_CHANNEL);
				dw1000_choose_antenna(LWB_ANTENNA);

				if( (_lwb_sched_en && ( (!_lwb_scheduled_init && standard_is_init_enabled()) ||
									    (!_lwb_scheduled_resp && standard_is_resp_enabled())   ) ) ||
					(_signal_pkt.info_type != SIGNAL_UNDEFINED)                                      ) {

					// Verify that there is no other signal we might overwrite when sending our schedule request
					if (_lwb_sched_en && (_signal_pkt.info_type == SIGNAL_UNDEFINED) ) {
						debug_msg("Sending schedule request...\n");

						prepare_schedule_signal();
					} else {
                        debug_msg("Sending signalling packet...\r\n");
                    }

                    // Enable flooding already at the source
                    _cur_glossy_depth          = 0; // seqNum in packet
                    _glossy_currently_flooding = TRUE;

					//dwt_forcetrxoff();

					uint16_t frame_len = sizeof(struct pp_signal_flood);
					dwt_writetxfctrl(frame_len, 0, MSG_TYPE_CONTROL);
                    debug_print_tx(frame_len);

					// Send out a signalling packet during this contention slot
					// Pick a random time offset to avoid colliding with others
#ifdef GLOSSY_ANCHOR_SYNC_TEST
					uint32_t sched_req_time = (uint32_t)(_sched_req_pkt.tag_sched_eui[0] - 0x31) * GLOSSY_FLOOD_TIMESLOT_US;
					uint32_t delay_time = (dwt_readsystimestamphi32() + DW_DELAY_FROM_PKT_LEN(sizeof(struct pp_signal_flood)) + DW_DELAY_FROM_US(sched_req_time)) & 0xFFFFFFFE;
					double turnaround_time = (double)((((uint64_t)(delay_time) << 8) - _last_sync_timestamp) & 0xFFFFFFFFFFUL);// + DW_DELAY_FROM_US(GLOSSY_FLOOD_TIMESLOT_US)*_last_sync_depth;
					turnaround_time /= _clock_offset;
					_sched_req_pkt.turnaround_time = (uint64_t)(turnaround_time);
					dw1000_choose_antenna(LWB_ANTENNA);
#else
					uint32_t sched_req_time = (ranval(&_prng_state) % (uint32_t)(_lwb_num_timeslots_cont * LWB_SLOT_US - (GLOSSY_MAX_DEPTH - 1)*GLOSSY_FLOOD_TIMESLOT_US)) + RANGING_CONTENTION_PADDING_US;
					uint32_t delay_time     = (dwt_readsystimestamphi32() + DW_DELAY_FROM_PKT_LEN(sizeof(struct pp_signal_flood)) + DW_DELAY_FROM_US(sched_req_time)) & 0xFFFFFFFE;
#endif
					_last_delay_time = delay_time;
					dwt_setdelayedtrxtime(delay_time);
					//dwt_setrxaftertxdelay(LWB_SLOT_US);
					dwt_starttx(DWT_START_TX_DELAYED);
					dwt_settxantennadelay(DW1000_ANTENNA_DELAY_TX);
					dwt_writetxdata(sizeof(struct pp_signal_flood), (uint8_t*) &_signal_pkt, 0);

					_signal_pkt.info_type = SIGNAL_UNDEFINED;
				} else {
				    // Listen for signalling messages from others
					dwt_rxenable(0);
				}

			// LWB Slot N-1: Get ready for next glossy flood
			}  else if(_lwb_counter == ( (GLOSSY_UPDATE_INTERVAL_US/LWB_SLOT_US) - 1) ){

				// Make sure we're in RX mode, ready for next glossy sync flood!
				//dwt_setdblrxbuffmode(FALSE);
				glossy_listen_for_next_sync();

			// LWB Slot > N: Invalid counter value
			} else if (_lwb_counter > ( (GLOSSY_UPDATE_INTERVAL_US/LWB_SLOT_US) - 1) ) {
				debug_msg("WARNING: LWB counter overshooting, currently at ");
				debug_msg_int(_lwb_counter);
				debug_msg("\n");
			}
		}

	} else {
		debug_msg("ERROR: Unknown Glossy role!\n");
	}

	// Both Master and Slave can do ranging if they are scheduled

	// LWB Slots 1 - I: Initiator slots
	if( (lwb_slot_init_start <= _lwb_counter					  ) &&
        (                       _lwb_counter < lwb_slot_resp_start)   ) {

		if( _lwb_scheduled_init 															     &&
		   ( ((_lwb_counter - lwb_slot_init_start) / LWB_SLOTS_PER_RANGE) == _lwb_timeslot_init) &&
		   ( ((_lwb_counter - lwb_slot_init_start) % LWB_SLOTS_PER_RANGE) == 0                 )   ) {
			// Our scheduled timeslot!
			standard_init_start_ranging_event();

		} else {

		    // Turn off reception after the start of the round if one is not a RESP; INIT start transmit in their slot
		    if (_lwb_counter == lwb_slot_init_start) {

		        if (_lwb_scheduled_resp) {
                    // Enable responders
                    standard_resp_start();
		        } else {
		            // Turn transceiver off (save energy) - initiators will wake up in their own slot
		            dwt_forcetrxoff();
		        }

            // Restart hybrids after their initiator slot if necessary
		    } else if (_lwb_scheduled_init 															             &&
                       ( ((_lwb_counter - lwb_slot_init_start) / LWB_SLOTS_PER_RANGE) == _lwb_timeslot_init + 1) &&
                       ( ((_lwb_counter - lwb_slot_init_start) % LWB_SLOTS_PER_RANGE) == 0                     )   ) {

		        // Turn off init role
		        standard_set_init_active(FALSE);

#ifdef PROTOCOL_REENABLE_HYBRIDS
                // Reenable hybrid nodes after their ranging slot, so that they can respond to others
                if (standard_is_resp_enabled() && standard_is_init_enabled()) {
                    standard_resp_start();
                }
#endif
		    }
		}

    // LWB Slots (I+1) - (I + R): Responder slots
	} else if( (lwb_slot_resp_start <= _lwb_counter                      ) &&
		       (                       _lwb_counter < lwb_slot_cont_start)   ) {

		if( _lwb_scheduled_resp                                                                      &&
			( (_lwb_counter - lwb_slot_resp_start) == (_lwb_timeslot_resp / LWB_RESPONSES_PER_SLOT) )  ) {
			// Our scheduled timeslot! The timer will trigger the correct message slot inside the LWB slot
			standard_resp_trigger_response(_lwb_timeslot_resp % LWB_RESPONSES_PER_SLOT);

		} else {

			// Turn on reception if you are interested in timestamps; either at the beginning (as an initiator) or again after your slot (as a hybrid)
			if ( (_lwb_counter == lwb_slot_resp_start)                                                        ||
                 (_lwb_scheduled_resp && ( (_lwb_counter - lwb_slot_resp_start) == (_lwb_timeslot_resp + 1) ))  ){

			    // Turn off RESP
                standard_set_resp_active(FALSE);

				if (_lwb_scheduled_init) {

				    // Calculate number of slots we need to listen
				    uint8_t nr_slots = glossy_get_resp_listening_slots_a();

                    // (Re-)Enable initiators to receive the rest of the responses
					standard_init_start_response_listening(nr_slots);
				} else {
                    // Turn transceiver off (save energy)
                    dwt_forcetrxoff();
				}
			}
		}
    // After the official round is over, we can turn off listening for signals
    } else if(_lwb_counter == (lwb_slot_last + (uint32_t)1)) {
        // Turn off all reception
        /*debug_msg(" - End of round; total length ");
        debug_msg_uint(_lwb_num_timeslots_total);
        debug_msg("\n");*/

        dwt_forcetrxoff();
    }

}

void lwb_set_sched_request(bool sched_en){
	_lwb_sched_en = sched_en;
}

bool glossy_process_txcallback(){

	// Signal whether the current Tx packet was actually a Glossy one
	bool is_glossy_callback = FALSE;

	if(_role == GLOSSY_MASTER && _sending_sync) {

	    //debug_msg("INFO: Successfully sent the schedule... resetting timers and starting a new round\n");

		// Sync has sent, set the timer to send the next one at a later time
		timer_reset(_glossy_timer, 0);
		_lwb_counter = 0;
		_sending_sync = FALSE;

		// Reset statistics about previous round
		_lwb_num_signalling_used  = 0;
		_lwb_prev_signalling_type = SIGNAL_UNDEFINED;
		memset(_lwb_prev_signalling_eui, 0, EUI_LEN);

#if (BOARD_V == SQUAREPOINT)
		// Signal normal round by turning on GREEN
		GPIO_WriteBit(STM_LED_RED_PORT,   STM_LED_RED_PIN,   Bit_SET);
		GPIO_WriteBit(STM_LED_BLUE_PORT,  STM_LED_BLUE_PIN,  Bit_SET);
		GPIO_WriteBit(STM_LED_GREEN_PORT, STM_LED_GREEN_PIN, Bit_RESET);
#endif

		is_glossy_callback = TRUE;

	} else if(_role == GLOSSY_SLAVE && _glossy_currently_flooding) {

        _cur_glossy_depth++;

        if (_cur_glossy_depth < GLOSSY_MAX_DEPTH) {

            /*debug_msg("Sending flooding message with depth ");
            debug_msg_uint(_cur_glossy_depth);
            debug_msg("\n");*/

            // We're flooding, keep doing it until the max depth!
            uint32_t delay_time = _last_delay_time + (DW_DELAY_FROM_US(GLOSSY_FLOOD_TIMESLOT_US) & 0xFFFFFFFE);
            delay_time &= 0xFFFFFFFE;
            _last_delay_time = delay_time;

            dwt_forcetrxoff();
            dwt_setdelayedtrxtime(delay_time);
            //dwt_setrxaftertxdelay(LWB_SLOT_US);
            dwt_starttx(DWT_START_TX_DELAYED);
            dwt_settxantennadelay(DW1000_ANTENNA_DELAY_TX);
            dwt_writetodevice( TX_BUFFER_ID, offsetof(struct ieee154_header_broadcast, seqNum), 1, &_cur_glossy_depth);
		} else {
			dwt_rxenable(0);
			_glossy_currently_flooding = FALSE;
		}

		is_glossy_callback = TRUE;

	}

	return is_glossy_callback;
}

void lwb_send_sync(uint32_t delay_time){
	uint16_t frame_len = get_sync_packet_length(&_sync_pkt);
	dwt_writetxfctrl(frame_len, 0, MSG_TYPE_CONTROL);
	debug_print_tx(frame_len);

	_last_delay_time = delay_time;

	dwt_setdelayedtrxtime(delay_time);
	dwt_setrxaftertxdelay(1);

	dwt_starttx(DWT_START_TX_DELAYED);
	dwt_settxantennadelay(DW1000_ANTENNA_DELAY_TX);
	dwt_writetxdata(frame_len, _sync_pkt_buffer, 0);
}

#define CW_CAL_12PF ((3.494350-3.494173)/3.4944*1e6/30)
#define CW_CAL_22PF ((3.494078-3.493998)/3.4944*1e6/30)
#define CW_CAL_33PF ((3.493941-3.493891)/3.4944*1e6/30)
int8_t clock_offset_to_trim_diff(double ppm_offset){
       return (int8_t) (floor(ppm_offset/CW_CAL_12PF + 0.5));
}

void glossy_process_rxcallback(uint64_t dw_timestamp, uint8_t *buf){

	struct pp_sched_flood  *in_glossy_sync   = (struct pp_sched_flood  *) buf;
	struct pp_signal_flood *in_glossy_signal = (struct pp_signal_flood *) buf;

	// Not required anymore, as already handled in dw1000.c for receive timestamps
	//dw_timestamp = dw1000_correct_timestamp(dw_timestamp);

	// Debugging information
    /*debug_msg("Rx -> depth: ");
    debug_msg_uint(in_glossy_sync->header.seqNum);
    debug_msg("; sender: ");
    debug_msg_uint(in_glossy_sync->header.sourceAddr[0]);
    debug_msg("\n");*/

	if(_role == GLOSSY_MASTER) {

		// If this is a schedule request, try to fit the requesting tag into the schedule
		if (in_glossy_signal->message_type == MSG_TYPE_PP_GLOSSY_SIGNAL) {

			// Detect whether its a new signal or we just receive multiple versions of the same flood
			if ( (memcmp(_lwb_prev_signalling_eui, in_glossy_signal->header.sourceAddr, EUI_LEN) != 0) ||
				 (_lwb_prev_signalling_type != in_glossy_signal->message_type                        )   ) {

				// Previously unheard signal
				_lwb_num_signalling_used++;
				_lwb_prev_signalling_type = in_glossy_signal->message_type;
				memcpy(_lwb_prev_signalling_eui, in_glossy_signal->header.sourceAddr, EUI_LEN);

				debug_msg("Received signalling packet: ");

#ifdef GLOSSY_ANCHOR_SYNC_TEST
                uint64_t actual_turnaround = (dw_timestamp - ((uint64_t)(_last_delay_time) << 8)) & 0xFFFFFFFFFFUL;//in_glossy_sched_req->turnaround_time;
                const uint8_t header[] = {0x80, 0x01, 0x80, 0x01};
                uart_write(4, header);

                actual_turnaround = in_glossy_sched_req->turnaround_time - actual_turnaround;

                uart_write(1, &(in_glossy_sched_req->tag_sched_eui[0]));
                uart_write(1, &(in_glossy_sched_req->sync_depth));
                //uart_write(1, &(in_glossy_sched_req->xtal_trim));
                uart_write(sizeof(uint32_t), &actual_turnaround);
                uart_write(sizeof(double), &(in_glossy_sched_req->clock_offset_ppm));

                dwt_forcetrxoff();
                dw1000_update_channel(LWB_CHANNEL);
                dw1000_choose_antenna(LWB_ANTENNA);
                dwt_rxenable(0);
#else
				switch (in_glossy_signal->info_type) {
					case SIGNAL_UNDEFINED: {
						debug_msg("UNDEFINED!\n");
						break;
					}
					case SCHED_REQUEST_INIT: {
						debug_msg("Schedule request for INIT\n");
						schedule_init(in_glossy_signal->device_eui);
						break;
					}
					case SCHED_REQUEST_RESP: {
						debug_msg("Schedule request for RESP\n");
						schedule_resp(in_glossy_signal->device_eui);
						break;
					}
					case SCHED_REQUEST_HYBRID: {
						debug_msg("Schedule request for HYBRID\n");
						schedule_hybrid(in_glossy_signal->device_eui);
						break;
					}
					case DESCHED_REQUEST_INIT: {
						debug_msg("Deschedule request for INIT\n");
						deschedule_init(in_glossy_signal->device_eui);
						break;
					}
					case DESCHED_REQUEST_RESP: {
						debug_msg("Deschedule request for RESP\n");
						deschedule_resp(in_glossy_signal->device_eui);
						break;
					}
					case DESCHED_REQUEST_HYBRID: {
						debug_msg("Deschedule request for HYBRID\n");
						deschedule_hybrid(in_glossy_signal->device_eui);
						break;
					}
					default: {
						debug_msg("UNKNOWN reason ");
						debug_msg_uint(in_glossy_signal->info_type);
						debug_msg("\n");
						break;
					}
				}
#endif
			} else {
			    //debug_msg("INFO: Received same signalling packet multiple times\n");
			}

			// Re-enable rx for other signalling packets
			dwt_rxenable(0);
		}
		else {
		    debug_msg("ERROR: Received unknown LWB packet as Glossy master!\n");
		}

#ifdef GLOSSY_PER_TEST
		_total_syncs_received++;
#endif
		return;
	}
	else if(_role == GLOSSY_SLAVE) {

		if(in_glossy_signal->message_type == MSG_TYPE_PP_GLOSSY_SIGNAL) {

		    debug_msg("Received signalling packet from another node\n");

#ifndef GLOSSY_ANCHOR_SYNC_TEST
			// Increment depth counter
			_cur_glossy_depth = ++in_glossy_signal->header.seqNum;

			if (_cur_glossy_depth < GLOSSY_MAX_DEPTH) {

                /*debug_msg("Sending flooding message with depth ");
                debug_msg_uint(_cur_glossy_depth);
                debug_msg("\n");*/

                _glossy_currently_flooding = TRUE;

                uint16_t frame_len = sizeof(struct pp_signal_flood);
                dwt_writetxfctrl(frame_len, 0, MSG_TYPE_CONTROL);
				debug_print_tx(frame_len);

                // Flood out as soon as possible
                uint32_t delay_time = (dw_timestamp >> 8) + (DW_DELAY_FROM_US(GLOSSY_FLOOD_TIMESLOT_US) & 0xFFFFFFFE);
                delay_time &= 0xFFFFFFFE;
                _last_delay_time = delay_time;
                dwt_forcetrxoff();
                //dwt_setrxaftertxdelay(LWB_SLOT_US);
                dwt_setdelayedtrxtime(delay_time);
                dwt_starttx(DWT_START_TX_DELAYED);
                dwt_settxantennadelay(DW1000_ANTENNA_DELAY_TX);
                dwt_writetxdata(sizeof(struct pp_signal_flood), (uint8_t *) in_glossy_signal, 0);

            } else {
                // We are on the very edge of the network - just listen
                dwt_rxenable(0);
			}
#endif
		}
		else if (in_glossy_sync->message_type == MSG_TYPE_PP_GLOSSY_SYNC) {

		    //debug_msg("Received schedule from Glossy master\n");

		    if (memcmp(_lwb_master_eui, in_glossy_sync->header.sourceAddr, PROTOCOL_EUI_LEN) != 0) {

#ifndef PROTOCOL_FLEXIBLE_MASTER
		    	if (_lwb_master_eui[0] == 0x00) // Uninitialized -> no specific Master set
				{
#endif
					// Found new Glossy master
					memcpy(_lwb_master_eui, in_glossy_sync->header.sourceAddr, EUI_LEN);
					host_interface_notify_master_change(_lwb_master_eui, EUI_LEN);

					debug_msg("Found new Glossy master: ");
					helper_print_EUI(_lwb_master_eui, EUI_LEN);
					debug_msg("\n");
#ifndef PROTOCOL_FLEXIBLE_MASTER
				} else {
					debug_msg("WARNING: Received schedule from incorrect Glossy Master: expected ");
					debug_msg_uint(_lwb_master_eui[0]);
					debug_msg(", but received from ");
					debug_msg_uint(in_glossy_sync->header.sourceAddr[0]);
					debug_msg("!\n");
					return;
				}
#endif
		    }

#if (BOARD_V == SQUAREPOINT)
			// Signal that in sync with Glossy by turning on GREEN
			if (GPIO_ReadOutputDataBit(STM_LED_GREEN_PORT, STM_LED_GREEN_PIN)) {
				GPIO_WriteBit(STM_LED_BLUE_PORT,  STM_LED_BLUE_PIN,  Bit_SET);
				GPIO_WriteBit(STM_LED_GREEN_PORT, STM_LED_GREEN_PIN, Bit_RESET);
			}
#endif

#ifdef PROTOCOL_ENABLE_GLOBAL_TIMESTAMPS
			// Check out if epoch has been updated
			if (_last_sync_epoch < in_glossy_sync->epoch_time) {
				_last_sync_epoch = in_glossy_sync->epoch_time;
			}
#endif

			// Check out whether the node has been scheduled
			int init_slot = check_if_scheduled((uint8_t*) in_glossy_sync->eui_array                                                          , in_glossy_sync->init_schedule_length * (uint8_t)PROTOCOL_EUI_LEN);
			int resp_slot = check_if_scheduled((uint8_t*) in_glossy_sync->eui_array + in_glossy_sync->init_schedule_length * PROTOCOL_EUI_LEN, in_glossy_sync->resp_schedule_length * (uint8_t)PROTOCOL_EUI_LEN);

			if(init_slot >= 0) {
				_lwb_scheduled_init = TRUE;
				_lwb_timeslot_init = (uint8_t)init_slot;
			} else {
                debug_msg("Device is not scheduled as INIT\n");
				_lwb_scheduled_init = FALSE;
			}

			if(resp_slot >= 0) {
				_lwb_scheduled_resp = TRUE;
				_lwb_timeslot_resp  = (uint8_t)resp_slot;
			} else {
                debug_msg("Device is not scheduled as RESP\n");
				_lwb_scheduled_resp = FALSE;
			}

			_lwb_num_timeslots_total = in_glossy_sync->round_length;
			_lwb_num_init            = in_glossy_sync->init_schedule_length;
			_lwb_num_resp            = in_glossy_sync->resp_schedule_length;
			_lwb_num_timeslots_cont  = _lwb_num_timeslots_total - (1 + _lwb_num_init * LWB_SLOTS_PER_RANGE + ceil_fraction(_lwb_num_resp, LWB_RESPONSES_PER_SLOT));

#ifdef PROTOCOL_ENABLE_MASTER_TAKEOVER
			// Store scheduling information if need to attempt network takeover afterwards
			memcpy(_init_sched_euis, (uint8_t*)in_glossy_sync->eui_array                                                          , _lwb_num_init * PROTOCOL_EUI_LEN);
			memcpy(_resp_sched_euis, (uint8_t*)in_glossy_sync->eui_array + in_glossy_sync->init_schedule_length * PROTOCOL_EUI_LEN, _lwb_num_resp * PROTOCOL_EUI_LEN);
#endif

			debug_msg("Scheduled nodes this round: I ");
			debug_msg_uint(_lwb_num_init);
			debug_msg(", R ");
			debug_msg_uint(_lwb_num_resp);
			debug_msg("\r\n");

#ifdef GLOSSY_ANCHOR_SYNC_TEST
			_sched_req_pkt.sync_depth = in_glossy_sync->header.seqNum;
#endif

			if(    (_last_sync_timestamp + ((uint64_t)(DW_DELAY_FROM_US(GLOSSY_UPDATE_INTERVAL_US * 0.5)) << 8) ) < dw_timestamp){
				if((_last_sync_timestamp + ((uint64_t)(DW_DELAY_FROM_US(GLOSSY_UPDATE_INTERVAL_US * 1.5)) << 8) ) > dw_timestamp){
					// If we're between 0.5 to 1.5 times the update interval, we are now able to update our clock and perpetuate the flood!
			
					// Calculate the ppm offset from the last two received sync messages
					double clock_offset_ppm = (((double)(dw_timestamp - 
					                                     ((uint64_t)(DW_DELAY_FROM_US(GLOSSY_FLOOD_TIMESLOT_US) & 0xFFFFFFFE) << 8)*(in_glossy_sync->header.seqNum) - 
					                                     _last_sync_timestamp) / ((uint64_t)(GLOSSY_UPDATE_INTERVAL_DW) << 8)) - 1.0) * 1e6;
#ifdef GLOSSY_ANCHOR_SYNC_TEST
					_sched_req_pkt.clock_offset_ppm = clock_offset_ppm;
#endif
					
					_clock_offset = (clock_offset_ppm/1e6)+1.0;
					_glossy_flood_timeslot_corrected_us = (uint64_t)((double)((uint64_t)(DW_DELAY_FROM_US(GLOSSY_FLOOD_TIMESLOT_US) & 0xFFFFFFFE) << 8)*_clock_offset);

					// Great, we're still sync'd!
					_last_sync_depth = in_glossy_sync->header.seqNum;
					_lwb_in_sync = TRUE;

					// Since we're sync'd, we should make sure to reset our LWB window timer
					_lwb_counter = 0;
					timer_reset(_glossy_timer, ((uint32_t)(in_glossy_sync->header.seqNum))*GLOSSY_FLOOD_TIMESLOT_US);

					// Update DW1000's crystal trim to account for observed PPM offset
					_last_xtal_trim = _xtal_trim;
					int8_t trim_diff = clock_offset_to_trim_diff(clock_offset_ppm);
					_xtal_trim += trim_diff;
					if(_xtal_trim < 1) _xtal_trim = 1;
					else if(_xtal_trim > 31) _xtal_trim = 31;

					dwt_setxtaltrim(_xtal_trim);
#ifdef GLOSSY_ANCHOR_SYNC_TEST
					_sched_req_pkt.xtal_trim = trim_diff;
					// Sync is invalidated if the xtal trim has changed (this won't happen often)
					if(_last_xtal_trim != _xtal_trim)
						_sched_req_pkt.sync_depth = 0xFF;
#endif

					// Perpetuate the flood!
                    _cur_glossy_depth = ++in_glossy_sync->header.seqNum;

                    if (_cur_glossy_depth < GLOSSY_MAX_DEPTH) {

                        /*debug_msg("Sending flooding message with depth ");
                        debug_msg_uint(_cur_glossy_depth);
                        debug_msg("\n");*/

                        _glossy_currently_flooding = TRUE;

                        memcpy(&_sync_pkt       , in_glossy_sync, get_sync_packet_length(in_glossy_sync) - sizeof(struct ieee154_footer)); // Write valid part of the packet (up to the valid part of the array) to the local _sync_pkt
                        memcpy(&_sync_pkt_buffer, in_glossy_sync, get_sync_packet_length(in_glossy_sync));

                        uint32_t delay_time = (dw_timestamp >> 8) + (DW_DELAY_FROM_US(GLOSSY_FLOOD_TIMESLOT_US) & 0xFFFFFFFE);
                        delay_time &= 0xFFFFFFFE;
                        dwt_forcetrxoff();
                        lwb_send_sync(delay_time);

                    } else {
                        // We are on the very edge of the network - just listen
                        dwt_rxenable(0);
                    }

				} else {
					// We lost sync :(
					_lwb_in_sync = FALSE;

                    // Get the number of DW time units for which we did not receive a new schedule
                    /*uint64_t curr_timestamp = dw1000_correct_timestamp((uint64_t)dwt_readsystimestamphi32() << 8); //32 highest bits, bitshifted to compare to the 40bit timestamp
                    uint64_t out_of_sync_dw = (curr_timestamp - _last_sync_timestamp);
                    uint32_t out_of_sync_us = (uint32_t)APP_DEVICETIME_TO_USU64(out_of_sync_dw);

                    debug_msg("WARNING: Lost sync, as last schedule received ");
                    debug_msg_uint(out_of_sync_us);
                    debug_msg(" us ago\n");*/
				}
			} else {
				// We've just received a following packet in the flood
				// This really shouldn't happen, but for now let's ignore it
				//debug_msg("WARNING: Received schedule too quickly!\n");
			}

			_last_sync_timestamp = dw_timestamp - (_glossy_flood_timeslot_corrected_us * in_glossy_sync->header.seqNum);

		} else {
		    debug_msg("ERROR: Received unknown LWB packet as Glossy slave!\n");
		}
	}
}

// Helper functions ----------------------------------------------------------------------------------------------------

void prepare_schedule_signal() {

	if (standard_is_init_enabled() && standard_is_resp_enabled()) {
		_signal_pkt.info_type = SCHED_REQUEST_HYBRID;
		memcpy(_signal_pkt.device_eui, standard_get_EUI(), PROTOCOL_EUI_LEN);
	} else if (standard_is_init_enabled()) {
		_signal_pkt.info_type = SCHED_REQUEST_INIT;
		memcpy(_signal_pkt.device_eui, standard_get_EUI(), PROTOCOL_EUI_LEN);
	} else if (standard_is_resp_enabled()) {
		_signal_pkt.info_type = SCHED_REQUEST_RESP;
		memcpy(_signal_pkt.device_eui, standard_get_EUI(), PROTOCOL_EUI_LEN);
	} else {
		debug_msg("WARNING: Configuration does not allow scheduling!\n");
	}
}

static uint8_t schedule_device(uint8_t * array, uint8_t array_length, uint8_t * eui) {

	uint8_t zero_array[PROTOCOL_EUI_LEN] = { 0 };
	uint8_t candidate_slot = 0xFF;

	for (uint8_t i = 0; i < (array_length / PROTOCOL_EUI_LEN); i++) {

		// Check if already inside
		if (memcmp( (array + i * PROTOCOL_EUI_LEN), eui, PROTOCOL_EUI_LEN) == 0) {
			debug_msg("INFO: Node has already been scheduled\n");
			return 0xFF;
		}

		// Search empty space - once we have found one, we still need to continue looking whether the node has been scheduled somewhere else (holes might exist due to deschedules)
		if (candidate_slot < 0xFF) {
			// We already have found a candidate slot, but verify that the node is not scheduled otherwise
			continue;
		} else if (memcmp( (array + i * PROTOCOL_EUI_LEN), zero_array, PROTOCOL_EUI_LEN) == 0) {
			candidate_slot = i;
		}
	}

	if (candidate_slot < 0xFF) {
		// Found empty space, and node is not already scheduled
		memcpy( (array + candidate_slot * PROTOCOL_EUI_LEN), eui, PROTOCOL_EUI_LEN);

		/*debug_msg("Scheduled EUI ");
        helper_print_EUI(eui, PROTOCOL_EUI_LEN);
        debug_msg("in slot ");
        debug_msg_uint(candidate_slot);*/

		return candidate_slot;

	} else {
		// No more space in array
		debug_msg("WARNING: No more slots available!\n");
		return 0xFF;
	}
}

static uint8_t schedule_init(uint8_t * eui) {

    uint8_t slot = schedule_device( (uint8_t*)_init_sched_euis, PROTOCOL_INIT_SCHED_MAX * PROTOCOL_EUI_LEN, eui);

    if (slot < 0xFF) {
        _sched_timeouts[PROTOCOL_INIT_SCHED_OFFSET + slot] = 0;
        _lwb_num_scheduled_init++;
    }

    return slot;
}

static uint8_t schedule_resp(uint8_t * eui) {

    uint8_t slot = schedule_device( (uint8_t*)_resp_sched_euis, PROTOCOL_RESP_SCHED_MAX * PROTOCOL_EUI_LEN, eui);

    if (slot < 0xFF) {
        _sched_timeouts[PROTOCOL_RESP_SCHED_OFFSET + slot] = 0;
        _lwb_num_scheduled_resp++;
    }

    return slot;
}

static uint8_t schedule_hybrid(uint8_t * eui) {

	uint8_t slot = schedule_device( (uint8_t*)_hybrid_sched_euis, PROTOCOL_HYBRID_SCHED_MAX * PROTOCOL_EUI_LEN, eui);

	if (slot < 0xFF) {
		_sched_timeouts[PROTOCOL_HYBRID_SCHED_OFFSET + slot] = 0;
		_lwb_num_scheduled_hybrid++;
	}

	return slot;
}

static uint8_t deschedule_device(uint8_t * array, uint8_t array_length, uint8_t * eui) {

	for (uint8_t i = 0; i < (array_length / PROTOCOL_EUI_LEN); i++) {

		if (memcmp( (array + i * PROTOCOL_EUI_LEN), eui, PROTOCOL_EUI_LEN) == 0) {
			memset( (array + i * PROTOCOL_EUI_LEN),   0, PROTOCOL_EUI_LEN);

			/*debug_msg("Descheduled EUI ");
			helper_print_EUI(eui, PROTOCOL_EUI_LEN);
			debug_msg("from slot ");
			debug_msg_uint(i);*/

			return i;
		}
	}

	// No more space in array
	debug_msg("WARNING: Could not find EUI in schedule!\n");
	return 0xFF;
}

static uint8_t deschedule_init(uint8_t * eui) {

    uint8_t slot = deschedule_device( (uint8_t*)_init_sched_euis, PROTOCOL_INIT_SCHED_MAX * PROTOCOL_EUI_LEN, eui);

    if (slot < 0xFF) {
        _sched_timeouts[PROTOCOL_INIT_SCHED_OFFSET + slot] = 0;
        _lwb_num_scheduled_init--;
    }

    return slot;
}

static uint8_t deschedule_resp(uint8_t * eui) {

    uint8_t slot = deschedule_device( (uint8_t*)_resp_sched_euis, PROTOCOL_RESP_SCHED_MAX * PROTOCOL_EUI_LEN, eui);

    if (slot < 0xFF) {
        _sched_timeouts[PROTOCOL_RESP_SCHED_OFFSET + slot] = 0;
        _lwb_num_scheduled_resp--;
    }

    return slot;
}

static uint8_t deschedule_hybrid(uint8_t * eui) {

	uint8_t slot = deschedule_device( (uint8_t*)_hybrid_sched_euis, PROTOCOL_HYBRID_SCHED_MAX * PROTOCOL_EUI_LEN, eui);

	if (slot < 0xFF) {
		_sched_timeouts[PROTOCOL_HYBRID_SCHED_OFFSET + slot] = 0;
		_lwb_num_scheduled_hybrid--;
	}

	return slot;
}

static int check_if_scheduled(uint8_t * array, uint8_t array_length) {

	for (uint8_t i = 0; i < (array_length / PROTOCOL_EUI_LEN); i++) {

		if (memcmp( (array + i * PROTOCOL_EUI_LEN), standard_get_EUI(), PROTOCOL_EUI_LEN) == 0) {
			debug_msg("Device is scheduled in slot ");
			debug_msg_uint(i);
			debug_msg("\n");
			return i;
		}
	}

	//debug_msg("Device is not scheduled\n");
	return -1;
}

// The master sends its epoch time regularly to synchronize the network
void glossy_set_epoch_time(uint32_t epoch) {

#ifdef PROTOCOL_ENABLE_GLOBAL_TIMESTAMPS
	// Use epoch time provided by carrier and distribute it throughout the network
	_last_sync_epoch = epoch;

	// Set inside the packet
	_sync_pkt.epoch_time = _last_sync_epoch;
#else
	debug_msg("ERROR: Global timestamps not enabled!\n");
#endif
}

uint32_t glossy_get_epoch_time() {
	return _last_sync_epoch;
}

// This function is called upon the successful reception of a packet; this means that other nodes are still scheduled and transmitting.
// Therefore, we simply missed a schedule and the master is still alive
void glossy_reset_counter_offset() {

    if (_role == GLOSSY_MASTER) {
        // The master never waits for itself, so we do not reset
        return;

    } else if ( _lwb_counter <= (2 * (GLOSSY_UPDATE_INTERVAL_US / LWB_SLOT_US)) ) {
        // Normal behaviour, we dont have to do anything; maybe we just missed a single schedule, the control loop will catch this
        return;

    } else if ( _lwb_counter >  (GLOSSY_MASTER_TAKEOVER_PERIOD - 50)) {
        // Soon, another Master will take over; as it will then start transmitting, we give up on waiting for the old master, as due to time desync, this could already be a part of the new schedule
        return;

    } else {

        // Prevent triggering a takeover or timeout by reducing the counter offset to a single round period, as some nodes are still scheduled
        // This means that we are still in recovery mode but give the master more time to deliver a schedule successfully
        _lwb_counter = (uint16_t) (GLOSSY_UPDATE_INTERVAL_US / LWB_SLOT_US);
    }
}

static uint8_t glossy_get_resp_listening_slots_a() {

    // We set the timer if:
    // i)  We are either a) solely an initiator or if b) we already finished our responder responsibilities and only act as an initiator
    // ii) We are only partly using a responder slot, i.e. there are less than LWB_RESPONSES_PER_SLOT in the last slot

    // Here, we only test for case a)
    if ( (!_lwb_scheduled_resp) && ( (_lwb_num_resp % LWB_RESPONSES_PER_SLOT) != 0 ) ) {
        // Set timer to expire nr_slots timeslots from now as soon as the last responder sent its message
        uint32_t lwb_slot_resp_start = 1 + _lwb_num_init * LWB_SLOTS_PER_RANGE;

        return (uint8_t)(_lwb_num_resp - (_lwb_counter - lwb_slot_resp_start) * LWB_RESPONSES_PER_SLOT);
    } else {
        return 0;
    }
}

uint8_t glossy_get_resp_listening_slots_b() {

    // We set the timer if:
    // i)  We are either a) solely an initiator or if b) we already finished our responder responsibilities and only act as an initiator
    // ii) We are only partly using a responder slot, i.e. there are less than LWB_RESPONSES_PER_SLOT in the last slot

    // Here, we only handle case b)
    if ( (_lwb_num_resp % LWB_RESPONSES_PER_SLOT) != 0) {
        // Set timer to expire nr_slots timeslots from now as soon as the last responder sent its message
        return (uint8_t)(_lwb_num_resp - (_lwb_timeslot_resp + 1));
    } else {
        return 0;
    }
}

static void lwb_increment_sched_timeout(){

    // Timeout initiators
    for(int ii=0; ii < (PROTOCOL_INIT_SCHED_MAX); ii++){

        // Check if slot is actually used
        if(_init_sched_euis[ii][0] > 0){
            _sched_timeouts[PROTOCOL_INIT_SCHED_OFFSET + ii]++;

            if(_sched_timeouts[PROTOCOL_INIT_SCHED_OFFSET + ii] == TAG_SCHED_TIMEOUT) {
                memset(_init_sched_euis[ii], 0, PROTOCOL_EUI_LEN);
                _lwb_num_scheduled_init--;

                // Reset
                _sched_timeouts[PROTOCOL_INIT_SCHED_OFFSET + ii] = 0;
            }
        } else {
            _sched_timeouts[PROTOCOL_INIT_SCHED_OFFSET + ii] = 0;
        }
    }

    // Timeout responders
    for(int ii=0; ii < (PROTOCOL_RESP_SCHED_MAX); ii++){

        // Check if slot is actually used
        if(_resp_sched_euis[ii][0] > 0){
            _sched_timeouts[PROTOCOL_RESP_SCHED_OFFSET + ii]++;

            if(_sched_timeouts[PROTOCOL_RESP_SCHED_OFFSET + ii] == TAG_SCHED_TIMEOUT) {
                memset(_resp_sched_euis[ii], 0, PROTOCOL_EUI_LEN);
                _lwb_num_scheduled_resp--;

                // Reset
                _sched_timeouts[PROTOCOL_RESP_SCHED_OFFSET + ii] = 0;
            }
        } else {
            _sched_timeouts[PROTOCOL_RESP_SCHED_OFFSET + ii] = 0;
        }
    }

	// Timeout hybrids
	for(int ii=0; ii < (PROTOCOL_HYBRID_SCHED_MAX); ii++){

		// Check if slot is actually used
		if(_hybrid_sched_euis[ii][0] > 0){
			_sched_timeouts[PROTOCOL_HYBRID_SCHED_OFFSET + ii]++;

			if(_sched_timeouts[PROTOCOL_HYBRID_SCHED_OFFSET + ii] == TAG_SCHED_TIMEOUT) {
				memset(_hybrid_sched_euis[ii], 0, PROTOCOL_EUI_LEN);
				_lwb_num_scheduled_hybrid--;

				// Reset
				_sched_timeouts[PROTOCOL_HYBRID_SCHED_OFFSET + ii] = 0;
			}
		} else {
			_sched_timeouts[PROTOCOL_HYBRID_SCHED_OFFSET + ii] = 0;
		}
	}

    // Glossy master verifies that he doesnt kick himself out of the network
    if (standard_is_init_enabled()) {
        if (memcmp(_init_sched_euis[0], standard_get_EUI(), PROTOCOL_EUI_LEN) == 0) {
            _sched_timeouts[PROTOCOL_INIT_SCHED_OFFSET] = 0;
        } else {
            debug_msg("ERROR: Glossy Master did not schedule itself for INIT!\n");
        }
    }

    if (standard_is_resp_enabled()) {
        if (memcmp(_resp_sched_euis[0], standard_get_EUI(), PROTOCOL_EUI_LEN) == 0) {
            _sched_timeouts[PROTOCOL_RESP_SCHED_OFFSET] = 0;
        } else {
            debug_msg("ERROR: Glossy Master did not schedule itself for RESP!\n");
        }
    }
}

static void lwb_adjust_contention_period() {

    // Adjustments of slot length depending on usage U:
    uint8_t prev_contention_length = (uint8_t)_lwb_num_timeslots_cont;

    // 50 % < U <= 100 % : 2x    (maximally until all slots are used)
    // 25 % < U <=  50 % : 1x    (remains)
    //  0 % < U <=  25 % : 1/2 x (minimally default)
    //        U  =   0 % :       (set to default)

    if        (_lwb_num_signalling_used > (_lwb_num_timeslots_cont / 2) ) {
        _lwb_num_timeslots_cont = _lwb_num_timeslots_cont * 2;

        // Maximally set it so that all slots are used
        uint8_t lwb_round_length_static = (uint8_t)(1 /*Sync*/ + _lwb_num_scheduled_init * LWB_SLOTS_PER_RANGE + ceil_fraction(_lwb_num_scheduled_resp, LWB_RESPONSES_PER_SLOT) + 1 /*Preparation for next round*/);
        while ( (lwb_round_length_static + _lwb_num_timeslots_cont) > (GLOSSY_UPDATE_INTERVAL_US / LWB_SLOT_US ) ) {
            _lwb_num_timeslots_cont--;
        }
    } else if (_lwb_num_signalling_used > (_lwb_num_timeslots_cont / 4) ) {
        // Nothing to be done
    } else if (_lwb_num_signalling_used > 0) {
        _lwb_num_timeslots_cont = _lwb_num_timeslots_cont / 2;

        // Minimally use default
        if (_lwb_num_timeslots_cont < PROTOCOL_STANDARD_CONT_LENGTH) {
            _lwb_num_timeslots_cont = PROTOCOL_STANDARD_CONT_LENGTH;
        }
    } else if (_lwb_num_signalling_used == 0) {
        _lwb_num_timeslots_cont = PROTOCOL_STANDARD_CONT_LENGTH;
    }

    if (prev_contention_length != _lwb_num_timeslots_cont) {
        debug_msg("INFO: Adjusted contention period from ");
        debug_msg_uint(prev_contention_length);
        debug_msg(" to ");
        debug_msg_uint(_lwb_num_timeslots_cont);
        debug_msg("\n");
    }
}

static uint8_t get_sync_packet_length(struct pp_sched_flood * packet) {
	uint8_t packet_size  = sizeof(struct ieee154_header_broadcast) + MSG_PP_SCHED_FLOOD_PAYLOAD_DEFAULT_LENGTH + sizeof(struct ieee154_footer);
			packet_size += (packet->init_schedule_length + packet->resp_schedule_length) * PROTOCOL_EUI_LEN;

	/*debug_msg("Schedule packet size: ");
	debug_msg_uint(packet_size);
	debug_msg("\n");*/

	return packet_size;
}

#define MAX_ELEMENT_LENGTH	sizeof(uint64_t)
void compress_array(uint8_t * array, uint8_t array_length, uint8_t element_length) {

	// Compare element of insufficient size
	if (element_length > MAX_ELEMENT_LENGTH) {
		debug_msg("ERROR: Element too long for this function!\n");
		return;
	}

	uint8_t zero_array[MAX_ELEMENT_LENGTH] = { 0 };

	uint8_t i_full  = 0;
	uint8_t i_empty = (array_length / element_length) - (uint8_t)1;
	for (i_full = 0; i_full < i_empty; i_full++) {

		// Find first non-full element from the back
		for (; i_full < i_empty; i_empty--) {

			if (memcmp( (array + i_empty * element_length), zero_array, element_length) != 0) {
				// Found non-zero element
				break;
			}
		}

		if (i_empty == i_full) {
			// Nothing to do anymore
			return;
		}

		if (memcmp( (array + i_full  * element_length), zero_array, element_length) == 0) {
			// Element is not used
			memcpy( (array + i_full  * element_length), (array + i_empty * element_length), element_length);
			memset( (array + i_empty * element_length), 0, element_length);
		}
	}
}

static void write_data_to_sync() {

    /// INFO: We use _sync_pkt primarily as a tool to shape the packet itself and read data from it; the sent operation is done over the _sync_pkt_buffer to eliminate unused space

    // Clean-up array in case deletions occurred
    compress_array( (uint8_t*)_init_sched_euis,   sizeof(_init_sched_euis),   PROTOCOL_EUI_LEN);
    compress_array( (uint8_t*)_resp_sched_euis,   sizeof(_resp_sched_euis),   PROTOCOL_EUI_LEN);
	compress_array( (uint8_t*)_hybrid_sched_euis, sizeof(_hybrid_sched_euis), PROTOCOL_EUI_LEN);

    // Set correct values
    _sync_pkt.init_schedule_length = (uint8_t)_lwb_num_scheduled_init + (uint8_t)_lwb_num_scheduled_hybrid;
    _sync_pkt.resp_schedule_length = (uint8_t)_lwb_num_scheduled_resp + (uint8_t)_lwb_num_scheduled_hybrid;
    _sync_pkt.round_length         = (uint8_t)( 1 /*Sync itself*/ + _sync_pkt.init_schedule_length * LWB_SLOTS_PER_RANGE + ceil_fraction(_sync_pkt.resp_schedule_length, LWB_RESPONSES_PER_SLOT) + _lwb_num_timeslots_cont);

    // For Glossy Master, as it does not read the schedule - note that due to signalling, _lwb_num_scheduled_X and _lwb_num_X might NOT be identital during the course of a round
    _lwb_num_init            = _sync_pkt.init_schedule_length;
    _lwb_num_resp            = _sync_pkt.resp_schedule_length;
    _lwb_num_timeslots_total = _sync_pkt.round_length;

    // Set array; as this array is sparse, we use the sync_pkt_buffer to send the data afterwards
    memset(_sync_pkt.eui_array, 0, sizeof(_sync_pkt.eui_array));

    // Set all the initiators - be advised that the combined number of pure initiators and hybrids must not exceed PROTOCOL_INIT_SCHED_MAX
    memcpy(_sync_pkt.eui_array + (PROTOCOL_INIT_SCHED_OFFSET + 0                      ) * PROTOCOL_EUI_LEN, _init_sched_euis,   _lwb_num_scheduled_init   * PROTOCOL_EUI_LEN);
	memcpy(_sync_pkt.eui_array + (PROTOCOL_INIT_SCHED_OFFSET + _lwb_num_scheduled_init) * PROTOCOL_EUI_LEN, _hybrid_sched_euis, _lwb_num_scheduled_hybrid * PROTOCOL_EUI_LEN);

    // Set all the responders
    memcpy(_sync_pkt.eui_array + PROTOCOL_RESP_SCHED_OFFSET * PROTOCOL_EUI_LEN, _resp_sched_euis, _lwb_num_scheduled_resp * PROTOCOL_EUI_LEN);

    // Ranging pyramid - schedule hybrids in reverse order for responses
    for (uint8_t i = 0; i < _lwb_num_scheduled_hybrid; i++) {
    	memcpy(_sync_pkt.eui_array + (PROTOCOL_RESP_SCHED_OFFSET + _lwb_num_scheduled_resp + i) * PROTOCOL_EUI_LEN, _hybrid_sched_euis + (_lwb_num_scheduled_hybrid - i - 1) * PROTOCOL_EUI_LEN, PROTOCOL_EUI_LEN);
    }

    // All INIT, RESP and HBRIDs are now correctly stored and the packet is ready to be sent
    /*debug_msg("INFO: Sending schedule with ");
    debug_msg_uint(_sync_pkt.init_schedule_length);
    debug_msg(" INITs, ");
    debug_msg_uint(_sync_pkt.resp_schedule_length);
    debug_msg(" RESPs\n");*/
}

static void write_sync_to_packet_buffer() {

	// Clear buffer
	memset(_sync_pkt_buffer, 0, sizeof(_sync_pkt_buffer));

	// Fill buffer:
	uint8_t offset = 0;

	// Header + Constant part
	uint8_t header_length = sizeof(struct ieee154_header_broadcast) + MSG_PP_SCHED_FLOOD_PAYLOAD_DEFAULT_LENGTH;

	memcpy(_sync_pkt_buffer + offset, &_sync_pkt, header_length);
	offset += header_length;

	// Schedules
	uint8_t init_length = _sync_pkt.init_schedule_length * (uint8_t)PROTOCOL_EUI_LEN;
	uint8_t resp_length = _sync_pkt.resp_schedule_length * (uint8_t)PROTOCOL_EUI_LEN;

	memcpy(_sync_pkt_buffer + offset, _sync_pkt.eui_array + PROTOCOL_INIT_SCHED_OFFSET * PROTOCOL_EUI_LEN, init_length);
	offset += init_length;
	memcpy(_sync_pkt_buffer + offset, _sync_pkt.eui_array + PROTOCOL_RESP_SCHED_OFFSET * PROTOCOL_EUI_LEN, resp_length);
	offset += resp_length;

	// Footer
	memcpy(_sync_pkt_buffer + offset, &_sync_pkt.footer, sizeof(struct ieee154_footer));
}

// Find the highest scheduled EUI in the network and propose it as new master for network takeover
static uint8_t get_master_candidate() {

	uint8_t candidate_eui = 0;

	// Search through INITs
	for (uint8_t i = 0; i < _lwb_num_init; i++) {

		if (_init_sched_euis[i][0] > candidate_eui) {
			candidate_eui = _init_sched_euis[i][0];
		}
	}

	// Search through RESPs
	for (uint8_t i = 0; i < _lwb_num_resp; i++) {

		if (_resp_sched_euis[i][0] > candidate_eui) {
			candidate_eui = _resp_sched_euis[i][0];
		}
	}

	if (candidate_eui > 0) {
		debug_msg("Found candidate EUI ");
		debug_msg_uint(candidate_eui);
		debug_msg("\n");
	} else {
		debug_msg("ERROR: Could not find valid candidate EUI!\n");
	}

	return candidate_eui;
}

// Copy over information into correct buffer space so we can restore it later-on
static void save_schedule_information(uint8_t* buffer) {

	_lwb_num_scheduled_init   = 0;
	_lwb_num_scheduled_resp   = 0;
	_lwb_num_scheduled_hybrid = 0;

	// Find all INITs and differentiate whether they are also hybrids
	for (uint8_t i = 0; i < _lwb_num_init; i++) {

		// Figure out if INIT or HYBRID
		bool is_hybrid = FALSE;

		for (uint8_t j = 0; i < _lwb_num_resp; j++) {

			if (_init_sched_euis[i][0] == _resp_sched_euis[j][0]) {
				is_hybrid = TRUE;
			}
		}

		if (is_hybrid) {
			*(buffer + PROTOCOL_HYBRID_SCHED_OFFSET + _lwb_num_scheduled_hybrid * PROTOCOL_EUI_LEN) = _init_sched_euis[i][0];
			_lwb_num_scheduled_hybrid++;
		} else {
			*(buffer + PROTOCOL_INIT_SCHED_OFFSET   + _lwb_num_scheduled_init   * PROTOCOL_EUI_LEN) = _init_sched_euis[i][0];
			_lwb_num_scheduled_init++;
		}
	}

	// Find all RESPs and differentiate whether they are also hybrids
	for (uint8_t i = 0; i < _lwb_num_resp; i++) {

		// Figure out if RESP or HYBRID
		bool is_hybrid = FALSE;

		for (uint8_t j = 0; i < _lwb_num_init; j++) {

			if (_resp_sched_euis[i][0] == _init_sched_euis[j][0]) {
				is_hybrid = TRUE;
			}
		}

		if (is_hybrid) {
			// Already added before
		} else {
			*(buffer + PROTOCOL_RESP_SCHED_OFFSET + _lwb_num_scheduled_resp * PROTOCOL_EUI_LEN) = _resp_sched_euis[i][0];
			_lwb_num_scheduled_resp++;
		}
	}
}

// Save information from buffer back to the app memory space
static void restore_schedule_information(uint8_t* buffer) {

	_lwb_num_scheduled_init   = 0;
	_lwb_num_scheduled_resp   = 0;
	_lwb_num_scheduled_hybrid = 0;

	// Restore INITs
	for (uint8_t i = 0; i < PROTOCOL_INIT_SCHED_MAX; i++) {

		if ( *(buffer + PROTOCOL_INIT_SCHED_OFFSET + i * PROTOCOL_EUI_LEN) > 0) {
			// Stored EUI
			memcpy(_init_sched_euis, buffer + PROTOCOL_INIT_SCHED_OFFSET + i * PROTOCOL_EUI_LEN, PROTOCOL_EUI_LEN);
			_lwb_num_scheduled_init++;
		} else {
			// End of stored INITs
			i = PROTOCOL_INIT_SCHED_MAX;
		}
	}

	// Restore RESPs
	for (uint8_t i = 0; i < PROTOCOL_RESP_SCHED_MAX; i++) {

		if ( *(buffer + PROTOCOL_RESP_SCHED_OFFSET + i * PROTOCOL_EUI_LEN) > 0) {
			// Stored EUI
			memcpy(_resp_sched_euis, buffer + PROTOCOL_RESP_SCHED_OFFSET + i * PROTOCOL_EUI_LEN, PROTOCOL_EUI_LEN);
			_lwb_num_scheduled_resp++;
		} else {
			// End of stored RESPs
			i = PROTOCOL_RESP_SCHED_MAX;
		}
	}

	// Restore HYBRIDs
	for (uint8_t i = 0; i < PROTOCOL_HYBRID_SCHED_MAX; i++) {

		if ( *(buffer + PROTOCOL_HYBRID_SCHED_OFFSET + i * PROTOCOL_EUI_LEN) > 0) {
			// Stored EUI
			memcpy(_hybrid_sched_euis, buffer + PROTOCOL_HYBRID_SCHED_OFFSET + i * PROTOCOL_EUI_LEN, PROTOCOL_EUI_LEN);
			_lwb_num_scheduled_hybrid++;
		} else {
			// End of stored HYBRIDs
			i = PROTOCOL_HYBRID_SCHED_MAX;
		}
	}
}

static uint8_t ceil_fraction(uint32_t nominator, uint32_t denominator) {

	if ( (nominator % denominator) > 0) {
		return (uint8_t)( (nominator / denominator) + 1);
	} else {
		return (uint8_t)(  nominator / denominator     );
	}
}
