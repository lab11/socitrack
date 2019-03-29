#! /usr/bin/env python3
import os
import math
import numpy as np
import matplotlib.pyplot as plt
from glob import glob
import argparse
from raw_to_agg import raw_to_agg

# Input files for simulation
parser = argparse.ArgumentParser(description='totternary speed experiment processing')
parser.add_argument('-t', dest='totternary_file', help='input totternary range file e.g. `*.LOG`')
parser.add_argument('-o', dest='optitrack_file', help='input optitrack data file e.g. `*.csv`')
parser.add_argument('-p', action="store_true", default=False, help='just plot the two traces')
parser.add_argument('-s', dest="start", type=float, help='start of search')
parser.add_argument('-e', dest="end", type=float, help='end of search')
args = parser.parse_args()

tot_fname = args.totternary_file
o_fname = args.optitrack_file

optitrack_data = []
totternary_data = []

def moving_average(values, window):
    weights = np.repeat(1.0, window)/window
    sma = np.convolve(values, weights, 'valid')
    return sma

def xyz_to_distance(points1, points2):
    diff = points1 - points2
    square = np.square(diff)
    sq_sum = np.sum(square, axis=1)
    return np.sqrt(sq_sum)

def find_nearest(array, value):
    array = np.asarray(array)
    idx = (np.abs(array - value)).argmin()
    return idx

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

# load and process optitrack file to get ranges
loc_data = np.genfromtxt(o_fname, delimiter=',', skip_header=7, usecols=[0,1,2,3,4,5,6,7], missing_values=[''],filling_values=[0])
xyz_1 = loc_data[:,2:5] * 1000 # in mm
xyz_2 = loc_data[:,5:] * 1000
distances = xyz_to_distance(xyz_1, xyz_2)
optitrack_data = loc_data[:, [1, 2]]
optitrack_data[:,1] = distances

# load tot ranges
offset = 23
tot_data = raw_to_agg(tot_fname)[:,[0,2]]

if args.p:
    plt.plot(optitrack_data[:,0], optitrack_data[:, 1])
    plt.plot(tot_data[:,0], tot_data[:, 1])
    plt.ylim(0, 2000)
    plt.show()
    exit()


# align data
# use multiple decades of steps
steps = [1,0.1,0.01,0.001]
start = args.start
end = args.end
for s in steps:
    possible_offsets = np.arange(start, end, s)
    errors = []

    for i, offset in enumerate(possible_offsets):
        # sum squared error
        aligned_optitrack = np.nan_to_num(optitrack_data, copy=True)
        aligned_optitrack[:,0] += offset
        error = sum_squared_error(tot_data, aligned_optitrack)
        errors.append(error)

    errors = np.array(errors)
    ind = errors.argmin()
    best_offset = possible_offsets[ind]
    start = best_offset - s
    end   = best_offset + s
    print(best_offset)

optitrack_data[:,0] += best_offset
#plt.plot(optitrack_data[:,0], optitrack_data[:, 1])
#plt.plot(tot_data[:,0], tot_data[:, 1])
#plt.ylim(0, 2000)
#plt.show()

speed = o_fname.split('.')[-2].split('-')[-1]
error = abs_error(optitrack_data, tot_data)/1000
plt.title('Moving average of error for speed ' + speed)
plt.xlabel('Time (s)')
plt.ylabel('Error (m)')
plt.ylim(0, 1)
plt.plot(moving_average(error, 30))
plt.savefig('error-'+speed+'.pdf', format='pdf')
