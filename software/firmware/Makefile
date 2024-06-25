PROJECT := SociTrack
TARGET := SociTrack
CONFIG := bin
SHELL := /bin/bash

ifdef BOARD_REV
REVISION := $(BOARD_REV)
else
REVISION := M
endif

ifeq ($(REVISION), I)
BSP = apollo4_blue
PART = apollo4b
PART_DEF = AM_PART_APOLLO4B
else
BSP = apollo4_blue_pro
PART = apollo4p
PART_DEF = AM_PART_APOLLO4P
endif

$(info Building for Revision $(REVISION):)
$(info BSP = $(BSP))
$(info PART = $(PART))
$(info PART_DEF = $(PART_DEF))

TOOLCHAIN ?= arm-none-eabi
CPU = cortex-m4
FPU = fpv4-sp-d16
FABI = hard
FLASH_START = 0x00018000
ID_FLASH_LOCATION = 0x001FFFF8

DEFINES  = -D_HW_REVISION=$(REVISION)
DEFINES += -D_DATETIME="\"$(shell date -u)\""
DEFINES += -DPART_$(PART)
DEFINES += -D$(PART_DEF)
DEFINES += -D'SEGGER_RTT_SECTION="SHARED_RW"'
DEFINES += -D__USE_FREERTOS__
DEFINES += -DAM_PACKAGE_BGA
DEFINES += -DDM_NUM_ADV_SETS=1
DEFINES += -Dgcc

DW_LIBRARY := ./src/external/decadriver/libdwt_uwb_driver-m4-hfp-6.0.7.a
LINKER_FILE := ./AmbiqSDK/bsp/$(BSP)/linker/socitrack.ld
STARTUP_FILE := ./AmbiqSDK/bsp/$(BSP)/linker/startup_gcc.c

#### Required Executables ####
CC = $(TOOLCHAIN)-gcc
GCC = $(TOOLCHAIN)-gcc
CPP = $(TOOLCHAIN)-cpp
LD = $(TOOLCHAIN)-ld
CP = $(TOOLCHAIN)-objcopy
OD = $(TOOLCHAIN)-objdump
RD = $(TOOLCHAIN)-readelf
AR = $(TOOLCHAIN)-ar
SIZE = $(TOOLCHAIN)-size
RM = $(shell which rm 2>/dev/null)
EXECUTABLES = CC LD CP OD AR RD SIZE GCC
K := $(foreach exec,$(EXECUTABLES),\
        $(if $(shell which $($(exec)) 2>/dev/null),,\
        $(info $(exec) not found on PATH ($($(exec))).)$(exec)))
$(if $(strip $(value K)),$(info Required Program(s) $(strip $(value K)) not found))

ifneq ($(strip $(value K)),)
all clean:
	$(info Tools $(TOOLCHAIN)-gcc not installed.)
	$(RM) -rf bin
else

INCLUDES  = -IAmbiqSDK/bsp/$(BSP)
INCLUDES += -IAmbiqSDK/mcu/$(PART)
INCLUDES += -IAmbiqSDK/mcu/$(PART)/hal
INCLUDES += -IAmbiqSDK/mcu/$(PART)/hal/mcu
INCLUDES += -IAmbiqSDK/cordio/wsf/include
INCLUDES += -IAmbiqSDK/cordio/wsf/sources
INCLUDES += -IAmbiqSDK/cordio/wsf/sources/port/freertos
INCLUDES += -IAmbiqSDK/cordio/wsf/sources/util
INCLUDES += -IAmbiqSDK/cordio/ble-host/include
INCLUDES += -IAmbiqSDK/cordio/ble-host/sources/hci/ambiq
INCLUDES += -IAmbiqSDK/cordio/ble-host/sources/hci/ambiq/cooper
INCLUDES += -IAmbiqSDK/cordio/ble-host/sources/sec/common
INCLUDES += -IAmbiqSDK/cordio/ble-host/sources/stack/att
INCLUDES += -IAmbiqSDK/cordio/ble-host/sources/stack/cfg
INCLUDES += -IAmbiqSDK/cordio/ble-host/sources/stack/dm
INCLUDES += -IAmbiqSDK/cordio/ble-host/sources/stack/hci
INCLUDES += -IAmbiqSDK/cordio/ble-host/sources/stack/l2c
INCLUDES += -IAmbiqSDK/cordio/ble-host/sources/stack/smp
INCLUDES += -IAmbiqSDK/cordio/ble-profiles/include/app
INCLUDES += -IAmbiqSDK/cordio/ble-profiles/sources/apps
INCLUDES += -IAmbiqSDK/cordio/ble-profiles/sources/apps/app
INCLUDES += -IAmbiqSDK/cordio/ble-profiles/sources/profiles
INCLUDES += -IAmbiqSDK/cordio/ble-profiles/sources/profiles/gatt
INCLUDES += -IAmbiqSDK/cordio/ble-profiles/sources/profiles/gap
INCLUDES += -IAmbiqSDK/cordio/ble-profiles/sources/services
INCLUDES += -IAmbiqSDK/FreeRTOSv10.5.1/Source/include
INCLUDES += -IAmbiqSDK/FreeRTOSv10.5.1/Source/portable/GCC/AMapollo4
INCLUDES += -IAmbiqSDK/CMSIS/AmbiqMicro/Include
INCLUDES += -IAmbiqSDK/CMSIS/ARM/Include
INCLUDES += -IAmbiqSDK/devices
INCLUDES += -IAmbiqSDK/utils
INCLUDES += -Isrc/app
INCLUDES += -Isrc/boards
INCLUDES += -Isrc/boards/rev$(REVISION)
INCLUDES += -Isrc/external/decadriver
INCLUDES += -Isrc/external/segger
INCLUDES += -Isrc/peripherals/include
INCLUDES += -Isrc/tasks
INCLUDES += -Isrc/tasks/bluetooth
INCLUDES += -Isrc/tasks/ranging

