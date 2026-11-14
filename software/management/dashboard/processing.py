#!/usr/bin/env python3
# -*- coding: utf-8 -*-

# PYTHON INCLUSIONS ---------------------------------------------------------------------------------------------------

from datetime import datetime, timedelta, timezone
import matplotlib.pyplot as plt
import matplotlib.dates as mdates
import pandas as pd
import pickle
import numpy as np
from matplotlib.widgets import Slider
from matplotlib.widgets import TextBox
import textwrap
import re
from scipy.signal import find_peaks
import os
import pytz
import tzlocal
try:
    from .deployment_record import load_records as load_deployment_records, find_record as find_deployment_record
except ImportError:
    from deployment_record import load_records as load_deployment_records, find_record as find_deployment_record

# suppress the pandas scientific notation
pd.options.display.float_format = '{:.3f}'.format

# Spacing of the uniform time grid that every series is resampled onto
GRID_STEP_S = 0.5

# TIME ZONES ----------------------------------------------------------------------------------------------------------
#
# A log's times are Unix timestamps. Plots and daily statistics show them on the study's clock: the zone named by the
# deployment record that matches the log (see load_data), unless a function is given timezone= outright. Where neither
# says, they fall back to this computer's zone and print a note saying so.

def machine_timezone():
    return tzlocal.get_localzone_name()

def study_timezone(data, timezone=None):
    """The zone to show `data` (a frame from load_data, or a tuple of them) in: `timezone` if given, else that of its
    deployment record, else this computer's, with a note the first time."""
    if timezone is not None:
        return timezone
    frames = data if isinstance(data, (list, tuple)) else (data,)
    for frame in frames:
        if frame.attrs.get('timezone'):
            return frame.attrs['timezone']
    zone = machine_timezone()
    if not any('timezone' in frame.attrs for frame in frames):
        print(f"Note: no deployment record was found for this data, so its times are shown in this computer's time zone, {zone}.")
        for frame in frames:
            frame.attrs['timezone'] = None
    return zone

def local_times(timestamps, timezone):
    """Unix timestamps as the clock in `timezone` read them, without the zone attached."""
    return pd.to_datetime(np.asarray(timestamps, dtype=float), unit='s', utc=True).tz_convert(timezone).tz_localize(None)

def plot_times(timestamps, timezone):
    """Matplotlib dates for Unix timestamps, placed where the clock in `timezone` read them."""
    return mdates.date2num(local_times(timestamps, timezone))

def grid_minutes(samples):
    """Minutes spanned by this many points on the time grid."""
    return samples * GRID_STEP_S / 60

def whole_minutes(samples):
    """grid_minutes() to the nearest minute, as the daily statistics print it."""
    return int(grid_minutes(samples) + 0.5)

# HELPER FUNCTIONS ----------------------------------------------------------------------------------------------------

def seconds_to_human_readable(seconds):
    # Calculate minutes and seconds
    minutes, seconds = divmod(seconds, 60)
    # Calculate hours and remaining minutes
    hours, minutes = divmod(minutes, 60)

    # Create a formatted string
    result = ''
    if hours > 0:
        result += f'{int(hours)}h'
    if minutes > 0:
        result += f'{int(minutes)}m'
    if seconds > 0:
        result += f'{int(seconds)}s'

    return result

def extract_simple_event_log(logpath):
    # Line format of the simple event log: 2024-02-22 07:33:11 blabla
    # Times are kept as the clock read them, so a plot places them on its own clock: the study's, unless told otherwise
    with open(logpath, errors='ignore') as f:
        event_log = f.readlines()
    event_dict = {}

    # Regular expression pattern to extract timestamp and message
    pattern = re.compile(r'(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}) (.*)')

    for line in event_log:
        match = pattern.match(line)
        if match:
            timestamp_str, message = match.groups()
            timestamp = mdates.date2num(datetime.strptime(timestamp_str, '%Y-%m-%d %H:%M:%S'))
            event_dict[timestamp] = message

    # Display the dictionary
    #for timestamp in event_dict:
    #    print(f'{timestamp}: {event_dict[timestamp]}')
    return event_dict

