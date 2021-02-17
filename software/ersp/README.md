ERSP Analysis Software
======================

*** Under construction! ***

This directory will contain a collection of Python scripts comprising an event classifier for TotTag data. It is currently a work in progress, with limited although promising functionality.

Usage
-----

### Plotting

To simply plot a log file, run `python3 plot.py LOG_FILE_PATH`. This will open a PyPlot window displaying the raw log data overlayed by a smoothed version. 

![Example plot](https://www.dropbox.com/s/8m98i1jxuozu928/Figure_1.png?dl=0)

### Classifying

To train the classifiers with an experiment you've run, assuming you've collected a TotTag log and recorded a diary labeling each event that took place, run `python3 tottagProcessing.py LOG_FILE_PATH DIARY_FILE_PATH`. 

This will print out a summary of the data you've fed the program, labeling windows of time with events based on the most common event type during that time period. After running training the classfiers with the dataset, it will print the accuracy of each classifier, and finally plot the log. 

It may be useful to cross-reference the plotted data and the window-labeling output in the console, to ensure windows are being reasonably labeled. Additionally, you may find it useful to plot your logs to help pinpoint event start and end timestamps.