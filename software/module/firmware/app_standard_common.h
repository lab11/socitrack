#ifndef __APP_STANDARD_COMMON_H
#define __APP_STANDARD_COMMON_H

#include "module_conf.h"
#include "system.h"
#include "dw1000.h"
#include "timer.h"
#include "glossy.h"

/******************************************************************************/
// Parameters for the localization and ranging protocol
/******************************************************************************/

// How many of the DW1000 supported UWB channels we are using for ranging
// packets.
#define NUM_RANGING_CHANNELS 3

// It's possible that someday the number of antennas should be configurable
// to support different hardware...
#define NUM_ANTENNAS 3

// Number of packets with unique antenna and channel combinations
#define NUM_UNIQUE_PACKET_CONFIGURATIONS (NUM_RANGING_CHANNELS*NUM_ANTENNAS*NUM_ANTENNAS)

// Use 30 broadcasts from the tag for ranging.
// (3 channels * 3 antennas on tag * 3 antennas on anchor) + 2
// We iterate through the first 2 twice so that we can make sure we made
// contact will all anchors, even if the anchors aren't listening on the
// first channel, plus we don't lose the first two if the anchor was listening
// on the third channel.
#define NUM_RANGING_BROADCASTS ((NUM_RANGING_CHANNELS*NUM_ANTENNAS*NUM_ANTENNAS) + NUM_RANGING_CHANNELS)

// Which channel we use to respond; Index 0 is used in "app_standard_common" and mapped to the first channel (should be == LWB_CHANNEL == 1)
#define RANGING_RESPONSE_CHANNEL_INDEX	0
//#define RANGING_RESPONSE_CHANNEL		LWB_CHANNEL

// Listen for responses from the anchors on different channels
#define NUM_RANGING_LISTENING_WINDOWS 3

// How many slots should be in each listening window for the anchors to respond
// in.
#define NUM_RANGING_LISTENING_SLOTS 20

// How long the slots inside each window should be for the anchors to choose from
#define RANGING_LISTENING_WINDOW_US 10000
#define RANGING_LISTENING_SLOT_US (RANGING_LISTENING_WINDOW_US/NUM_RANGING_LISTENING_SLOTS)

// Maximum number of anchors a tag is willing to hear from
#define MAX_NUM_ANCHOR_RESPONSES 10

// Reasonable constants to rule out unreasonable ranges
#define MIN_VALID_RANGE_MM -1000      // -1 meter
#define MAX_VALID_RANGE_MM (50*1000)  // 50 meters

// How many valid ranges we have to get from the anchor in order to bother
// including it in our calculations for the distance to the tag.
#define MIN_VALID_RANGES_PER_ANCHOR 10

// When the tag is calculating range for each of the anchors given a bunch
// of measurements, these define which percentile of the measurements to use.
// They are split up to facilitate non-floating point math.
// EXAMPLE: N=1, D=10 means take the 10th percentile.
#define RANGE_PERCENTILE_NUMERATOR 1
#define RANGE_PERCENTILE_DENOMENATOR 10


/******************************************************************************/
// Data Structs for packet messages between tags and anchors
/******************************************************************************/

#define MSG_MAX_PACK_LEN	296

// Message types that identify the UWB packets.
#define MSG_TYPE_PP_NOSLOTS_TAG_POLL  0x80
#define MSG_TYPE_PP_NOSLOTS_ANC_FINAL 0x81
#define MSG_TYPE_PP_GLOSSY_SYNC       0x82
#define MSG_TYPE_PP_GLOSSY_SIGNAL  	  0x83

// Used to show whether message is a ranging packet or not
#define MSG_TYPE_CONTROL	0
#define MSG_TYPE_RANGING	1

// Packet the tag broadcasts to all nearby anchors
struct pp_tag_poll  {
	struct ieee154_header_broadcast header;
	uint8_t message_type;                   // Packet type identifier so the anchor knows what it is receiving.
	uint8_t subsequence;                    // Index of which broadcast sequence number this packet is.
	struct ieee154_footer footer;
} __attribute__ ((__packed__));


