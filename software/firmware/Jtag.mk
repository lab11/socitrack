# Ensure that this file is only included once
ifndef JTAG_MAKEFILE
JTAG_MAKEFILE = 1

# JTAG tools
ifeq ($(OS),Windows_NT)
JLINK = JLink
else
JLINK = JLinkExe
endif

# Default port for GDB
GDB_PORT_NUMBER ?= 2331

# Configuration flags for JTAG tools
JLINK_FLAGS = -device AMA4B2KK-KBR -if swd -speed 4000

# Allow users to select a specific JTAG device with a variable
ifdef SEGGER_SERIAL
  JLINK_FLAGS += -SelectEmuBySn $(SEGGER_SERIAL)
endif

# ID Flash Rule
ifdef ID
  # Write the ID to flash as well
  ID_BYTES = $(subst :, ,$(ID))
  NUM_ID_BYTES = $(words $(ID_BYTES))
  ifneq ($(NUM_ID_BYTES),6)
    $(error "Invalid number of bytes in ID string (expecting 6)")
  endif
  ID_FIRST = $(word 1,$(ID_BYTES))$(word 2,$(ID_BYTES))
  ID_SECON = $(word 3,$(ID_BYTES))$(word 4,$(ID_BYTES))$(word 5,$(ID_BYTES))$(word 6,$(ID_BYTES))
endif

# ID-Only Flash Rule
.PHONY: UID flash
UID: $(CONFIG)
	printf "r\n" > $(CONFIG)/flash.jlink
ifdef ID
	printf "w4 $(ID_FLASH_LOCATION), 0x$(ID_SECON) 0x$(ID_FIRST)\n" >> $(CONFIG)/flash.jlink
	printf "exit\n" >> $(CONFIG)/flash.jlink
	$(JLINK) $(JLINK_FLAGS) $(CONFIG)/flash.jlink
endif

# Code Flash Rule
flash: all
	printf "r\n" > $(CONFIG)/flash.jlink
	printf "loadfile $(CONFIG)/$(TARGET).bin $(FLASH_START)\nr\ng\nexit\n" >> $(CONFIG)/flash.jlink
	$(JLINK) $(JLINK_FLAGS) $(CONFIG)/flash.jlink

endif
