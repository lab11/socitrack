TotTag Firmware
===============

This firmware runs on a Nordic nRF52840 BLE microcontroller and implements the
TotTag interaction tracking application. Its purpose is:

- To provide a simple user interface to interact with, configure, and restart a device
- To access ranging and location data and offload it to an SD Card or PC
- To discover neighboring TotTags and start the SquarePoint module upon network discovery


Programming and Debugging
-------------------------

For detailed programming instructions, refer to the
[Provisioning Documentation](../../../doc/Provisioning.md).

To program the microcontroller, connect a SEGGER J-Link Programmer to the board
via USB, then enter the following command, replacing `<segger_id>` with the 
9-digit serial number of your J-Link Programmer:

> `make SEGGER_SERIAL=<segger_id> flash`

If this is the first time you are programming the board (or you have a specific 
need to change the unique Device ID on the target), you may set the correct ID 
by using the following command, replacing `<device_id>` with your deired 
Device ID, such as `c0:98:e5:42:00:01`:

> `make SEGGER_SERIAL=<segger_id> ID=<device_id> flash`

If you are flashing the board for use in a non-production debugging environment,
append `DEBUG_MODE=1` to the flash command to allow certain debugging tasks
and device checks to run:

> `make SEGGER_SERIAL=<segger_id> DEBUG_MODE=1 flash`

To display any J-Link debugging messages, enter the following commands in order
(note that the `JLinkExe` command may be shortened to `JLink` on a
Windows-based PC):

> `JLinkExe -Device NRF52840_XXAA -if SWD -speed 4000 -RTTTelnetPort 9201 -SelectEmuBySN <J-Link S/N>`

> `connect`

> `r`

> `g`

This will create and open a connection to the microcontroller, hard-reset the
microcontroller (command `r`), then start the firmware application (command
`g`).

You can view the debugging output coming in over this connection in real time
by opening up a different terminal and entering:

> `telnet localhost 9201`


Resetting the Real-Time Clock
-----------------------------

Each TotTag contains an on-board Real-Time Clock (RTC) that continues to keep
accurate universal time even when the device is powered down or low on battery.

Should you find that the RTC clock value is incorrect, it can be reconfigured by
flashing the microcontroller as follows:

> `make SEGGER_SERIAL=<segger_id> FORCE_RTC_RESET=1 flash`

> <--- DEVICE IS NOW FLASHED AND RTC IS SET TO THE CORRECT DATE AND TIME --->

> `make SEGGER_SERIAL=<segger_id> flash`

**IMPORTANT:** You *must* execute the same command twice, with the second command
removing the `FORCE_RTC_RESET=1` flag; otherwise, the RTC will be reset to 
the compilation time every time the device is restarted.

Also note, if you need to flash the TotTag firmware onto an older board revision,
you should use the following command to do so:

> `make SEGGER_SERIAL=<segger_id> BOARD_REV=G flash`

where the `BOARD_REV` flag should be set to the correct board revision letter.


LED Color Codes
---------------

The RGB LED labeled `D2` on the hardware board is used to indicate the current
status of the running TotTag firmware. The meaning of its colors are as
follows:

- **White**: Critical hardware initialization problem.
- **Red**: SD Card is missing or not properly inserted.
- **Purple**: Cannot communicate with the SquarePoint module over I2C.
- **Blue**: Board is initialized, but has not detected any other devices in range.
- **Orange**: A TotTag network is detected, but the SquarePoint module is not yet running.
- **Green**: A TotTag network is detected, and the SquarePoint module is running successfully.


Bluetooth Functionality
-----------------------

A TotTag advertises via Bluetooth Low-Energy (LE) according to the
[Eddystone](https://github.com/google/eddystone) protocol and provides a
BLE service for manipulating all ranging operations. Characteristics in that
service provide control and data access to the SquarePoint ranging module:

- **Ranging Service**: UUID: `d68c3152-a23f-ee90-0c45-5231395e5d2e`
- **Localization Characteristic**: Short UUID: `3153`. Provides direct access
                                   to data published by the SquarePoint module.
- **Configuration Characteristic**: Short UUID: `3154`. Allows updating a
                                    device role or setting the current epoch time.
- **Enable Characteristic**: Short UUID: `3155`. Disables or enables the
                             SquarePoint ranging operation by writing a 0 or 1
                             for each respective operation.
- **Status Characteristic**: Short UUID: `3156`. Read-only; returns whether the
                             SquarePoint ranging module is currently enabled.
- **Calibration Characteristic**: Short UUID: `3157`. Enables the device to be
                                  put into "Calibration Mode" by writing a value
                                  of 0, 1, or 2, where each value represents a
                                  calibration index in the SquarePoint calibration
                                  algorithm. The device with calibration index
                                  0 will immediately begin a calibration procedure
                                  and should be assigned last.
  