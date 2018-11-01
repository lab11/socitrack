#!/usr/bin/env python3

import glob
import json
import numpy as np
import os
import pprint
import sys

import dataprint

# INIT / CONFIG --------------------------------------------------------------------------------------------------------

OUTPUT_FNAME = 'module_calibration.data'

if len(sys.argv) != 2:
	print('Need to pass condensed calibration file as first argument.')
	sys.exit(1)

condensed = sys.argv[1]
outfilename_base = os.path.splitext(condensed)[0]

meta = os.path.splitext(condensed)[0] + '.meta'
meta = json.loads(open(meta).read())

SPEED_OF_LIGHT = 299792458
DWT_TIME_UNITS = (1.0/499.2e6/128.0) #!< = 15.65e-12 s

CALIBRATION_NUM_NODES=3
CALIB_NUM_ANTENNAS=3
CALIB_NUM_CHANNELS=3


# HELPERS --------------------------------------------------------------------------------------------------------------

def dwtime_to_dist(dwtime):
	dist = dwtime * DWT_TIME_UNITS * SPEED_OF_LIGHT;
	#dist += ANCHOR_CAL_LEN;
	#dist -= txDelayCal[anchor_id*NUM_CHANNELS + subseq_num_to_chan(subseq, true)];
	return dist;

def dist_to_dwtime(dist):
	dwtime = dist / (DWT_TIME_UNITS * SPEED_OF_LIGHT)
	return dwtime

def nodeid_to_module_id(nodeid):
	module_base = 'c0:98:e5:42:0'
	out = '{}{}:{}'.format(module_base, nodeid[9], nodeid[10:])
	return out

# Distance in dwtime between tags
l = dist_to_dwtime(1.00)

def sub_dw_ts(a,b):
	if b > a:
		print(b)
		print(a)
		raise
	return a-b


# PARSING --------------------------------------------------------------------------------------------------------------

out = [
		('round', 'calibration', 'deltaB', 'epsilonC', 'converted'),
		]

# { 'NodeA': {(ant,ch): cal, (ant,ch): cal, ...}, 'NodeB': ... }
calibration = {'A': {}, 'B': {}, 'C': {}}

for line in open(condensed):
	try:
		round_num, node, letters = line.split(maxsplit=2)
		round_num = int(round_num)
		L, M, N, O, P, Q = map(int, letters.split())
	except ValueError:
		continue

	antenna = int(round_num / CALIBRATION_NUM_NODES) % CALIB_NUM_ANTENNAS
	channel = int(int(round_num / CALIBRATION_NUM_NODES) / CALIB_NUM_ANTENNAS) % CALIB_NUM_CHANNELS
	node_cal = calibration[node]

	if (antenna,channel) not in node_cal:
		node_cal[(antenna,channel)] = []

	# Adjust clock from C to B
	k = sub_dw_ts(Q,O) / sub_dw_ts(P,N)

	# Calculate BRX2 - BRX1
	deltaB = sub_dw_ts(O,L)

	# Calculate CTX2 - CRX1
	epsilonC = sub_dw_ts(N,M)

	# Calculate r_c + t_c
	cal = deltaB - epsilonC * k - l

	node_cal[(antenna,channel)].append(cal)


# http://stackoverflow.com/questions/11686720/is-there-a-numpy-builtin-to-reject-outliers-from-a-list
def reject_outliers(data, m=2.):
	d = np.abs(data - np.median(data))
	mdev = np.median(d)
	s = d/mdev if mdev else 0.
	return data[s<m]

for node in ('A', 'B', 'C'):
	for conf in calibration[node]:
		cal = np.array(calibration[node][conf])
		rej = reject_outliers(cal)

		if (np.std(rej) > 1000):
			print('WARN: Bad calibration for node {} conf {}'.format(node,conf))
			print(cal)
			print(rej)
			calibration[node][conf] = -1
		else:
			print(len(rej))
			#calibration[node][conf] = int(round(np.mean(rej)))
			calibration[node][conf] = int(round(np.percentile(rej, 12)))

pprint.pprint(calibration)


# PRINT ----------------------------------------------------------------------------------------------------------------

# Desired print order is (channel,antenna)
print_order = []
for ch in range(3):
	for ant in range(3):
		print_order.append((ch,ant))

module_nodes = {}
try:
	for line in open(OUTPUT_FNAME):
		if '#' in line:
			continue
		module_node_id,rest = line.split(maxsplit=1)
		module_nodes[module_node_id] = rest.split()
except IOError:
	pass

for node in (('A','0'), ('B','1'), ('C','2')):
	node_id = meta[node[1]]
	row = []
	for conf in print_order:
		try:
			row.append(calibration[node[0]][conf])
		except KeyError:
			row.append(-1)
	module_nodes[nodeid_to_module_id(node_id)] = row

outdata = []
outdata.append('# Columns are formatted as (channel, antenna)'.split())
header = ['# Node ID',]
header.extend(map(str, print_order))
outdata.append(header)

for module_id in sorted(module_nodes.keys()):
	row = [module_id,]
	row.extend(module_nodes[module_id])
	outdata.append(row)

print(outdata)

dataprint.to_newfile(OUTPUT_FNAME, outdata, overwrite=True)

