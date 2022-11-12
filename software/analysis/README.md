TotTag Analysis Scripts
=======================

This directory contains a collection of Python scripts for use in managing and
analyzing stored TotTag measurement data. To ensure that all scripts run
without problem, you may pre-install all necessary packages at one time by
entering the following terminal command (depending on whether you are
using Python version 2 or 3):

For Python 2: `pip install -r requirements.txt`

For Python 3: `pip3 install -r requirements.txt`

Quick Visualization
----------
For quick check of data quality, if the `quickplot_folder.py` is already in the folder with logs from a single day, use 

    python3 quickplot_folder.py

Otherwise, use

    python3 quickplot_folder.py path_to_the_single_day_folder

Management
----------

The script entitled `tottagLogManagement.py` can be used to fully manage
the log files stored on any TotTag's SD Card. It allows you to list the files
present on a device, download them individually or as a whole, and erase them.

To run the script, first ensure that your TotTag is connected to your computer
via USB, then enter the following in a terminal (again the `python` command
may need to be replaced with `python3`):

    python tottagLogManagement.py

This will bring up a command menu that looks like the following:

    [0]: List Log Files
    [1]: Download All Log Files
    [2]: Download Specific Log File
    [3]: Erase All Log Files
    [4]: Erase Specific Log File
    [5]: Exit TotTag SD Card Management Utility

The command functions should be self-explanatory, but please note that erasing
a log file is an irreversible operation, so make sure that you have downloaded
all logs files and stored them in a safe place before running either of the
erase commands!

When you are done using the utility, you can exit by simply entering command
number `5`.


Monitoring
----------

There are two scripts available for real-time monitoring of a deployed network
of TotTags in the vicinity of a desktop computer. The 'tottagCurrentTimestamps.py'
script may be used to retrieve the current Unix timestamp from the point of
view of the real-time clock programmed on each visible TotTag within range of
the Bluetooth radio.

The 'tottagRealtimeRanging.py' script may be used to output a list of current
ranges between TotTag devices as calculated from a specific device's point
of view. When running this script, you will be presented with a list of
available TotTag devices in your immediate area. You may select one of these
units to subscribe to its real-time ranging data which will be updated once
per second.


Analysis
--------

The remaining scripts in this directory can be used to analyze the downloaded
TotTag log files. To begin, first run the `tottagAverager.py` script,
which takes at least 3 arguments: the starting Unix timestamp, the ending Unix
timestamp, and a list of every log file that you would like to average together.
The data is averaged accross timestamps by dyad, so the measured value at a
certain timestamp from one TotTag is averaged with the value at the same
timestamp from its companion TotTag. The script can be run like so:

    python tottagAverager.py START_TIME_VAL END_TIME_VAL LOG_FILE_1 LOG_FILE_2...

Next, run the `tottagSmoother.py` script, which takes at least 2 arguments:
the number of data points over which to smooth, followed by a list of every log
file you would like to smooth. The smoother works by taking a moving average with
a width of SMOOTHING_VAL. When it encounters a gap in the data greater than the
aforementioned value, the smoothing buffer is cleared and it starts over after
the gap. To run the script, enter:

    python tottageSmoother.py SMOOTHING_VAL AVERAGED_LOG_FILE_1 AVERAGED_LOG_FILE_2...

Now, your data is ready to go, and you can begin running the `tottagStats.py`
script on it. This script produces summary statistics on the smoothed log file.
The statistics it outputs are the amount of time each dyad spent within 3ft of
one another, the amount of time the TotTags were in range of one another, and
the number of times a dyad re-enters 3ft after leaving it for at least 30
seconds. The input for this script is simply a single log file:

    python tottagStats.py SMOOTHED_LOG_FILE_1
