// Header Inclusions ---------------------------------------------------------------------------------------------------

#include <math.h>
#include "deca_interface.h"
#include "logging.h"
#include "ranging.h"


// Static Global Variables ---------------------------------------------------------------------------------------------

static void *spi_handle;
static dwt_config_t dw_config;
static const dwt_txconfig_t tx_config_ch5 = { 0x34, 0xFDFDFDFD, 0x0 }, tx_config_ch9 = { 0x34, 0xFEFEFEFE, 0x0 };
static volatile bool spi_ready;
static uint8_t eui64_array[8];


// Private Helper Functions --------------------------------------------------------------------------------------------

static void ranging_radio_spi_ready(const dwt_cb_data_t *rxData)
{
   // Set the initialization state if the SPI interface is ready
   spi_ready = ((rxData->status & DWT_INT_SPIRDY_BIT_MASK) != 0);
}

static void ranging_radio_isr(void *args)
{
   // Call the DW3000 ISR as long as the interrupt pin is asserted
   static uint32_t pin_status;
   do
   {
      dwt_isr();
      am_hal_gpio_state_read(PIN_RADIO_INTERRUPT, AM_HAL_GPIO_INPUT_READ, &pin_status);
   } while (pin_status);
}

static void ranging_radio_spi_slow(void)
{
   static const am_hal_iom_config_t spi_slow_config = {
      .eInterfaceMode = AM_HAL_IOM_SPI_MODE, .ui32ClockFreq = AM_HAL_IOM_6MHZ, .eSpiMode = AM_HAL_IOM_SPI_MODE_0,
      .pNBTxnBuf = NULL, .ui32NBTxnBufLength = 0 };
   am_hal_iom_power_ctrl(spi_handle, AM_HAL_SYSCTRL_WAKE, false);
   am_hal_iom_configure(spi_handle, &spi_slow_config);
   am_hal_iom_enable(spi_handle);
}

static void ranging_radio_spi_fast(void)
{
   static const am_hal_iom_config_t spi_fast_config = {
      .eInterfaceMode = AM_HAL_IOM_SPI_MODE, .ui32ClockFreq = AM_HAL_IOM_24MHZ, .eSpiMode = AM_HAL_IOM_SPI_MODE_0,
      .pNBTxnBuf = NULL, .ui32NBTxnBufLength = 0 };
   am_hal_iom_power_ctrl(spi_handle, AM_HAL_SYSCTRL_WAKE, false);
   am_hal_iom_configure(spi_handle, &spi_fast_config);
   am_hal_iom_enable(spi_handle);
}

static int readfromspi(uint16_t headerLength, uint8_t *headerBuffer, uint16_t readLength, uint8_t *readBuffer)
{
   // Create the SPI read transaction structure
   uint64_t instruction = 0;
   for (uint32_t i = 0; i < headerLength; ++i)
      ((uint8_t*)&instruction)[headerLength-1-i] = headerBuffer[i];
   am_hal_iom_transfer_t read_transaction = {
      .uPeerInfo.ui32SpiChipSelect  = 0,
      .ui32InstrLen                 = headerLength,
      .ui64Instr                    = instruction,
      .eDirection                   = AM_HAL_IOM_RX,
      .ui32NumBytes                 = readLength,
      .pui32TxBuffer                = NULL,
      .pui32RxBuffer                = (uint32_t*)readBuffer,
      .bContinue                    = false,
      .ui8RepeatCount               = 0,
      .ui8Priority                  = 1,
      .ui32PauseCondition           = 0,
      .ui32StatusSetClr             = 0
   };

   // Repeat the transfer until it succeeds
   while (am_hal_iom_blocking_transfer(spi_handle, &read_transaction) != AM_HAL_STATUS_SUCCESS);
   return 0;
}

static int writetospi(uint16_t headerLength, const uint8_t *headerBuffer, uint16_t bodyLength, const uint8_t *bodyBuffer)
{
   // Create the SPI write transaction structure
   uint64_t instruction = 0;
   for (uint32_t i = 0; i < headerLength; ++i)
      ((uint8_t*)&instruction)[headerLength-1-i] = headerBuffer[i];
   am_hal_iom_transfer_t write_transaction = {
      .uPeerInfo.ui32SpiChipSelect  = 0,
      .ui32InstrLen                 = headerLength,
      .ui64Instr                    = instruction,
      .eDirection                   = AM_HAL_IOM_TX,
      .ui32NumBytes                 = bodyLength,
      .pui32TxBuffer                = (uint32_t*)bodyBuffer,
      .pui32RxBuffer                = NULL,
      .bContinue                    = false,
      .ui8RepeatCount               = 0,
      .ui8Priority                  = 1,
      .ui32PauseCondition           = 0,
      .ui32StatusSetClr             = 0
   };

   // Repeat the transfer until it succeeds
   while (am_hal_iom_blocking_transfer(spi_handle, &write_transaction) != AM_HAL_STATUS_SUCCESS);
   return 0;
}

