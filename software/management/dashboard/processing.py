#!/usr/bin/env python3
# -*- coding: utf-8 -*-

# PYTHON INCLUSIONS ---------------------------------------------------------------------------------------------------

from datetime import datetime, timedelta
import matplotlib.pyplot as plt
import matplotlib.dates as mdates
import pandas, pickle
from matplotlib.widgets import Slider
import textwrap
import re


# HELPER FUNCTIONS ----------------------------------------------------------------------------------------------------
def seconds_to_human_readable(seconds):
    # Calculate minutes and seconds
    minutes, seconds = divmod(seconds, 60)
    # Calculate hours and remaining minutes
    hours, minutes = divmod(minutes, 60)

    # Create a formatted string
    result = ""
    if hours > 0:
        result += f"{int(hours)}h"
    if minutes > 0:
        result += f"{int(minutes)}m"
    if seconds > 0:
        result += f"{int(seconds)}s"

    return result

def extract_simple_event_log(logpath):
    #line fomat of the simple event log: 2024-02-22 07:33:11 blabla
    with open(logpath) as f:
        event_log = f.readlines()
    event_dict = {}

    # Regular expression pattern to extract timestamp and message
    pattern = re.compile(r'(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}) (.*)')

    for line in event_log:
        match = pattern.match(line)
        if match:
            timestamp_str, message = match.groups()
            #timestamp = datetime.strptime(timestamp_str, '%Y-%m-%d %H:%M:%S')
            timestamp = mdates.date2num(datetime.strptime(timestamp_str, "%Y-%m-%d %H:%M:%S"))
            event_dict[timestamp] = message

    # Display the dictionary
    #for timestamp in event_dict:
    #    print(f"{timestamp}: {event_dict[timestamp]}")
    return event_dict

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

