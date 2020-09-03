function [lifetime_totternary, lifetime_surepoint] = get_protocol_lifetime(num_init, num_resp, num_hybrid, update_freq, frequ_div, antenna_div, use_optimized_params)
%GET_PROTOCOL_LIFETIME Returnes estimated life time based on the energy
%models
%   
% Based on the energy models for both TotTernary and SurePoint, this
% function returns the estimated life times for each node class
%
% INPUTS:
% num_init:     number of (pure) initiators
% num_resp:     number of (pure) responders
% num_hybrid:   number of hybrids
% update_freq:  targeted update frequency / round period [Hz]
% frequ_div:    frequency diversity channels (1-3)
% antenna_div:  antenna diversity channels (1-3)

% use_optimized_params: Adjust parameters to reflect an improved version

%% General params

% INPUT PARAMS ------------------------------------------------------------

frequ_diversity   = frequ_div;
antenna_diversity = antenna_div;

discovery_latency     = 5000;
discovery_probability = 0.9;

nr_nodes = num_init + num_resp + num_hybrid;

% PROTOCOL PARAMS ---------------------------------------------------------

% Bluetooth

%interval_scan = discovery_latency; % ms
%interval_adv  = 125; % ms

% Derive directly using the BLEnd optimizer
[interval_scan, interval_adv] = get_optimized_ble_parameters(discovery_latency, discovery_probability, nr_nodes);


% Scheduling

interval_round = 1000/update_freq; % ms
interval_slot  = 10; % ms

duration_schedule = 10;

if (use_optimized_params > 0)
    interval_flood = 1; %ms
else
    interval_flood  = 2; % ms
end

max_flood_depth = 5;

protocol_automatic_timeout_rounds   = 250; % rounds
protocol_standard_contention_length = 1;

% Ranging

num_antennas    = antenna_diversity;
num_frequ_bands = frequ_diversity;

num_ow_rangings = num_antennas * num_antennas * num_frequ_bands;
num_tw_rangings = num_frequ_bands;
num_rangings    = num_ow_rangings + num_tw_rangings;

interval_poll = interval_flood;

if (use_optimized_params > 0)
    duration_rang_requ_passive = 0; %ms
    duration_rang_resp  = 2; % ms
else
    duration_rang_requ_passive = 10; %ms
    duration_rang_resp  = 2.5; % ms
end
duration_rang_requ_active  = num_rangings * interval_poll;
duration_rang_requ         = duration_rang_requ_active + duration_rang_requ_passive;

response_length   = 96;
max_packet_length = 1023;

% SD card

bytes_per_ranging_log = 10 + 1 + 2*8 + 7 + 1 + 6 + 1;

% PLATFORM PARAMS ---------------------------------------------------------

% System
U_sys = 3.3; %V_CC
Q_bat = 2000 * 3600; % mA * s

I_sleep = 2.1; % mA

% Bluetooth

I_ble_idle = 0; % mA (overhead counted for UWB as baseband, as it needs to trigger it for sleeping)

ble_adv_probability_zero  = 0.2;
ble_adv_probability_one   = 0.54;
ble_adv_probability_two   = 0.24;
ble_adv_probability_three = 0.02;

ble_adv_length_zero  = 4.6; % ms
ble_adv_length_one   = 5.2; % ms
ble_adv_length_two   = 5.8; % ms
ble_adv_length_three = 6.4; % ms

I_ble_adv_zero  = 2.68; % mA
I_ble_adv_one   = 3.01; % mA
I_ble_adv_two   = 3.30; % mA
I_ble_adv_three = 3.60; % mA

Q_ble_adv =             ble_adv_probability_zero  * ble_adv_length_zero  * I_ble_adv_zero;
Q_ble_adv = Q_ble_adv + ble_adv_probability_one   * ble_adv_length_one   * I_ble_adv_one;
Q_ble_adv = Q_ble_adv + ble_adv_probability_two   * ble_adv_length_two   * I_ble_adv_two;
Q_ble_adv = Q_ble_adv + ble_adv_probability_three * ble_adv_length_three * I_ble_adv_three;

duration_adv = ble_adv_probability_zero * ble_adv_length_zero + ble_adv_probability_one * ble_adv_length_one + ble_adv_probability_two * ble_adv_length_two + ble_adv_probability_three * ble_adv_length_three;
I_ble_adv    = Q_ble_adv / duration_adv;

