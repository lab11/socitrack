#include <stddef.h>
#include <string.h>

#include "dw1000.h"
#include "deca_device_api.h"
#include "deca_regs.h"

#include "timer.h"
#include "delay.h"
#include "SEGGER_RTT.h"

#include "host_interface.h"
#include "firmware.h"

#include "app_calibration.h"

// APPLICATION STATE ---------------------------------------------------------------------------------------------------

// Configure the RF channels to use. This is just a mapping from 0..2 to
// the actual RF channel numbers the DW1000 uses. It MUST be the same as the ones used in app_standard_common
static const uint8_t channel_index_to_channel_rf_number[CALIB_NUM_CHANNELS] = {
        1, 4, 3
};

// All of the configuration passed to us by the host for how this application should operate.
static calibration_config_t _config;

// Scratchspace for calibration
static calibration_scratchspace_struct _app_scratchspace;


/******************************************************************************/
// Function prototypes
/******************************************************************************/

static void calibration_txcallback (const dwt_cb_data_t *txd);
static void calibration_rxcallback (const dwt_cb_data_t *rxd);

static void setup_round_antenna_channel (uint32_t round_num);
static void calib_start_round ();
static void send_calibration_pkt (uint8_t message_type, uint8_t packet_num);
static void finish ();

/******************************************************************************/
// Application API for main()
/******************************************************************************/

static void calibration_init () {

    // Make sure the SPI speed is slow for this function
    dw1000_spi_slow();

    // Setup callbacks to this TAG
    dwt_setcallbacks(calibration_txcallback, calibration_rxcallback, calibration_rxcallback, calibration_rxcallback);

    // Make sure the radio starts off
    dwt_forcetrxoff();

    // Allow data and ack frames
    dwt_enableframefilter(DWT_FF_DATA_EN | DWT_FF_ACK_EN);

    // Setup parameters of how the radio should work
    dwt_setdblrxbuffmode(FALSE);
    dwt_setrxtimeout(FALSE);

    // Put source EUI in the pp_tag_poll packet
    dw1000_read_eui(_app_scratchspace.pp_calibration_pkt.header.sourceAddr);
    // FIXME: Use actual EUI
    _app_scratchspace.pp_calibration_pkt.header.sourceAddr[0] = APP_EUI_FIRST_BYTE;

    // Make SPI fast now that everything has been setup
    dw1000_spi_fast();
}

// Setup the calibration
void calibration_configure (calibration_config_t* config) {
    dw1000_err_e err;

    debug_msg("Initiating calibration as node ");
    debug_msg_uint(config->index);
    debug_msg("\n");

    // Save the settings
    memcpy(&_config, config, sizeof(calibration_config_t));

    // Set scratchspace to known zeros
    memset(&_app_scratchspace, 0, sizeof(_app_scratchspace));

    // Initialize scratchspace
    _app_scratchspace.round_num      = UINT32_MAX;
    _app_scratchspace.timeout_firing = 0;
    _app_scratchspace.init_received  = FALSE;
    _app_scratchspace.dw_slack_delay_multiplier = 5;

    _app_scratchspace.pp_calibration_pkt = (struct pp_calibration_msg) {
            .header = {
                    .frameCtrl = {
                            0x41, // FCF[0]: data frame, panid compression
                            0xC8  // FCF[1]: ext source address, compressed destination
                    },
                    .seqNum = 0,
                    .panID = {
                            MODULE_PANID & 0xFF,
                            MODULE_PANID >> 8
                    },
                    .destAddr = {
                            0xFF, // Destination address: broadcast
                            0xFF
                    },
                    .sourceAddr = { 0 }     // Source (blank for now)
            },
            // PACKET BODY
            .message_type = MSG_TYPE_PP_CALIBRATION_INIT,
            .round_num    = 0,
            .num          = 0
    };

    // Get a timer
    _app_scratchspace.app_timer = timer_init();

    // Make sure the DW1000 is awake before trying to do anything.
    err = dw1000_wakeup();
    if (err == DW1000_WAKEUP_SUCCESS) {
        // Going to init anyway
    } else if (err) {
        // Failed to wakeup
        module_reset();
        return;
    }

    // Set all of the calibration init settings
    calibration_init();
}

