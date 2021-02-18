ERSP Analysis Software
======================

*** Under construction! ***

This directory will contain a collection of Python scripts comprising an event classifier for TotTag data. It is currently a work in progress, with limited although promising functionality.

Usage
-----

### Prerequisites

These scripts rely on several Python libraries, which can be installed by `pip3 install -r requirements.txt`.

There appear to be some issues running on Python 3.8 and below; if you follow all the other steps here correctly and still cannot get the scripts to work, try updating to Python 3.9+.

### Plotting

To simply plot a log file, run `python3 plot.py LOG_FILE_PATH`. This will open a PyPlot window displaying the raw log data overlayed by a smoothed version. 

![Example plot](https://www.dropbox.com/s/8m98i1jxuozu928/Plot%20Example.png?raw=1)

### Classifying

To train the classifiers with an experiment you've run, assuming you've collected a TotTag log and recorded a diary labeling each event that took place, run `python3 tottagProcessing.py LOG_FILE_PATH DIARY_FILE_PATH`. 

This will print out a summary of the data you've fed the program, labeling windows of time with events based on the most common event type during that time period. After training the classfiers with the dataset, it will print the accuracy of each classifier, and finally plot the log. 

![Output example](https://www.dropbox.com/s/1isa8jcawidhnrm/Classifier%20Output.png?raw=1)

It may be useful to cross-reference the plotted data and the window-labeling output in the console, to ensure windows are being reasonably labeled. Additionally, you may find it useful to plot your logs before recording your diary to help pinpoint event start and end timestamps.

### Diaries

Supervised machine learning requires labeled data, so once you've collected data, your next task is to label it manually. You can either edit a diary via raw CSV, or use our Excel template. In the [diaries directory](diaries/), there is an [Excel workbook containing a diary template](diaries/Template-Diary.xlsx), which contains a few useful features.

![Excel Template](https://www.dropbox.com/s/uua55mvdijc6vd6/Excel%20Template.png?raw=1)

The script ignores any lines beginning with the '#' sign, which is why row 1 begins as such. As you may observe from Row 1, Column A is for event labels, Column B is for start timestamps, Column C is for end timestamps, and Columns D and E are readable times, calculated via formulas from B and C. Columns A, B, and C are the ones that you need to manipulate; the only thing you need to do with D and E are to extend the formulas down as you fill in the diary, as seen below.

![Formula extension](https://www.dropbox.com/s/ktqc36jlj96a8xt/Formula%20Example.gif?raw=1)

The real times serve as little more than a sanity check; they are not processed in the scripts at all. If you have any issues, feel free to contact us. Happy diary-ing!