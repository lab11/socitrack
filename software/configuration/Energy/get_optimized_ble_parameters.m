function [interval_scan,interval_adv] = get_optimized_ble_parameters(DISC_LATENCY_T, DISC_PROBABILITY, NR_NODES)

%GET_OPTIMIZED_BLE_PARAMETERS Implementation of the BLEnd companion optimizer
%   Based on the maximum discovery latency, the minimum discovery
%   probability and the maximum number of nodes in a collision domain, we
%   calculate the energy-optimal parameters
%
%   Based on:   "BLEnd: Practical Continuous Neighbor Discovery for
%   Bluetooth Low Energy" - Julien 2017

% Constants in mA for current / ms for time -------------------------------

% TX
TX_ADV_BARE_I  = 2.68;
TX_ADV_REQ_1_I = 3.01;
TX_ADV_REQ_2_I = 3.3;
TX_ADV_REQ_3_I = 3.6;

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
RX_ADV_I = 5.42;

% General
IDLE_I = 0;

BLE_SLACK_MAX   = 10; % 10ms randomness

% -------------------------------------------------------------------------

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
% fprintf('Results for BLEnd optimizer:\n')
% Q = Q_min;
% P = P_final;
E = E_final;
A = A_final;

% avg_I = Q / lambda;
%
% fprintf('Epoch length: %i ms\n',E);
% fprintf('Advertising interval: %i ms\n',A);
% fprintf('Average current draw: %.2f mA\n',avg_I);
% 
% L = A + b + s;
% 
% if mode_bi
%     nb = (floor(E / A) - 1);
% else
%     nb = (floor(E /(2*A) ) - 1);
% end
% 
% fprintf('Estimate a discovery probability of %.2f with %i advertisements per epoch\n',P,nb);

interval_scan = E;
interval_adv  = A;
end