void calibration_start () {

    // Start at round 0, but we increment at the start
    _app_scratchspace.round_num = UINT32_MAX;

    // Setup channel and antenna settings for round 0
    setup_round_antenna_channel(0);

    // Prepare to receive inititial frame
    _app_scratchspace.init_received = FALSE;

    // Determine what to do during calibration based on what index we are
    if (_config.index == CALIBRATION_MASTER_NODE) {
        // We are the master and start things.
        timer_start(_app_scratchspace.app_timer, CALIBRATION_ROUND_PERIOD_US, calib_start_round);

    } else if (_config.index > 0 && _config.index <= 2) {
        // Other nodes just enter RX mode
        dwt_rxenable(0);
    } else {
        debug_msg("ERROR: Incorrect index ");
        debug_msg_uint(_config.index);
        debug_msg("\n");
    }
}

void calibration_stop () {

    timer_stop(_app_scratchspace.app_timer);

    dwt_forcetrxoff();
}

void calibration_reset () {

    calibration_init();
}

/******************************************************************************/
// Calibration action functions
/******************************************************************************/

// We keep settings the same for three rounds in a row to get values for
// each node before switching things up.
static void setup_round_antenna_channel (uint32_t round_num) {

    // Turn transceiver off first
    dwt_forcetrxoff();

    // This rotates the fastest
    uint8_t antenna =  (round_num / CALIBRATION_NUM_NODES) % CALIB_NUM_ANTENNAS;
    uint8_t channel = ((round_num / CALIBRATION_NUM_NODES) / CALIB_NUM_ANTENNAS) % CALIB_NUM_CHANNELS;

    dw1000_choose_antenna(antenna);
    dw1000_update_channel(channel_index_to_channel_rf_number[channel]);
}

// Timer callback that marks the start of each round
static void calib_start_round () {

    //debug_msg("Starting a calibration round\n");

    // Increment the round number
    if (_app_scratchspace.round_num == UINT32_MAX) {
        _app_scratchspace.round_num = 0; // Unknown behaviour depending on HW and compiler, so we just take care of this case ourselves
    } else {
        _app_scratchspace.round_num++;
    }

    // Before the INIT packet, use the default settings
    setup_round_antenna_channel(0);

    // Send a packet to announce the start of the a calibration round.
    send_calibration_pkt(MSG_TYPE_PP_CALIBRATION_INIT, 0);
}

// Send a packet
static void send_calibration_pkt (uint8_t message_type, uint8_t packet_num) {
    int ret;

    debug_msg("Sending calibration packet: type ");
    debug_msg_uint(message_type);
    debug_msg(", number ");
    debug_msg_uint(packet_num);
    debug_msg("\n");

    // Record the packet length to send to DW1000
    uint16_t tx_len = sizeof(struct pp_calibration_msg);

    // Setup what needs to change in the outgoing packet
    _app_scratchspace.pp_calibration_pkt.header.seqNum++;
    _app_scratchspace.pp_calibration_pkt.message_type = message_type;
    _app_scratchspace.pp_calibration_pkt.round_num    = _app_scratchspace.round_num;
    _app_scratchspace.pp_calibration_pkt.num          = packet_num;

    // Make sure we're out of RX mode before attempting to transmit
    dwt_forcetrxoff();

    // Tell the DW1000 about the packet
    dwt_writetxfctrl(tx_len, 0, 1);

    // Setup the time the packet will go out at, and save that timestamp
    uint32_t delay_time = dwt_readsystimestamphi32() + (DW_DELAY_FROM_PKT_LEN(tx_len) * _app_scratchspace.dw_slack_delay_multiplier);

    // uint32_t delay_time = dwt_readsystimestamphi32() + (APP_US_TO_DEVICETIMEU32(1000)>>8);
    delay_time &= 0xFFFFFFFE; // Make sure last bit is zero
    dwt_setdelayedtrxtime(delay_time);

    // Store timing for calculations
    _app_scratchspace.calibration_timing[packet_num] = ((uint64_t) delay_time) << 8;

    // Write the data
    dwt_writetxdata(tx_len, (uint8_t*) &(_app_scratchspace.pp_calibration_pkt), 0);

    // Start the transmission and enter RX mode
    //dwt_setrxaftertxdelay(1); // us

    ret = dwt_starttx(DWT_START_TX_DELAYED);
    if (ret != DWT_SUCCESS) {
        // If we could not send this delayed packet, try extending the delay period next time.
        debug_msg("WARNING: Could not send successfully!\n");
        _app_scratchspace.dw_slack_delay_multiplier++;
    }

    // MP bug - TX antenna delay needs reprogramming as it is not preserved
    dwt_settxantennadelay(DW1000_ANTENNA_DELAY_TX);
}

