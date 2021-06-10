import sys
import matplotlib.pyplot as plt
# from scipy.signal import savgol_filter
# from sklearn.ensemble import VotingClassifier
from sklearn.metrics import accuracy_score
from sklearn.metrics import f1_score
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


def interleave(tags: dict[Device, TotTagData]):
    for device in tags:
        data = tags[device]
        print("before:", len(data))
        for i in range(len(data)):
            curr_time = data[i][0]
            last_time = data[i-1][0]
            curr_dist = data[i][1]
            last_dist = data[i-1][1]
            if curr_time - 2 == last_time:
                new_dist = (curr_dist + last_dist) // 2
                new_time = curr_time - 1
                data.insert(i, (new_time, new_dist))
                i += 1

        for i in range(1, len(data)):
            assert data[i][0] > data[i-1][0]
        print("after: ", len(data))


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

    # for i in range(windows[0][0][0], windows[-1][-1][0], 30):
        # plt.axvline(x=i)

    plt.show()


def graph_labeling(windows, window_labels, reverse_event_map, window_setting=-1):

    offset = 0
    # if window_setting == 2:
    #     offset = 0
    # else:
    #     offset = 30

    # print(type(windows[0]))

    bins = {} 
    for i in range(len(windows)):
        if window_labels[i] not in bins: 
            bins[window_labels[i]] = windows[i]
        else:
            last_time = bins[window_labels[i]][-1][0]
            next_time = windows[i][0][0]
            bins[window_labels[i]].extend([(i, np.nan) for i in range(last_time + 1, next_time)])
            bins[window_labels[i]].extend(windows[i])

    if window_setting != -1:
        plt.title(f'TotTag data labeling (window size {window_setting})')
    else:
        plt.title('TotTag data predictions')
    plt.xlabel('Timestamp (s)')
    plt.ylabel('Distance (ft)')

    for data_label, data in bins.items():
        x_1, y_1 = zip(*data)
        x_1 = tuple(map(lambda x : dt.datetime.utcfromtimestamp(x).ctime(), x_1)) # consider removing date
        y_1 = tuple(map(lambda x : x/304.8 + offset, y_1))
        plt.plot(x_1, y_1, label=reverse_event_map[data_label], lw=4)

    plt.legend()
    locs, _ = plt.xticks()
    plt.xticks(tuple(filter(lambda x: locs.index(x)%300 == 0, locs)))
    plt.show()


