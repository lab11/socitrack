#include <string.h>
#include "stm32f0xx.h"
#include "stm32f0xx_spi.h"
#include "stm32f0xx_dma.h"
#include "stm32f0xx_exti.h"
#include "stm32f0xx_syscfg.h"
#include "stm32f0xx_misc.h"
#include "stm32f0xx_gpio.h"
#include "stm32f0xx_rcc.h"
#include "stm32f0xx_usart.h"

#include "deca_device_api.h"
#include "deca_regs.h"

#include "board.h"
#include "dw1000.h"
#include "delay.h"
#include "firmware.h"
#include "module_conf.h"
#include "board.h"
#include "led.h"
#include "SEGGER_RTT.h"

/******************************************************************************/
// Constants for the DW1000
/******************************************************************************/

const uint8_t pgDelay[DW1000_NUM_CHANNELS] = {
	0x0,
	0xc9,
	0xc2,
	0xc5,
	0x95,
	0xc0,
	0x0,
	0x93
};

//NOTE: THIS IS DEPENDENT ON BAUDRATE
// Max power: 0x1F1F1F1FUL
const uint32_t txPower_smartDisabled[DW1000_NUM_CHANNELS] = {
	0x0,
	0x67676767UL,
	0x67676767UL,
	0x8B8B8B8BUL,
	0x9A9A9A9AUL,
	0x85858585UL,
	0x0,
	0xD1D1D1D1UL
};

#ifndef DW1000_MAXIMIZE_TX_POWER
const uint32_t txPower_smartEnabled[DW1000_NUM_CHANNELS] = {
	0x0,
	//0x17375777UL,
	0x07274767UL,
	0x07274767UL,
	0x2B4B6B8BUL,
	0x3A5A7A9AUL,
	0x25456585UL,
	0x0,
	0x5171B1D1UL
};
#else
const uint32_t txPower_smartEnabled[DW1000_NUM_CHANNELS] = {
	0x0,
	0x1F1F1F1FUL,
	0x1F1F1F1FUL,
	0x1F1F1F1FUL,
	0x1F1F1F1FUL,
	0x1F1F1F1FUL,
	0x0,
	0x1F1F1F1FUL
};
#endif

/******************************************************************************/
// Data structures used in multiple functions
/******************************************************************************/

// These are for configuring the hardware peripherals on the STM32F0
static DMA_InitTypeDef DMA_InitStructure;
static DMA_InitTypeDef DMA_UART_InitStructure;
static SPI_InitTypeDef SPI_InitStructure;

// Setup TX/RX settings on the DW1000
static dwt_config_t   _dw1000_config;
static dwt_txconfig_t global_tx_config;

// Calibration values and other things programmed in with flash
static dw1000_programmed_values_t _prog_values;

static uint64_t _last_dw_timestamp;
static uint64_t _dw_timestamp_overflow;

/******************************************************************************/
// Internal state for this file
/******************************************************************************/

// Keep track of whether we have inited the STM hardware
static bool _stm_dw1000_interface_setup = FALSE;

// Whether or not interrupts are enabled.
decaIrqStatus_t dw1000_irq_onoff = 0;

// Whether the DW1000 is in SLEEP mode
static bool _dw1000_asleep = FALSE;


/******************************************************************************/
// STM32F0 Hardware setup functions
/******************************************************************************/

static void dw1000_interrupt_enable() {
	NVIC_InitTypeDef NVIC_InitStructure;

	// Enable and set EXTIx Interrupt
	NVIC_InitStructure.NVIC_IRQChannel = DW_INTERRUPT_EXTI_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPriority = 0x00;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);
}

static void dw1000_interrupt_disable() {
	NVIC_InitTypeDef NVIC_InitStructure;

	// Disable the EXTIx Interrupt
	NVIC_InitStructure.NVIC_IRQChannel = DW_INTERRUPT_EXTI_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPriority = 0x00;
	NVIC_InitStructure.NVIC_IRQChannelCmd = DISABLE;
	NVIC_Init(&NVIC_InitStructure);

}

