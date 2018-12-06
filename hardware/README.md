Hardware
==================

The PolyPoint system consists of several PCBs centered around TriPoint,
the core UWB ranging module.

1. **TriPoint** (`tripoint`): Triangular module which contains all necessary hardware
for using the DecaWave DW1000. It has castellated headers so it can be
soldered onto a carrier board to add indoor localization functionality
to any device.

2. **TriTag** (`tritag`): Portable carrier board designed to be used as an indoor
localization tag. Has TriPoint on one side and a BLE radio on the other.


The TotTernary system consists of a single PCB which integrates both the carrier board and the module.
The module itself, called **SquarePoint**, is available from the [*Lab11 EAGLE* library](https://github.com/lab11/eagle) as a design block (check version number).

- **TotTag** (`tottag`): PCB featuring updated microcontrollers and additional functionality such as SD card logging, accelerometer information, debugging chips and LEDs as well as voltage sensing.
