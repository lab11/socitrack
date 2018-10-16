
#include <string.h>
#include <stddef.h>
#include <math.h>

#include "stm32f0xx_gpio.h"

#include "glossy.h"

#include "board.h"
#include "dw1000.h"
#include "deca_regs.h"
#include "app_standard_common.h"
#include "timer.h"
#include "prng.h"
#include "SEGGER_RTT.h"

void send_sync(uint32_t delay_time);

static stm_timer_t* 			 _glossy_timer;
static struct pp_sched_flood 	 _sync_pkt;
static struct pp_sched_req_flood _sched_req_pkt;
static glossy_role_e 			 _role;

static uint8_t  _last_sync_depth;
static uint64_t _last_sync_timestamp;
static uint64_t _last_overall_timestamp;
static uint64_t _time_overflow;
static uint64_t _last_time_sent;
static uint64_t _glossy_flood_timeslot_corrected_us;
static uint32_t _last_delay_time;
static bool  	_lwb_in_sync;
static uint8_t  _xtal_trim;
static uint8_t  _last_xtal_trim;
static bool     _sending_sync;
static uint32_t _lwb_counter;
static uint8_t  _cur_glossy_depth;
static bool     _glossy_currently_flooding;

static bool     _lwb_sched_en;
static bool     _lwb_scheduled;
static uint32_t _lwb_num_timeslots;
static uint32_t _lwb_timeslot;
static uint32_t _lwb_mod_timeslot;
static void (*_lwb_schedule_callback)(void);
static double _clock_offset;

static uint8_t _sched_euis [MAX_SCHED_TAGS][EUI_LEN];
static uint8_t _tag_timeout[MAX_SCHED_TAGS];

static ranctx _prng_state;

#ifdef GLOSSY_PER_TEST
static uint32_t _total_syncs_sent;
static uint32_t _total_syncs_received;
#endif

uint8_t uint64_count_ones(uint64_t number){
	int ii;
	uint8_t ret = 0;
	for(ii = 0; ii < 64; ii++){
		if(number & (1 << ii)) ret++;
	}
	return ret;
}

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
		.message_type     = MSG_TYPE_PP_GLOSSY_SYNC,
		.tag_ranging_mask = 0,
		.tag_sched_idx    = 0,
		.tag_sched_eui    = { 0 }
	};

	memcpy(_sync_pkt.tag_sched_eui, my_eui, EUI_LEN);

	// Init Glossy packet: Schedule request
	_sched_req_pkt.header 		   = _sync_pkt.header;
	_sched_req_pkt.message_type    = MSG_TYPE_PP_GLOSSY_SCHED_REQ;
	_sched_req_pkt.deschedule_flag = 0;
	memcpy(_sched_req_pkt.tag_sched_eui, my_eui, EUI_LEN);

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
	_lwb_scheduled 			   = FALSE;
	_lwb_num_timeslots		   = 0;
	_lwb_timeslot			   = 0;
	_lwb_mod_timeslot		   = 0;
	_lwb_schedule_callback 	   = NULL;
	_clock_offset			   = 0;

	memset(_sched_euis,  0, sizeof(_sched_euis));
	memset(_tag_timeout, 0, sizeof(_tag_timeout));
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
		    memcpy(_sched_euis[0], standard_get_EUI(), EUI_LEN);
		    _lwb_sched_en  = TRUE;
		    _lwb_scheduled = TRUE;
		    _lwb_timeslot  = 0;
			_sync_pkt.tag_ranging_mask |= (uint64_t)(1) << 0;
		}
	}

	// The glossy timer acts to synchronize everyone to a common timebase
	_glossy_timer = timer_init();
}

void glossy_start() {

    // Kick-off the Glossy timer
    timer_start(_glossy_timer, LWB_SLOT_US, glossy_sync_task);
}

void increment_sched_timeout(){
	for(int ii=0; ii < MAX_SCHED_TAGS; ii++){
		if(_sync_pkt.tag_ranging_mask & ((uint64_t)(1) << ii)){
			_tag_timeout[ii]++;
			if(_tag_timeout[ii] == TAG_SCHED_TIMEOUT)
				_sync_pkt.tag_ranging_mask &= ~((uint64_t)(1) << ii);
		} else {
			_tag_timeout[ii] = 0;
		}
	}

	// Glossy master verifies that he doesnt kick himself out of the network
	if (standard_is_init_enabled()) {
        if (memcmp(_sched_euis[0], standard_get_EUI(), EUI_LEN) == 0) {
            _tag_timeout[0] = 0;
        } else {
            debug_msg("ERROR: Glossy Master did not schedule itself!\n");
        }
	}
}

