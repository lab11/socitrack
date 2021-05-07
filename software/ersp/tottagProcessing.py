import sys
import matplotlib.pyplot as plt
from scipy.signal import savgol_filter
from sklearn.ensemble import VotingClassifier
import datetime as dt
from classifiers import *
from exceptions import *

###
###     Type definitions
###
Timestamp = int
Distance = int
TotTagData = list[tuple[Timestamp,Distance]]
Device = str
EventLabel = int
DiaryEvent = tuple[EventLabel,Timestamp,Timestamp]


### 
###     File Processing
###
def parse_args() -> list[str]:
    """Parses command-line arguments to ensure proper format and returns the provided filename."""
    # Plot
    if sys.argv[0] == 'plot.py':
        if len(sys.argv) == 2:
            return [sys.argv[1]]
        else:
            print('USAGE: python3 plot.py LOG_FILE')
            sys.exit(1)

    # Train
    elif sys.argv[0] == 'tottagProcessing.py':
        if len(sys.argv) == 5:
            return sys.argv[1:5]
        else:
            print('USAGE: python3 tottagProcessing.py LOG_FILE DIARY_FILE TESTLOG_FILE TEST_DIARYFILE')
            sys.exit(1)


def load_log(log_filepath: str) -> dict[Device,TotTagData]:
    """Loads TotTag data from a specified filepath, and returns a dictionary containing the data."""
    tags = {}

    with open(log_filepath) as f:
        for line in f:

            if line[0] == '#':
                continue

            timestamp, tag_id, distance = line.split()

            if int(distance) == 999999:
                # distance = np.nan
                continue

            timestamp = int(timestamp)
            tag_id = tag_id.split(':')[-1]
            distance = int(distance)

            if tag_id not in tags:
                tags[tag_id] = []
            tags[tag_id].append((timestamp,distance))

    return tags
    

def load_diary(diary_filepath: str) -> tuple[list[DiaryEvent],dict[str,int]]:
    """Loads diary data from a specified filepath, and returns a list containing the events in the diary."""
    diary = []
    event_map = {}
    encoding_index = 1

    event_map["Undefined"] = 0

    with open(diary_filepath) as f:
        for line in f:

            if line[0] == '#':
                continue

            label_string, start, end = line.split(",")[0:3]

            if label_string not in event_map:
                event_map[label_string] = encoding_index
                encoding_index += 1

            encoded_label = event_map[label_string]

            diary.append((encoded_label, int(start), int(end)))
    
    return diary, event_map


def load_testdiary(diary_filepath: str, event_map) -> tuple[list[DiaryEvent],dict[str,int]]:
    """Loads diary data from a specified filepath, and returns a list containing the events in the diary."""
    diary = []

    with open(diary_filepath) as f:
        for line in f:

            if line[0] == '#':
                continue

            label_string, start, end = line.split(",")[0:3]

            if label_string not in event_map:
                label_string = "Undefined"

            encoded_label = event_map[label_string]

            diary.append((encoded_label, int(start), int(end)))

    return diary


###
###     Window Generation
###
def fill_buf(data: TotTagData, buf_size: int, start: int) -> tuple[TotTagData,int]:
    """Fills buffer with first valid window starting at index 'start', and returns a tuple containing the filled buffer and actual start index."""
    buf = []
    while len(buf) < buf_size:
        for i in range(0, buf_size):
            curr_index = start+i
            if curr_index < len(data):
                buf.append(data[curr_index])
                # print(len(buf), curr_index, buf[-1], i)   # uncomment for fill_buf debug
                prev_time = buf[i-1][0]
                curr_time = buf[i][0]
                if prev_time > curr_time:
                    raise ReverseTimeError('Invalid log file.', data[curr_index-3:curr_index+3], data[curr_index-1], data[curr_index])
                if len(buf) > 1 and prev_time + 1 != curr_time:
                    start += i
                    buf.clear()
                    break
            else:
                raise EOFError("Failed to fill sliding window buffer")
    return (buf, start)


def generate_sliding_windows(data: dict[Device, TotTagData], tag: str, window_length: int, window_shift: int) -> list[TotTagData]:
    """Generates and returns a list of windows of specified length and shift size."""
    if tag in data:
        index = 0
        windows = []

        while index + window_length - 1 < len(data[tag]):
            try:
                curr_window, index = fill_buf(data[tag], window_length, index)
            except EOFError:
                break
            index += window_shift
            windows.append(curr_window)

        return windows


###
###     Window Processing
###
def label_events(windows: list[TotTagData], diary: list[DiaryEvent], event_labels: list[EventLabel]) -> list[int]:
    """Given windows of timeseries data, labels each one by the most common event during that period, and returns the list of labels."""
    labels = []

    for window in windows:
        voting = []
        for _ in event_labels:
            voting.append(0)
        
        for event in diary:
            start_event = max(event[1], window[0][0])
            end_event = min(event[2], window[-1][0])
            event_length = end_event - start_event

            if event_length < 0:
                continue
            
            window_length = window[-1][0] - window[0][0]

            voting[event[0]] += event_length / window_length
        
        label = voting.index(max(voting))

        labels.append(label)

    return labels


