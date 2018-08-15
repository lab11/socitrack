J-Link Real-Time Terminal (RTT)
===============================

The SEGGER J-Link Debugger can be used to directly print debugging output to the console by mapping a specific section of RAM directly as console output. Compared to UART, the transmissions take much less time (order of microseconds vs order of tens of milliseconds) and less setup time. Furthermore, it is also possible on devices such as the nRF where physical access to pins is difficult to obtain.

Usage
-----

1. Start the J-Link Commander:	`$ JLinkExe`
2. Connect to the device:
	- `J-Link>connect`
	- `Device> <press enter>`
	- `TIF>s`
	- `Speed> <press enter>`
3. Connect the RTT viewer:	`$ JLinkRTTClient`
4. If you have multiple RTT sessions in parallel, do the following:

        $ JLinkExe -Device NRF51822 -if SWD -speed 4000 -SelectEmuBySN <J-Link S/N> -RTTTelnetPort 9300
        J-Link>connect
        
        $ telnet localhost 9300

To print inside the code, import `SEGGER_RTT.h`:
- `debug_msg(const char* c)`: Prints a string on the terminal
- `debug_msg_int(int i)`: Print an integer as ASCII

You can disable debug output using `DEBUG_OUTPUT` in `SEGGER_RTT_Conf.h`.
