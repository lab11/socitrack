#include "logging.h"
#include "ranging.h"
#include "system.h"

// Start-to-start delay between frames, expressed in halves of the 499.2 MHz fundamental frequency (around 4 ns)
#define CONT_FRAME_PERIOD 249600

static uint8_t eui[EUI_LEN];
static dwt_config_t config = {
   5,                /* Channel number. */
   DWT_PLEN_128,     /* Preamble length. Used in TX only. */
   DWT_PAC8,         /* Preamble acquisition chunk size. Used in RX only. */
   9,                /* TX preamble code. Used in TX only. */
   9,                /* RX preamble code. Used in RX only. */
   1,                /* 0 to use standard 8 symbol SFD, 1 to use non-standard 8 symbol, 2 for non-standard 16 symbol SFD and 3 for 4z 8 symbol SDF type */
   DWT_BR_6M8,       /* Data rate. */
   DWT_PHRMODE_STD,  /* PHY header mode. */
   DWT_PHRRATE_STD,  /* PHY header rate. */
   (129 + 8 - 8),    /* SFD timeout (preamble length + 1 + SFD length - PAC size). Used in RX only. */
   DWT_STS_MODE_OFF, /* STS disabled */
   DWT_STS_LEN_64,   /* STS length see allowed values in Enum dwt_sts_lengths_e */
   DWT_PDOA_M0       /* PDOA mode off */
};
static dwt_txconfig_t txconfig_options = {
   0x34,       /* PG delay. */
   0xfdfdfdfd, /* TX power. */
   0
};

// Standard 802.15.4e blink:
//   Byte 0: frame type (0xC5 for a blink)
//   Byte 1: sequence number, put to 0
//   Byte 2 -> 9: device ID, hard coded constant in this example for simplicity
//   Byte 10/11: frame check-sum, automatically set by DW IC in a normal transmission and set to 0 here for simplicity.
static uint8_t tx_msg[] = { 0xC5, 0, 'D', 'E', 'C', 'A', 'W', 'A', 'V', 'E', 0, 0 };

int main(void)
{
   // Set up system hardware
   setup_hardware();
   system_enable_interrupts(true);
   system_read_UID(eui, EUI_LEN);
   ranging_radio_init(eui);

   // Activate continuous frame mode with recommended parameters
   dwt_configure(&config);
   dwt_configuretxrf(&txconfig_options);
   dwt_configcontinuousframemode(CONT_FRAME_PERIOD);

   // Initiate continuous frame transmission like a normal transmission
   dwt_writetxdata(sizeof(tx_msg), tx_msg, 0);
   dwt_writetxfctrl(sizeof(tx_msg), 0, 0);
   dwt_starttx(DWT_START_TX_IMMEDIATE);

   // Loop forever
   while (true)
      am_hal_sysctrl_sleep(AM_HAL_SYSCTRL_SLEEP_DEEP);

   // Should never reach this point
   return 0;
}
