#include <math.h>
#include "logging.h"
#include "ranging.h"
#include "system.h"

// Comment the following line to flash RECEIVER device
#define IS_TRANSMITTER

typedef struct __attribute__ ((__packed__))
{
   ieee154_header_t header;
   uint8_t data[24];
   ieee154_footer_t footer;
} data_packet_t;

static uint8_t eui[EUI_LEN];
static data_packet_t read_packet, write_packet;

static void tx_callback(const dwt_cb_data_t *txData) {}

static void rx_done_callback(const dwt_cb_data_t *rxData)
{
   // Check the reception timestamp and re-enable packet reception
   volatile uint64_t rx_timestamp = ranging_radio_readrxtimestamp();
   print("Callback 'rx_callback' fired at %lu us\n", DWT_TO_US(rx_timestamp));
   ranging_radio_rxenable(DWT_START_RX_IMMEDIATE);
   dwt_readrxdata((uint8_t*)&read_packet, rxData->datalength, 0);
   print("Message Length: %u, Data[0-4]: %u %u %u %u, RX Power dBm: %0.02f\n", (uint32_t)rxData->datalength,
         (uint32_t)read_packet.data[0], (uint32_t)read_packet.data[1],
         (uint32_t)read_packet.data[2], (uint32_t)read_packet.data[3],
         ranging_radio_received_signal_level(false));
}

static void rx_error_callback(const dwt_cb_data_t *rxData)
{
   // Re-enable packet reception upon error
   volatile uint64_t rx_timestamp = ranging_radio_readrxtimestamp();
   print("Callback 'rx_error_callback' fired at %lu us\n", DWT_TO_US(rx_timestamp));
   print("Status Flags: %u, RX Flags: %u\n", rxData->status, (uint32_t)rxData->rx_flags);
   ranging_radio_rxenable(DWT_START_RX_IMMEDIATE);
}

int main(void)
{
   // Generate the static write packet contents
   write_packet = (data_packet_t){ .header = { .frameCtrl = { 0x41, 0x88 }, .msgType = 0,
         .panID = { MODULE_PANID & 0xFF, MODULE_PANID >> 8 }, .destAddr = { 0xFF, 0xFF }, .sourceAddr = { 0 } },
      .data = { 0 }, .footer = { { 0 } } };
   for (uint8_t i = 0; i < sizeof(write_packet.data); ++i)
      write_packet.data[i] = i;

   // Set up system hardware
   setup_hardware();
   system_enable_interrupts(true);
   system_read_UID(eui, EUI_LEN);
   ranging_radio_init(eui);
   ranging_radio_enable_rx_diagnostics();
   ranging_radio_register_callbacks(tx_callback, rx_done_callback, rx_error_callback, rx_error_callback);

   // Select the appropriate antenna and channel
   ranging_radio_disable();
   ranging_radio_choose_antenna(0);
   ranging_radio_choose_channel(5);

   // Loop forever running ranging power tests
   while (true)
   {
#ifdef IS_TRANSMITTER
      // Transmit the static write buffer
      uint16_t buffer_size = sizeof(write_packet);
      dwt_writetxfctrl(buffer_size, 0, 0);
      dwt_writetxdata(buffer_size, (uint8_t*)&write_packet, 0);
      dwt_starttx(DWT_START_TX_IMMEDIATE);
#else
      // Start listening for incoming data
      ranging_radio_rxenable(DWT_START_RX_IMMEDIATE);
#endif

      // Go to sleep for 1 second
      am_hal_delay_us(1000000);
   }

   // Should never reach this point
   return 0;
}