// Configure SPI + GPIOs for SPI. Also preset some DMA constants.
static void setup () {

	GPIO_InitTypeDef GPIO_InitStructure;
	EXTI_InitTypeDef EXTI_InitStructure;

	// Enable the SPI peripheral
	RCC_APB2PeriphClockCmd(SPI1_CLK, ENABLE);

	// Enable the DMA peripheral
	RCC_AHBPeriphClockCmd(DMA1_CLK, ENABLE);

	// Enable SCK, MOSI, MISO and NSS GPIO clocks
	RCC_AHBPeriphClockCmd(SPI1_SCK_GPIO_CLK  |
	                      SPI1_MISO_GPIO_CLK |
	                      SPI1_MOSI_GPIO_CLK |
	                      SPI1_NSS_GPIO_CLK , ENABLE);

	// SPI pin mappings
	GPIO_PinAFConfig(SPI1_SCK_GPIO_PORT,  SPI1_SCK_SOURCE,  SPI1_SCK_AF);
	GPIO_PinAFConfig(SPI1_MOSI_GPIO_PORT, SPI1_MOSI_SOURCE, SPI1_MOSI_AF);
	GPIO_PinAFConfig(SPI1_MISO_GPIO_PORT, SPI1_MISO_SOURCE, SPI1_MISO_AF);

	// Configure SPI pins
	GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_NOPULL;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;

	// SPI SCK pin configuration
	GPIO_InitStructure.GPIO_Pin = SPI1_SCK_PIN;
	GPIO_Init(SPI1_SCK_GPIO_PORT, &GPIO_InitStructure);

	// SPI MOSI pin configuration
	GPIO_InitStructure.GPIO_Pin = SPI1_MOSI_PIN;
	GPIO_Init(SPI1_MOSI_GPIO_PORT, &GPIO_InitStructure);

	// SPI MISO pin configuration
	GPIO_InitStructure.GPIO_Pin = SPI1_MISO_PIN;
	GPIO_Init(SPI1_MISO_GPIO_PORT, &GPIO_InitStructure);

	// SPI NSS pin configuration
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStructure.GPIO_Pin  = SPI1_NSS_PIN;
	GPIO_Init(SPI1_NSS_GPIO_PORT, &GPIO_InitStructure);
	GPIO_WriteBit(SPI1_NSS_GPIO_PORT, SPI1_NSS_PIN, Bit_SET);

	// SPI configuration
	SPI_I2S_DeInit(SPI1);
	SPI_InitStructure.SPI_Direction         = SPI_Direction_2Lines_FullDuplex;
	SPI_InitStructure.SPI_DataSize          = SPI_DataSize_8b;
	SPI_InitStructure.SPI_CPOL              = SPI_CPOL_Low;
	SPI_InitStructure.SPI_CPHA              = SPI_CPHA_1Edge;
	SPI_InitStructure.SPI_NSS               = SPI_NSS_Soft;
	SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_32;
	SPI_InitStructure.SPI_FirstBit          = SPI_FirstBit_MSB;
	SPI_InitStructure.SPI_CRCPolynomial     = 7;
	SPI_InitStructure.SPI_Mode              = SPI_Mode_Master;
	SPI_Init(SPI1, &SPI_InitStructure);

	// Initialize the FIFO threshold
	// This is critical for 8 bit transfers
	SPI_RxFIFOThresholdConfig(SPI1, SPI_RxFIFOThreshold_QF);

	// Setup interrupt from the DW1000
	// Enable GPIOB clock
	RCC_AHBPeriphClockCmd(DW_INTERRUPT_CLK, ENABLE);

	// Configure PA0 pin as input floating
	GPIO_InitStructure.GPIO_Pin  = DW_INTERRUPT_PIN;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(DW_INTERRUPT_PORT, &GPIO_InitStructure);

	// Enable SYSCFG clock
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_SYSCFG, ENABLE);
	// Connect EXTIx Line to DW Int pin
	SYSCFG_EXTILineConfig(DW_INTERRUPT_EXTI_PORT, DW_INTERRUPT_EXTI_PIN);

	// Configure EXTIx line for interrupt
	EXTI_InitStructure.EXTI_Line    = DW_INTERRUPT_EXTI_LINE;
	EXTI_InitStructure.EXTI_Mode    = EXTI_Mode_Interrupt;
	EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;
	EXTI_InitStructure.EXTI_LineCmd = ENABLE;
	EXTI_Init(&EXTI_InitStructure);

	// Enable interrupt from the DW1000
	dw1000_interrupt_enable();
	dw1000_irq_onoff = 1;

	// Setup reset pin. Make it input unless we need it
	RCC_AHBPeriphClockCmd(DW_RESET_CLK, ENABLE);
	// Configure reset pin
	/*GPIO_InitStructure.GPIO_Pin = DW_RESET_PIN;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(DW_RESET_PORT, &GPIO_InitStructure);*/
	// Make the reset pin output
	GPIO_InitStructure.GPIO_Pin   = DW_RESET_PIN;
	GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_OUT;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_NOPULL;
	GPIO_Init(DW_RESET_PORT, &GPIO_InitStructure);
	GPIO_WriteBit(DW_RESET_PORT, DW_RESET_PIN, Bit_SET);

	// Setup wakeup pin.
	RCC_AHBPeriphClockCmd(DW_WAKEUP_CLK, ENABLE);
	GPIO_InitStructure.GPIO_Pin   = DW_WAKEUP_PIN;
	GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_OUT;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_NOPULL;
	GPIO_Init(DW_WAKEUP_PORT, &GPIO_InitStructure);
	GPIO_WriteBit(DW_WAKEUP_PORT, DW_WAKEUP_PIN, Bit_RESET);

	// Setup antenna pins - select no antennas
	RCC_AHBPeriphClockCmd(ANT_SEL0_CLK, ENABLE);
	RCC_AHBPeriphClockCmd(ANT_SEL1_CLK, ENABLE);
	RCC_AHBPeriphClockCmd(ANT_SEL2_CLK, ENABLE);

	GPIO_InitStructure.GPIO_Pin   = ANT_SEL0_PIN;
	GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_OUT;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_NOPULL;
	GPIO_Init(ANT_SEL0_PORT, &GPIO_InitStructure);

	GPIO_InitStructure.GPIO_Pin   = ANT_SEL1_PIN;
	GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_OUT;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_NOPULL;
	GPIO_Init(ANT_SEL1_PORT, &GPIO_InitStructure);

	GPIO_InitStructure.GPIO_Pin   = ANT_SEL2_PIN;
	GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_OUT;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_NOPULL;
	GPIO_Init(ANT_SEL2_PORT, &GPIO_InitStructure);

	// Initialize the RF Switch
	GPIO_WriteBit(ANT_SEL0_PORT, ANT_SEL0_PIN, Bit_SET);
	GPIO_WriteBit(ANT_SEL1_PORT, ANT_SEL1_PIN, Bit_RESET);
	GPIO_WriteBit(ANT_SEL2_PORT, ANT_SEL2_PIN, Bit_RESET);

	// Pre-populate DMA fields that don't need to change
	DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t) SPI1_DR_ADDRESS;
	DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
	DMA_InitStructure.DMA_MemoryDataSize     = DMA_MemoryDataSize_Byte;
	DMA_InitStructure.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
	DMA_InitStructure.DMA_Mode               = DMA_Mode_Normal;
	DMA_InitStructure.DMA_M2M                = DMA_M2M_Disable;

	// TODO: for STM32F091CC, this remap of USART1Tx might have to be done using DMA1->CSELR and hardware request 1000 on channel 4
	// DMA1->CSELR |= DMA1_CSELR_CH4_USART1_TX;
	/*SYSCFG->CFGR1 |= SYSCFG_DMARemap_USART1Tx;
	DMA_UART_InitStructure.DMA_PeripheralBaseAddr = (uint32_t) USART1_DR_ADDRESS;
	DMA_UART_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
	DMA_UART_InitStructure.DMA_MemoryDataSize     = DMA_MemoryDataSize_Byte;
	DMA_UART_InitStructure.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
	DMA_UART_InitStructure.DMA_Mode               = DMA_Mode_Normal;
	DMA_UART_InitStructure.DMA_M2M                = DMA_M2M_Disable;*/

	// Pull from flash the calibration values
	memcpy(&_prog_values, (uint8_t*) FLASH_LOCATION_MAGIC, sizeof(dw1000_programmed_values_t));
	if (_prog_values.magic != PROGRAMMED_MAGIC) {
		// Hmm this wasn't set on this chip. Not much we can do other than use default values.
		debug_msg("WARNING: Calibration values not found in FLASH\r\n");
		for (uint8_t i=0; i < 3; i++) {
			for (uint8_t j=0; j < 3; j++) {
				_prog_values.calibration_values[i][j] = DW1000_DEFAULT_CALIBRATION;
			}
		}
	} else {
	    debug_msg("INFO: Successfully loaded calibration values with EUI ");
	    debug_msg_uint(_prog_values.eui[0]);
	    debug_msg("\r\n");

        //dw1000_printCalibrationValues();
	}

	// Mark that this function has run so we don't do it again.
	_stm_dw1000_interface_setup = TRUE;
}

