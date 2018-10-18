
#include <string.h>
#include <stddef.h>
#include <math.h>

#include "stm32f0xx_gpio.h"

#include "dw1000.h"
#include "deca_regs.h"

#include "board.h"
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
static uint64_t _last_overall_timestamp;
static uint64_t _time_overflow;
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
static bool     _lwb_sched_en;
static bool     _lwb_scheduled_init;
static bool     _lwb_scheduled_resp;
static uint32_t _lwb_num_timeslots_total;
static uint32_t _lwb_num_timeslots_init;
static uint32_t _lwb_num_timeslots_resp;
static uint32_t _lwb_num_timeslots_cont;
static uint32_t _lwb_timeslot_init;
static uint32_t _lwb_timeslot_resp;
static double _clock_offset;

static struct pp_sched_flood  _sync_pkt;
static        uint8_t         _sync_pkt_buffer[sizeof(struct pp_sched_flood)]; // We will copy the bytes into this buffer before sending the packet

static struct pp_signal_flood _signal_pkt;

static uint8_t _init_sched_euis [PROTOCOL_INIT_SCHED_MAX][PROTOCOL_EUI_LEN];
static uint8_t _resp_sched_euis [PROTOCOL_RESP_SCHED_MAX][PROTOCOL_EUI_LEN];
static uint8_t _sched_timeouts  [PROTOCOL_INIT_SCHED_MAX + PROTOCOL_RESP_SCHED_MAX];

#ifdef GLOSSY_PER_TEST
static uint32_t _total_syncs_sent;
static uint32_t _total_syncs_received;
#endif

// STATIC FUNCTIONS ----------------------------------------------------------------------------------------------------

static void    prepare_schedule_signal();
static uint8_t   schedule_init(uint8_t * eui);
static uint8_t   schedule_resp(uint8_t * eui);
static uint8_t deschedule_init(uint8_t * eui);
static uint8_t deschedule_resp(uint8_t * eui);
static int 	   check_if_scheduled(uint8_t * array, uint8_t array_length);

static void    increment_sched_timeout();

static void    write_data_to_sync();
static void	   write_sync_to_packet_buffer();
static uint8_t get_sync_packet_length(struct pp_sched_flood * packet);
static void    lwb_send_sync(uint32_t delay_time);

// ---------------------------------------------------------------------------------------------------------------------


void glossy_init(glossy_role_e role){

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
	_last_sync_depth		= 0;
	_last_sync_timestamp    = 0;
	_last_overall_timestamp = 0;
	_time_overflow			= 0;
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
	_lwb_num_timeslots_total   = 0;
	_lwb_num_timeslots_init    = 0;
	_lwb_num_timeslots_resp	   = 0;
	_lwb_num_timeslots_cont    = PROTOCOL_STANDARD_CONT_LENGTH;
	_lwb_timeslot_init		   = 0;
	_lwb_timeslot_resp		   = 0;
	_clock_offset			   = 0;

	memset(_init_sched_euis,    0, sizeof(_init_sched_euis));
    memset(_resp_sched_euis,    0, sizeof(_resp_sched_euis));
    memset(_sched_timeouts,     0, sizeof(_sched_timeouts));

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
	}

	// The glossy timer acts to synchronize everyone to a common timebase
	_glossy_timer = timer_init();
}

