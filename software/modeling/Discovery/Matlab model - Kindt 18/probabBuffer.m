% Probability Buffer Class
% Philipp H. Kindt <philipp.kindt@tum.de>
%
% This class provides a flexible (yet, computational not very efficient) representation of piecewise continuous proability density functions.
% New segments can be added on top of others easily. The number of indices is sorted in ascending order
%
% --- Usage example ---
% p = probabBuffer;
% p.add(1,4,1);
% p.add(2,3,1);
% p.plot();
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

classdef probabBuffer < matlab.mixin.Copyable
    properties (SetAccess = private)
        bordersLeft;
        bordersRight;
        probabilityDensities;
        debug = 0;
        floatingPointEps = 1e-15;
        longestDistances;
    end;
    methods        
        function reset(obj)
        %Resets everything to its defaults (empty)
            obj.probabilityDensities = [];
            obj.bordersLeft = [];
            obj.bordersRight = [];
            obj.longestDistances = [];
            if(obj.debug)
                disp('PB reset');
            end;
        end
        
        
        function border = getBorderL(obj,i)
        %Get left border of segment i
            border = obj.bordersLeft(i);
        end;
        
        function border = getBorderR(obj,i)
            %Get right border of segment i
            border = obj.bordersRight(i);
        end;
        
        function dist = getLongestTravel(obj,i)
            %Get the longest distance traveled for a particular segment
            dist = obj.longestDistances(i);
        end;
        
        function dist = getMaxLongestTravel(obj)
            %get the longest distance traveled for all segments
            dist = max([0,obj.longestDistances]);          
        end;
        
        function resetLongestTravel(obj)
            %reset the longest distance traveled to zero for all segments
            obj.longestDistances = zeros(1,length(obj.longestDistances));
        end;
           
        function probability = getProbability(obj,i)
            %Get probability of segment i (i.e., the probability density
            %multiplied with the segment width)
            probability = obj.probabilityDensities(i) * (obj.bordersRight(i) - obj.bordersLeft(i));
        end;

        function probabilityDensity = getProbabilityDensity(obj,i)
            %Get probabilityDensity density in segment i (
            probabilityDensity = obj.probabilityDensities(i);
        end;

        
        function len = getNSegments(obj,i)
            % get total number of segments in probabilityDensity buffer
            if(length(obj.probabilityDensities) ~= length(obj.bordersLeft))||(length(obj.probabilityDensities) ~= length(obj.bordersRight))
                obj.bordersLeft
                obj.bordersRight
                obj.probabilityDensities
                obj.print();
                assert(2==1)
            end;
            len = length(obj.probabilityDensities);
        end;
        
        function totalProbab = getTotalProbab(obj)
            %return the integral over all probabilities in the buffer
            totalProbab = (obj.bordersRight - obj.bordersLeft) * transpose(obj.probabilityDensities);
        end;

        
        
        function add(obj,borderLeft, borderRight, probabilityDensity, varargin)
            %add a segment spanning from borderLeft to BorderRight having the PDF probabilityDensity 
            if(length(varargin) == 0)
                travelDistance = 0;
            else
                travelDistance = varargin{1};
            end;
            
              
            if(obj.debug)
                disp(sprintf('adding: [%d, %d] <- %d    dist %f\n',borderLeft, borderRight, probabilityDensity, travelDistance));
            end;
            if(borderLeft > borderRight);
                disp('borderLEft > borderRight!??');
                return;
            end;
            indexSplitL = 0;
            indexSplitR = 0;
            indicesAdjust = [];
            indicesAdd = [];
            bordersAddL = [];
            bordersAddR = [];
            probabilityDensitiesAdd = [];
            travelDistancesAdd = [];
            indicesAddPrioLow = [];
            bordersAddLPrioLow = [];
            bordersAddRPrioLow = [];
            probabilityDensitiesAddPrioLow = [];
            probabilityDensitiesAdjust = [];
            travelDistancesAdd = [];
            travelDistancesAddPrioLow = [];
            travelDistanceAdjust = [];
            travelDistanceSplit = 0;
            if(isempty(obj.bordersLeft))
                
                %------------------------------------- [ Empty Fill ] -------------------------------------------------------------------------
                %The probabBuffer is empty and this is the first entry
                obj.bordersLeft(1) = borderLeft;
                obj.bordersRight(1) = borderRight;
                obj.probabilityDensities(1) = probabilityDensity;
                obj.longestDistances(1) = travelDistance;
                return;
            end;
            
            tmp1 = find((obj.bordersLeft <= borderLeft)&(obj.bordersRight <= borderLeft));
            tmp2 = find((obj.bordersLeft >= borderRight)&(obj.bordersRight >= borderLeft));
            tmp3 = find((borderRight < obj.bordersRight)&(borderLeft > obj.bordersLeft));

            %------ [ Check if this is a new, freestanding segment ]-------------------------------------------
            if(length(tmp1)+length(tmp2) == length(obj.probabilityDensities))
                indicesAdd = min(find(borderLeft < obj.bordersLeft));
                if(isempty(indicesAdd))
                    if(obj.debug)
                        disp('new/freestanding');
                    end;
                    indicesAdd(1) = length(obj.bordersLeft) + 1;
                end;
                bordersAddL(1) = borderLeft;
                bordersAddR(1) = borderRight;
                probabilityDensitiesAdd(1) = probabilityDensity;     
                travelDistancesAdd(1) = travelDistance;

            %------[ Check it fully lies within another one ]---------------------------------------------------
            elseif(length(tmp3)==1)
                  if(obj.debug)
                        disp('fully within other one => split left and right');
                  end;

                indexSplitL = tmp3(1);
                indexSplitR = indexSplitL;
           %------[ Check if the borders are equivalent to an existing one]---------------------------------------------------

            
            %-----[ weired case where multiple segments might lie within the new segment ]----------------------
            else
                  if(obj.debug)
                        disp('multiple segments lie within the newly added one');
                  end;

                %1) find fully affected ones and adjust them
                inbetween = find((obj.bordersLeft >= borderLeft)&(obj.bordersRight <= borderRight));
                if(~isempty(inbetween))
                    indicesAdjust = inbetween;
                    probabilityDensitiesAdjust = probabilityDensity;
                    travelDistanceAdjust = travelDistance;
                    
                end; 
                
                %2) find partial affected ones and split (max. 2!)
                affectedL = find((obj.bordersLeft < borderLeft)&(obj.bordersRight > borderLeft));
                affectedR = find((obj.bordersLeft < borderRight)&(obj.bordersRight > borderRight));
                if(isempty(affectedL))
                    indexSplitL = 0;
                else
                    indexSplitL = affectedL;
                end;
                if(isempty(affectedR))
                    indexSplitR = 0;
                else
                    indexSplitR = affectedR;                   
                end;
                travelDistanceSplit = travelDistance;
                %3) find gaps to be filled
                %3a) Gaps before the lefternmost existing segment
                if(indexSplitL == 0)
                    if(~isempty(inbetween))
                        leftNeighbor = inbetween(1);
                    else
                        if(indexSplitR ~= 0)
                            leftNeighbor = indexSplitR;
                        else
                            disp('Error: indexSplitL and indexSplitR is zero but not adding new segment');
                            assert(1==0);
                        end;
                    end;
                    indicesAdd(end+1) = leftNeighbor;
                    bordersAddL(end+1) = borderLeft;
                    bordersAddR(end+1) = obj.bordersLeft(leftNeighbor);
                    probabilityDensitiesAdd(end + 1) = probabilityDensity;
                    travelDistancesAdd(end + 1) = travelDistance;

                end;
                %3b: Gaps betwen indexSplitL and inbetween(1)
                if(obj.debug)
                    disp(sprintf('indexSplitL = %d, inbetween = %d, indexSplitR = %d\n',indexSplitL,inbetween,indexSplitR));
                end;
                if(indexSplitL ~= 0)&&(length(inbetween) ~= 0)
                    indicesAddPrioLow(end+1) = inbetween(1);
                    bordersAddLPrioLow(end+1) = obj.bordersRight(indexSplitL);
                    bordersAddRPrioLow(end+1) = obj.bordersLeft(inbetween(1));
                    probabilityDensitiesAddPrioLow(end + 1) = probabilityDensity;
                    travelDistancesAddPrioLow(end+1) = travelDistance;

                elseif(indexSplitL ~= 0)&&(length(inbetween) == 0)&&(indexSplitR ~= 0)
                    indicesAddPrioLow(end+1) = indexSplitR;
                    bordersAddLPrioLow(end+1) = obj.bordersRight(indexSplitL);
                    bordersAddRPrioLow(end+1) = obj.bordersLeft(indexSplitR);
                    probabilityDensitiesAddPrioLow(end + 1) = probabilityDensity;
                    travelDistancesAddPrioLow(end+1) = travelDistance;

                end;

                %3c) Gaps in between existing segments
                if(length(inbetween) > 1)
                    for(i = inbetween(1):(inbetween(end) - 1))
                        indicesAdd(end+1) = i + 1;
                        bordersAddL(end + 1) = obj.bordersRight(i);
                        bordersAddR(end + 1) = obj.bordersLeft(i + 1);
                        probabilityDensitiesAdd(end + 1) = probabilityDensity;
                        travelDistancesAdd(end+1) = travelDistance;

                    end;
                end;

                %3d) Gaps between the righternmost inbetween segment and
                %indexSplitR               
               if(indexSplitR ~= 0)&&(length(inbetween) ~= 0)
                    indicesAddPrioLow(end+1) = indexSplitR;
                    bordersAddLPrioLow(end+1) = obj.bordersRight(inbetween(end));
                    bordersAddRPrioLow(end+1) = obj.bordersLeft(indexSplitR);
                    probabilityDensitiesAddPrioLow(end + 1) = probabilityDensity;
                    travelDistancesAddPrioLow(end + 1) = travelDistance;

               end;

               %3e) Gaps after the righternmost inbetween segment 
                
               if(indexSplitR == 0)
                     if(~isempty(inbetween))
                        rightNeighbor = inbetween(end);
                    else
                        rightNeighbor = indexSplitL;
                    end;
                    indicesAddPrioLow(end+1) = rightNeighbor + 1;
                    bordersAddLPrioLow(end+1) = obj.bordersRight(rightNeighbor);
                    bordersAddRPrioLow(end+1) = borderRight;
                    probabilityDensitiesAddPrioLow(end + 1) = probabilityDensity;
                    travelDistancesAddPrioLow(end + 1) = travelDistance;

               end;

               
            end;
            processModifications(obj,borderLeft, borderRight,probabilityDensity, indicesAdjust, probabilityDensitiesAdjust, indexSplitL, indexSplitR, indicesAdd, bordersAddL, bordersAddR, probabilityDensitiesAdd, indicesAddPrioLow, bordersAddLPrioLow, bordersAddRPrioLow, probabilityDensitiesAddPrioLow, travelDistanceSplit, travelDistancesAdd, travelDistancesAddPrioLow, travelDistanceAdjust);
                     
        end;
        
        
        function processModifications(obj,borderLeft, borderRight, probabilityDensity, indicesAdjust, probabilityDensitiesAdjust, indexSplitL, indexSplitR, indicesAdd, bordersAddL, bordersAddR, probabilityDensitiesAdd, indicesAddPrioLow, bordersAddLPrioLow, bordersAddRPrioLow, probabilityDensitiesAddPrioLow, travelDistanceSplit, travelDistancesAdd, travelDistancesAddPrioLow, travelDistanceAdjust)
        %Do the processing of buffer modifications. This is: 
        % - adding new segments (indicesAdd, bordersAddL, bordersAddR, probabilitiyDensitiesAdd, ...)
        % - split up to two segments (indexSplitL, indexSplitR)
        % - adjust the PDF of a given amount of segments (indicesAdjust, probabilityDensitiesAdjust)

              %------------------------------------- [ Do the processing!] --------------------------------------------------
            
