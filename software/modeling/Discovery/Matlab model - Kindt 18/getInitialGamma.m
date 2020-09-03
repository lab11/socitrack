% Function for computing gamma_1
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


% GETINITIALGAMMA Computes the mode and the gamma-parameter of the process with order 1.
% Parameters:
% Ta: advertising interval [s]
% Ts: Scan interval [s]
% Return
% mode: The mode of the order-1 process. 1 => shrinking, 2=>growing, 3=> coupling
% gamma: The value of gamma_1 [s]
function [mode, gamma] = getInitialGamma(Ta, Ts);


debugMode = 0;
float_epsilon = 0;%10^(-14);

if(Ta < Ts)
    gammaSh = Ts - floor(Ts/Ta)*Ta;
       gammaGr = ceil(Ts/Ta)*Ta - Ts ;%+ sin(Ta/(0.001*Ts))*Ts/10;
    if(gammaSh  < float_epsilon)
      if(debugMode==1)
        disp(sprintf('coupling, gamma = %f\n',0));
      end;
        mode = 3;   %coupling
        gamma = 0;

    elseif(gammaSh < 0.5*Ta)
      mode = 1;   %shrinking
      gamma = gammaSh; 
      if(debugMode==1)
          disp(sprintf('shrinking, gamma = %f\n',gammaSh));
      end;
    else
        mode = 2;   % growing
        gamma = gammaGr;
      if(debugMode==1)
        disp(sprintf('growing, gamma = %f\n',gammaGr));
      end;
    end;

else
    gammaSh = ceil(Ta/Ts)*Ts - Ta;
    gammaGr = Ta - floor(Ta/Ts)*Ts ;%+ sin(Ta/(0.001*Ts))*Ts/10;

    if(gammaSh < float_epsilon)
        mode = 3;   %coupling
        gamma = 0;
        if(debugMode == 1)
            disp(sprintf('[Ts > Ta] coupling, gammaOrig  = %f (set to zero)\n',gamma));
        end;

    elseif(gammaSh < 0.5*Ts)
      mode = 1;   %shrinking
      gamma = gammaSh;  
        if(debugMode == 1)
            disp(sprintf('[Ts > Ta] shrinking, gamma  = %f\n',gamma));
        end;
    else

        mode = 2;   % growing
        gamma = gammaGr;
        if(debugMode == 1)
            disp(sprintf('[Ts > Ta] growing, gamma  = %f\n',gamma));
        end;

    end;
end;
if(checkFloatingPointEqual(gamma,0))
    mode = 3;
end;