static void wakeup_device_with_io(void)
{
   // Assert the WAKEUP pin for >=500us
   am_hal_gpio_output_set(PIN_RADIO_WAKEUP);
   am_hal_delay_us(500);
   am_hal_gpio_output_clear(PIN_RADIO_WAKEUP);
}


// DW3000 Required Driver Function Implementations ---------------------------------------------------------------------

static const struct dwt_spi_s spi_functions = { .readfromspi = readfromspi, .writetospi = writetospi,
   .writetospiwithcrc = NULL, .setslowrate = ranging_radio_spi_slow, .setfastrate = ranging_radio_spi_fast };
static const struct dwt_probe_s driver_interface = { .dw = NULL, .spi = (void*)&spi_functions, .wakeup_device_with_io = NULL };

decaIrqStatus_t decamutexon(void) { return (decaIrqStatus_t)am_hal_interrupt_master_disable(); }
void decamutexoff(decaIrqStatus_t status) { am_hal_interrupt_master_set((uint32_t)status); }
void deca_sleep(unsigned int time_ms) { am_hal_delay_us(time_ms * 1000); }
void deca_usleep(unsigned long time_us) { am_hal_delay_us(time_us); }


// Public API Functions ------------------------------------------------------------------------------------------------

void ranging_radio_init(uint8_t *uid)
{
   // Convert the device UID into the necessary 64-bit EUI format
   spi_ready = false;
   eui64_array[0] = uid[0]; eui64_array[1] = uid[1]; eui64_array[2] = uid[2];
   eui64_array[3] = 0xFE; eui64_array[4] = 0xFF;
   eui64_array[5] = uid[3]; eui64_array[6] = uid[4]; eui64_array[7] = uid[5];

   // Set up the DW3000 reset pin as a high-impedance output
   configASSERT0(am_hal_gpio_pinconfig(PIN_RADIO_RESET, am_hal_gpio_pincfg_tristate));
   am_hal_gpio_output_tristate_disable(PIN_RADIO_RESET);
   am_hal_gpio_output_clear(PIN_RADIO_RESET);

   // Set up the DW3000 wake-up pin as an output, initially set to low
   configASSERT0(am_hal_gpio_pinconfig(PIN_RADIO_WAKEUP, am_hal_gpio_pincfg_output));
   am_hal_gpio_output_clear(PIN_RADIO_WAKEUP);
#if REVISION_ID >= REVISION_L
   configASSERT0(am_hal_gpio_pinconfig(PIN_RADIO_WAKEUP2, am_hal_gpio_pincfg_output));
   am_hal_gpio_output_clear(PIN_RADIO_WAKEUP2);
   configASSERT0(am_hal_gpio_pinconfig(PIN_RADIO_WAKEUP3, am_hal_gpio_pincfg_output));
   am_hal_gpio_output_clear(PIN_RADIO_WAKEUP3);
#endif

   // Set up the DW3000 antenna selection pins
#if REVISION_ID < REVISION_L
   configASSERT0(am_hal_gpio_pinconfig(PIN_RADIO_ANTENNA_SELECT1, am_hal_gpio_pincfg_output));
   am_hal_gpio_output_clear(PIN_RADIO_ANTENNA_SELECT1);
   configASSERT0(am_hal_gpio_pinconfig(PIN_RADIO_ANTENNA_SELECT2, am_hal_gpio_pincfg_output));
   am_hal_gpio_output_clear(PIN_RADIO_ANTENNA_SELECT2);
#else
   configASSERT0(am_hal_gpio_pinconfig(PIN_RADIO_SPI_CS2, am_hal_gpio_pincfg_output));
   am_hal_gpio_output_set(PIN_RADIO_SPI_CS2);
   configASSERT0(am_hal_gpio_pinconfig(PIN_RADIO_SPI_CS3, am_hal_gpio_pincfg_output));
   am_hal_gpio_output_set(PIN_RADIO_SPI_CS3);
#endif

   // Set up incoming interrupts from the DW3000
   uint32_t radio_interrupt_pin = PIN_RADIO_INTERRUPT;
   am_hal_gpio_pincfg_t interrupt_pin_config = AM_HAL_GPIO_PINCFG_INPUT;
   interrupt_pin_config.GP.cfg_b.ePullup = AM_HAL_GPIO_PIN_PULLDOWN_50K;
   configASSERT0(am_hal_gpio_pinconfig(PIN_RADIO_INTERRUPT, interrupt_pin_config));
   configASSERT0(am_hal_gpio_interrupt_control(AM_HAL_GPIO_INT_CHANNEL_0, AM_HAL_GPIO_INT_CTRL_INDV_ENABLE, &radio_interrupt_pin));
   NVIC_SetPriority(GPIO0_001F_IRQn + GPIO_NUM2IDX(PIN_RADIO_INTERRUPT), NVIC_configMAX_SYSCALL_INTERRUPT_PRIORITY);
   NVIC_EnableIRQ(GPIO0_001F_IRQn + GPIO_NUM2IDX(PIN_RADIO_INTERRUPT));
#if REVISION_ID >= REVISION_L
   radio_interrupt_pin = PIN_RADIO_INTERRUPT2;
   am_hal_gpio_pincfg_t interrupt_pin_config = AM_HAL_GPIO_PINCFG_INPUT;
   interrupt_pin_config.GP.cfg_b.ePullup = AM_HAL_GPIO_PIN_PULLDOWN_50K;
   configASSERT0(am_hal_gpio_pinconfig(PIN_RADIO_INTERRUPT2, interrupt_pin_config));
   configASSERT0(am_hal_gpio_interrupt_control(AM_HAL_GPIO_INT_CHANNEL_0, AM_HAL_GPIO_INT_CTRL_INDV_ENABLE, &radio_interrupt_pin));
   //NVIC_SetPriority(GPIO0_001F_IRQn + GPIO_NUM2IDX(PIN_RADIO_INTERRUPT2), NVIC_configMAX_SYSCALL_INTERRUPT_PRIORITY);
   //NVIC_EnableIRQ(GPIO0_001F_IRQn + GPIO_NUM2IDX(PIN_RADIO_INTERRUPT2));
   radio_interrupt_pin = PIN_RADIO_INTERRUPT3;
   am_hal_gpio_pincfg_t interrupt_pin_config = AM_HAL_GPIO_PINCFG_INPUT;
   interrupt_pin_config.GP.cfg_b.ePullup = AM_HAL_GPIO_PIN_PULLDOWN_50K;
   configASSERT0(am_hal_gpio_pinconfig(PIN_RADIO_INTERRUPT3, interrupt_pin_config));
   configASSERT0(am_hal_gpio_interrupt_control(AM_HAL_GPIO_INT_CHANNEL_0, AM_HAL_GPIO_INT_CTRL_INDV_ENABLE, &radio_interrupt_pin));
   //NVIC_SetPriority(GPIO0_001F_IRQn + GPIO_NUM2IDX(PIN_RADIO_INTERRUPT3), NVIC_configMAX_SYSCALL_INTERRUPT_PRIORITY);
   //NVIC_EnableIRQ(GPIO0_001F_IRQn + GPIO_NUM2IDX(PIN_RADIO_INTERRUPT3));
#endif

   // Initialize the SPI module and enable all relevant SPI pins
   am_hal_gpio_pincfg_t sck_config = g_AM_BSP_GPIO_IOM0_SCK;
   am_hal_gpio_pincfg_t miso_config = g_AM_BSP_GPIO_IOM0_MISO;
   am_hal_gpio_pincfg_t mosi_config = g_AM_BSP_GPIO_IOM0_MOSI;
   am_hal_gpio_pincfg_t cs_config = g_AM_BSP_GPIO_IOM0_CS;
   sck_config.GP.cfg_b.uFuncSel = PIN_RADIO_SPI_SCK_FUNCTION;
   miso_config.GP.cfg_b.uFuncSel = PIN_RADIO_SPI_MISO_FUNCTION;
   mosi_config.GP.cfg_b.uFuncSel = PIN_RADIO_SPI_MOSI_FUNCTION;
   cs_config.GP.cfg_b.uFuncSel = PIN_RADIO_SPI_CS_FUNCTION;
   cs_config.GP.cfg_b.uNCE = 4 * RADIO_SPI_NUMBER;
   configASSERT0(am_hal_iom_initialize(RADIO_SPI_NUMBER, &spi_handle));
   configASSERT0(am_hal_gpio_pinconfig(PIN_RADIO_SPI_SCK, sck_config));
   configASSERT0(am_hal_gpio_pinconfig(PIN_RADIO_SPI_MISO, miso_config));
   configASSERT0(am_hal_gpio_pinconfig(PIN_RADIO_SPI_MOSI, mosi_config));
   configASSERT0(am_hal_gpio_pinconfig(PIN_RADIO_SPI_CS, cs_config));
   ranging_radio_spi_fast();

   // Reset and initialize the DW3000 radio
   ranging_radio_reset();
   dwt_setcallbacks(NULL, NULL, NULL, NULL, NULL, ranging_radio_spi_ready, NULL);
}