void glossy_start() {

    // Enable the schedule request
    lwb_set_sched_request(TRUE);

    // Kick-off the Glossy timer
    timer_start(_glossy_timer, LWB_SLOT_US, glossy_sync_task);
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

void glossy_enable_reception() {

	// Start receiving
	dwt_forcetrxoff();
	dw1000_update_channel(LWB_CHANNEL);
	dw1000_choose_antenna(LWB_ANTENNA);
	dwt_rxenable(0);
}

void glossy_sync_task(){
	_lwb_counter++;

    /*debug_msg("GLOSSY is in LWB slot ");
    debug_msg_int(_lwb_counter);
    debug_msg("\n");*/

    // The different round stages:
	uint8_t lwb_slot_sync 	    = 0;
	uint8_t lwb_slot_init_start = 1;
	uint8_t lwb_slot_resp_start = (uint8_t)( lwb_slot_init_start + _lwb_num_timeslots_init * LWB_SLOTS_PER_RANGE);
	uint8_t lwb_slot_cont_start = (uint8_t)( lwb_slot_resp_start + _lwb_num_timeslots_resp );
	uint8_t lwb_slot_last		= (uint8_t)( _lwb_num_timeslots_total - 1 );

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
		} else if(_lwb_counter == ( (GLOSSY_UPDATE_INTERVAL_US/LWB_SLOT_US) - 1) ){

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

			increment_sched_timeout();

			// Copy information to sync packet
			write_data_to_sync();

			// Copy information into buffer
			write_sync_to_packet_buffer();

			_last_time_sent += GLOSSY_UPDATE_INTERVAL_DW;
			lwb_send_sync(_last_time_sent);
			_sending_sync = TRUE;

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
		if( (!_lwb_in_sync || (_lwb_counter > (GLOSSY_UPDATE_INTERVAL_US/LWB_SLOT_US)) ) && ((_lwb_counter % 5) == 0) ) {

			glossy_enable_reception();

			debug_msg("Not in sync with Glossy master (yet)\r\n");

#if (BOARD_V == SQUAREPOINT)
			// Signal normal operation by turning on BLUE
			if (GPIO_ReadOutputDataBit(STM_LED_BLUE_PORT, STM_LED_BLUE_PIN)) {
				GPIO_WriteBit(STM_LED_GREEN_PORT, STM_LED_GREEN_PIN, Bit_SET);
				GPIO_WriteBit(STM_LED_BLUE_PORT,  STM_LED_BLUE_PIN,  Bit_RESET);
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

				    standard_set_init_active(TRUE);
				    standard_set_resp_active(FALSE);

					dwt_forcetrxoff();

					uint16_t frame_len = sizeof(struct pp_signal_flood);
					dwt_writetxfctrl(frame_len, 0, MSG_TYPE_CONTROL);

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
					// FIXME: Reduce padding for contention slots
					uint8_t  cont_length    = lwb_slot_last - lwb_slot_cont_start + 1;
					uint32_t sched_req_time = (ranval(&_prng_state) % (uint32_t)(cont_length * LWB_SLOT_US - 2*GLOSSY_FLOOD_TIMESLOT_US)) + 2*GLOSSY_FLOOD_TIMESLOT_US;
					uint32_t delay_time     = (dwt_readsystimestamphi32() + DW_DELAY_FROM_PKT_LEN(sizeof(struct pp_signal_flood)) + DW_DELAY_FROM_US(sched_req_time)) & 0xFFFFFFFE;
#endif
					_last_delay_time = delay_time;
					dwt_setdelayedtrxtime(delay_time);
					dwt_setrxaftertxdelay(LWB_SLOT_US);
					dwt_starttx(DWT_START_TX_DELAYED | DWT_RESPONSE_EXPECTED);
					dwt_settxantennadelay(DW1000_ANTENNA_DELAY_TX);
					dwt_writetxdata(sizeof(struct pp_signal_flood), (uint8_t*) &_signal_pkt, 0);

					_signal_pkt.info_type = SIGNAL_UNDEFINED;
				} else {
					dwt_rxenable(0);
				}

			// LWB Slot N-1: Get ready for next glossy flood
			}  else if(_lwb_counter == ( (GLOSSY_UPDATE_INTERVAL_US/LWB_SLOT_US) - 1) ){

				// Make sure we're in RX mode, ready for next glossy sync flood!
				//dwt_setdblrxbuffmode(FALSE);
				glossy_enable_reception();

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
                    standard_set_resp_active(TRUE);
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

		if( _lwb_scheduled_resp 										  &&
			( (_lwb_counter - lwb_slot_resp_start) == _lwb_timeslot_resp)   ) {
			// Our scheduled timeslot!
			standard_resp_send_response();

		} else {

			// Turn on reception if you are interested in timestamps; either at the beginning (as an initiator) or again after your slot (as a hybrid)
			if ( (_lwb_counter == lwb_slot_resp_start)                                                        ||
                 (_lwb_scheduled_resp && ( (_lwb_counter - lwb_slot_resp_start) == (_lwb_timeslot_resp + 1) ))  ){

			    // Turn off RESP
                standard_set_resp_active(FALSE);

				if (_lwb_scheduled_init) {
					// (Re-)Enable initiators to receive the rest of the responses
					standard_init_start_response_listening();
				} else {
                    // Turn transceiver off (save energy)
                    dwt_forcetrxoff();
				}
			}
		}
    // After the official round is over, we can turn off listening for signals
    } else if(_lwb_counter == (lwb_slot_last + 1)) {
        // Turn off all reception
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

		// Sync has sent, set the timer to send the next one at a later time
		timer_reset(_glossy_timer, 0);
		_lwb_counter = 0;
		_sending_sync = FALSE;

#if (BOARD_V == SQUAREPOINT)
		// Signal normal round by turning on GREEN
		GPIO_WriteBit(STM_LED_RED_PORT,   STM_LED_RED_PIN,   Bit_SET);
		GPIO_WriteBit(STM_LED_BLUE_PORT,  STM_LED_BLUE_PIN,  Bit_SET);
		GPIO_WriteBit(STM_LED_GREEN_PORT, STM_LED_GREEN_PIN, Bit_RESET);
#endif

		is_glossy_callback = TRUE;

	} else if(_role == GLOSSY_SLAVE && _glossy_currently_flooding) {

	    //debug_msg("Sending flooding message...\n");

		// We're flooding, keep doing it until the max depth!
		uint32_t delay_time = _last_delay_time + (DW_DELAY_FROM_US(GLOSSY_FLOOD_TIMESLOT_US) & 0xFFFFFFFE);
		delay_time &= 0xFFFFFFFE;
		_last_delay_time = delay_time;

		_cur_glossy_depth++;
		if (_cur_glossy_depth < GLOSSY_MAX_DEPTH){
			dwt_forcetrxoff();
			dwt_setrxaftertxdelay(LWB_SLOT_US);
			dwt_setdelayedtrxtime(delay_time);
			dwt_starttx(DWT_START_TX_DELAYED | DWT_RESPONSE_EXPECTED);
			dwt_settxantennadelay(DW1000_ANTENNA_DELAY_TX);
			dwt_writetodevice( TX_BUFFER_ID, offsetof(struct ieee154_header_broadcast, seqNum), 1, &_cur_glossy_depth) ;
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

void glossy_sync_process(uint64_t dw_timestamp, uint8_t *buf){

	struct pp_sched_flood  *in_glossy_sync   = (struct pp_sched_flood  *) buf;
	struct pp_signal_flood *in_glossy_signal = (struct pp_signal_flood *) buf;

	// Due to frequent overflow in the Decawave system time counter, we must keep a running total of the number of times it's overflown
	if(dw_timestamp < _last_overall_timestamp) {
		_time_overflow += 0x10000000000ULL;
	}
	_last_overall_timestamp = dw_timestamp;
	dw_timestamp += _time_overflow;

	if(_role == GLOSSY_MASTER) {

		// If this is a schedule request, try to fit the requesting tag into the schedule
		if(in_glossy_sync->message_type == MSG_TYPE_PP_GLOSSY_SIGNAL){

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
					schedule_init(in_glossy_signal->device_eui);
					schedule_resp(in_glossy_signal->device_eui);
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
					deschedule_init(in_glossy_signal->device_eui);
					deschedule_resp(in_glossy_signal->device_eui);
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

		if(in_glossy_sync->message_type == MSG_TYPE_PP_GLOSSY_SIGNAL) {

		    debug_msg("Received signalling packet from another node\n");

#ifndef GLOSSY_ANCHOR_SYNC_TEST
			// Increment depth counter
			_cur_glossy_depth = ++in_glossy_signal->header.seqNum;
			_glossy_currently_flooding = TRUE;

			uint16_t frame_len = sizeof(struct pp_signal_flood);
			dwt_writetxfctrl(frame_len, 0, MSG_TYPE_CONTROL);

			// Flood out as soon as possible
			uint32_t delay_time = (dw_timestamp >> 8) + (DW_DELAY_FROM_US(GLOSSY_FLOOD_TIMESLOT_US) & 0xFFFFFFFE);
			delay_time &= 0xFFFFFFFE;
			_last_delay_time = delay_time;
			dwt_forcetrxoff();
			dwt_setrxaftertxdelay(LWB_SLOT_US);
			dwt_setdelayedtrxtime(delay_time);
			dwt_starttx(DWT_START_TX_DELAYED | DWT_RESPONSE_EXPECTED);
			dwt_settxantennadelay(DW1000_ANTENNA_DELAY_TX);
			dwt_writetxdata(sizeof(struct pp_signal_flood), (uint8_t*) in_glossy_signal, 0);
#endif
		}
		else if (in_glossy_sync->message_type == MSG_TYPE_PP_GLOSSY_SYNC) {

		    //debug_msg("Received schedule from Glossy master\n");

#if (BOARD_V == SQUAREPOINT)
			// Signal that in sync with Glossy by turning on GREEN
			if (GPIO_ReadOutputDataBit(STM_LED_GREEN_PORT, STM_LED_GREEN_PIN)) {
				GPIO_WriteBit(STM_LED_BLUE_PORT,  STM_LED_BLUE_PIN,  Bit_SET);
				GPIO_WriteBit(STM_LED_GREEN_PORT, STM_LED_GREEN_PIN, Bit_RESET);
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
			_lwb_num_timeslots_init  = in_glossy_sync->init_schedule_length;
			_lwb_num_timeslots_resp  = in_glossy_sync->resp_schedule_length;

			debug_msg("Scheduled nodes this round: I ");
			debug_msg_uint(_lwb_num_timeslots_init);
			debug_msg(", R ");
			debug_msg_uint(_lwb_num_timeslots_resp);
			debug_msg("\r\n");

#ifdef GLOSSY_ANCHOR_SYNC_TEST
			_sched_req_pkt.sync_depth = in_glossy_sync->header.seqNum;
#endif

			if(_last_sync_timestamp + ((uint64_t)(DW_DELAY_FROM_US(GLOSSY_UPDATE_INTERVAL_US * 0.5)) << 8) < dw_timestamp){
				if(_last_sync_timestamp + ((uint64_t)(DW_DELAY_FROM_US(GLOSSY_UPDATE_INTERVAL_US * 1.5)) << 8) > dw_timestamp){
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
                    memcpy(&_sync_pkt,        in_glossy_sync, get_sync_packet_length(in_glossy_sync) - sizeof(struct ieee154_footer)); // Write valid part of the packet (up to the valid part of the array) to the local _sync_pkt
					memcpy(&_sync_pkt_buffer, in_glossy_sync, get_sync_packet_length(in_glossy_sync));

					uint32_t delay_time = (dw_timestamp >> 8) + (DW_DELAY_FROM_US(GLOSSY_FLOOD_TIMESLOT_US) & 0xFFFFFFFE);
					delay_time &= 0xFFFFFFFE;
					dwt_forcetrxoff();
					lwb_send_sync(delay_time);

					_glossy_currently_flooding = TRUE;
				} else {
					// We lost sync :(
					_lwb_in_sync = FALSE;
				}
			} else {
				// We've just received a following packet in the flood
				// This really shouldn't happen, but for now let's ignore it
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
        helper_print_EUI(eui);
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
        _lwb_num_timeslots_init++;
    }

    return slot;
}

static uint8_t schedule_resp(uint8_t * eui) {

    uint8_t slot = schedule_device( (uint8_t*)_resp_sched_euis, PROTOCOL_RESP_SCHED_MAX * PROTOCOL_EUI_LEN, eui);

    if (slot < 0xFF) {
        _sched_timeouts[PROTOCOL_RESP_SCHED_OFFSET + slot] = 0;
        _lwb_num_timeslots_resp++;
    }

    return slot;
}

static uint8_t deschedule_device(uint8_t * array, uint8_t array_length, uint8_t * eui) {

	for (uint8_t i = 0; i < (array_length / PROTOCOL_EUI_LEN); i++) {

		if (memcmp( (array + i * PROTOCOL_EUI_LEN), eui, PROTOCOL_EUI_LEN) == 0) {
			memset( (array + i * PROTOCOL_EUI_LEN),   0, PROTOCOL_EUI_LEN);

			/*debug_msg("Descheduled EUI ");
			helper_print_EUI(eui);
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
        _lwb_num_timeslots_init--;
    }

    return slot;
}

static uint8_t deschedule_resp(uint8_t * eui) {

    uint8_t slot = deschedule_device( (uint8_t*)_resp_sched_euis, PROTOCOL_RESP_SCHED_MAX * PROTOCOL_EUI_LEN, eui);

    if (slot < 0xFF) {
        _sched_timeouts[PROTOCOL_RESP_SCHED_OFFSET + slot] = 0;
        _lwb_num_timeslots_resp--;
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

static void increment_sched_timeout(){

    // Timeout initiators
    for(int ii=0; ii < (PROTOCOL_INIT_SCHED_MAX); ii++){

        // Check if slot is actually used
        if(_init_sched_euis[ii][0] > 0){
            _sched_timeouts[PROTOCOL_INIT_SCHED_OFFSET + ii]++;

            if(_sched_timeouts[PROTOCOL_INIT_SCHED_OFFSET + ii] == TAG_SCHED_TIMEOUT) {
                memset(_init_sched_euis[ii], 0, PROTOCOL_EUI_LEN);
                _lwb_timeslot_init--;

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
                _lwb_timeslot_resp--;

                // Reset
                _sched_timeouts[PROTOCOL_RESP_SCHED_OFFSET + ii] = 0;
            }
        } else {
            _sched_timeouts[PROTOCOL_RESP_SCHED_OFFSET + ii] = 0;
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

static uint8_t get_sync_packet_length(struct pp_sched_flood * packet) {
	uint8_t packet_size  = sizeof(struct ieee154_header_broadcast) + 4 + sizeof(struct ieee154_footer);
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
    compress_array( (uint8_t*)_init_sched_euis, sizeof(_init_sched_euis), PROTOCOL_EUI_LEN);
    compress_array( (uint8_t*)_resp_sched_euis, sizeof(_init_sched_euis), PROTOCOL_EUI_LEN);

    // Set correct values
    _sync_pkt.init_schedule_length = (uint8_t)_lwb_num_timeslots_init;
    _sync_pkt.resp_schedule_length = (uint8_t)_lwb_num_timeslots_resp;
    _sync_pkt.round_length         = (uint8_t)( 1 /*Sync itself*/ + _sync_pkt.init_schedule_length * LWB_SLOTS_PER_RANGE + _sync_pkt.resp_schedule_length + _lwb_num_timeslots_cont);

    // Set array; as this array is sparse, we use the sync_pkt_buffer to send the data afterwards
    memset(_sync_pkt.eui_array, 0, sizeof(_sync_pkt.eui_array));

    memcpy(_sync_pkt.eui_array + PROTOCOL_INIT_SCHED_OFFSET * PROTOCOL_EUI_LEN, _init_sched_euis, _lwb_num_timeslots_init * PROTOCOL_EUI_LEN);
    memcpy(_sync_pkt.eui_array + PROTOCOL_RESP_SCHED_OFFSET * PROTOCOL_EUI_LEN, _resp_sched_euis, _lwb_num_timeslots_resp * PROTOCOL_EUI_LEN);
}

static void write_sync_to_packet_buffer() {

	// Clear buffer
	memset(_sync_pkt_buffer, 0, sizeof(_sync_pkt_buffer));

	// Fill buffer:
	uint8_t offset = 0;

	// Header + Constant part
	uint8_t header_length = sizeof(struct ieee154_header_broadcast) + 4;

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