VPATH  = AmbiqSDK/bsp/$(BSP)/linker
VPATH += AmbiqSDK/devices
VPATH += AmbiqSDK/utils
VPATH += AmbiqSDK/cordio/wsf/sources/port/freertos
VPATH += AmbiqSDK/cordio/wsf/sources/util
VPATH += AmbiqSDK/cordio/ble-host/sources/hci/ambiq
VPATH += AmbiqSDK/cordio/ble-host/sources/hci/ambiq/cooper
VPATH += AmbiqSDK/cordio/ble-host/sources/sec/common
VPATH += AmbiqSDK/cordio/ble-host/sources/stack/att
VPATH += AmbiqSDK/cordio/ble-host/sources/stack/cfg
VPATH += AmbiqSDK/cordio/ble-host/sources/stack/dm
VPATH += AmbiqSDK/cordio/ble-host/sources/stack/hci
VPATH += AmbiqSDK/cordio/ble-host/sources/stack/l2c
VPATH += AmbiqSDK/cordio/ble-host/sources/stack/smp
VPATH += AmbiqSDK/cordio/ble-profiles/sources/apps/app
VPATH += AmbiqSDK/cordio/ble-profiles/sources/apps/app/common
VPATH += AmbiqSDK/cordio/ble-profiles/sources/profiles/gap
VPATH += AmbiqSDK/cordio/ble-profiles/sources/profiles/gatt
VPATH += AmbiqSDK/cordio/ble-profiles/sources/services
VPATH += AmbiqSDK/FreeRTOSv10.5.1/Source
VPATH += AmbiqSDK/FreeRTOSv10.5.1/Source/portable/MemMang
VPATH += AmbiqSDK/FreeRTOSv10.5.1/Source/portable/GCC/AMapollo4
VPATH += src/app
VPATH += src/boards
VPATH += src/boards/rev$(REVISION)
VPATH += src/external/decadriver
VPATH += src/external/segger
VPATH += src/peripherals/src
VPATH += src/tasks
VPATH += src/tasks/bluetooth
VPATH += src/tasks/ranging

SRC =
SRC += am_devices_cooper.c
SRC += am_devices_led.c
SRC += am_util_ble_cooper.c
SRC += am_util_delay.c
SRC += am_util_stdio.c
SRC += am_util_string.c
SRC += app_db.c
SRC += app_main.c
SRC += app_slave.c
SRC += att_main.c
SRC += att_uuid.c
SRC += atts_ccc.c
SRC += atts_csf.c
SRC += atts_ind.c
SRC += atts_main.c
SRC += atts_proc.c
SRC += atts_read.c
SRC += atts_write.c
SRC += bda.c
SRC += calc128.c
SRC += cfg_stack.c
SRC += dm_adv.c
SRC += dm_adv_leg.c
SRC += dm_conn.c
SRC += dm_conn_slave.c
SRC += dm_conn_slave_leg.c
SRC += dm_conn_sm.c
SRC += dm_dev.c
SRC += dm_main.c
SRC += dm_phy.c
SRC += dm_priv.c
SRC += dm_scan.c
SRC += dm_scan_leg.c
SRC += dm_sec.c
SRC += dm_sec_slave.c
SRC += event_groups.c
SRC += gatt_main.c
SRC += hci_cmd.c
SRC += hci_cmd_phy.c
SRC += hci_core.c
SRC += hci_core_ps.c
SRC += hci_drv_cooper.c
SRC += hci_evt.c
SRC += hci_main.c
SRC += hci_tr.c
SRC += hci_vs_cooper.c
SRC += l2c_main.c
SRC += l2c_slave.c
SRC += list.c
SRC += port.c
SRC += queue.c
SRC += sec_aes.c
SRC += sec_cmac_hci.c
SRC += sec_ecc_hci.c
SRC += sec_main.c
SRC += smp_main.c
SRC += startup_gcc.c
SRC += tasks.c
SRC += timers.c
SRC += wsf_assert.c
SRC += wsf_buf.c
SRC += wsf_msg.c
SRC += wsf_os.c
SRC += wsf_queue.c
SRC += wsf_timer.c
SRC += wsf_trace.c
SRC += wstr.c

