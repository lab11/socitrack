#include "bluetooth.h"
#include "buzzer.h"
#include "imu.h"
#include "led.h"
#include "logging.h"
#include "ranging.h"
#include "rtc.h"
#include "storage.h"
#include "system.h"

// Uncomment the following for the master anchor tag
//#define MASTER_TAG
#define RX_ERROR_TIMER_NUMBER 6

typedef struct __attribute__ ((__packed__))
{
   ieee154_header_t header;
   uint8_t data[24];
   ieee154_footer_t footer;
} data_packet_t;

static volatile uint8_t antenna = 0;

static void tx_callback(const dwt_cb_data_t *txData) {}

static void rx_error_callback(const dwt_cb_data_t *rxData)
{
   // Switch to the next antenna and re-enable reception
   antenna = (antenna + 1) % 3;
   ranging_radio_disable();
   ranging_radio_choose_antenna(antenna);
   ranging_radio_rxenable(DWT_START_RX_IMMEDIATE);
   am_hal_timer_clear(RX_ERROR_TIMER_NUMBER);

   // Illuminate the error LED
   led_off(LED_ALL);
   led_on(LED_RED);
}

static void rx_done_callback(const dwt_cb_data_t *rxData)
{
   // Switch to the next antenna and re-enable reception
   antenna = (antenna + 1) % 3;
   ranging_radio_disable();
   ranging_radio_choose_antenna(antenna);
   ranging_radio_rxenable(DWT_START_RX_IMMEDIATE);
   am_hal_timer_clear(RX_ERROR_TIMER_NUMBER);

   // Illuminate the LEDs based on the received signal level
   const float signal_level = ranging_radio_received_signal_level(false);
   led_off(LED_ALL);
   led_on((signal_level < -85.0f) ? LED_RED : ((signal_level < -70.0f) ? LED_ALL : LED_GREEN));
}

void am_timer06_isr(void)
{
   // Clear the timer interrupt and turn off all LEDs
   am_hal_timer_interrupt_clear(AM_HAL_TIMER_MASK(RX_ERROR_TIMER_NUMBER, AM_HAL_TIMER_COMPARE0));
   led_off(LED_ALL);
}

int main(void)
{
   // Generate the transmission packet contents
   data_packet_t write_packet = { .header = { .msgType = 0 }, .data = { 0 }, .footer = { { 0 } } };
   for (uint8_t i = 0; i < sizeof(write_packet.data); ++i)
      write_packet.data[i] = i;

   // Set up the hardware
   static uint8_t uid[EUI_LEN] = { 0x01, 0x00, 0x42, 0xe5, 0x98, 0xc0 };
   setup_hardware();

   // Initialize and test all feedback peripherals
   leds_init();
   buzzer_init();
   for (int i = 0; i < 3; ++i)
   {
      led_on(LED_ALL);
      am_util_delay_ms(400);
      led_off(LED_ALL);
      am_util_delay_ms(200);
   }
   led_off(LED_ALL);
   buzzer_indicate_plugged_in();

   // Initialize the RTC clock, storage chip, and IMU
   rtc_init();
   if (!storage_init() || !imu_init())
      while (1)
      {
         buzzer_indicate_unplugged();
         am_util_delay_ms(1000);
      }

   // Initialize the BLE peripheral
   if (!bluetooth_init(uid))
      while (1)
      {
         buzzer_indicate_error();
         am_util_delay_ms(1000);
      }

   // Enable interrupts and initialize the ranging radio
   system_enable_interrupts(true);
   if (!ranging_radio_init(uid))
      while (1)
      {
         led_on(LED_RED);
         am_util_delay_ms(1000);
         led_off(LED_RED);
         am_util_delay_ms(1000);
      }
   ranging_radio_enable_rx_diagnostics();
   ranging_radio_register_callbacks(tx_callback, rx_done_callback, rx_error_callback, rx_error_callback);

   // Set up the RX error timer configuration
   am_hal_timer_config_t timer_config;
   am_hal_timer_default_config_set(&timer_config);
   timer_config.eFunction = AM_HAL_TIMER_FN_UPCOUNT;
   timer_config.eInputClock = AM_HAL_TIMER_CLOCK_HFRC_DIV256;
   timer_config.ui32Compare0 = 3 * (AM_HAL_CLKGEN_FREQ_MAX_HZ / 256);
   am_hal_timer_config(RX_ERROR_TIMER_NUMBER, &timer_config);
   am_hal_timer_interrupt_enable(AM_HAL_TIMER_MASK(RX_ERROR_TIMER_NUMBER, AM_HAL_TIMER_COMPARE0));
   NVIC_SetPriority(TIMER0_IRQn + RX_ERROR_TIMER_NUMBER, NVIC_configKERNEL_INTERRUPT_PRIORITY - 1);
   NVIC_EnableIRQ(TIMER0_IRQn + RX_ERROR_TIMER_NUMBER);

   // Select the starting antenna and channel
   ranging_radio_disable();
   ranging_radio_choose_antenna(antenna);
   ranging_radio_choose_channel(5);

   // Loop forever running ranging power tests
   while (true)
   {
#ifdef MASTER_TAG
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
