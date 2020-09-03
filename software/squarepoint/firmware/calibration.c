#include <stddef.h>
#include <string.h>
#include "calibration.h"
#include "configuration.h"
#include "deca_device_api.h"
#include "deca_regs.h"
#include "delay.h"
#include "host_interface.h"
#include "SEGGER_RTT.h"
#include "timer.h"

// Application state ---------------------------------------------------------------------------------------------------

static const uint8_t channel_index_to_channel_rf_number[CALIBRATION_NUM_CHANNELS] = { 1, 4, 3 };
static bool _is_running = FALSE, _is_configured = FALSE;
static calibration_config_t _config;
static calibration_state_t _state;
static stm_timer_t *_calibration_timer = NULL;
static calibration_packet_t _calibration_packet;
static uint8_t _read_buffer[CALIBRATION_MAX_PACKET_LEN];
static uint8_t _response_buffer[CALIBRATION_MAX_PACKET_LEN];

// Private helper functions --------------------------------------------------------------------------------------------

static void send_calibration_packet(uint8_t message_type, uint8_t packet_num)
{
   // Display which calibration packet is currently being sent
   debug_msg("INFO: Sending calibration packet: type ");
   debug_msg_uint(message_type);
   debug_msg(", number ");
   debug_msg_uint(packet_num);
   debug_msg("\n");

   // Setup the calibration packet
   const uint16_t packet_length = sizeof(calibration_packet_t);
   ++_calibration_packet.header.seqNum;
   _calibration_packet.message_type = message_type;
   _calibration_packet.round_num = _state.round_num;
   _calibration_packet.packet_index = packet_num;

   // Setup the DW1000 radio and transmission delay time
   dwt_forcetrxoff();
   dwt_writetxfctrl(packet_length, 0, MSG_TYPE_RANGING);
   dwt_writetxdata(packet_length, (uint8_t*)&_calibration_packet, 0);
   _state.calibration_timing[packet_num] = dw1000_setdelayedtrxtime(dwt_readsystimestamphi32() + (DW_DELAY_FROM_PKT_LEN(packet_length) * _state.dw_slack_delay_multiplier));
   bool success = (dwt_starttx(DWT_START_TX_DELAYED) == DWT_SUCCESS);
   while (!success)
   {
      // Find the optimal delay so that packet can be successfully sent in the future
      debug_msg("ERROR: Calibration packet transmission delay too short...increasing delay time\n");
      _state.calibration_timing[packet_num] = dw1000_setdelayedtrxtime(dwt_readsystimestamphi32() + (DW_DELAY_FROM_PKT_LEN(packet_length) * (++_state.dw_slack_delay_multiplier)));
      success = (dwt_starttx(DWT_START_TX_DELAYED) == DWT_SUCCESS);
   }
}

static void setup_round_antenna_channel(uint32_t round_num)
{
   // Setup the correct channel and antenna for the calibration broadcast
   dwt_forcetrxoff();
   uint8_t channel_index = ((round_num / CALIBRATION_NUM_NODES) / CALIBRATION_NUM_ANTENNAS) % CALIBRATION_NUM_CHANNELS;
   uint8_t antenna_index = (round_num / CALIBRATION_NUM_NODES) % CALIBRATION_NUM_ANTENNAS;
   dw1000_update_channel(channel_index_to_channel_rf_number[channel_index]);
   dw1000_choose_antenna(antenna_index);
}

static void round_timeout(void)
{
   // Skip the immediate callback which is triggered when the timer starts
   if (!_state.timeout_firing)
      _state.timeout_firing = TRUE;
   else
   {
      // Reset state
      debug_msg("WARNING: Timed out, try to enter again next round\n");
      timer_stop(_calibration_timer);
      _state.init_received = FALSE;
      setup_round_antenna_channel(0);
      dw1000_rxenable(DWT_START_RX_IMMEDIATE, UINT8_MAX, UINT8_MAX);
   }
}

