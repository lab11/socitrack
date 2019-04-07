#! /usr/bin/env python3
import os
import math
import numpy as np
from scipy import stats
import matplotlib.pyplot as plt
from glob import glob
import argparse
from raw_to_agg import raw_to_agg

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

def proc(tot_fname, o_fname, fname, offsets, plot=False, gradient=False):
    # load and process optitrack file to get ranges
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

    if plot:
        plt.plot(optitrack_data[:,0], optitrack_data[:, 1])
        plt.plot(tot_data[:,0], tot_data[:, 1])
        plt.ylim(0, 2000)
        plt.show()
        exit()

    # align data
    # use multiple decades of steps
    steps = [1,0.1,0.01,0.001]
    offset_start = offsets[0]
    offset_end = offsets[1]
    for s in steps:
        possible_offsets = np.arange(offset_start, offset_end, s)
        errors = []

        for i, offset in enumerate(possible_offsets):
            # sum squared error
            aligned_optitrack = np.nan_to_num(optitrack_data, copy=True)
            aligned_optitrack[:,0] += offset
            if gradient:
                error = sum_squared_error(np.gradient(tot_data, 1, axis=0), np.gradient(aligned_optitrack, 1.0/120, axis=0))
            else:
                error = sum_squared_error(tot_data, aligned_optitrack)
            errors.append(error)

        errors = np.array(errors)
        ind = errors.argmin()
        best_offset = possible_offsets[ind]
        offset_start = best_offset - s
        offset_end   = best_offset + s
        print(best_offset)

    speed = o_fname.split('.')[-2].split('-')[-1]
    optitrack_data[:,0] += best_offset
    plt.figure()
    plt.plot(optitrack_data[:,0], optitrack_data[:, 1])
    plt.plot(tot_data[:,0], tot_data[:, 1])
    plt.ylim(0, 2000)
    plt.savefig('ranges-'+speed+'.pdf', format='pdf')

    error = abs_error(optitrack_data, tot_data)/1000
    print(stats.describe(error))
    error_ma = moving_average(error, 10)
    plt.subplot(2,1,2)
    plt.plot(tot_data[:len(error_ma),0]/60,error_ma)

    plt.xlabel('Time (minutes)')
    plt.ylabel('Error (m)')
    plt.xlim((start-5)/60, (end+5)/60)
    plt.ylim(0, 1)
    plt.grid(True)

    plt.subplot(2,1,1)
    speed_2 = xyz_to_speed(xyz_1)
    speed_2_sec = np.mean((speed_2[:len(speed_2)//120*120]).reshape(-1, 120), axis=1)
    #plt.plot(moving_average(speed_2_sec, 5))
    plt.plot(optitrack_data[0:-120:120,0]/60, speed_2_sec/1000)
    plt.tick_params(axis='x', which='both', bottom=False,top=False,labelbottom=False)

    #plt.xlabel('Time (minutes)')
    plt.ylabel('Speed (m/s)')
    plt.xlim((start-5)/60, (end+5)/60)
    plt.ylim(0, 1)

    plt.savefig(fname, format='pdf')

if __name__ == '__main__':
    # Input files for simulation
    parser = argparse.ArgumentParser(description='totternary speed experiment processing')
    parser.add_argument('-t', dest='totternary_file', help='input totternary range file e.g. `*.LOG`')
    parser.add_argument('-i', dest='optitrack_file', help='input optitrack data file e.g. `*.csv`')
    parser.add_argument('-p', action="store_true", default=False, help='just plot the two traces')
    parser.add_argument('-s', dest="start", type=float, help='start of search')
    parser.add_argument('-e', dest="end", type=float, help='end of search')
    parser.add_argument('-g', dest="gradient", default=False, help='use gradients to align')
    parser.add_argument('-o', dest="output", help='output pdf file')
    args = parser.parse_args()

    tot_fname = args.totternary_file
    o_fname = args.optitrack_file
    fname = args.output

    proc(tot_fname, o_fname, fname, (args.start, args.end), args.p, args.gradient)