def load_data(filename, timezone=None, records=None):
    """A .pkl's records on the time grid, one column per instant, to be shown in the study's time zone.

    That zone is `timezone` if given, otherwise the one named by the deployment record that matches the log, looked
    for in `records` (a record file, a folder of them, or records already loaded) or else in the log's own folder.
    """
    # Records are snapped to the grid before reindexing
    with open(filename, 'rb') as file:
        data = pd.json_normalize(data=pickle.load(file))
    if data is None or not len(data.index):
        return None
    first_timestamp = data['t'].min()
    data['t'] = np.round(data['t'] / GRID_STEP_S) * GRID_STEP_S
    data = data.groupby('t').first()
    data = data.reindex(pd.Series(np.arange(data.index[0], 1 + data.index[-1], GRID_STEP_S))).T
    if timezone is None:
        record = find_deployment_record(filename, records, first_timestamp=first_timestamp)
        timezone = record['deployment']['timezone'] if record else None
        if timezone is None:
            print(f"Note: no deployment record matches {os.path.basename(filename)}, so its times are shown in this computer's time zone, {machine_timezone()}.")
    data.attrs['timezone'] = timezone
    return data

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

def get_voltage_time_series(data, tottag_label, timezone=None):
    zone = study_timezone(data, timezone)
    voltages = data.loc['v'].dropna()
    timestamps = plot_times(voltages.keys(), zone)
    plot_data(f'Battery Voltage for {tottag_label}', f'Date and Time ({zone})', 'Voltage (mV)', timestamps, voltages)

def get_motion_time_series(data, tottag_label, timezone=None):
    zone = study_timezone(data, timezone)
    motions = data.loc['m'].ffill()
    timestamps = plot_times(motions.keys(), zone)
    plot_data(f'Motion Status for {tottag_label}', f'Date and Time ({zone})', 'Motion Status', timestamps, motions)

def get_imu_time_series(data, tottag_label, timezone=None):
    zone = study_timezone(data, timezone)
    imu_data = data.loc['i'].dropna()
    timestamps = plot_times(imu_data.keys(), zone)
    x_data = imu_data.str[0]
    y_data = imu_data.str[1]
    z_data = imu_data.str[2]
    plt.close()
    plt.title(f'IMU Data for {tottag_label}')
    axis = plt.gca()
    axis.xaxis.set_major_formatter(mdates.DateFormatter('%Y-%m-%d %H:%M:%S'))
    axis.xaxis.set_label_text(f'Date and Time ({zone})')
    axis.yaxis.set_label_text('IMU Data')
    axis.figure.autofmt_xdate(rotation=45, bottom=0.3)
    plt.plot(timestamps, x_data, label='X-axis')
    plt.plot(timestamps, y_data, label='Y-axis')
    plt.plot(timestamps, z_data, label='Z-axis')
    plt.legend()
    plt.show()

def get_ble_scan_time_series(data, tottag_label, timezone=None):
    zone = study_timezone(data, timezone)
    ble_devices = data.loc['b'].apply(lambda val: 0 if not isinstance(val, list) or not len(val) else len(val))
    timestamps = plot_times(ble_devices.keys(), zone)
    plot_data(f'BLE Scan Results for {tottag_label}', f'Date and Time ({zone})', 'Num Detected TotTags', timestamps, ble_devices)
    ble_devices = data.loc['b'].dropna()
    ble_devices = ble_devices[ble_devices.astype(bool)]
    timestamps = local_times(ble_devices.keys(), zone).strftime("%m/%d/%Y, %H:%M:%S")
    pd.set_option('display.max_rows', None)
    print(pd.DataFrame(list(zip(timestamps, ble_devices.values)), columns=['Datetime', 'TotTags']))
    pd.reset_option('display.max_rows')

def extract_ranging_time_series(data, destination_tottag_label, start_timestamp=None, end_timestamp=None, cutoff_distance=30, unit='ft', timezone=None):
    zone = study_timezone(data, timezone)
    if unit == 'ft':
        conversion_factor_from_mm = 304.8
    elif unit == 'm':
        conversion_factor_from_mm = 1000.0
    ranges = data.loc['r.' + destination_tottag_label] / conversion_factor_from_mm
    start = start_timestamp if start_timestamp is not None else data.T.index[0]
    end = end_timestamp if end_timestamp is not None else 1 + data.T.index[-1]
    start = np.round(start / GRID_STEP_S) * GRID_STEP_S
    ranges = ranges.mask(ranges > cutoff_distance)\
                   .reindex(pd.Series(np.arange(start, end, GRID_STEP_S)))
    timestamps = plot_times(ranges.keys(), zone)
    return timestamps, ranges