%              indexSplitL
%              indexSplitR
%              borderLeft
%              borderRight
%              travelDistanceSplit
%              travelDistancesAdd
%              travelDistanceAdjust
%              indicesAdjust
%              indicesAdd
%              indicesAddPrioLow
%              bordersAddLPrioLow
%              bordersAddRPrioLow
%              travelDistancesAddPrioLow
            %------[Prevent cases where two borders lie exactly on each other]---------
            if(indexSplitL ~= 0)
                if(checkFloatingPointEqual(obj.bordersRight(indexSplitL),borderLeft) == 1);
                    %  indicesAdjust(end+1) = indexSplitL;
                    indexSplitL = 0;
                end;
            end;
            if(indexSplitR ~= 0)
                if(checkFloatingPointEqual(obj.bordersLeft(indexSplitR),borderRight) == 1)
                    %  indicesAdjust(end+1) = indexSplitR;
                    indexSplitR = 0;
                end;
            end;
            
            
            
            
            %------[Adjust sections in between]----------------------------------------
            
            %adjust all that need to be adjusted
            if(length(indicesAdjust) > 0)
                obj.probabilityDensities(indicesAdjust) = obj.probabilityDensities(indicesAdjust) + probabilityDensity;
                for(i=1:length(indicesAdjust))
                    obj.longestDistances(indicesAdjust(i)) = max(obj.longestDistances(indicesAdjust(i)),travelDistanceAdjust);                  
                end;
            end;
            
            
            %------------------------[Split up segments]-----------------------------------------------------------------------------------
            
            %do the splits
            if(indexSplitL == 0)&&(indexSplitR == 0)
                    %nothing to split
            else
                
                if((indexSplitL == indexSplitR)&&(indexSplitL ~= 0))
                    %------[Split when new section lies entirely within other]------------------
                    
                    %split up one section only as the new one lies entirely within it
                    
                    tmpBorder = obj.bordersRight(indexSplitL);
                    tmpProbab = obj.probabilityDensities(indexSplitL);
                    tmpTravelDistance = obj.longestDistances(indexSplitL);
                    
                    %adjust border of left neighbor
                    obj.bordersRight(indexSplitL) = borderLeft;

                    %add new segment for the newly added section
                    indicesAdd(end+1) = indexSplitL + 1 ;
                    bordersAddL(end+1) = borderLeft;
                    bordersAddR(end+1) = borderRight;
                    probabilityDensitiesAdd(end+1) = tmpProbab + probabilityDensity;
                    travelDistancesAdd(end+1) = max(travelDistanceSplit, tmpTravelDistance);
                    
                    %add new segment for the right-leftover part
                    indicesAdd(end+1) = indexSplitL + 1;
                    bordersAddL(end+1) = borderRight;
                    bordersAddR(end+1) = tmpBorder;
                    probabilityDensitiesAdd(end+1) = tmpProbab;
                    travelDistancesAdd(end+1) = tmpTravelDistance;
                    
                else
                    %------[Split when new section does not lie entirely within other - left ]------------------
                    %split up two sections
                    
                    %handle left section
                    if(indexSplitL ~= 0)
                        tmpBorderRight = obj.bordersRight(indexSplitL);
                        tmpProbab = obj.probabilityDensities(indexSplitL);
                        tmpTravelDistance = obj.longestDistances(indexSplitL);

                        %adjust border of left neighbor
                        obj.bordersRight(indexSplitL) = borderLeft;
                        
                        %add a new segments for the left overlapping part
                        indicesAdd(end+1) = indexSplitL + 1;
                        bordersAddL(end+1) = borderLeft;
                        bordersAddR(end+1) = tmpBorderRight; 
                        probabilityDensitiesAdd(end+1) = tmpProbab + probabilityDensity; 
                        travelDistancesAdd(end+1) = max(travelDistanceSplit, tmpTravelDistance);

                        
                    end;
                end;
            end;
            
            %------[Add new sections]---------------------------------------------------
           i = 1;
           while(i <= length(indicesAdd))
               iA = indicesAdd(i);
                    %obj.plot();
                    %input('--');
               if(checkFloatingPointEqual(bordersAddL(i),bordersAddR(i)) == 0)
                   if(obj.debug)
                       disp(sprintf('add.real [%d, %d] <- %f\n',bordersAddL(i),bordersAddR(i),probabilityDensitiesAdd(i)));
                   end;
                   
                   %Add new segments which are not at the end, move everything one to the right
                   if(iA <= length(obj.probabilityDensities))
                       obj.bordersLeft(iA + 1:end+1) = obj.bordersLeft(iA:end);
                       obj.bordersRight(iA + 1:end+1) = obj.bordersRight(iA:end);
                       obj.probabilityDensities(iA + 1:end+1) = obj.probabilityDensities(iA:end);    
                       obj.longestDistances(iA + 1:end+1) = obj.longestDistances(iA:end);    
                   end;
                   
                   obj.bordersLeft(iA) = bordersAddL(i);
                   obj.bordersRight(iA) = bordersAddR(i);
                   obj.probabilityDensities(iA) = probabilityDensitiesAdd(i);