void ranging_radio_deinit(void)
{
   // Ensure that the radio is in deep sleep mode and disable all SPI communications
   ranging_radio_sleep(true);
   while (am_hal_iom_disable(spi_handle) != AM_HAL_STATUS_SUCCESS);
   am_hal_iom_uninitialize(spi_handle);

   // Disable all radio-based interrupts
   uint32_t radio_interrupt_pin = PIN_RADIO_INTERRUPT;
   NVIC_DisableIRQ(GPIO0_001F_IRQn + GPIO_NUM2IDX(PIN_RADIO_INTERRUPT));
   am_hal_gpio_interrupt_register(AM_HAL_GPIO_INT_CHANNEL_0, radio_interrupt_pin, NULL, (void*)radio_interrupt_pin);
   am_hal_gpio_interrupt_control(AM_HAL_GPIO_INT_CHANNEL_0, AM_HAL_GPIO_INT_CTRL_INDV_DISABLE, &radio_interrupt_pin);
}

void ranging_radio_reset(void)
{
   // Assert the DW3000 reset pin for 1us to manually reset the device
   am_hal_gpio_output_tristate_enable(PIN_RADIO_RESET);
   am_hal_delay_us(1);
   am_hal_gpio_output_tristate_disable(PIN_RADIO_RESET);
   am_hal_delay_us(2000);

   // Initialize the DW3000 driver and transceiver
   while (dwt_probe((struct dwt_probe_s*)&driver_interface) != DWT_SUCCESS)
      am_hal_delay_us(2000);
   configASSERT0(dwt_initialise(DWT_DW_IDLE));

   // Set up the DW3000 interrupts and overall configuration
   dw_config = (dwt_config_t){ .chan = 9, .txPreambLength = DW_PREAMBLE_LENGTH, .rxPAC = DW_PAC_SIZE,
      .txCode = 9, .rxCode = 9, .sfdType = DWT_SFD_IEEE_4Z, .dataRate = DW_DATA_RATE, .phrMode = DWT_PHRMODE_STD,
      .phrRate = DWT_PHRRATE_DTA, .sfdTO = DW_SFD_TO, .stsMode = DWT_STS_MODE_OFF, .stsLength = DWT_STS_LEN_32,
      .pdoaMode = DWT_PDOA_M0 };
   configASSERT0(dwt_configure(&dw_config));
   configASSERT0(am_hal_gpio_interrupt_register(AM_HAL_GPIO_INT_CHANNEL_0, PIN_RADIO_INTERRUPT, ranging_radio_isr, NULL));
   dwt_setinterrupt(DWT_INT_TXFRS_BIT_MASK | DWT_INT_RXFCG_BIT_MASK | DWT_INT_RXPHE_BIT_MASK |
         DWT_INT_RXFCE_BIT_MASK | DWT_INT_RXFSL_BIT_MASK | DWT_INT_RXFTO_BIT_MASK |
         DWT_INT_RXPTO_BIT_MASK | DWT_INT_RXSTO_BIT_MASK | DWT_INT_ARFE_BIT_MASK  |
         DWT_INT_SPIRDY_BIT_MASK, 0, DWT_ENABLE_INT_ONLY);
   dwt_writesysstatuslo(DWT_INT_RCINIT_BIT_MASK | DWT_INT_SPIRDY_BIT_MASK);
   dwt_configuretxrf((dwt_txconfig_t*)&tx_config_ch9);
   dwt_configciadiag(DW_CIA_DIAG_LOG_ALL);
   dwt_configmrxlut(9);

   // Set this node's PAN ID and EUI
   dwt_setpanid(MODULE_PANID);
   dwt_seteui(eui64_array);

   // Disable double-buffer mode, receive timeouts, and auto-ack mode
   dwt_setdblrxbuffmode(DBL_BUF_STATE_DIS, DBL_BUF_MODE_MAN);
   dwt_enableautoack(0, 0);
   dwt_setrxtimeout(0);

   // Set this device so that it only receives regular and extended data packets
   dwt_configureframefilter(DWT_FF_ENABLE_802_15_4, DWT_FF_DATA_EN);

   // Clear the internal TX/RX antenna delays
   dwt_settxantennadelay(0);
   dwt_setrxantennadelay(0);
}

