#ifndef TOTTAG_H
#define TOTTAG_H

#include "nrf_gpio.h"

#ifndef DEVICE_NAME
#define DEVICE_NAME "tottag"
#endif


#define ADDRESS_FLASH_LOCATION 0x0003fff8


// TOTTAG
#define LED_START      17
#define LED_0          17
#define LED_STOP       17

#define I2C_SCL_PIN   28
#define I2C_SDA_PIN   29

#define MODULE_INTERRUPT_PIN 25

#define BATTERY_MONITOR_PIN 1

#define SER_CONN_ASSERT_LED_PIN     LED_0

#endif/* TOTTAG_H */