static void finish(void)
{
   // Stop the timeout timer if we are not the Master
   if (_config.index != CALIBRATION_MASTER_NODE)
      timer_stop(_calibration_timer);

   // Notify host if we are node 0 or we got the init() packet AND
   // we are not the round starting node (that node doesn't have any useful timestamps)
   if ((_state.init_received || _config.index == CALIBRATION_MASTER_NODE) && !CALIBRATION_ROUND_STARTED_BY_ME(_state.round_num, _config.index))
   {
      // Fill in the host response buffer
      debug_msg("Finishing round ");
      debug_msg_uint(_state.round_num);
      debug_msg("\n");

      // Round number: calibration_response_buf[0-3]
      uint8_t offset = 0;
      memcpy(_response_buffer, &_state.round_num, sizeof(_state.round_num));
      offset += sizeof(_state.round_num);

      // Calibration timing - calibration_response_buf[4-8]
      _response_buffer[offset + 0] = (_state.calibration_timing[0] >> 0) & 0xFF;
      _response_buffer[offset + 1] = (_state.calibration_timing[0] >> 8) & 0xFF;
      _response_buffer[offset + 2] = (_state.calibration_timing[0] >> 16) & 0xFF;
      _response_buffer[offset + 3] = (_state.calibration_timing[0] >> 24) & 0xFF;
      _response_buffer[offset + 4] = (_state.calibration_timing[0] >> 32) & 0xFF;
      offset += 5;

      // Difference X1-X0 - calibration_response_buf[9-12]
      uint32_t diff = (uint32_t)(_state.calibration_timing[1] - _state.calibration_timing[0]);
      memcpy(_response_buffer + offset, &diff, sizeof(diff));
      offset += sizeof(diff);

      // Difference X2-X1 - calibration_response_buf[13-16]
      diff = (uint32_t)(_state.calibration_timing[2] - _state.calibration_timing[1]);
      memcpy(_response_buffer + offset, &diff, sizeof(diff));
      offset += sizeof(diff);

      // Send packet
      host_interface_notify_calibration(_response_buffer, offset);
   }

   // Start the next round
   _state.init_received = FALSE;
   setup_round_antenna_channel(0);
   if (_config.index != CALIBRATION_MASTER_NODE)
      dw1000_rxenable(DWT_START_RX_IMMEDIATE, UINT8_MAX, UINT8_MAX);
}

static void calibration_txcallback(const dwt_cb_data_t *txd)
{
   // Ensure that packet transmission was successful
   if (!(txd->status & SYS_STATUS_TXFRS))
   {
      debug_msg("ERROR: Failed to send packet, status: ");
      debug_msg_uint(txd->status);
      debug_msg("\n");
   }

   // If we just sent an INIT packet, set ourselves for the correct next position
   if (_calibration_packet.message_type == MSG_TYPE_CALIBRATION_INIT)
   {
      // Setup the correct channel for the next round
      setup_round_antenna_channel(_state.round_num);

      // Send or receive the next ranging cycle in this round
      if (CALIBRATION_ROUND_STARTED_BY_ME(_state.round_num, _config.index))
      {
         mDelay(2);
         send_calibration_packet(MSG_TYPE_CALIBRATION_MSG, 0);
      }
      else
         dw1000_rxenable(DWT_START_RX_IMMEDIATE, UINT8_MAX, UINT8_MAX);
   }
   else if (CALIBRATION_ROUND_FOR_ME(_state.round_num, _config.index))
   {
      if (_calibration_packet.packet_index == 1)
      {
         // Send the second ranging cycle in this round
         mDelay(2);
         send_calibration_packet(MSG_TYPE_CALIBRATION_MSG, 2);
      }
      else if (_calibration_packet.packet_index == 2)
         finish();
      else
      {
         debug_msg("ERROR: Sent incorrect number of packets ");
         debug_msg_uint(_calibration_packet.packet_index);
         debug_msg("\n");
      }
   }
   else if (CALIBRATION_ROUND_STARTED_BY_ME(_state.round_num, _config.index))
   {
      if (_calibration_packet.packet_index == 0)
         finish();
   }
   else
   {
      debug_msg("ERROR: Incorrect Tx; sent message of type ");
      debug_msg_uint(_calibration_packet.message_type);
      debug_msg(" in round ");
      debug_msg_uint(_state.round_num);
      debug_msg("\n");
   }
}

