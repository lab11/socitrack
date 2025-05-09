#ifndef __PINOUT_HEADER_H__
#define __PINOUT_HEADER_H__

// Hardware Revision ID
#include "revisions.h"
#define REVISION_ID                                 REVISION_APOLLO4_EVB

// Battery
#define PIN_BATTERY_VOLTAGE                         18
#define PIN_BATTERY_VOLTAGE_FUNCTION                AM_HAL_PIN_18_ADCSE1
#define PIN_BATTERY_VOLTAGE_ADC_CHANNEL             AM_HAL_ADC_SLOT_CHSEL_SE1
#define PIN_BATTERY_CHARGING_STATUS                 25
#define PIN_BATTERY_INPUT_POWER_GOOD                26
#define PIN_BATTERY_CHARGE_COMPLETE_SIGNAL          27
#define VOLTAGE_DIVIDER_LOWER                       33
#define VOLTAGE_DIVIDER_UPPER                       51

// Buttons
#define PIN_BUTTON_1                                AM_BSP_GPIO_BUTTON0
#define PIN_BUTTON_2                                AM_BSP_GPIO_BUTTON1

// Buzzer
#define PIN_BUZZER_DRIVER                           28

// DecaWave Radio
#define RADIO_SPI_NUMBER                            6
#define PIN_RADIO_SPI_SCK                           61
#define PIN_RADIO_SPI_MISO                          63
#define PIN_RADIO_SPI_MOSI                          62
#define PIN_RADIO_SPI_CS                            47
#define PIN_RADIO_INTERRUPT                         48
#define PIN_RADIO_RESET                             53
#define PIN_RADIO_WAKEUP                            49
#define PIN_RADIO_ANTENNA_SELECT1                   25
#define PIN_RADIO_ANTENNA_SELECT2                   26
#define PIN_RADIO_SPI_SCK_FUNCTION                  AM_HAL_PIN_61_M6SCK
#define PIN_RADIO_SPI_MISO_FUNCTION                 AM_HAL_PIN_63_M6MISO
#define PIN_RADIO_SPI_MOSI_FUNCTION                 AM_HAL_PIN_62_M6MOSI
#define PIN_RADIO_SPI_CS_FUNCTION                   AM_HAL_PIN_47_NCE47

// IMU
#define IMU_I2C_NUMBER                              0
#define IMU_I2C_ADDRESS                             0x29
#define PIN_IMU_I2C_SCL                             5
#define PIN_IMU_I2C_SDA                             6
#define PIN_IMU_I2C_SCL_FUNCTION                    AM_HAL_PIN_5_M0SCL
#define PIN_IMU_I2C_SDA_FUNCTION                    AM_HAL_PIN_6_M0SDAWIR3
#define PIN_IMU_INTERRUPT                           0
#define PIN_IMU_RESET                               0

// LEDs
#define PIN_LED1_RED                                AM_BSP_GPIO_LED0
#define PIN_LED1_GREEN                              AM_BSP_GPIO_LED1
#define PIN_LED2_RED                                AM_BSP_GPIO_LED0
#define PIN_LED2_YELLOW                             AM_BSP_GPIO_LED2

// Logging
#define PIN_SWO                                     28
#define PIN_SWO_FUNCTION                            AM_HAL_PIN_28_SWO

// Memory Storage (SPI)
#define STORAGE_SPI_NUMBER                          1
#define PIN_STORAGE_SPI_SCK                         8
#define PIN_STORAGE_SPI_MISO                        10
#define PIN_STORAGE_SPI_MOSI                        9
#define PIN_STORAGE_SPI_CS                          11
#define PIN_STORAGE_SPI_SCK_FUNCTION                AM_HAL_PIN_8_M1SCK
#define PIN_STORAGE_SPI_MISO_FUNCTION               AM_HAL_PIN_10_M1MISO
#define PIN_STORAGE_SPI_MOSI_FUNCTION               AM_HAL_PIN_9_M1MOSI
#define PIN_STORAGE_SPI_CS_FUNCTION                 AM_HAL_PIN_11_NCE11
#define PIN_STORAGE_WRITE_PROTECT                   12
#define PIN_STORAGE_HOLD                            13
#define MEMORY_PAGE_SIZE_BYTES                      2048
#define MEMORY_BLOCK_COUNT                          1024
#define MEMORY_ECC_BYTES_PER_PAGE                   64
#define STORAGE_DEVICE_ID                           { 0xEF, 0xBA, 0x21 }

// Timers
#define BUZZER_TIMER_NUMBER                         0
#define RADIO_WAKEUP_TIMER_NUMBER                   2
#define RADIO_WAKEUP_TIMER_TICK_RATE_HZ             (AM_HAL_CLKGEN_FREQ_MAX_HZ / 16)
#define BLE_SCANNING_TIMER_NUMBER                   3
#define BLE_SCANNING_TIMER_TICK_RATE_HZ             (AM_HAL_CLKGEN_FREQ_MAX_HZ / 16)

#endif  // #ifndef __PINOUT_HEADER_H__
