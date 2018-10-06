#ifndef __RANGETEST_COMMON_H
#define __RANGETEST_COMMON_H

#include "polypoint_conf.h"
#include "system.h"
#include "dw1000.h"
#include "timer.h"
#include "oneway_common.h"

/******************************************************************************/
// Parameters for the localization and ranging protocol
/******************************************************************************/

// How many of the DW1000 supported UWB channels we are using for ranging
// packets.
#define NUM_RANGING_CHANNELS 3

// It's possible that someday the number of antennas should be configurable
// to support different hardware...
#define NUM_ANTENNAS 3

/******************************************************************************/
// Data Structs for packet messages between tags and anchors
/******************************************************************************/

// Message types that identify the UWB packets. Very reminiscent of
// Active Messages from the TinyOS days.
#define MSG_TYPE_PP_NOSLOTS_TAG_POLL  0x80

// Used to show whether message is a ranging packet or not
#define MSG_TYPE_CONTROL	0
#define MSG_TYPE_RANGING	1

// Packet the tag broadcasts to all nearby anchors
struct rangetest_packet  {
	struct ieee154_header_broadcast header;
	uint8_t message_type;                   // Packet type identifier so the anchor knows what it is receiving.
	uint8_t subsequence;                    // Index of which broadcast sequence number this packet is.
	uint8_t reply_after_subsequence;        // Tells anchor which broadcast subsequence number to respond after.
	uint32_t anchor_reply_window_in_us;     // How long each anchor response window is. Each window allows multiple anchor responses.
	uint16_t anchor_reply_slot_time_in_us;  // How long that slots that break up each window are.
	struct ieee154_footer footer;
} __attribute__ ((__packed__));

/******************************************************************************/
// State objects for the Rangetest application
/******************************************************************************/

// Keep config settings for a node
typedef struct {
	dw1000_role_e my_role;
	uint8_t update_rate;
} rangetest_config_t;

void rangetest_configure (oneway_config_t* config, stm_timer_t* app_timer, void *app_scratchspace);
void rangetest_start ();

void rangetest_set_ranging_broadcast_settings (dw1000_role_e role, uint8_t subseq_num);

#endif
