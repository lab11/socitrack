Module software
=================

This firmware runs on the module and provides the basis
for the ranging system. Ideally, each module will ship with
this firmware already installed.


Programming
-----------

> For more detailed programming instructions, [see the Provisioning instructions](../../doc/Provisioning.md#programming-the-stm)

1. Program the STM32F091CC, and set the ID:

        make flash ID=c0:98:e5:42:00:01
        
    If you have multiple JLink boxes attached to your computer:
    
        SEGGER_SERIAL=<segger id> make flash ID=c0:98:e5:42:00:01

1. To output J-Link RTT packets, use the J-Link Commander `JLinkExe`:
    
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

