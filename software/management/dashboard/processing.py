#!/usr/bin/env python3
# -*- coding: utf-8 -*-

# PYTHON INCLUSIONS ---------------------------------------------------------------------------------------------------

from datetime import datetime, timedelta
import matplotlib.pyplot as plt
import matplotlib.dates as mdates
import pandas as pd
import pickle
import numpy as np
from matplotlib.widgets import Slider
from matplotlib.widgets import TextBox
import textwrap
import re

# suppress the pandas scientific notation
pd.options.display.float_format = '{:.3f}'.format

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
            timestamp = mdates.date2num(datetime.strptime(timestamp_str, "%Y-%m-%d %H:%M:%S"))
            event_dict[timestamp] = message

    # Display the dictionary
    #for timestamp in event_dict:
    #    print(f"{timestamp}: {event_dict[timestamp]}")
    return event_dict

def load_data(filename):
    with open(filename, 'rb') as file:
        data = pd.json_normalize(data=pickle.load(file)).groupby('t').first()
    return data.reindex(pd.Series(np.arange(data.head(1).index[0], data.tail(1).index[0], 0.5))).T \
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
                   .reindex(pd.Series(np.arange(start_timestamp if start_timestamp is not None else data.T.head(1).index[0],
                                                end_timestamp if end_timestamp is not None else 1+data.T.tail(1).index[0],
                                                0.5)))
    timestamps = mdates.date2num([datetime.fromtimestamp(ts) for ts in ranges.keys()])
    return timestamps, ranges

def get_ranging_time_series(data, source_tottag_label, destination_tottag_label, start_timestamp=None, end_timestamp=None, cutoff_distance=30, unit="ft"):
    timestamps, ranges = extract_ranging_time_series(data, destination_tottag_label, start_timestamp=start_timestamp, end_timestamp=end_timestamp, cutoff_distance=cutoff_distance, unit=unit)
    plot_data(f"Ranging Data from {source_tottag_label} to {destination_tottag_label}",
              "Date and Time", f"Range ({unit})", timestamps, ranges)

def visualize_ranging_pair_slider(data1, data2, label1, label2, start_timestamp=None, end_timestamp=None, unit="ft", events=dict()):
    ylabel = f"Range ({unit})"
    #extract timestamps and ranges
    timestamps1, ranges1 = extract_ranging_time_series(data1, label2, start_timestamp = start_timestamp, end_timestamp = end_timestamp, unit=unit)
    timestamps2, ranges2 = extract_ranging_time_series(data2, label1, start_timestamp = start_timestamp, end_timestamp = end_timestamp, unit=unit)

    #set up subplots: left side ax1, ax2; right side ax3, ax4
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
    def draw_indications(events, event_offset):
        events_timestamps = list(events.keys())
        # Convert matplotlib date to datetime object
        timestamps = mdates.num2date(events_timestamps)
        # Add 1 second to the timestamp
        timestamps = [t+timedelta(seconds=float(event_offset)) for t in timestamps]
        # Convert the updated timestamp back to the matplotlib date format
        events_timestamps_with_offset = mdates.date2num(timestamps)

        vlines1 = ax1.vlines(x=events_timestamps_with_offset, ymin=ymin, ymax=ymax, colors='orange', ls='-', lw=2, label='vline_multiple - full height')
        vlines2 = ax2.vlines(x=events_timestamps_with_offset, ymin=ymin, ymax=ymax, colors='orange', ls='-', lw=2, label='vline_multiple - full height')
        return vlines1, vlines2

    if len(events):
        vlines1, vlines2 = draw_indications(events, 0)

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

    # choose the slider color
    slider_color = 'White'

    # set up the window size slider
    axis_size= plt.axes([0.1, 0.08, 0.65, 0.03], facecolor = slider_color)
    window_sizes = [10,15,30,60,300,600,900,1800,3600]

    init_sel = 2
    window_size_slider = Slider(axis_size, 'WindowSize', 0, 8, valinit=init_sel, valstep=1)
    window_size = window_sizes[init_sel]/86400.0
    window_size_slider.valtext.set_text(seconds_to_human_readable(window_sizes[init_sel]))

    # setup the window position slider
    axis_position = plt.axes([0.1, 0.05, 0.7, 0.03], facecolor = slider_color)
    window_position_slider = Slider(axis_position, 'WindowStart', tmin, tmax-window_size, valinit=init_pos)

    # set the initial window display
    ax3.axis([init_pos, init_pos+window_size, 0, ymax])
    ax4.axis([init_pos, init_pos+window_size, 0, ymax])
    # set the slider text
    window_position_slider.valtext.set_text(mdates.num2date(init_pos).strftime('%y-%m-%d %H:%M:%S'))

    # setup the delay input box
    if len(events):
        axbox = fig.add_axes([0.1, 0.11, 0.3, 0.02])
        text_box = TextBox(axbox, "EventOffset (s)")
        text_box.set_val(f"{0}")  # Trigger `submit` with the initial string.

    def draw_annotations(events, event_offset):
        annotations = []
        if len(events):
            for e in events:
                wrapped_text = textwrap.fill(events[e], width=12)
                e_with_offset = mdates.date2num(mdates.num2date(e)+timedelta(seconds=float(event_offset)))
                anno = ax4.annotate(wrapped_text, xy =(e_with_offset, 0), xytext =(e_with_offset, 1), fontsize=5, arrowprops = dict(facecolor ='green', shrink = 0.05),)
                annotations.append(anno)
        return annotations

    # draw the annotations with 0s offset
    # this should be done after tight layout, otherwise the layout will be messed up
    annotations = draw_annotations(events, 0)

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
        # limiting the movable position
        if pos+window_size>tmax:
            window_position_slider.set_val(tmax-window_size)
            return
        window_position_slider.valtext.set_text(mdates.num2date(pos).strftime('%y-%m-%d %H:%M:%S'))
        update_shades_and_window(pos)

    def update_window_size(val):
        nonlocal window_size
        window_size = window_sizes[val]/86400.0
        window_size_slider.valtext.set_text(seconds_to_human_readable(window_sizes[val]))
        pos = window_position_slider.val
        # dial the slider back if the range is reached
        if pos+window_size>tmax:
            pos = tmax-window_size
            window_position_slider.set_val(pos)
        update_shades_and_window(pos)

    def submit(event_offset):
        """
        Change the events timestamp. Positive offset: delay the events. Negative offset: advance the events.
        When the labels lagged behind, apply negative offset; otherwise apply positive offset.
        """
        nonlocal vlines1, vlines2, annotations
        # redraw the indication vlines
        vlines1.remove()
        vlines2.remove()
        draw_indications(events, event_offset)

        # redraw the annotations
        for anno in annotations:
            anno.remove()
        annotations = draw_annotations(events, event_offset)
        fig.canvas.draw_idle()

    # to store the spans for efficient redraw
    avs1 = ax1.axvspan(tmin, tmin+window_size, color='gray', alpha=0.3)
    avs2 = ax2.axvspan(tmin, tmin+window_size, color='gray', alpha=0.3)
    update_window_position.avs = [avs1, avs2]

    # update function called using on_changed() function
    window_position_slider.on_changed(update_window_position)
    window_size_slider.on_changed(update_window_size)
    if len(events):
        text_box.on_submit(submit)
    # Display the plot
    plt.get_current_fig_manager().full_screen_toggle()
    plt.show()