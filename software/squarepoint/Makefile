PROJECT_NAME = $(shell basename "$(realpath ./)")
CDBG := 

#List all of the application source files that need to be compiled
#############################################
APPLICATION_SRCS = $(notdir $(wildcard dw1000-driver/*.c))
APPLICATION_SRCS += $(notdir $(wildcard firmware/*.c))
APPLICATION_SRCS += $(notdir $(wildcard peripherals/*.c))

APPLICATION_SRCS += stm32f0xx_gpio.c
APPLICATION_SRCS += stm32f0xx_rcc.c
APPLICATION_SRCS += stm32f0xx_i2c.c
APPLICATION_SRCS += stm32f0xx_iwdg.c
APPLICATION_SRCS += stm32f0xx_dma.c
APPLICATION_SRCS += stm32f0xx_misc.c
APPLICATION_SRCS += stm32f0xx_tim.c
APPLICATION_SRCS += stm32f0xx_spi.c
APPLICATION_SRCS += stm32f0xx_pwr.c
APPLICATION_SRCS += stm32f0xx_exti.c
APPLICATION_SRCS += stm32f0xx_syscfg.c
APPLICATION_SRCS += stm32f0xx_usart.c
APPLICATION_SRCS += stm32f0xx_wwdg.c

APPLICATION_SRCS += stm32f0xx_i2c_cpal.c
APPLICATION_SRCS += stm32f0xx_i2c_cpal_hal.c

# Comment out the following line to remove debugging capabilities from firmware
CDBG += -DDEBUG_OUTPUT_RTT=1 -g
CFLAGS += $(CDBG)
CFLAGS += -Wall -Wextra -Wno-unused-parameter -Os -s
LDFLAGS += -lm

#The exact STM part number All CAPS
############################################
DEVICE ?= STM32F091CC

ifeq ($(DEVICE), STM32F031G6)
DEVICE_FAMILY ?= STM32F031
CFLAGS += -DBOARD_V=1
endif
ifeq ($(DEVICE), STM32F091CC)
DEVICE_FAMILY ?= STM32F091
CFLAGS += -DBOARD_V=2
endif

## HACK FOR stm32f031
## Need to define this so that CPAL will compile.
## It is never used, however.
ifeq ($(DEVICE_FAMILY), STM32F031)
CFLAGS += -DI2C2_IRQn=24
endif

# Adjust clock speed of HSE (High-Speed external oscillator)
ifeq ($(DEVICE_FAMILY), STM32F091)
# Use 16 MHz external oscillator
HSE_CLK_FREQ = 16000000
CFLAGS += -DHSE_VALUE=$(HSE_CLK_FREQ)
endif

# Allow to circumvent BLE interface
ifneq ($(BYPASS_HOST_INTERFACE), )
CFLAGS += -DBYPASS_HOST_INTERFACE
endif
ifneq ($(CALIBRATION), )
CFLAGS += -DCALIBRATION
endif

#The install locations of the STM Standard library
#################################################
LIBRARY_START_PATH ?= stm32f0-base/sdk/stm32f0xx-1.5.0/

#The location of the gcc setup template
################################################
TEMPLATE_PATH ?= stm32f0-base/stm32f0xx-gcc-setup/template/

#Any places that you put custom header files
###############################################
LIBRARY_PATHS += dw1000-driver firmware peripherals

#Any places that you put custom source files
##############################################
SOURCE_PATHS += dw1000-driver firmware peripherals

ifndef LIBRARY_START_PATH
$(error An LIBRARY_START_PATH must be defined. Edit Makefile to define a valid LIBRARY_PATH)
endif

# Set an ID for programming the nodes EUI
ifeq ($(ID), )
$(error Must set an ID for the target device when compiling (e.g. ID=c0:98:e5:42:00:FF))
endif

# This creates a .bin file for loading the calibration values into the flash of the module.
USER_FLASH_COMMANDS = $(shell python insert_calibration.py $(ID))

# Set correct port for GDB server
GDB_PORT_NUMBER = 2331

include $(TEMPLATE_PATH)Makefile
