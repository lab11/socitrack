#ifndef __SQUAREPOINT_H
#define __SQUAREPOINT_H

#include "stm32f0xx.h"
#include "stm32f0xx_exti.h"
#include "stm32f0xx_gpio.h"
#include "stm32f0xx_syscfg.h"
#include "dw1000.h"

/******************************************************************************/
// EUI LOCATION IN FLASH
/******************************************************************************/
#define FLASH_LOCATION_MAGIC       0x0803FF00
#define FLASH_LOCATION_EUI         (FLASH_LOCATION_MAGIC + OFFSET_EUI)
#define FLASH_LOCATION_CALIBRATION (FLASH_LOCATION_MAGIC + OFFSET_CALIB)

/******************************************************************************/
// Clock settings
/******************************************************************************/
// High-Speed External Oscillator (HSE): 16 MHz
#ifndef HSE_VALUE
#define HSE_VALUE   16000000
#endif

#define DW_CLOCK_DRIFT_MAX_PPM  40

/******************************************************************************/
// LEDS
/******************************************************************************/
#define LEDn 3
#define LED_ON   Bit_RESET
#define LED_OFF  Bit_SET

// 1 tri-color LED = 3 one-color LEDs
#define STM_LED_RED_PIN     GPIO_Pin_3
#define STM_LED_RED_PORT    GPIOB

#define STM_LED_BLUE_PIN    GPIO_Pin_4
#define STM_LED_BLUE_PORT   GPIOB

#define STM_LED_GREEN_PIN   GPIO_Pin_5
#define STM_LED_GREEN_PORT  GPIOB

/******************************************************************************/
// DEBUGGING PINS
/******************************************************************************/
#define STM_SWDIO_PIN               GPIO_Pin_13
#define STM_SWDCLK_PIN              GPIO_Pin_14

/******************************************************************************/
// INTERRUPT TO HOST DEVICE
/********************************************************************************/
#define EXT_WAKEUP_PIN_ENABLED      0
#define EXT_INTERRUPT_PIN           GPIO_Pin_2
#define EXT_INTERRUPT_PORT          GPIOA
#define EXT_INTERRUPT_CLK           RCC_AHBPeriph_GPIOA
#define EXT_INTERRUPT_EXTI_LINE     EXTI_Line0
#define EXT_INTERRUPT_EXTI_IRQn     EXTI0_1_IRQn
#define EXT_INTERRUPT_EXTI_PORT     EXTI_PortSourceGPIOA
#define EXT_INTERRUPT_EXTI_PIN      EXTI_PinSource2

/******************************************************************************/
// I2C
/******************************************************************************/
#define I2C_TIMING  0x00100209

/******************************************************************************/
// SPI
/******************************************************************************/

#define SPI1_DR_ADDRESS                  0x4001300C
#define SPI1_RX_DMA_CHANNEL              DMA1_Channel2
#define SPI1_RX_DMA_FLAG_TC              DMA1_FLAG_TC2
#define SPI1_RX_DMA_FLAG_GL              DMA1_FLAG_GL2
#define SPI1_TX_DMA_CHANNEL              DMA1_Channel3
#define SPI1_TX_DMA_FLAG_TC              DMA1_FLAG_TC3
#define SPI1_TX_DMA_FLAG_GL              DMA1_FLAG_GL3
#define SPI1_DMA_IRQn                    DMA1_Channel2_3_IRQn

// See dw1000.c for remapping of UART1 to channel 4
#define USART1_DR_ADDRESS                0x40013828
#define USART1_TX_DMA_CHANNEL            DMA1_Channel4
#define USART1_TX_DMA_FLAG_TC            DMA1_FLAG_TC4
#define USART1_TX_DMA_FLAG_GL            DMA1_FLAG_GL4
#define USART1_DMA_IRQn                  DMA1_Channel4_IRQn

#define DMA1_CLK                         RCC_AHBPeriph_DMA1

#define SPI1_CLK                         RCC_APB2Periph_SPI1
#define SPI1_IRQn                        SPI1_IRQn
#define SPI1_IRQHandler                  SPI1_IRQHandler