struct pp_anc_final_init_response {
    uint8_t  init_eui[PROTOCOL_EUI_LEN];
    uint8_t  first_rxd_idx;
    uint64_t first_rxd_toa;
    uint8_t  last_rxd_idx;
    uint64_t last_rxd_toa;
    uint16_t TOAs[NUM_RANGING_BROADCASTS]; // The anchor timestamps of when it received the tag poll messages.
};
// Packet the anchor sends back to the tag.
struct pp_anc_final {
	struct ieee154_header_broadcast header;
	uint8_t  message_type;
    uint8_t  final_antenna;                // The antenna the responder used when sending this packet.
    uint64_t dw_time_sent;                 // The anchor timestamp of when it sent this packet
    uint8_t  init_response_length;
	struct   pp_anc_final_init_response init_responses[PROTOCOL_INIT_SCHED_MAX];
	struct ieee154_footer footer;
} __attribute__ ((__packed__));

#define MSG_PP_ANC_FINAL_PAYLOAD_DEFAULT_LENGTH     11  // 3 * uint8_t + 1 * uint64_t

/******************************************************************************/
// State objects for the standard application
/******************************************************************************/

// Enum for what the module should provide the host.
typedef enum {
	MODULE_REPORT_MODE_RANGES = 0,   // Return just range measurements to anchors
	MODULE_REPORT_MODE_LOCATION = 1  // Determine location and provide location coordinates
} module_report_mode_e;

// Enum for when the TAG should do a ranging event
typedef enum {
	MODULE_UPDATE_MODE_PERIODIC = 0,  // Range at regular intervals
	MODULE_UPDATE_MODE_DEMAND = 1     // Range only when the host instructs
} module_update_mode_e;

// Enum for what role this particular module should do
typedef enum {
	APP_ROLE_INVALID = 0,
    APP_ROLE_INIT_RESP,
    APP_ROLE_INIT_NORESP,
    APP_ROLE_NOINIT_RESP,
    APP_ROLE_NOINIT_NORESP,
    APP_ROLE_UNDECIDED = 255
} module_role_e;

// Keep config settings for a oneway node
typedef struct {
	module_role_e my_role;
	bool		  init_enabled;	// Depending on the role, init and resp are enabled
	bool		  resp_enabled;
	bool		  init_active; // In case they are enabled, the can be in an active state (i.e. currently executing init code)
	bool		  resp_active;
	glossy_role_e my_glossy_role;
	uint8_t 	  my_glossy_master_EUI[EUI_LEN];
	uint8_t       my_EUI[EUI_LEN];
	module_report_mode_e report_mode;
	module_update_mode_e update_mode;
	uint8_t update_rate;
	bool    sleep_mode;
} module_config_t;


/******************************************************************************/
// Public functions
/******************************************************************************/

void             standard_configure (module_config_t* config);
void             standard_start ();
void             standard_stop ();
void             standard_reset ();
void             standard_do_range ();
module_config_t* standard_get_config ();
module_role_e    standard_get_role();
void             standard_set_role(module_role_e role);
bool			 standard_is_init_enabled();
bool			 standard_is_init_active();
void 			 standard_set_init_active(bool init_active);
bool			 standard_is_resp_enabled();
bool			 standard_is_resp_active();
void 			 standard_set_resp_active(bool resp_active);
uint8_t *        standard_get_EUI();

uint8_t  standard_subsequence_number_to_antenna (bool resp_active, uint8_t subseq_num);
void     standard_set_ranging_broadcast_subsequence_settings (bool resp_active, uint8_t subseq_num);
void     standard_set_ranging_response_settings (bool init_active, uint8_t antenna_num);
uint8_t  standard_get_ss_index_from_settings (uint8_t anchor_antenna_index, uint8_t channel_index);
uint64_t standard_get_txdelay_from_subsequence (uint8_t subseq_num);
uint64_t standard_get_rxdelay_from_subsequence (uint8_t subseq_num);
uint64_t standard_get_txdelay_from_ranging_response_channel (uint8_t channel_index);
uint64_t standard_get_rxdelay_from_ranging_response_channel (uint8_t channel_index);

// TX/RX - defined inside app_standard_init and app_standard_resp
void clear_frame_event();
void init_txcallback  (const dwt_cb_data_t *txd);
void init_rxcallback  (const dwt_cb_data_t *rxd, uint8_t * buf, uint64_t dw_rx_timestamp);
void resp_txcallback  (const dwt_cb_data_t *txd);
void resp_rxcallback  (const dwt_cb_data_t *rxd, uint8_t * buf, uint64_t dw_rx_timestamp);

// Helper functions
void debug_print_tx(uint32_t length);
void helper_print_EUI(uint8_t * address);

#endif