// Something went wrong (packet dropped most likely, or
// one of the nodes is not configured yet) and our timeout fired. This
// resets us so we can be ready for the next round.
// This timeout does not apply to node 0.
static void round_timeout () {

    if (_app_scratchspace.timeout_firing == 0) {
        // Skip the immediate callback which is triggered when the timer starts
        _app_scratchspace.timeout_firing = 1;
    } else {

        debug_msg("WARNING: Timed out, try to enter again next round\n");
        timer_stop(_app_scratchspace.app_timer);

        // Reset state
        _app_scratchspace.init_received = FALSE;
        dwt_forcetrxoff();

        // Wait for next round
        setup_round_antenna_channel(0);
        dwt_rxenable(0);
    }
}

// After we have sent/received all of the packets, tell the host about our timestamps.
static void finish () {

    if (_config.index != CALIBRATION_MASTER_NODE) {
        // Stop the timeout timer
        timer_stop(_app_scratchspace.app_timer);
    }

    // Notify host if we are node 0 or we got the init() packet AND
    // we are not the round starting node (that node doesn't have any useful timestamps)
    if ((_app_scratchspace.init_received || _config.index == CALIBRATION_MASTER_NODE) &&
        !CALIBRATION_ROUND_STARTED_BY_ME(_app_scratchspace.round_num, _config.index)) {

        debug_msg("Sending packet for round ");
        debug_msg_uint(_app_scratchspace.round_num);
        debug_msg(" to host\n");

        uint8_t offset = 0;

        // Round number - calibration_response_buf[0-3]
        memcpy(_app_scratchspace.calibration_response_buf, &(_app_scratchspace.round_num), sizeof(uint32_t));
        offset += 4; // sizeof(uint32_t)

        // Calibration timing - calibration_response_buf[4-8]
        _app_scratchspace.calibration_response_buf[offset + 0] = (_app_scratchspace.calibration_timing[0] >>  0) & 0xFF;
        _app_scratchspace.calibration_response_buf[offset + 1] = (_app_scratchspace.calibration_timing[0] >>  8) & 0xFF;
        _app_scratchspace.calibration_response_buf[offset + 2] = (_app_scratchspace.calibration_timing[0] >> 16) & 0xFF;
        _app_scratchspace.calibration_response_buf[offset + 3] = (_app_scratchspace.calibration_timing[0] >> 24) & 0xFF;
        _app_scratchspace.calibration_response_buf[offset + 4] = (_app_scratchspace.calibration_timing[0] >> 32) & 0xFF;
        offset += 5;

        uint32_t diff;

        // Difference X1-X0 - calibration_response_buf[9-12]
        diff = (uint32_t) (_app_scratchspace.calibration_timing[1] - _app_scratchspace.calibration_timing[0]);
        memcpy(_app_scratchspace.calibration_response_buf + offset, &diff, sizeof(uint32_t));
        offset += 4;

        // Difference X2-X1 - calibration_response_buf[13-16]
        diff = (uint32_t) (_app_scratchspace.calibration_timing[2] - _app_scratchspace.calibration_timing[1]);
        memcpy(_app_scratchspace.calibration_response_buf + offset, &diff, sizeof(uint32_t));
        offset += 4;

        // Send packet
        host_interface_notify_calibration(_app_scratchspace.calibration_response_buf, offset);
    }

    // Reset this
    _app_scratchspace.init_received = FALSE;

    // Now that this round is over, start the next one
    setup_round_antenna_channel(0);

    if (_config.index != CALIBRATION_MASTER_NODE) {
        dwt_rxenable(0);
    }
}