SRC += battery.c
SRC += bluetooth.c
SRC += button.c
SRC += buzzer.c
SRC += imu.c
SRC += led.c
SRC += logging.c
SRC += ranging.c
SRC += rtc.c
SRC += storage.c
SRC += system.c

SRC += main.c
SRC += app_tasks.c
SRC += app_task_maintenance.c
SRC += app_task_ranging.c
SRC += ble_task.c
SRC += device_info_service.c
SRC += gap_gatt_service.c
SRC += live_stats_functionality.c
SRC += live_stats_service.c
SRC += maintenance_functionality.c
SRC += maintenance_service.c
SRC += computation_phase.c
SRC += ranging_phase.c
SRC += ranging_task.c
SRC += schedule_phase.c
SRC += scheduler.c
SRC += status_phase.c
SRC += storage_task.c
SRC += subscription_phase.c
SRC += time_aligned_task.c

CSRC = $(filter %.c,$(SRC))
ASRC = $(filter %.s,$(SRC))

OBJS = $(CSRC:%.c=$(CONFIG)/%.o)
OBJS+= $(ASRC:%.s=$(CONFIG)/%.o)

DEPS = $(CSRC:%.c=$(CONFIG)/%.d)
DEPS+= $(ASRC:%.s=$(CONFIG)/%.d)

LIBS = AmbiqSDK/bsp/$(BSP)/gcc/bin/libam_bsp.a
LIBS += AmbiqSDK/mcu/$(PART)/hal/mcu/gcc/bin/libam_hal.a

CFLAGS = -mthumb -mcpu=$(CPU) -mfpu=$(FPU) -mfloat-abi=$(FABI)
CFLAGS+= -ffunction-sections -fdata-sections -fomit-frame-pointer
CFLAGS+= -MMD -MP -std=c99 -Wall -O3
CFLAGS+= $(DEFINES)
CFLAGS+= $(INCLUDES)

LFLAGS = -mthumb -mcpu=$(CPU) -mfpu=$(FPU) -mfloat-abi=$(FABI)
LFLAGS+= -nostartfiles -static
LFLAGS+= -Wl,--gc-sections,--entry,Reset_Handler,-Map,$(CONFIG)/$(TARGET).map
LFLAGS+= -Wl,--start-group -lm -lc -lgcc -lnosys $(LIBS) -Wl,--end-group
LFLAGS+= -Wl,--whole-archive $(DW_LIBRARY) -Wl,--no-whole-archive
LFLAGS+= -Wl,--print-memory-usage

CPFLAGS = -Obinary
ODFLAGS = -S

#### Rules ####
all: directories $(CONFIG)/$(TARGET).bin

directories: $(CONFIG)

$(CONFIG):
	@mkdir -p $@

$(CONFIG)/%.o: %.c $(CONFIG)/%.d
	@echo " Compiling $<" ;\
	$(CC) -c $(CFLAGS) $< -o $@

$(CONFIG)/%.o: %.s $(CONFIG)/%.d
	@echo " Assembling $<" ;\
	$(CC) -c $(CFLAGS) $< -o $@

$(CONFIG)/$(TARGET).axf: $(OBJS) $(LIBS)
	@echo " Linking $@" ;\
	$(CC) -Wl,-T,$(LINKER_FILE) -o $@ $(OBJS) $(LFLAGS)

$(CONFIG)/$(TARGET).bin: $(CONFIG)/$(TARGET).axf
	@echo " Copying $@..." ;\
	$(CP) $(CPFLAGS) $< $@ ;\
	$(OD) $(ODFLAGS) $< > $(CONFIG)/$(TARGET).lst
	@$(SIZE) $(OBJS) $(LIBS) $(CONFIG)/$(TARGET).axf >$(CONFIG)/$(TARGET).size

clean:
	@echo "Cleaning..." ;\
	$(RM) -rf $(CONFIG)
$(CONFIG)/%.d: ;

# Include JTag flashing Makefile
include Jtag.mk

# Automatically include any generated dependencies
-include $(DEPS)

endif
.PHONY: all clean directories