// Functions to configure the SPI speed
void dw1000_spi_fast () {
	SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_4;
	SPI_Init(SPI1, &SPI_InitStructure);
}

void dw1000_spi_slow () {
	SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_32;
	SPI_Init(SPI1, &SPI_InitStructure);
}

void uart_write(uint32_t length, const uint8_t* tx){
	DMA_UART_InitStructure.DMA_BufferSize = length;
	DMA_UART_InitStructure.DMA_MemoryBaseAddr = (uint32_t) tx;
	DMA_UART_InitStructure.DMA_DIR = DMA_DIR_PeripheralDST;
	DMA_UART_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
	DMA_UART_InitStructure.DMA_Priority = DMA_Priority_High;
	DMA_Init(USART1_TX_DMA_CHANNEL, &DMA_UART_InitStructure);

	USART_DMACmd(USART1, USART_DMAReq_Tx, ENABLE);
	DMA_Cmd(USART1_TX_DMA_CHANNEL, ENABLE);
	while (DMA_GetFlagStatus(USART1_TX_DMA_FLAG_TC) == RESET);
	DMA_ClearFlag(USART1_TX_DMA_FLAG_GL);
	DMA_Cmd(USART1_TX_DMA_CHANNEL, DISABLE);
	USART_DMACmd(USART1, USART_DMAReq_Tx, DISABLE);
}

void uart_write_message(uint32_t length, const char* msg){

	if (length == 0)
		return;

	// Start things off with a packet header
	//const uint8_t header[] = {0x80, 0x01, 0x80, 0x01};
	//uart_write(4, header);

    uart_write(length, (uint8_t*)msg);

	// Finish things off with a packet footer
	//const uint8_t footer[] = {0x80, 0xfe};
	//uart_write(2, footer);
}

void uart_write_debug(uint32_t length, const char* msg) {

#ifdef DEBUG_OUTPUT_UART
	uart_write_message(length, msg);
#endif

}

// Only write data to the DW1000, and use DMA to do it.
static void setup_dma_write (uint32_t length, const uint8_t* tx) {
	static uint8_t throwAway;

	DMA_InitStructure.DMA_BufferSize = length;
	DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t) &throwAway;
	DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC;
	DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Disable;
	DMA_InitStructure.DMA_Priority = DMA_Priority_High;
	DMA_Init(SPI1_RX_DMA_CHANNEL, &DMA_InitStructure);

	// DMA channel Tx of SPI Configuration
	DMA_InitStructure.DMA_BufferSize = length;
	DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t) tx;
	DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralDST;
	DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
	DMA_InitStructure.DMA_Priority = DMA_Priority_High;
	DMA_Init(SPI1_TX_DMA_CHANNEL, &DMA_InitStructure);
}

// Setup just a read over SPI using DMA.
static void setup_dma_read (uint32_t length, uint8_t* rx) {

	//volatile uint8_t throw = SPI1->DR;
	//throw = SPI1->SR;
	// DMA channel Rx of SPI Configuration
	DMA_InitStructure.DMA_BufferSize = length;
	DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t) rx;
	DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC;
	DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
	DMA_InitStructure.DMA_Priority = DMA_Priority_High;
	DMA_Init(SPI1_RX_DMA_CHANNEL, &DMA_InitStructure);

	static uint8_t tx = 0;
	// DMA channel Tx of SPI Configuration
	DMA_InitStructure.DMA_BufferSize = length;
	DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t) &tx;
	DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralDST;
	DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Disable;
	DMA_InitStructure.DMA_Priority = DMA_Priority_High;
	DMA_Init(SPI1_TX_DMA_CHANNEL, &DMA_InitStructure);
}

// Setup full duplex SPI over DMA.
// Commenting out for no compiler warning. May still want to try it out
// at some point.
// TODO: Determine if memcpy() plus 1 dma is better than no memcpy and
//       2 dma.
// static void setup_dma (uint32_t length, uint8_t* rx, uint8_t* tx) {

// 	// DMA channel Rx of SPI Configuration
// 	DMA_InitStructure.DMA_BufferSize = length;
// 	DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t) rx;
// 	DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC;
// 	DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
// 	DMA_InitStructure.DMA_Priority = DMA_Priority_High;
// 	DMA_Init(SPI1_RX_DMA_CHANNEL, &DMA_InitStructure);