/******************************************************************************/
// TX/RX callbacks
/******************************************************************************/

// Use this callback to start the next cycle in the round
static void calibration_txcallback (const dwt_cb_data_t *txd) {

    if (txd->status & SYS_STATUS_TXFRS) {
        /*debug_msg("Tx -> type ");
        debug_msg_uint(_app_scratchspace.pp_calibration_pkt.message_type);
        debug_msg("\n");*/
    } else {
        debug_msg("ERROR: Failed in sending packet, status: ");
        debug_msg_uint(txd->status);
        debug_msg("\n");
    }

    // If we are the Master node and just sent the init, we then need to set ourselves for the correct next position
    if (_app_scratchspace.pp_calibration_pkt.message_type == MSG_TYPE_PP_CALIBRATION_INIT) {

        // For the next round, setup the correct channel
        setup_round_antenna_channel(_app_scratchspace.round_num);

        if (CALIBRATION_ROUND_STARTED_BY_ME(_app_scratchspace.round_num, _config.index)) {
            // We just sent the "get everybody on the same page packet".
            // Now start the actual cycle because it is our turn to send the first packet.
            // Delay a bit to give the other nodes a chance to download and process.
            mDelay(2);

            // Send on the next ranging cycle in this round
            send_calibration_pkt(MSG_TYPE_PP_CALIBRATION_MSG, 0);

        } else {
            // Prepare to receive the first packet of the round
            debug_msg("Sent Init packet for round ");
            debug_msg_uint(_app_scratchspace.round_num);
            debug_msg("\n");

            // Sent the start of round message
            dwt_rxenable(0);
        }

    } else if (CALIBRATION_ROUND_FOR_ME(_app_scratchspace.round_num, _config.index)) {

        if (_app_scratchspace.pp_calibration_pkt.num == 1) {

            // We sent the first response, now send another
            mDelay(2);

            // Send on the next ranging cycle in this round
            send_calibration_pkt(MSG_TYPE_PP_CALIBRATION_MSG, 2);

        } else if (_app_scratchspace.pp_calibration_pkt.num == 2) {
            // We have sent enough packets, call this a day.
            finish();

        } else {
            debug_msg("ERROR: Sent incorrect number of packets ");
            debug_msg_uint(_app_scratchspace.pp_calibration_pkt.num);
            debug_msg("\n");
        }

    } else if (CALIBRATION_ROUND_STARTED_BY_ME(_app_scratchspace.round_num, _config.index)) {

        if (_app_scratchspace.pp_calibration_pkt.num == 0) {
            // We just started the round and are done for now... just wait for the next round
            finish();
        }

    } else {
        debug_msg("ERROR: Incorrect Tx; sent message of type ");
        debug_msg_uint(_app_scratchspace.pp_calibration_pkt.message_type);
        debug_msg(" in round ");
        debug_msg_uint(_app_scratchspace.round_num);
        debug_msg("\n");
    }
}

