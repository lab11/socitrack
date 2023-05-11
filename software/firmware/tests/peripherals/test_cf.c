#include <assert.h>
#include "logging.h"
#include "ranging.h"
#include "scheduler.h"
#include "schedule_phase.h"
#include "system.h"


/* Start-to-start delay between frames, expressed in halves of the 499.2 MHz fundamental frequency (around 4 ns). See NOTE 1 below. */
#define CONT_FRAME_PERIOD 249600

/* Continuous frame duration, in milliseconds. */
#define CONT_FRAME_DURATION_MS 10000

static uint8_t tx_msg[] = { 0xC5, 0, 'D', 'E', 'C', 'A', 'W', 'A', 'V', 'E', 0, 0 };

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


void cw_tx_test(uint8_t antenna, uint8_t channel){
	
    // Select the appropriate antenna and channel
    ranging_radio_disable();
    ranging_radio_choose_antenna(0);
    ranging_radio_choose_channel(5);
	
	dwt_configcwmode();
	
	print("testing cw mode\n");
	
	am_hal_delay_us(2000000);
}


void cf_tx_test(uint8_t antenna, uint8_t channel)
{

   // Select the appropriate antenna and channel
   ranging_radio_disable();
   ranging_radio_choose_antenna(antenna);
   ranging_radio_choose_channel(channel);
   
   /* Activate continuous frame mode. */
   dwt_configcontinuousframemode(CONT_FRAME_PERIOD);

   /* Once configured, continuous frame must be started like a normal transmission. */
   dwt_writetxdata(sizeof(tx_msg), tx_msg, 0); /* Zero offset in TX buffer. */
   dwt_writetxfctrl(sizeof(tx_msg), 0, 0);     /* Zero offset in TX buffer, no ranging. */
   dwt_starttx(DWT_START_TX_IMMEDIATE);

   /* Wait for the required period of repeated transmission. */
   am_hal_delay_us(CONT_FRAME_DURATION_MS*1000);
   
   print("testing cf mode\n");

   /* Disable continuous frame mode. */
   dwt_disablecontinuousframemode();

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
   
   print("before init!\n");
   
   ranging_radio_init_cf();
   //dwt_setxtaltrim(46);
   //ranging_radio_register_callbacks(tx_callback, rx_done_callback, rx_timeout_callback, rx_error_callback);
   print("please print this!\n");
   // Loop forever running whichever test is uncommented
   while (true)
   {
      //reset_test();
      //regular_sleep_test();
	   //deep_sleep_test();
	   //delayed_write_test();
	   //read_test();
	   
	   cf_tx_test(0,5);
	   //cf_tx_test(1,5);
	   //cf_tx_test(2,5);
	   
	   //cf_tx_test(0,9);
	   //cf_tx_test(1,9);
	   //cf_tx_test(2,9);
   }

   // Should never reach this point
   return 0;
}