duration_scan = interval_adv + duration_adv + 10; % ms -> 10ms is max BLE random slack
I_ble_scan = 5.42; % mA

% Schedule

if (use_optimized_params > 0)
    I_schedule   =  51.54;
else
    I_schedule   = 407.70; % including overhead
end
I_contention = 131.93;

% Ranging

if (use_optimized_params > 0)
    I_rang_idle = 11.1;
    I_rang_dc   = I_sleep;
else
    I_rang_idle  = 22.53;
    I_rang_dc    = 7.14;
end

I_rang_poll_tx_1ms = 36.65;
I_rang_poll_tx     = (I_rang_poll_tx_1ms + (interval_poll - 1) * I_rang_idle) / interval_poll;

I_rang_poll_rx_1ms = 87.18;
I_rang_poll_rx     = (I_rang_poll_rx_1ms + (interval_poll - 1) * I_rang_idle) / interval_poll;

I_rang_requ_tx     = ( I_rang_poll_tx * duration_rang_requ_active + I_rang_idle * duration_rang_requ_passive) / duration_rang_requ;
I_rang_requ_rx     = ( I_rang_poll_rx * duration_rang_requ_active + I_rang_idle * duration_rang_requ_passive) / duration_rang_requ;


I_rang_resp_tx         =  29.72;
I_rang_resp_rx_active  =  99.61;
I_rang_resp_rx_passive = 131.93;

% SD card - 20 * 1024 bytes in 240ms

I_sd_write    = 26.5; % mA
sd_write_size = 20 * 1024; % bytes
sd_write_time = 240; % ms

%% TotTernary

% Functionality

% Do not use ranging pyramid - all hybrids will gather all ranges locally
protocol_reenable_hybrids = 0;

%protocol_enable_timeout = 1;
%protocol_timeout_period = 5 / update_freq;
%protocol_enable_master_takeover = 0;
%protocol_master_takeover_period = 10 / update_freq;

% Stop packet reception as initiator after a given amount
protocol_max_responses = 15; % 0: off, X : stop listening after maximally X responses

% Enable local logging to SD card
protocol_local_logging = 1; % 0 : off, 1 : on

% CALCULATIONS ------------------------------------------------------------

% Bluetooth
nr_adv = floor(interval_scan / interval_adv) - 1;

I_ble = (I_ble_adv  * (nr_adv * duration_adv) ...
       + I_ble_scan * (     1 * duration_scan) ...
       + I_ble_idle * (interval_scan - nr_adv * duration_adv - 1 * duration_scan) ) ...
       / interval_scan;

dc_ble = (nr_adv * duration_adv + 1 * duration_scan) / interval_scan;

% Scheduling
nr_contention_avg = protocol_standard_contention_length;

% Calculate number of responses per responder
num_responses = ceil( (num_init + num_hybrid) * response_length / max_packet_length);

% Durations
duration_common = duration_schedule + nr_contention_avg * interval_slot;
duration_init   = (num_init + num_hybrid) * duration_rang_requ + num_responses * (num_resp + num_hybrid) * duration_rang_resp;
duration_resp   = (num_init + num_hybrid) * duration_rang_requ + num_responses * (num_resp + num_hybrid) * duration_rang_resp;
duration_hybrid = (num_init + num_hybrid) * duration_rang_requ + num_responses * (num_resp + num_hybrid) * duration_rang_resp;

I_common = (I_schedule * duration_schedule + I_contention * nr_contention_avg * interval_slot) / duration_common;

% Initiator costs
I_init   = (I_common * duration_common ...
          + I_rang_requ_tx        *                                       1 * duration_rang_requ + I_rang_idle * (num_init + num_hybrid - 1) * duration_rang_requ ...
          + I_rang_resp_rx_active * num_responses * (num_resp + num_hybrid) * duration_rang_resp) ...
          / (duration_common + duration_init);
      
% Responder costs
I_resp   = (I_common * duration_common ...
          + I_rang_requ_rx * (num_init + num_hybrid) * duration_rang_requ ...
          + I_rang_resp_tx *           num_responses * duration_rang_resp + I_rang_idle * num_responses * (num_resp + num_hybrid - 1) * duration_rang_resp)...
          / (duration_common + duration_resp);
      
