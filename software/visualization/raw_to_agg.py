#! /usr/bin/env python3
import os
import numpy as np
import matplotlib.pyplot as plt
from glob import glob

num_channels = 30
percentile = 45
avg_span_1 = 1
avg_span_2 = 5

def raw_to_agg(fname):
    col_time = 0
    col_nr   = 2
    col_chan = 3
    col_meas = 4

    ranges = np.loadtxt(fname, delimiter='\t', skiprows=1, dtype='int32')

    num_measurements = int(ranges.shape[0] / num_channels)

    # Calculate what the reported range would have been
    ranges_sim = np.zeros((num_measurements, 1))
    ranges_avg = np.zeros((num_measurements, 3))
    times_sim  = np.zeros((num_measurements, 1))
    nr_sim = np.zeros((num_measurements, 1))

    for i in range(num_measurements):
        distances = ranges[(i * num_channels):(1 + i * num_channels), col_meas]
        ranges_sim[i] = np.percentile(distances, percentile)
        times_sim[i] = ranges[i * num_channels, col_time]
        nr_sim[i] = ranges[i * num_channels, col_nr]

    for i in range(num_measurements):
        ranges_avg[i, 0] = np.median(ranges_sim[max(0,i-avg_span_1):min(num_measurements,i+avg_span_1)])
        ranges_avg[i, 1] = np.median(ranges_sim[max(0,i-avg_span_2):min(num_measurements,i+avg_span_2)])

    #channels = np.zeros((num_measurements, num_channels))
    #for i in range(num_channels):
    #    channels[:,i] = ranges[i * num_measurements: (1 + i) * num_measurements]
    #for i in range(num_measurements):
    #    ranges_avg[i, 3] = np.median(channels[max(0,i-avg_span_2):min(num_measurements,i+avg_span_2)])

    sim = np.zeros((num_measurements, 3))
    sim[:,0] = times_sim[:,0]
    sim[:,1] = nr_sim[:,0]
    sim[:,2] = ranges_sim[:,0]
    return sim.astype('int32')


# Make folders if needed
if not os.path.exists('agg'):
    os.makedirs('agg')

fnames = glob('raw/*.LOG')

for fname in fnames:
    data = raw_to_agg(fname)
    new_fname = fname.split('/')[-1].split('.')
    new_fname = new_fname[0] + '_agg' + '.' + new_fname[1]
    np.savetxt('agg/'+new_fname, data, fmt='%010d', delimiter='\t')
