#include "logging.h"
#include "ranging.h"
#include "system.h"

static uint8_t eui[EUI_LEN];
static dwt_config_t config = {
   5,                /* Channel number. */
   DWT_PLEN_1024,    /* Preamble length. Used in TX only. */
   DWT_PAC32,        /* Preamble acquisition chunk size. Used in RX only. */
   9,                /* TX preamble code. Used in TX only. */
   9,                /* RX preamble code. Used in RX only. */
   1,                /* 0 to use standard 8 symbol SFD, 1 to use non-standard 8 symbol, 2 for non-standard 16 symbol SFD and 3 for 4z 8 symbol SDF type */
   DWT_BR_850K,      /* Data rate. */
   DWT_PHRMODE_STD,  /* PHY header mode. */
   DWT_PHRRATE_STD,  /* PHY header rate. */
   (1025 + 8 - 32),  /* SFD timeout (preamble length + 1 + SFD length - PAC size). Used in RX only. */
   DWT_STS_MODE_OFF, /* STS disabled */
   DWT_STS_LEN_64,   /* STS length see allowed values in Enum dwt_sts_lengths_e */
   DWT_PDOA_M0       /* PDOA mode off */
};
static dwt_txconfig_t txconfig_options = {
   0x34,       /* PG delay. */
   0xfdfdfdfd, /* TX power. */
   0
};

int main(void)
{
   // Set up system hardware
   setup_hardware();
   system_enable_interrupts(true);
   system_read_UID(eui, EUI_LEN);
   ranging_radio_init(eui);

   // Activate continuous wave mode with recommended parameters
   dwt_configure(&config);
   dwt_configuretxrf(&txconfig_options);
   dwt_configcwmode();

   // Loop forever
   while (true)
      am_hal_sysctrl_sleep(AM_HAL_SYSCTRL_SLEEP_DEEP);

   // Should never reach this point
   return 0;
}