% Hybrid costs
I_hybrid = (I_common * duration_common ...
          + I_rang_requ_tx *             1 * duration_rang_requ + I_rang_requ_rx *                        (num_init + num_hybrid - 1) * duration_rang_requ ...
          + I_rang_resp_tx * num_responses * duration_rang_resp + I_rang_resp_rx_active * num_responses * (num_resp + num_hybrid - 1) * duration_rang_resp) ...
          / (duration_common + duration_hybrid);
      
      
if (protocol_max_responses)
    % Calculate number of responses
    nr_resp = min(protocol_max_responses, num_resp + num_hybrid);
    
    I_init = (I_common * duration_common ...
            + I_rang_requ_tx        *                       1 * duration_rang_requ + I_rang_idle *                 (num_init + num_hybrid -       1) * duration_rang_requ ...
            + I_rang_resp_rx_active * num_responses * nr_resp * duration_rang_resp + I_rang_idle * num_responses * (num_init + num_hybrid - nr_resp) * duration_rang_resp) ...
            / (duration_common + duration_init);
end
      
if (not(protocol_reenable_hybrids))      
    % Use ranging pyramid - average number
    I_hybrid = (I_common * duration_common ...
          + I_rang_requ_tx *             1 * duration_rang_requ + I_rang_requ_rx        *                 (num_init + (num_hybrid - 1)/2) * duration_rang_requ + I_rang_idle * (                 (num_hybrid - 1)/2 ) * duration_rang_requ ...
          + I_rang_resp_tx * num_responses * duration_rang_resp + I_rang_resp_rx_active * num_responses * (num_resp + (num_hybrid - 1)/2) * duration_rang_resp + I_rang_idle * ( num_responses * (num_hybrid - 1)/2 ) * duration_rang_resp) ...
          / (duration_common + duration_hybrid);
end
      
% Add duty-cycling costs
I_init_tot   = (I_init   * (duration_common + duration_init  ) + I_rang_dc * (interval_round - (duration_common + duration_init  ) ) ) / (interval_round);

I_resp_tot   = (I_resp   * (duration_common + duration_resp  ) + I_rang_dc * (interval_round - (duration_common + duration_resp  ) ) ) / (interval_round);

I_hybrid_tot = (I_hybrid * (duration_common + duration_hybrid) + I_rang_dc * (interval_round - (duration_common + duration_hybrid) ) ) / (interval_round);



% SD card
I_sd_init   = I_sd_write * ((           num_resp + num_hybrid) * bytes_per_ranging_log / interval_round) * (sd_write_time / sd_write_size);
I_sd_resp   = I_sd_write * ((num_init +            num_hybrid) * bytes_per_ranging_log / interval_round) * (sd_write_time / sd_write_size);
I_sd_hybrid = I_sd_write * ((num_init + num_resp + num_hybrid) * bytes_per_ranging_log / interval_round) * (sd_write_time / sd_write_size);

if (not(protocol_local_logging))
    I_sd_init   = 0;
    I_sd_resp   = 0;
    I_sd_hybrid = 0;
end

% Total system current
I_system_init   = I_init_tot; %   + I_sd_init   + I_ble;
I_system_resp   = I_resp_tot; %   + I_sd_resp   + I_ble;
I_system_hybrid = I_hybrid_tot; % + I_sd_hybrid + I_ble;

% EVAL --------------------------------------------------------------------

lifetime_totternary(1) = Q_bat / I_system_init   / 3600;
lifetime_totternary(2) = Q_bat / I_system_resp   / 3600;
lifetime_totternary(3) = Q_bat / I_system_hybrid / 3600;



%% SurePoint

duration_rang_resp_slot = 10; %ms
duration_rang_resp    = 3 * duration_rang_resp_slot;
duration_rang_resp_rx = 2; %ms
duration_rang_resp_tx = 2; %ms

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

lifetime_surepoint(1) = Q_bat / I_system_init   / 3600;
lifetime_surepoint(2) = Q_bat / I_system_resp   / 3600;
lifetime_surepoint(3) = Q_bat / I_system_hybrid / 3600;

end

