import sys
import matplotlib.pyplot as plt
from scipy.signal import savgol_filter
from knn import train_knn
from forest import train_forest

Timestamp = int
Distance = int
TotTagData = list[tuple[Timestamp,Distance]]
Device = str
EventLabel = int
DiaryEvent = tuple[EventLabel,Timestamp,Timestamp]


def parse_args() -> str:
    """Parses command-line arguments to ensure proper format and returns the provided filename."""
    if len(sys.argv) != 2:
        print('USAGE: python3 FILE_NAME.py LOG_FILE_PATH')
        sys.exit(1)
    log_filepath = sys.argv[1]
    return log_filepath


def load_log(log_filepath: str) -> dict[Device,TotTagData]:
    """Loads TotTag data from a specified filepath, and returns a dictionary containing the data."""
    tags = {}

    with open(log_filepath) as f:
        for line in f:

            if line[0] == '#':
                continue

            timestamp, tag_id, distance = line.split()

            if int(distance) == 999999:
                continue

            timestamp = int(timestamp)
            tag_id = tag_id.split(':')[-1]
            distance = int(distance)
            #distance = int(round(distance / 100))

            if tag_id not in tags:
                tags[tag_id] = []
            tags[tag_id].append((timestamp,distance))

    return tags


def fill_buf(data: TotTagData, buf_size: int, start: int) -> tuple[TotTagData,int]:
    """Fills buffer with first valid window starting at index 'start', and returns a tuple containing the filled buffer and actual start index."""
    old_start = start
    buf = []
    try:
        while len(buf) < buf_size:
            for i in range(0, buf_size):
                buf.append(data[start+i])
                if len(buf) > 1 and buf[i-1][0] != buf[i][0] - 1:
                    start += i
                    buf.clear()
                    break
        return (buf, start)

    except IndexError:
        print("Could not find valid window of length", buf_size, "starting at", old_start, "->", start)
        print("This should NOT show up; notify Colin if it does and send the log file you ran this script on.")


def generate_sliding_windows(data: dict[Device, TotTagData], tag: str, window_length: int, window_shift: int) -> list[TotTagData]:
    """Generates and returns a list of windows of specified length and shift size."""
    if tag in data:
        index = 0
        windows = []

        while index + window_length - 1 < len(data[tag]):
            curr_window, index = fill_buf(data[tag], window_length, index)
            index += window_shift
            windows.append(curr_window)

        return windows


def label_events(windows: list[TotTagData], diary: list[DiaryEvent], event_labels: list[EventLabel]) -> list[EventLabel]:
    """Given windows of timeseries data, labels each one by the most common event during that period, and returns the list of labels."""
    labels = []

    for window in windows:
        voting = []
        for event in event_labels:
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


def print_window_times_and_labels(windows: list[TotTagData]) -> None:
    """Prints the start and end timestamps and event label of each window in the list."""
    for i in range(len(windows)):
        print("Event" + str(i) + ":", windows[i][0][0], windows[i][-1][0], labels[i])


def plot(tags: dict[Device, TotTagData]):
    """Plots the provided TotTag data in both its original form and an experimental smoothed form."""
    for tag, data in tags.items():
        print("Plotting data for", tag)
        x_axis, y_axis = zip(*data)

        plt.plot(x_axis, y_axis, 'c', label='Original')
        plt.title('TotTag data for device ' + tag)
        plt.xlabel('Timestamp')
        plt.ylabel('Distance in mm')

        smoothed_y_axis = savgol_filter(y_axis, window_length=9, polyorder=3)

        plt.plot(x_axis, smoothed_y_axis, 'k', label='Smoothed')
        # plt.title('Savitzky-Golay Filtered TotTag data for device ' + tag)
        # plt.xlabel('Timestamp')
        # plt.ylabel('Distance in mm')

        plt.legend(['Original', 'Smoothed'])
        plt.show()


if __name__ == "__main__":
    # Test the code here!

    log_filepath = parse_args()
    tags = load_log(log_filepath)

    windows = generate_sliding_windows(tags, '41', 30, 30)

    diary = [
        # (event_label, start_time, end_time)
        (0, 0, 120),
        (1, 120, 240),
        (0, 240, 550)
    ]

    event_labels = [0, 1]

    labels = label_events(windows, diary, event_labels)

    print_window_times_and_labels(windows)
    
    stripped_windows = strip_timestamps(windows)

    train_knn(stripped_windows, labels)
    train_forest(stripped_windows, labels)



# Misc. Testing Code (IGNORE ALL BELOW)

# def demo_sliding_window(tags: dict):
#     """Divides the provided data into windows and plots each window in an alternating pattern, with vertical bars marking start/ends of windows."""
#     windows = generate_sliding_windows(tags, '41', 30, 30)
    
#     half1 = windows[::2]
#     half2 = windows[1::2]
#     data1 = []
#     data2 = []
#     for window in half1:
#         data1.extend(window)
#     for window in half2:
#         data2.extend(window)
    
#     x_1, y_1 = zip(*data1)
#     x_2, y_2 = zip(*data2)

#     plt.title('TotTag sliding window demo')
#     plt.xlabel('Timestamp')
#     plt.ylabel('Distance in mm')
#     plt.scatter(x_1, y_1)

#     plt.scatter(x_2, y_2)

#     for i in range(windows[0][0][0], windows[-1][-1][0], 30):
#         plt.axvline(x=i)

#     plt.show()
