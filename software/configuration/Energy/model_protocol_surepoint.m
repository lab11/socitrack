% Energy model - SurePoint
% Author: Andreas Biri
% Date:   2018-12-02
clear all;

% INPUT PARAMS ------------------------------------------------------------

update_freq = 1; % Hz

accuracy  = 1000; % mm
precision = 1000; %mm

frequ_diversity = 3;
antenna_diversity = 3;

% Range is adjustable according ot DW1000 mode; this does however also
% influence timing and therefore requires non-trivial changes

num_init    = 0;
num_resp    = 0;
num_hybrid  = 2;
num_support = 0;

nr_nodes = num_init + num_resp + num_hybrid + num_support;

% PROTOCOL PARAMS ---------------------------------------------------------

% Scheduling

interval_round = 1000/update_freq; % ms
interval_slot  = 10; % ms

duration_schedule = 10;

interval_flood  = 2; % ms
max_flood_depth = 5;

protocol_standard_contention_length = 1;

% Ranging

num_antennas    = antenna_diversity;
num_frequ_bands = frequ_diversity;

num_ow_rangings = num_antennas * num_antennas * num_frequ_bands;
num_tw_rangings = num_frequ_bands;
num_rangings    = num_ow_rangings + num_tw_rangings;

interval_poll = interval_flood;

duration_rang_requ_passive = 10; %ms
duration_rang_requ_active  = num_rangings * interval_poll;
duration_rang_requ         = duration_rang_requ_active + duration_rang_requ_passive;

duration_rang_resp_slot = 10; %ms
duration_rang_resp    = 3 * duration_rang_resp_slot;
duration_rang_resp_rx = 2; %ms
duration_rang_resp_tx = 2; %ms

% PLATFORM PARAMS ---------------------------------------------------------

% System
U_sys = 3.3; %V_CC
Q_bat = 2000 * 3600; % mA * s

I_sleep = 2.1; % mA

% Schedule

I_schedule   =  25.8;
I_contention = 151.0;

% Ranging

I_rang_idle  = 23.1;
I_rang_dc    = 11.1;

I_rang_poll_tx_1ms = 41.1;
I_rang_poll_tx     = (I_rang_poll_tx_1ms + (interval_poll - 1) * I_rang_idle) / interval_poll;

I_rang_poll_rx_1ms = 94.7;
I_rang_poll_rx     = (I_rang_poll_rx_1ms + (interval_poll - 1) * I_rang_idle) / interval_poll;

I_rang_requ_tx     = ( I_rang_poll_tx * duration_rang_requ_active + I_rang_idle * duration_rang_requ_passive) / duration_rang_requ;
I_rang_requ_rx     = ( I_rang_poll_rx * duration_rang_requ_active + I_rang_idle * duration_rang_requ_passive) / duration_rang_requ;


I_rang_resp_tx = 37.8;
I_rang_resp_rx_active  =  93.5;
I_rang_resp_rx_passive = 151.0;

% CALCULATIONS ------------------------------------------------------------

% Scheduling
nr_contention_avg = protocol_standard_contention_length;


% Durations
duration_common = duration_schedule + nr_contention_avg * interval_slot;
duration_init   = (num_init + num_hybrid) * duration_rang_requ + (num_init + num_hybrid) * duration_rang_resp;
duration_resp   = (num_init + num_hybrid) * duration_rang_requ + (num_init + num_hybrid) * duration_rang_resp;
duration_hybrid = (num_init + num_hybrid) * duration_rang_requ + (num_init + num_hybrid) * duration_rang_resp;

I_common = (I_schedule * duration_schedule + I_contention * nr_contention_avg * interval_slot) / duration_common;

% Calculate average responses per responder (due to contention)
p_one_col = 1 - ((duration_rang_resp_slot - duration_rang_resp_tx)/duration_rang_resp_slot)^(        1 * (num_resp + num_hybrid - 1));
p_two_col = 1 - ((duration_rang_resp_slot - duration_rang_resp_tx)/duration_rang_resp_slot)^(p_one_col * (num_resp + num_hybrid - 1));
num_responses = (1 + p_one_col * 1 + p_two_col * 1);

num_responses_received = min( (num_resp + num_hybrid) * num_responses, duration_rang_resp / duration_rang_resp_tx);

% Initiator costs
I_init   = (I_common * duration_common ...
          + I_rang_requ_tx        *                           1 *  duration_rang_requ ...
          + I_rang_resp_rx_active *      num_responses_received *  duration_rang_resp_rx + I_rang_resp_rx_passive * 1 * (duration_rang_resp -  num_responses_received * duration_rang_resp_rx) ...
          + I_rang_idle           * (num_init + num_hybrid - 1) * (duration_rang_requ + duration_rang_resp) ) ...
          / (duration_common + duration_init);
      
