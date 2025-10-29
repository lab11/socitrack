#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import sys
from tottag import *


def process_tottag_data(data, experiment_start_time):
   """
   Parses and processes raw binary data read from .ttg files.
   The processed sensor log is saved to a file named 'converted.pkl'.
   IMU data is currently skipped.

   Parameters:
        data (bytes): Raw binary byte stream read from .ttg files.
        experiment_start_time (float): 10-digit Unix timestamp (in seconds).

   Returns:
        List[dict]: A list of timestamped dict of data. Each dictionary has:
            - 't': Absolute timestamp (float)
            - 'v' (Voltage):
                Indicates the battery voltage of the device in millivolts.
            - 'c' (Charging Event):
                Encodes battery charging status as one of several predefined states, such as
                charging, full, or discharging. The values are mapped via `BATTERY_CODES`.
            - 'm' (Motion):
                A boolean value indicating whether motion was detected at the time.
                `True` means motion is detected; `False` indicates stillness.
            - 'r' (Ranges):
                A dictionary mapping device UIDs (unique IDs) to measured distances (in millimeters).
                These distances represent proximity to neighboring devices. Only values.
                below `MAX_RANGING_DISTANCE_MM` are included.
            - 'b' (BLE Scan Results):
                A list of device UIDs detected during a Bluetooth Low Energy scan. These IDs represent.
                nearby TotTags.
   """
   i = 0
   log_data = defaultdict(dict)
   try:
      while i + 5 < len(data):
         timestamp_raw = struct.unpack('<I', data[i+1:i+5])[0]
         timestamp = experiment_start_time + (timestamp_raw / 1000)
         if timestamp > int(time.time()) or ((timestamp_raw % 500) != 0) or data[i] < 1 or data[i] >= STORAGE_NUM_TYPES:
            i += 1
         elif data[i] == STORAGE_TYPE_VOLTAGE:
            datum = struct.unpack('<I', data[i+5:i+9])[0]
            if datum > 0 and datum < 4500:
               log_data[timestamp]['v'] = datum
               i += 9
            else:
               i += 1
         elif data[i] == STORAGE_TYPE_CHARGING_EVENT:
            if data[i+5] > 0 and data[i+5] < 5:
               log_data[timestamp]['c'] = BATTERY_CODES[data[i+5]]
               i += 6
            else:
               i += 1
         elif data[i] == STORAGE_TYPE_MOTION:
            if data[i+5] == 0 or data[i+5] == 1:
               log_data[timestamp]['m'] = data[i+5] > 0
               i += 6
            else:
               i += 1
         elif data[i] == STORAGE_TYPE_RANGES:
            log_data[timestamp]['r'] = {}
            if data[i+5] < MAX_NUM_DEVICES:
               for j in range(data[i+5]):
                  uid = data[i+6+(j*3)]
                  datum = struct.unpack('<H', data[i+7+(j*3):i+9+(j*3)])[0]
                  if datum < MAX_RANGING_DISTANCE_MM:
                     log_data[timestamp]['r'][uid] = datum
               i += 6 + data[i+5]*3
            else:
               i += 1
         elif data[i] == STORAGE_TYPE_IMU:
            imu_length = data[i+5]
            if imu_length <= MAX_IMU_DATA_LENGTH:
               log_data[timestamp]['i'] = [
                  struct.unpack('<H', data[i+6:i+8])[0],
                  struct.unpack('<H', data[i+8:i+10])[0],
                  struct.unpack('<H', data[i+10:i+12])[0]
               ]
               i += 5 + imu_length
            else:
               i += 1
         elif data[i] == STORAGE_TYPE_BLE_SCAN:
            if data[i+5] < MAX_NUM_DEVICES:
               log_data[timestamp]['b'] = []
               for j in range(data[i+5]):
                  uid = data[i+6+j]
                  log_data[timestamp]['b'].append(uid)
               i += 6 + data[i+5]
            else:
               i += 1
         else:
            i += 1
   except Exception:
      pass
   log_data = [dict({'t': ts}, **datum) for ts, datum in log_data.items()]
   with open(os.path.join(get_download_directory(), 'converted.pkl'), 'wb') as file:
      pickle.dump(log_data, file, protocol=pickle.HIGHEST_PROTOCOL)
   return log_data

if __name__ == "__main__":

   if len(sys.argv) != 3:
      print('Usage: ./parse.py [LOGFILE] [EXPERIMENT_START_TIME]')
      sys.exit(0)

   experiment_start_time = int(sys.argv[2])
   with open(sys.argv[1], mode="rb") as file:
      process_tottag_data(file.read(), experiment_start_time)
