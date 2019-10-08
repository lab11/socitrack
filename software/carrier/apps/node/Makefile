PROJECT_NAME = $(shell basename "$(realpath ./)")

# Configuration
NRF_MODEL = NRF52
NRF_IC = nrf52840

SDK_VERSION = 15
SOFTDEVICE_MODEL = s140

# Set BLE_ADDRESS
ifneq ($(PID), )
BLE_ADDRESS = c0:98:e5:42:00:$(PID)
endif
ifneq ($(BLE_ADDRESS), )
CFLAGS += -DBLE_ADDRESS=$(BLE_ADDRESS)
endif

# Allow to circumvent BLE interface
ifneq ($(BYPASS_USER_INTERFACE), )
CFLAGS += -DBYPASS_USER_INTERFACE
endif
ifneq ($(ROLE), )
CFLAGS += -DROLE_$(ROLE)
endif
ifneq ($(GLOSSY_MASTER), )
CFLAGS += -DGLOSSY_MASTER
endif
ifneq ($(APP_BLE_CALIBRATION), )
CFLAGS += -DAPP_BLE_CALIBRATION
endif
ifneq ($(FORCE_RTC_RESET), )
CFLAGS += -DFORCE_RTC_RESET
endif

# Source and header files
APP_HEADER_PATHS += .
APP_SOURCE_PATHS += .
APP_SOURCES = $(notdir $(wildcard ./*.c))


# Compiler flags
# Optimize for debugging; as only the last optimization flag is actually use, delete "CFLAGS += 0s" in /nrf5x-base/make/Makefile : l. 237
#CFLAGS = -Og
# Optimize for size
CFLAGS += -Os
# Set correct port for GDB server
GDB_PORT_NUMBER = 2331

# Add empty variable to add flags over command line
CDBG +=
CFLAGS += $(CDBG)

# HACK: Enable "cmac.c" for Eddystone libary to compile
CFLAGS += -DMBEDTLS_CMAC_C

# Include board Makefile (if any)
ifneq ($(BOARD_LEGACY), )
include ../../boards/TotTag_revD/Board.mk
else
include ../../boards/TotTag_revF/Board.mk
endif

# Path to base of nRF52-base repo
NRF_BASE_DIR = ../../nrf5x-base

# Include main Makefile
include $(NRF_BASE_DIR)/make/AppMakefile.mk