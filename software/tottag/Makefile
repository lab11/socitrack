SUBDIRS        := firmware

.PHONY: all flash debug clean $(SUBDIRS)

all : $(SUBDIRS)

flash : $(SUBDIRS)

debug: $(SUBDIRS)

clean : $(SUBDIRS)

$(SUBDIRS) :
	$(MAKE) -C $@ -f Makefile $(MAKECMDGOALS) ID=${ID} BLE_CALIBRATION=${BLE_CALIBRATION} FORCE_RTC_RESET=${FORCE_RTC_RESET} BOARD_REV=${BOARD_REV}