// 	// DMA channel Tx of SPI Configuration
// 	DMA_InitStructure.DMA_BufferSize = length;
// 	DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t) tx;
// 	DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralDST;
// 	DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
// 	DMA_InitStructure.DMA_Priority = DMA_Priority_Low;
// 	DMA_Init(SPI1_TX_DMA_CHANNEL, &DMA_InitStructure);
// }


/******************************************************************************/
// Interrupt callbacks
/******************************************************************************/

// Not needed, but handle the interrupt from the SPI DMA
void DMA1_Channel2_3_IRQHandler(void) {
}


// HW interrupt for the interrupt pin from the DW1000
void EXTI2_3_IRQHandler (void) {
	if (EXTI_GetITStatus(DW_INTERRUPT_EXTI_LINE) != RESET) {
		// Mark this interrupt as had occurred in the main thread.
		mark_interrupt(INTERRUPT_DW1000);

		// Clear the EXTI line 2 pending bit
		EXTI_ClearITPendingBit(DW_INTERRUPT_EXTI_LINE);
	}
}

// Main thread interrupt handler for the interrupt from the DW1000. Basically
// just passes knowledge of the interrupt on to the DW1000 library.
void dw1000_interrupt_fired () {
	// Keep calling the decawave interrupt handler as long as the interrupt pin
	// is asserted, but add an escape hatch so we don't get stuck forever.
	uint8_t count = 0;
	do {
		dwt_isr();
		count++;
	} while (GPIO_ReadInputDataBit(DW_INTERRUPT_PORT, DW_INTERRUPT_PIN) &&
	         count < DW1000_NUM_CONSECUTIVE_INTERRUPTS_BEFORE_RESET);

	if (count >= DW1000_NUM_CONSECUTIVE_INTERRUPTS_BEFORE_RESET) {
		// Well this is not good. It looks like the interrupt got stuck high,
		// so we'd spend the rest of the time just reading this interrupt.
		// Not much we can do here but reset everything.
		module_reset();
	}
}

/******************************************************************************/
// Required API implementation for the DecaWave library
/******************************************************************************/

// Blocking SPI transfer
static int spi_transfer () {

	uint32_t loop = 0;

	DMA_Cmd(SPI1_RX_DMA_CHANNEL, ENABLE);
	DMA_Cmd(SPI1_TX_DMA_CHANNEL, ENABLE);

	// Wait for everything to finish
	while ((DMA_GetFlagStatus(SPI1_RX_DMA_FLAG_TC) == RESET) && loop < 100000) {
		loop++;
	};
	if (loop < 100000) {
		while ((DMA_GetFlagStatus(SPI1_TX_DMA_FLAG_TC) == RESET));
		/* The BSY flag can be monitored to ensure that the SPI communication is complete.
		This is required to avoid corrupting the last transmission before disabling
		the SPI or entering the Stop mode. The software must first wait until TXE=1
		and then until BSY=0.*/
		while ((SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_TXE) == RESET));
		while ((SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY) == SET));
	}

	// End the SPI transaction and DMA
	// Clear DMA1 global flags
	DMA_ClearFlag(SPI1_TX_DMA_FLAG_GL);
	DMA_ClearFlag(SPI1_RX_DMA_FLAG_GL);

	// Disable the DMA channels
	DMA_Cmd(SPI1_RX_DMA_CHANNEL, DISABLE);
	DMA_Cmd(SPI1_TX_DMA_CHANNEL, DISABLE);

	if (loop >= 100000) {
		return -1;
	} else {
		return 0;
	}
}

static void dma_enable() {

    // Enable NSS output for master mode
    SPI_SSOutputCmd(SPI1, ENABLE);

    // Enable DMA1 Channel1 Transfer Complete interrupt
    //DMA_ITConfig(SPI1_TX_DMA_CHANNEL, DMA_IT_TC, ENABLE);

    // Enable the DMA channels
    SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Rx, ENABLE);
    SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, ENABLE);

}

static void dma_disable() {

    // Disable the SPI Rx and Tx DMA requests
    SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Rx, DISABLE);
    SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, DISABLE);
}

// Called by the DW1000 library to issue a read command to the DW1000.
int readfromspi (uint16_t headerLength,
                 const uint8_t *headerBuffer,
                 uint32_t readlength,
                 uint8_t *readBuffer) {
	int ret;

	SPI_Cmd(SPI1, ENABLE);
	GPIO_WriteBit(SPI1_NSS_GPIO_PORT, SPI1_NSS_PIN, Bit_RESET);

    // Enable DMA for both of the two following transactions
    dma_enable();

	setup_dma_write(headerLength, headerBuffer);
	ret = spi_transfer();
	if (ret) goto error;

	setup_dma_read(readlength, readBuffer);
	ret = spi_transfer();
	if (ret) goto error;

    // Disable DMA again
	dma_disable();

	GPIO_WriteBit(SPI1_NSS_GPIO_PORT, SPI1_NSS_PIN, Bit_SET);
	SPI_Cmd(SPI1, DISABLE);
	return 0;

error:
    dma_disable();
	GPIO_WriteBit(SPI1_NSS_GPIO_PORT, SPI1_NSS_PIN, Bit_SET);
	SPI_Cmd(SPI1, DISABLE);
	module_reset();
	return -1;
}