% Responder costs
I_resp   = (I_common * duration_common ...
          + I_rang_requ_rx *                 (num_init + num_hybrid) * duration_rang_requ ...
          + I_rang_resp_tx * num_responses * (num_init + num_hybrid) * duration_rang_resp_tx + I_rang_idle * (num_init + num_hybrid) * (duration_rang_resp - num_responses * duration_rang_resp_tx) ) ...
          / (duration_common + duration_resp);
      
% Hybrid costs
I_hybrid = (I_common * duration_common ...
          + I_rang_requ_tx        *                                           1 * duration_rang_requ ...
          + I_rang_resp_rx_active *                      num_responses_received * duration_rang_resp_rx + I_rang_resp_rx_passive *                           1 * (duration_rang_resp - num_responses_received * duration_rang_resp_rx) ...
          + I_rang_requ_rx        *                 (num_init + num_hybrid - 1) * duration_rang_requ ...
          + I_rang_resp_tx        * num_responses * (num_init + num_hybrid - 1) * duration_rang_resp_tx + I_rang_idle            * (num_init + num_hybrid - 1) * (duration_rang_resp -          num_responses * duration_rang_resp_tx) ) ...
          / (duration_common + duration_hybrid);
    
% Add duty-cycling costs
I_init_tot   = (I_init   * (duration_common + duration_init  ) + I_rang_dc * (interval_round - (duration_common + duration_init  ) ) ) / (interval_round);

I_resp_tot   = (I_resp   * (duration_common + duration_resp  ) + I_rang_dc * (interval_round - (duration_common + duration_resp  ) ) ) / (interval_round);

I_hybrid_tot = (I_hybrid * (duration_common + duration_hybrid) + I_rang_dc * (interval_round - (duration_common + duration_hybrid) ) ) / (interval_round);


% Total system current
I_system_init   = I_init_tot;
I_system_resp   = I_resp_tot;
I_system_hybrid = I_hybrid_tot;

% EVAL --------------------------------------------------------------------

duration_day = 24;

life_time_init   = Q_bat / I_system_init   / 3600 / duration_day;
life_time_resp   = Q_bat / I_system_resp   / 3600 / duration_day;
life_time_hybrid = Q_bat / I_system_hybrid / 3600 / duration_day;

power_budget_init   = [ (I_init   * (duration_common + duration_init  ) / interval_round) / I_system_init  , (I_rang_dc * (interval_round - (duration_common + duration_init  ) ) / interval_round) / I_system_init  ];
power_budget_resp   = [ (I_resp   * (duration_common + duration_resp  ) / interval_round) / I_system_resp  , (I_rang_dc * (interval_round - (duration_common + duration_resp  ) ) / interval_round) / I_system_resp  ];
power_budget_hybrid = [ (I_hybrid * (duration_common + duration_hybrid) / interval_round) / I_system_hybrid, (I_rang_dc * (interval_round - (duration_common + duration_hybrid) ) / interval_round) / I_system_hybrid];

fprintf('Estimated lifetime INIT: \t %1.2f days @ %2.1f mA\n',     life_time_init, I_system_init);
fprintf('Estimated lifetime RESP: \t %1.2f days @ %2.1f mA\n',     life_time_resp, I_system_resp);
fprintf('Estimated lifetime HYBRID: \t %1.2f days @ %2.1f mA\n', life_time_hybrid, I_system_hybrid);

% FIGURES -----------------------------------------------------------------

% Power budget
font_size = 20;
figure('Name', 'Power budget distribution', 'DefaultAxesFontSize', font_size)
name = categorical({'Initiator', 'Responder', 'Hybrid'});
data_relative = [power_budget_init; power_budget_resp; power_budget_hybrid];
bar(name, data_relative);
ylim([0,1]);
xlabel('Node classes', 'FontSize', font_size);
ylabel('Energy consumption [%]', 'FontSize', font_size);
legend('Active period', 'Passive period');

font_size = 20;
figure('Name', 'Power budget distribution', 'DefaultAxesFontSize', font_size)
name = categorical({'Initiator', 'Responder', 'Hybrid'});
data_absolute = [power_budget_init * I_system_init; power_budget_resp * I_system_resp; power_budget_hybrid * I_system_hybrid];
bar(name, data_absolute, 'stacked');
xlabel('Node classes', 'FontSize', font_size);
ylabel('Energy consumption [mA]', 'FontSize', font_size);

% Life time
font_size = 20;
figure('Name', 'Life time', 'DefaultAxesFontSize', font_size)
name = categorical({'Initiator', 'Responder', 'Hybrid'});
data = [life_time_init * duration_day; life_time_resp * duration_day; life_time_hybrid * duration_day];
bar(name, data);
xlabel('Node classes', 'FontSize', font_size);
ylabel('Life time [h]', 'FontSize', font_size);