void glossy_deschedule(){
	_sched_req_pkt.deschedule_flag = 1;
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

	if(_role == GLOSSY_MASTER) {

		// LWB Slot 1: During the first timeslot, put ourselves back into RX mode to listen for schedule requests
		if(_lwb_counter == 1){

		    //debug_msg("Listening for requests...\n");

			dwt_rxenable(0);
#ifdef GLOSSY_ANCHOR_SYNC_TEST
			dw1000_choose_antenna(LWB_ANTENNA);
#endif

#if (BOARD_V == SQUAREPOINT)
			// Signal normal round by turning on GREEN
			GPIO_WriteBit(STM_LED_RED_PORT,   STM_LED_RED_PIN,   Bit_SET);
			GPIO_WriteBit(STM_LED_BLUE_PORT,  STM_LED_BLUE_PIN,  Bit_SET);
			GPIO_WriteBit(STM_LED_GREEN_PORT, STM_LED_GREEN_PIN, Bit_RESET);
#endif

		// LWB Slot N-1: Last timeslot is used by the master to schedule the next glossy sync packet
		} else if(_lwb_counter == (GLOSSY_UPDATE_INTERVAL_US/LWB_SLOT_US)-1){
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
		
			_last_time_sent += GLOSSY_UPDATE_INTERVAL_DW;
			send_sync(_last_time_sent);
			_sending_sync = TRUE;

			debug_msg("Sent LWB schedule\r\n");

#if (BOARD_V == SQUAREPOINT)
			// Signal that distributing schedule by turning on WHITE (will blink and be turned off after 10ms)
			GPIO_WriteBit(STM_LED_RED_PORT,   STM_LED_RED_PIN,   Bit_RESET);
			GPIO_WriteBit(STM_LED_BLUE_PORT,  STM_LED_BLUE_PIN,  Bit_RESET);
			GPIO_WriteBit(STM_LED_GREEN_PORT, STM_LED_GREEN_PIN, Bit_RESET);
#endif
		// LWB Slot > N: Invalid counter value
		} else if (_lwb_counter > (GLOSSY_UPDATE_INTERVAL_US/LWB_SLOT_US)-1) {
			debug_msg("WARNING: LWB counter overshooting, currently at ");
			debug_msg_int(_lwb_counter);
			debug_msg("\n");
		}

	} else if (_role == GLOSSY_SLAVE) {

		// OUT OF SYNC: Force ourselves into RX mode if we still haven't received any sync floods
		if( (!_lwb_in_sync || (_lwb_counter > (GLOSSY_UPDATE_INTERVAL_US/LWB_SLOT_US)) ) && ((_lwb_counter % 5) == 0)) {

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

			// LWB Slot 1: Contention slot
			if(_lwb_counter == 1) {
				dw1000_update_channel(LWB_CHANNEL);
				dw1000_choose_antenna(LWB_ANTENNA);

				if((!_lwb_scheduled && _lwb_sched_en) || _sched_req_pkt.deschedule_flag) {

				    debug_msg("Sending schedule request...\r\n");
				    standard_set_init_active(TRUE);
				    standard_set_resp_active(FALSE);

					dwt_forcetrxoff();

					uint16_t frame_len = sizeof(struct pp_sched_req_flood);
					dwt_writetxfctrl(frame_len, 0, MSG_TYPE_CONTROL);

					// Send out a schedule request during this contention slot
					// Pick a random time offset to avoid colliding with others
#ifdef GLOSSY_ANCHOR_SYNC_TEST
					uint32_t sched_req_time = (uint32_t)(_sched_req_pkt.tag_sched_eui[0] - 0x31) * GLOSSY_FLOOD_TIMESLOT_US;
					uint32_t delay_time = (dwt_readsystimestamphi32() + DW_DELAY_FROM_PKT_LEN(sizeof(struct pp_sched_req_flood)) + DW_DELAY_FROM_US(sched_req_time)) & 0xFFFFFFFE;
					double turnaround_time = (double)((((uint64_t)(delay_time) << 8) - _last_sync_timestamp) & 0xFFFFFFFFFFUL);// + DW_DELAY_FROM_US(GLOSSY_FLOOD_TIMESLOT_US)*_last_sync_depth;
					turnaround_time /= _clock_offset;
					_sched_req_pkt.turnaround_time = (uint64_t)(turnaround_time);
					dw1000_choose_antenna(LWB_ANTENNA);
#else
					uint32_t sched_req_time = (ranval(&_prng_state) % (uint32_t)(LWB_SLOT_US-2*GLOSSY_FLOOD_TIMESLOT_US)) + GLOSSY_FLOOD_TIMESLOT_US;
					uint32_t delay_time = (dwt_readsystimestamphi32() + DW_DELAY_FROM_PKT_LEN(sizeof(struct pp_sched_req_flood)) + DW_DELAY_FROM_US(sched_req_time)) & 0xFFFFFFFE;
#endif

					dwt_setdelayedtrxtime(delay_time);
					dwt_setrxaftertxdelay(LWB_SLOT_US);
					dwt_starttx(DWT_START_TX_DELAYED | DWT_RESPONSE_EXPECTED);
					dwt_settxantennadelay(DW1000_ANTENNA_DELAY_TX);
					dwt_writetxdata(sizeof(struct pp_sched_req_flood), (uint8_t*) &_sched_req_pkt, 0);

					_sched_req_pkt.deschedule_flag = 0;
				} else {
					dwt_rxenable(0);
				}

			// LWB Slot N-1: Get ready for next glossy flood
			}  else if(_lwb_counter == (GLOSSY_UPDATE_INTERVAL_US/LWB_SLOT_US)-1){

				// Make sure we're in RX mode, ready for next glossy sync flood!
				//dwt_setdblrxbuffmode(FALSE);
				glossy_enable_reception();

			// LWB Slot > N: Invalid counter value
			} else if (_lwb_counter > (GLOSSY_UPDATE_INTERVAL_US/LWB_SLOT_US)-1) {
				debug_msg("WARNING: LWB counter overshooting, currently at ");
				debug_msg_int(_lwb_counter);
				debug_msg("\n");
			}
		}

	} else {
		debug_msg("ERROR: Unknown Glossy role!\n");
	}

	// Both Master and Slave can do ranging if they are scheduled
	// LWB Slots 2-N-2: Ranging slots
	if( (1 < _lwb_counter) &&
        (    _lwb_counter < (GLOSSY_UPDATE_INTERVAL_US/LWB_SLOT_US - LWB_SLOTS_PER_RANGE)) ) {

		if( _lwb_scheduled &&
		   ( ((_lwb_counter - 2) / LWB_SLOTS_PER_RANGE) == _lwb_timeslot) &&
		   ( ((_lwb_counter - 2) % LWB_SLOTS_PER_RANGE) == 0            ) ) {
			// Our scheduled timeslot!  Call the timeslot callback which will likely kick off a ranging event
			// Note: If all slots should be used (i.e. we wrap the schedule around), substitute "== _lwb_timeslot" with "% _lwb_num_timeslots == _lwb_mod_timeslot"
			if (_lwb_schedule_callback) {
				_lwb_schedule_callback();
			} else {
				debug_msg("ERROR: Invalid LWB callback function!\n");
			}
		}
	}
}

