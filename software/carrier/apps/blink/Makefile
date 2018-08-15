PROJECT_NAME = $(shell basename "$(realpath ./)")

APPLICATION_SRCS = $(notdir $(wildcard ./*.c))
APPLICATION_SRCS += softdevice_handler.c
APPLICATION_SRCS += app_timer.c
APPLICATION_SRCS += led.c

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

SOFTDEVICE_MODEL = s140

SDK_VERSION = 15

LIBRARY_PATHS += ../../nrf5x-base/peripherals ../../nrf5x-base/devices ../../nrf5x-base/services ../../src
SOURCE_PATHS += ../../nrf5x-base/peripherals ../../nrf5x-base/devices ../../nrf5x-base/services

CFLAGS = -Os
GDB_PORT_NUMBER = 2331

# Include the main Makefile
NRF_BASE_PATH ?= ../../nrf5x-base
include $(NRF_BASE_PATH)/make/Makefile
