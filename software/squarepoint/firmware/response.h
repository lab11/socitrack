#ifndef __RESPONSE_HEADER_H__
#define __RESPONSE_HEADER_H__

// Includes ------------------------------------------------------------------------------------------------------------

#include "configuration.h"
#include "dw1000.h"

// Defines -------------------------------------------------------------------------------------------------------------

#define RESPONSE_PACKET_PAYLOAD_LENGTH              11  // 3 * uint8_t + 1 * uint64_t
#define RESPONSE_RESEND_INTERVAL_US                 3500

// Data structures -----------------------------------------------------------------------------------------------------

typedef struct __attribute__ ((__packed__))
{
   PROTOCOL_EUI_TYPE requester_eui;
   uint8_t first_rxd_idx;
   uint64_t first_rxd_toa;
   uint8_t last_rxd_idx;
   uint64_t last_rxd_toa;
   uint16_t TOAs[NUM_RANGING_BROADCASTS];
} request_info_t;

typedef struct __attribute__ ((__packed__))
{
   struct ieee154_header_broadcast header;
   uint8_t message_type;
   uint64_t dw_time_sent;
   uint8_t requests_count;
   request_info_t requests[PROTOCOL_MAX_NUM_REQUESTERS + PROTOCOL_MAX_NUM_HYBRIDS];
   struct ieee154_footer footer;
} response_packet_t;

typedef struct
{
   bool sending_response;
   uint32_t dw_transmit_start_time;
   uint8_t current_subsequence_number;
   uint8_t num_packets_received_per_antenna[NUM_ANTENNAS];
   PROTOCOL_EUI_TYPE expected_response_eui;
} response_state_t;

// Public functions ----------------------------------------------------------------------------------------------------

void initialize_response(uint8_t *src_address);
void reset_response_phase(void);
uint8_t prepare_for_next_subsequence(void);
bool handle_incoming_request(uint64_t dw_rx_timestamp, PROTOCOL_EUI_TYPE source_eui, uint8_t subsequence_number, bool new_device);
void ranging_response_txcallback(bool transmission_success);
void receive_ranging_response(PROTOCOL_EUI_TYPE this_eui, uint64_t dw_raw_timestamp, const response_packet_t* response);
void send_ranging_response(uint32_t dw_timestamp_to_begin);
void receive_ranging_responses(PROTOCOL_EUI_TYPE expected_eui);

#endif  // __RESPONSE_HEADER_H__