// Called by the DW1000 library to issue a write to the DW1000.
int writetospi (uint16_t headerLength,
                const uint8_t *headerBuffer,
                uint32_t bodylength,
                const uint8_t *bodyBuffer) {
	int ret;

	SPI_Cmd(SPI1, ENABLE);
	GPIO_WriteBit(SPI1_NSS_GPIO_PORT, SPI1_NSS_PIN, Bit_RESET);

	// Enable DMA for both of the two following transactions
	dma_enable();

	setup_dma_write(headerLength, headerBuffer);
	ret = spi_transfer();
	if (ret) goto error;

	setup_dma_write(bodylength, bodyBuffer);
	ret = spi_transfer();
	if (ret) goto error;

	// Disable DMA again
	dma_disable();

	GPIO_WriteBit(SPI1_NSS_GPIO_PORT, SPI1_NSS_PIN, Bit_SET);
	SPI_Cmd(SPI1, DISABLE);
	return 0;

error:
    dma_disable();
	GPIO_WriteBit(SPI1_NSS_GPIO_PORT, SPI1_NSS_PIN, Bit_SET);
	SPI_Cmd(SPI1, DISABLE);
	module_reset();
	return -1;
}

// Atomic blocks for the DW1000 library
decaIrqStatus_t decamutexon () {
	if (dw1000_irq_onoff == 1) {
		dw1000_interrupt_disable();
		dw1000_irq_onoff = 0;
		return 1;
	}
	return 0;
}

// Atomic blocks for the DW1000 library
void decamutexoff (decaIrqStatus_t s) {
	if (s) {
		dw1000_interrupt_enable();
		dw1000_irq_onoff = 1;
	}
}

void deca_sleep(unsigned int time_ms) {
	// Expected to be blocking
	mDelay(time_ms);
}

// Rename this function for the DW1000 library.
void usleep (uint32_t u) {
	uDelay(u);
}


/******************************************************************************/
// Generic DW1000 functions - shared with anchor and tag
/******************************************************************************/

// Hard reset the DW1000 using its reset pin
void dw1000_reset_hard () {

	// To reset, assert the reset pin for 100ms
	GPIO_WriteBit(DW_RESET_PORT, DW_RESET_PIN, Bit_RESET);
	// Wait for ~100ms
	mDelay(100);
	GPIO_WriteBit(DW_RESET_PORT, DW_RESET_PIN, Bit_SET);

	_dw1000_asleep = FALSE;
}

// Soft reset the DW1000 using its internal registers
void dw1000_reset_soft () {

	uint8_t buffer;

	// Slow down DW1000 to talk
	dw1000_spi_slow();

	uDelay(1000);

	// Reset procedure for PMSC_CTRL0

	// 1. Set SYSCLKS to 01
	buffer = 1;
	dwt_writetodevice(PMSC_ID, PMSC_CTRL0_OFFSET,           1, &buffer);

	// 2. Clear SOFTRESET to all zeros
	buffer = 0x00;
	dwt_writetodevice(PMSC_ID, PMSC_CTRL0_SOFTRESET_OFFSET, 1, &buffer);

	// 3. Set SOFTRESET to all ones
	buffer = 0xF0;
	dwt_writetodevice(PMSC_ID, PMSC_CTRL0_SOFTRESET_OFFSET, 1, &buffer);

	// 4. Again enable auto-clock selection
	buffer = 0;
	dwt_writetodevice(PMSC_ID, PMSC_CTRL0_OFFSET,           1, &buffer);

	uDelay(1000);

	// Put the SPI back.
	dw1000_spi_fast();
}

// Choose which antenna to connect to the radio
void dw1000_choose_antenna (uint8_t antenna_number) {
	// Antenna selection comes from the STM32 chip instead of the DW1000 now

	// Set all of them low
	GPIO_WriteBit(ANT_SEL0_PORT, ANT_SEL0_PIN, Bit_RESET);
	GPIO_WriteBit(ANT_SEL1_PORT, ANT_SEL1_PIN, Bit_RESET);
	GPIO_WriteBit(ANT_SEL2_PORT, ANT_SEL2_PIN, Bit_RESET);

	// Set the one we want high
	switch (antenna_number) {
		case 0: GPIO_WriteBit(ANT_SEL0_PORT, ANT_SEL0_PIN, Bit_SET); break;
		case 1: GPIO_WriteBit(ANT_SEL1_PORT, ANT_SEL1_PIN, Bit_SET); break;
		case 2: GPIO_WriteBit(ANT_SEL2_PORT, ANT_SEL2_PIN, Bit_SET); break;
	}
}

// Read this node's EUI from the correct address in flash
void dw1000_read_eui (uint8_t *eui_buf) {
	memcpy(eui_buf, (uint8_t*) FLASH_LOCATION_EUI, EUI_LEN);
}

// Return the TX+RX delay calibration value for this particular node
// in DW1000 time format.
uint64_t dw1000_get_tx_delay (uint8_t channel_index, uint8_t antenna_index) {
	// Make sure that antenna and channel are 0<=index<3
	channel_index = channel_index % 3;
	antenna_index = antenna_index % 3;

	return (uint64_t) (_prog_values.calibration_values[channel_index][antenna_index] * DW1000_DELAY_TX) / 100;
}

uint64_t dw1000_get_rx_delay (uint8_t channel_index, uint8_t antenna_index) {
	// Make sure that antenna and channel are 0<=index<3
	channel_index = channel_index % 3;
	antenna_index = antenna_index % 3;

	return (uint64_t) (_prog_values.calibration_values[channel_index][antenna_index] * DW1000_DELAY_RX) / 100;
}

// Get access to the pointer of calibration values. Used for the host interface.
uint8_t* dw1000_get_txrx_delay_raw () {
	return (uint8_t*) _prog_values.calibration_values;
}

