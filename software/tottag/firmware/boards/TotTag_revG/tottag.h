// Pin definitions for TotTag Revision G

#pragma once

#ifndef BOARD_V
#define BOARD_V 0x10
#endif

#include "nrfx_gpiote.h"
#include "nrfx_saadc.h"

#ifndef DEVICE_NAME
#define DEVICE_NAME "TotTag"
#endif

// BLE Address used to advertise
#ifndef BLE_ADDRESS
#error "Did not specify a BLE address...cannot continue"
#endif

// Battery monitor
#define CARRIER_BATTERY_MONITOR      NRF_GPIO_PIN_MAP(0,30)
#define CARRIER_BATTERY_PIN          NRF_SAADC_INPUT_AIN6

// Charging monitor
#define CHARGER_INPUT_POWER_GOOD     NRF_GPIO_PIN_MAP(1,3)
#define CHARGER_CHARGING_STATUS      NRF_GPIO_PIN_MAP(1,2)
#define CHARGER_COMPLETE_SIGAL       NRF_GPIO_PIN_MAP(0,25)

// Buzzer
#define BUZZER_DRIVER       NRF_GPIO_PIN_MAP(1,15)

// GPIOs
#define CARRIER_GPIO_0      NRF_GPIO_PIN_MAP(0,27)
#define CARRIER_GPIO_1      NRF_GPIO_PIN_MAP(0,26)

// LEDs
#define CARRIER_LED_RED     NRF_GPIO_PIN_MAP(1,4)
#define CARRIER_LED_BLUE    NRF_GPIO_PIN_MAP(1,7)
#define CARRIER_LED_GREEN   NRF_GPIO_PIN_MAP(1,5)

// SPI bus
#define CARRIER_SPI_SCLK    NRF_GPIO_PIN_MAP(0,17)
#define CARRIER_SPI_MISO    NRF_GPIO_PIN_MAP(0,15)
#define CARRIER_SPI_MOSI    NRF_GPIO_PIN_MAP(0,13)

// SD Card
#define CARRIER_CS_SD       NRF_GPIO_PIN_MAP(0,20)
#define CARRIER_SD_ENABLE   NRF_GPIO_PIN_MAP(0,14)
#define CARRIER_SD_DETECT   NRF_GPIO_PIN_MAP(0,11)

// To use "simple_logger", we define some additional macros
#define SD_CARD_ENABLE          CARRIER_SD_ENABLE
#define SD_CARD_DETECT          CARRIER_SD_DETECT
#define SD_CARD_SPI_CS          CARRIER_CS_SD
#define SD_CARD_SPI_MISO        CARRIER_SPI_MISO
#define SD_CARD_SPI_MOSI        CARRIER_SPI_MOSI
#define SD_CARD_SPI_SCLK        CARRIER_SPI_SCLK
#define SD_CARD_SPI_INSTANCE    NRF_SPI0

// Accelerometer
#define CARRIER_CS_ACC      NRF_GPIO_PIN_MAP(0,16)
#define CARRIER_ACC_INT1    NRF_GPIO_PIN_MAP(0,22)
#define CARRIER_ACC_INT2    NRF_GPIO_PIN_MAP(0,24)

// Real Time Clock (RTC)
#define CARRIER_CS_RTC      NRF_GPIO_PIN_MAP(0,29)
#define CARRIER_RTC_INT     NRF_GPIO_PIN_MAP(0,2)
#define CARRIER_RTC_WDI     NRF_GPIO_PIN_MAP(0,31)

// I2C connection to module
#define CARRIER_I2C_SCL     NRF_GPIO_PIN_MAP(1,9)
#define CARRIER_I2C_SDA     NRF_GPIO_PIN_MAP(0,12)

// Interrupt line to module (STM controller)
#define CARRIER_INTERRUPT_MODULE    NRF_GPIO_PIN_MAP(0,8)

// Unused GPIO pins
#define UNUSED_GPIO_PINS {{0,3},{0,4},{0,5},{0,6},{0,7},{0,9},{0,10},{0,19},{0,21},{0,23},{0,26},{0,27},{0,28},{1,0},{1,1},{1,6},{1,8},{1,10},{1,11},{1,12},{1,13},{1,14}}
