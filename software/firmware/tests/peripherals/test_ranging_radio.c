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
   print("Callback 'tx_callback' fired at %lu us %llu \n", APP_DEVICETIMEU64_TO_US(tx_timestamp), tx_timestamp);
   print("Tx Xtrim %d\n",dwt_getxtaltrim());
}

static void rx_done_callback(const dwt_cb_data_t *rxData)
{
   volatile uint64_t rx_timestamp = ranging_radio_readrxtimestamp();
   dwt_rxdiag_t rx_diag;
   memset(&rx_diag, 0, sizeof(rx_diag));
   dwt_readdiagnostics(&rx_diag);
   uint32_t sys_status_lo = dwt_readsysstatuslo();
   uint32_t sys_status_hi = dwt_readsysstatushi();
   
   
   print("Callback 'rx_callback' fired at %lu us %llu \n", APP_DEVICETIMEU64_TO_US(rx_timestamp), rx_timestamp); 
   ranging_radio_rxenable(DWT_START_RX_IMMEDIATE);
   dwt_readrxdata(read_buffer, rxData->datalength, 0);
   print("Rx Xtrim %d\n",dwt_getxtaltrim());
   print("Message Length: %u, Type: %u, Seq: %u\n Status Flags: %u, RX Flags: %u\n", (uint32_t)rxData->datalength, (uint32_t)read_buffer[sizeof(ieee154_header_t)], (uint32_t)read_buffer[2],rxData->status, (uint32_t)rxData->rx_flags);
}

static void rx_error_callback(const dwt_cb_data_t *rxData)
{
   volatile uint64_t rx_timestamp = ranging_radio_readrxtimestamp();
   dwt_rxdiag_t rx_diag;
   memset(&rx_diag, 0, sizeof(rx_diag));
   dwt_readdiagnostics(&rx_diag);
   uint32_t sys_status_lo = dwt_readsysstatuslo();
   uint32_t sys_status_hi = dwt_readsysstatushi();
   
   print("Callback 'rx_error_callback' fired at %lu us %llu \n", APP_DEVICETIMEU64_TO_US(rx_timestamp),rx_timestamp);
   print("Status Flags: %u, RX Flags: %u\n", rxData->status, (uint32_t)rxData->rx_flags);
   print("Rx Xtrim %d\n",dwt_getxtaltrim());
   ranging_radio_rxenable(DWT_START_RX_IMMEDIATE);
}

static void rx_timeout_callback(const dwt_cb_data_t *rxData)
{
   volatile uint64_t rx_timestamp = ranging_radio_readrxtimestamp();
   dwt_rxdiag_t rx_diag;
   memset(&rx_diag, 0, sizeof(rx_diag));
   dwt_readdiagnostics(&rx_diag);
   uint32_t sys_status_lo = dwt_readsysstatuslo();
   uint32_t sys_status_hi = dwt_readsysstatushi();
	
   print("Callback 'rx_timeout_callback' fired at %lu us %llu \n", APP_DEVICETIMEU64_TO_US(rx_timestamp),rx_timestamp);
   print("Status Flags: %u, RX Flags: %u\n", rxData->status, (uint32_t)rxData->rx_flags);
   print("Rx Xtrim %d\n",dwt_getxtaltrim());
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

static uint8_t seq = 0;
void delayed_write_test(uint8_t antenna, uint8_t channel)
{
   // Create a test packet for sending
   static schedule_packet_t packet = (schedule_packet_t){ .header = { .frameCtrl = { 0x41, 0xC8 }, .seqNum = 0,
         .panID = { MODULE_PANID & 0xFF, MODULE_PANID >> 8 }, .destAddr = { 0xFF, 0xFF }, .sourceAddr = { 0 } },
      .message_type = SCHEDULE_PACKET, .epoch_time_unix = 12345678, .num_devices = 0,
      .schedule = { 0 }, .footer = { { 0 } } };
   static uint16_t packet_size = sizeof(packet);
   
   seq+=1;
   if (seq==10){seq = 0;}
   
 

   // Select the appropriate antenna and channel
   ranging_radio_disable();
   ranging_radio_choose_antenna(antenna);
   ranging_radio_choose_channel(channel);

   // Transmit the packet 2ms in the future
   dwt_writetxfctrl(packet_size, 0, 0);
   dwt_setdelayedtrxtime(dwt_readsystimestamphi32() + DW_DELAY_FROM_US(2000));
   assert(dwt_writetxdata(packet_size, (uint8_t*)&packet, 0) == DWT_SUCCESS);
   assert(dwt_starttx(DWT_START_TX_DELAYED) == DWT_SUCCESS);

   // Sleep for 2 seconds
   am_hal_delay_us(2000000);
}

void read_test(uint8_t antenna, uint8_t channel)
{
   // Enable packet reception
   ranging_radio_choose_antenna(antenna);
   ranging_radio_choose_channel(channel);
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
   //dwt_setxtaltrim(0);
   dwt_setxtaltrim(40);
   ranging_radio_register_callbacks(tx_callback, rx_done_callback, rx_timeout_callback, rx_error_callback);
   print("please print this!");
   // Loop forever running whichever test is uncommented
   while (true)
   {
      //reset_test();
      //regular_sleep_test();
	   //deep_sleep_test();
	   
	   
	   //delayed_write_test(0,5);
	   //delayed_write_test(1,5);
	   //delayed_write_test(2,5);
	   
	   //delayed_write_test(0,9);
	   //delayed_write_test(1,9);
	   //delayed_write_test(2,9);
	   
	   read_test(0,5);
	   //read_test(1,5);
	   //read_test(2,5);
	   
	   //read_test(0,9);
	   //read_test(1,9);
	   //read_test(2,9);
	   

   }

   // Should never reach this point
   return 0;
}
