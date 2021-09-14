# Included folders and source files for building nrf applications
# Included by Configuration.mk

# Ensure that this file is only included once
ifndef INCLUDES_MAKEFILE
INCLUDES_MAKEFILE = 1


# ---- This repo's files
REPO_HEADER_PATHS += $(NRF_BASE_DIR)/lib/
REPO_HEADER_PATHS += $(dir $(wildcard $(NRF_BASE_DIR)/lib/simple_logger/))
#REPO_HEADER_PATHS += $(dir $(wildcard $(NRF_BASE_DIR)/lib/simple_logger/*/))
REPO_SOURCE_PATHS += $(NRF_BASE_DIR)/lib/
REPO_SOURCE_PATHS += $(dir $(wildcard $(NRF_BASE_DIR)/lib/simple_logger/))
#REPO_SOURCE_PATHS += $(dir $(wildcard $(NRF_BASE_DIR)/lib/simple_logger/*/))

# ---- SDK files

ifneq (,$(filter $(NRF_IC),nrf52840))

    # Set the path
    SDK_ROOT = $(NRF_BASE_DIR)/sdk/nrf5_sdk_17.1.0/
    
    # default files for ICs
    ifeq ($(NRF_IC),nrf52840)
      SDK_SOURCES += system_nrf52840.c
      SDK_AS = gcc_startup_nrf52840.S
    endif

    # default C files necessary for any application
    #XXX: are there other C files that I can include here?
    SDK_SOURCES += app_error.c
    SDK_SOURCES += app_error_weak.c

    # To make SEGGER RTT retarget to printf correctly, you need to get around
    # the silly ANSI C compatibility stuff that breaks our GCC builds
    PARAMS_DEFINE = -D_PARAMS\(paramlist\)=paramlist

    # Add paths for sdk-specific linker files
    SDK_LINKER_PATHS += $(SDK_ROOT)modules/nrfx/mdk/

    # Path for default sdk_config.h
    SDK_HEADER_PATHS += $(NRF_BASE_DIR)/make/config/$(NRF_IC)/config/

    # Need to add the paths for all the directories in the SDK.
    # Note that we do not use * because some folders have conflicting files.
    SDK_HEADER_PATHS += $(SDK_ROOT)modules/nrfx/
    SDK_HEADER_PATHS += $(SDK_ROOT)modules/nrfx/drivers/
    SDK_HEADER_PATHS += $(SDK_ROOT)modules/nrfx/drivers/include/
    SDK_HEADER_PATHS += $(SDK_ROOT)modules/nrfx/hal/
    SDK_HEADER_PATHS += $(SDK_ROOT)modules/nrfx/mdk/
    SDK_HEADER_PATHS += $(SDK_ROOT)modules/nrfx/soc/
    SDK_HEADER_PATHS += $(SDK_ROOT)integration/nrfx/
    SDK_HEADER_PATHS += $(SDK_ROOT)integration/nrfx/legacy/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/atomic/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/atomic_fifo/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/atomic_flags/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/balloc/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/block_dev/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/block_dev/sdc/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/bootloader/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/bsp/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/button/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/cli/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/crc16/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/crc32/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/crypto/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/csense/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/csense_drv/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/delay/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/ecc/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/experimental_section_vars/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/experimental_task_manager/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/fds/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/fifo/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/fstorage/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/gfx/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/gpiote/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/hardfault/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/hardfault/$(NRF_MODEL)/handler/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/hci/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/led_softblink/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/libuarte/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/log/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/log/src/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/low_power_pwm/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/mem_manager/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/memobj/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/mpu/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/mutex/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/pwm/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/pwr_mgmt/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/queue/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/ringbuf/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/scheduler/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/sdcard/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/sensorsim/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/simple_timer/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/slip/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/sortlist/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/spi_mngr/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/stack_guard/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/stack_info/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/strerror/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/svc/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/timer/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/twi_mngr/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/twi_sensor/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/uart/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/usbd/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/usbd/class/cdc/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/usbd/class/cdc/acm/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/libraries/util/
    SDK_HEADER_PATHS += $(wildcard $(SDK_ROOT)components/libraries/crypto/backend/*/)
    SDK_HEADER_PATHS += $(wildcard $(SDK_ROOT)components/drivers_nrf/radio_config/)
    SDK_HEADER_PATHS += $(wildcard $(SDK_ROOT)components/drivers_nrf/sdio/)
    SDK_HEADER_PATHS += $(wildcard $(SDK_ROOT)components/drivers_nrf/spi_master/)
    SDK_HEADER_PATHS += $(wildcard $(SDK_ROOT)components/drivers_nrf/twi_master/)
    SDK_HEADER_PATHS += $(wildcard $(SDK_ROOT)components/drivers_ext/*/)
    SDK_HEADER_PATHS += $(SDK_ROOT)components/toolchain/gcc/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/toolchain/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/toolchain/cmsis/include/
    SDK_HEADER_PATHS += $(SDK_ROOT)components/softdevice/common/
    SDK_HEADER_PATHS += $(SDK_ROOT)external/fatfs/port/
    SDK_HEADER_PATHS += $(SDK_ROOT)external/fatfs/src/
    SDK_HEADER_PATHS += $(SDK_ROOT)external/fprintf/
    SDK_HEADER_PATHS += $(SDK_ROOT)external/fprintf/
    SDK_HEADER_PATHS += $(SDK_ROOT)external/protothreads/
    SDK_HEADER_PATHS += $(SDK_ROOT)external/protothreads/pt-1.4/
    SDK_HEADER_PATHS += $(SDK_ROOT)external/segger_rtt/
    SDK_HEADER_PATHS += $(SDK_ROOT)external/utf_converter/

    SDK_SOURCE_PATHS += $(SDK_ROOT)components/
    SDK_SOURCE_PATHS += $(wildcard $(SDK_ROOT)components/*/)
    SDK_SOURCE_PATHS += $(SDK_ROOT)modules/nrfx/
    SDK_SOURCE_PATHS += $(SDK_ROOT)modules/nrfx/mdk/
    SDK_SOURCE_PATHS += $(SDK_ROOT)modules/nrfx/hal/
    SDK_SOURCE_PATHS += $(SDK_ROOT)modules/nrfx/drivers/src/
    SDK_SOURCE_PATHS += $(SDK_ROOT)modules/nrfx/drivers/src/prs/
    SDK_SOURCE_PATHS += $(wildcard $(SDK_ROOT)modules/nrfx/*/)
    SDK_SOURCE_PATHS += $(SDK_ROOT)integration/nrfx/legacy/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/libraries/atomic/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/libraries/atomic_fifo/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/libraries/atomic_flags/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/libraries/balloc/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/libraries/block_dev/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/libraries/block_dev/sdc/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/libraries/bootloader/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/libraries/bsp/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/libraries/button/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/libraries/cli/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/libraries/crc16/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/libraries/crc32/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/libraries/crypto/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/libraries/csense/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/libraries/csense_drv/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/libraries/delay/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/libraries/ecc/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/libraries/experimental_section_vars/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/libraries/experimental_task_manager/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/libraries/fds/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/libraries/fifo/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/libraries/fstorage/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/libraries/gfx/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/libraries/gpiote/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/libraries/hardfault/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/libraries/hardfault/$(NRF_MODEL)/handler/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/libraries/hci/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/libraries/led_softblink/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/libraries/libuarte/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/libraries/log/src/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/libraries/low_power_pwm/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/libraries/mem_manager/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/libraries/memobj/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/libraries/mpu/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/libraries/mutex/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/libraries/pwm/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/libraries/pwr_mgmt/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/libraries/queue/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/libraries/ringbuf/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/libraries/scheduler/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/libraries/sdcard/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/libraries/sensorsim/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/libraries/simple_timer/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/libraries/slip/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/libraries/sortlist/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/libraries/spi_mngr/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/libraries/stack_guard/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/libraries/stack_info/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/libraries/strerror/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/libraries/svc/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/libraries/timer/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/libraries/twi_mngr/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/libraries/twi_sensor/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/libraries/uart/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/libraries/usbd/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/libraries/usbd/class/cdc/acm/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/libraries/util/
    SDK_SOURCE_PATHS += $(wildcard $(SDK_ROOT)components/libraries/crypto/backend/*/)
    SDK_SOURCE_PATHS += $(wildcard $(SDK_ROOT)components/drivers_nrf/*/)
    SDK_SOURCE_PATHS += $(wildcard $(SDK_ROOT)components/drivers_ext/*/)
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/toolchain/gcc/
    SDK_SOURCE_PATHS += $(SDK_ROOT)components/softdevice/common/
    SDK_SOURCE_PATHS += $(SDK_ROOT)external/fatfs/port/
    SDK_SOURCE_PATHS += $(SDK_ROOT)external/fatfs/src/
    SDK_SOURCE_PATHS += $(SDK_ROOT)external/fprintf/
    SDK_SOURCE_PATHS += $(SDK_ROOT)external/protothreads/
    SDK_SOURCE_PATHS += $(SDK_ROOT)external/protothreads/pt-1.4/
    SDK_SOURCE_PATHS += $(SDK_ROOT)external/segger_rtt/
    SDK_SOURCE_PATHS += $(SDK_ROOT)external/utf_converter/

    ifeq ($(USE_BLE),1)
      SDK_HEADER_PATHS += $(wildcard $(SDK_ROOT)components/ble/*/)
      SDK_HEADER_PATHS += $(wildcard $(SDK_ROOT)components/ble/ble_services/*/)
      SDK_SOURCE_PATHS += $(wildcard $(SDK_ROOT)components/ble/*/)
      SDK_SOURCE_PATHS += $(wildcard $(SDK_ROOT)components/ble/ble_services/*/)
      SDK_VARS += BLE_STACK_SUPPORT_REQD SOFTDEVICE_PRESENT NRF_SD_BLE_API_VERSION=6
    endif

    ifneq ($(SOFTDEVICE_MODEL), blank)
      SDK_HEADER_PATHS += $(SDK_ROOT)components/softdevice/common/softdevice_handler/
      SDK_HEADER_PATHS += $(SDK_ROOT)components/softdevice/$(SOFTDEVICE_MODEL)/headers/
      SDK_HEADER_PATHS += $(SDK_ROOT)components/softdevice/$(SOFTDEVICE_MODEL)/headers/nrf52

      SDK_SOURCE_PATHS += $(SDK_ROOT)components/softdevice/common/softdevice_handler/
      SDK_SOURCE_PATHS += $(SDK_ROOT)components/softdevice/$(SOFTDEVICE_MODEL)/headers/
      SDK_SOURCE_PATHS += $(SDK_ROOT)components/softdevice/$(SOFTDEVICE_MODEL)/headers/nrf52
    else
      SDK_HEADER_PATHS += $(wildcard $(SDK_ROOT)components/drivers_nrf/nrf_soc_nosd/)
    endif

endif # nrf52


# ---- Create variables for Configuration use

# Location of softdevice
SOFTDEVICE_PATH ?= $(SDK_ROOT)/components/softdevice/$(SOFTDEVICE_MODEL)/hex/$(SOFTDEVICE_MODEL)_nrf52_$(SOFTDEVICE_VERSION)_softdevice.hex

# Flags for compiler
HEADER_INCLUDES = $(addprefix -I,$(SDK_HEADER_PATHS)) $(addprefix -I,$(REPO_HEADER_PATHS)) $(addprefix -I,$(BOARD_HEADER_PATHS)) $(addprefix -I,$(APP_HEADER_PATHS))
LINKER_INCLUDES = $(addprefix -L,$(SDK_LINKER_PATHS)) $(addprefix -L,$(BOARD_LINKER_PATHS))
SDK_DEFINES = $(addprefix -D,$(SDK_VARS)) $(addprefix -D,$(BOARD_VARS)) $(addprefix -D,$(APP_VARS)) $(PARAMS_DEFINE)

# Directories make searches for prerequisites
VPATH = $(SDK_SOURCE_PATHS) $(REPO_SOURCE_PATHS) $(BOARD_SOURCE_PATHS) $(APP_SOURCE_PATHS)

SOURCES = $(notdir $(APP_SOURCES)) $(notdir $(BOARD_SOURCES)) $(notdir $(SDK_SOURCES))
OBJS = $(addprefix $(BUILDDIR), $(SOURCES:.c=.o))
DEBUG_OBJS = $(addprefix $(BUILDDIR), $(SOURCES:.c=.o-debug))
DEPS = $(addprefix $(BUILDDIR), $(SOURCES:.c=.d))

SOURCES_AS = $(notdir $(SDK_AS)) $(notdir $(BOARD_AS)) $(notdir $(APP_AS))
OBJS_AS = $(addprefix $(BUILDDIR), $(SOURCES_AS:.S=.os))
DEBUG_OBJS_AS = $(addprefix $(BUILDDIR), $(SOURCES_AS:.S=.os-debug))

endif