// First (generic) init of the DW1000
dw1000_err_e dw1000_init () {
	dw1000_err_e err = 0;

	// Do the STM setup that initializes pin and peripherals and whatnot.
	if (!_stm_dw1000_interface_setup) {
		setup();
	}

	// Reset the dw1000...for some reason
	dw1000_reset_soft();

	// Make sure the SPI clock is slow so that the DW1000 doesn't miss any edges.
	dw1000_spi_slow();

	// Make sure we can talk to the DW1000
	uint32_t devID;
	devID = dwt_readdevid();
	if (devID != DWT_DEVICE_ID) {
		//if we can't talk to dw1000, return with an error
		uDelay(1000);
		return DW1000_COMM_ERR;
	}

	// Choose antenna 0 as a default
	dw1000_choose_antenna(0);

	_last_dw_timestamp = 0;
	_dw_timestamp_overflow = 0;

#ifdef CW_TEST_MODE
	uint8_t buf[2];
	dwt_configcwmode(1);
	buf[0] = 0x61;
	dwt_writetodevice(FS_CTRL_ID, FS_XTALT_OFFSET, 1, buf);
	while(1){};
#endif
	// Setup our settings for the DW1000
	err = dw1000_configure_settings();
	if (err) return err;

	// Put the SPI back.
	dw1000_spi_fast();

	return DW1000_NO_ERR;
}

uint16_t dw1000_preamble_time_in_us(){
	//value is X*0.99359 for 16 MHz PRF and X*1.01763 for 64 MHz PRF
	uint16_t preamble_len;
	float preamble_time;
	switch(_dw1000_config.txPreambLength){
		case DWT_PLEN_64:
			preamble_len = 64; break;
		case DWT_PLEN_128:
			preamble_len = 128; break;
		case DWT_PLEN_256:
			preamble_len = 256; break;
		case DWT_PLEN_512:
			preamble_len = 512; break;
		case DWT_PLEN_1024:
			preamble_len = 1024; break;
		case DWT_PLEN_2048:
			preamble_len = 2048; break;
		default:
			preamble_len = 4096; break;
	}
	if(_dw1000_config.prf == DWT_PRF_16M)
		preamble_time = (float)preamble_len * 0.99359 + 0.5;
	else
		preamble_time = (float)preamble_len * 1.01763 + 0.5;
	return (uint16_t) preamble_time;
}

uint32_t dw1000_packet_data_time_in_us(uint16_t data_len){
	float time_per_byte;
	switch(_dw1000_config.dataRate){
		case DWT_BR_110K:
			time_per_byte = 8.0/110e3; break;
		case DWT_BR_850K:
			time_per_byte = 8.0/850e3; break;
		default: 
			time_per_byte = 8.0/6.8e6; break;
	}

	return (uint32_t) (time_per_byte * data_len * 1e6 + 0.5);
}

// Apply a suite of baseline settings that we care about.
// This is split out so we can call it after sleeping.
dw1000_err_e dw1000_configure_settings () {

	// Also need the SPI slow here.
	dw1000_spi_slow();

	// Initialize the dw1000 hardware
	uint32_t err;
	err = dwt_initialise(DWT_LOADUCODE);

	if (err != DWT_SUCCESS) {
		return DW1000_COMM_ERR;
	}

#ifdef  DW1000_WAKEUP_CS
	// Configure sleep parameters.
	// Note: This is taken from the decawave fast2wr_t.c file. I don't have
	//       a great idea as to whether this is right or not.
	dwt_configuresleep(DWT_PRESRV_SLEEP |
	                   DWT_LOADOPSET |
	                   DWT_CONFIG |
	                   DWT_LOADEUI,
	                   DWT_WAKE_CS | DWT_SLP_EN);
#else
    dwt_configuresleep(DWT_PRESRV_SLEEP |
	                   DWT_LOADOPSET |
	                   DWT_CONFIG |
	                   DWT_LOADEUI,
	                   DWT_WAKE_WK | DWT_SLP_EN);
#endif

	// Configure interrupts
	dwt_setinterrupt(0xFFFFFFFF, 0);
	dwt_setinterrupt(DWT_INT_TFRS |
	                 DWT_INT_RFCG |
	                 DWT_INT_RPHE |
	                 DWT_INT_RFCE |
	                 DWT_INT_RFSL |
	                 DWT_INT_RFTO |
	                 DWT_INT_RXPTO |
	                 DWT_INT_SFDT |
	                 DWT_INT_ARFE, 1);

	// Set the parameters of ranging and channel and whatnot
	_dw1000_config.chan           = 1;
	_dw1000_config.prf            = DWT_PRF_64M;
	_dw1000_config.txPreambLength = DW1000_PREAMBLE_LENGTH;
	_dw1000_config.rxPAC          = DW1000_PAC_SIZE;
	_dw1000_config.txCode         = 9;  // preamble code
	_dw1000_config.rxCode         = 9;  // preamble code
	_dw1000_config.nsSFD          = 0;
	_dw1000_config.dataRate       = DW1000_DATA_RATE;
	_dw1000_config.phrMode        = DWT_PHRMODE_EXT; //Enable extended PHR mode (up to 1024-byte packets)
	_dw1000_config.sfdTO          = DW1000_SFD_TO;
	_dw1000_config.smartPowerEn   = DW1000_SMART_PWR_EN;

	dwt_configure(&_dw1000_config);

	dwt_setsmarttxpower(_dw1000_config.smartPowerEn);

	// Configure TX power based on the channel used
	global_tx_config.PGdly = pgDelay[_dw1000_config.chan];

	if (_dw1000_config.smartPowerEn) {
		global_tx_config.power = txPower_smartEnabled[_dw1000_config.chan];
	} else {
		global_tx_config.power = txPower_smartDisabled[_dw1000_config.chan];
	}

	dwt_configuretxrf(&global_tx_config);

	// Need to set some radio properties. Ideally these would come from the
	// OTP memory on the DW1000
#if DW1000_USE_OTP == 0

	// This defaults to 8. Don't know why.
	dwt_setxtaltrim(DW1000_DEFAULT_XTALTRIM);

	// Antenna delay we don't really care about so we just use 0
	dwt_setrxantennadelay(DW1000_ANTENNA_DELAY_RX);
	dwt_settxantennadelay(DW1000_ANTENNA_DELAY_TX);
#endif

	// Set this node's ID and the PAN ID for our DW1000 ranging system
	uint8_t eui_array[8];
	dw1000_read_eui(eui_array);

	dwt_seteui(eui_array);
	dwt_setpanid(MODULE_PANID);

	// Always good to make sure we don't trap the SPI speed too slow
	dw1000_spi_fast();

	return DW1000_NO_ERR;
}