void lwb_set_sched_request(bool sched_en){
	_lwb_sched_en = sched_en;
}

void lwb_set_sched_callback(void (*callback)(void)){
	_lwb_schedule_callback = callback;
}

bool glossy_process_txcallback(){

	// Signal whether the current Tx packet was actually a Glossy one
	bool is_glossy_callback = FALSE;

	if(_role == GLOSSY_MASTER && _sending_sync) {

		// Sync has sent, set the timer to send the next one at a later time
		timer_reset(_glossy_timer, 0);
		_lwb_counter = 0;
		_sending_sync = FALSE;

		is_glossy_callback = TRUE;
	}
	else if(_role == GLOSSY_SLAVE && _glossy_currently_flooding) {

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

void send_sync(uint32_t delay_time){
	uint16_t frame_len = sizeof(struct pp_sched_flood);
	dwt_writetxfctrl(frame_len, 0, MSG_TYPE_CONTROL);

	_last_delay_time = delay_time;

	dwt_setdelayedtrxtime(delay_time);
	dwt_setrxaftertxdelay(1);

	dwt_starttx(DWT_START_TX_DELAYED);
	dwt_settxantennadelay(DW1000_ANTENNA_DELAY_TX);
	dwt_writetxdata(sizeof(_sync_pkt), (uint8_t*) &_sync_pkt, 0);
}

#define CW_CAL_12PF ((3.494350-3.494173)/3.4944*1e6/30)
#define CW_CAL_22PF ((3.494078-3.493998)/3.4944*1e6/30)
#define CW_CAL_33PF ((3.493941-3.493891)/3.4944*1e6/30)
int8_t clock_offset_to_trim_diff(double ppm_offset){
       return (int8_t) (floor(ppm_offset/CW_CAL_12PF + 0.5));
}

void glossy_sync_process(uint64_t dw_timestamp, uint8_t *buf){

	struct pp_sched_flood *in_glossy_sync = (struct pp_sched_flood *) buf;
	struct pp_sched_req_flood *in_glossy_sched_req = (struct pp_sched_req_flood *) buf;

	// Due to frequent overflow in the Decawave system time counter, we must keep a running total of the number of times it's overflown
	if(dw_timestamp < _last_overall_timestamp) {
		_time_overflow += 0x10000000000ULL;
	}
	_last_overall_timestamp = dw_timestamp;
	dw_timestamp += _time_overflow;

	if(_role == GLOSSY_MASTER) {

		// If this is a schedule request, try to fit the requesting tag into the schedule
		if(in_glossy_sync->message_type == MSG_TYPE_PP_GLOSSY_SCHED_REQ){

		    debug_msg("Received schedule request\n");

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
			int i, candidate_slot = -1;
			for(i = 0; i < MAX_SCHED_TAGS; i++){
				if(memcmp(_sched_euis[i], in_glossy_sched_req->tag_sched_eui, EUI_LEN) == 0){
					_sync_pkt.tag_sched_idx = i;
					candidate_slot = i;
					break;
				} else if((_sync_pkt.tag_ranging_mask & ((uint64_t)(1) << i)) == 0){
					candidate_slot = i;
				}
			}

			if (candidate_slot == -1) {
			    debug_msg("WARNING: No more ranging slots available!\n");
			} else {

			    /*debug_msg("Found candidate slot for EUI ");
			    helper_print_EUI(in_glossy_sched_req->tag_sched_eui);
			    debug_msg(": ");
			    debug_msg_int(candidate_slot);*/

                memcpy(_sched_euis[candidate_slot], in_glossy_sched_req->tag_sched_eui, EUI_LEN);
                memcpy(_sync_pkt.tag_sched_eui,     in_glossy_sched_req->tag_sched_eui, EUI_LEN);

                if (in_glossy_sched_req->deschedule_flag) {
                    _sync_pkt.tag_ranging_mask &= ~((uint64_t) (1) << candidate_slot);
                } else {
                    _sync_pkt.tag_ranging_mask |= (uint64_t) (1) << candidate_slot;
                }

                _sync_pkt.tag_sched_idx = (uint8_t)candidate_slot;
                _tag_timeout[candidate_slot] = 0;
            }
#endif
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

		if(in_glossy_sync->message_type == MSG_TYPE_PP_GLOSSY_SCHED_REQ) {

		    debug_msg("Received schedule request from another node\n");
#ifndef GLOSSY_ANCHOR_SYNC_TEST
			// Increment depth counter
			_cur_glossy_depth = ++in_glossy_sched_req->header.seqNum;
			_glossy_currently_flooding = TRUE;

			uint16_t frame_len = sizeof(struct pp_sched_req_flood);
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
			dwt_writetxdata(sizeof(struct pp_sched_req_flood), (uint8_t*) in_glossy_sched_req, 0);
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

            /*debug_msg("Scheduled node with EUI ");
            helper_print_EUI(in_glossy_sync->tag_sched_eui);
            debug_msg(" in slot ");
            debug_msg_int(_lwb_timeslot);
            debug_msg("\n");*/

			// First check to see if this sync packet contains a schedule update for this node
			if(memcmp(in_glossy_sync->tag_sched_eui, standard_get_EUI(), EUI_LEN) == 0) {
				_lwb_timeslot = in_glossy_sync->tag_sched_idx;
				_lwb_scheduled = TRUE;
			}

			// Next, make sure the tag is still scheduled
			if(_lwb_scheduled && ((in_glossy_sync->tag_ranging_mask & ((uint64_t)(1) << _lwb_timeslot)) == 0)) {
                _lwb_scheduled = FALSE;
            }

			_lwb_num_timeslots = uint64_count_ones(in_glossy_sync->tag_ranging_mask);

			debug_msg("Scheduled nodes this round: ");
			debug_msg_int(_lwb_num_timeslots);
			debug_msg("\r\n");

			_lwb_mod_timeslot = uint64_count_ones(in_glossy_sync->tag_ranging_mask & (((uint64_t)(1) << _lwb_timeslot) - 1));

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
					memcpy(&_sync_pkt, in_glossy_sync, sizeof(struct pp_sched_flood));
					_cur_glossy_depth = ++_sync_pkt.header.seqNum;

					uint32_t delay_time = (dw_timestamp >> 8) + (DW_DELAY_FROM_US(GLOSSY_FLOOD_TIMESLOT_US) & 0xFFFFFFFE);
					delay_time &= 0xFFFFFFFE;
					dwt_forcetrxoff();
					send_sync(delay_time);

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

			// Enable responders
			if (standard_is_resp_enabled()) {
				standard_set_resp_active(TRUE);
			}

		} else {
		    debug_msg("ERROR: Received unknown LWB packet as Glossy slave!\n");
		}
	}
}