def strip_timestamps(windows: list[TotTagData]) -> list[list[Distance]]:
    """Removes timestamps from a list of windows, priming it for the model, and returns the list of stripped windows."""
    stripped_windows = []

    for window in windows:
        strip_win = []
        for tup in window:
            strip_win.append(tup[1])
        stripped_windows.append(strip_win)

    return stripped_windows


###
###     Miscellaneous
###
def print_window_times_and_labels(windows: list[TotTagData], labels: list[EventLabel], reverse_event_map: dict[int,str], mode="train") -> None:
    """Prints the start and end timestamps and event label of each window in the list."""
    if mode == "train":
        print("Training Data:\t|\tStart\t End\t Label")
    elif mode == "test":
        print("Testing Data:\t|\tStart\t End\t Label")
        
    print("="*50)
    for i in range(len(windows)):
        print(f"Window {i+1}:\t|\t{windows[i][0][0]} \t {windows[i][-1][0]} \t {reverse_event_map[labels[i]]}")
    print("="*50)


def plot(tags: dict[Device, TotTagData]) -> None:
    """Plots the provided TotTag data in both its original form and an experimental smoothed form."""
    for tag, data in tags.items():
        print("Plotting data for", tag)
        x_axis, y_axis = zip(*data)

        x_axis = tuple(map(lambda x : dt.datetime.utcfromtimestamp(x).ctime(), x_axis))
        y_axis = tuple(map(lambda x : x/304.8, y_axis)) 

        plt.plot(x_axis, y_axis)#, 'c', label='Original')
        plt.xticks(tuple(filter(lambda x: x_axis.index(x)%100 == 0, x_axis)))
        plt.title('TotTag data for device ' + tag)
        plt.xlabel('Timestamp')
        plt.ylabel('Distance in ft')

        # smoothed_y_axis = savgol_filter(y_axis, window_length=9, polyorder=3)

        # plt.plot(x_axis, smoothed_y_axis, 'k', label='Smoothed')
        # plt.title('Savitzky-Golay Filtered TotTag data for device ' + tag)
        # plt.xlabel('Timestamp')
        # plt.ylabel('Distance in mm')

        #plt.legend(['Original', 'Smoothed'])
        plt.show()


def demo_sliding_window(windows: list[TotTagData]) -> None:
    """Divides the provided data into windows and plots each window in an alternating pattern, with vertical bars marking start/ends of windows."""    
    half1 = windows[::2]
    half2 = windows[1::2]
    data1 = []
    data2 = []
    for window in half1:
        data1.extend(window)
    for window in half2:
        data2.extend(window)
    
    x_1, y_1 = zip(*data1)
    x_2, y_2 = zip(*data2)

    plt.title('TotTag sliding window demo')
    plt.xlabel('Timestamp')
    plt.ylabel('Distance in mm')
    plt.scatter(x_1, y_1)

    plt.scatter(x_2, y_2)

    for i in range(windows[0][0][0], windows[-1][-1][0], 30):
        plt.axvline(x=i)

    plt.show()


def graph_labeling(windows, window_labels, reverse_event_map, window_setting):

    offset = 0
    # if window_setting == 2:
    #     offset = 0
    # else:
    #     offset = 30

    bins = {} 
    for i in range(len(windows)):
        if window_labels[i] not in bins: 
            bins[window_labels[i]] = windows[i]
        else:
            last_time = bins[window_labels[i]][-1][0]
            next_time = windows[i][0][0]
            bins[window_labels[i]].extend([(i, np.nan) for i in range(last_time + 1, next_time)])
            bins[window_labels[i]].extend(windows[i])

    plt.title(f'TotTag training data labeling (window size {window_setting})')
    plt.xlabel('Timestamp (s)')
    plt.ylabel('Distance (ft)')

    for data_label, data in bins.items():
        x_1, y_1 = zip(*data)
        # x_1 = tuple(map(lambda x : dt.datetime.utcfromtimestamp(x).ctime(), x_1))
        y_1 = tuple(map(lambda x : x/304.8 + offset, y_1))
        plt.plot(x_1, y_1, label=reverse_event_map[data_label])

    plt.legend()
    # locs, _ = plt.xticks()
    # plt.xticks(tuple(filter(lambda x: locs.index(x)%300 == 0, locs)))
    plt.show()


