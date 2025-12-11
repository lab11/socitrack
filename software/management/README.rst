TotTag Management Dashboard
===========================

Overview
--------

This directory contains all source code for the TotTag Management Dashboard. All setup and preparation for live pilot testing should be carried out through the TotTag Dashboard.


Installation
------------

The easiest way to install this tool is through `pip` by entering:

``python3 -m pip install tottag``

If you wish to install the package manually or develop it further, you should first clone the `SociTrack repository <https://github.com/lab11/socitrack>`_ to
your hard drive, `cd` into the ``software/management`` directory, then issue the following command in a terminal:

``python3 -m pip install -e .``


Usage
-----

Once installed, the management dashboard is accessible from any terminal by entering the following command:

``tottag``

You do not need to navigate to the SociTrack source code directory to run the application.

Usage of the TotTag Dashboard should be relatively straightforward, keeping in mind a few caveats:

•	A TotTag device is only discoverable and visible if it is currently being charged. If the device is not on a charger, it will not appear in the TotTag Dashboard.
•	You will not be able to carry out any management functionality unless you are actively connected to a TotTag, so if the action buttons are grayed out, ensure that you are actually connected to a device.
•	If, at any time, the application freezes or becomes unresponsive, you can always force it to close by returning to the terminal in which you entered the “tottag” command and simultaneously pressing CTRL+C. This should cause the application to forcefully terminate.

The suggested procedure for setting up a pilot deployment is the following:

1.	Determine exactly how many TotTags will be needed for the deployment, and choose this number of devices to set aside.
2.	Make a note on a piece of scratch paper about which Device IDs should be associated with which user-friendly labels.
3.	Open the TotTag Dashboard and go to the “Deployment” tab. Do not connect to any TotTags at this time.
4.	Enter all relevant information about the upcoming deployment, including the mapping of TotTag labels to Device IDs.
5.	Once all information has been entered, ensure that all relevant TotTags are discoverable in the device bar at the top of the dashboard.
6.	Click the “Start” button to start the deployment programming process for all relevant devices.
7.	Once complete, it is a good idea to individually connect to each of the participating devices and verify that both the current time and the experiment details are correct.

After the above steps have been completed, your devices are ready for shipment and will automatically shut down until time for the deployment to begin. Upon completion of a pilot test, the first thing you should do upon receipt of the TotTags is to download all log files before putting the TotTags back into storage.
