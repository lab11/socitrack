PROJECT_NAME = $(shell basename "$(realpath ./)")
CFLAGS += -D_DATETIME="\"$(shell date -u)\""
CDBG :=

# Configuration
NRF_MODEL = NRF52
NRF_IC = nrf52840
SDK_VERSION = 17
SOFTDEVICE_MODEL = s140
FW_VERSION = $(shell git describe --always --dirty --broken)

# Allow to circumvent BLE interface
CFLAGS += -DFW_VERSION="\"$(FW_VERSION)\""
ifneq ($(FORCE_RTC_RESET), )
CFLAGS += -DFORCE_RTC_RESET
endif
ifneq ($(DEBUG_MODE), )
CFLAGS += -DDEBUG_MODE
endif

# Source and header files
APP_HEADER_PATHS += .
APP_SOURCE_PATHS += .
APP_SOURCES = $(notdir $(wildcard ./*.c))

# Optimize for speed
CFLAGS += -O3 -s

# Set correct port for GDB server
GDB_PORT_NUMBER = 2331

# Comment out the following line to remove debugging capabilities from firmware
#CDBG += -DDEBUG_NRF -O0 -g
CFLAGS += $(CDBG)

# Include board Makefile (if any)
ifeq ($(BOARD_REV), F)
include boards/TotTag_revF/Board.mk
else ifeq ($(BOARD_REV), G)
include boards/TotTag_revG/Board.mk
else
include boards/TotTag_revH/Board.mk
endif

# Path to base of nRF52-base repo
NRF_BASE_DIR = nrf5x-base

# Include main Makefile
include $(NRF_BASE_DIR)/make/AppMakefile.mk