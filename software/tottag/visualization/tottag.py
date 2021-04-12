#!/usr/bin/env python2

# Python imports
import os
import sys
import matplotlib.dates as md
import matplotlib.pyplot as plt
from datetime import datetime, tzinfo, timedelta


# User-defined constants
MAX_PLOT_FEET = 50
#PLOT_START_TIME = '19:55:00'
#PLOT_END_TIME = '08:25:00'

# Algorithm-defined constants
OUT_OF_RANGE_CODE = 999999
OUT_OF_RANGE_VALUE = None
OUT_OF_RANGE_MAX_NUM_INTERPOLATIONS = 10
RANGE_TYPE_IN = 0
RANGE_TYPE_OUT = 1
RANGE_TYPE_OFF = 2
RANGE_TYPE_IN_MESSAGE = 'IN RANGE'
RANGE_TYPE_OUT_MESSAGE = 'OUT OF RANGE'
RANGE_TYPE_OFF_MESSAGE = 'OUT OF RANGE (RECORDING DEVICE MAYBE POWERED OFF)'

# Global variables
power_ons = []
power_offs = []
range_data = {}
last_timestamp = 0
recording_device = 'UNKNOWN'

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
   print('USAGE: python tottag.py LOG_FILE_PATH')
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

      # Handle a valid measurement reading
      if line[0] != '#':

         # Parse the individual reading parts
         try:
            tokens = line.split('\t')
            timestamp = int(tokens[0])
            measurement = int(tokens[2].rstrip('\n'))
            range_feet = OUT_OF_RANGE_VALUE if (measurement == OUT_OF_RANGE_CODE) else ((measurement / 25.4) / 12.0)
            data = range_data.setdefault(tokens[1], {'timestamps': [], 'feet': [], 'last_datum': OUT_OF_RANGE_VALUE,
                                         'last_timestamp': 0, 'valid_count': 0})
         except:
            continue

         # Fill in any timestamp gaps with OUT_OF_RANGE values
         for i in range(timestamp - (timestamp if (data['last_timestamp'] == 0) else (data['last_timestamp'] + 1))):
            data['last_timestamp'] = data['last_timestamp'] + 1
            data['timestamps'].append(data['last_timestamp'])
            data['feet'].append(OUT_OF_RANGE_VALUE)

         # Add the current measurement reading
         if ((data['last_datum'] != OUT_OF_RANGE_VALUE) and (range_feet != OUT_OF_RANGE_VALUE) and
             (abs(range_feet - data['last_datum']) > 10)):
            range_feet = data['last_datum']
         data['timestamps'].append(timestamp)
         data['feet'].append(range_feet)
         data['last_timestamp'] = timestamp
         data['valid_count'] = data['valid_count'] + 1
         data['last_datum'] = range_feet
         last_timestamp = timestamp

      # Handle an informational logfile line containing a timestamp
      elif (line.find('HEADER') != -1) and (line.find('Timestamp: ') != -1):

         # Take a guess at the time the device was powered off
         if line.find('Device: ') != -1:
            recording_device = line[(line.find('Device: ')+8):line.find(', Date:')]
         timestamp = int(line[(line.find('Timestamp: ')+11):].rstrip('\n'))
         logfile_date = datetime.utcfromtimestamp(timestamp)
         power_offs.append(last_timestamp)
         power_ons.append(timestamp)
         last_timestamp = timestamp

         # Fill in all measurement readings with OUT_OF_RANGE values during the time the device was off
         for data in range_data.values():
            for i in range(timestamp - (timestamp if (data['last_timestamp'] == 0) else (data['last_timestamp'] + 1))):
               data['last_timestamp'] = data['last_timestamp'] + 1
               data['timestamps'].append(data['last_timestamp'])
               data['feet'].append(OUT_OF_RANGE_VALUE)

# Remove any extraneous device readings
bad_devices = { key: value for key, value in range_data.items() if (value['valid_count'] < 10) or (key[-3:] == ':00') }
for device in bad_devices:
   del range_data[device]

# Interpolate any small measurement gaps in the data
for data in range_data.values():
   bad_value_start = None
   for i, datum in enumerate(data['feet']):
      if datum == OUT_OF_RANGE_VALUE:
         if not bad_value_start:
            bad_value_start = i
      elif bad_value_start:
         if ((i - bad_value_start) <= OUT_OF_RANGE_MAX_NUM_INTERPOLATIONS) and data['feet'][bad_value_start-1]:
            slope = (datum - data['feet'][bad_value_start-1]) / (1 + i - bad_value_start)
            for j in range(bad_value_start, i):
               data['feet'][j] = data['feet'][j-1] + slope
         bad_value_start = None

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

