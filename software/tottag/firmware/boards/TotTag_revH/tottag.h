// Pin definitions for TotTag Revision H

#pragma once

#ifndef BOARD_V
#define BOARD_V 0x11
#endif

#ifndef DEVICE_NAME
#define DEVICE_NAME "TotTag"
#endif

// Battery
#define BATTERY_PIN                  NRF_SAADC_INPUT_AIN2
#define BATTERY_MONITOR              NRF_GPIO_PIN_MAP(0,4)

// Battery Charger
#define CHARGER_INPUT_POWER_GOOD     NRF_GPIO_PIN_MAP(1,3)
#define CHARGER_CHARGING_STATUS      NRF_GPIO_PIN_MAP(1,2)
#define CHARGER_COMPLETE_SIGNAL      NRF_GPIO_PIN_MAP(0,25)

// Buzzer
#define BUZZER_DRIVER                NRF_GPIO_PIN_MAP(1,15)

// GPIOs
#define TEST_POINT_GPIO_0            NRF_GPIO_PIN_MAP(0,27)

// Inertial Measurement Unit
#define IMU_SPI_BUS_IDX              0
#define IMU_SPI_CS                   NRF_GPIO_PIN_MAP(0,16)
#define IMU_SPI_SCLK                 NRF_GPIO_PIN_MAP(0,20)
#define IMU_SPI_MISO                 NRF_GPIO_PIN_MAP(0,24)
#define IMU_SPI_MOSI                 NRF_GPIO_PIN_MAP(0,22)
#define IMU_INT1                     NRF_GPIO_PIN_MAP(0,17)
#define IMU_INT2                     NRF_GPIO_PIN_MAP(0,15)
#define MAGNETOMETER_INT             NRF_GPIO_PIN_MAP(0,23)
#define MAGNETOMETER_DRDY            NRF_GPIO_PIN_MAP(1,0)

// LEDs
#define LED_RED                      NRF_GPIO_PIN_MAP(1,4)
#define LED_BLUE                     NRF_GPIO_PIN_MAP(1,7)
#define LED_GREEN                    NRF_GPIO_PIN_MAP(1,5)

// Real-Time Clock
#define RTC_SPI_BUS_IDX              2
#define RTC_SPI_CS                   NRF_GPIO_PIN_MAP(0,29)
#define RTC_SPI_SCLK                 NRF_GPIO_PIN_MAP(0,6)
#define RTC_SPI_MISO                 NRF_GPIO_PIN_MAP(0,5)
#define RTC_SPI_MOSI                 NRF_GPIO_PIN_MAP(0,7)
#define RTC_INT                      NRF_GPIO_PIN_MAP(0,2)
#define RTC_WDI                      NRF_GPIO_PIN_MAP(0,31)

// SD Card
#define SD_CARD_SPI_BUS_IDX          RTC_SPI_BUS_IDX
#define SD_CARD_ENABLE               NRF_GPIO_PIN_MAP(0,14)
#define SD_CARD_DETECT               NRF_GPIO_PIN_MAP(0,11)
#define SD_CARD_SPI_CS               NRF_GPIO_PIN_MAP(0,13)
#define SD_CARD_SPI_MISO             RTC_SPI_MISO
#define SD_CARD_SPI_MOSI             RTC_SPI_MOSI
#define SD_CARD_SPI_SCLK             RTC_SPI_SCLK

// STM controller lines
#define STM_INTERRUPT                NRF_GPIO_PIN_MAP(0,8)
#define STM_WAKEUP                   NRF_GPIO_PIN_MAP(0,26)
#define STM_I2C_SCL                  NRF_GPIO_PIN_MAP(1,9)
#define STM_I2C_SDA                  NRF_GPIO_PIN_MAP(0,12)

// Unused GPIO pins
#define UNUSED_GPIO_PINS {{0,3},{0,9},{0,10},{0,19},{0,21},{0,27},{0,28},{0,30},{1,1},{1,6},{1,8},{1,10},{1,11},{1,12},{1,13},{1,14}}