static void calibration_rxcallback(const dwt_cb_data_t *rxd)
{
   // Only process successfully received packets
   if (rxd->status & SYS_STATUS_RXFCG)
   {
      // Retrieve timestamp, packet length, and packet bytes
      uint64_t dw_rx_timestamp = dw1000_readrxtimestamp();
      dwt_readrxdata(_read_buffer, MIN(CALIBRATION_MAX_PACKET_LEN, rxd->datalength), 0);
      uint8_t message_type = _read_buffer[offsetof(calibration_packet_t, message_type)];
      calibration_packet_t *rx_start_pkt = (calibration_packet_t*)_read_buffer;

      // Handle the received packet based on type and index
      if (message_type == MSG_TYPE_CALIBRATION_INIT)
      {
         // Configure for the first calibration packet of a new round
         debug_msg("INFO: Received INIT packet for round ");
         debug_msg_uint(rx_start_pkt->round_num);
         debug_msg("\n");
         _state.init_received = TRUE;
         _state.round_num = rx_start_pkt->round_num;
         setup_round_antenna_channel(_state.round_num);

         // Set a timeout timer to allow us to return to the initial state if nothing is received
         if (_config.index != CALIBRATION_MASTER_NODE)
         {
            _state.timeout_firing = FALSE;
            timer_start(_calibration_timer, CALIBRATION_ROUND_TIMEOUT_US, round_timeout, TRUE);
         }
         else
            debug_msg("ERROR: Received INIT Packet as Master; this should not happen!\n");

         // Send or receive the next ranging cycle in this round
         if (CALIBRATION_ROUND_STARTED_BY_ME(_state.round_num, _config.index))
         {
            mDelay(2);
            send_calibration_packet(MSG_TYPE_CALIBRATION_MSG, 0);
         }
         else
            dw1000_rxenable(DWT_START_RX_IMMEDIATE, UINT8_MAX, UINT8_MAX);
      }
      else if (message_type == MSG_TYPE_CALIBRATION_MSG)
      {
         // Output the type and index of the packet received
         uint8_t packet_num = rx_start_pkt->packet_index;
         debug_msg("INFO: Received calibration packet: type ");
         debug_msg_uint(message_type);
         debug_msg(", number ");
         debug_msg_uint(packet_num);
         debug_msg("\n");

         // Store timestamps
         if (packet_num < CALIBRATION_NUM_PACKETS)
            _state.calibration_timing[packet_num] = dw_rx_timestamp;
         else
         {
            _state.init_received = FALSE;
            debug_msg("ERROR: Invalid packet number ");
            debug_msg_uint(packet_num);
            debug_msg("\n");
         }

         // Move on to the next packet number based on our current role
         if (CALIBRATION_ROUND_FOR_ME(_state.round_num, _config.index))
         {
            // After the first packet, based on the round number the node to be calibrated sends the next two packets
            if (packet_num == 0)
               send_calibration_packet(MSG_TYPE_CALIBRATION_MSG, 1);
            else
            {
               dw1000_rxenable(DWT_START_RX_IMMEDIATE, UINT8_MAX, UINT8_MAX);
               debug_msg("ERROR: Incorrect packet number ");
               debug_msg_uint(packet_num);
               debug_msg("\n");
            }
         }
         else if ((packet_num == (CALIBRATION_NUM_PACKETS - 1)) && !CALIBRATION_ROUND_STARTED_BY_ME(_state.round_num, _config.index))
            finish();
         else
            dw1000_rxenable(DWT_START_RX_IMMEDIATE, UINT8_MAX, UINT8_MAX);
      }
      else
      {
         dw1000_rxenable(DWT_START_RX_IMMEDIATE, UINT8_MAX, UINT8_MAX);
         debug_msg("ERROR: Received invalid message type: ");
         debug_msg_uint(message_type);
         debug_msg("\n");
      }
   }
   else if ((rxd->status & SYS_STATUS_ALL_RX_ERR) || (rxd->status & SYS_STATUS_ALL_RX_TO))
   {
      if (!(rxd->status & SYS_STATUS_RXPTO) && !(rxd->status & SYS_STATUS_RXSFDTO))
         dwt_rxreset();
      setup_round_antenna_channel(0);
      dw1000_rxenable(DWT_START_RX_IMMEDIATE, UINT8_MAX, UINT8_MAX);
      debug_msg("ERROR: Packet reception error, Status Code: ");
      debug_msg_uint(rxd->status);
      debug_msg("\n");
   }
}

static void calibration_round_start(void)
{
   // Increment the round number
   if (_state.round_num == UINT32_MAX)
      _state.round_num = 0;
   else
      ++_state.round_num;

   // Set the default settings, force enable TX clock, and send an INIT packet
   setup_round_antenna_channel(0);
   dwt_write8bitoffsetreg(PMSC_ID, PMSC_CTRL0_OFFSET, PMSC_CTRL0_TXCLKS_125M);
   send_calibration_packet(MSG_TYPE_CALIBRATION_INIT, 0);
}

// Public functions ----------------------------------------------------------------------------------------------------

