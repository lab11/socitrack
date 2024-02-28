#!/usr/bin/env python3
# -*- coding: utf-8 -*-

# PYTHON INCLUSIONS ---------------------------------------------------------------------------------------------------

from datetime import datetime
import matplotlib.pyplot as plt
import matplotlib.dates as mdates
import pandas, pickle


# HELPER FUNCTIONS ----------------------------------------------------------------------------------------------------

def load_data(filename):
    with open(filename, 'rb') as file:
        data = pandas.json_normalize(data=pickle.load(file))
    return data.set_index('t').reindex(pandas.Series(range(data.head(1)['t'].iloc[0], 1+data.tail(1)['t'].iloc[0]))).T \
           if data is not None and len(data.index) > 0 else None

def plot_data(title, x_axis_label, y_axis_label, x_axis_data, y_axis_data):
    plt.close()
    plt.title(title)
    axis = plt.gca()
    axis.xaxis.set_major_formatter(mdates.DateFormatter('%Y-%m-%d %H:%M:%S'))
    axis.xaxis.set_label_text(x_axis_label)
    axis.yaxis.set_label_text(y_axis_label)
    axis.figure.autofmt_xdate(rotation=45, bottom=0.3)
    plt.plot(x_axis_data, y_axis_data)
    plt.show()


# DATA PROCESSING FUNCTIONALITY ---------------------------------------------------------------------------------------

def get_voltage_time_series(data, tottag_label):
    voltages = data.loc['v'].dropna()
    timestamps = mdates.date2num([datetime.fromtimestamp(ts) for ts in voltages.keys()])
    plot_data(f"Battery Voltage for {tottag_label}", "Date and Time", "Voltage (mV)", timestamps, voltages)

def get_motion_time_series(data, tottag_label):
    motions = data.loc['m'].ffill()
    timestamps = mdates.date2num([datetime.fromtimestamp(ts) for ts in motions.keys()])
    plot_data(f"Motion Status for {tottag_label}", "Date and Time", "Motion Status", timestamps, motions)

def extract_ranging_time_series(data, destination_tottag_label, start_timestamp=None, end_timestamp=None, cutoff_distance=30, unit="ft"):
    if unit == "ft":
        conversion_factor_from_mm = 304.8
    elif unit == "m":
        conversion_factor_from_mm = 1000.0
    ranges = data.loc['r.' + destination_tottag_label] / conversion_factor_from_mm
    ranges = ranges.mask(ranges > cutoff_distance)\
                   .reindex(pandas.Series(range(start_timestamp if start_timestamp is not None else data.T.head(1).index[0],
                                                end_timestamp if end_timestamp is not None else 1+data.T.tail(1).index[0])))
    timestamps = mdates.date2num([datetime.fromtimestamp(ts) for ts in ranges.keys()])
    return timestamps, ranges

def get_ranging_time_series(data, source_tottag_label, destination_tottag_label, start_timestamp=None, end_timestamp=None, cutoff_distance=30, unit="ft"):
    timestamps, ranges = extract_ranging_time_series(data, destination_tottag_label, start_timestamp=start_timestamp, end_timestamp=end_timestamp, cutoff_distance=cutoff_distance, unit=unit)
    plot_data(f"Ranging Data from {source_tottag_label} to {destination_tottag_label}",
              "Date and Time", f"Range ({unit})", timestamps, ranges)