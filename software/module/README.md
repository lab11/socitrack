Module software
=================

This firmware runs on the module and provides the basis
for the ranging system. Ideally, each module will ship with
this firmware already installed.

The following steps demonstrate how to re-program them:

1. Get the arm-gcc compiler for your platform: https://launchpad.net/gcc-arm-embedded

2. In the `/firmware` folder, build the software:

        make

3. Programming the STM32F091CC on the module requires a [JLink JTAG programmer](
https://www.segger.com/jlink-general-info.html) and the [JLink programming software](
https://www.segger.com/jlink-software.html). To connect them to the board, you further require a ARM JTAG to Tag-Connect
adapter from either of two sources:

- Lab11 version: https://github.com/lab11/jtag-tagconnect/tree/master/hardware/jlink_to_tag/rev_d
- Off-the-shelf version: https://www.segger.com/jlink-6-pin-needle-adapter.html

4. Program the STM32F091CC, and set the ID:

        make flash ID=c0:98:e5:42:00:01
        
    If you have multiple JLink boxes attached to your computer:
    
        SEGGER_SERIAL=<segger id> make flash ID=c0:98:e5:42:00:01

5. To output J-Link RTT packets, use the J-Link Commander `JLinkExe`:
    
        JLinkExe -Device STM32F091CC -if SWD -speed 4000
        
   You can then use the J-Link RTT Viewer to see and log the packets:
   
        $ JLinkRTTClient
        
   To specify the port you want to listen on, do the following:
   
        $ JLinkExe -Device STM32F091CC -if SWD -speed 4000 -SelectEmuBySN <J-Link S/N> -RTTTelnetPort 9200
        J-Link>connect
        
        $ telnet localhost 9200

LED color code
--------------

The central RGB LED `D3` in the middle of the board on the right, east of the STM32F091CC, is the status LED for this app.
It demonstrates the current state of the state-machine on the module:

-  **Red**: Board did not (yet) finish initialization; usually stuck trying to access the DW.
-  **Blue**: Board is ready for a UWB network but is not connected to another device.
-  **Green**: Device is part of a UWB network and is receiving schedules.
-  **Green with white flashes**: Device is the host of a UWB network; the white flash signals a schedule distribution.

I2C API
-------

The interface between the host and module is described in `firmware/API.md`.

