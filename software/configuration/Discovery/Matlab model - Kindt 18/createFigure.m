% Create a plot of the discovery latencies over sweeping values of Ta
% Philipp H. Kindt <philipp.kindt@tum.de>
%
%    PI-LatencyComp
%    Copyright (C) 2017 Philipp H. Kindt
%
%    This program is free software: you can redistribute it and/or modify
%    it under the terms of the GNU General Public License as published by
%    the Free Software Foundation, either version 3 of the License, or
%    (at your option) any later version.
%
%    This program is distributed in the hope that it will be useful,
%    but WITHOUT ANY WARRANTY; without even the implied warranty of
%    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
%    GNU General Public License for more details.
%
%    You should have received a copy of the GNU General Public License
%    along with this program.  If not, see <http://www.gnu.org/licenses/>.


clear;

%plot parameters
 TaMin = 0.02;          % Ta to start with
 TaMax = 3.0;         % Ta the sweeping ends at
 step = 1*0.000625;     % steps for increasing Ta
 Ts = 2.56;             % Scan interval
 ds(1) = 0.32;          % Scan window. Add multiple if you'd like to have multiple curves for different values of ds in one plot
 dA = 248*10^(-6);      % advertising packet legnth
 iterationLimit = 200;  % Maximum process order examined

 
clf;
close all;




 
for(di = 1:size(ds,2))
     d = ds(di);
     i = 1;
     for(Ta = TaMin:step:TaMax)
          Tas(i) = Ta;          
          [ep(i),dMin(i),dMax(i), order(i)] = getDiscoveryLatency(Ta,Ts,dA,d,iterationLimit); 
          i = i + 1;
     end;
     
      hold on;
      plot(Tas,ep, 'color',[1.0,0.0,di/size(ds,2)],'LineStyle','-','LineWidth',1);
      plot(Tas,dMax, 'color',[0.5,0.5,di/size(ds,2)],'LineStyle','-','LineWidth',1);
      plot(Tas,order, 'color',[0.2,0.2,di/size(ds,2)],'LineStyle',':','LineWidth',1);
      hold off;
end;
xlabel('Advertising Interval T_a [s]');
ylabel('Discovery Latency d_{nd} [s]');
legend('mean', 'max', 'max. order');

