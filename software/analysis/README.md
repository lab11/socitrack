Home for analysis scrips.

To use:
First, run the tottagAverager script. This script takes at least 3 arguments, which are the start and end time value (in epoch time), and then every data file you want averaged together. The data is averaged accross timestamps by dyad, so the measured value at a certain time stamp from one tottag is averaged together with the the value at the same timestamp in it's companion tottag. Example input:

python ./tottagAverager START_TIME_VAL END_TIME_VAL LOG_FILE_1 LOG_FILE_2...

Then, run the tottagSmoother script. This script takes at least 2 arguments. This first is the value you want smoothed to, followed by every data file you want smoothed. The smoother smooths by taking a moving average with a width of SMOOTHING_VAL. When it encounters a gap in the data greater than the aforementioned value, the smoothing buffer is cleared and it starts over after the gap. Example input:

python ./tottageSmoother SMOOTHING_VAL AVERAGED_LOG_FILE_1 AVERAGED_LOG_FILE_2...

Now, your data is ready to go, at which point you can run the stats script on it. The stats script, tottagStats, produces summary statistics on the smoothed log file. The stats it outputs are the amount of time each dyad spent within 3ft, amount of time in range, and amount of times a dyad re-enters 3ft after leaving it for 30s. The input for the stats script is just the single log file. Example Input:

python ./tottagStats SMOOTHED_LOG_FILE_1
