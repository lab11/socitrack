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
	RSTATE_PENDING,
	RSTATE_RESPONDING
} standard_resp_state_e;

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

	// Which spot in the response slot we are scheduled in
	uint8_t resp_window_timeslot;
	// Which spot in the response slot we are currently at
	uint8_t resp_window_nr;
	
	// Keep track of, in each ranging session with a tag, how many packets we
	// receive on each antenna. This lets us pick the best antenna to use
	// when responding to a tag.
	uint8_t resp_antenna_recv_num[NUM_ANTENNAS];
	
	// Packet that the anchor unicasts to the tag
	struct pp_anc_final pp_anc_final_pkt;

	uint8_t pp_anc_final_pkt_buffer[sizeof(struct pp_anc_final)];

} standard_resp_scratchspace_struct;

// PUBLIC FUNCTIONS ----------------------------------------------------------------------------------------------------

void 		 standard_resp_init (standard_resp_scratchspace_struct *app_scratchspace);
dw1000_err_e standard_resp_start ();
void 		 standard_resp_stop ();

void		 standard_resp_trigger_response (uint8_t slot_nr);

#endif
