#ifndef __SCHEDULER_HEADER_H__
#define __SCHEDULER_HEADER_H__

// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "app_tasks.h"
#include "ranging.h"


// Data Structures -----------------------------------------------------------------------------------------------------

typedef enum {
   RANGING_STOP = 0b00000001,
   RANGING_NEW_ROUND_START = 0b00000010,
   RANGING_TX_COMPLETE = 0b00000100,
   RANGING_RX_COMPLETE = 0b00001000,
   RANGING_RX_TIMEOUT = 0b00010000
} ranging_interrupt_reason_t;

typedef enum
{
   SCHEDULE_PHASE,
   SUBSCRIPTION_PHASE,
   RANGING_PHASE,
   RANGE_STATUS_PHASE,
   RANGE_COMPUTATION_PHASE,
   UNSCHEDULED_TIME_PHASE,
   RANGING_ERROR,
   RADIO_ERROR,
   MESSAGE_COLLISION
} scheduler_phase_t;

typedef enum
{
   RANGING_PACKET = 0x80,
   SCHEDULE_PACKET = 0x81,
   STATUS_SUCCESS_PACKET = 0x82,
   SUBSCRIPTION_PACKET = 0x83,
   UNKNOWN_PACKET = 0x84
} packet_t;


// Public API ----------------------------------------------------------------------------------------------------------

void scheduler_init(uint8_t *uid);
schedule_role_t scheduler_get_current_role(void);
void scheduler_run(schedule_role_t role);
void scheduler_stop(void);


#endif  // #ifndef __SCHEDULER_HEADER_H__