void ranging_radio_register_callbacks(dwt_cb_t tx_done, dwt_cb_t rx_done, dwt_cb_t rx_timeout, dwt_cb_t rx_err)
{
   // Register the DW3000 interrupt event callbacks
   dwt_setcallbacks(tx_done, rx_done, rx_timeout, rx_err, NULL, ranging_radio_spi_ready, NULL);
}

void ranging_radio_choose_channel(uint8_t channel)
{
   // Only send commands to the radio if the channel was actually changed
   if ((channel != DO_NOT_CHANGE_FLAG) && (dw_config.chan != channel))
   {
      // Update the channel number and corresponding power configuration
      dw_config.chan = channel;
      dwt_configure(&dw_config);
      if (channel == 5)
         dwt_configuretxrf((dwt_txconfig_t*)&tx_config_ch5);
      else
         dwt_configuretxrf((dwt_txconfig_t*)&tx_config_ch9);
      dwt_configmrxlut(channel);
   }
}

void ranging_radio_choose_antenna(uint8_t antenna_number)
{
   // Enable the desired antenna
#if REVISION_ID < REVISION_L
   switch (antenna_number)
   {
      case 0:
         am_hal_gpio_output_clear(PIN_RADIO_ANTENNA_SELECT1);
         am_hal_gpio_output_set(PIN_RADIO_ANTENNA_SELECT2);
         break;
      case 1:
         am_hal_gpio_output_set(PIN_RADIO_ANTENNA_SELECT1);
         am_hal_gpio_output_clear(PIN_RADIO_ANTENNA_SELECT2);
         break;
      case 2:
         am_hal_gpio_output_set(PIN_RADIO_ANTENNA_SELECT1);
         am_hal_gpio_output_set(PIN_RADIO_ANTENNA_SELECT2);
         break;
      default:
         break;
   }
#endif
}