def get_ranging_time_series(data, source_tottag_label, destination_tottag_label, start_timestamp=None, end_timestamp=None, cutoff_distance=30, unit='ft', timezone=None):
    zone = study_timezone(data, timezone)
    timestamps, ranges = extract_ranging_time_series(data, destination_tottag_label, start_timestamp=start_timestamp, end_timestamp=end_timestamp, cutoff_distance=cutoff_distance, unit=unit, timezone=zone)
    plot_data(f'Ranging Data from {source_tottag_label} to {destination_tottag_label}',
              f'Date and Time ({zone})', f'Range ({unit})', timestamps, ranges)

def get_off_and_on_charger_times(data, label, peak_width=50, window_size=10, visualize=True, timezone=None):
    """
    default usage: get_off_and_on_charger_times(A,"10043_S1")
    """
    zone = study_timezone(data, timezone)
    voltages = data.loc['v'].dropna()
    # The timestamps are UTC; each is printed in UTC and on the study's clock
    timestamps = np.array(pd.to_datetime(np.asarray(voltages.keys(), dtype=float), unit='s', utc=True).tz_convert(zone))
    voltages = voltages.rolling(window=window_size).mean()

    peak_idx, _ = find_peaks(voltages, width=peak_width)
    valley_idx, _ = find_peaks(-voltages, width=peak_width)

    taken_off_times = timestamps[peak_idx]
    put_on_times = timestamps[valley_idx]

    off = {x:"TAKEN OFF CHARGER" for x in taken_off_times}
    on = {x:"PUT ON CHARGER" for x in put_on_times}

    combined = {k: off.get(k, "") + on.get(k, "") for k in sorted(set(off) | set(on))}

    for key in combined:
        utc_timestamp = key.astimezone(pytz.utc)
        print(utc_timestamp.strftime('%Y-%m-%d %H:%M:%S %Z'), key.strftime('%Y-%m-%d %H:%M:%S %Z'), combined[key])

    if visualize:
        # Placed where the study's clock read them, as every other plot here is
        times = plot_times(voltages.keys(), zone)
        plt.plot(times, voltages)
        axis = plt.gca()
        axis.plot(times[peak_idx], np.array(voltages)[peak_idx], 'r.', label="taken off from charger")
        axis.plot(times[valley_idx], np.array(voltages)[valley_idx], 'g.', label="put on the charger")

        axis.xaxis.set_major_formatter(mdates.DateFormatter('%Y-%m-%d %H:%M:%S'))
        axis.figure.autofmt_xdate(rotation=45, bottom=0.3)
        plt.xlabel(f'Date and Time ({zone})')
        plt.ylabel('Voltage (mV)')
        plt.title(f'Battery Voltage for {label}')
        plt.legend()
        plt.show()

