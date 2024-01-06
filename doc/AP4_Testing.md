Testing
============

## Run tests on Apollo4 based TotTags

### Flash the boards
1. Navigate to the test folder

        cd socitrack/software/firmware/tests

2. Clean up the old builds

        make clean
3. Program the device ID (using `ff` as an example here)

        make ID=c0:98:e5:42:00:ff UID
4. Program the test firmware (available BOARD_REV for apollo4 based TotTag: `EVB`, `I`, `K`, `L`, see the revision letter on the side of the TotTag name on the board; for the list of available tests, see the makefile)
```bash
#test individual peripheral
make ranging_radio BOARD_REV=EVB
#test the full app
make full BOARD_REV=M
```

### Debug the boards

#### Read real-time SWO output
With the JLink debugger connected, run the following command.


        jLinkSWOViewerCL -swoattach 1 -swofreq 1000000 -device AMA4B2KP-KBR -itmport 0x0

You should be seeing the debugging messages coming after that.

#### Resetting the device without restarting

        JLinkExe -Device AMA4B2KP-KBR -if SWD -speed 4000 -RTTTelnetPort 9201

Then in the JLink interface, use command `r` and `g` to reset the device.

When the device is reset without restarting, the RTC value would not get lost.

#### GDB based debugging
1. In one terminal tab, run the following command to start the GDB server

        JLinkGDBServer -if swd -device AMAP42KK-KBR -endian little -speed 1000 -port 2331 -swoport 2332 -telnetport 2333 -RTTTelnetport 2334 -vd -ir -localhostonly 1 -singlerun -strict -timeout 0

2. In another terminal tab, navigate to the `bin` folder with the compiled files

        cd socitrack/software/firmware/tests/bin

3. invoke the GDB debugger

        arm-none-eabi-gdb

4. With the GDB debugger starting, load the `.axf` file and start the test

        file TestRangingRadio.axf
        target remote localhost:2331
        load
        mon reset 0