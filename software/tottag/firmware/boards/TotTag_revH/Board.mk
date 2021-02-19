# Ensure that this file is only included once
ifndef BOARD_MAKEFILE
BOARD_MAKEFILE = 1

# Board-specific configurations
BOARD = TotTag
REVISION = H
USE_BLE = 1

# Get directory of this makefile
BOARD_DIR := $(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))

# Include any files in this directory in the build process
BOARD_SOURCE_PATHS = $(BOARD_DIR) $(BOARD_DIR)/../../peripherals/src $(BOARD_DIR)/../../application
BOARD_HEADER_PATHS = $(BOARD_DIR) $(BOARD_DIR)/../../peripherals/include $(BOARD_DIR)/../../application
BOARD_LINKER_PATHS = $(BOARD_DIR)
BOARD_SOURCES      = $(foreach BOARD_PATH,$(BOARD_SOURCE_PATHS),$(wildcard $(BOARD_PATH)/*.c))
BOARD_AS           = $(foreach BOARD_PATH,$(BOARD_SOURCE_PATHS),$(wildcard $(BOARD_PATH)/*.s))

# Convert board to upper case
BOARD_UPPER = $(shell echo $(BOARD) | tr a-z A-Z)

# Additional #define's to be added to code by the compiler
BOARD_VARS = \
	BOARD_$(BOARD_UPPER)\
	USE_APP_CONFIG

# Default SDK source files to be included
BOARD_SOURCES += \
	app_error_handler_gcc.c\
	app_timer.c\
	app_usbd.c\
	app_usbd_cdc_acm.c\
	app_usbd_core.c\
	app_usbd_serial_num.c\
	app_usbd_string_desc.c\
	app_util_platform.c\
	ble_advdata.c\
	ble_advertising.c\
	ble_conn_params.c\
	ble_srv_common.c\
	diskio.c\
	ff.c\
	nrf_atfifo.c\
	nrf_atomic.c\
	nrf_balloc.c\
	nrf_ble_gatt.c\
	nrf_ble_qwr.c\
	nrf_drv_clock.c\
	nrf_drv_power.c\
	nrf_log_frontend.c\
	nrf_memobj.c\
	nrf_pwr_mgmt.c\
	nrf_sdh.c\
	nrf_sdh_ble.c\
	nrf_section_iter.c\
	nrf_strerror.c\
	nrfx_atomic.c\
	nrfx_clock.c\
	nrfx_gpiote.c\
	nrfx_power.c\
	nrfx_prs.c\
	nrfx_pwm.c\
	nrfx_rtc.c\
	nrfx_saadc.c\
	nrfx_spim.c\
	nrfx_twi.c\
	nrfx_usbd.c\
	nrfx_wdt.c\
	SEGGER_RTT.c\
	SEGGER_RTT_printf.c\
	SEGGER_RTT_Syscalls_GCC.c\
	simple_logger.c\
	system_nrf52840.c

# Make sure that bluetooth.c is always compiled so the ID is picked up correctly
_build/bluetooth.o: FORCE

# Make sure that rtc.c is always compiled so that the compile time is correct
_build/rtc_external.o: FORCE

.PHONY: FORCE

endif
