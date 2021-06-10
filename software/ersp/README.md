User Guide
======================

Welcome to Team Pat's Guide!

Background Knowledge
--------------------

This script is written in Python and implements a machine learning algorithm known as K-Nearest Neighbors (KNN). It is closer to a statistical approach than traditional machine learning algorithms and is relatively simple.

-> https://towardsdatascience.com/machine-learning-basics-with-the-k-nearest-neighbors-algorithm-6a6e71d01761

-> https://www.analyticsvidhya.com/blog/2018/03/introduction-k-neighbours-algorithm-clustering/

-> https://scikit-learn.org/stable/modules/generated/sklearn.neighbors.KNeighborsClassifier.html

The KNN algorithm is fed data from TotTags broken into "sliding windows" that allow continuous data to be processed. We use information from both 5-second sliding windows and 2-second sliding windows, preferring decisions from the 5-second windows whenever possible.

-> https://towardsdatascience.com/ml-approaches-for-time-series-4d44722e48fe

Random Forest was a companion machine learning algorithm that we experimented with. It might be useful for more complex learning in the future.

-> https://towardsdatascience.com/understanding-random-forest-58381e0602d2

-> https://scikit-learn.org/stable/modules/generated/sklearn.ensemble.RandomForestClassifier.html

Python can be a rather difficult language to jump into without prior experience. Therefore, before looking into the code, it may be useful to review Python's syntax and semantics.

-> https://docs.python.org/3/reference/introduction.html

Additionally, our code is not always the cleanest or most efficient, and there are some niche functions that might be difficult for those unfamiliar with functional programming and/or higher-order functions.

```python
# the * indicates this is actually an unzipping, which in this case means that
# a list of ordered pairs is split (unzipped) into two separate lists; the first
# being the timestamps list, and the second being the distances list
x_axis, y_axis = zip(*data)
```
-> https://www.geeksforgeeks.org/zip-in-python/

```python
# map takes in a function and list of elements, and runs the function on each element

# run datetime's UNIX timestamp -> UTC date+time conversion on x_axis data
x_axis = tuple(map(lambda x : dt.datetime.utcfromtimestamp(x).ctime(), x_axis))

# convert distances from mm to feet (final conversion factor is just dividing by 304.8
y_axis = tuple(map(lambda x : x/304.8, y_axis))
```

-> https://www.learnpython.org/en/Map,_Filter,_Reduce

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
