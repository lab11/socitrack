#ifndef __SCHEDULER_HEADER_H__
#define __SCHEDULER_HEADER_H__

// Includes ------------------------------------------------------------------------------------------------------------

#include "configuration.h"
#include "dw1000.h"

// Defines -------------------------------------------------------------------------------------------------------------

#define MIN_NETWORK_SEARCH_TIME_US       3000000
#define MAX_NETWORK_SEARCH_TIME_US       7000000
#define MAXIMUM_NUM_RXTX_FAILURES        2
#define MIN_PACKETS_FOR_NONEMPTY_ROUND   5
#define DO_NOT_CHANGE_FLAG               UINT8_MAX
#define SCHEDULE_XMIT_CHANNEL            1
#define SCHEDULE_XMIT_ANTENNA            0
#define SCHEDULE_XMIT_TIME_OFFSET_US     2000
#define SCHEDULE_PACKET_PAYLOAD_LENGTH   (13 + (2*PROTOCOL_EUI_SIZE))           // 5 * uint8_t + 2 * uint32_t + 2 * EUI
#define RESULTS_PACKET_PAYLOAD_LENGTH    (2 + PROTOCOL_EUI_SIZE)                // 2 * uint8_t + EUI
#define PACKET_SINGLE_RESULT_LENGTH      (PROTOCOL_EUI_SIZE + sizeof(int32_t))  // EUI + Range
#define RANGING_REQUEST_TIME_OFFSET_US   2000
#define RANGING_RESPONSE_TIME_OFFSET_US  2000
#define SECONDS_PER_YEAR                 60*60*24*365

// Data structures -----------------------------------------------------------------------------------------------------

typedef enum { UNKNOWN = 0, SCHEDULER, BACKUP_SCHEDULER, PARTICIPANT } schedule_role_t;
typedef enum { UNASSIGNED = 0, HYBRID, REQUESTER, RESPONDER, SUPPORTER } device_role_t;

typedef struct
{
   device_role_t device_role;
   schedule_role_t scheduler_role;
   bool hybrids_perform_all_rangings;
   bool radio_sleep_while_passive, radio_wakeup_from_host;
   uint32_t startup_timestamp;
   PROTOCOL_EUI_TYPE EUI;
} app_config_t;

typedef enum
{
   BACKUP_SCHEDULE_PHASE,
   RANGE_REQUEST_PHASE,
   RANGE_RESPONSE_PHASE,
   SHARED_BUS_PHASE,
   MASTER_SCHEDULE_PHASE,
   RANGING_PHASE,
   RESULTS_PHASE,
   ERROR_CORRECTION_PHASE,
   UNSCHEDULED_TIME_PHASE
} scheduler_phase_t;

typedef enum
{
   CONTROL_PACKET_UNDEFINED,
   CONTROL_ADD_REQUESTER,
   CONTROL_ADD_RESPONDER,
   CONTROL_ADD_HYBRID,
   CONTROL_REMOVE_REQUESTER,
   CONTROL_REMOVE_RESPONDER,
   CONTROL_REMOVE_HYBRID
} control_packet_type;

typedef enum
{
   RANGE_REQUEST_PACKET = 0x80,
   RANGE_RESPONSE_PACKET = 0x81,
   SCHEDULE_PACKET = 0x82,
   CONTROL_PACKET = 0x83,
   SCHEDULE_SYNC_PACKET = 0x84,
   RESULTS_PACKET = 0x85
} packet_type;

typedef struct __attribute__ ((__packed__))
{
   struct ieee154_header_broadcast header;
   uint8_t message_type;    // enum packet_type
   uint32_t epoch_time_unix;
   PROTOCOL_EUI_TYPE scheduler_eui, backup_scheduler_eui;
   uint8_t request_schedule_length;
   uint8_t hybrid_schedule_length;
   uint8_t response_schedule_length;
   PROTOCOL_EUI_TYPE eui_array[3 * PROTOCOL_MAX_NUM_DEVICES_PER_TYPE];
   struct ieee154_footer footer;
} schedule_packet_t;

typedef struct __attribute__ ((__packed__))
{
   struct ieee154_header_broadcast header;
   uint8_t message_type;    // enum packet_type
   uint8_t packet_type;     // enum control_packet_type
   PROTOCOL_EUI_TYPE device_eui;
   struct ieee154_footer footer;
} control_packet_t;

typedef struct __attribute__ ((__packed__))
{
   struct ieee154_header_broadcast header;
   uint8_t message_type;    // enum packet_type
   uint8_t results_length;
   uint8_t results[(2 * PROTOCOL_MAX_NUM_DEVICES_PER_TYPE) * (PROTOCOL_EUI_SIZE + sizeof(int32_t))];
   struct ieee154_footer footer;
} results_packet_t;

// Public functions ----------------------------------------------------------------------------------------------------

void scheduler_set_timestamp(uint32_t timestamp);
bool scheduler_verify_config(app_config_t *config);
bool scheduler_configure(app_config_t *config);
bool scheduler_start(void);
void scheduler_stop(void);

#endif // __SCHEDULER_HEADER_H__
