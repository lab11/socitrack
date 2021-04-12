#!/usr/bin/env python

import os
import sys
from sortedcontainers import SortedDict

OUT_OF_RANGE_CODE = 999999

if len(sys.argv) < 2:
   print('USAGE: python tottag.py START_VAL END_VAL LOG_FILE_PATH LOG_FILE_PATH LOG_FILE_PATH LOG_FILE_PATH')
   sys.exit(1)
logs = sys.argv[3:]
vals = sys.argv[1:3]
logfile_date = None

#TODO Put everything in github
#TODO create a README of how to use the scripts
#TODO Message Pat my Github info

for i in logs:
    outFile = i[:-4] + "-averaged.log"
    s = open(outFile,"w+")
    with open(i) as f:
        tag = f.readline()
        header = tag
        if tag.strip(): # strip will remove all leading and trailing whitespace such as '\n' or ' ' by default
            tag = tag.strip("\n ' '")
            tag = tag.split()[6].split(',')[0]
        sd = SortedDict()
        for line in f:
            if line[0] != '#':
                tokens = line.split('\t')
                if (int(tokens[2]) != OUT_OF_RANGE_CODE):
                    if (int(tokens[0]) >= int(vals[0]) and int(tokens[0]) <= int(vals[1])):
                        sd.setdefault(tokens[0], {}).setdefault(tokens[1], []).append(tokens[2].rstrip('\n'))
        for x in logs:
            if (x != i):
                with open(x) as w:
                    find = w.readline()
                    if find.strip(): # strip will remove all leading and trailing whitespace such as '\n' or ' ' by default
                        find = find.strip("\n ' '")
                        find = find.split()[6].split(',')[0]
                    for row in w:
                        if row[0] != '#':
                            token = row.split('\t')
                            if (token[1] == tag):
                                if (int(token[2]) != OUT_OF_RANGE_CODE):
                                    if (int(token[0]) >= int(vals[0]) and int(token[0]) <= int(vals[1])):
                                        sd.setdefault(token[0], {}).setdefault(find, []).append(token[2].rstrip('\n'))
                w.close()
        totalVal = 0
        s.write(header)
        for time in sd:
            for tag in sd[time]:
                for val in sd[time][tag]:
                    totalVal += int(val)
                s.write(time+'\t'+tag+'\t'+str(int(totalVal/len(sd[time][tag])))+'\n')
                totalVal = 0
    f.close()
