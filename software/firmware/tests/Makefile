PROJECT := SociTrack
CONFIG := bin
SHELL := /bin/bash

ifdef BOARD_REV
REVISION := $(BOARD_REV)
else
REVISION := M
endif

NONBLOCKING ?= 0

ifeq ($(REVISION), I)
BSP = apollo4_blue
PART = apollo4b
PART_DEF = AM_PART_APOLLO4B
else
BSP = apollo4_blue_pro
PART = apollo4p
PART_DEF = AM_PART_APOLLO4P
endif

$(info Building test for Revision $(REVISION):)
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
DEFINES += -DAM_PACKAGE_BGA
DEFINES += -DDM_NUM_ADV_SETS=1
DEFINES += -DWSF_TRACE_ENABLED
DEFINES += -DAM_DEBUG_PRINTF
DEFINES += -Dgcc

DW_LIBRARY := ../src/external/decadriver/libdwt_uwb_driver-m4-hfp-6.0.7.a
LINKER_FILE := ../AmbiqSDK/bsp/$(BSP)/linker/socitrack.ld
STARTUP_FILE := ../AmbiqSDK/bsp/$(BSP)/linker/startup_gcc.c

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

INCLUDES  = -I../AmbiqSDK/bsp/$(BSP)
INCLUDES += -I../AmbiqSDK/mcu/$(PART)
INCLUDES += -I../AmbiqSDK/mcu/$(PART)/hal
INCLUDES += -I../AmbiqSDK/mcu/$(PART)/hal/mcu
INCLUDES += -I../AmbiqSDK/cordio/wsf/include
INCLUDES += -I../AmbiqSDK/cordio/wsf/sources
INCLUDES += -I../AmbiqSDK/cordio/wsf/sources/port/freertos
INCLUDES += -I../AmbiqSDK/cordio/wsf/sources/util
INCLUDES += -I../AmbiqSDK/cordio/ble-host/include
INCLUDES += -I../AmbiqSDK/cordio/ble-host/sources/hci/ambiq
INCLUDES += -I../AmbiqSDK/cordio/ble-host/sources/hci/ambiq/cooper
INCLUDES += -I../AmbiqSDK/cordio/ble-host/sources/sec/common
INCLUDES += -I../AmbiqSDK/cordio/ble-host/sources/stack/att
INCLUDES += -I../AmbiqSDK/cordio/ble-host/sources/stack/cfg
INCLUDES += -I../AmbiqSDK/cordio/ble-host/sources/stack/dm
INCLUDES += -I../AmbiqSDK/cordio/ble-host/sources/stack/hci
INCLUDES += -I../AmbiqSDK/cordio/ble-host/sources/stack/l2c
INCLUDES += -I../AmbiqSDK/cordio/ble-host/sources/stack/smp
INCLUDES += -I../AmbiqSDK/cordio/ble-profiles/include/app
INCLUDES += -I../AmbiqSDK/cordio/ble-profiles/sources/apps
INCLUDES += -I../AmbiqSDK/cordio/ble-profiles/sources/apps/app
INCLUDES += -I../AmbiqSDK/cordio/ble-profiles/sources/profiles
INCLUDES += -I../AmbiqSDK/cordio/ble-profiles/sources/profiles/gatt
INCLUDES += -I../AmbiqSDK/cordio/ble-profiles/sources/profiles/gap
INCLUDES += -I../AmbiqSDK/cordio/ble-profiles/sources/services
INCLUDES += -I../AmbiqSDK/FreeRTOSv10.5.1/Source/include
INCLUDES += -I../AmbiqSDK/FreeRTOSv10.5.1/Source/portable/GCC/AMapollo4
INCLUDES += -I../AmbiqSDK/CMSIS/AmbiqMicro/Include
INCLUDES += -I../AmbiqSDK/CMSIS/ARM/Include
INCLUDES += -I../AmbiqSDK/devices
INCLUDES += -I../AmbiqSDK/utils
INCLUDES += -I../src/app
INCLUDES += -I../src/boards
INCLUDES += -I../src/boards/rev$(REVISION)
INCLUDES += -I../src/external/decadriver
INCLUDES += -I../src/peripherals/include
INCLUDES += -I../src/tasks
INCLUDES += -I../src/tasks/bluetooth
INCLUDES += -I../src/tasks/ranging
INCLUDES += -I./peripherals
INCLUDES += -I./tasks
INCLUDES += -I./tools

