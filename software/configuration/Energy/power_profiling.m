% Author: Andreas Biri
% Date:   05.04.2019
clear all;

% read in RLD-file
data_raw = rld('20190405_totternary_profiling.rld');

% Correct data for sign
data = data_raw;

for i = 1:1
    data.channels(i).values = (-1) * data_raw.channels(i).values;
end

% Median filter
measurements_dim = size(data.channels(1).values);
nr_measurements = measurements_dim(1)
channel_current = 1;
filter_width = 3;
median_values = zeros(nr_measurements,1);

for i = 1:nr_measurements
    median_values(i) = (-1)* median(data_raw.channels(channel_current).values(max(1,i-(filter_width-1)/2):min(i+(filter_width-1)/2, nr_measurements)));
end

% show all sampled channels
%channel_cell = data.get_channels();

% plot all analog channels
%data.plot('currents');
%data.plot('voltages');

% selective plots
%data.plot({'V3', 'I1H'});

% merge and plot current channels
%merged_data = data.merge_channels();
%merged_data.plot('currents');

% plot channels with absolute time (may take a while)
%data.plot('all', 1);

% extract channel data (as matrix) for further processing
values = data.get_data({'V3', 'I1H'});

% extract absolute timestamp-array
timestamps = data.get_time(1);

% Plot with indices
plot(median_values);

% Baseline
baseline_start = 1825000;
baseline_end   = 1847500;
baseline_avg = mean(values(baseline_start:baseline_end,2));

base_active_start = 1857450;
base_active_end   = 1857950;
base_active_avg = mean(values(base_active_start:base_active_end,2));

% BLE
ble_0_start = 1821055;
ble_0_end   = 1821350;
ble_0_avg = mean(values(ble_0_start:ble_0_end,2)) - baseline_avg;

ble_1_start = 2013520;
ble_1_end   = 2013852;
ble_1_avg = mean(values(ble_1_start:ble_1_end,2)) - baseline_avg;

ble_scan_start = 1552190;
ble_scan_end   = 1584090;
ble_scan_avg = mean(values(ble_scan_start:ble_scan_end,2)) - baseline_avg;

% UWB
overhead_start = 1852850 - 3*640;
overhead_end   = 1852850;
overhead_avg   = mean(values(overhead_start:overhead_end,2));

sched_start = 1852850;
sched_end   = sched_start + 1*640;
sched_avg_pure = mean(values(sched_start:sched_end,2));
sched_avg      = sched_avg_pure + 3*(overhead_avg - baseline_avg);

rx_start    = sched_start + 1*640;
rx_end      = sched_start + 8*640;
rx_avg = mean(values(rx_start:rx_end,2));

rx_single_start = 1857000;
rx_single_end   = 1857000 + 1*64;
rx_single_avg   = mean(values(rx_single_start:rx_single_end,2));

tx_start    = sched_start + 8*640;
tx_end      = sched_start + 15*640;
tx_avg = mean(values(tx_start:tx_end,2));

tx_single_start = 1861500;
tx_single_end   = 1861500 + 1*64;
tx_single_avg   = mean(values(tx_single_start:tx_single_end,2));

resp_start  = sched_start + 15*640;
resp_end    = sched_start + 16*640;
resp_avg = mean(values(resp_start:resp_end,2));

resp_rx_start = sched_start + 15*640;
resp_rx_end   = sched_start + 15*640 + 640/4;
resp_rx_avg = mean(values(resp_rx_start:resp_rx_end,2));

resp_tx_start = sched_start + 15*640 + 3*640/4;
resp_tx_end   = sched_start + 15*640 + 4*640/4;
resp_tx_avg = mean(values(resp_tx_start:resp_tx_end,2));

cont_start  = sched_start + 16*640;
cont_end    = sched_start + 17*640;
cont_avg = mean(values(cont_start:cont_end,2));