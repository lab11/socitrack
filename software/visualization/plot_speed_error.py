#! /usr/bin/env python3
import os
import math
import numpy as np
import matplotlib
matplotlib.rcParams['pdf.fonttype'] = 42
matplotlib.rcParams['ps.fonttype'] = 42
matplotlib.rcParams['font.family'] = 'sans-serif'
matplotlib.rcParams['font.sans-serif'] = 'Arial'
from scipy import stats
import matplotlib.pyplot as plt
from glob import glob
import argparse
from raw_to_agg import raw_to_agg
plt.rcParams['axes.grid'] = True

def moving_average(values, window):
    weights = np.repeat(1.0, window)/window
    sma = np.convolve(values, weights, 'valid')
    return sma

def xyz_to_distance(points1, points2):
    diff = points1 - points2
    square = np.square(diff)
    sq_sum = np.sum(square, axis=1)
    return np.sqrt(sq_sum)

def xyz_to_speed(points):
    grad = np.gradient(points, 1.0/120, axis=0)
    return np.sqrt(np.sum(np.square(grad), axis=1))

def find_nearest(array, value):
    array = np.asarray(array)
    idx = (np.abs(array - value)).argmin()
    return idx

def get_shared_beginning(a, b):
    if a[0] > b[0]: return a[0]
    else: return b[0]

def get_shared_ending(a, b):
    if a[-1] < b[-1]: return a[-1]
    else: return b[-1]

def sum_squared_error(a, b):
    error = 0
    longer = a
    shorter = b
    if len(a) < len(b):
        longer = b
        shorter = a
    for x in shorter:
        y = longer[find_nearest(b[:,0], x[0])]
        square = (x[1] - y[1])**2
        error += square
    return np.array(error)

def sum_squared_error_gradient(a, b):
    error = 0
    longer = a
    shorter = b
    if len(a) < len(b):
        longer = b
        shorter = a
    for x in shorter:
        y = longer[find_nearest(b[:,0], x[0])]
        square = (x[1] - y[1])**2
        error += square
    return np.array(error)

def abs_error(a, b):
    error = []
    longer = a
    shorter = b
    if len(a) < len(b):
        longer = b
        shorter = a
    for x in shorter:
        y = longer[find_nearest(b[:,0], x[0])]
        diff = abs(x[1] - y[1])
        error.append(diff)
    return np.array(error)

f, axarr = plt.subplots(2, 2, sharex='col', sharey='row')
((ax1, ax2), (ax3, ax4)) = axarr

# load and process train speed 70
tot_fname = 'raw/DATA_1_3_27_speed-70.LOG'
o_fname = 'optitrack/Take 2019-03-27 02.47.56 PM_011-speed-70.csv'
loc_data = np.genfromtxt(o_fname, delimiter=',', skip_header=7, usecols=[0,1,2,3,4,5,6,7], missing_values=[''],filling_values=[0], dtype='float')
xyz_1 = np.nan_to_num(loc_data[:,2:5]) * 1000
xyz_2 = np.nan_to_num(loc_data[:,5:]) * 1000
distances = xyz_to_distance(xyz_1, xyz_2)
optitrack_data = loc_data[:, [1, 2]]
optitrack_data[:,1] = distances

# load tot ranges
tot_data = raw_to_agg(tot_fname)[:,[0,2]]

start = get_shared_beginning(tot_data[:,0], optitrack_data[:,0])
#end = get_shared_ending(tot_data[:,0], optitrack_data[:,0])
end = start + 10*60

optitrack_data[:,0] += 18.33

error = abs_error(optitrack_data, tot_data)/10
print('average error train: ' + str(np.average(error)))
print('90th percentile error train: ' + str(np.percentile(error, 90)))
print('95th percentile error train: ' + str(np.percentile(error, 95)))
print(stats.describe(error))
error_ma = moving_average(error, 10)
#plt.subplot(2,2,2)
#plt.plot(tot_data[:len(error_ma),0]/60,error_ma)

