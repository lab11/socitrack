#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import sys
from tottag import *
import tottag_format


def process_tottag_data(data, experiment_start_time=None):
   """
   Parses and processes raw binary data read from .ttg files, in either log format.
   The processed sensor log is saved to a file named 'converted.pkl'.

   Parameters:
        data (bytes): Raw binary byte stream read from .ttg files.
        experiment_start_time (float): 10-digit Unix timestamp (in seconds). Required for legacy (v1)
            files, which carry no metadata; optional for page-framed (v2) files, which embed the
            experiment details and can supply it themselves.

   Returns:
        List[dict]: A list of timestamped dicts of data. Each dictionary has:
            - 't': Absolute timestamp (float)
            - 'v' (Voltage): battery voltage of the device in millivolts.
            - 'c' (Charging Event): battery charging status, mapped via `BATTERY_CODES`.
            - 'm' (Motion): True if motion was detected at the time.
            - 'r' (Ranges): dict mapping device UIDs to measured distances in millimetres, for values
                below `MAX_RANGING_DISTANCE_MM`.
            - 'b' (BLE Scan Results): list of nearby TotTag UIDs detected during a BLE scan.
            - 'i' (IMU): three-axis accelerometer sample.

   UIDs are returned as raw integers here rather than mapped to labels, which is the only difference
   from tottag.process_tottag_data().
   """
   log_data, report = tottag_format.parse(data, experiment_start_time, None)

   # A v1 file cannot report loss at all; for v2 say plainly what is missing rather than silently
   # returning a short log that looks complete
   if report['holes'] or report['crc_failures'] or report['truncated']:
      print('WARNING: this log is incomplete:')
      if report['holes']:
         print(f"   {len(report['holes'])} page(s) unreadable on the device (position, seq): {report['holes']}")
      if report['crc_failures']:
         print(f"   {len(report['crc_failures'])} page(s) failed CRC (position, seq): {report['crc_failures']}")
      if report['short_pages']:
         print(f"   {len(report['short_pages'])} page(s) stopped decoding early (position, seq, decoded, expected): {report['short_pages'][:5]}")
      if report['truncated']:
         print(f"   file ends early: {report['pages_read']} of {report['total_pages']} pages present")

   with open(os.path.join(get_download_directory(), 'converted.pkl'), 'wb') as file:
      pickle.dump(log_data, file, protocol=pickle.HIGHEST_PROTOCOL)
   return log_data


if __name__ == "__main__":

   # The start time is only needed for legacy files; page-framed files embed their own experiment details
   if len(sys.argv) not in (2, 3):
      print('Usage: ./parse.py [LOGFILE] [EXPERIMENT_START_TIME]')
      print('       EXPERIMENT_START_TIME is required for legacy .ttg files and ignored for newer ones')
      sys.exit(0)

   with open(sys.argv[1], mode="rb") as file:
      data = file.read()
   if len(sys.argv) == 3:
      process_tottag_data(data, int(sys.argv[2]))
   elif tottag_format.detect_format(data) == tottag_format.FORMAT_V2:
      process_tottag_data(data)
   else:
      print('ERROR: this is a legacy .ttg file, so EXPERIMENT_START_TIME must be supplied')
      sys.exit(1)
