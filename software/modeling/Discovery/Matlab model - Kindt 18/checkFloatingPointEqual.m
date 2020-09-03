% Compare two floating-point values for equailty.
% Philipp H. Kindt <philipp.kindt@tum.de>
%  
%  PI-LatencyComp
%    Copyright (C) 2015, 2016, 2017 Philipp H. Kindt <philipp.kindt@tum.de>
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


function equality = checkFloatingPointEqual(x,y)
    %checks if two floating point numbers are equal
    if(abs(x -y) < 1e-16)
        equality = 1;
    else
        equality = 0;
    end;
end
