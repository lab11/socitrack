#ifndef __APP_TEST_INIT_H
#define __APP_TEST_INIT_H

#include "app_test_common.h"
#include "deca_device_api.h"
#include "deca_regs.h"

typedef enum {
	TEST_ISTATE_IDLE,
	TEST_ISTATE_BROADCASTS,
	TEST_ISTATE_LISTENING
} test_init_state_e;

// Size buffers for reading in packets
#define RANGETEST_TAG_MAX_RX_PKT_LEN 296

typedef struct {
	// Our timer object that we use for timing packet transmissions
	stm_timer_t* tag_timer;
	
	test_init_state_e state;
	
	// Which subsequence slot we are on when transmitting broadcast packets
	// for ranging.
	uint8_t ranging_broadcast_ss_num;
	
	// How many anchor responses we have gotten
	uint8_t anchor_response_count;

	// Prepopulated struct of the outgoing broadcast poll packet.
	struct rangetest_packet pp_tag_poll_pkt;

} test_init_scratchspace_struct;

test_init_scratchspace_struct *test_ot_scratch;

void 	     rangetest_tag_init (test_init_scratchspace_struct *app_scratchspace);
dw1000_err_e rangetest_tag_start_ranging_event ();

dw1000_err_e simpletest_tag_start ();

#endif
