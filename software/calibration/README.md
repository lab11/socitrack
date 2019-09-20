Calibration
===========

We need to calibrate each module to account for unique TX and RX delays with
DW1000 radios. During normal operation, a tag applies calibration correction
automatically, but only if it knows the calibration information for the tags
it is ranging with.

This means it is important to first calibrate all tags, and then program all
tags for deployment, so that all tags have all of the calibration data.


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


Detailed Instructions
---------------------

1. To calibrate, you need **exactly** three devices. It's important that only
   three powered-on TotTags be nearby.

1. Place the three tags in an equilateral triangle configuration, all exactly 1
   meter apart from one another. It's important to set this up accurately. Any
   error here will affect all future measurements with these tags.

1. Install [node](https://nodejs.org/en/download/) if you have not already.
   Then install the dependencies:

        # In the calibration folder
        npm install

        # Problems? See below

1. Make noble run without sudo:

        # Note: You only need to do this on Linux machines, not Macs
        # Note: You only need to do this once per machine
        sudo setcap cap_net_raw+eip $(eval readlink -f `which node`)

1. Turn off neighbour discovery, as nodes will automatically start ranging
   otherwise. You can do this using compile-time arguments while flashing onto
   the nRF (the STM does not require any adaptations):

        mark flash BLE_ADDRESS=c0:98:e5:42:00:XX APP_BLE_CALIBRATION=1

1. Collect the data from each node with EUIs as specified (examples are
   `c0:98:e5:42:00:07`, `c0:98:e5:42:00:08` and `c0:98:e5:42:00:09`):

        node ./calibration_log.js -target_addr 07 08 09

    Make sure there are only 3 nodes on during calibration.

1. Process the data into a single file. Get the date string from the produced
   files from step 2.

        ./calibration_condense.py YYYY-MM-DD_HH-MM-SS

1. Compute the calibration values for each node involved and add the results to
   the main calibration file.

        ./calibration_compute.py module_calibration_YYYY-MM-DD_HH-MM-SS.condensed


You are now finished collecting calibration data. The next time the STM
firmware is flashed, the build system will read `module_calibration.data` for
calibration constants and use those if they exist.

[Return to Provisioning document](../../doc/Provisioning#Calibration)


### Partial notes for debugging node issues

Basically I think the solution is to jump back in time a bit, as noble's not
been updated lately, and that causes trouble. What made things work in one
case for me:

 - Install [`nvm`](https://github.com/nvm-sh/nvm#installation-and-update)
 - `nvm use v5` (it chose v5.4.1 for me)
 - `env CXXFLAGS="-mmacosx-version-min=10.9" LDFLAGS='-mmacosx-version-min=10.9" npm install`