// Put the DW1000 into sleep mode
void dw1000_sleep () {
	if (_dw1000_asleep) {
		// The chip is already in SLEEP
		return;
	}

	// Don't need the DW1000 to be in TX or RX mode
	dwt_forcetrxoff();

	// Put the TAG into sleep mode at this point.
	// The chip will need to come out of sleep mode
	dwt_entersleep();

	// Mark that we put the DW1000 to sleep.
	_dw1000_asleep = TRUE;
}

// Wake the DW1000 from sleep by asserting the WAKEUP pin
dw1000_err_e dw1000_wakeup () {

	if (!_dw1000_asleep) {
		// The chip is already awake
		return DW1000_NO_ERR;
	}


#ifdef DW1000_WAKEUP_CS
    // Dummy buffer for DW1000 wake-up SPI read - must write for at least 500us
    #define DUMMY_BUFFER_LEN 600
    static uint8 dummy_buffer[DUMMY_BUFFER_LEN];

	// Read for >500us so that the SPI select line is low and the chip will wake up
	dwt_spicswakeup(dummy_buffer, DUMMY_BUFFER_LEN);
#else
	// Assert the WAKEUP pin. There seems to be some weirdness where a single
	// WAKEUP assert can get missed, so we do it multiple times to make
	// sure the DW1000 is awake.
	GPIO_WriteBit(DW_WAKEUP_PORT, DW_WAKEUP_PIN, Bit_SET);
	uDelay(1000);
	GPIO_WriteBit(DW_WAKEUP_PORT, DW_WAKEUP_PIN, Bit_RESET);
	uDelay(100);
	GPIO_WriteBit(DW_WAKEUP_PORT, DW_WAKEUP_PIN, Bit_SET);
	uDelay(1000);
	GPIO_WriteBit(DW_WAKEUP_PORT, DW_WAKEUP_PIN, Bit_RESET);
	uDelay(100);
	GPIO_WriteBit(DW_WAKEUP_PORT, DW_WAKEUP_PIN, Bit_SET);
	uDelay(1000);
	GPIO_WriteBit(DW_WAKEUP_PORT, DW_WAKEUP_PIN, Bit_RESET);
	uDelay(100);
	GPIO_WriteBit(DW_WAKEUP_PORT, DW_WAKEUP_PIN, Bit_SET);
	uDelay(1000);
	GPIO_WriteBit(DW_WAKEUP_PORT, DW_WAKEUP_PIN, Bit_RESET);
#endif

	// Now wait for 5ms for the chip to move from the wakeup to the idle
	// state. The datasheet says 4ms, but we buffer a little in case things
	// take longer or mDelay isn't exact.
	mDelay(5);

	// Slow SPI and wait for the DW1000 to respond.
	dw1000_spi_slow();
	uint32_t devID;
	uint8_t tries = 0;
	do {
		devID = dwt_readdevid();
		if (devID != DWT_DEVICE_ID) {
			//if we can't talk to dw1000, try again
			uDelay(100);
			tries++;
		}
	} while ((devID != DWT_DEVICE_ID) && (tries <= DW1000_NUM_CONTACT_TRIES_BEFORE_RESET));

	if (tries > DW1000_NUM_CONTACT_TRIES_BEFORE_RESET) {
		// Something went wrong with wakeup. In theory, this won't happen,
		// but having an unterminated while() loop is probably bad, so we
		// have this escape hatch. At this point I have no idea what went wrong,
		// but we probably need to reset the chip at this point.
		return DW1000_WAKEUP_ERR;
	}

	// No longer asleep
	_dw1000_asleep = FALSE;

	// Go back fast again
	dw1000_spi_fast();

	// This puts all of the settings back on the DW1000. In theory it
	// is capable of remembering these, but that doesn't seem to work
	// very well. This does work, so we do it and move on.
	dw1000_configure_settings();

	return DW1000_WAKEUP_SUCCESS;
}

dw1000_err_e dw1000_force_wakeup () {

    // Disable check
    _dw1000_asleep = TRUE;

    // Now wake up
    return dw1000_wakeup();
}

// Call to change the DW1000 channel and force set all of the configs
// that are needed when changing channels.
void dw1000_update_channel (uint8_t chan) {
	_dw1000_config.chan = chan;
	dw1000_reset_configuration();
}

// Called when dw1000 tx/rx config settings and constants should be re applied
void dw1000_reset_configuration () {

	dwt_configure(&_dw1000_config);

	// Custom function: only reset channel related information
	//dwt_setchannel(&_dw1000_config, 0);

	dwt_setsmarttxpower(_dw1000_config.smartPowerEn);
	global_tx_config.PGdly = pgDelay[_dw1000_config.chan];

	if (_dw1000_config.smartPowerEn) {
		global_tx_config.power = txPower_smartEnabled[_dw1000_config.chan];
	} else {
		global_tx_config.power = txPower_smartDisabled[_dw1000_config.chan];
	}

	dwt_configuretxrf(&global_tx_config);
#if DW1000_USE_OTP == 0
	dwt_setrxantennadelay(DW1000_ANTENNA_DELAY_RX);
	dwt_settxantennadelay(DW1000_ANTENNA_DELAY_TX);
#endif
}

// Called to go get information on the current status of the DW
uint32_t dw1000_get_status_register () {

	uint32_t status = (uint32_t) dwt_read32bitreg(SYS_STATUS_ID); // Read status register low 32bits

	debug_msg("\n-----------------------\r\n");
	debug_msg("INFO: Status register: ");
	debug_msg_uint(status);
	debug_msg("\n-----------------------\r\n\r\n");

	return status;
}


/******************************************************************************/
// Decawave specific utility functions
/******************************************************************************/