#define SPI1_SCK_PIN                     GPIO_Pin_5
#define SPI1_SCK_GPIO_PORT               GPIOA
#define SPI1_SCK_GPIO_CLK                RCC_AHBPeriph_GPIOA
#define SPI1_SCK_SOURCE                  GPIO_PinSource5
#define SPI1_SCK_AF                      GPIO_AF_0

#define SPI1_MISO_PIN                    GPIO_Pin_6
#define SPI1_MISO_GPIO_PORT              GPIOA
#define SPI1_MISO_GPIO_CLK               RCC_AHBPeriph_GPIOA
#define SPI1_MISO_SOURCE                 GPIO_PinSource6
#define SPI1_MISO_AF                     GPIO_AF_0

#define SPI1_MOSI_PIN                    GPIO_Pin_7
#define SPI1_MOSI_GPIO_PORT              GPIOA
#define SPI1_MOSI_GPIO_CLK               RCC_AHBPeriph_GPIOA
#define SPI1_MOSI_SOURCE                 GPIO_PinSource7
#define SPI1_MOSI_AF                     GPIO_AF_0

#define SPI1_NSS_PIN                     GPIO_Pin_4
#define SPI1_NSS_GPIO_PORT               GPIOA
#define SPI1_NSS_GPIO_CLK                RCC_AHBPeriph_GPIOA
#define SPI1_NSS_SOURCE                  GPIO_PinSource4
#define SPI1_NSS_AF                      GPIO_AF_0

/******************************************************************************/
// INTERRUPT FROM DECAWAVE
/********************************************************************************/
#define DW_INTERRUPT_PIN        GPIO_Pin_2
#define DW_INTERRUPT_PORT       GPIOB
#define DW_INTERRUPT_CLK        RCC_AHBPeriph_GPIOB
#define DW_INTERRUPT_EXTI_LINE  EXTI_Line2
#define DW_INTERRUPT_EXTI_IRQn  EXTI2_3_IRQn
#define DW_INTERRUPT_EXTI_PORT  EXTI_PortSourceGPIOB
#define DW_INTERRUPT_EXTI_PIN   EXTI_PinSource2

/******************************************************************************/
// DECAWAVE RESET
/********************************************************************************/
#define DW_RESET_PIN    GPIO_Pin_0
#define DW_RESET_PORT   GPIOB
#define DW_RESET_CLK    RCC_AHBPeriph_GPIOB

/******************************************************************************/
// DECAWAVE WAKEUP
/*****************************************************************************/
#define DW_WAKEUP_PIN   GPIO_Pin_1
#define DW_WAKEUP_PORT  GPIOB
#define DW_WAKEUP_CLK   RCC_AHBPeriph_GPIOB

/*****************************************************************************/
// ANTENNA PINS
/*****************************************************************************/
#define ANT_SEL0_PIN	GPIO_Pin_14
#define ANT_SEL0_PORT	GPIOB
#define ANT_SEL0_CLK	RCC_AHBPeriph_GPIOB

#define ANT_SEL1_PIN	GPIO_Pin_13
#define ANT_SEL1_PORT	GPIOB
#define ANT_SEL1_CLK	RCC_AHBPeriph_GPIOB

#define ANT_SEL2_PIN	GPIO_Pin_12
#define ANT_SEL2_PORT	GPIOB
#define ANT_SEL2_CLK	RCC_AHBPeriph_GPIOB

/*****************************************************************************/
// MISC GPIOs
/*****************************************************************************/

// GPIO0 and GPIO1 allow for: I2C1, USART1
#define STM_GPIO0_PIN	GPIO_Pin_6
#define STM_GPIO0_PORT	GPIOB
#define STM_GPIO0_CLK	RCC_AHBPeriph_GPIOB
#define STM_GPIO0_SRC   GPIO_PinSource6

#define STM_GPIO1_PIN	GPIO_Pin_7
#define STM_GPIO1_PORT	GPIOB
#define STM_GPIO1_CLK	RCC_AHBPeriph_GPIOB
#define STM_GPIO1_SRC   GPIO_PinSource7

#define STM_GPIO3_PIN	GPIO_Pin_15
#define STM_GPIO3_PORT	GPIOB
#define STM_GPIO3_CLK	RCC_AHBPeriph_GPIOB

#endif /* __SQUAREPOINT_H */
