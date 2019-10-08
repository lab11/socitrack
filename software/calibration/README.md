Calibration
===========

We need to calibrate each module to account for unique TX and RX delays with
DW1000 radios. During normal operation, a tag applies calibration correction
automatically.

<!-- npm i -g markdown-toc; markdown-toc -i README.md -->

<!-- toc -->

- [Process](#process)
- [Detailed Instructions](#detailed-instructions)

<!-- tocstop -->

Process
-------

Calibration requires three nodes. The nodes need to placed at
well-known distances from each other; the calibration scripts currently
assume that nodes are placed in a triangle each 1m apart.

The software includes the code needed to run calibration and does not require a
different application. To do calibration with the hardware, flash the `node`
application onto the carrier board. Then, putting the node in calibration mode
requires writing the calibration node index to the `0x3157` characteristic.
There are three "roles" (0,1,2) that nodes can be assigned during calibration.
Calibration roles are set by writing one of 0, 1, or 2 to the `0x3157`
characteristic. Role 0 must be written last, as calibration will start once
this role is assigned.


Detailed Instructions
---------------------

1. To calibrate, you need **exactly** three devices. It's important that only
   three powered-on TotTags be nearby.

1. Place the three tags in an equilateral triangle configuration, all exactly 1
   meter apart from one another. It's important to set this up accurately. Any
   error here will affect all future measurements with these tags.

1. Make sure you have followed all of the Python and Noble steps in the
   [Setup guide](../../doc/Setup.md#getting-python).

1. Turn off neighbour discovery, as nodes will automatically start ranging
   otherwise.

   The easiest way to do this is to re-flash the nodes you are going to
   calibrate. You only need to re-flash the nRF:

        cd lab11/totternary/software/carrier/apps/node
        make flash BLE_ADDRESS=c0:98:e5:42:00:XX APP_BLE_CALIBRATION=1

1. Collect the data from each node with EUIs as specified (examples are
   `c0:98:e5:42:00:20`, `c0:98:e5:42:00:21` and `c0:98:e5:42:00:25`):

        node ./calibration_log.js -target_addr 20 21 25

    Make sure there are only 3 nodes on during calibration.

    When calibration is working, it should print out something like this:


    ```bash
    ppannuto@ubuntu:/mnt/hgfs/totternary/software/calibration$ node ./calibration_log.js -target_addr 20 21 25
    Looking for peripherals with addresses c098e5420020, c098e5420021 and c098e5420025
    Scanning...
    Found TotTag: c098e5420020
    Found TotTag: c098e5420021
    Found TotTag: c098e5420025
    Starting to connect...
    Connected to TotTag c098e5420021
    Successfully connected to c098e5420021
    Connected to TotTag c098e5420025
    Successfully connected to c098e5420025
    Successfully set c098e5420021 to index 1
    Connected to TotTag c098e5420020
    Successfully connected to c098e5420020
    Successfully set c098e5420025 to index 2
    Successfully set c098e5420020 to index 0
    #
    # Everything above here may take a little bit (up to maybe 15 seconds or
    # so) as tags are discovered and connected to. Once all the tags are
    # connected, calibration should run very quickly.
    #
    Received notification about data of length 128 from c098e5420021
    Round 0 on c098e5420021
    Received notification about data of length 128 from c098e5420025
    Round 0 on c098e5420025
    Received notification about data of length 128 from c098e5420025
    Round 1 on c098e5420025
    Received notification about data of length 128 from c098e5420020
    Round 1 on c098e5420020
    Received notification about data of length 128 from c098e5420021
    Round 2 on c098e5420021
    Received notification about data of length 128 from c098e5420020
    Round 2 on c098e5420020
    #
    # This will go as long as you let it. What's happening is we are taking
    # many samples of the tags at a known distance and averaging the result.
    # The more samples, the better calibration will be. A couple hundred is
    # plenty. At any point, simply hit Ctrl-C to stop recording calibration
    # data and move on to the next step.
    ```

1. Process the data into a single file. Get the date string from the produced
   files from step 2.

        ./calibration_condense.py YYYY-MM-DD_HH-MM-SS

1. Compute the calibration values for each node involved and add the results to
   the main calibration file.

        ./calibration_compute.py module_calibration_YYYY-MM-DD_HH-MM-SS.condensed

1. (Optional): Want to see what changed? Run `git diff module_calibration.data`:

    ```diff
    --- a/software/calibration/module_calibration.data
    +++ b/software/calibration/module_calibration.data
    @@ -16,4 +16,7 @@ c0:98:e5:42:00:0d  32918    32807   32908      32918   32827      32921     3289
     c0:98:e5:42:00:0e  32913    32783   32919      32941   32804      32902     32914   32804   32900
     c0:98:e5:42:00:0f  32983    32839   32940      32969   32862      32903     32945   32813   32915
     c0:98:e5:42:00:10  32917    32808   32899      32931   32832      32929     32919   32803   32905
    +c0:98:e5:42:00:20  33084    32814   32966      32915   32838      32888     32933   32830   32919
    +c0:98:e5:42:00:21  32993    32827   32932      32961   32883      33022     32963   32829   32925
    +c0:98:e5:42:00:25  32852    32811   32895      32950   32792      32894     32860   32778   32880
     c0:98:e5:42:00:ff  32923    32808   32912      32931   32836      32917     32914   32809   32906
    ```

    The added lines are the newly calibrated nodes. Here we see just after
    calibrating nodes 20, 21, and 25.


The next time the STM firmware is flashed, the build system will read
`module_calibration.data` for calibration constants and use those if they
exist.

[Return to Provisioning document](../../doc/Provisioning.md#Calibration)

