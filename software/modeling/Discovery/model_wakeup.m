% 
% Model: Wake-up radio consumption
%
% Author: Andreas Biri
% Date: 2018-08-03

% Setup
clear all;
addpath(genpath('Matlab model - Kindt 18'))

% Constants in mA for current / ms for time

% TX
TX_ADV_BARE_I  = 4.1;
TX_ADV_REQ_1_I = 4.6;
TX_ADV_REQ_2_I = 4.9;
TX_ADV_REQ_3_I = 5.2;

TX_ADV_BARE_T  = 4.6;
TX_ADV_REQ_1_T = 5.2;
TX_ADV_REQ_2_T = 5.8;
TX_ADV_REQ_3_T = 6.4;

ADV_BARE_P  = 0.2;
ADV_REQ_1_P = 0.54;
ADV_REQ_2_P = 0.24;
ADV_REQ_3_P = 0.02;

TX_ADV_I = ADV_BARE_P * TX_ADV_BARE_I + ADV_REQ_1_P * TX_ADV_REQ_1_I + ADV_REQ_2_P * TX_ADV_REQ_2_I + ADV_REQ_3_P * TX_ADV_REQ_3_I;
TX_ADV_T = ADV_BARE_P * TX_ADV_BARE_T + ADV_REQ_1_P * TX_ADV_REQ_1_T + ADV_REQ_2_P * TX_ADV_REQ_2_T + ADV_REQ_3_P * TX_ADV_REQ_3_T;

% RX
RX_ADV_I = 6.9;

% General
IDLE_I = 0.7;

PACKET_LENGTH_B = 47; % 16 bytes preamble + 0-31 bytes payload
BLE_FREQU       = 1e6; % 1 MHz
BLE_SLACK_MAX   = 10; % 10ms randomness

MS_TO_S = 1/1000;
S_TO_MS = 1000;

% Parameters in ms for time
DISC_LATENCY_T    = 5000;
DISC_PROBABILITY  = 0.9;
NR_NODES          = 2;

% -------------------------------------------------------------------------
%
% BLEnd
%

% Energy consumption
% EPOCH_T = 5000;
% NR_ADV = 10; % nb: number of advertisements per epoch
% 
% HQ_SCAN_T = (EPOCH_T / 2) / NR_ADV % time in-between advertisements during active period
% INTERVAL_SCAN_T = HQ_SCAN_T + TX_ADV_T + 10;
% 
% EPOCH_Q = (RX_ADV_I*INTERVAL_SCAN_T + TX_ADV_I*NR_ADV*TX_ADV_T + IDLE_I*(EPOCH_T - INTERVAL_SCAN_T - NR_ADV*TX_ADV_T)) * (MS_TO_S)^2;
% EPOCH_I = EPOCH_Q / (EPOCH_T*MS_TO_S) * S_TO_MS;

% Optimizer
% Input
mode_bi = true;
lambda  = DISC_LATENCY_T;
P       = DISC_PROBABILITY;
N       = NR_NODES;

b = TX_ADV_T;
s = BLE_SLACK_MAX;

% Iteration
Q_min   = 1000 * lambda; % Q = A * T
Q_temp  = zeros(lambda,lambda);
E_final = 0;
A_final = 0;
P_final = 0;

for E = 20:lambda
    for A = 20:E

        % Update temp variables
        L = A + b + s;

        if mode_bi
            nb = (floor(E / A) - 1);
        else
            nb = (floor(E /(2*A)) - 1);
        end
        
        % Calculate probability of collision with other advertisings
        if mode_bi
            C = N;
        else
            C = ceil(N/2);
        end
        
        k = (lambda - mod(lambda,E)) / E;
        W = s*nb/2;
        gamma = (C-2)*(1 + s/(L-b)/2);
        omega = 1 + W/(L-b)*(gamma - 1);
        
        P_nc = (1 - (2*b)/(L - b))^gamma;
        
        if (W < (L-b))
            P_nc_W = (1 - 2*b/W)^omega;
        else
            P_nc_W = P_nc;
        end
        
        P_disc = 1 - (1 - P_nc)*(1 - P_nc_W)^(k-1)*( 1 - (lambda - k*E)/E * P_nc_W);
        
        if (P_disc > P)
            
            % Evaluate power
            Q_epoch = RX_ADV_I*L + TX_ADV_I*nb*b + IDLE_I*(E - L - nb*b);
            Q_temp(E,A) = Q_epoch * lambda/E; % make energy consumption comparable by stretching epoch consumption over discovery latency

            if (Q_temp(E,A) < Q_min)
                Q_min   = Q_temp(E,A);
                P_final = P_disc;
                E_final = E;
                A_final = A;
            end
            
        end
    end
end

% Output
fprintf('Results for BLEnd optimizer:\n')
Q = Q_min;
P = P_final;
E = E_final;
A = A_final;

avg_I = Q / lambda;

fprintf('Epoch length: %i ms\n',E);
fprintf('Advertising interval: %i ms\n',A);
fprintf('Average current draw: %.2f mA\n',avg_I);

L = A + b + s;

if mode_bi
    nb = (floor(E / A) - 1);
else
    nb = (floor(E /(2*A) ) - 1);
end

fprintf('Estimate a discovery probability of %.2f with %i advertisements per epoch\n',P,nb);

% Plot contour: https://www.mathworks.com/help/matlab/ref/contour.html
axis = 1:lambda;
contour_levels = 10;

[C,h] = contourf( axis, axis, log(Q_temp / lambda)', contour_levels);
clabel(C,h);

xlabel('Epoch length E [ms]');
ylabel('Advertising Interval A [ms]');



% -------------------------------------------------------------------------
%
% KINDT 18 - Latency verification calculation
% We use the library by Kindt et al. : http://www.rcs.ei.tum.de/forschung/wireless-systems/discovery-latency-computation-library/
%
INTERVAL_ADV_T    = A; % Ta: advertisement interval
INTERVAL_SCAN_T   = E; % Ts: scanning interval
TX_PACKET_T       = PACKET_LENGTH_B * 8  / BLE_FREQU; % dA: time per advertising packet
RX_SCAN_WINDOW_T  = L; %dS: scanning window
SIM_ITERATION_MAX = 1000; %il: maximum number of iterations for simulation

DISC_LATENCY_SIM_T = getDiscoveryLatency( INTERVAL_ADV_T*MS_TO_S, INTERVAL_SCAN_T*MS_TO_S, TX_PACKET_T*MS_TO_S, RX_SCAN_WINDOW_T*MS_TO_S, SIM_ITERATION_MAX) * S_TO_MS;

fprintf('Estimated bi-directional discovery latency: %.f ms\n',DISC_LATENCY_SIM_T);

















