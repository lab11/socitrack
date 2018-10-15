#ifndef __APP_STANDARD_RESP_H
#define __APP_STANDARD_RESP_H

#include "deca_device_api.h"
#include "deca_regs.h"

#include "dw1000.h"
#include "prng.h"
#include "app_standard_common.h"

// DATA STRUCTURES -----------------------------------------------------------------------------------------------------

typedef enum {
	RSTATE_IDLE,
	RSTATE_RANGING,
	RSTATE_RESPONDING
} standard_resp_state_e;

// Configuration data for the ANCHOR provided by the TAG
typedef struct {
	uint8_t  reply_after_subsequence;
	uint32_t resp_reply_window_in_us;
	uint16_t resp_reply_slot_time_in_us;
	uint16_t resp_reply_num_slots;
} standard_resp_config_t;

typedef struct {
	// Our timer object that we use for timing packet transmissions
	stm_timer_t* resp_timer;
	
	// State for the PRNG
	ranctx prng_state;
	
	/******************************************************************************/
	// Keep track of state for the given ranging event this anchor is handling.
	/******************************************************************************/
	// What the anchor is currently doing
	standard_resp_state_e state;
	// Which spot in the ranging broadcast sequence we are currently at
	uint8_t ranging_broadcast_ss_num;
	// What config parameters the tag sent us
	standard_resp_config_t ranging_operation_config;
	// Which spot in the listening window sequence we are in.
	// The listening window refers to the time after the ranging broadcasts
	// when the tag listens for anchor responses on each channel
	uint8_t ranging_listening_window_num;
	
	// Keep track of, in each ranging session with a tag, how many packets we
	// receive on each antenna. This lets us pick the best antenna to use
	// when responding to a tag.
	uint8_t resp_antenna_recv_num[NUM_ANTENNAS];
	
	// Packet that the anchor unicasts to the tag
	struct pp_anc_final pp_anc_final_pkt;

	bool final_ack_received;
} standard_resp_scratchspace_struct;

// PUBLIC FUNCTIONS ----------------------------------------------------------------------------------------------------

void 		 standard_resp_init (standard_resp_scratchspace_struct *app_scratchspace);
dw1000_err_e standard_resp_start ();
void 		 standard_resp_stop ();

#endif
