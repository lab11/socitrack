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
        return pandas.json_normalize(data=pickle.load(file)).set_index('t').T

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
    plot_data('Battery Voltage for {}'.format(tottag_label), 'Date and Time', 'Voltage (mV)', timestamps, voltages)

def get_motion_time_series(data, tottag_label):
    motions = data.loc['m'].ffill()
    timestamps = mdates.date2num([datetime.fromtimestamp(ts) for ts in motions.keys()])
    plot_data('Motion Status for {}'.format(tottag_label), 'Date and Time', 'Motion Status', timestamps, motions)

def get_ranging_time_series(data, source_tottag_label, destination_tottag_label):
    ranges = data.loc['r.' + destination_tottag_label].dropna() / 304.8
    timestamps = mdates.date2num([datetime.fromtimestamp(ts) for ts in ranges.keys()])
    plot_data('Ranging Data from {} to {}'.format(source_tottag_label, destination_tottag_label),
              'Date and Time', 'Range (ft)', timestamps, ranges)