VPATH  = ../AmbiqSDK/bsp/$(BSP)/linker
VPATH += ../AmbiqSDK/devices
VPATH += ../AmbiqSDK/utils
VPATH += ../AmbiqSDK/cordio/wsf/sources/port/freertos
VPATH += ../AmbiqSDK/cordio/wsf/sources/util
VPATH += ../AmbiqSDK/cordio/ble-host/sources/hci/ambiq
VPATH += ../AmbiqSDK/cordio/ble-host/sources/hci/ambiq/cooper
VPATH += ../AmbiqSDK/cordio/ble-host/sources/sec/common
VPATH += ../AmbiqSDK/cordio/ble-host/sources/stack/att
VPATH += ../AmbiqSDK/cordio/ble-host/sources/stack/cfg
VPATH += ../AmbiqSDK/cordio/ble-host/sources/stack/dm
VPATH += ../AmbiqSDK/cordio/ble-host/sources/stack/hci
VPATH += ../AmbiqSDK/cordio/ble-host/sources/stack/l2c
VPATH += ../AmbiqSDK/cordio/ble-host/sources/stack/smp
VPATH += ../AmbiqSDK/cordio/ble-profiles/sources/apps/app
VPATH += ../AmbiqSDK/cordio/ble-profiles/sources/apps/app/common
VPATH += ../AmbiqSDK/cordio/ble-profiles/sources/profiles/gap
VPATH += ../AmbiqSDK/cordio/ble-profiles/sources/profiles/gatt
VPATH += ../AmbiqSDK/cordio/ble-profiles/sources/services
VPATH += ../AmbiqSDK/FreeRTOSv10.5.1/Source
VPATH += ../AmbiqSDK/FreeRTOSv10.5.1/Source/portable/MemMang
VPATH += ../AmbiqSDK/FreeRTOSv10.5.1/Source/portable/GCC/AMapollo4
VPATH += ../src/app
VPATH += ../src/boards
VPATH += ../src/boards/rev$(REVISION)
VPATH += ../src/external/decadriver
VPATH += ../src/peripherals/src
VPATH += ../src/tasks
VPATH += ../src/tasks/bluetooth
VPATH += ../src/tasks/ranging
VPATH += ./peripherals
VPATH += ./tasks
VPATH += ./tools

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
SRC += hci_dbg_trc.c
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

.PHONY: all program clean battery ble_and_range ble_reset bluetooth button buzzer full imu led logging power_off ranging ranging_radio ranging_power rtc_set rtc storage system
.PRECIOUS: $(CONFIG)/%.axf
.SECONDEXPANSION:

all:
	$(error Make targets include: battery ble_and_range ble_reset bluetooth button buzzer full imu led logging power_off ranging ranging_radio ranging_power rtc_set rtc storage system)

# TODO: Create separate targets for non-flashing versions
program:
ifneq ($(BUILT_BY), eclipse)
	printf "r\n" > $(CONFIG)/flash.jlink
	printf "loadfile $(CONFIG)/$(TARGET).bin $(FLASH_START)\nr\ng\nexit\n" >> $(CONFIG)/flash.jlink
	$(JLINK) $(JLINK_FLAGS) $(CONFIG)/flash.jlink
endif

battery: TARGET = TestBattery
battery: SRC += test_battery.c
battery: $(CONFIG) $(CONFIG)/test_battery.o $(CONFIG)/$$(TARGET).bin program

ble_and_range: TARGET = TestBleAndRanging
ble_and_range: SRC += test_ble_and_ranging.c
ble_and_range: CFLAGS += -D_TEST_BLE_RANGING_TASK -D__USE_FREERTOS__
ble_and_range: $(CONFIG) $(CONFIG)/test_ble_and_ranging.o $(CONFIG)/$$(TARGET).bin program

ble_reset: TARGET = TestBleReset
ble_reset: SRC += test_ble_reset.c
ble_reset: CFLAGS += -D_TEST_BLE_RANGING_TASK -D__USE_FREERTOS__
ble_reset: $(CONFIG) $(CONFIG)/test_ble_reset.o $(CONFIG)/$$(TARGET).bin program

ble_range_imu: TARGET = TestBleRangingIMU
ble_range_imu: SRC += test_ble_range_imu.c
ble_range_imu: CFLAGS += -D_TEST_BLE_RANGING_TASK -D_TEST_IMU_DATA -D__USE_FREERTOS__
ble_range_imu: $(CONFIG) $(CONFIG)/test_ble_range_imu.o $(CONFIG)/$$(TARGET).bin program

bluetooth: TARGET = TestBluetooth
bluetooth: SRC += test_bluetooth.c
bluetooth: CFLAGS += -D_TEST_BLUETOOTH -D__USE_FREERTOS__
bluetooth: $(CONFIG) $(CONFIG)/test_bluetooth.o $(CONFIG)/$$(TARGET).bin program

button: TARGET = TestButton
button: SRC += test_button.c
button: $(CONFIG) $(CONFIG)/test_button.o $(CONFIG)/$$(TARGET).bin program

buzzer: TARGET = TestBuzzer
buzzer: SRC += test_buzzer.c
buzzer: $(CONFIG) $(CONFIG)/test_buzzer.o $(CONFIG)/$$(TARGET).bin program