void ranging_radio_disable(void)
{
   // Turn off the radio
   dwt_forcetrxoff();
}

void ranging_radio_sleep(bool deep_sleep)
{
   // Disable all antennas
#if REVISION_ID < REVISION_L
   am_hal_gpio_output_clear(PIN_RADIO_ANTENNA_SELECT1);
   am_hal_gpio_output_clear(PIN_RADIO_ANTENNA_SELECT2);
#endif

   // Make sure the radio is disabled and clear the interrupt mask to disable unwanted events
   dwt_forcetrxoff();
   dwt_setinterrupt(DWT_INT_SPIRDY_BIT_MASK, 0, DWT_ENABLE_INT_ONLY);
   dwt_writesysstatuslo(DWT_INT_ALL_LO);

   // Put the DW3000 into sleep mode
   dwt_configuresleep(DWT_CONFIG | DWT_PGFCAL | DWT_LOADLDO | DWT_LOADDGC | DWT_LOADBIAS,
                      (deep_sleep ? 0 : DWT_SLEEP) | DWT_WAKE_WUP | DWT_SLP_EN);
   dwt_entersleep(DWT_DW_IDLE);
   spi_ready = false;
}

void ranging_radio_wakeup(void)
{
   // Assert the WAKEUP pin for >=500us and wait for it to become accessible
   wakeup_device_with_io();
   for (int i = 0; !spi_ready && (i < 100); ++i)
      am_hal_delay_us(20);
   if (!spi_ready)
   {
      print("WARNING: DW3000 radio could not be woken up...resetting peripheral\n");
      ranging_radio_reset();
   }
   else
   {
      // Restore configuration and re-enable allowable interrupts
      dwt_restoreconfig();
      dwt_setinterrupt(DWT_INT_TXFRS_BIT_MASK | DWT_INT_RXFCG_BIT_MASK | DWT_INT_RXPHE_BIT_MASK |
            DWT_INT_RXFCE_BIT_MASK | DWT_INT_RXFSL_BIT_MASK | DWT_INT_RXFTO_BIT_MASK |
            DWT_INT_RXPTO_BIT_MASK | DWT_INT_RXSTO_BIT_MASK | DWT_INT_ARFE_BIT_MASK  |
            DWT_INT_SPIRDY_BIT_MASK, 0, DWT_ENABLE_INT_ONLY);
   }
}

