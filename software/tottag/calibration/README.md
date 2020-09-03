Calibration
===========

Calibration is required for each new board revision to account for the unique TX
and RX delays with respect to the DW1000 radios and their antennas. During normal
operation, a TotTag will automatically apply any stored calibration values.


General Process
---------------

The calibration process requires three nodes placed precisely 1 meter apart from
one another in the shape of a triangle. It is crucial that the nodes be placed
as exactly as possible, as any error in their placement will result in errors in
the SquarePoint ranging algorithm.

Both the SquarePoint and the TotTag firmware already include the code needed to
carry out calibration and do not require flashing a different application. To
perform calibration, ensure that all three devices are set up in their correct
triangle positions, and then individually put each device into calibration mode
by writing a different calibration index between 0-2 to the `0x3157` BLE
characteristic. All devices must have a different calibration index, and index 0
must be written last, as calibration will begin immediately once this index is
assigned.


Detailed Instructions
---------------------

1. To calibrate, you need **exactly** three devices. It is important that only
   three powered-on TotTags be in the vicinity.

2. Place the three TotTags in an equilateral triangle configuration, all exactly
   1 meter apart from one another. It is important to set this up accurately. Any
   error here will affect all future measurements.

3. Make sure you have followed all of the Python and Noble setup steps in the
   [Setup Guide](../../../doc/Setup.md).

4. Turn off BLE neighbor discovery on the devices, as ranging will begin
   automatically otherwise. The easiest way to do this is to re-flash the devices
   you are going to use for calibration. Only the nRF chip needs to be flashed:
   
    * `cd software/tottag/firmware`
    * `make flash BLE_ADDRESS=c0:98:e5:42:00:XX BLE_CALIBRATION=1`

5. Begin calibration and data collection using the provided NodeJS scripts. If
   the device IDs for the three TotTags are, for example, `c0:98:e5:42:00:20`,
   `c0:98:e5:42:00:21` and `c0:98:e5:42:00:25`, then the calibration
   procedure would be:
   
    * `node ./calibration_log.js -target_addr c0:98:e5:42:00:20 c0:98:e5:42:00:21 c0:98:e5:42:00:25`

6. After a few minutes, simply enter `Ctrl-C` to stop recording calibration
   data and move on to the next step.

7. Process the recorded data into a single file. Get the date string from the
   generated files from Step 6 and run:
   
    * `./calibration_condense.py YYYY-MM-DD_HH-MM-SS`

8. Compute the calibration values for each node involved and add the results to
   the main calibration file:
   
    * `./calibration_compute.py module_calibration_YYYY-MM-DD_HH-MM-SS.condensed`

TODO: Add these values to the Board revision header file at some point