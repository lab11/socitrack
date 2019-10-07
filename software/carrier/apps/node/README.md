TotTag BLE App
==============

This apps provides the BLE interface for the TotTag localization tag.

It is used for three primary use cases:
- to provide a simple user interface to interact with, configure and restart the device
- to access ranging and location data and offload it easily (using the provided JS scripts)
- for neighbour discovery to start-up the module upon discovery of a network

Programming
-----------
    
> For more detailed programming instructions, [see the Provisioning instructions](../../../../doc/Provisioning.md#programming-the-nrf)

    make flash SEGGER_SERIAL=xxxxxxxxx BLE_ADDRESS=c0:98:e5:42:00:01
    
For easy debugging, you can circumvent the BLE user interface and directly programm a role:

    make flash BYPASS_USER_INTERFACE=1 ROLE=INITIATOR GLOSSY_MASTER=1    

LED color code
--------------

The central RGB LED `D2` at the top of the board, northwest of the nRF52840, is the status LED for this app.
It demonstrates the current state of the state-machine on the carrier board:

-  **Red**: Board did not (yet) finish initialization; usually stuck trying to access peripherals such as SD card.
-  **Blue**: Board is advertising BLE advertisements but is not connected to another device.
-  **Green**: Another device (smartphone, PC) is connected over a BLE connection.
-  **White flashing**: The app encountered a critical error and is not able to continue operation; the code is busy-looping inside the error handler.

Advertisement
-------------

TotTag advertises according to the [Eddystone](https://github.com/google/eddystone)
protocol. It works with our [Summon](https://github.com/lab11/summon) project
that provides a browser-based UI for BLE devices.


Services
--------

TotTag provides a service for all ranging operations. Characteristics in that service
provide control and data for the interface with SquarePoint.

- **Ranging Service**: UUID: `d68c3152-a23f-ee90-0c45-5231395e5d2e`
  - **Localization Characteristic**: Short UUID: `3153`. Provides direct access to the data published from the
  SquarePoint when the SquarePoint interrupts the host. See `API.md` for a description of
  the possible data returned.
  - **Configuration Characteristic**: Short UUID: `3154`. Allows setting the role and the current epoch time.
  - **Enable Characteristic**: Short UUID: `3155`. Write a 0 to this to stop the ranging
  operation. Write a 1 to start ranging.
  - **Status Characteristic**: Short UUID: `3156`. Read-only; see whether module was initialized.
  - **Calibration Characteristic**: Short UUID: `3157`. Writing to this characteristic
  puts the node in calibration mode. The value written to this characteristic assigns
  the calibration index to the SquarePoint. Valid indices are 0,1,2. The node with index
  0 will immediately start the calibration procedure and should be assigned last.
  
  
