% Original ranging protocol - SurePoint

% Parameters are actual ones from the DW1000
% We ignore DEEPSLEEP current, as its 100nA and therefore much less than BLE and the STM
clear all;

interval_ranging = 1000; % ms
U_system = 3.3; %V_CC

rx_current = 171; %mA
tx_current = 55; %mA

rx_power = U_system * rx_current;
tx_power = U_system * tx_current;

% Wakeup cost
wakeup_time    = 5; %ms
wakeup_current = 1; %mA
cost_wakeup = wakeup_time * U_system * wakeup_current; %uW

interval_start = 1000;
interval_end   = 4*7*24*3600000;
interval_step  = 1000;
interval_range = interval_start:interval_step:interval_end;
for interval_ranging = interval_range

    delta_tot = U_system * 170 * (20 / 1000000);
    early_wakeup_cost = interval_ranging * delta_tot;

    cost_tot = early_wakeup_cost; %Rx
    cost_tot = cost_tot + U_system * ( (5 * 54 + 30 * 55  + 1 * 170 + 5 * 170) + (5 * 38 + 30 * 70 + 1 * 55 + 5 * 40) ); % Transmission

    c_tot(interval_ranging/interval_step - (interval_start/interval_step - 1)) = cost_tot;
end

% Figure
xlabel('Ranging interval [ms]');
ylabel('Cost per range measurement [uW]');
hold on;
loglog(interval_range, c_tot, 'Color', 'blue');
hold off;

% Eval

