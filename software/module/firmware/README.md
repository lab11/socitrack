Module Firmware
=================

This is the software that runs on the module.


Build
-----

    make


Install
-------

To configure the devices, you must also define their EUI which usually is the same as their BLE address. For any node which has already done calibration:

    make flash ID=c0:98:e5:42:00:01

or (to specify the SEGGER connected to the STM):

    make flash SEGGER_SERIAL=303202100 ID=c0:98:e5::42:00:01
    
If the default values should be used, make use of the special address

    make flash ID=c0:98:e5:42:00:FF
    
Be aware that this influences scheduling, as the node directly uses the last byte for its EUI. In such a case, one should handle the EUI on the application level using the configured ID (handed over as a #define directly through the compiler).


**Install options:**

To not use the carrier and simply debug on the module, use the line below. Please make sure to adjust the roles directly in "main.c" for the different boards.

    make flash BYPASS_HOST_INTERFACE=1

Additionally, to perform a range test:

    make flash RANGE_TEST=1
    
Additionally, to enforce calibration mode (for debugging):

    make flash CALIBRATION=1


Debug
-----

If you have multiple tags, it's generally useful to fully specify everything. You'll need two terminals for this:

    JLinkExe -AutoConnect 1 -Device STM32F091CC -if SWD -speed 4000 -SelectEmuBySN <Written on back of JLink> -RTTTelnetPort <Pick A Number, I match ID>

    telnet localhost <The port you chose above>

Note that you will have to manually kill off this JLink session **before** trying to reprogram the device.
