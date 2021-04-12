#!/usr/bin/env python

import os
import sys

#class to handle the moving average.
#works kind of like a queue, keeping smoothVal values stored
class SmoothedGroup:
    def __init__(self, time, mote, val, size, s):
        self.stamps = []
        self.data = []

        self.size = size
        self.stamps.append(time)
        self.data.append(val)
        self.mote = mote
        self.lastTime = time
        self.outfile = s

    def addVal(self, time, val):
        self.stamps.insert(0, time)
        self.data.insert(0, val)
        self.lastTime = time
        if (len(self.data) >= self.size):
            self.average()

    def clear(self):
        self.stamps = []
        self.data = []

#helper methof to average and write out the line. Pops oldest value off
    def average(self):
        averageVal = 0
        index = int(self.size/2)
        for i in range(0, self.size):
            averageVal += self.data[i]
        self.data.pop()
        self.stamps.pop()
        averageVal /= self.size
        self.outfile.write(str(self.stamps[index])+"\t"+self.mote+"\t"+str(round((int(averageVal)/25.4/12), 2))+"\n")



OUT_OF_RANGE_CODE = 999999

if len(sys.argv) < 2:
   print('USAGE: python tottag.py SMOOTHING VALUE LOG_FILE_PATH LOG_FILE_PATH LOG_FILE_PATH LOG_FILE_PATH')
   sys.exit(1)
logs = sys.argv[2:]
smoothVal = int(sys.argv[1])
logfile_date = None

for i in logs:
    outFile = i[:-4] + "-smoothed.log"
    s = open(outFile, "w+")
    first = {}
    classDict = {}
    with open(i) as f:
        s.write(f.readline())
        for line in f:
            if line[0] != '#':
                tokens = line.split('\t')
                #this if only operates on the first recording from each mote.
                #serves to initialize the class
                if (first.setdefault(tokens[1], True) and int(tokens[2]) != OUT_OF_RANGE_CODE):
                    classDict[tokens[1]] = classDict.setdefault(tokens[1], SmoothedGroup(int(tokens[0]), tokens[1], int(tokens[2]), smoothVal, s))
                    first[tokens[1]] = False
                elif (int(tokens[2]) != OUT_OF_RANGE_CODE):
                    #checks here for time skips
                    timeDiff = int(tokens[0]) - classDict[tokens[1]].lastTime
                    if (timeDiff == 1):
                        classDict[tokens[1]].addVal(int(tokens[0]), int(tokens[2]))
                    #If the skip is small, fills in time with current value
                    elif (timeDiff > 1 and timeDiff <= smoothVal):
                        for i in range(classDict[tokens[1]].lastTime + 1, int(tokens[0]) + 1):
                            classDict[tokens[1]].addVal(i, int(tokens[2]))
                    #If the skip is larger than the smoothVal, it starts the buffer over
                    else:
                        classDict[tokens[1]].clear()
                        classDict[tokens[1]].addVal(int(tokens[0]), int(tokens[2]))
        f.close()
    s.close()