// Handle when we receive packets
static void calibration_rxcallback (const dwt_cb_data_t *rxd) {

    if (rxd->status & SYS_STATUS_RXFCG) {

        /*debug_msg("Rx -> length ");
        debug_msg_uint(rxd->datalength);
        debug_msg("\n");*/

        // Read in parameters of this packet reception
        uint64_t dw_rx_timestamp;
        uint8_t  buf[CALIBRATION_MAX_RX_PKT_LEN];
        uint8_t  message_type;

        // Get the received time of this packet first
        dwt_readrxtimestamp(buf);
        dw_rx_timestamp = DW_TIMESTAMP_TO_UINT64(buf);

        // Get the actual packet bytes
        dwt_readrxdata(buf, MIN(CALIBRATION_MAX_RX_PKT_LEN, rxd->datalength), 0);

        // We process based on the first byte in the packet
        message_type = buf[offsetof(struct pp_calibration_msg, message_type)];

        // Packet
        struct pp_calibration_msg* rx_start_pkt = (struct pp_calibration_msg*) buf;

        if (message_type == MSG_TYPE_PP_CALIBRATION_INIT) {

            debug_msg("Received Init packet for round ");
            debug_msg_uint(rx_start_pkt->round_num);
            debug_msg("\n");

            // Got the start of round message
            // Set the round number, and configure for that round
            _app_scratchspace.round_num = rx_start_pkt->round_num;
            setup_round_antenna_channel(_app_scratchspace.round_num);

            // Note that we got the init() packet.
            // This allows us to only report this round if we setup the antenna and channel correctly.
            _app_scratchspace.init_received = TRUE;

            // Set a timeout timer. If everything doesn't complete in a certain amount of time, go back to initial state.
            if (_config.index != CALIBRATION_MASTER_NODE) {
                _app_scratchspace.timeout_firing = 0;
                timer_start(_app_scratchspace.app_timer, CALIBRATION_ROUND_TIMEOUT_US, round_timeout);
            } else {
                // Just make sure that for some weird reason (aka it should never happen) that node 0 zero does not do this.
                debug_msg("ERROR: Received Init Packet as Master; this should not happen!\n");
            }

            // Decide which node should send packet number 0
            if (CALIBRATION_ROUND_STARTED_BY_ME(_app_scratchspace.round_num, _config.index)) {

                // Delay to make sure all other nodes are ready to receive this packet.
                mDelay(2);

                // This is us! Let's do it
                debug_msg("Starting the new round ");
                debug_msg_uint(_app_scratchspace.round_num);
                debug_msg("\n");

                send_calibration_pkt(MSG_TYPE_PP_CALIBRATION_MSG, 0);
            }

        } else if (message_type == MSG_TYPE_PP_CALIBRATION_MSG) {
            uint8_t packet_num = rx_start_pkt->num;

            debug_msg("Received calibration packet: type ");
            debug_msg_uint(message_type);
            debug_msg(", number ");
            debug_msg_uint(packet_num);
            debug_msg("\n");

            // Store timestamps.
            if (packet_num < 3) {
                _app_scratchspace.calibration_timing[packet_num] = dw_rx_timestamp;
            } else {
                // Uhh, this is bad
                _app_scratchspace.init_received = FALSE;
                debug_msg("ERROR: Invalid packet number ");
                debug_msg_uint(packet_num);
                debug_msg("\n");
            }

            if (CALIBRATION_ROUND_FOR_ME(_app_scratchspace.round_num, _config.index)) {

                if (packet_num == 0) {
                    // After the first packet, based on the round number the node to be calibrated sends the next two packets.
                    send_calibration_pkt(MSG_TYPE_PP_CALIBRATION_MSG, 1);
                } else {
                    debug_msg("ERROR: Incorrect packet number ");
                    debug_msg_uint(packet_num);
                    debug_msg("\n");
                }

            } else if ( (packet_num == 2) &&
                        !CALIBRATION_ROUND_STARTED_BY_ME(_app_scratchspace.round_num, _config.index) ) {
                // This is the last packet, notify the host of our findings - in some cases, the message might have been received by the starting node, which we must prevent (as it does not notify the host)
                finish();
                return;
            }

        } else {
            debug_msg("ERROR: Received invalid message type: ");
            debug_msg_uint(message_type);
            debug_msg("\n");
        }
    } else {

        if ( (rxd->status & SYS_STATUS_ALL_RX_ERR) ||
             (rxd->status & SYS_STATUS_ALL_RX_TO )   ) {
            debug_msg("WARNING: Rx error, status: ");
            debug_msg_int((uint32_t)rxd->status);
            debug_msg("\n");
        } else {
            debug_msg("ERROR: Unknown error!");
        }

        setup_round_antenna_channel(0);
    }

    // Listen for the next packet
    dwt_rxenable(0);
}