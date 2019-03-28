#! /usr/bin/env python3
import os
import numpy as np
import matplotlib.pyplot as plt
from glob import glob
import argparse
from raw_to_agg import raw_to_agg

# Input files for simulation
parser = argparse.ArgumentParser(description='totternary speed experiment processing')
parser.add_argument('-t', dest='totternary_file', help='input totternary range file e.g. `*.LOG`')
parser.add_argument('-o', dest='optitrack_file', help='input optitrack data file e.g. `*.csv`')
args = parser.parse_args()

tot_fname = args.totternary_file
o_fname = args.optitrack_file

optitrack_data = []
totternary_data = []

def xyz_to_distance(points1, points2):
    diff = points1 - points2
    square = np.square(diff)
    sq_sum = np.sum(square, axis=1)
    return np.sqrt(sq_sum)

# load and process optitrack file to get ranges
loc_data = np.genfromtxt(o_fname, delimiter=',', skip_header=7, usecols=[0,1,2,3,4,5,6,7], missing_values=[''],filling_values=[0])
xyz_1 = loc_data[:,2:5] * 1000 # in mm
xyz_2 = loc_data[:,5:] * 1000
distances = xyz_to_distance(xyz_1, xyz_2)
optitrack_data = loc_data[:, [1, 2]]
optitrack_data[:,1] = distances
plt.plot(optitrack_data[:,0], optitrack_data[:, 1])

# load tot ranges
offset = 23
tot_data = raw_to_agg(tot_fname)
plt.plot(tot_data[:,0] - offset, tot_data[:, 2])
plt.ylim(0, 2000)
plt.show()
