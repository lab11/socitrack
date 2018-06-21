nRF BLE Code
============

The software in this folder runs on the nRF51822 BLE chip. Look in the `/apps`
folder for the software that runs on the different hardware platforms.

We leverage the `nrf5x-base` repository for some helper code and
build files.

Remote debugging
----------------

**Attention:** Before debugging, don't forget to run `make debug -B` first to create the debug symbol list (appends compiler flag `-d`).

If you intend to use a remote debugging tool for step-by-step debugging (such as J-Link),
we suggest extending the `Makefile.posix` file in `nrf5x-base/make` with the following lines:

    remotedbg: debug-gdbinit
    	$(TERMINAL) "$(JLINKGDBSERVER) -port $(GDB_PORT_NUMBER)"

This will allow you to start a JLinkGDBServer on the remote device which you can then connect to from your host device.
This requires the following configurations for your GDB (usually directly in the *Debug* configs of your IDE):

- GDB: `arm-none-eabi-gdb`
- 'target remote' args: `:2331`
- Symbol file: `/nrf51822/apps/${app}/${app_name}.elf`

*Note*:  An alternative way to achieve the same goal without adapting the files manually is by running `make startdebug`
and then closing the GDB window using `quit`.