def do_model_stuff(window_setting, train_log_filepath, train_diary_filepath, test_log_filepath, test_diary_filepath):
    # Load log and diary files
    tags = load_log(train_log_filepath)
    
    # interleave(tags)

    # exit()
    diary, event_map = load_diary(train_diary_filepath)
    test_tags = load_log(test_log_filepath)
    test_diary = load_testdiary(test_diary_filepath, event_map)

    # Create sliding windows
    window_length = window_setting
    window_shift = 1
    windows      = generate_sliding_windows(tags, target_tag, window_length, window_shift)
    test_windows = generate_sliding_windows(test_tags, target_tag, window_length, window_shift)

    # demo_sliding_window(windows)

    # Label the windows
    labels      = label_events(windows,      diary,      event_map.values())
    test_labels = label_events(test_windows, test_diary, event_map.values())

    # Print a summary for each window we've labeled (debug step; unnecessary)
    reverse_event_map = {v: k for k, v in event_map.items()}
    print("="*70)
    # print_window_times_and_labels(windows, labels, reverse_event_map)
    # print_window_times_and_labels(testwindows, testlabels, reverse_event_map, "test")
    
    filtered_windows,      filtered_labels      = zip(*tuple(filter(lambda x: reverse_event_map[x[1]] != 'Undefined', tuple(zip(windows,      labels)))))
    filtered_test_windows, filtered_test_labels = zip(*tuple(filter(lambda x: reverse_event_map[x[1]] != 'Undefined', tuple(zip(test_windows, test_labels)))))

    # filtered_windows, filtered_labels = windows, labels
    # filtered_test_windows, filtered_test_labels = test_windows, test_labels
    
    # Remove timestamps from windows; they are in order anyways, and would affect training if included
    stripped_windows     = strip_timestamps(filtered_windows)
    stripped_test_windows = strip_timestamps(filtered_test_windows)

    # Train models on the processed data
    knn_model    = train_knn(stripped_windows, filtered_labels)
    forest_model = train_forest(stripped_windows, filtered_labels)

    knn_preds = evaluate(knn_model,    stripped_test_windows, filtered_test_labels, window_setting)
    rf_preds  = evaluate(forest_model, stripped_test_windows, filtered_test_labels, window_setting)

    # label: [(x1, y1), ...], ...
    # -> label (x1, y1), label (x2, y2), ...

    flattened_knn_preds = []
    for pred in knn_preds:
        for _ in range(window_setting):
            flattened_knn_preds.append(pred)
    
    flattened_test_labels = []
    for label in filtered_test_labels:
        for _ in range(window_setting):
            flattened_test_labels.append(label)

    flattened_rf_preds = []
    for pred in rf_preds:
        for _ in range(window_setting):
            flattened_rf_preds.append(pred)

    flattened_test_windows = []
    for window in filtered_test_windows:
        flattened_test_windows.extend(window)

    print("="*70)

    # Plot the windows, allowing user to compare labels with those printed by debug step 
    # plot(tags)

    graph_labeling(windows, labels, reverse_event_map, window_setting)
    
    # return (knn_preds, rf_preds)
    return flattened_knn_preds, flattened_rf_preds, flattened_test_windows, flattened_test_labels, reverse_event_map


