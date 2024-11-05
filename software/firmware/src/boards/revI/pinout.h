#ifndef __PINOUT_HEADER_H__
#define __PINOUT_HEADER_H__

// Hardware Revision ID
#include "revisions.h"
#define REVISION_ID                                 REVISION_I

// Battery
#define PIN_BATTERY_VOLTAGE                         18
#define PIN_BATTERY_VOLTAGE_FUNCTION                AM_HAL_PIN_18_ADCSE1
#define PIN_BATTERY_VOLTAGE_ADC_CHANNEL             AM_HAL_ADC_SLOT_CHSEL_SE1
#define PIN_BATTERY_CHARGING_STATUS                 58
#define PIN_BATTERY_INPUT_POWER_GOOD                60
#define PIN_BATTERY_CHARGE_COMPLETE_SIGNAL          47
#define VOLTAGE_DIVIDER_LOWER                       33
#define VOLTAGE_DIVIDER_UPPER                       51

// Buzzer
#define PIN_BUZZER_DRIVER                           54

// DecaWave Radio
#define RADIO_SPI_NUMBER                            0
#define PIN_RADIO_SPI_SCK                           5
#define PIN_RADIO_SPI_MISO                          7
#define PIN_RADIO_SPI_MOSI                          6
#define PIN_RADIO_SPI_CS                            29
#define PIN_RADIO_INTERRUPT                         27
#define PIN_RADIO_RESET                             49
#define PIN_RADIO_WAKEUP                            26
#define PIN_RADIO_ANTENNA_SELECT1                   24
#define PIN_RADIO_ANTENNA_SELECT2                   25
#define PIN_RADIO_SPI_SCK_FUNCTION                  AM_HAL_PIN_5_M0SCK
#define PIN_RADIO_SPI_MISO_FUNCTION                 AM_HAL_PIN_7_M0MISO
#define PIN_RADIO_SPI_MOSI_FUNCTION                 AM_HAL_PIN_6_M0MOSI
#define PIN_RADIO_SPI_CS_FUNCTION                   AM_HAL_PIN_29_NCE29

// IMU
#define IMU_I2C_NUMBER                              1
#define IMU_I2C_ADDRESS                             0x29
#define PIN_IMU_I2C_SCL                             8
#define PIN_IMU_I2C_SDA                             9
#define PIN_IMU_I2C_SCL_FUNCTION                    AM_HAL_PIN_8_M1SCL
#define PIN_IMU_I2C_SDA_FUNCTION                    AM_HAL_PIN_9_M1SDAWIR3
#define PIN_IMU_INTERRUPT                           17
#define PIN_IMU_RESET                               48

// LEDs
#define PIN_LED1_RED                                52
#define PIN_LED1_GREEN                              80
#define PIN_LED2_RED                                76
#define PIN_LED2_YELLOW                             81

// Logging
#define PIN_SWO                                     67
#define PIN_SWO_FUNCTION                            AM_HAL_PIN_67_SWO

// Memory Storage (SPI)
#define STORAGE_SPI_NUMBER                          3
#define PIN_STORAGE_SPI_SCK                         31
#define PIN_STORAGE_SPI_MISO                        33
#define PIN_STORAGE_SPI_MOSI                        32
#define PIN_STORAGE_SPI_CS                          63
#define PIN_STORAGE_SPI_SCK_FUNCTION                AM_HAL_PIN_31_M3SCK
#define PIN_STORAGE_SPI_MISO_FUNCTION               AM_HAL_PIN_33_M3MISO
#define PIN_STORAGE_SPI_MOSI_FUNCTION               AM_HAL_PIN_32_M3MOSI
#define PIN_STORAGE_SPI_CS_FUNCTION                 AM_HAL_PIN_63_NCE63
#define PIN_STORAGE_WRITE_PROTECT                   66
#define PIN_STORAGE_HOLD                            67
#define MEMORY_BLOCK_COUNT                          1024
#define MEMORY_ECC_BYTES_PER_PAGE                   64
#define STORAGE_DEVICE_ID                           { 0xEF, 0xBA, 0x21 }

// Timers
#define BUZZER_TIMER_NUMBER                         0
#define RADIO_WAKEUP_TIMER_NUMBER                   2
#define RADIO_WAKEUP_TIMER_TICK_RATE_HZ             (AM_HAL_CLKGEN_FREQ_MAX_HZ / 16)
#define BLE_ERROR_TIMER_NUMBER                      3
#define BLE_ERROR_TIMER_TICK_RATE_HZ                (AM_HAL_CLKGEN_FREQ_MAX_HZ / 16)
#define BLE_SCANNING_TIMER_NUMBER                   4
#define BLE_SCANNING_TIMER_TICK_RATE_HZ             (AM_HAL_CLKGEN_FREQ_MAX_HZ / 16)

#endif  // #ifndef __PINOUT_HEADER_H__
