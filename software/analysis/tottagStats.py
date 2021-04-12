#!/usr/bin/env python

import os
import sys

# Algorithm-defined constants
OUT_OF_RANGE_CODE = 999999

# Parse command line parameters
if len(sys.argv) != 2:
   print('USAGE: python tottag.py LOG_FILE_PATH')
   sys.exit(1)
logfile = sys.argv[1]
logfile_date = None

def touchingDistance():
    with open(logfile) as f:
        first = True
        cDict = {}
        for line in f:
            if line[0] != '#':
                tokens = line.split('\t')
                if (first):
                    firstTime = int(tokens[0])
                    first = False
                if (float(tokens[2]) <= 3.0):
                    cDict[tokens[1]] = cDict.setdefault(tokens[1], 0) + 1
        lastTime = int(tokens[0])
        print("total run time ", int(((lastTime-firstTime)/60/60)), "hours", int(((lastTime-firstTime)/60%60)), "minutes and", ((lastTime-firstTime)%60), "seconds")
        for key in cDict:
            print(key, '->', int(cDict[key]/60/60), "hours", int(cDict[key]/60%60), "minutes and", (cDict[key]%60), "seconds within 3 feet")
    f.close()
    return (lastTime - firstTime)

def skip():
    with open(logfile) as f:
        lastVal = 0
        for line in f:
            if line[0] != '#':
                # Parse the individual reading parts
                tokens = line.split('\t')
                if (lastVal == 0):
                    lastVal = tokens[0]
                elif(int(tokens[0]) - int(lastVal) > 30):
                    print("skip of " + str((int(tokens[0]) - int(lastVal))) + " seconds at " + lastVal)
                lastVal = tokens[0]
    f.close()

def range():
    with open(logfile) as f:
        cDict = {}
        for line in f:
            if line[0] != '#':
                tokens = line.split('\t')
                if (tokens[2] != str(OUT_OF_RANGE_CODE)):
                    cDict[tokens[1]] = cDict.setdefault(tokens[1], 0) + 1
        for key in cDict:
            if (int(cDict[key]) > 15):
                print(key, '->', int(cDict[key]/60/60), "hours", int(cDict[key]/60%60), "minutes and", (cDict[key]%60), "seconds in range")
    f.close()

def regain():
    with open(logfile) as f:
        cDict = {}
        tDict = {}
        distanceDict = {}
        lastDict = {}
        regainValue = 30
        for line in f:
            if line[0] != '#':
                tokens = line.split('\t')
                if (float(tokens[2]) <= 3.0):
                    if (cDict.setdefault(tokens[1], 0) >= regainValue):
                        tDict[tokens[1]] = tDict.setdefault(tokens[1], 0) + 1
                    cDict[tokens[1]] = 0
                    lastDict[tokens[1]] = lastDict.setdefault(tokens[1], tokens[0])
                    distanceDict[tokens[1]] = distanceDict.setdefault(tokens[1], True)
                    distanceDict[tokens[1]] = True
                elif(distanceDict.setdefault(tokens[1], False)):
                    cDict[tokens[1]] = cDict.setdefault(tokens[1], 0) + (int(tokens[0]) - int(lastDict[tokens[1]]))
                    #print(tokens[1], '->', tokens[2], ': ', cDict[tokens[1]])
                lastDict[tokens[1]] = tokens[0]
        for key in tDict:
            print(key, '->', "Re-entered 3 feet after 30s", tDict[key], "times")
    f.close()
def main():
    totalTime = touchingDistance()
    #skip()
    range()
    regain()

if __name__ == "__main__":
    main()
