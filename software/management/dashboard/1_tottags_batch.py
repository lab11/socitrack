"""
Function: This script finds all .pkl files in a specified directory, extracts the subject_id from the filename,
loads the corresponding data, and processes it using the get_daily_ranging_statistics function.

Usage:
cd SociTrack/software/management/dashboard
python3 1_tottags_batch.py
"""

import os
from processing import *


def process_pkl_files(directory):
    """Find all .pkl files in the directory, extract subject_id, load data, and process it."""
    for filename in os.listdir(directory):
        if filename.endswith(".pkl"):
            filepath = os.path.join(directory, filename)

            # Extract subject_id from filename
            subject_id = filename.split("_")[0]

            # Load pkl data
            TC = load_data(filepath)

            # Call the function with formatted parameters
            get_daily_ranging_statistics(TC, [f"{subject_id}_CG1", f"{subject_id}_CG2"], 3)


# Define directory path
directory_path = "/Users/hannahpiersiak/Desktop/tottags"

# Run the function
process_pkl_files(directory_path)