// Convert a time of flight measurement to millimeters
int dwtime_to_millimeters (double dwtime) {
	// Get meters using the speed of light
	double dist = dwtime * DWT_TIME_UNITS * SPEED_OF_LIGHT;

	// And return millimeters
	return (int) (dist*1000.0);
}


/******************************************************************************/
// Misc Utility
/******************************************************************************/

// Shoved this here for now.
// Insert an element into a sorted array.
// end is the number of elements in the array.
void insert_sorted (int arr[], int new, unsigned end) {
	unsigned insert_at = 0;
	while ((insert_at < end) && (new >= arr[insert_at])) {
		insert_at++;
	}
	if (insert_at == end) {
		arr[insert_at] = new;
	} else {
		while (insert_at <= end) {
			int temp = arr[insert_at];
			arr[insert_at] = new;
			new = temp;
			insert_at++;
		}
	}
}

// Get linearly increasing 40bit timestamps; ATTENTION: when comparing to the current time (32bit), make sure to cast it to uint64_t and shift it so it corresponds to a 40bit timestamp
uint64_t dw1000_correct_timestamp(uint64_t dw_timestamp) {

    // Due to frequent overflow in the Decawave system time counter, we must keep a running total of the number of times it's overflown
    if(dw_timestamp < _last_dw_timestamp) {
        _dw_timestamp_overflow += 0x10000000000ULL;
        //debug_msg("INFO: Overflow 1 in DecaWave clock\r\n");
    }
    _last_dw_timestamp = dw_timestamp;

    return _dw_timestamp_overflow + dw_timestamp;
}

uint64_t dw1000_readrxtimestamp() {

	uint64_t cur_dw_timestamp = 0;
	dwt_readrxtimestamp(&cur_dw_timestamp);
	
	// Check to see if an overflow has occurred.
	if(cur_dw_timestamp < _last_dw_timestamp){
		_dw_timestamp_overflow += 0x10000000000ULL;
        //debug_msg("INFO: Overflow 2 in DecaWave clock\r\n");
	}
	_last_dw_timestamp = cur_dw_timestamp;

	return _dw_timestamp_overflow + cur_dw_timestamp;
}

uint64_t dw1000_setdelayedtrxtime(uint32_t delay_time){
	uint64_t cur_dw_timestamp = ((uint64_t) delay_time) << 8;
	
	// Check to see if an overflow has occurred.
	if(cur_dw_timestamp < _last_dw_timestamp){
		_dw_timestamp_overflow += 0x10000000000ULL;
        //debug_msg("INFO: Overflow 3 in DecaWave clock\r\n");
	}
	_last_dw_timestamp = cur_dw_timestamp;
	
	dwt_setdelayedtrxtime(delay_time);

	return _dw_timestamp_overflow + cur_dw_timestamp;
}

uint64_t dw1000_gettimestampoverflow(){
	return _dw_timestamp_overflow;
}

void dw1000_calculatediagnostics(){

    // Estimate First path and Rx signal strength; see User Manual chapter 4.7
    //uint32_t A_16MHz = 11377 / 100;
    //uint32_t A_64MHz = 12174 / 100;

    // Get data
    dwt_rxdiag_t diagnostics = {0};
    dwt_readdiagnostics(&diagnostics);

    // Print variables
    /*debug_msg("DEBUG: Printing diagnostics values:\r\n");
    debug_msg_uint(diagnostics.firstPathAmp1);
    debug_msg("\r\n");
    debug_msg_uint(diagnostics.firstPathAmp2);
    debug_msg("\r\n");
    debug_msg_uint(diagnostics.firstPathAmp3);
    debug_msg("\r\n");
    debug_msg_uint(diagnostics.rxPreamCount);
    debug_msg("\r\n");
    debug_msg_uint(diagnostics.maxGrowthCIR);
    debug_msg("\r\n");*/

    uint32_t N_squared = (uint32_t)diagnostics.rxPreamCount * (uint32_t)diagnostics.rxPreamCount;

    // First path signal (leading edge -> LOS path)
    uint32_t fp_signal  = (uint32_t)diagnostics.firstPathAmp1 * (uint32_t)diagnostics.firstPathAmp1;
             fp_signal += (uint32_t)diagnostics.firstPathAmp2 * (uint32_t)diagnostics.firstPathAmp2;
             fp_signal += (uint32_t)diagnostics.firstPathAmp3 * (uint32_t)diagnostics.firstPathAmp3;

    // Rx signal
    uint32_t rx_signal  = (uint32_t)diagnostics.maxGrowthCIR;
             rx_signal *= (uint32_t)(0x1 << 17);

    // Calculate dBm
    //int fp_signal_dBm = (uint32_t)(10 * log10((double)fp_signal / N_squared)) - A_64MHz;
    //int rx_signal_dBm = (uint32_t)(10 * log10((double)rx_signal / N_squared)) - A_64MHz;

    debug_msg("DEBUG: Estimated signal strength: First path - ");
    debug_msg_uint(fp_signal / N_squared);
    //debug_msg_int(fp_signal_dBm);
    debug_msg(" ; Rx - ");
    debug_msg_uint(rx_signal / N_squared);
    //debug_msg_int(rx_signal_dBm);
    debug_msg("\r\n");
}

void dw1000_printCalibrationValues(){

    uint16_t* calibration_values = (uint16_t*)dw1000_get_txrx_delay_raw();

    const uint8_t nr_channels = 3;
    const uint8_t nr_antennas = 3;
    uint8_t i,j;

    debug_msg("DEBUG: Calibration values are:\n");
    for (i = 0; i < nr_channels; i++) {
        debug_msg("DEBUG: ");
        for (j = 0; j < nr_antennas; j++) {
            debug_msg_int(_prog_values.calibration_values[i][j]);
            debug_msg("\t\t");
        }
        debug_msg("\n");
    }
}
