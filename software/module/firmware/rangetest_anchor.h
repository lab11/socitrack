#ifndef __RANGETEST_ANCHOR_H
#define __RANGETEST_ANCHOR_H

#include "deca_device_api.h"
#include "deca_regs.h"

#include "dw1000.h"
#include "prng.h"

// Set at some arbitrary length for what the longest packet we will receive is.
#define RANGETEST_ANCHOR_MAX_RX_PKT_LEN 64

typedef enum {
	TEST_ASTATE_IDLE,
	TEST_ASTATE_RESPONDING
} rangetest_anchor_state_e;

typedef struct {
	// Our timer object that we use for timing packet transmissions
	stm_timer_t* timer;
	
	/******************************************************************************/
	// Keep track of state for the given ranging event this anchor is handling.
	/******************************************************************************/
	// What the anchor is currently doing
	rangetest_anchor_state_e state;
	// Which spot in the ranging broadcast sequence we are currently at
	uint8_t ranging_broadcast_ss_num;
	
	// Keep track of, in each ranging session with a tag, how many packets we
	// receive on each antenna. This lets us pick the best antenna to use
	// when responding to a tag.
	uint8_t anchor_antenna_recv_num[NUM_ANTENNAS];
} rangetest_anchor_scratchspace_struct;

rangetest_anchor_scratchspace_struct *test_oa_scratch;

void 	     rangetest_anchor_init (void *app_scratchspace);
dw1000_err_e rangetest_anchor_start ();

#endif
