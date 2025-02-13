"""
Function: This script finds all .pkl files in a specified directory, extracts the subject_id from the filename,
loads the corresponding data, and processes it using the get_daily_ranging_statistics function.

Usage:
cd SociTrack/software/management/dashboard
python3 1_tottags_batch.py
"""

import re
import csv

# Define input and output file paths
input_file = "/Users/hannahpiersiak/Desktop/tottags/ranging_statistics.txt"
output_file = "/Users/hannahpiersiak/Desktop/tottags/ranging_statistics.csv"

# Open the input file and read its contents
with open(input_file, "r") as file:
    data = file.readlines()

# Prepare data extraction
csv_data = []
current_date = None

# Regex patterns to extract data
date_pattern = re.compile(r"Ranging Statistics on (\d{2}/\d{2}/\d{4}):")
stats_pattern = re.compile(r"Statistics to (r\.\d+_CG\d|Either of \[.*\]):")
value_pattern = re.compile(r"^\s+(\w.+):\s+([\d.]+|nan)")

# Parse the file line by line
for line in data:
    date_match = date_pattern.search(line)
    stats_match = stats_pattern.search(line)
    value_match = value_pattern.search(line)

    if date_match:
        current_date = date_match.group(1)  # Extract the date
    elif stats_match:
        current_target = stats_match.group(1)  # Extract the target (CG1, CG2, or Either)
        minutes_range = minutes_touch = mean_distance = None
    elif value_match:
        key, value = value_match.groups()
        if key == "Minutes in Range":
            minutes_range = value
        elif key == "Minutes in Touching Distance":
            minutes_touch = value
        elif key == "Mean Distance While in Range":
            mean_distance = value
            # Save row when all values for a section are collected
            csv_data.append([current_date, current_target, minutes_range, minutes_touch, mean_distance])

# Write to CSV file
with open(output_file, "w", newline="") as file:
    writer = csv.writer(file)
    writer.writerow(["Date", "Target", "Minutes in Range", "Minutes in Touching Distance", "Mean Distance While in Range"])
    writer.writerows(csv_data)

print(f"CSV file saved: {output_file}")