def visualize_ranging_pair_slider(data1, data2, label1, label2, start_timestamp=None, end_timestamp=None, unit="ft", events=None):
    ylabel = f"Range ({unit})"
    #extract timestamps and ranges
    timestamps1, ranges1 = extract_ranging_time_series(data1, label2, start_timestamp = start_timestamp, end_timestamp = end_timestamp, unit=unit)
    timestamps2, ranges2 = extract_ranging_time_series(data2, label1, start_timestamp = start_timestamp, end_timestamp = end_timestamp, unit=unit)

    #set up subplots
    fig, axes = plt.subplots(2,2)
    ax1 = axes[0,0]
    ax2 = axes[1,0]
    ax3 = axes[0,1]
    ax4 = axes[1,1]

    plt.subplots_adjust(bottom=0.25)

    #time is expressed in days as floats
    tmin = min(timestamps1[0], timestamps2[0])
    tmax = max(timestamps1[-1], timestamps2[-1])
    ymin = min(ranges1.min(), ranges2.min())
    ymax = max(ranges2.max(), ranges2.max())
    init_pos = tmin

    #be careful with the timezone of the log if place of collection and place of analysis are in different timezones
    #plot the indication first so it will not hide the actual plot
    if events is not None:
        ax1.vlines(x=list(events.keys()), ymin=ymin, ymax=ymax, colors='orange', ls='-', lw=2, label='vline_multiple - full height')
        ax2.vlines(x=list(events.keys()), ymin=ymin, ymax=ymax, colors='orange', ls='-', lw=2, label='vline_multiple - full height')

    ax1.plot(timestamps1, ranges1)
    ax2.plot(timestamps2, ranges2)
    ax1.yaxis.set_label_text(f"{label1}-{label2} {ylabel}")
    ax2.yaxis.set_label_text(f"{label2}-{label1} {ylabel}")
    ax1.set_ylim([0, ymax])
    ax2.set_ylim([0, ymax])
    #suppress the tick texts
    ax1.xaxis.set_ticklabels([])
    ax2.xaxis.set_major_formatter(mdates.DateFormatter('%y-%m-%d %H:%M:%S'))
    ax2.figure.autofmt_xdate(rotation=60, bottom=0.3)
    ax2.xaxis.set_tick_params(labelsize=8)

    ax3.plot(timestamps1, ranges1)
    ax4.plot(timestamps2, ranges2)
    # suppress the tick texts
    ax3.xaxis.set_ticklabels([])
    plt.setp(ax3.get_xticklabels(), visible=False)
    ax4.xaxis.set_major_formatter(mdates.DateFormatter('%y-%m-%d %H:%M:%S'))
    ax4.figure.autofmt_xdate(rotation=60, bottom=0.3)
    ax4.xaxis.set_tick_params(labelsize=8)

    # use rect to avoid overlapping with the slider
    plt.tight_layout(rect=[0, 0.11, 1, 1])

    # this should be done after tight layout, otherwise the layout will be messed up
    if events is not None:
        for e in events:
            wrapped_text = textwrap.fill(events[e], width=12)
            ax3.annotate(wrapped_text, xy =(e, 0), xytext =(e, 1), fontsize=5, arrowprops = dict(facecolor ='green', shrink = 0.05),)

    # choose the slider color
    slider_color = 'White'

    axis_size= plt.axes([0.1, 0.08, 0.65, 0.03], facecolor = slider_color)
    window_sizes = [10,15,30,60,300,600,900,1800,3600]
    #window_size_slider = Slider(axis_size, 'WindowSize', 30, 3600, valinit=window_size*86400)
    init_sel = 2
    window_size_slider = Slider(axis_size, 'WindowSize', 0, 7, valinit=init_sel, valstep=1)
    window_size = window_sizes[init_sel]/86400.0
    window_size_slider.valtext.set_text(seconds_to_human_readable(window_sizes[init_sel]))

    # set the window slider
    axis_position = plt.axes([0.1, 0.05, 0.7, 0.03], facecolor = slider_color)
    window_position_slider = Slider(axis_position, 'WindowStart', tmin, tmax-window_size, valinit=init_pos)

    # set the initial window display
    ax3.axis([init_pos, init_pos+window_size, 0, ymax])
    ax4.axis([init_pos, init_pos+window_size, 0, ymax])
    # set the slider text
    window_position_slider.valtext.set_text(mdates.num2date(init_pos).strftime('%y-%m-%d %H:%M:%S'))

    def update_shades_and_window(pos):
        if len(update_window_position.avs)>0:
            update_window_position.avs[0].remove()
            update_window_position.avs[1].remove()

        avs1 = ax1.axvspan(pos, pos+window_size, color='gray', alpha=0.3)
        avs2 = ax2.axvspan(pos, pos+window_size, color='gray', alpha=0.3)
        update_window_position.avs = [avs1, avs2]
        ax3.axis([pos, pos+window_size, 0, ymax])
        ax4.axis([pos, pos+window_size, 0, ymax])

        fig.canvas.draw_idle()

    def update_window_position(val):
        pos = window_position_slider.val
        window_position_slider.valtext.set_text(mdates.num2date(pos).strftime('%y-%m-%d %H:%M:%S'))

        update_shades_and_window(pos)

    def update_window_size(val):
        nonlocal window_size
        window_size = window_sizes[val]/86400.0
        window_size_slider.valtext.set_text(seconds_to_human_readable(window_sizes[val]))
        pos = window_position_slider.val
        update_shades_and_window(pos)

    #to store the spans for efficient redraw
    avs1 = ax1.axvspan(tmin, tmin+window_size, color='gray', alpha=0.3)
    avs2 = ax2.axvspan(tmin, tmin+window_size, color='gray', alpha=0.3)
    update_window_position.avs = [avs1, avs2]

    # update function called using on_changed() function
    window_position_slider.on_changed(update_window_position)
    window_size_slider.on_changed(update_window_size)
    # Display the plot
    plt.get_current_fig_manager().full_screen_toggle()
    plt.show()