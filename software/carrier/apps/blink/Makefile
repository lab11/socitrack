PROJECT_NAME = $(shell basename "$(realpath ./)")

# Configuration
NRF_MODEL = NRF52
NRF_IC = nrf52840

SDK_VERSION = 15
SOFTDEVICE_MODEL = s140

# Set ID
ifneq ($(PID), )
ID = c0:98:e5:44:50:$(PID)
endif


# Source and header files
APP_HEADER_PATHS += .
APP_SOURCE_PATHS += .
APP_SOURCES = $(notdir $(wildcard ./*.c))


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


# Include board Makefile (if any)
include ../../boards/TotTag_revF/Board.mk

# Path to base of nRF52-base repo
NRF_BASE_DIR = ../../nrf5x-base

# Include main Makefile
include $(NRF_BASE_DIR)/make/AppMakefile.mk