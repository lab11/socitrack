Calibration
===========

We need to calibrate each module to account for unique TX and RX
delays with DW1000 radios.


Process
-------

Calibration requires three nodes. The nodes need to placed at
well-known distances from each other; the calibration scripts currently
assume that nodes are placed in a triangle each 1m apart.

The software includes the code needed to run calibration and does not require a different application. To do
calibration with the hardware, flash the `node` application onto the carrier board. Then, putting
the node in calibration mode requires writing the calibration node index
to the `0x3157` characteristic. There are three "roles" (0,1,2) that nodes
can be assigned during calibration. Calibration roles are set by writing
one of 0, 1, or 2 to the `0x3157` characteristic. Role 0 must be written
last, as calibration will start once this role is assigned.

In practice, do this:

1. Install [node](https://nodejs.org/en/download/) first. Version 8.14+ should work.
Then install the dependencies:

        # In the calibration folder
        npm install

2. Make noble run without sudo:

        sudo setcap cap_net_raw+eip $(eval readlink -f `which node`)

3. Turn off neighbour discovery, as nodes will automatically start ranging otherwise. You can do this using compile-time arguments while flashing onto the nRF (the STM does not require any adaptations):

        mark flash BLE_ADDRESS=c0:98:e5:42:00:XX APP_BLE_CALIBRATION=1

4. Collect the data from each node with EUIs as specified (examples are `c0:98:e5:42:00:07`, `c0:98:e5:42:00:08` and `c0:98:e5:42:00:09`):

        node ./calibration_log.js -target_addr 07 08 09

    Make sure there are only 3 nodes on during calibration.

5. Process the data into a single file. Get the date string from the produced files from step 2.

        ./calibration_condense.py YYYY-MM-DD_HH-MM-SS

6. Compute the calibration values for each node involved and add the
results to the main calibration file.

        ./calibration_compute.py module_calibration_YYYY-MM-DD_HH-MM-SS.condensed

7. When flashing the firmware, the build system will check
`module_calibration.data` for calibration constants and use those
if they exist.