if __name__ == "__main__":
    ### Test the code here! :)

    print("\n"*5)

    ## IMPORTANT!! ##
    target_tag = '51'
    size1 = 2
    size2 = 2

    # Load command-line arguments
    train_log_filepath, train_diary_filepath, test_log_filepath, test_diary_filepath = parse_args() 

    # flattened knn predictions = fkp
    # flattened random-forest predictions = frp
    # flattened test windows = ftw
    # number = window size
    flat_knnsize1, flat_rfsize1, flat_test_windowssize1, flat_test_labels_size1, reverse_event_mapsize1 = do_model_stuff(size1, train_log_filepath, train_diary_filepath, test_log_filepath, test_diary_filepath)
    flat_knnsize2, flat_rfsize2, flat_test_windowssize2, flat_test_labels_size2, reverse_event_mapsize2 = do_model_stuff(size2, train_log_filepath, train_diary_filepath, test_log_filepath, test_diary_filepath)

    # for i in range (3,30):
        # do_model_stuff(i, train_log_filepath, train_diary_filepath, test_log_filepath, test_diary_filepath)

    # print(len(flat_knnsize1), len(flat_testwindowssize1))

    # print("Window size size1 KNN:", len(flat_knnsize1))
    # print("Window size size2 KNN:", len(flat_knnsize2))
    # print("Window size size1  RF:", len(flat_knnsize1))
    # print("Window size size2  RF:", len(flat_knnsize2))

    labelstuff = {}   

    pred = 0
    real = 1

    window_size = size1
    for i in range(len(flat_test_windowssize1)):

        timestamp = flat_test_windowssize1[i][0]
        if timestamp not in labelstuff:
            labelstuff[timestamp] = {}
        
        if window_size not in labelstuff[timestamp]:
            labelstuff[timestamp][window_size] = {}
            labelstuff[timestamp][window_size][pred] = []
            labelstuff[timestamp][window_size][real] = []

        labelstuff[timestamp][window_size][pred].append(flat_knnsize1[i])
        labelstuff[timestamp][window_size][real].append(flat_test_labels_size1[i])

    window_size = size2
    for i in range(len(flat_test_windowssize2)):

        timestamp = flat_test_windowssize2[i][0]
        if timestamp not in labelstuff:
            labelstuff[timestamp] = {}
        
        if window_size not in labelstuff[timestamp]:
            labelstuff[timestamp][window_size] = {}
            labelstuff[timestamp][window_size][pred] = []
            labelstuff[timestamp][window_size][real] = []

        labelstuff[timestamp][window_size][pred].append(flat_knnsize2[i])
        labelstuff[timestamp][window_size][real].append(flat_test_labels_size2[i])

    # print(labelstuff)

    # { time1:
    #     {
    #         window_size_1: [label1, label2, ...],
    #         window_size_2: [label1, label2, ...],
    #         ...
    #     } ,
    #   time2:
    #     {
    #         ...
    #     },
    #   ...
    # }

    # window size 2 and 5
    # Do we have a label from model_2 and/or model_5?
    # Case 1: 2 yes, 5 no   : just pick what exists
    # Case 2: 2 no,  5 yes  : ^^^
    # Case 3: both have some information: prefer labels from model_2

    # Streamlined cases:
    # Case 1: 2 no, 5 yes: pick from 5
    # Case 2: 2 yes, 5 x : pick from 2

    # within each case, it's possible that we have multiple labels for one point. 
    # therefore, we take the most common label from model_x 

    flat_test_windowssize1.extend(flat_test_windowssize2)
    all_data = flat_test_windowssize1

    time_to_dist = {}
    for time, dist in all_data:
        time_to_dist[time] = dist

    
    # timestamp = 1614991856

    from collections import Counter

    final_output_pred = {}
    final_output_real = {}

    for timestamp in labelstuff:
        if size1 not in labelstuff[timestamp]: # case 1
            # pick from size1
            # pick most common label?
            all_labels_pred = labelstuff[timestamp][size2][pred]
            all_labels_real = labelstuff[timestamp][size2][real]
            labelcounts_pred = Counter(all_labels_pred)
            labelcounts_real = Counter(all_labels_real)
            picked_label_pred = labelcounts_pred.most_common(1)[0][0]
            picked_label_real = labelcounts_real.most_common(1)[0][0]
        # elif size1 not in labelstuff[timestamp]: # case size1
        #     # pick from 5
        #     # pick most common label?
        #     all_labels = labelstuff[timestamp][5]
        #     labelcounts = Counter(all_labels)
        #     picked_label = labelcounts.most_common(1)[0][0]
        else:
            # pick from size1, because we prefer size1 over 5
            # pick most common label from model_size1
            all_labels_pred = labelstuff[timestamp][size1][pred]
            all_labels_real = labelstuff[timestamp][size1][real]
            labelcounts_pred = Counter(all_labels_pred)
            labelcounts_real = Counter(all_labels_real)
            picked_label_pred = labelcounts_pred.most_common(1)[0][0]
            picked_label_real = labelcounts_real.most_common(1)[0][0]
        final_output_pred[(timestamp, time_to_dist[timestamp])] = picked_label_pred
        final_output_real[(timestamp, time_to_dist[timestamp])] = picked_label_real

    data, labels = zip(*final_output_pred.items())
    _, real_labels = zip(*final_output_real.items())


    model_string = 'KNeighborsClassifier'
    y_test = real_labels
    y_pred = labels
    print(f"{model_string} Accuracy, Double Model: {accuracy_score(y_test, y_pred) * 100}%") 
    print(f"{model_string} Macro F1, Double Model: {f1_score(y_test, y_pred, average='macro')}")

    # print("\n"*20)
    # print(data)
    data = tuple(map(lambda x: [x], data))
    # print("\n"*20)
    # print(data)
    # print("\n"*20)

    graph_labeling(data, labels, reverse_event_mapsize1)

    # { time_1: 
    #       (dist_1, label_1),
    #   time_2:
    #       (dist_2, label_2),
    # ...
    # }

    # [ (time_1, dist_1), (time_2, dist_2), ...]
    # [ label_1, label_2, ...]


    





















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
