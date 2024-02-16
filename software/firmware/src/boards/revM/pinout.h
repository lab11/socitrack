#ifndef __PINOUT_HEADER_H__
#define __PINOUT_HEADER_H__

// Hardware Revision ID
#include "revisions.h"
#define REVISION_ID                                 REVISION_M

// Battery
#define PIN_BATTERY_VOLTAGE                         18
#define PIN_BATTERY_VOLTAGE_FUNCTION                AM_HAL_PIN_18_ADCSE1
#define PIN_BATTERY_VOLTAGE_ADC_CHANNEL             AM_HAL_ADC_SLOT_CHSEL_SE1
#define PIN_BATTERY_CHARGING_STATUS                 58
#define PIN_BATTERY_INPUT_POWER_GOOD                60
#define PIN_BATTERY_CHARGE_COMPLETE_SIGNAL          66
#define VOLTAGE_DIVIDER_LOWER                       187
#define VOLTAGE_DIVIDER_UPPER                       510

// Buzzer
#define PIN_BUZZER_DRIVER                           55

// DecaWave Radio
#define RADIO_SPI_NUMBER                            3
#define PIN_RADIO_SPI_SCK                           31
#define PIN_RADIO_SPI_MISO                          33
#define PIN_RADIO_SPI_MOSI                          32
#define PIN_RADIO_SPI_CS                            24
#define PIN_RADIO_SPI_CS2                           29
#define PIN_RADIO_SPI_CS3                           25
#define PIN_RADIO_INTERRUPT                         72
#define PIN_RADIO_INTERRUPT2                        27
#define PIN_RADIO_INTERRUPT3                        76
#define PIN_RADIO_RESET                             82
#define PIN_RADIO_RESET2                            50
#define PIN_RADIO_RESET3                            83
#define PIN_RADIO_WAKEUP                            63
#define PIN_RADIO_WAKEUP2                           26
#define PIN_RADIO_WAKEUP3                           88
#define PIN_RADIO_SPI_SCK_FUNCTION                  AM_HAL_PIN_31_M3SCK
#define PIN_RADIO_SPI_MISO_FUNCTION                 AM_HAL_PIN_33_M3MISO
#define PIN_RADIO_SPI_MOSI_FUNCTION                 AM_HAL_PIN_32_M3MOSI
#define PIN_RADIO_SPI_CS_FUNCTION                   AM_HAL_PIN_24_NCE24
#define PIN_RADIO_SPI_CS2_FUNCTION                  AM_HAL_PIN_29_NCE29
#define PIN_RADIO_SPI_CS3_FUNCTION                  AM_HAL_PIN_25_NCE25

// IMU
#define IMU_I2C_NUMBER                              1
#define IMU_I2C_ADDRESS                             0x29
#define PIN_IMU_I2C_SCL                             8
#define PIN_IMU_I2C_SDA                             9
#define PIN_IMU_I2C_SCL_FUNCTION                    AM_HAL_PIN_8_M1SCL
#define PIN_IMU_I2C_SDA_FUNCTION                    AM_HAL_PIN_9_M1SDAWIR3
#define PIN_IMU_INTERRUPT                           17
#define PIN_IMU_RESET                               65

// LEDs
#define PIN_LED1_RED                                53
#define PIN_LED1_GREEN                              51

// Logging
#define PIN_SWO                                     3
#define PIN_SWO_FUNCTION                            AM_HAL_PIN_3_SWO

// Memory Storage (SPI)
#define STORAGE_SPI_NUMBER                          5
#define PIN_STORAGE_SPI_SCK                         47
#define PIN_STORAGE_SPI_MISO                        49
#define PIN_STORAGE_SPI_MOSI                        48
#define PIN_STORAGE_SPI_CS                          69
#define PIN_STORAGE_SPI_SCK_FUNCTION                AM_HAL_PIN_47_M5SCK
#define PIN_STORAGE_SPI_MISO_FUNCTION               AM_HAL_PIN_49_M5MISO
#define PIN_STORAGE_SPI_MOSI_FUNCTION               AM_HAL_PIN_48_M5MOSI
#define PIN_STORAGE_SPI_CS_FUNCTION                 AM_HAL_PIN_69_NCE69
#define PIN_STORAGE_WRITE_PROTECT                   30
#define PIN_STORAGE_HOLD                            6

// Timers
#define BUZZER_TIMER_NUMBER                         0
#define RADIO_WAKEUP_TIMER_NUMBER                   2
#define RADIO_WAKEUP_TIMER_TICK_RATE_HZ             (AM_HAL_CLKGEN_FREQ_MAX_HZ / 16)
#define BLE_ERROR_TIMER_NUMBER                      3
#define BLE_ERROR_TIMER_TICK_RATE_HZ                (AM_HAL_CLKGEN_FREQ_MAX_HZ / 16)
#define BLE_SCANNING_TIMER_NUMBER                   4
#define BLE_SCANNING_TIMER_TICK_RATE_HZ             (AM_HAL_CLKGEN_FREQ_MAX_HZ / 16)

#endif  // #ifndef __PINOUT_HEADER_H__
