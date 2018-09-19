# Board-specific configurations for the TotTernary system

# Ensure that this file is only included once
ifndef BOARD_MAKEFILE
BOARD_MAKEFILE = 1

# Board-specific configurations
BOARD = TotTag
USE_BLE = 1

# Get directory of this makefile
BOARD_DIR := $(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))

# Include any files in this directory in the build process
BOARD_SOURCE_PATHS = $(BOARD_DIR)/.
BOARD_HEADER_PATHS = $(BOARD_DIR)/.
BOARD_LINKER_PATHS = $(BOARD_DIR)/.
BOARD_SOURCES = $(notdir $(wildcard $(BOARD_DIR)/*.c))
BOARD_AS = $(notdir $(wildcard $(BOARD_DIR)/*.s))

# Add source and header files
BOARD_SOURCE_PATHS += $(BOARD_DIR)/../../src
BOARD_HEADER_PATHS += $(BOARD_DIR)/../../include

# Convert board to upper case
BOARD_UPPER = $(shell echo $(BOARD) | tr a-z A-Z)

# Additional #define's to be added to code by the compiler
BOARD_VARS = \
	BOARD_$(BOARD_UPPER)\
	USE_APP_CONFIG\
	DEBUG\
	DEBUG_NRF\

# Default SDK source files to be included
BOARD_SOURCES += \
	app_error_handler_gcc.c\
	app_fifo.c\
	app_uart_fifo.c\
	app_scheduler.c\
	app_timer.c\
	app_util_platform.c\
	fds.c\
	mem_manager.c\
	nrf_atflags.c\
	nrf_atfifo.c\
	nrf_assert.c\
	nrf_atomic.c\
	nrf_balloc.c\
	nrf_ble_gatt.c\
	nrf_ble_qwr.c\
	nrf_crypto_aes.c\
	nrf_crypto_aead.c\
	nrf_crypto_ecc.c\
	nrf_crypto_ecdh.c\
	nrf_crypto_hkdf.c\
	nrf_crypto_hmac.c\
	nrf_crypto_init.c\
	nrf_crypto_rng.c\
	nrf_drv_twi.c\
	nrf_drv_uart.c\
	nrf_fstorage.c\
	nrf_fstorage_sd.c\
	nrf_fprintf.c\
	nrf_fprintf_format.c\
	nrf_log_backend_rtt.c\
	nrf_log_backend_serial.c\
	nrf_log_backend_uart.c\
	nrf_log_default_backends.c\
	nrf_log_frontend.c\
	nrf_log_str_formatter.c\
	nrf_pwr_mgmt.c\
	nrf_memobj.c\
	nrf_section_iter.c\
	nrf_sdh.c\
	nrf_sdh_ble.c\
	ble_advdata.c\
	ble_advertising.c\
	ble_conn_state.c\
	ble_conn_params.c\
	ble_srv_common.c\
	nrf_serial.c\
	nrf_strerror.c\
	nrf_queue.c\
	nrfx_gpiote.c\
	nrfx_power.c\
	nrfx_prs.c\
	nrfx_timer.c\
	nrfx_twi.c\
	nrfx_twim.c\
	nrfx_uart.c\
	nrfx_uarte.c\
	nrfx_saadc.c\
	nrfx_spi.c\
	nrfx_spim.c\
	nrf_spi_mngr.c\
	nrf_drv_spi.c\
	nrfx_rtc.c\
	nrfx_clock.c\
	nrf_drv_clock.c\
	nrf_hw_backend_rng_mbedtls.c\
	mbedtls_backend_aes.c\
	mbedtls_backend_ecc.c\
	mbedtls_backend_ecdh.c\
	mbedtls_backend_hmac.c\
	aes.c\
	arc4.c\
	bignum.c\
	blowfish.c\
	camellia.c\
	ccm.c\
	cipher.c\
	cipher_wrap.c\
	cmac.c\
	ctr_drbg.c\
	des.c\
	ecp.c\
	ecp_curves.c\
	ecdh.c\
	gcm.c\
	md.c\
	md5.c\
	md_wrap.c\
	ripemd160.c\
	sha1.c\
	sha256.c\
	sha512.c\
	cifra_backend_aes_aead.c\
	cifra_eax_aes.c\
	cifra_cmac.c\
	blockwise.c\
	eax.c\
	gf128.c\
	modes.c\
	SEGGER_RTT.c\
	SEGGER_RTT_Syscalls_GCC.c\
	SEGGER_RTT_printf.c\
	system_nrf52840.c\
	simple_logger.c\
	ff.c\
	mmc_nrf.c\
	accelerometer_lis2dw12.c\
	better_error_handling.c\
	module_interface.c\
	led.c\

# Files required for Eddystone
#es_adv.c\
#es_adv_frame.c\
#es_adv_timing.c\
#es_adv_timing_resolver.c\
#es_battery_voltage_saadc.c\
#es_flash.c\
#es_gatts.c\
#es_gatts_read.c\
#es_gatts_write.c\
#es_security.c\
#es_slot.c\
#es_slot_reg.c\
#es_stopwatch.c\
#es_tlm.c\
#nrf_ble_es.c\
#nrf_ble_escs.c\

endif
