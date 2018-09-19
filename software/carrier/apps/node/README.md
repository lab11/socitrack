TotTag BLE App
==============

This apps provides the BLE interface for the TotTag localization tag.

Programming
-----------

    SEGGER_SERIAL=xxxxxxxxx make flash BLE_ADDRESS=c0:98:e5:42:00:01

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
  
  