bool calibration_configure(calibration_config_t *config)
{
   // Ensure that calibration is currently stopped
   calibration_stop();
   _is_configured = FALSE;
   debug_msg("INFO: Configuring calibration module as node ");
   debug_msg_uint(config->index);
   debug_msg("\n");

   // Save the calibration settings
   memcpy(&_config, config, sizeof(_config));

   // Initialize the calibration packet
   _calibration_packet = (calibration_packet_t){ .header = { .frameCtrl = { 0x41, 0xC8 }, .seqNum = 0,
         .panID = { MODULE_PANID & 0xFF, MODULE_PANID >> 8 }, .destAddr = { 0xFF, 0xFF }, .sourceAddr = { 0 } },
      .message_type = MSG_TYPE_CALIBRATION_INIT, .round_num = 0, .packet_index = 0 };

   // Make sure the DW1000 is awake and retrieve its EUI
   if (!dw1000_wakeup())
      return FALSE;
   dw1000_read_eui(_calibration_packet.header.sourceAddr);

   // Return success if calibration was configured correctly
   debug_msg("INFO: Calibration module successfully configured!\n");
   _is_configured = TRUE;
   return TRUE;
}

bool calibration_start(void)
{
   // Do nothing if calibration is already running
   if (!_is_configured)
      return FALSE;
   else if (_is_running)
      return TRUE;
   debug_msg("INFO: Starting calibration module...\n");

   // Initialize all static global variables
   memset(&_state, 0, sizeof(_state));
   _state.round_num = UINT32_MAX;
   _state.timeout_firing = _state.init_received = FALSE;
   _state.dw_slack_delay_multiplier = 5;

   // Make sure the DW1000 is awake
   if (!dw1000_wakeup())
      return FALSE;

   // Set all DW1000 settings and turn the radio OFF
   dw1000_spi_slow();
   dwt_setcallbacks(calibration_txcallback, calibration_rxcallback, calibration_rxcallback, calibration_rxcallback);
   dwt_forcetrxoff();

   // Set this device so that it only receives data and acknowledgment packets
   dwt_enableframefilter(DWT_FF_DATA_EN | DWT_FF_ACK_EN);

   // If enabled, load the specific Operational Parameter Set to deal with 64-symbol preambles
   // This has to be done with the DW1000 set to crystal speed
#ifdef DW1000_ENABLE_OPSET_64LEN
   dwt_loadopsettabfromotp(DWT_OPSET_64LEN);
#endif

   // Disable the DW1000 double receive buffer mode, receive timeouts, and set the SPI to fast
   dwt_setdblrxbuffmode(FALSE);
   dwt_setrxtimeout(FALSE);
   dw1000_spi_fast();

   // Initialize the calibration timer
   _calibration_timer = timer_init(0);
   if (!_calibration_timer)
   {
      debug_msg("ERROR: Unable to initialize the calibration timer...cannot continue!\n");
      calibration_stop();
      return FALSE;
   }

   // Start the calibration timer or just start listening if not the Master
   setup_round_antenna_channel(0);
   if (_config.index == CALIBRATION_MASTER_NODE)
      timer_start(_calibration_timer, CALIBRATION_ROUND_PERIOD_US, calibration_round_start, TRUE);
   else if ((_config.index > CALIBRATION_MASTER_NODE) && (_config.index < CALIBRATION_NUM_NODES))
      dw1000_rxenable(DWT_START_RX_IMMEDIATE, UINT8_MAX, UINT8_MAX);
   else
   {
      debug_msg("ERROR: Invalid node index: ");
      debug_msg_uint(_config.index);
      debug_msg("\n");
      calibration_stop();
      return FALSE;
   }

   // Inform that the calibration module is now running
   _is_running = TRUE;
   debug_msg("INFO: Calibration module successfully started\n");
   return TRUE;
}

void calibration_stop(void)
{
   // Disable calibration and stop the calibration timers
   debug_msg("INFO: Stopping calibration module...\n");
   _is_running = FALSE;
   if (_calibration_timer)
   {
      timer_stop(_calibration_timer);
      timer_free(_calibration_timer);
      _calibration_timer = NULL;
   }

   // Turn the DW1000 radio OFF
   dwt_forcetrxoff();
   debug_msg("INFO: Calibration module stopped!\n");
}

bool calibration_reset(void)
{
   // Force a complete restart of the calibration module
   debug_msg("INFO: Resetting calibration module...\n");
   calibration_stop();
   return calibration_start();
}
