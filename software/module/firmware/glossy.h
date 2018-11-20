#ifndef __GLOSSY_H
#define __GLOSSY_H

#include <stdint.h>

#include "deca_device_api.h"

#include "firmware.h"
#include "module_conf.h"

// DEFINES -------------------------------------------------------------------------------------------------------------

#define LWB_CHANNEL               1
#define LWB_ANTENNA               0

#define LWB_SLOT_US               1e4

#define LWB_SLOTS_PER_RANGE       10
#define LWB_RESPONSES_PER_SLOT    4

#define MAX_SCHED_TAGS            10
#define GLOSSY_MAX_DEPTH          5
#define TAG_SCHED_TIMEOUT         10

#ifdef GLOSSY_PER_TEST
#define GLOSSY_UPDATE_INTERVAL_US 1e4
#else
#define GLOSSY_UPDATE_INTERVAL_US 1e6
#endif

#define GLOSSY_FLOOD_TIMESLOT_US  2e3

#define GLOSSY_UPDATE_INTERVAL_DW (DW_DELAY_FROM_US(GLOSSY_UPDATE_INTERVAL_US) & 0xFFFFFFFE)

#define GLOSSY_MASTER_TIMEOUT_PERIOD   (10 * (GLOSSY_UPDATE_INTERVAL_US / LWB_SLOT_US))
#define GLOSSY_MASTER_TAKEOVER_PERIOD  ( 5 * (GLOSSY_UPDATE_INTERVAL_US / LWB_SLOT_US))

#define GLOSSY_SCHEDULE_RETRY_SLACK_US 2000
#define GLOSSY_SCHEDULE_RECV_SLACK_US  1000
#define GLOSSY_SCHEDULE_RECV_SLACK_DW  (DW_DELAY_FROM_US(GLOSSY_SCHEDULE_RECV_SLACK_US) & 0xFFFFFFFE)

// DATA STRUCTURES -----------------------------------------------------------------------------------------------------

typedef enum {
	GLOSSY_SLAVE  = 0,
	GLOSSY_MASTER = 1
} glossy_role_e;

typedef enum {
    SIGNAL_UNDEFINED,
    SCHED_REQUEST_INIT,         // Request to be scheduled as INIT
    SCHED_REQUEST_RESP,         // Request to be scheduled as RESP
    SCHED_REQUEST_HYBRID,       // Request to be scheduled as HYBRID
    DESCHED_REQUEST_INIT,       // Request to be descheduled as INIT
    DESCHED_REQUEST_RESP,       // Request to be descheduled as RESP
    DESCHED_REQUEST_HYBRID,     // Request to be descheduled as HYBRID
    COMPLAIN_NO_MASTER,         // Inform that no master was observed
    COMPLAIN_SLOT_UNUSED_INIT,  // Inform that INIT did not use its slot
    COMPLAIN_SLOT_UNUSED_RESP   // Inform that RESP did not use its slot
} lwb_signal_e;

struct pp_sched_flood {
	struct ieee154_header_broadcast header;
	uint8_t  message_type;
	uint8_t  round_length;
#ifdef PROTOCOL_ENABLE_GLOBAL_TIMESTAMPS
	uint32_t epoch_time;
#endif
	uint8_t  init_schedule_length;
	uint8_t  resp_schedule_length;
	uint8_t  eui_array[PROTOCOL_INIT_SCHED_MAX + PROTOCOL_RESP_SCHED_MAX][PROTOCOL_EUI_LEN]; // The array MUST be the last member, as it will be only partially sent ( anything beyond index "init_sched_length + resp_sched_length" is invalid)
	struct ieee154_footer footer;
} __attribute__ ((__packed__));

#ifdef PROTOCOL_ENABLE_GLOBAL_TIMESTAMPS
#define MSG_PP_SCHED_FLOOD_PAYLOAD_DEFAULT_LENGTH   8 // 4 * uint8_t + 1 * uint32_t
#else
#define MSG_PP_SCHED_FLOOD_PAYLOAD_DEFAULT_LENGTH   4 // 4 * uint8_t
#endif

struct pp_signal_flood {
	struct ieee154_header_broadcast header;
	uint8_t message_type;
	uint8_t info_type; // enum "lwb_signal_e"
	uint8_t device_eui[PROTOCOL_EUI_LEN];
#ifdef GLOSSY_ANCHOR_SYNC_TEST
	uint64_t turnaround_time;
	double clock_offset_ppm;
	uint8_t sync_depth;
	int8_t xtal_trim;
#endif
	struct ieee154_footer footer;
} __attribute__ ((__packed__));

// PUBLIC FUNCTIONS ----------------------------------------------------------------------------------------------------

void glossy_init(glossy_role_e role, uint8_t config_master_eui);
void glossy_start();
void glossy_deschedule();
void lwb_set_sched_request(bool sched_en);
void glossy_process_rxcallback(uint64_t dw_timestamp, uint8_t *buf);
bool glossy_process_txcallback();

// Helpers

//	 	 glossy_get_resp_listening_slots_a -> static
uint8_t  glossy_get_resp_listening_slots_b();
void     glossy_reset_counter_offset();
void     glossy_set_epoch_time(uint32_t epoch);
uint32_t glossy_get_epoch_time();

#endif