def do_model_stuff(window_setting, train_log_filepath, train_diary_filepath, test_log_filepath, test_diary_filepath):
    # Load log and diary files
    tags = load_log(train_log_filepath)
    diary, event_map = load_diary(train_diary_filepath)
    testtags = load_log(test_log_filepath)
    testdiary = load_testdiary(test_diary_filepath, event_map)

    # Create sliding windows
    window_length = window_setting
    window_shift = 1
    windows     = generate_sliding_windows(tags, target_tag, window_length, window_shift)
    testwindows = generate_sliding_windows(testtags, target_tag, window_length, window_shift)

    # Label the windows
    labels     = label_events(windows,     diary,     event_map.values())
    testlabels = label_events(testwindows, testdiary, event_map.values())

    # Print a summary for each window we've labeled (debug step; unnecessary)
    reverse_event_map = {v: k for k, v in event_map.items()}
    print("="*70)
    # print_window_times_and_labels(windows, labels, reverse_event_map)
    # print_window_times_and_labels(testwindows, testlabels, reverse_event_map, "test")
    
    # Remove timestamps from windows; they are in order anyways, and would affect training if included
    stripped_windows     = strip_timestamps(windows)
    stripped_testwindows = strip_timestamps(testwindows)

    filtered_windows,      filtered_labels      = zip(*tuple(filter(lambda x: reverse_event_map[x[1]] != 'Undefined', tuple(zip(stripped_windows,     labels)))))
    filtered_test_windows, filtered_test_labels = zip(*tuple(filter(lambda x: reverse_event_map[x[1]] != 'Undefined', tuple(zip(stripped_testwindows, testlabels)))))

    # Train models on the processed data
    knn_model    = train_knn(filtered_windows, filtered_labels)
    forest_model = train_forest(filtered_windows, filtered_labels)

    knn_preds = evaluate(knn_model,    filtered_test_windows, filtered_test_labels, window_setting)
    rf_preds  = evaluate(forest_model, filtered_test_windows, filtered_test_labels, window_setting)

    # label: [(x1, y1), ...], ...
    # -> label (x1, y1), label (x2, y2), ...

    flattened_knn_preds = []
    for pred in knn_preds:
        for _ in range(window_setting):
            flattened_knn_preds.append(pred)

    flattened_rf_preds = []
    for pred in rf_preds:
        for _ in range(window_setting):
            flattened_rf_preds.append(pred)

    flattened_testwindows = []
    for window in testwindows:
        flattened_testwindows.extend(window)

    print("="*70)

    # Plot the windows, allowing user to compare labels with those printed by debug step 
    # plot(tags)

    #graph_labeling(windows, labels, reverse_event_map, window_setting)
    
    # return (knn_preds, rf_preds)
    return flattened_knn_preds, flattened_rf_preds, flattened_testwindows


if __name__ == "__main__":
    ### Test the code here! :)

    ## IMPORTANT!! ##
    target_tag = '51'

    # Load command-line arguments
    train_log_filepath, train_diary_filepath, test_log_filepath, test_diary_filepath = parse_args() 

    fkp2, frp2, ftw2 = do_model_stuff(2, train_log_filepath, train_diary_filepath, test_log_filepath, test_diary_filepath)
    fkp5, frp5, ftw5 = do_model_stuff(5, train_log_filepath, train_diary_filepath, test_log_filepath, test_diary_filepath)

    print("Window size 2 KNN:", len(fkp2))
    print("Window size 5 KNN:", len(fkp5))
    print("Window size 2  RF:", len(fkp2))
    print("Window size 5  RF:", len(fkp5))

    

    # for i in range(2,20):
        # do_model_stuff(i, train_log_filepath, train_diary_filepath, test_log_filepath, test_diary_filepath)

    # do_model_stuff(2, train_log_filepath, train_diary_filepath, test_log_filepath, test_diary_filepath)
    # do_model_stuff(5, train_log_filepath, train_diary_filepath, test_log_filepath, test_diary_filepath)

    # # Load log and diary files
    # tags = load_log(train_log_filepath)
    # diary, event_map = load_diary(train_diary_filepath)
    # testtags = load_log(test_log_filepath)
    # testdiary = load_testdiary(test_diary_file, event_map)

    # # Create sliding windows
    # window_length = 2
    # window_shift = 1
    # windows = generate_sliding_windows(tags, target_tag, window_length, window_shift)
    # testwindows = generate_sliding_windows(testtags, target_tag, window_length, window_shift)

    # # Label the windows
    # labels = label_events(windows, diary, event_map.values())
    # testlabels = label_events(testwindows, testdiary, event_map.values())

    # # Print a summary for each window we've labeled (debug step; unnecessary)
    # reverse_event_map = {v: k for k, v in event_map.items()}
    # print("="*50)
    # print_window_times_and_labels(windows, labels, reverse_event_map)
    # print_window_times_and_labels(testwindows, testlabels, reverse_event_map, "test")
    
    # # Remove timestamps from windows; they are in order anyways, and would affect training if included
    # stripped_windows = strip_timestamps(windows)
    # stripped_testwindows = strip_timestamps(testwindows)

    # # Train models on the processed data
    # knn_model = train_knn(stripped_windows, labels)
    # forest_model = train_forest(stripped_windows, labels)

    # evaluate(knn_model, stripped_testwindows, testlabels)
    # evaluate(forest_model, stripped_testwindows, testlabels)

    # print("="*50)

    # # Plot the windows, allowing user to compare labels with those printed by debug step 
    # # plot(tags)

    # graph_labeling(windows, labels, reverse_event_map)
