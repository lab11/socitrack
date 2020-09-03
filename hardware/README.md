Hardware
========

PolyPoint
---------

The PolyPoint system consists of several PCBs centered around TriPoint, the core
UWB ranging module.

1. **TriPoint**: A triangular module which contains all of the necessary hardware
for utilizing a DecaWave DW1000 UWB radio. It has castellated headers so it can
be soldered onto a carrier board to add indoor localization functionality to any
device.

2. **TriTag**: A portable carrier board designed to be used as an indoor
localization tag. Contains TriPoint on one side and a BLE radio on the other.


SociTrack
---------

The SociTrack system consists of a single PCB which integrates both an
application board and a SquarePoint module. SquarePoint is available from the
[*Lab11 EAGLE* library](https://github.com/lab11/eagle) as a design block
(check version number).

- **TotTag**: A single PCB featuring dual microcontrollers and enhanced
functionality such as SD card logging, accelerometer information, USB connectivity,
wireless charging, debugging ports, and LEDs, as well as battery voltage sensing.
