#!/usr/bin/env python2

# Python imports
import os
import sys
import matplotlib.dates as md
import matplotlib.pyplot as plt
from datetime import datetime, tzinfo, timedelta


# Global variables
recording_device = 'UNKNOWN'
timestamps = []
voltages = []

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
   print('USAGE: python tottagVoltage.py LOG_FILE_PATH')
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

      # Store a valid voltage reading or device ID
      if (line[0] == '#') and (line.find('VOLTAGE: ') != -1) and (line.find('Timestamp: ') != -1):
         voltages.append(int(line[(line.find('VOLTAGE: ')+9):line.find(' mV')]))
         timestamps.append(int(line[(line.find('Timestamp: ')+11):].rstrip('\n')))
      elif (line.find('HEADER') != -1) and (line.find('Device: ') != -1) and (line.find('Timestamp: ') != -1):
         recording_device = line[(line.find('Device: ')+8):line.find(', Date:')]

# Save time series plot of the battery voltage over time
print('\n\nTime series plots can be found at the following location:\n')
plt.clf()
plt.ylim(3200, 4300)
plt.plot([datetime.utcfromtimestamp(x) for x in timestamps], voltages)
plt.title('Battery Voltage Over Time for\nDevice "{}"'.format(recording_device))
plt.xlabel('Timestamps', labelpad=10)
plt.ylabel('Voltage (mV)', labelpad=10)
plt.gcf().autofmt_xdate()
plt.xticks(rotation=45)
plt.gca().xaxis.set_major_formatter(md.DateFormatter('%H:%M:%S'))
plt.grid(False)
filename = os.path.join(logfile_dir, 'figs', 'fig_{}.png'.format(recording_device.replace(':', '')))
print('    {}: {}'.format(recording_device, filename))
plt.subplots_adjust(bottom=0.2, left=0.15)
plt.savefig(filename, dpi=400)
plt.show()