def get_daily_ranging_statistics(data, target_tottag_labels, max_touching_distance, unit='ft', timezone=None):
    zone = study_timezone(data, timezone)
    ranges = data.loc[['r.' + label for label in target_tottag_labels]] / (304.8 if unit == 'ft' else 1000.0)
    # Days run midnight to midnight on the study's clock
    dates = local_times(ranges.keys(), zone).strftime('%m/%d/%Y')
    for date in dict.fromkeys(dates):
        print(f'\nRanging Statistics on {date}:')
        day_ranges = ranges.T[dates == date]
        for target in day_ranges.keys():
            print(f'   Statistics to {target}:')
            in_range_data = day_ranges[target].dropna()
            touching_distance_data = day_ranges[target].mask(day_ranges[target] > max_touching_distance).dropna()
            minutes_in_range = whole_minutes(len(in_range_data.index))
            minutes_in_touching_distance = whole_minutes(len(touching_distance_data.index))
            mean_distance_in_range = in_range_data.mean()
            print(f'      Minutes in Range: {minutes_in_range}\n      Minutes in Touching Distance: {minutes_in_touching_distance}\n      Mean Distance While in Range: {mean_distance_in_range}')
        if len(target_tottag_labels) > 1:
            print(f'   Statistics to Either of {target_tottag_labels}:')
            in_range_data = day_ranges.dropna(how='all')
            touching_distance_data = day_ranges.mask(day_ranges > max_touching_distance).dropna(how='all')
            minutes_in_range = whole_minutes(len(in_range_data.index))
            minutes_in_touching_distance = whole_minutes(len(touching_distance_data.index))
            mean_distance_in_range = pd.concat([in_range_data[col] for col in in_range_data.columns]).mean()
            print(f'      Minutes in Range: {minutes_in_range}\n      Minutes in Touching Distance: {minutes_in_touching_distance}\n      Mean Distance While in Range: {mean_distance_in_range}')
        if len(target_tottag_labels) == 2:
            print(f'   More statistics')
            in_range_data_exlusive_0 = day_ranges[day_ranges['r.'+target_tottag_labels[1]].isna() & day_ranges['r.'+target_tottag_labels[0]].notna()]
            in_range_data_exlusive_1 = day_ranges[day_ranges['r.'+target_tottag_labels[0]].isna() & day_ranges['r.'+target_tottag_labels[1]].notna()]
            in_range_data_both = day_ranges[day_ranges['r.'+target_tottag_labels[0]].notna() & day_ranges['r.'+target_tottag_labels[1]].notna()]
            minutes_in_range_exclusive_0 = grid_minutes(len(in_range_data_exlusive_0.index))
            minutes_in_range_exclusive_1 = grid_minutes(len(in_range_data_exlusive_1.index))
            minutes_in_range_both = grid_minutes(len(in_range_data_both.index))
            print(f"      Minutes in range with {target_tottag_labels[0]} but not {target_tottag_labels[1]}: {minutes_in_range_exclusive_0}")
            print(f"      Minutes in range with {target_tottag_labels[1]} but not {target_tottag_labels[0]}: {minutes_in_range_exclusive_1}")
            print(f"      Minutes in range with both targets: {minutes_in_range_both}")

def visualize_ranging_pair_slider(data1, data2, label1, label2, start_timestamp=None, end_timestamp=None, unit='ft', events=dict(), timezone=None):
    ylabel = f'Range ({unit})'
    # Both logs, and the events, on one clock: the study's unless told otherwise
    zone = study_timezone((data1, data2), timezone)
    #extract timestamps and ranges
    timestamps1, ranges1 = extract_ranging_time_series(data1, label2, start_timestamp = start_timestamp, end_timestamp = end_timestamp, unit=unit, timezone=zone)
    timestamps2, ranges2 = extract_ranging_time_series(data2, label1, start_timestamp = start_timestamp, end_timestamp = end_timestamp, unit=unit, timezone=zone)

    #set up subplots: left side ax1, ax2; right side ax3, ax4
    fig, axes = plt.subplots(2,2)
    ax1 = axes[0,0]
    ax2 = axes[1,0]
    ax3 = axes[0,1]
    ax4 = axes[1,1]

    plt.subplots_adjust(bottom=0.25)

    #time is expressed in days as floats
    tmin = max(timestamps1[0], timestamps2[0])
    tmax = min(timestamps1[-1], timestamps2[-1])
    ymin = min(ranges1.min(), ranges2.min())
    ymax = max(ranges1.max(), ranges2.max())
    init_pos = tmin

    #event times are read as the plot's clock reads them, so they line up in whichever zone it is shown in
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
    ax1.yaxis.set_label_text(f'{label1}-{label2} {ylabel}')
    ax2.yaxis.set_label_text(f'{label2}-{label1} {ylabel}')
    ax1.set_xlim([tmin, tmax])
    ax2.set_xlim([tmin, tmax])
    ax1.set_ylim([0, ymax])
    ax2.set_ylim([0, ymax])
    #suppress the tick texts
    ax1.xaxis.set_ticklabels([])
    ax2.xaxis.set_major_formatter(mdates.DateFormatter('%y-%m-%d %H:%M:%S'))
    ax2.figure.autofmt_xdate(rotation=60, bottom=0.3)
    ax2.xaxis.set_tick_params(labelsize=8)

    ax3.plot(timestamps1, ranges1)
    ax4.plot(timestamps2, ranges2)
    ax3.set_xlim([tmin, tmax])
    ax4.set_xlim([tmin, tmax])
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
        text_box = TextBox(axbox, 'EventOffset (s)')
        text_box.set_val(f'{0}')  # Trigger `submit` with the initial string.

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
        vlines1, vlines2 = draw_indications(events, event_offset)

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
