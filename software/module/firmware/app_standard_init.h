#ifndef __APP_STANDARD_INIT_H
#define __APP_STANDARD_INIT_H

#include "app_standard_common.h"
#include "deca_device_api.h"
#include "deca_regs.h"

// DATA STRUCTURES -----------------------------------------------------------------------------------------------------

typedef enum {
	ISTATE_IDLE,
	ISTATE_BROADCASTS,
	ISTATE_TRANSITION_TO_ANC_FINAL,
	ISTATE_LISTENING,
	ISTATE_CALCULATE_RANGE
} init_state_e;

typedef struct {
    uint8_t  anchor_addr[PROTOCOL_EUI_LEN];
    uint8_t  anchor_final_antenna_index; // The antenna the anchor used when it responded.
    uint64_t anc_final_tx_timestamp; // When the anchor node sent the ANC_FINAL
    uint64_t anc_final_rx_timestamp; // When the tag received the ANC_FINAL
    uint8_t  tag_poll_first_idx;
    uint64_t tag_poll_first_TOA;
    uint8_t  tag_poll_last_idx;
    uint64_t tag_poll_last_TOA;
    uint16_t tag_poll_TOAs[NUM_RANGING_BROADCASTS];
} __attribute__ ((__packed__)) anchor_responses_t;

// ERRORS for reporting to the TAG host what happened with ranges from different
// anchors. If an anchor didn't respond or the packet didn't go through then
// it will not be included. If the anchor did respond, then it will be included,
// and if something went wrong with the range an invalid range from below
// will be returned.

// The ANCHOR did not receive matching packets from the first three cycle.
// This prevents us from calculating clock skew, and we have to skip this
// anchor range.
#define STANDARD_INIT_RANGE_ERROR_NO_OFFSET 0x80000001
// The anchor did not receive enough packets from the tag, so we don't have
// enough observations (ranges) to actually calculate a range to this
// anchor.
#define STANDARD_INIT_RANGE_ERROR_TOO_FEW_RANGES 0x80000002
// Something else went wrong that we don't have pinned down.
#define STANDARD_INIT_RANGE_ERROR_MISC 0x8000000F

typedef struct {
	// Our timer object that we use for timing packet transmissions
	stm_timer_t* init_timer;
	
	init_state_e state;
	
	// Which subsequence slot we are on when transmitting broadcast packets
	// for ranging.
	uint8_t ranging_broadcast_ss_num;
	
	// Array of when we sent each of the broadcast ranging packets
	uint64_t ranging_broadcast_ss_send_times[NUM_RANGING_BROADCASTS];
	
	// How many anchor responses we have gotten
	uint8_t anchor_response_count;

	// Boolean to prevent stopping the listening period too early when timers are used
	bool response_listening_timer_active;
	
	// Array of when we received ANC_FINAL packets and from whom
	anchor_responses_t anchor_responses[MAX_NUM_ANCHOR_RESPONSES];
	
	// These are the ranges we have calculated to a series of anchors.
	// They use the same index as the _anchor_responses array.
	// Invalid ranges are marked with INT32_MAX.
	int32_t ranges_millimeters[MAX_NUM_ANCHOR_RESPONSES];

	// Buffer of anchor IDs and ranges to the anchor.
	// Long enough to hold an anchor id followed by the range, plus the number of ranges
	uint8_t anchor_ids_ranges[(MAX_NUM_ANCHOR_RESPONSES*(EUI_LEN+sizeof(int32_t)))+1];
	
	// Prepopulated struct of the outgoing broadcast poll packet.
	struct pp_tag_poll pp_tag_poll_pkt;
} standard_init_scratchspace_struct;

// PUBLIC FUNCTIONS ----------------------------------------------------------------------------------------------------

void         standard_initiator_init (standard_init_scratchspace_struct *app_scratchspace);
dw1000_err_e standard_init_start_ranging_event ();

void         standard_init_start_response_listening(uint8_t nr_slots);
void         standard_init_stop_response_listening();
void         standard_init_stop ();

#endif
