#ifndef __RANGETEST_TAG_H
#define __RANGETEST_TAG_H

#include "rangetest_common.h"
#include "deca_device_api.h"
#include "deca_regs.h"

typedef enum {
	TEST_TSTATE_IDLE,
	TEST_TSTATE_BROADCASTS,
	TEST_TSTATE_LISTENING
} rangetest_tag_state_e;

// Size buffers for reading in packets
#define RANGETEST_TAG_MAX_RX_PKT_LEN 296

typedef struct {
	// Our timer object that we use for timing packet transmissions
	stm_timer_t* tag_timer;
	
	rangetest_tag_state_e state;
	
	// Which subsequence slot we are on when transmitting broadcast packets
	// for ranging.
	uint8_t ranging_broadcast_ss_num;
	
	// How many anchor responses we have gotten
	uint8_t anchor_response_count;

	// Prepopulated struct of the outgoing broadcast poll packet.
	struct rangetest_packet pp_tag_poll_pkt;

} rangetest_tag_scratchspace_struct;

rangetest_tag_scratchspace_struct *test_ot_scratch;

void 	     rangetest_tag_init (void *app_scratchspace);
dw1000_err_e rangetest_tag_start_ranging_event ();

#endif