bool ranging_radio_rxenable(int mode)
{
   // Enable the receiver
   return (dwt_rxenable(mode) == DWT_SUCCESS);
}

uint64_t ranging_radio_readrxtimestamp(void)
{
   // Read the current DW3000 RX timestamp
   static uint64_t cur_dw_timestamp;
   dwt_readrxtimestamp((uint8_t*)&cur_dw_timestamp);
   return cur_dw_timestamp;
}

uint64_t ranging_radio_readtxtimestamp(void)
{
   // Read the current DW3000 TX timestamp
   static uint64_t cur_dw_timestamp;
   dwt_readtxtimestamp((uint8_t*)&cur_dw_timestamp);
   return cur_dw_timestamp;
}

float ranging_radio_received_signal_level(void)
{
   // Read the current RX diagnostics and compute the signal level in dBm
   static dwt_nlos_alldiag_t diagnostics;
   dwt_nlos_alldiag(&diagnostics);
   const float F1 = 0.25f * (float)diagnostics.F1, F2 = 0.25f * (float)diagnostics.F2, F3 = 0.25f * (float)diagnostics.F3;
   const float N = (float)diagnostics.accumCount, D = (float)diagnostics.D, A = 121.7f;
   return (10.0f * log10f((F1*F1 + F2*F2 + F3*F3) / (N*N))) + (6.0f * D) - A;
}

uint64_t ranging_radio_compute_correction_for_signal_level(float signal_level_dbm)
{
   const uint32_t signal_level_inverted = (uint32_t)(-signal_level_dbm);
   switch (signal_level_inverted)
   {
      case 61:
      case 62:
         return (uint64_t)(-0.110f / (SPEED_OF_LIGHT * DWT_TIME_UNITS));
      case 63:
      case 64:
         return (uint64_t)(-0.105f / (SPEED_OF_LIGHT * DWT_TIME_UNITS));
      case 65:
      case 66:
         return (uint64_t)(-0.100f / (SPEED_OF_LIGHT * DWT_TIME_UNITS));
      case 67:
      case 68:
         return (uint64_t)(-0.093f / (SPEED_OF_LIGHT * DWT_TIME_UNITS));
      case 69:
      case 70:
         return (uint64_t)(-0.082f / (SPEED_OF_LIGHT * DWT_TIME_UNITS));
      case 71:
      case 72:
         return (uint64_t)(-0.069f / (SPEED_OF_LIGHT * DWT_TIME_UNITS));
      case 73:
      case 74:
         return (uint64_t)(-0.051f / (SPEED_OF_LIGHT * DWT_TIME_UNITS));
      case 75:
      case 76:
         return (uint64_t)(-0.027f / (SPEED_OF_LIGHT * DWT_TIME_UNITS));
      case 77:
      case 78:
         return 0;
      case 79:
      case 80:
         return (uint64_t)(0.021f / (SPEED_OF_LIGHT * DWT_TIME_UNITS));
      case 81:
      case 82:
         return (uint64_t)(0.035f / (SPEED_OF_LIGHT * DWT_TIME_UNITS));
      case 83:
      case 84:
         return (uint64_t)(0.042f / (SPEED_OF_LIGHT * DWT_TIME_UNITS));
      case 85:
      case 86:
         return (uint64_t)(0.049f / (SPEED_OF_LIGHT * DWT_TIME_UNITS));
      case 87:
      case 88:
         return (uint64_t)(0.062f / (SPEED_OF_LIGHT * DWT_TIME_UNITS));
      case 89:
      case 90:
         return (uint64_t)(0.071f / (SPEED_OF_LIGHT * DWT_TIME_UNITS));
      case 91:
      case 92:
         return (uint64_t)(0.076f / (SPEED_OF_LIGHT * DWT_TIME_UNITS));
      case 93:
      case 94:
         return (uint64_t)(0.081f / (SPEED_OF_LIGHT * DWT_TIME_UNITS));
      default:
         return (signal_level_inverted < 61) ? (uint64_t)(-0.11f / (SPEED_OF_LIGHT * DWT_TIME_UNITS)) :
                                               (uint64_t)(0.081f / (SPEED_OF_LIGHT * DWT_TIME_UNITS));
   }
}

int ranging_radio_time_to_millimeters(double dwtime)
{
   return (int)((dwtime - RADIO_TX_PLUS_RX_DELAY) * SPEED_OF_LIGHT * DWT_TIME_UNITS * 1000.0);
}
