#!/usr/bin/env python3

import matplotlib.pyplot as plt
import matplotlib as mpl
import matplotlib.dates as mdates
from datetime import datetime
import itertools
import glob
import os 
import sys
import re

#figure settings
plt.rc('axes', titlesize=8)         # fontsize of the axes title
plt.rc('axes',facecolor="#efefef")  # background color
plt.rc('axes', labelsize=8)         # sizes of x labels and y labels
plt.rc('xtick', labelsize=8)        # size of xtick text
plt.rc('ytick', labelsize=8)        # size of ytick text
plt.rc('legend',fontsize=6)         # size of legend text
plt.rc( 'lines' , linewidth = 0.5 ) # width of plot line
mpl.rcParams['grid.color'] = 'white'# color of grids

#regex patterns for line checking
pattern_timestamp = re.compile("[0-9]{10}")
pattern_id = re.compile("[a-f0-9]{2}:[a-f0-9]{2}:[a-f0-9]{2}:[a-f0-9]{2}:[a-f0-9]{2}:[a-f0-9]{2}")
pattern_distance = re.compile("[0-9]{6}")

def check_ranging_line_pattern(tokens):
    return bool(pattern_timestamp.match(tokens[0])) and bool(pattern_id.match(tokens[1])) and bool(pattern_distance.match(tokens[2]))

