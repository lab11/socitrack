#ifndef __REQUEST_HEADER_H__
#define __REQUEST_HEADER_H__

// Includes ------------------------------------------------------------------------------------------------------------

#include "configuration.h"
#include "timer.h"

// Data structures -----------------------------------------------------------------------------------------------------

typedef struct __attribute__ ((__packed__))
{
   struct ieee154_header_broadcast header;
   uint8_t message_type, subsequence_number;
   struct ieee154_footer footer;
} request_packet_t;

typedef struct
{
   bool sending_request, listening_for_requests, receiving_request;
   uint8_t subsequence_num;
   uint32_t dw_transmit_start_time;
   PROTOCOL_EUI_TYPE expected_request_eui;
} request_state_t;

// Public functions ----------------------------------------------------------------------------------------------------

void initialize_request(stm_timer_t *ranging_timer, uint8_t *src_address);
void reset_request_phase(void);
void ranging_request_txcallback(bool transmission_success);
void receive_ranging_request(uint64_t dw_raw_timestamp, const request_packet_t* request);
void send_ranging_request(uint32_t dw_timestamp_to_begin);
void receive_ranging_requests(PROTOCOL_EUI_TYPE expected_eui);

#endif  // __REQUEST_HEADER_H__
