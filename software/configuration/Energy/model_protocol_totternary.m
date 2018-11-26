% New ranging protocol - TotTernary

% Parameters are actual ones from the DW1000
% We ignore DEEPSLEEP current, as its 100nA and therefore much less than BLE and the STM
clear all;

% PROTOCOL PARAMS ---------------------------------------------------------

num_init    = 0;
num_resp    = 0;
num_hybrid  = 2;
num_support = 0;

% Bluetooth

interval_adv  =  100; % ms
interval_scan = 5000; % ms
duration_scan = interval_adv + 10; % ms

% Scheduling

interval_round = 1000; % ms
interval_slot  = 10; % ms

duration_schedule = 10;

interval_flood  = 2; % ms
max_flood_depth = 5;

protocol_automatic_timeout_rounds   = 250; % rounds
protocol_standard_contention_length = 1;

% Ranging

num_antennas = 3;
num_channels = 3;

num_ow_rangings = num_antennas * num_antennas * num_channels;
num_tw_rangings = num_channels;
num_rangings    = num_ow_rangings + num_tw_rangings;

interval_poll  = interval_flood;

interval_rang_requ = interval_poll;

duration_rang_requ_passive = 10; %ms
duration_rang_requ_active  = num_rangings * interval_rang_requ;
duration_rang_requ         = duration_rang_requ_active + duration_rang_requ_passive;

duration_rang_resp  = 2.5; % ms

% Functionality
protocol_reenable_hybrids = 1;

protocol_flexible_master = 0;

protocol_enable_timeout = 1;
protocol_timeout_period = 5 * interval_round;
protocol_enable_master_takeover = 0;
protocol_master_takeover_period = 10 * interval_round;


% PLATFORM PARAMS ---------------------------------------------------------

% System
U_sys = 3.3; %V_CC
Q_bat = 2000 * 3600; % mA * s

I_sleep = 2.1; % mA

% Bluetooth

I_ble_idle = 0.7; % mA

ble_adv_probability_zero  = 0.2;
ble_adv_probability_one   = 0.54;
ble_adv_probability_two   = 0.24;
ble_adv_probability_three = 0.02;

ble_adv_length_zero  = 4.6; % ms
ble_adv_length_one   = 5.2; % ms
ble_adv_length_two   = 5.8; % ms
ble_adv_length_three = 6.4; % ms

I_ble_adv_zero  = 7.7; % mA
I_ble_adv_one   = 8.4; % mA
I_ble_adv_two   = 9.0; % mA
I_ble_adv_three = 9.5; % mA

Q_ble_adv =             ble_adv_probability_zero  * ble_adv_length_zero  * I_ble_adv_zero;
Q_ble_adv = Q_ble_adv + ble_adv_probability_one   * ble_adv_length_one   * I_ble_adv_one;
Q_ble_adv = Q_ble_adv + ble_adv_probability_two   * ble_adv_length_two   * I_ble_adv_two;
Q_ble_adv = Q_ble_adv + ble_adv_probability_three * ble_adv_length_three * I_ble_adv_three;

duration_adv = ble_adv_probability_zero * ble_adv_length_zero + ble_adv_probability_one * ble_adv_length_one + ble_adv_probability_two * ble_adv_length_two + ble_adv_probability_three * ble_adv_length_three;
I_ble_adv    = Q_ble_adv / duration_adv;

I_ble_scan = 13.9; % mA

% Schedule

I_schedule   =  25.8;
I_contention = 151.0;

% Ranging

I_rang_idle  = 23.1;
I_rang_dc    = I_sleep + 9.0;
I_rang_sleep = I_sleep;

I_rang_poll_tx_1ms = 41.1;
I_rang_poll_tx     = (I_rang_poll_tx_1ms + (interval_poll - 1) * I_rang_idle) / interval_poll;

I_rang_poll_rx_1ms = 94.7;
I_rang_poll_rx     = (I_rang_poll_rx_1ms + (interval_poll - 1) * I_rang_idle) / interval_poll;

I_rang_requ_tx     = ( I_rang_poll_tx * duration_rang_requ_active + I_rang_idle * duration_rang_requ_passive) / duration_rang_requ;
I_rang_requ_rx     = ( I_rang_poll_rx * duration_rang_requ_active + I_rang_idle * duration_rang_requ_passive) / duration_rang_requ;


I_rang_resp_tx = 37.8;
I_rang_resp_rx = 93.5;


% CALCULATIONS ------------------------------------------------------------

% Bluetooth
I_ble = (I_ble_adv  * (interval_scan / interval_adv * duration_adv ) ...
       + I_ble_scan * (                           1 * duration_scan) ...
       + I_ble_idle * (interval_scan - (interval_scan / interval_adv * duration_adv) - duration_scan)) ...
       / interval_scan;



% Scheduling
nr_contention_avg = protocol_standard_contention_length;


% Durations
duration_common = duration_schedule + nr_contention_avg * interval_slot;
duration_init   = (num_init + num_hybrid) * duration_rang_requ + (num_resp + num_hybrid) * duration_rang_resp;
duration_resp   = (num_init + num_hybrid) * duration_rang_requ + (num_resp + num_hybrid) * duration_rang_resp;
duration_hybrid = (num_init + num_hybrid) * duration_rang_requ + (num_resp + num_hybrid) * duration_rang_resp;

I_common = (I_schedule * duration_schedule + I_contention * nr_contention_avg * interval_slot) / duration_common;

% Initiator costs
I_init   = (I_common * duration_common ...
          + I_rang_requ_tx *                       1 * duration_rang_requ + I_rang_idle * (num_init + num_hybrid - 1) * duration_rang_requ ...
          + I_rang_resp_rx * (num_resp + num_hybrid) * duration_rang_resp) ...
          / (duration_common + duration_init);
      
% Responder costs
I_resp   = (I_common * duration_common ...
          + I_rang_requ_rx * (num_init + num_hybrid) * duration_rang_requ ...
          + I_rang_resp_tx *                       1 * duration_rang_resp + I_rang_idle * (num_resp + num_hybrid - 1) * duration_rang_resp)...
          / (duration_common + duration_resp);
      
% Hybrid costs
I_hybrid = (I_common * duration_common ...
          + I_rang_requ_tx *                       1 * duration_rang_requ + I_rang_requ_rx * (num_init + num_hybrid - 1) * duration_rang_requ ...
          + I_rang_resp_tx *                       1 * duration_rang_resp + I_rang_resp_rx * (num_resp + num_hybrid - 1) * duration_rang_resp) ...
          / (duration_common + duration_hybrid);
      
% Add duty-cycling costs
I_init_tot   = (I_init   * duration_init   + I_rang_dc * (interval_round - duration_init  ) ) / (interval_round)

I_resp_tot   = (I_resp   * duration_resp   + I_rang_dc * (interval_round - duration_resp  ) ) / (interval_round)

I_hybrid_tot = (I_hybrid * duration_hybrid + I_rang_dc * (interval_round - duration_hybrid) ) / (interval_round)


% EVAL --------------------------------------------------------------------

duration_day = 24;

life_time_init   = Q_bat / (I_init_tot   + I_ble) / 3600 / duration_day
life_time_resp   = Q_bat / (I_resp_tot   + I_ble) / 3600 / duration_day
life_time_hybrid = Q_bat / (I_hybrid_tot + I_ble) / 3600 / duration_day

% FIGURES -----------------------------------------------------------------
