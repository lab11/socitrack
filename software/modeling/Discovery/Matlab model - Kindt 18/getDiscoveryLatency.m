% Function to compute the mean discovery latency.
% Philipp H. Kindt <philipp.kindt@tum.de>
%
% Computes the average, minimum and maximum discovery latency fo a given
% set of parameters. It also computes the maximum order of the
% gamma-processes involved

% Parameters:
% Ta: Advertising interval [s]
% Ts: Scan interval [s]
% dA: Length of one advertising packet [s]
% ds: Scan window [s]
% Iteration limit: maximum number of iterations after which the algorithm
%                  is aborted. Make sure to set this higher than the
%                  maximum process order you expect in order to get
%                  accurate results.
% debugMode: 0 => no debug messages
%             1 => debug messages
%
% Return values:
% dAvg: Mean discovery latency [s]
% dMin: Minimum discovery latency [s]
% dMax: Max discovery latency [s]
% Order: Maximum process order that occurred
%
%
%    This file is part of PI-LatencyComp
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
%    along with this program.  If not, see <http://www.gnu.org/licensesfunction [dAvg, dMin, dMax, order] = getDiscoveryLatency(Ta,Ts,dA,ds,iterationLimit,debugMode)

function [dAvg, dMin, dMax, order] = getDiscoveryLatency(Ta,Ts,dA,ds,iterationLimit)




%Algorithm parametrization
infinity = 1000;            %This value is considered as infinity. If latencies converge to infinity, they will be truncated to this value
debugMode = false;          %set to one if you want debugging figures and messages

dConst = dA;
Tae = Ta;
ds = ds - dA;
abortionEpsilon = 1e-10;


%initialization
travelDist = Ts;
dMaxGamma = 0;
penaltySum = 0;
order = 0;
dAvg = 0;
dMin = 0;
dMax = 0;

if(debugMode == 1)
    clf;
end;




d = dA;
dMin = dA;  %Holds true all the time



%-------------------------------[Non-Gamma-Stage]---------------------------------------------------------------------------------------
%Continuous scanning
if(Ts <= ds)
    dMax = dConst;
    dAvg = dConst;
    order = 0;
    if(debugMode)
        disp('Continuous scanning or Ts <= ds (=> invalid configuration)');
    end;
    return;
end;
%-------------------------------[Gamma - 1 to N - preparation]---------------------------------------------------------------------------------------
% Gamma-1-hit: Hit after an offset of Tsmall time-units has been 'gammarized'
dE = durationEstimator;

%Which debug modes should be taken from the calling functions suggestions?
dE.debugParamCalculation = debugMode;
dE.debugGamma = debugMode;
if(Ta <= Ts)
    gammaVectorFull = [Ta];
    penaltyVectorFull = [Ta];
    modeVectorFull = [2];
else
    [mode, gamma] = getInitialGamma(Tae, Ts);
    
    if(gamma > abortionEpsilon)
        mode = 1;
    else
        mode = 3;
    end
    gammaVectorFull = [gamma];
    modeVectorFull = [mode];
    penaltyVectorFull = [Ta];
    
    
end;
dGamma = 0;
dMaxDirectHit = 0;
pbRes = probabBuffer;
pbRes.add(0,Ts - ds,1/Ts);










%-------------------------------[Gamma - N - Stages]---------------------------------------------------------------------------------------

firstOrder = 0;

for(i = firstOrder:(iterationLimit - 1))
    order = i;
    
    if(modeVectorFull(end) ~= 3)&&(gammaVectorFull(end) >= ds)
        [gammaVectorFull(end + 1), penaltyVectorFull(end + 1), modeVectorFull(end + 1),travelDist, penaltySum] = dE.getNextParameters(Tae, Ts,gammaVectorFull,penaltyVectorFull,modeVectorFull, travelDist, penaltySum,i);
    end;
    
    %Computation of next Order's gamma and mode
    gamma = gammaVectorFull(order + 1);
    penalty = penaltyVectorFull(order + 1);
    mode = modeVectorFull(order + 1);
    if(length(modeVectorFull) > order + 1)
        modeNext = modeVectorFull(order + 2);
    else
        modeNext = 2;
    end;
    
    if(mode == 2)
        [dGamma, pbRes, dMaxGamma] = dE.growToRight(pbRes, Ts, ds, gamma, penalty,modeNext);
    elseif(mode == 1)
        [dGamma, pbRes, dMaxGamma] = dE.shrinkToLeft(pbRes, Ts, ds, gamma, penalty, modeNext);
    else
        if(debugMode)
            disp(sprintf('Aborting on stage %d since mode = %d',i + 1,mode));
        end;
        dAvg = infinity;
        dMax = infinity;
        return;
    end;
    dAvg = dAvg + dGamma;
    
    
    if(gamma < ds + abortionEpsilon)||(pbRes.getNSegments() == 0)
        
        if(debugMode)
            disp(sprintf('Aborting on stage %d. gamma = %f, pbRes.getNSegments() = %d. dMaxGamma = %d',i + 1,gamma,pbRes.getNSegments(),dMaxGamma));
        end;
        dAvg = dAvg + dConst;
        dMax = dMaxGamma + dConst;
        if(dAvg > infinity)
            dAvg = infinity;
        end;
        if(dMax > infinity)
            dMax = infinity;
        end;
        return;
    end;
    
    if((i>=iterationLimit-1)&&(gamma > ds))
        if(debugMode)
            disp('Iteration Limit reached');
        end;
        
        dAvg = -1;
        dMax = -1;
        return;
    end;
    
    
    
end;
if(dAvg > infinity)
    dAvg = infinity;
end;
if(dMax > infinity)
    dMax = infinity;
end;

if(debugMode)
    disp('finished regularly');
end;

