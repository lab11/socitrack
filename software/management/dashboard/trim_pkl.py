#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import datetime, pytz, tzlocal
import os, pickle, sys

def trim_data(filename, start_timestamp, end_timestamp):
   with open(filename, 'rb') as file:
      data = pickle.load(file)
      return [item for item in data if item['t'] >= start_timestamp and item['t'] <= end_timestamp]


if __name__ == "__main__":

   if len(sys.argv) != 4:
      print('Usage: ./trim_pkl.py [LOGFILE] [EXPERIMENT_START_TIME MM/DD/YYYY] [EXPERIMENT_END_TIME MM/DD/YYYY]')
      sys.exit(0)

   experiment_start_time = pytz.timezone(str(tzlocal.get_localzone())).localize(datetime.datetime.strptime(sys.argv[2], '%m/%d/%Y')).astimezone(pytz.utc).timestamp()
   experiment_end_time = pytz.timezone(str(tzlocal.get_localzone())).localize(datetime.datetime.strptime(sys.argv[3], '%m/%d/%Y')).astimezone(pytz.utc).timestamp()
   trimmed = trim_data(sys.argv[1], experiment_start_time, experiment_end_time)
   with open(os.path.join(os.path.dirname(sys.argv[1]), 'trimmed_' + os.path.basename(sys.argv[1])), 'wb') as file:
      pickle.dump(trimmed, file, protocol=pickle.HIGHEST_PROTOCOL)
   print(trimmed)
