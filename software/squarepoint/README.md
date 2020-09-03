SquarePoint Firmware
====================

This firmware runs on an STM32F091CC microcontroller and provides the basis for
the SociTrack ranging system.


Programming and Debugging
-------------------------

For detailed programming instructions, refer to the
[Provisioning Documentation](../../doc/Provisioning.md).

To program the microcontroller, connect a SEGGER J-Link Programmer to the board
via USB, then enter the following command, making sure to set the correct Device
ID for the target:

> `make SEGGER_SERIAL=<segger_id> ID=c0:98:e5:42:00:01 flash`

To display any J-Link debugging messages, enter the following commands in order
(note that the `JLinkExe` command may be shortened to `JLink` on a
Windows-based PC):

> `JLinkExe -Device STM32F091CC -if SWD -speed 4000 -RTTTelnetPort 9200 -SelectEmuBySN <J-Link S/N>`

> `connect`

> `r`

> `g`

This will create and open a connection to the microcontroller, hard-reset the
microcontroller (command `r`), then start the firmware application (command
`g`).

You can view the debugging output coming in over this connection in real time
by opening up a different terminal and entering:

> `telnet localhost 9200`


LED Color Codes
---------------

The RGB LED labeled `D3` on the hardware board is used to indicate the current
status of the running SquarePoint firmware. The meaning of its colors are as
follows:

- **Red**: Board has not finished initializing; usually indicates a hardware error if it persists.
- **Blue**: Board is initialized, but has not detected any other devices in range.
- **Orange**: Board is attempting to take part in ranging, but failed on its previous attempt.
- **Green**: Board is attempting to range and succeeded on its previous attempt.

Note that the LED will blink every time it receives or transmits a communication
schedule to or from the network. As such, the LEDs on most devices will remain
BLUE until they are brought into contact with one or more ranging devices, at
which point they will turn ORANGE while attempting to connect to the network.
The ORANGE LED will start blinking when regular schedule reception has begun,
then the LED will change to a blinking GREEN once the ranging results are
successfully being computed. If the device leaves the network or moves out of
range of the other devices, it will first turn ORANGE as it starts failing to
calculate ranges and will then turn BLUE to indicate that it is no longer part of
a ranging network.


I<sup>2</sup>C API
-------

The I<sup>2</sup>C message interface between the SquarePoint firmware and any external
hosting application is as follows:

Incoming I<sup>2</sup> messages may be any of the following:

- **0x01**: HOST_CMD_INFO: Indicates a request for the availability of this
                           SquarePoint module. If the module does not reply or
                           replies with a pre-defined 3-byte `NULL` packet
                           containing (`0xAA, 0xAA, 0`), the external host
                           application will know that something is wrong with
                           the SquarePoint module and it is unavailable for use.
                           If SquarePoint replies with a pre-defined 3-byte `INFO`
                           packet containing (`0xB0, 0x1A, 1`),
                           then the SquarePoint module is ready to accept
                           commands from the host and begin ranging functionality.
- **0x02**: HOST_CMD_READ_CALIBRATION: Indicates a request for any UWB antenna
                                       calibration data.
- **0x03**: HOST_CMD_READ_PACKET_LENGTH: Requests the number of bytes to be
                                         returned from the next packet read
                                         command.
- **0x04**: HOST_CMD_READ_PACKET: Requests a read of the next data packet.
- **0x05**: HOST_CMD_START: Starts the SquarePoint ranging module with parameters
                            specified inside of the request packet.
- **0x06**: HOST_CMD_STOP: Stops the currently running ranging module.
- **0x07**: HOST_CMD_RESET: Causes the SquarePoint module to perform a full
                            device reset.
- **0x08**: HOST_CMD_SET_TIME: Updates the network time epoch being used by the
                               SquarePoint firmware.
- **0x09**: HOST_CMD_WAKEUP: Causes the SquarePoint module to wake up so that it
                             may begin its next ranging round. This is only used
                             if external wakeups are enabled, which by default,
                             they are not
- **0x0A**: HOST_CMD_ACK: Indicates acknowledgment of message reception by
                          the external host to a ping or STOPPED notification
                          made by the SquarePoint module.

Outgoing I<sup>2</sup>C messages may be any of the following:

- **0x01**: HOST_SEND_RANGES: Used to send ranging results from the current
                              epoch to the external host application.
- **0x02**: HOST_SEND_CALIBRATION: Sends UWB antenna calibration values to the
                                   external host.
- **0x03**: HOST_REQUEST_WAKEUP: Requests that the host wake up the SquarePoint
                                 module after a specified length of time.
- **0x04**: HOST_NOTIFY_STOPPED: Informs the host application that the
                                 SquarePoint module has stopped.
- **0x05**: HOST_REQUEST_TIME: Requests an update of the current time epoch from
                               the external host application.
- **0x06**: HOST_PING_REQUEST: Sends a ping request to the host application to
                               make sure that it is still running and the
                               I<sup>2</sup>C connectivity is still alive.