#plt.xlabel('Time (minutes)')
#plt.ylabel('Error (m)')
#plt.xlim((start-5)/60, (end+5)/60)
#plt.ylim(0, 1)
#plt.grid(True)
ax3.plot(tot_data[int(.5*60):int(5.5*60),0] - .5*60, error_ma[int(.5*60):int(5.5*60)])

#plt.subplot(2,2,1)
speed_2 = xyz_to_speed(xyz_1)
speed_2_sec = np.mean((speed_2[:len(speed_2)//120*120]).reshape(-1, 120), axis=1)
#plt.plot(moving_average(speed_2_sec, 5))

#plt.tick_params(axis='x', which='both', bottom=False,top=False,labelbottom=False)
#plt.ylabel('Speed (m/s)')
#plt.xlim((start-5)/60, (end+5)/60)
#plt.ylim(0, 1)
optitrack_sec = optitrack_data[0::120,0]
ax1.plot(optitrack_sec[int(.5*60):int(5.5*60)] - .5*60, speed_2_sec[int(.5*60):int(5.5*60)]/1000)
ax1.set_title("Model Train")


# load and process car fast speed
tot_fname = 'raw/DATA_1_4_2_19_1.863.LOG'
o_fname = 'optitrack/1.863v_take1.csv'
loc_data = np.genfromtxt(o_fname, delimiter=',', skip_header=7, usecols=[0,1,2,3,4,5,6,7], missing_values=[''],filling_values=[0], dtype='float')
xyz_1 = np.nan_to_num(loc_data[:,2:5]) * 1000
xyz_2 = np.nan_to_num(loc_data[:,5:]) * 1000
distances = xyz_to_distance(xyz_1, xyz_2)
optitrack_data = loc_data[:, [1, 2]]
optitrack_data[:,1] = distances

# load tot ranges
tot_data = raw_to_agg(tot_fname)[:,[0,2]]

start = get_shared_beginning(tot_data[:,0], optitrack_data[:,0])
#end = get_shared_ending(tot_data[:,0], optitrack_data[:,0])
end = start + 10*60

optitrack_data[:,0] += 37.713

error = abs_error(optitrack_data, tot_data)/1000
print('average error car: ' + str(np.average(error)))
print('90th percentile error car: ' + str(np.percentile(error, 90)))
print('95th percentile error car: ' + str(np.percentile(error, 95)))
error_ma = moving_average(error, 10) * 100
#plt.subplot(2,2,4)

#plt.xlabel('Time (minutes)')
#plt.ylabel('Error (m)')
#plt.xlim((start-5)/60, (end+5)/60)
#plt.ylim(0, 1)
#plt.grid(True)
ax4.plot(tot_data[int(1*60):6*60,0] - 60, error_ma[int(1*60):6*60])

#plt.subplot(2,2,3)
speed_2 = xyz_to_speed(xyz_2)
speed_2_sec = np.mean((speed_2[:len(speed_2)//120*120]).reshape(-1, 120), axis=1)
#plt.plot(moving_average(speed_2_sec, 5))
#plt.tick_params(axis='x', which='both', bottom=False,top=False,labelbottom=False)

#plt.xlabel('Time (minutes)')
#plt.ylabel('Speed (m/s)')
#plt.xlim((start-5)/60, (end+5)/60)
#plt.ylim(0, 1)
ax2.plot((optitrack_data[::120,0])[int(1*60):6*60] - 60, moving_average(speed_2_sec, 10)[int(1*60):6*60]/1000)
ax2.set_title("Slot Car")

for i, ax in enumerate(axarr.flat):
    ax.set(xlabel='Time (s)')
    ax.set(xlim=(0, 5*60))
    if i < 2:
        ax.set(ylabel='Speed (m/s)', ylim=(0,3.5), yticks=np.arange(0,4, .5))
    else:
        ax.set(ylabel='Error (cm)', ylim=(0,1*100))
    ax.label_outer()
plt.tight_layout()
plt.savefig('speed_vs_error.pdf', format='pdf')
