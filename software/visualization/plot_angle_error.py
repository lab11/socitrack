#! /usr/bin/env python3
import os
import math
import matplotlib
matplotlib.rcParams['pdf.fonttype'] = 42
matplotlib.rcParams['ps.fonttype'] = 42
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
    angles.append(angle)

    # load tot ranges
    tot_data = raw_to_agg(fname)[:,[0,2]]
    datas.append(np.abs(tot_data[:120,1] - 1000)/10)
print(angles)

w, h = matplotlib.figure.figaspect(0.5)
plt.figure(dpi=300,figsize=(w,h))
plt.grid(True, 'both', 'y')
plt.boxplot(datas, 0, '')

plt.yticks(fontsize=20)
plt.xticks(range(1,len(datas)+1), angles, fontsize=20)
plt.ylabel('Error (cm)', fontsize=22)
plt.xlabel('Totternary Angle (°)', fontsize=22)
#ax = plt.gca()
#ax.set_theta_min(0)
#ax.set_theta_max(180)
plt.savefig('angle.pdf', bbox_inches='tight', format='pdf')
