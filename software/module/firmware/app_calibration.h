#ifndef __APP_CALIBRATION_H
#define __APP_CALIBRATION_H

#include "stdint.h"
#include "system.h"
#include "timer.h"
#include "dw1000.h"


/******************************************************************************/
// Parameters for the calibration protocol
/******************************************************************************/

// How long the master waits before starting a new round of calibration timing.
#define CALIBRATION_ROUND_PERIOD_US 100000 // 100 ms

// How long non-master nodes wait before assuming something went wrong and
// resume waiting for the start of the next round.
#define CALIBRATION_ROUND_TIMEOUT_US 50000 // 50 ms

// How long between receiving a calibration packet and responding.
// This must be known.
#define CALIBRATION_EPSILON_US 5000 // 3 ms

// How many nodes participate in calibration
#define CALIBRATION_NUM_NODES   3
#define CALIBRATION_MASTER_NODE 0

// How many timestamps we are actually collecting during the ranging
#define CALIBRATION_NUM_TIMESTAMPS 3

// How many antennas we want to calibrate with
#define CALIB_NUM_ANTENNAS 3
#define CALIB_NUM_CHANNELS 3

#define CALIBRATION_MAX_RX_PKT_LEN 64

// Message types that identify the UWB packets
#define MSG_TYPE_PP_CALIBRATION_INIT 0x90
#define MSG_TYPE_PP_CALIBRATION_MSG  0x91

#define CALIBRATION_ROUND_STARTED_BY_ME(round_, index_) \
	((round_ % CALIBRATION_NUM_NODES) == index_)

// Returns true if this node index is the one being calibrated on this round.
// Round 0: calibrate node with index 2, started by node with index 0
// Round 1: calibrate node with index 0, started by node with index 1
// Round 2: calibrate node with index 1, started by node with index 2
#define CALIBRATION_ROUND_FOR_ME(round_, index_) \
	( (( ((int) (round_%CALIBRATION_NUM_NODES)) - ((int) index_) ) == 1) || (( ((int) (round_%CALIBRATION_NUM_NODES)) - ((int) index_) ) == -2) )


/******************************************************************************/
// Data Structs for packet messages between calibrating nodes
/******************************************************************************/

// Passed in over the host interface.
typedef struct {
    uint8_t index;
} calibration_config_t;

// Packet the tag broadcasts to all nearby anchors
struct pp_calibration_msg  {
    struct ieee154_header_broadcast header;
    uint8_t  message_type; // Packet type identifier so the anchor knows what it is receiving.
    uint32_t round_num;    // Index of which which round we are currently in. This sets antenna/channel.
    uint8_t  num;          // Index of this packet in the ranging round.
    struct ieee154_footer footer;
} __attribute__ ((__packed__));

typedef struct {

    // Configuration and settings

    // Our local reference to the timer for all of the high-level application code.
    stm_timer_t* app_timer;


    // Calibration state

    // Which calibration round we are currently in
    uint32_t round_num;

    // Timing of packet transmissions and receptions.
    // What these are vary based on which node this is.
    uint64_t calibration_timing[CALIBRATION_NUM_TIMESTAMPS];

    // Buffer to send back to the host
    uint8_t calibration_response_buf[CALIBRATION_MAX_RX_PKT_LEN];

    // Counter for the backup timers used in case of packet drop
    uint8_t timeout_firing;

    // Keep track of if we got the init() packet from node 0. If not, then we didn't
    // set the antenna and channel correctly, so we shouldn't report these values.
    bool init_received;

    // Use this in case we get transmission delay errors to extend how long we delay the packet.
    uint32_t dw_slack_delay_multiplier;

    // Prepopulated struct of the outgoing broadcast poll packet.
    struct pp_calibration_msg pp_calibration_pkt;
} calibration_scratchspace_struct;


/******************************************************************************/
// Public functions
/******************************************************************************/

void calibration_configure (calibration_config_t* config);
void calibration_start ();
void calibration_stop ();
void calibration_reset ();

#endif