%                    if(length(obj.longestDistances) >= iA)
%                         obj.longestDistances(iA) = max(obj.longestDistances(iA),travelDistancesAdd(i));
%                    else

                    %2do re-examine - changed
                   obj.longestDistances(iA) = travelDistancesAdd(i);
                    
                    
%                    end;
                   tmp = find(indicesAdd >= iA);
                   indicesAdd(tmp) = indicesAdd(tmp) + 1;
                   tmp = find(indicesAddPrioLow >= iA);
                   indicesAddPrioLow(tmp) = indicesAddPrioLow(tmp) + 1;
                    if(indexSplitR >= iA);
                       indexSplitR = indexSplitR + 1;
                   end;

                   
               end;
               i = i + 1;

           end;

           
           %Low Priority Add
           i = 1;
           while(i <= length(indicesAddPrioLow))
              
               iA = indicesAddPrioLow(i);
                  % obj.plot();
                  % input('--');
               if(checkFloatingPointEqual(bordersAddLPrioLow(i),bordersAddRPrioLow(i)) == 0)
                   %shift old content one place to the right
                   if(iA <= length(obj.probabilityDensities))
                       obj.bordersLeft(iA + 1:end+1) = obj.bordersLeft(iA:end);
                       obj.bordersRight(iA + 1:end+1) = obj.bordersRight(iA:end);
                       obj.probabilityDensities(iA + 1:end+1) = obj.probabilityDensities(iA:end);
                       obj.longestDistances(iA + 1:end+1) = obj.longestDistances(iA:end);    
                   end;
                   obj.bordersLeft(iA) = bordersAddLPrioLow(i);
                   obj.bordersRight(iA) = bordersAddRPrioLow(i);
                   obj.probabilityDensities(iA) = probabilityDensitiesAddPrioLow(i);
                   obj.longestDistances(iA) = travelDistancesAddPrioLow(i);
                   tmp = find(indicesAddPrioLow >= iA);
                   indicesAddPrioLow(tmp) = indicesAddPrioLow(tmp) + 1;
                   if(indexSplitR >= iA);
                       indexSplitR = indexSplitR + 1;
                   end;
               end;
               i = i + 1;
           end;
           
           
           
           %-------------------------[Split up right section]---------------------------------
                     %handle right section
            if(indexSplitR ~= 0)&&(indexSplitR ~= indexSplitL)
                tmpBorderLeft = obj.bordersLeft(indexSplitR);
                tmpProbab = obj.probabilityDensities(indexSplitR);
                 tmpTravelDistance = obj.longestDistances(indexSplitR);

                %adjust border of right neighbor
                obj.bordersLeft(indexSplitR) = borderRight;

                %add a new segments for the left overlapping part
                iA = indexSplitR;
                if(iA <= length(obj.probabilityDensities))
                       obj.bordersLeft(iA + 1:end+1) = obj.bordersLeft(iA:end);
                       obj.bordersRight(iA + 1:end+1) = obj.bordersRight(iA:end);
                       obj.probabilityDensities(iA + 1:end+1) = obj.probabilityDensities(iA:end);   
                       obj.longestDistances(iA + 1:end+1) = obj.longestDistances(iA:end);    
                end;
                obj.bordersLeft(iA) = tmpBorderLeft;
                obj.bordersRight(iA) = borderRight;
                obj.probabilityDensities(iA) = tmpProbab + probabilityDensity;
                obj.longestDistances(iA) = max(obj.longestDistances(iA),travelDistanceSplit);
            end;
        end;

        
        
        
        
        
        
        
        
        
        function obj = probabBuffer()
        %constructor
            obj.reset();
        end;
        
        function print(obj)
           %print the contents of the probability buffer
            disp('------------[ ProbabBuffer]------------');
            for i = 1:length(obj.bordersLeft)
                disp(sprintf('[%d - %d]: %f dist %f',obj.bordersLeft(i),obj.bordersRight(i),obj.probabilityDensities(i), obj.longestDistances(i)));
            end;
            disp('------------------------------------------');
        end;
        
        
        function plot(obj)
        %plot the contents of the probability buffer
        %    clf;
            for i = 1:length(obj.bordersLeft)
                rectangle('Position',[obj.bordersLeft(i),0,obj.bordersRight(i) - obj.bordersLeft(i),obj.probabilityDensities(i)],'EdgeColor',[1 - i/length(obj.probabilityDensities),1 - i/length(obj.probabilityDensities),i/length(obj.probabilityDensities)],'FaceColor',[i / length(obj.probabilityDensities) / length(obj.probabilityDensities),1.0,1 - i/length(obj.probabilityDensities)], 'lineWidth',3);
                str = sprintf('%d',i);
                text((obj.bordersLeft(i) + obj.bordersRight(i))/2,0.3,str);
                str = sprintf('%f',obj.longestDistances(i));
                text((obj.bordersLeft(i) + obj.bordersRight(i))/2,0.1,str);
            end;
            
        end;
        function consistencyCheck(obj)
            for(i = 1:(length(obj.probabilityDensities) - 1))
                if(obj.bordersLeft(i) >= obj.bordersRight(i+1))
                    disp(sprintf('Consistency Check failed for border %d <-> %d\n',i,i+1));
                    assert(1==0);

                end;
            end;
            disp('Consistency check passed successfully');
        end;
        
    end;  
      
end
