#ifndef __APP_TEST_RESP_H
#define __APP_TEST_RESP_H

#include "deca_device_api.h"
#include "deca_regs.h"

#include "dw1000.h"
#include "app_test_common.h"
#include "prng.h"

// Set at some arbitrary length for what the longest packet we will receive is.
#define RANGETEST_ANCHOR_MAX_RX_PKT_LEN 64

typedef enum {
	TEST_RSTATE_IDLE,
	TEST_RSTATE_RESPONDING
} test_resp_state_e;

typedef struct {
	// Our timer object that we use for timing packet transmissions
	stm_timer_t* timer;
	
	/******************************************************************************/
	// Keep track of state for the given ranging event this anchor is handling.
	/******************************************************************************/
	// What the anchor is currently doing
	test_resp_state_e state;
	// Which spot in the ranging broadcast sequence we are currently at
	uint8_t ranging_broadcast_ss_num;
	
	// Keep track of, in each ranging session with a tag, how many packets we
	// receive on each antenna. This lets us pick the best antenna to use
	// when responding to a tag.
	uint8_t anchor_antenna_recv_num[NUM_ANTENNAS];
} test_resp_scratchspace_struct;

test_resp_scratchspace_struct *test_oa_scratch;

void 	     rangetest_anchor_init (test_resp_scratchspace_struct *app_scratchspace);
dw1000_err_e rangetest_anchor_start ();

dw1000_err_e simpletest_anchor_start ();

#endif