full: TARGET = TestFull
full: SRC += main.c
full: CFLAGS += -D__USE_FREERTOS__
full: $(CONFIG) $(CONFIG)/main.o $(CONFIG)/$$(TARGET).bin program

imu: TARGET = TestIMU
imu: SRC += test_imu.c
imu: CFLAGS += -DNONBLOCKING=$(NONBLOCKING)
imu: $(CONFIG) $(CONFIG)/test_imu.o $(CONFIG)/$$(TARGET).bin program

led: TARGET = TestLED
led: SRC += test_led.c
led: $(CONFIG) $(CONFIG)/test_led.o $(CONFIG)/$$(TARGET).bin program

logging: TARGET = TestLogging
logging: SRC += test_logging.c
logging: $(CONFIG) $(CONFIG)/test_logging.o $(CONFIG)/$$(TARGET).bin program

power_off: TARGET = TestPowerOff
power_off: SRC += test_power_off.c
power_off: $(CONFIG) $(CONFIG)/test_power_off.o $(CONFIG)/$$(TARGET).bin program

ranging: TARGET = TestRanging
ranging: SRC += test_ranging_task.c
ranging: CFLAGS += -D_TEST_RANGING_TASK -D__USE_FREERTOS__
ranging: $(CONFIG) $(CONFIG)/test_ranging_task.o $(CONFIG)/$$(TARGET).bin program

ranging_radio: TARGET = TestRangingRadio
ranging_radio: SRC += test_ranging_radio.c
ranging_radio: $(CONFIG) $(CONFIG)/test_ranging_radio.o $(CONFIG)/$$(TARGET).bin program

ranging_power: TARGET = TestRangingPower
ranging_power: SRC += ranging_power_test.c
ranging_power: $(CONFIG) $(CONFIG)/ranging_power_test.o $(CONFIG)/$$(TARGET).bin program

rtc_set: TARGET = TestRTCSet
rtc_set: SRC += test_rtc.c
rtc_set: CFLAGS += -DSET_RTC
rtc_set: $(CONFIG) $(CONFIG)/test_rtc.o $(CONFIG)/$$(TARGET).bin program

rtc: TARGET = TestRTC
rtc: SRC += test_rtc.c
rtc: $(CONFIG) $(CONFIG)/test_rtc.o $(CONFIG)/$$(TARGET).bin program

storage: TARGET = TestStorage
storage: SRC += test_storage.c
storage: $(CONFIG) $(CONFIG)/test_storage.o $(CONFIG)/$$(TARGET).bin program

system: TARGET = TestSystem
system: SRC += test_system.c
system: $(CONFIG) $(CONFIG)/test_system.o $(CONFIG)/$$(TARGET).bin program

OBJS = $(SRC:%.c=$(CONFIG)/%.o)
DEPS = $(SRC:%.c=$(CONFIG)/%.d)

LIBS  = ../AmbiqSDK/bsp/$(BSP)/gcc/bin/libam_bsp.a
LIBS += ../AmbiqSDK/mcu/$(PART)/hal/mcu/gcc/bin/libam_hal.a

CFLAGS = -mthumb -mcpu=$(CPU) -mfpu=$(FPU) -mfloat-abi=$(FABI)
CFLAGS+= -ffunction-sections -fdata-sections -fno-omit-frame-pointer
CFLAGS+= -MMD -MP -std=c99 -Wall -g -O0
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

$(CONFIG):
	@mkdir -p $@

$(CONFIG)/%.o: %.c $(CONFIG)/%.d
	@echo " Compiling $<" ;\
	$(CC) -c $(CFLAGS) $< -o $@

$(CONFIG)/%.o: %.s $(CONFIG)/%.d
	@echo " Assembling $<" ;\
	$(CC) -c $(CFLAGS) $< -o $@

$(CONFIG)/%.axf: $(OBJS) $(LIBS)
	@echo " Linking $@" ;\
	$(CC) -Wl,-T,$(LINKER_FILE) -o $@ $(OBJS) $(LFLAGS)

$(CONFIG)/%.bin: $(CONFIG)/%.axf
	@echo " Copying $@..." ;\
	$(CP) $(CPFLAGS) $< $@ ;\
	$(OD) $(ODFLAGS) $< > $(CONFIG)/$(TARGET).lst
	@$(SIZE) $(OBJS) $(LIBS) $(CONFIG)/$(TARGET).axf >$(CONFIG)/$(TARGET).size

clean:
	@echo "Cleaning..." ;\
	$(RM) -rf $(CONFIG)
$(CONFIG)/%.d: ;

# Include JTag flashing Makefile
include ../Jtag.mk

# Automatically include any generated dependencies
-include $(DEPS)

endif
