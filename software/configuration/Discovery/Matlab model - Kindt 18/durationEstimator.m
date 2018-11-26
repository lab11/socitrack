% Duration estimator - class that implements all functions used by the getDiscoveryLatency-algorithm
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




classdef durationEstimator < handle
    properties
        debugGamma = 0;
        debugParamCalculation = 0;
        floatingPointEps = 1e-10;
    end;
    methods
        
        
        
        %GROWTORIGHT
        %The GrowToRight function as described in the paper.
        %
        %Parameters:
        % pb: Input probability buffer
        % Ts: Scan interval [s]
        % ds: Scan window [s]
        % gammaN: gamma-parameter of the current order
        % sigmaN: penalty-parameter of the current order
        % nextmode: The mode of the next process.
        %          1=>shrinking, 2=> growing
        %          For the highest-order
        %          process, this value is ignored and can therefore be
        %          of arbitray value.
        %
        %Return values:
        % dGamma: Partial to the mean discovery latency
        % pbRes: Resulting probability buffer
        % dSafeHitMax: Maximum sum of penalties in the current
        % iteration, for computing the maximu discovery latency.
        
        
        function [dGamma, pbRes, dSafeHitMax] = growToRight(obj, pb, Ts, ds, gammaN, sigmaN, nextMode)
            
            dSafeHitMax = 0;
            dGamma = 0;
            pbRes = probabBuffer;
            dur = 0;
            if(obj.debugGamma)
                disp(sprintf('\n***** growToRight gammaN = %f, Ts = %f, ds = %f sigmaN = %f *****',gammaN,Ts,ds,sigmaN));
                pb
                pb.print();
            end;
            
            
            for(k = 1:pb.getNSegments)
                bL = pb.getBorderL(k);
                bR = pb.getBorderR(k);
                
                pK = pb.getProbabilityDensity(k);
                Nl = ceil((Ts - ds - bR)/gammaN);
                Nu = floor((Ts - ds - bL)/gammaN);
                
                
                dNl = bR - (Ts - ds - Nl * gammaN);
                dNu = (Ts - ds - Nu * gammaN) - bL;
                dF = bR - bL;
                
                
                %Case a) - Nu > Nl
                if(Nu >= Nl)
                    if(dNu < 0)||(dNl < 0)
                        
                        dNu
                        dNl
                        assert(dNu >= 0);
                        assert(dNl >= 0);
                    end;
                    %General Case
                    if(gammaN > ds)
                        
                        
                        if(dNu < gammaN  -  ds)
                            
                            dSafeHitMax = max(dSafeHitMax,(Nu)* sigmaN + pb.getLongestTravel(k));
                            if(nextMode == 1)
                                %shrinking
                                if(obj.debugGamma)
                                    disp('Case aA1, nextMode = s');
                                end;
                                pbRes.add(bL - Ts + (Nu + 1)*gammaN,gammaN - ds, pK, (Nu + 1)* sigmaN + pb.getLongestTravel(k));
                                A = pK * sigmaN * dNu * (Nu + 1);
                            else
                                %growing
                                if(obj.debugGamma)
                                    disp('Case aA1, nextMode = g');
                                end;
                                pbRes.add(bL + (Nu)*gammaN,Ts - ds, pK, (Nu)* sigmaN + pb.getLongestTravel(k));
                                A = pK * sigmaN * dNu * (Nu);
                            end;
                        else
                            dSafeHitMax = max(dSafeHitMax,(Nu + 1)* sigmaN + pb.getLongestTravel(k));
                            if(nextMode == 1)
                                %shrinking
                                if(obj.debugGamma)
                                    disp('Case aA2, nextmode = s');
                                end;
                                
                                pbRes.add(0, gammaN - ds, pK,(Nu + 1)* sigmaN + pb.getLongestTravel(k));
                                A = pK * sigmaN * (Nu + 1) * dNu;
                            else
                                %growing
                                if(obj.debugGamma)
                                    disp('Case aA2, nextmode = g');
                                end;
                                pbRes.add(Ts - gammaN, Ts - ds, pK,(Nu)* sigmaN + pb.getLongestTravel(k));
                                A = pK * sigmaN * ((Nu + 1)* (dNu - (gammaN - ds)) + (gammaN - ds)*Nu);
                            end;
                        end;
                        if(Nu ~= Nl)
                            dSafeHitMax = max(dSafeHitMax,Nu* sigmaN + pb.getLongestTravel(k));
                            if(nextMode == 1)
                                %shrinking
                                if(obj.debugGamma)
                                    disp('Case aB, nextmode = s');
                                end;
                                B = pK*sigmaN * gammaN * 1/2 * (Nu - Nl) * (Nu + Nl + 1);
                                pbRes.add(0, gammaN - ds, pK * (Nu - Nl),Nu * sigmaN + pb.getLongestTravel(k));
                            else
                                %growing
                                if(obj.debugGamma)
                                    disp('Case aB, nextmode = g');
                                end;
                                B = pK*sigmaN *  1/2 * (Nu - Nl) * (2*ds + gammaN * (Nu + Nl - 1)) ;
                                
                                pbRes.add(Ts - gammaN, Ts - ds, pK * (Nu - Nl),(Nu - 1) * sigmaN + pb.getLongestTravel(k));
                            end;
                        else
                            if(obj.debugGamma)
                                disp('no case aB');
                            end;
                            B = 0;
                        end;
                        if(dNl < ds)
                            if(obj.debugGamma)
                                disp('Case aC1');
                            end;
                            dSafeHitMax = max(dSafeHitMax,(Nl)* sigmaN + pb.getLongestTravel(k));
                            C = pK * dNl * Nl * sigmaN;
                        else
                            dSafeHitMax = max(dSafeHitMax,(Nl)* sigmaN + pb.getLongestTravel(k));
                            if(nextMode == 1)
                                %shrinking
                                if(obj.debugGamma)
                                    disp('Case aC2, nextmode = s');
                                end;
                                pbRes.add(0, bR - Ts + Nl * gammaN, pK,(Nl)* sigmaN + pb.getLongestTravel(k));
                                C = pK * sigmaN * Nl * dNl;
                            else
                                %growing
                                if(obj.debugGamma)
                                    disp('Case aC2, nextmode = g');
                                end;
                                pbRes.add(Ts - gammaN, bR + (Nl-1) * gammaN, pK,(Nl - 1)* sigmaN + pb.getLongestTravel(k));
                                %                                C = pK * sigmaN *  (ds * Nl + (dNl - ds)*(Nl - 1));
                                C = pK * sigmaN * (ds + dNl * (Nl - 1));
                            end;
                        end;
                        dur = A + B + C;
                    else
                        % gammaN < ds   - algorithm ends here
                        if(obj.debugGamma)
                            disp('Case aA NonFt');
                        end;
                        if(dNu < 0)||(dNl < 0)
                            
                            dNu
                            dNl
                            assert(dNu >= 0);
                            assert(dNl >= 0);
                        end;
                        
                        
                        if(dNu > obj.floatingPointEps)
                            dSafeHitMax = max(dSafeHitMax,(Nu + 1)* sigmaN + pb.getLongestTravel(k));
                        else
                            dSafeHitMax = max(dSafeHitMax,Nu* sigmaN + pb.getLongestTravel(k));
                        end;
                        dur = pK * sigmaN *( ...
                            dNl * Nl +...
                            gammaN * 1/2 * (Nu - Nl) * (Nu + Nl + 1) +...
                            dNu * (Nu + 1)...
                            );
                    end;
                    
                    
                    
                    
                    
                    
                    
                    %Case c)
                else
                    if(gammaN > ds)
                        if(bL - (Ts - ds - Nl * gammaN) <= ds)
                            %dur = (bL - (Ts - gammaN))*Nl * sigmaN;
                            
                            if(dNl <= ds)
                                %both borders lie within hit zone
                                dSafeHitMax = max(dSafeHitMax,(Nl)* sigmaN + pb.getLongestTravel(k));
                                dur = pK * Nl * sigmaN * (bR - bL);
                                if(obj.debugGamma)
                                    disp('Case c1 NonFt');
                                end;
                            else
                                %left border in hit zone, right border not
                                dSafeHitMax = max(dSafeHitMax,(Nl)* sigmaN + pb.getLongestTravel(k));
                                if(nextMode == 1)
                                    %shrinking
                                    if(obj.debugGamma)
                                        disp(sprintf('Case c2) nextMode = s - Adding [%f,%f] to resulting ProbabBuffer',0, bR - Ts + Nl*gammaN));
                                    end;
                                    dur = pK* sigmaN * Nl * (bR - bL);
                                    pbRes.add(0, bR - Ts + Nl*gammaN,pK,(Nl)* sigmaN + pb.getLongestTravel(k));
                                else
                                    %growing
                                    if(obj.debugGamma)
                                        disp(sprintf('Case c2) nextMode = g - Adding [%f,%f] to resulting ProbabBuffer',0, bR - Ts + Nl*gammaN));
                                    end;
                                    dur = pK* sigmaN * (Nl * (ds - gammaN + dNu) + (Nl - 1)*(dNl - ds));
                                    pbRes.add(Ts  - gammaN, bR  + (Nl - 1)*gammaN,pK,(Nl - 1)* sigmaN + pb.getLongestTravel(k));
                                    
                                end;
                            end;
                        else
                            %none of the borders lie in the hit zone
                            
                            dSafeHitMax = max(dSafeHitMax,(Nl)* sigmaN + pb.getLongestTravel(k));
                            if(nextMode == 1)
                                %shrinking
                                dur = pK * sigmaN * (bR - bL) * Nl;
                                pbRes.add(bL - Ts + Nl*gammaN, bR - Ts + Nl*gammaN,pK,(Nl)* sigmaN + pb.getLongestTravel(k));
                                if(obj.debugGamma)
                                    disp(sprintf('Case c3) nextMode = s: Adding [%f,%f] to resulting ProbabBuffer',bL - Ts + Nl*gammaN, bR - Ts + Nl*gammaN));
                                end;
                            else
                                %growing
                                if(obj.debugGamma)
                                    disp(sprintf('Case c3) nextMode = g: Adding [%f,%f] to resulting ProbabBuffer',bL - Ts + Nl*gammaN, bR - Ts + Nl*gammaN));
                                end;
                                dur = pK * sigmaN * (bR - bL) * (Nl);
                                pbRes.add(bL + Nu*gammaN, bR + (Nl-1)*gammaN,pK,(Nl - 1)* sigmaN + pb.getLongestTravel(k));
                                
                            end;
                        end;
                    else
                        if(obj.debugGamma)
                            disp('Case c4 NonFt');
                        end;
                        dSafeHitMax = max(dSafeHitMax,(Nl)* sigmaN + pb.getLongestTravel(k));
                        dur = pK * (bR - bL) * Nl * sigmaN;
                    end;
                end;
                
                
                
                if(obj.debugGamma)
                    clf;
                    hold on;
                    pb.plot;
                    if(Nu > Nl)
                        for(i = Nl:Nu)
                            line([Ts - ds - i*gammaN, Ts - ds - i*gammaN],[-0.2,max(pb.probabilityDensities) + 0.2],'color',[1,0,0],'LineWidth',3);
                            text((Ts - ds - i*gammaN) + gammaN/2,max(pb.probabilityDensities)/2 - 0.2,sprintf('i = %d/%d',i,Nu));
                            if(i > 100)
                                break;
                            end;
                        end;
                        line([Ts - ds - Nl*gammaN, Ts - ds - Nl*gammaN - (Nu - Nl)*gammaN],[0,0], 'color',[1,0,0],'LineWidth', 3,'LineStyle',':');
                        
                    else
                        for(i = Nu:Nl)
                            line([Ts - ds - i*gammaN, Ts - ds - i*gammaN],[-0.2,max(pb.probabilityDensities) + 0.2],'color',[1,0,0],'LineWidth',3);
                            text((Ts - ds - i*gammaN) + gammaN/2,max(pb.probabilityDensities)/2 - 0.2,sprintf('i = %d/%d',i,Nl));
                            if(i > 100)
                                break;
                            end;
                            
                        end;
                        
                    end;
                    text((bR + bL) / 2,max(pb.probabilityDensities) +0.1,'growToRight');
                    text(Ts - ds - Nu*gammaN,-0.1,'Nu');
                    text(Ts - ds - Nl*gammaN,-0.3,'Nl');
                    text((bR + bL)/2,max(pb.probabilityDensities)/2,sprintf('k=%d, gammaN = %f',k,gammaN));
                    line([Ts - ds - Nl*gammaN + dNl, Ts - ds - Nl*gammaN],[0.05,0.05], 'color',[0.5,0.5,0.5],'LineWidth', 5);
                    line([Ts - ds - Nu*gammaN - dNu, Ts - ds - Nu*gammaN],[0,0], 'color',[0,0,0],'LineWidth', 5);
                    
                    hold off;
                    
                    dur
                    input('-(growToRight)--');
                end;
                dGamma = dGamma + dur;
                
            end;
            
            if(obj.debugGamma)
                disp('*** Final aftergamma distribution ***');
                clf;
                pbRes.plot();
                pbRes.print();
                dGamma
                dSafeHitMax
                if(length(pb.probabilityDensities) > 0)
                    
                    line([gammaN - ds,gammaN-ds],[-0.1, max(pbRes.probabilityDensities) + 0.1],'color',[1,0,0],'LineWidth',3,'LineStyle',':');
                    line([Ts - ds,Ts-ds],[-0.1, max(pbRes.probabilityDensities) + 0.1],'color',[1,0,0],'LineWidth',3,'LineStyle',':');
                    text((gammaN - ds)/2, max(pb.probabilityDensities),'Final aftergamma Distribution');
                else
                    disp('ProbabBuffer Empty - no plot');
                end;
                gammaN
                gammaN - ds
                ds
                input('---');
            end;
        end;
        
        %SHRINKTOLEFT
        %shrinkToLeft function as described in the paper.
        %
        %Parameters:
        % pb: Input probability buffer
        % Ts: Scan interval [s]
        % ds: Scan window [s]
        % gammaN: gamma-parameter of the current order
        % sigmaN: penalty-parameter of the current order
        % nextmode: The mode of the next process.
        %          1=>shrinking, 2=> growing
        %          For the highest-order
        %          process, this value is ignored and can therefore be
        %          of arbitray value.
        %
        %Return values:
        % dGamma: Partial to the mean discovery latency
        % pbRes: Resulting probability buffer
        % dSafeHitMax: Maximum sum of penalties in the current
        % iteration, for computing the maximu discovery latency.
        function [dGamma, pbRes, dSafeHitMax] = shrinkToLeft(obj, pb, Ts, ds, gammaN, sigmaN, nextMode)
            
            dGamma = 0;
            pbRes = probabBuffer;
            dSafeHitMax = 0;
            if(obj.debugGamma)
                disp(sprintf('\n***** shrinkToLeft gammaN = %f, Ts = %f, ds = %f *****',gammaN,Ts,ds));
                
            end;
            
            
            for(k = 1:pb.getNSegments)
                bL = pb.getBorderL(k);
                bR = pb.getBorderR(k);
                
                pK = pb.getProbabilityDensity(k);
                Nl = ceil((bL)/gammaN);
                Nu = floor((bR)/gammaN);
                
                dNl = Nl*gammaN - bL;
                dNu = bR - Nu*gammaN;
                dF = bR - bL;
                
                
                
                
                %Case a) - Nu > Nl
                if(Nu >= Nl)
                    %General Case
                    if(gammaN > ds)
                        if(dNl < ds)
                            %left part only hits
                            if(obj.debugGamma)
                                disp('case a/b 1)');
                            end;
                            dSafeHitMax = max(dSafeHitMax,(Nl)* sigmaN + pb.getLongestTravel(k));
                            A = pK * sigmaN * Nl * dNl;
                        else
                            dSafeHitMax = max(dSafeHitMax, Nl * sigmaN + pb.getLongestTravel(k));
                            %there is a hitting part and a missing part
                            if(nextMode == 1)
                                %shrinking
                                if(obj.debugGamma)
                                    disp('case a/b  2) nextMode=s');
                                end;
                                A = pK * sigmaN * (ds * Nl + (dNl - ds) * (Nl - 1));
                                pbRes.add(gammaN - dNl, gammaN - ds,pK,(Nl - 1) * sigmaN + pb.getLongestTravel(k));
                            else
                                %growing
                                if(obj.debugGamma)
                                    disp('case a/b  2) nextMode=g');
                                end;
                                A = pK * sigmaN * dNl * Nl;
                                pbRes.add(Ts - dNl, Ts - ds,pK,Nl * sigmaN + pb.getLongestTravel(k));
                            end;
                            
                        end;
                        
                        if(Nu > Nl)
                            dSafeHitMax = max(dSafeHitMax, Nu * sigmaN + pb.getLongestTravel(k));
                            if(nextMode == 1)
                                %shrink
                                %                                B = pK * sigmaN * (ds  * 1/2 * (Nu - Nl) * (Nl + Nu + 1) + (gammaN - ds)  * 1/2 * (Nu - Nl) * (Nl + Nu - 1));
                                B = 1/2 * pK * sigmaN * ((Nu - Nl) * (2 * ds + gammaN * (Nu + Nl - 1)));
                                pbRes.add(0, gammaN - ds,pK * (Nu - Nl),(Nu - 1)* sigmaN + pb.getLongestTravel(k));
                            else
                                %grow
                                B = pK * sigmaN * (gammaN)  * 1/2 * (Nu - Nl) * (Nl + Nu + 1);
                                pbRes.add(Ts - gammaN, Ts - ds,pK * (Nu - Nl),Nu * sigmaN + pb.getLongestTravel(k));
                            end;
                        else
                            assert(Nu == Nl)
                            B = 0;
                        end;
                        if(dNu < gammaN - ds)
                            %no part hits
                            if(obj.debugGamma)
                                disp('case a/b 3)');
                            end;
                            dSafeHitMax = max(dSafeHitMax, Nu * sigmaN + pb.getLongestTravel(k));
                            if(nextMode == 1)
                                %shrink
                                if(obj.debugGamma)
                                    disp('case a/b 4), nextMode = s');
                                end;
                                C = pK * sigmaN * (Nu) * dNu;
                                pbRes.add(0, dNu,pK,Nu * sigmaN + pb.getLongestTravel(k));
                            else
                                %grow
                                if(obj.debugGamma)
                                    disp('case a/b 4), nextMode = g');
                                end;
                                C = pK * sigmaN * (Nu + 1) * dNu;
                                pbRes.add(Ts - gammaN, Ts - gammaN + dNu,pK,(Nu + 1) * sigmaN + pb.getLongestTravel(k));
                            end;
                        else
                            %one part misses, one part hits
                            dSafeHitMax = max(dSafeHitMax, (Nu + 1) * sigmaN + pb.getLongestTravel(k));
                            if(nextMode == 1)
                                %shrink
                                if(obj.debugGamma)
                                    disp('case a/b 5), nextMode = s');
                                end;
                                C = pK * sigmaN * ((gammaN - ds) * (Nu) + (dNu - (gammaN - ds)) * (Nu + 1));
                                pbRes.add(0, gammaN - ds,pK,Nu * sigmaN + pb.getLongestTravel(k));
                            else
                                %grow
                                if(obj.debugGamma)
                                    disp('case a/b 6), nextMode = g');
                                end;
                                C = pK * sigmaN * (Nu + 1) * dNu;
                                pbRes.add(Ts - gammaN, Ts - ds,pK,(Nu + 1) * sigmaN + pb.getLongestTravel(k));
                                
                            end;
                        end;
                        dur = A + B + C;
                        
                        % gammaN < ds   - algorithm ends here
                    else
                        if(obj.debugGamma)
                            disp('case a  NonFT');
                        end;
                        
                        
                        
                        if(dNu > obj.floatingPointEps)
                            dSafeHitMax = max(dSafeHitMax, (Nu + 1)* sigmaN + pb.getLongestTravel(k));
                        else
                            dSafeHitMax = max(dSafeHitMax, (Nu)* sigmaN + pb.getLongestTravel(k));
                        end;
                        
                        dur = pK * sigmaN *( ...
                            dNl * Nl +...
                            gammaN * 1/2 * (Nu - Nl) * (Nu + Nl + 1) +...
                            dNu * (Nu + 1)...
                            );
                    end;
                    
                    
                    
                    
                    
                    
                    
                    %Case C)
                else
                    if(gammaN > ds)
                        %                       disp('case C');
                        if(Nl * gammaN - bR >= ds)
                            %rhigh part is out of hitting area (and therefore left part as well)
                            dSafeHitMax = max(dSafeHitMax, (Nl)* sigmaN + pb.getLongestTravel(k));
                            
                            if(nextMode == 1)
                                %shrink
                                if(obj.debugGamma)
                                    disp('case C1, nextMode = s');
                                end;
                                dur = pK * (Nl - 1)*  sigmaN * (bR - bL);
                                pbRes.add(gammaN - dNl,dNu, pK,(Nl - 1)* sigmaN + pb.getLongestTravel(k));
                            else
                                %grow
                                if(obj.debugGamma)
                                    disp('case C1, nextMode = g');
                                end;
                                dur = pK * Nl *  sigmaN * (bR - bL);
                                pbRes.add(Ts - dNl,dNu + Ts - gammaN, pK,(Nl)* sigmaN + pb.getLongestTravel(k));
                            end;
                        else
                            %right part is in hitting area
                            if(dNl >= ds)
                                %right part is in hitting area and left part is out of hitting area
                                dSafeHitMax = max(dSafeHitMax, (Nl)* sigmaN + pb.getLongestTravel(k));
                                if(nextMode == 1)
                                    %shrink
                                    if(obj.debugGamma)
                                        disp('case C2, nextmode = s');
                                    end;
                                    dur = pK * sigmaN * ((dNl - ds) * (Nl - 1) + (ds - gammaN + dNu) * (Nl));
                                    pbRes.add(gammaN - dNl,gammaN - ds, pK,(Nl-1)* sigmaN + pb.getLongestTravel(k));
                                else
                                    %grow
                                    if(obj.debugGamma)
                                        disp('case C2, nextmode = g');
                                    end;
                                    dur = pK * Nl *  sigmaN * (bR - bL);
                                    pbRes.add(Ts - dNl,Ts - ds, pK,(Nl)* sigmaN + pb.getLongestTravel(k));
                                    
                                end;
                            else
                                %right part is in hitting area and left part is also in hitting area
                                dSafeHitMax = max(dSafeHitMax, (Nl)* sigmaN + pb.getLongestTravel(k));
                                dur = sigmaN * Nl * pK * dF;
                                if(obj.debugGamma)
                                    disp(sprintf('case C3 NonFt shiftBy = %d',Nl));
                                end;
                                
                            end;
                        end;
                    else
                        if(obj.debugGamma)
                            disp('case C NonFT');
                        end;
                        dSafeHitMax = max(dSafeHitMax, (Nl)* sigmaN + pb.getLongestTravel(k));
                        dur = pK * dF * Nl * sigmaN;
                    end;
                end;
                
                
                if(obj.debugGamma)
                    clf;
                    hold on;
                    pb.plot;
                    if(Nu > Nl)
                        for(i = Nl:Nu)
                            line([i*gammaN, i*gammaN],[-0.2,max(pb.probabilityDensities) + 0.2],'color',[1,0,0],'LineWidth',3);
                            text((i*gammaN) - gammaN/2,max(pb.probabilityDensities)/2 - 0.2,sprintf('i = %d/%d',i,Nu));
                        end;
                        line([Nl*gammaN, Nl*gammaN + (Nu - Nl)*gammaN],[0,0], 'color',[1,0,0],'LineWidth', 3,'LineStyle',':');
                        
                    else
                        for(i = Nu:Nl)
                            line([i*gammaN, i*gammaN],[-0.2,max(pb.probabilityDensities) + 0.2],'color',[1,0,0],'LineWidth',3);
                            
                            text((i*gammaN) + gammaN/2,max(pb.probabilityDensities)/2 - 0.2,sprintf('i = %d/%d',i,Nl));
                        end;
                        
                    end;
                    text((bR + bL) / 2,max(pb.probabilityDensities) +0.1,'shrinkToLeft');
                    text(Nu*gammaN,-0.1,'Nu');
                    text(Nl*gammaN,-0.3,'Nl');
                    text((bR + bL)/2,max(pb.probabilityDensities)/2,sprintf('k=%d, gammaN = %f',k,gammaN));
                    line([Nl*gammaN - dNl, Nl*gammaN],[0.05,0.05], 'color',[0.5,0.5,0.5],'LineWidth', 5);
                    line([Nu*gammaN + dNu, Nu*gammaN],[0,0], 'color',[0,0,0],'LineWidth', 5);
                    
                    hold off;
                    dur
                    input('-(shrinkToLeft)--');
                end;
                dGamma = dGamma + dur;
                
            end;
            
            if(obj.debugGamma)
                disp('*** [shrinkToLeft] Final aftergamma distribution ***');
                clf;
                pbRes.plot();
                pbRes.print();
                line([Ts - ds - gammaN,Ts - ds - gammaN],[-0.1, max(pbRes.probabilityDensities) + 0.1],'color',[1,0,0],'LineWidth',3,'LineStyle',':');
                line([Ts - ds,Ts - ds],[-0.1, max(pbRes.probabilityDensities) + 0.1],'color',[1,0,0],'LineWidth',3,'LineStyle',':');
                line([Ts,Ts],[-0.1, max(pbRes.probabilityDensities) + 0.1],'color',[1,0,0],'LineWidth',3,'LineStyle','-');
                if(pb.getNSegments() > 0)
                    text((gammaN - ds)/2, max(pb.probabilityDensities),'Final aftergamma Distribution');
                else
                    text((gammaN - ds)/2, 0,'Final aftergamma Distribution');
                    
                end;
                xlim([-ds, Ts + ds]);
                gammaN
                gammaN - ds
                ds
                dSafeHitMax
                disp('Whole duration stats:');
                disp(sprintf('dGamma = %f',dGamma));
                input('---');
            end;
        end;
        
        
        
        
        %GETNEXTPARAMETERS Implements the recursion scheme for
        %computing gammaN given gammaN-1 as presented in the paper.
        % The core of the recursion has been outsourced into
        % paritialtravel, whereas getNexstParameters() contains the
        % control logic of the recursion.
        % It also computes the nexdt penalty, the next mode, the next
        % distance left to travel (denoted as dtn in the paper), and the next penalty sum (denoted as sigmaS in
        % the paper)
        %
        % Parameters:
        % Ta: Advertising interval [s]
        % Ts: Scan interval [s]
        % gammaVector:   Vector of gammas containing all values for all
        %                lower-order processes as follows [Ta, gamma1, gamma2,...,gamma_n-1]
        % modevector:    Vector containing the modes of all lower-order processes.
        %                The first mode needs to be 2 if Ta < Ts
        % penaltyVector: Vector containing the penalties of all
        %                lower-order processes
        % dist:          Distance left to travel from the previous
        %                iteration [s]
        % penaltySum:    penaltySum (sigmaS) from the previous
        %                iteration
        % order:         Counting index denoting the order of the
        %                current calculation
        %
        %Return Values:
        % gammaN:       Next value of the gamma parameter
        % penaltyN:     Next value of the penalty parameter
        % modeN:        Next mode
        % dist:         Next distance left to travel
        % penaltySum:   Next penalty sum
        
        function [gammaN,penaltyN, modeN,dist, penaltySum] = getNextParameters(obj, Ta, Ts, gammaVector,penaltyVector,modeVector,dist, penaltySum,order)
            if(obj.debugParamCalculation)
                disp('--- getNextParameters ---');
                
            end;
            
            if(modeVector(end) == 1)
                if(dist - floor(abs(dist)/gammaVector(end))*gammaVector(end) < 0.5*gammaVector(end))
                    modeNext = 2;
                else
                    modeNext = 1;
                end;
            else
                if(dist - floor(abs(dist)/gammaVector(end))*gammaVector(end) > 0.5*gammaVector(end))
                    modeNext = 2;
                else
                    modeNext = 1;
                end;
            end;
            distPrev = dist;
            [gammaNext, dist, penaltySum, penaltyNext] = obj.partialTravel(dist, gammaVector(end),modeVector(end), modeNext, penaltyVector(end), penaltySum);
            assert(abs(penaltyNext/Ta- round(penaltyNext/Ta)) <1e-6);
            % assert(penaltyNext/Ta > ceilpenaltyVector(end)
            if(checkFloatingPointEqual(gammaNext,gammaVector(end))||(checkFloatingPointEqual(gammaNext,0)))
                modeN = 3;
            else
                modeN = modeNext;
            end;
            
            gammaN = gammaNext;
            penaltyN = penaltyNext;
            
            if(penaltyVector(end) ~= Ta)
                assert(penaltyVector(end) < penaltyNext);
            end;
            if(modeNext ~= 3)&&(modeN ~= 3)
                if(distPrev <= dist)
                    distPrev
                    dist
                    gammaVector
                    modeNext
                    modeN
                    checkFloatingPointEqual(gammaNext,0)
                end;
                
                assert(distPrev > dist);
                assert(gammaNext < gammaVector(end));
            end;
            if(obj.debugParamCalculation)
                disp(sprintf('[### getNextStage Result] gammaNext = %f, penaltySum = %f, penaltyNext = %f, modeNext= %f\n',gammaN,penaltySum, penaltyNext, modeN));
            end;
        end;
        
        
        %PARTIALTRAVEL Implements the recursion scheme for
        % computing gammaN given gammaN-1 as presented in the paper.
        % See also: getNextParameter
        %
        % Parameters:
        % dist:          Distance left to travel fromt the previous
        %                call of this function
        % gammaN:        Current value of gamma
        % mode:          Mode of the current process (1=> shrinking, 2=> growing).
        % modeNext:      Mode of the next higher-order process. For the
        %                highest order process, this value can be
        %                choosen arbitrarily.
        % penaltyN:      penalty of the current process
        % penaltySum:    PenaltySum of the current process (sigmaS in
        %                the paper)
        %
        %Return Values:
        % gammaNext:    Next value of the gamma parameter
        % penaltyNext:  Next value of the penalty parameter
        % dist:         Next distance left to travel
        % penaltySum:   Next penalty sum
        function [gammaNext, dist, penaltySum, penaltyNext] = partialTravel(obj, dist, gammaN, mode, modeNext, penaltyN, penaltySum)
            N = floor(dist/gammaN);
            assert(N > 0);
            penaltyNext = 0;
            distPrev = dist;
            if(obj.debugParamCalculation)
                disp(sprintf('Initial penaltySum: %f - penaltyN = %f\n',penaltySum, penaltyN));
            end;
            if(mode == modeNext)
                %Growing => Growing or Shrinking => Shrinking
                gammaNext = (N+1)*gammaN - dist;
                dist = dist - N*gammaN;
                penaltyNext = penaltySum + (N +1) * penaltyN;
                penaltySum = penaltySum + (N) * penaltyN;
                if(obj.debugParamCalculation)
                    disp(sprintf('*** Growing -> Growing, N = %d, dist=%f, penaltyNext = %f, penaltySum = %f',N,dist,penaltyNext, penaltySum));
                end;
                
            else
                %Shrinking => Growing or Growing => Shrinking
                gammaNext = dist - N * gammaN;
                dist = (N+1)*gammaN  - dist;
                penaltyNext = penaltySum + (N)* penaltyN;
                penaltySum = penaltySum + (N+1) * penaltyN;
                if(obj.debugParamCalculation)
                    disp(sprintf('*** Shrinking -> Growing, N = %d, dist=%f, penaltyNext = %f, penaltySum = %f\n',N,dist,penaltyNext, penaltySum));
                end;
            end;
            if(checkFloatingPointEqual(gammaN, gammaNext)||checkFloatingPointEqual(gammaNext,0))
                modeNext = 3;
            end;
            if(modeNext ~= 3)&&(mode ~= 3)
                
                assert(gammaNext < gammaN);
                if(distPrev <= dist)
                    distPrev
                    dist
                    gammaN
                    gammaNext
                    mode
                    modeNext
                    checkFloatingPointEqual(gammaNext,0)
                    
                end;
                
                assert(distPrev > dist);
            end;
        end;
        %GETMAXORDER
        %Computes the highest possible order nm, as described in the Paper
        function [maxOrder] = getMaxOrder(Ta,Ts,ds)
            if(min(Ta,Ts) < ds)
                maxOrder = 0;
            else
                maxOrder = ceil((log(min(Ta,Ts))-log(ds))/log(2));
            end;
        end;
        
    end;
    
end