# Output statistics about the contents of the log file
print('\nThis log file contains ranges to {} TotTag(s):\n'.format(len(range_data)))
for device in range_data:
   print('    {}'.format(device))
print('\n\nThe device was rebooted {} time(s) at the following timestamps:\n'.format(len(power_ons)))
for reboot in power_ons:
   print('    {}'.format(datetime.utcfromtimestamp(reboot).strftime('%b %d, %Y @ %H:%M:%S')))
print('\n\nTotTag ranging statistics are as follows:')
for device, data in range_data.items():
   print('\n    Device ID {}:\n'.format(device))
   range_start = data['timestamps'][0]
   range_type = RANGE_TYPE_IN if (data['feet'][0] != OUT_OF_RANGE_VALUE) else RANGE_TYPE_OUT
   for i in range(len(data['timestamps'])):
      if ((data['timestamps'][i] in power_offs) and (range_type != RANGE_TYPE_OFF) and
          (data['feet'][i] == OUT_OF_RANGE_VALUE)):
         print('        {} - {}: {}'.format(datetime.utcfromtimestamp(range_start).strftime('%H:%M:%S'),
                                            datetime.utcfromtimestamp(data['timestamps'][i]).strftime('%H:%M:%S'),
                                            RANGE_TYPE_IN_MESSAGE if range_type == RANGE_TYPE_IN else
                                            (RANGE_TYPE_OUT_MESSAGE if range_type == RANGE_TYPE_OUT else
                                             RANGE_TYPE_OFF_MESSAGE)))
         range_start = data['timestamps'][i]
         range_type = RANGE_TYPE_OFF
      elif (range_type == RANGE_TYPE_IN) and (data['feet'][i] == OUT_OF_RANGE_VALUE):
         print('        {} - {}: {}'.format(datetime.utcfromtimestamp(range_start).strftime('%H:%M:%S'),
                                            datetime.utcfromtimestamp(data['timestamps'][i]).strftime('%H:%M:%S'),
                                            RANGE_TYPE_IN_MESSAGE if range_type == RANGE_TYPE_IN else
                                            (RANGE_TYPE_OUT_MESSAGE if range_type == RANGE_TYPE_OUT else
                                             RANGE_TYPE_OFF_MESSAGE)))
         range_start = data['timestamps'][i]
         range_type = RANGE_TYPE_OUT
      elif (range_type != RANGE_TYPE_IN) and (data['feet'][i] != OUT_OF_RANGE_VALUE):
         print('        {} - {}: {}'.format(datetime.utcfromtimestamp(range_start).strftime('%H:%M:%S'),
                                            datetime.utcfromtimestamp(data['timestamps'][i]).strftime('%H:%M:%S'),
                                            RANGE_TYPE_IN_MESSAGE if range_type == RANGE_TYPE_IN else
                                            (RANGE_TYPE_OUT_MESSAGE if range_type == RANGE_TYPE_OUT else
                                             RANGE_TYPE_OFF_MESSAGE)))
         range_start = data['timestamps'][i]
         range_type = RANGE_TYPE_IN
   print('        {} - {}: {}'.format(datetime.utcfromtimestamp(range_start).strftime('%H:%M:%S'),
                                      datetime.utcfromtimestamp(data['timestamps'][-1]).strftime('%H:%M:%S'),
                                      RANGE_TYPE_IN_MESSAGE if range_type == RANGE_TYPE_IN else
                                      (RANGE_TYPE_OUT_MESSAGE if range_type == RANGE_TYPE_OUT else
                                       RANGE_TYPE_OFF_MESSAGE)))

# Save time series plots of the ranging data to each device
print('\n\nTime series plots can be found at the following locations:\n')
for device, data in range_data.items():
   try: start_index = data['timestamps'].index(plot_begin)
   except ValueError: start_index = 0
   try: stop_index = data['timestamps'].index(plot_end)
   except ValueError: stop_index = -1
   plt.clf()
   plt.ylim(0, MAX_PLOT_FEET)
   plt.plot([datetime.utcfromtimestamp(x) for x in data['timestamps'][start_index:stop_index]],
            data['feet'][start_index:stop_index])
   plt.title('Distance from "{}"\nto "{}"'.format(recording_device, device))
   plt.xlabel('Timestamps', labelpad=10)
   plt.ylabel('Measurement (ft)', labelpad=10)
   plt.gcf().autofmt_xdate()
   plt.xticks(rotation=45)
   plt.gca().xaxis.set_major_formatter(md.DateFormatter('%H:%M:%S'))
   plt.grid(False)
   filename = os.path.join(logfile_dir, 'figs', 'fig_{}.png'.format(device.replace(':', '')))
   plt.subplots_adjust(bottom=0.2, left=0.15)
   plt.savefig(filename, dpi=400)
   plt.show()
   print('    {}: {}'.format(device, filename))
