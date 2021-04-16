#!/usr/bin/env python

# Python imports
import os
import sys
import matplotlib.dates as md
import matplotlib.pyplot as plt
from datetime import datetime, tzinfo, timedelta


# User-defined constants
#PLOT_START_TIME = '11:15:00'
#PLOT_END_TIME = '11:30:00'

# Global variables
recording_device = 'UNKNOWN'
last_timestamp = 0
last_motion = 0
timestamps = []
motions = []

# Timezone class for Python2
class UTC(tzinfo):
   def utcoffset(self, dt):
      return timedelta(hours=0)
   def dst(self, dt):
      return timedelta(0)
   def tzname(self, dt):
      return "UTC"


# Parse command line parameters
if len(sys.argv) != 2:
   print('USAGE: python tottagMotion.py LOG_FILE_PATH')
   sys.exit(1)
logfile = sys.argv[1]
logfile_dir = os.path.dirname(os.path.abspath(logfile))
logfile_date = datetime.today()

# Create an output directory
results_dir = os.path.join(logfile_dir, 'figs')
if not os.path.isdir(results_dir):
   os.makedirs(results_dir)

# Parse all data in the log file
with open(logfile) as f:
   for line in f:

      # Search for valid motion readings or device IDs
      if (line[0] == '#') and (line.find('MOTION CHANGE: ') != -1) and (line.find('Timestamp: ') != -1):

         # Parse a valid motion reading
         timestamp = int(line[(line.find('Timestamp: ')+11):].rstrip('\n'))
         motion = 1 if (line[(line.find('MOTION CHANGE: ')+15):line.find('; ')] == 'IN MOTION') else 0

         # Fill in any timestamp gaps with the previous motion value
         for i in range(timestamp - (timestamp if (last_timestamp == 0) else (last_timestamp + 1))):
            last_timestamp = last_timestamp + 1
            timestamps.append(last_timestamp)
            motions.append(last_motion)

         # Store the current motion reading
         timestamps.append(timestamp)
         motions.append(motion)
         last_timestamp = timestamp
         last_motion = motion

      elif (line.find('HEADER') != -1) and (line.find('Device: ') != -1) and (line.find('Timestamp: ') != -1):
         recording_device = line[(line.find('Device: ')+8):line.find(', Date:')]
         timestamp = int(line[(line.find('Timestamp: ')+11):].rstrip('\n'))
         logfile_date = datetime.utcfromtimestamp(timestamp)

# Determine plotting start and end times
try: PLOT_START_TIME
except NameError: PLOT_START_TIME = None
try: PLOT_END_TIME
except NameError: PLOT_END_TIME = None
start_time_tokens = PLOT_START_TIME.split(':') if PLOT_START_TIME else ['0', '0', '0']
end_time_tokens = PLOT_END_TIME.split(':') if PLOT_END_TIME else ['23', '59', '59']
plot_begin = (datetime(logfile_date.year, logfile_date.month, logfile_date.day, int(start_time_tokens[0]),
                       int(start_time_tokens[1]), int(start_time_tokens[2]), tzinfo=UTC()) -
              datetime(1970, 1, 1, tzinfo=UTC())).total_seconds()
plot_end = (datetime(logfile_date.year, logfile_date.month, logfile_date.day, int(end_time_tokens[0]),
                     int(end_time_tokens[1]), int(end_time_tokens[2]), tzinfo=UTC()) -
            datetime(1970, 1, 1, tzinfo=UTC())).total_seconds()

# Save time series plot of the device's motion over time
print('\n\nTime series plots can be found at the following location:\n')
try: start_index = timestamps.index(plot_begin)
except ValueError: start_index = 0
try: stop_index = timestamps.index(plot_end)
except ValueError: stop_index = -1
plt.clf()
plt.ylim(0, 1.2)
plt.plot([datetime.utcfromtimestamp(x) for x in timestamps[start_index:stop_index]], motions[start_index:stop_index])
plt.title('Motion Characteristics Over Time for\nDevice "{}"'.format(recording_device))
plt.xlabel('Timestamps', labelpad=10)
plt.ylabel('Device Motion\n(0 = Stationary, 1 = Moving)', labelpad=10)
plt.gcf().autofmt_xdate()
plt.xticks(rotation=45)
plt.gca().xaxis.set_major_formatter(md.DateFormatter('%H:%M:%S'))
plt.grid(False)
filename = os.path.join(logfile_dir, 'figs', 'fig_{}.png'.format(recording_device.replace(':', '')))
print('    {}: {}'.format(recording_device, filename))
plt.subplots_adjust(bottom=0.2, left=0.15)
plt.savefig(filename, dpi=400)
plt.show()
