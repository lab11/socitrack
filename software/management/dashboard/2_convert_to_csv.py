import csv
import re

# Input and output file paths
input_file = "/Users/hannahpiersiak/Desktop/tottags/ranging_statistics.txt"
output_file = "/Users/hannahpiersiak/Desktop/tottags/ranging_statistics.txt"

# Regular expressions to extract relevant data
date_pattern = re.compile(r"Ranging Statistics on (\d{2}/\d{2}/\d{4}):")
entity_pattern = re.compile(r"Statistics to (\S+):")
minutes_range_pattern = re.compile(r"Minutes in Range: (\d+)")
minutes_touch_pattern = re.compile(r"Minutes in Touching Distance: (\d+)")
mean_distance_pattern = re.compile(r"Mean Distance While in Range: (.+)")

# Storage variables
data = []
current_date = None
current_entity = None

# Read and parse the text file
with open(input_file, "r") as infile:
    for line in infile:
        line = line.strip()
        
        # Extract date
        date_match = date_pattern.match(line)
        if date_match:
            current_date = date_match.group(1)
            continue
        
        # Extract entity
        entity_match = entity_pattern.match(line)
        if entity_match:
            current_entity = entity_match.group(1)
            continue
        
        # Extract statistics
        minutes_range_match = minutes_range_pattern.match(line)
        minutes_touch_match = minutes_touch_pattern.match(line)
        mean_distance_match = mean_distance_pattern.match(line)
        
        if minutes_range_match:
            minutes_range = int(minutes_range_match.group(1))
        if minutes_touch_match:
            minutes_touch = int(minutes_touch_match.group(1))
        if mean_distance_match:
            mean_distance = mean_distance_match.group(1)  # Keep as string for 'nan' handling

            # Save extracted data
            data.append([current_date, current_entity, minutes_range, minutes_touch, mean_distance])

# Write to CSV
with open(output_file, "w", newline="") as outfile:
    writer = csv.writer(outfile)
    writer.writerow(["Date", "Entity", "Minutes in Range", "Minutes in Touching Distance", "Mean Distance While in Range"])
    writer.writerows(data)

print(f"Data successfully written to {output_file}")

