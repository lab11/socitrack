PROJECT_NAME = $(shell basename "$(realpath ./)")

APPLICATION_SRCS = $(notdir $(wildcard ./*.c))
#APPLICATION_SRCS += system_nrf52840.c
APPLICATION_SRCS += softdevice_handler.c
APPLICATION_SRCS += ble_advdata.c
APPLICATION_SRCS += ble_conn_params.c
APPLICATION_SRCS += app_timer.c
APPLICATION_SRCS += ble_srv_common.c
APPLICATION_SRCS += nrf_drv_twi.c
APPLICATION_SRCS += app_util_platform.c
APPLICATION_SRCS += nrf_drv_common.c
APPLICATION_SRCS += nrf_drv_gpiote.c
APPLICATION_SRCS += nrf_delay.c
APPLICATION_SRCS += led.c

APPLICATION_SRCS += simple_ble.c
APPLICATION_SRCS += simple_adv.c

APPLICATION_SRCS += module_interface.c
APPLICATION_SRCS += SEGGER_RTT.c

DEVICE = NRF52

ifndef TARGET
TARGET = tottag
endif

ifneq ($(PID), )
ID = c0:98:e5:44:50:$(PID)
endif

TARGET_UPPER = $(shell echo $(TARGET) | tr a-z A-Z)
TARGET_LOWER = $(shell echo $(TARGET) | tr A-Z a-z)
BOARD = BOARD_$(TARGET_UPPER)

APPLICATION_SRCS += $(TARGET_LOWER).c

SOFTDEVICE_MODEL = s140
SDK_VERSION = 15

LIBRARY_PATHS += .  ../../include
SOURCE_PATHS += ../../src

# Compiler flags
# Optimize for debugging; as only the last optimization flag is actually use, delete "CFLAGS += 0s" in /nrf5x-base/make/Makefile : l. 237
#CFLAGS = -Og
# Optimize for size
CFLAGS = -Os
# Set correct port for GDB server
GDB_PORT_NUMBER = 2331

# Add empty variable to add flags over command line
CDBG +=
CFLAGS += $(CDBG)

NRF_BASE_PATH ?= ../../nrf5x-base/
include $(NRF_BASE_PATH)/make/Makefile
