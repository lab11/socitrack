#ifndef __CALIBRATION_HEADER_H__
#define __CALIBRATION_HEADER_H__

// Includes ------------------------------------------------------------------------------------------------------------

#include <stdint.h>
#include "dw1000.h"
#include "system.h"

// Calibration parameters ----------------------------------------------------------------------------------------------

#define CALIBRATION_ROUND_PERIOD_US             100000  // 100 ms
#define CALIBRATION_ROUND_TIMEOUT_US            50000   // 50 ms
#define CALIBRATION_EPSILON_US                  5000    // 3 ms
#define CALIBRATION_NUM_NODES                   3
#define CALIBRATION_MASTER_NODE                 0
#define CALIBRATION_NUM_PACKETS                 3
#define CALIBRATION_NUM_ANTENNAS                3
#define CALIBRATION_NUM_CHANNELS                3
#define CALIBRATION_MAX_PACKET_LEN              64

// Calibration message types -------------------------------------------------------------------------------------------

#define MSG_TYPE_CALIBRATION_INIT               0x90
#define MSG_TYPE_CALIBRATION_MSG                0x91

// Returns true if this node index is the one being calibrated on this round.
// Round 0: calibrate node with index 3, started by node with index 0
// Round 1: calibrate node with index 0, started by node with index 1
// Round 2: calibrate node with index 1, started by node with index 2
// Round 3: calibrate node with index 2, started by node with index 3
#define CALIBRATION_ROUND_FOR_ME(round_, index_) \
    ( ((((int)(round_ % CALIBRATION_NUM_NODES)) - ((int)index_)) == 1) || ((((int)(round_ % CALIBRATION_NUM_NODES)) - ((int)index_)) == (1 - CALIBRATION_NUM_NODES)) )
#define CALIBRATION_ROUND_STARTED_BY_ME(round_, index_) ((round_ % CALIBRATION_NUM_NODES) == index_)

// Calibration data structures -----------------------------------------------------------------------------------------

typedef struct { uint8_t index; } calibration_config_t;

typedef struct __attribute__ ((__packed__))
{
   struct ieee154_header_broadcast header;
   uint8_t message_type;
   uint32_t round_num;
   uint8_t packet_index;
   struct ieee154_footer footer;
} calibration_packet_t;

typedef struct
{
   uint32_t round_num;
   uint64_t calibration_timing[CALIBRATION_NUM_PACKETS];
   bool timeout_firing, init_received;
   uint32_t dw_slack_delay_multiplier;
} calibration_state_t;

// Public functions ----------------------------------------------------------------------------------------------------

bool calibration_configure(calibration_config_t *config);
bool calibration_start();
void calibration_stop();
bool calibration_reset();

#endif // __CALIBRATION_HEADER_H__