def plot_all(foldername):
    #display the full path
    print('plotting for:',os.path.abspath(foldername),'\n')
          
    #get all the logs in the folder
    filelists = glob.glob(os.path.join(foldername,'*[0-9].log'))
    #compatible with older log files with .LOG names
    if len(filelists)==0:
        filelists = glob.glob(os.path.join(foldername,'*[0-9].LOG'))
        
    print('list of files to be plotted:')
    print(' '.join(filelists)+'\n')

    #extract the short name of the tags from files with names like 88@2022-01-01.log
    taglist = sorted([x.split('@')[0].split(os.path.sep)[-1] for x in filelists])
    #list of combinations of tags
    tagpairs = sorted(list(set(itertools.combinations(taglist, 2))))
    # Create a data structure to hold the parsed data
    tagdata = {tag:{x:[] for x in taglist} for tag in taglist}

    #one row, two columns per pair
    fig, axs = plt.subplots(len(tagpairs), 2, sharex=True)
    
    fig_together, axs_together = plt.subplots(len(tagpairs), 1, sharex=True)
    
    print('READING FILES......')

    for index, log in enumerate(filelists):
    
        current_tag = log.split('@')[0].split(os.path.sep)[-1]
    
        # Open the log file, escaping the messed up characters
        with open(log,encoding="ascii",errors="surrogateescape") as f:
            # Read data from the log file
            for line in f:
                # For now, just ignore these events
                if '#' in line:
                    continue
                # Parse out the fields
                tokens = [x.strip() for x in line.split()]
            
                # To deal with incomplete lines
                if len(tokens)!=3 or check_ranging_line_pattern(tokens)==False:
                    print('IN FILE:',log,'BAD LINE:',f'{index}', line)
                    continue
                
                timestamp,tag_id,distance = tokens[0],tokens[1],tokens[2]
                # Convert to meaningful data types
                timestamp = int(timestamp)
                tag_id = tag_id.split(':')[-1]
                distance = int(distance)
                # Filter out bad readings
                if distance == 0 or distance == 999999:
                    continue
                # Save this measurement
                if tag_id not in tagdata[current_tag]:
                    tagdata[current_tag][tag_id] = []
                tagdata[current_tag][tag_id].append((timestamp, distance))
        
    # Done parsing, plot!
    # Considers tag combination with empty ranging data
    for i, tagpair in enumerate(tagpairs):
        print("Plotting data for pair:", tagpair)
        
        if len(tagpairs)==1:
            axsAB = axs[0]
            axsBA = axs[1]
        else:
            axsAB = axs[i,0]
            axsBA = axs[i,1]
    
        dataAB = tagdata[tagpair[0]][tagpair[1]]
        dataBA = tagdata[tagpair[1]][tagpair[0]]
        
        print(f'{tagpair[1]} -> {tagpair[0]}: {len(dataAB):< 6} data points, {tagpair[0]} -> {tagpair[1]}: {len(dataBA):> 6} data points')
        
        xAB, yAB, xBA, yBA = [],[],[],[]
        
        #test equality
        dictAB = {x[0]:x[1] for x in dataAB}
        dictBA = {x[0]:x[1] for x in dataBA}
        
        timestampAB = set(dictAB.keys())
        timestampBA = set(dictBA.keys())
        shared_timestamp = timestampAB.intersection(timestampBA)
        AmB_timestamp = timestampAB.difference(timestampBA)
        BmA_timestamp = timestampBA.difference(timestampAB)
        
        inconsistent_shared = []
        
        if shared_timestamp:
            for k in shared_timestamp:
                if dictAB[k]!=dictBA[k]:
                    inconsistent_shared.append(k)
                    print(f'At: {k}, {tagpair[1]} -> {tagpair[0]}: {dictAB[k]},{tagpair[0]} -> {tagpair[1]}: {dictBA[k]}')   
            print('percent:',len(inconsistent_shared)/len(shared_timestamp), 'of shared timestamps have different ranges')
        inconsistent_shared_timestamp = set(inconsistent_shared)
        consistent_shared_timestamp = shared_timestamp.difference(inconsistent_shared)
        
        
        if len(dataAB)!=0:
            xAB, yAB = zip(*dataAB)
            xAB_h = [datetime.fromtimestamp(s) for s in xAB]
        
        if len(dataBA)!=0:
            xBA, yBA = zip(*dataBA)
            xBA_h = [datetime.fromtimestamp(s) for s in xBA]
                   
        #plot left panel
        if len(xAB)!=0:
            axsAB.scatter(xAB_h,yAB,s=1)
            axsAB.set_ylabel('dist (mm)')
            axsAB.xaxis.set_major_formatter(mdates.DateFormatter('%H:%M'))
            axsAB.grid()
        else:
            axsAB.plot() #empty subplot    
            axsAB.yaxis.set_ticks([])     
        axsAB.set_title(tagpair[1]+" seen from "+tagpair[0])
        
        #plot right panel
        if len(xBA)!=0:
            axsBA.scatter(xBA_h,yBA,s=1)
            axsBA.set_ylabel('dist (mm)')
            axsBA.xaxis.set_major_formatter(mdates.DateFormatter('%H:%M'))
            axsBA.grid()
        else:
            axsBA.plot() #empty subplot
            axsBA.yaxis.set_ticks([]) 
        axsBA.set_title(tagpair[0]+" seen from "+tagpair[1])
        
        #plot shared figure where the two sets of data are plotted on the same graph             
        if len(consistent_shared_timestamp):
            consistent_shared_timestamp = sorted(consistent_shared_timestamp)
            consistent_shared_xh, consistent_shared_y = zip( *[(datetime.fromtimestamp(t),dictAB[t]) for t in consistent_shared_timestamp])
            axs_together[i].scatter(consistent_shared_xh,consistent_shared_y,color='orange',s=1,label='consistent_shared')
        if len(inconsistent_shared_timestamp):
            inconsistent_shared_xh, inconsistent_shared_yAB,inconsistent_shared_yBA = zip(*[(datetime.fromtimestamp(t),dictAB[t], dictBA[t]) for t in inconsistent_shared_timestamp])
            axs_together[i].scatter(inconsistent_shared_xh,inconsistent_shared_yAB,color='green',label='inconsistent_shared',s=8, marker="x")
            axs_together[i].scatter(inconsistent_shared_xh,inconsistent_shared_yBA,color='green',s=8, marker="x")
        if len(AmB_timestamp):
            AmB_xh, AmB_y = zip( *[(datetime.fromtimestamp(t),dictAB[t]) for t in AmB_timestamp])
            axs_together[i].scatter(AmB_xh,AmB_y,color='r',s=1,label=f'only {tagpair[1]} -> {tagpair[0]}')
        if len(BmA_timestamp):
            BmA_xh, BmA_y = zip( *[(datetime.fromtimestamp(t),dictBA[t]) for t in BmA_timestamp])
            axs_together[i].scatter(BmA_xh,BmA_y,color='b',s=1,label=f'only {tagpair[0]} -> {tagpair[1]}')
            
        if len(consistent_shared_timestamp) or len(inconsistent_shared_timestamp) or len(AmB_timestamp) or len(BmA_timestamp):
            axs_together[i].xaxis.set_major_formatter(mdates.DateFormatter('%H:%M'))
            #plottting outside
            axs_together[i].legend(loc='upper left', bbox_to_anchor=(1, 1))
            axs_together[i].set_ylabel('dist (mm)')
        else:
            axs_together[i].yaxis.set_ticks([]) 
        axs_together[i].set_title(tagpair[0]+" and "+tagpair[1])
        
        #add space
        print('\n')

        
    fig.autofmt_xdate()
    fig.tight_layout()
    fig_together.autofmt_xdate()
    fig_together.tight_layout()
    
    plt.show()
    
    fig.savefig(os.path.join(os.path.abspath(foldername),'_'.join(taglist)+'_saved_separated_plot.png'),dpi=300)
    fig_together.savefig(os.path.join(os.path.abspath(foldername),'_'.join(taglist)+'_saved_together_plot.png'),dpi=300)

if __name__ == '__main__':
    print('Usage: python3 quickplot_folder.py for plot in current folder with the log files form a single day')
    print('Usage: python3 quickplot_folder.py foldername for plotting log files in the specified folder\n') 
    
    if len(sys.argv) == 1:
        dir_path = os.path.dirname(os.path.realpath(__file__))
    elif len(sys.argv) == 2:
        dir_path = sys.argv[1]
    else:
        print('Incorrect Usage')
        exit()             
    plot_all(dir_path)