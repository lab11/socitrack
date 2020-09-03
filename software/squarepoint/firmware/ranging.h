#ifndef __RANGING_HEADER_H__
#define __RANGING_HEADER_H__

// Includes ------------------------------------------------------------------------------------------------------------

#include "deca_device_api.h"
#include "response.h"

// Defines -------------------------------------------------------------------------------------------------------------

#define RANGE_ERROR_OUT_OF_RANGE                            0x80000001
#define RANGE_ERROR_TOO_FEW_RANGES                          0x80000002
#define RANGE_ERROR_MISC                                    0x8000000F

// Data structures -----------------------------------------------------------------------------------------------------

typedef struct
{
   PROTOCOL_EUI_TYPE responder_eui;
   uint64_t response_tx_timestamp;
   uint64_t response_rx_timestamp;
   uint8_t first_rxd_idx;
   uint64_t first_rxd_toa;
   uint8_t last_rxd_idx;
   uint64_t last_rxd_toa;
   uint16_t TOAs[NUM_RANGING_BROADCASTS];
} ranging_info_t;

typedef struct
{
   uint8_t num_responses, num_ranges;
   uint64_t subsequence_send_times[NUM_RANGING_BROADCASTS];
   ranging_info_t responses[PROTOCOL_MAX_NUM_RESPONDERS + PROTOCOL_MAX_NUM_HYBRIDS];
} ranging_state_t;

// Public functions ----------------------------------------------------------------------------------------------------

uint8_t channel_index_to_channel(uint8_t channel_index);
uint8_t subsequence_number_to_channel_index(uint8_t subsequence_num);
uint8_t subsequence_number_to_channel(uint8_t subsequence_num);
uint8_t subsequence_number_to_antenna(bool is_requester, uint8_t subsequence_num);

void reset_ranging_phase(void);
bool ranging_response_received(PROTOCOL_EUI_TYPE eui);
void add_ranging_request_send_time(uint8_t subsequence_number, uint64_t send_time);
void add_ranging_response(PROTOCOL_EUI_TYPE this_eui, uint64_t dw_raw_timestamp, const response_packet_t* response);
uint8_t perform_ranging(uint8_t *ids_and_ranges, PROTOCOL_EUI_TYPE *expected_devices, uint8_t expected_devices_len);

#endif  // __RANGING_HEADER_H__
