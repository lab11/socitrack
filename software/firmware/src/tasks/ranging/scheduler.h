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
   RANGING_RX_TIMEOUT = 0b00010000,
   RANGING_BEGIN = 0b00100000,
   RANGING_RX_ERROR = 0b01000000
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


typedef enum
{
   SCHEDULER_EVENT_SCHEDULE_HEARD,           // a schedule copy was decoded, with its sequence number
   SCHEDULER_EVENT_SCHEDULE_RESEND_FAILED,   // a schedule copy could not be armed in time, with its sequence number
   SCHEDULER_EVENT_JOIN_REQUEST_SENT,        // this unscheduled device asked to join
   SCHEDULER_EVENT_JOIN_REQUEST_HEARD        // the master's join window heard a request, with the requester's address
} scheduler_event_t;


// Public API ----------------------------------------------------------------------------------------------------------

#if DIAGNOSTIC_BUILD
void scheduler_note_event(scheduler_event_t event, uint32_t value);
#else
#define scheduler_note_event(event, value) do {} while (0)
#endif

void scheduler_init(experiment_details_t *details);
schedule_role_t scheduler_get_current_role(void);
void scheduler_reload_experiment_details(void);
const experiment_details_t* scheduler_get_experiment_details(void);
void scheduler_get_round_counts(uint32_t *scheduled, uint32_t *with_ranges);
bool scheduler_master_eligible(void);
uint8_t scheduler_get_master_cycle_failures(void);
void scheduler_note_rx_arm_failure(scheduler_phase_t phase, uint32_t slot, uint32_t schedule_size, uint32_t deadline_us);
void scheduler_run(schedule_role_t role);
void scheduler_stop(void);


#endif  // #ifndef __SCHEDULER_HEADER_H__
