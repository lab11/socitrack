#! /usr/bin/env python3
import os
import math
import matplotlib
matplotlib.rcParams['pdf.fonttype'] = 42
matplotlib.rcParams['ps.fonttype'] = 42
matplotlib.rcParams['font.family'] = 'sans-serif'
matplotlib.rcParams['font.sans-serif'] = 'Arial'
import numpy as np
from scipy import stats
import matplotlib.pyplot as plt
from glob import glob
import argparse
from raw_to_agg import raw_to_agg

fnames = sorted(glob("angle_raw/2*.LOG"), key=lambda x: int(x.split('.')[0].split('_')[-1]))
print(fnames)

angles = []
datas = []

for fname in fnames:
    angle = fname.split('.')[0].split('_')[-1]
    print(fname)
    print(angle)
    angles.append(str(int(angle) - 90))

    # load tot ranges
    tot_data = raw_to_agg(fname)[:,[0,2]]
    datas.append(np.abs(tot_data[:120,1] - 1000)/10)
print(angles)

w, h = matplotlib.figure.figaspect(0.4)
fig = plt.figure(dpi=300,figsize=(w,h))
#ax = fig.add_subplot(111, polar=True)
plt.grid(True, 'both', 'y')
plt.boxplot(datas, 0, '')

plt.yticks(fontsize=20)
plt.xticks(range(1,len(datas)+1), angles, fontsize=20)
plt.ylabel('Error (cm)', fontsize=22)
plt.xlabel('Angle of Arrival (°)', fontsize=22)
#ax.set_thetamin(0)
#ax.set_thetamax(180)
plt.savefig('angle.pdf', bbox_inches='tight', format='pdf')
