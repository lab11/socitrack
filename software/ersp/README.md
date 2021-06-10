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

### Prerequisites 

Before you run the code and look into anything, these scripts rely on several Python libraries, which can be installed by the terminal input `pip3 install -r requirements.txt.`

There appear to be some issues running on Python 3.8 and below; if you follow all the other steps here correctly and still cannot get the scripts to work, try updating to Python 3.9+.

The Python libraries used are contained within the text of this document requirements.txt. There are quite a few libraries to look into in this document, so the description of each is listed below:

Python Libraries used:

Asteroid: https://pypi.org/project/astroid/
Powering pylint’s capabilities

Cycler: https://pypi.org/project/Cycler/
Cycling of colors in the data visualizations

isort: https://pypi.org/project/isort/
Sorts imports

Joblib: https://pypi.org/project/joblib/#downloads
Provides tools that help achieve better performance

Kiwisolver: https://pypi.org/project/kiwisolver/
Constraint solving algorithm

Lazy-object-proxy: https://pypi.org/project/lazy-object-proxy/
Fast and thorough lazy object proxy

Matplotlib: https://pypi.org/project/matplotlib/
Creating data visualizations 

Mccabe: https://pypi.org/project/mccabe/
Checks mccabe complexity 

NumPy: https://numpy.org
Scientific computing

Pillow: https://pypi.org/project/Pillow/
Adds image processing abilities

Pycryptodomex:  https://pypi.org/project/pycryptodomex/
Low-level cryptographic primitives

Pylint: https://pypi.org/project/pylint/
Code analysis tool that helps look for errors and give suggestions

Pyparsing: https://pypi.org/project/pyparsing/
Creates simple grammars.

Python-dateutil: https://pypi.org/project/python-dateutil/
Extensions to datetime module

Scikit-learn: https://pypi.org/project/scikit-learn/
Module for machine learning that integrates other scientific python packages

Scipy: https://pypi.org/project/scipy/
Manipulation of numbers (can use w numpy!!)

Six: https://pypi.org/project/six/
Smooths the differences between Python 2&3

Threadpoolctl: https://pypi.org/project/threadpoolctl/
Limits the number of threads coming from other libraries

Toml: https://pypi.org/project/toml/
Parses and creates TOML files

Wrapt: https://pypi.org/project/wrapt/
Function wrappers and decorator functions

### Usage

There are two processes at which you can run with this code: plotting and processing.

### Plotting

To simply plot a log file, run `python3 plot.py LOG_FILE_PATH` in your terminal. This will open a PyPlot window displaying the raw log data overlayed by a smoothed version. 

![Example plot](https://www.dropbox.com/s/8m98i1jxuozu928/Plot%20Example.png?raw=1)

### Classifying

##### Data Collection

When collecting data, two things are required in order to run the model for classification: the TotTag log containing the experiment's data, and a human-recorded diary of the interactions that occurred during the experiment, with UNIX timestamps corresponding to the TotTag logs' timestamps. In order to correctly run the script, these two files are necessary.

##### Log File

Simple use the log file you intend to run on the algorithm corresponding to the interactions you want to train on the model. This log will be corresponding to the diary you labeled for the interactions recorded.

##### Diary

Supervised machine learning requires labeled data, so once you've collected a TotTag log, your next task is to manually label it. You can either edit a diary via raw CSV, or use our Excel template. In the [diaries directory](diaries/), there is an [Excel workbook containing a diary template](diaries/Template-Diary.xlsx), which contains a few useful features.

![Excel Template](https://www.dropbox.com/s/uua55mvdijc6vd6/Excel%20Template.png?raw=1)

The script ignores any lines beginning with the '#' sign, which is why row begins as such. As you may observe from Row 1, Column A is for event labels, Column B is for start timestamps, Column C is for end timestmaps, and Columns D and E are readable times, calculated via formulas from B and C. Columns A, B, and C are the ones that you need to manipulate; the only thing you need to do with D and E are to extend the formulas down as you fill in the diary.

It may be useful to cross-reference the plotted data and the window-labeling output in the console, to ensure windows are being reasonably labeled. Additionally, you may find it useful to plot your logs before recording your diary to help pinpoint event start and end timestamps.

##### Classifying

To train the classifiers with an experiment you've run, assuming you've collected a TotTag log and recorded a diary labeling each event that took place, run `python3 tottagProcessing.py TRAIN_LOG_FILE_PATH TRAIN_DIARY_FILE_PATH TEST_LOG_FILE_PATH TEST_DIARY_FILE_PATH` in the terminal. 

We have two logs and two diaries used to gather a better insight on our algorithm's performance, by using one log/diary pair for training the model, and another log/diary pair for testing the model's performance. Ideally, you can use the same LOG_FILE you originally wanted to run, but split the diary to a 70/30 split of training and testing data to get a better understanding of how accurate the algorithm is. You can simply do this by taking the first 70% of the interactions recorded in the diary for “train_diary_filepath” and taking the rest of the 30% into another diary file as “test_diary_filepath”. You can use the same log for train_log_filepath and test_log_filepath if the diaries used refer to the same log.

Thus, after running the script, it will print out a summary of the window sizes used for sliding windows in data parsing and the accuracy of each model: the single model window size output of each K-Nearest Neighbor classifier, the combined output of running two differing window sizes on K-Nearest Neighbor, and the output of each window size for Random Forest classifier. After training the classifiers with the dataset and printing the accuracy of each classifier, it will finally plot the log based on the interactions predicted for each time slot on the data.
