#include <assert.h>
#include "logging.h"
#include "ranging.h"
#include "scheduler.h"
#include "schedule_phase.h"
#include "system.h"

static uint8_t eui[EUI_LEN], read_buffer[1024];

static void tx_callback(const dwt_cb_data_t *txData)
{
   volatile uint64_t tx_timestamp = ranging_radio_readtxtimestamp();
   print("Callback 'tx_callback' fired at %lu us\n", DWT_TO_US(tx_timestamp));
}

static void rx_done_callback(const dwt_cb_data_t *rxData)
{
   // Check the reception timestamp and re-enable packet reception
   volatile uint64_t rx_timestamp = ranging_radio_readrxtimestamp();
   print("Callback 'rx_callback' fired at %lu us\n", DWT_TO_US(rx_timestamp));
   ranging_radio_rxenable(DWT_START_RX_IMMEDIATE);
   dwt_readrxdata(read_buffer, rxData->datalength, 0);
   print("Message Length: %u, Seq: %u, Type: %u\n", (uint32_t)rxData->datalength, (uint32_t)read_buffer[sizeof(ieee154_header_t)], (uint32_t)read_buffer[2]);
}

static void rx_error_callback(const dwt_cb_data_t *rxData)
{
   // Re-enable packet reception upon error
   volatile uint64_t rx_timestamp = ranging_radio_readrxtimestamp();
   print("Callback 'rx_error_callback' fired at %lu us\n", DWT_TO_US(rx_timestamp));
   print("Status Flags: %u, RX Flags: %u\n", rxData->status, (uint32_t)rxData->rx_flags);
   ranging_radio_rxenable(DWT_START_RX_IMMEDIATE);
}

void reset_test(void)
{
   ranging_radio_reset();
   am_hal_delay_us(3000000);
}

void regular_sleep_test(void)
{
   ranging_radio_sleep(false);
   am_hal_delay_us(3000000);
   ranging_radio_wakeup();
   am_hal_delay_us(3000000);
}

void deep_sleep_test(void)
{
   ranging_radio_sleep(true);
   am_hal_delay_us(3000000);
   ranging_radio_wakeup();
   am_hal_delay_us(3000000);
}

void delayed_write_test(void)
{
   // Create a test packet for sending
   schedule_packet_t packet = (schedule_packet_t){ .header = { .msgType = SCHEDULE_PACKET, },
      .src_addr = 0, .sequence_number = 0, .epoch_time_unix = 12345678, .num_devices = 0,
      .schedule = { 0 }, .footer = { { 0 } } };
   uint16_t packet_size = sizeof(packet);

   // Select the appropriate antenna and channel
   ranging_radio_disable();
   ranging_radio_choose_antenna(0);
   ranging_radio_choose_channel(5);

   // Transmit the packet 2ms in the future
   dwt_writetxfctrl(packet_size, 0, 0);
   dwt_setdelayedtrxtime(dwt_readsystimestamphi32() + DW_DELAY_FROM_US(2000));
   assert(dwt_writetxdata(packet_size, (uint8_t*)&packet, 0) == DWT_SUCCESS);
   assert(dwt_starttx(DWT_START_TX_DELAYED) == DWT_SUCCESS);

   // Sleep for 5 seconds
   am_hal_delay_us(1000000);
}

void read_test(void)
{
   // Enable packet reception
   ranging_radio_choose_antenna(0);
   ranging_radio_choose_channel(5);
   ranging_radio_rxenable(DWT_START_RX_IMMEDIATE);

   // Sleep for 60 seconds
   am_hal_delay_us(60000000);
}

int main(void)
{
   // Set up system hardware
   setup_hardware();
   system_enable_interrupts(true);
   system_read_UID(eui, EUI_LEN);
   ranging_radio_init(eui);
   ranging_radio_register_callbacks(tx_callback, rx_done_callback, rx_error_callback, rx_error_callback);

   // Loop forever running whichever test is uncommented
   while (true)
   {
      //reset_test();
      //regular_sleep_test();
      //deep_sleep_test();
      //delayed_write_test();
      read_test();
   }

   // Should never reach this point
   return 0;
}
