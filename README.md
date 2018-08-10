SurePoint
=========

<img src="https://raw.githubusercontent.com/abiri/totternary/master/media/tern_comic_1280.svg" alt="TotTernary" width="20%" align="left">

TotTernary is a system for using ultra-wideband RF time-of-flight ranging to perform indoor ranging and localization.
It incorporates the *SquarePoint* module, containing the DecaWave DW1000 for UWB packet transmission and timestamping.
The module provides node-to-node ranges over an I2C interface which can then be sent further on using the radio on the mother board.



Name
----

The name **TotTernary** originates in the concatenation of *tot* ("a small (quantity)", demonstrating its small size) and *ternary* ("composed of three parts", stressing the number three in antenna and frequency diversity). It furthermore references one of the driving use cases of the project: the application of
ranging for caretaker-children ("toddlers") interaction studies.


Git Clone
---------

When cloning this repository, be absolutely sure to do

    git clone --recursive https://github.com/abiri/totternary.git

so that you get the submodules as well. All of the supporting
libraries and build tools are in submodules for the various
hardware platforms used in this project.


Hardware
--------

The **TotTernary** system is composed of several hardware pieces. At the core is the
*SquarePoint* module, a 30mm x 15mm PCB that encompasses all of the
core ranging hardware and software. *SquarePoint* is provided as an EAGLE board and allows for direct integration as a design block as part of the [Lab11 EAGLE library](https://github.com/lab11/eagle). The *TotTag* is one such
carrier board which incorporates the module and is designed to be the tag in the ranging system. It includes the
UWB antennas and a Bluetooth Low Energy radio plus a battery charging circuit, as well as an SD card for logging and an accelerometer for the detection of movement and sensor fusion.
TotTag is able to provide ranges to a mobile phone application.

### SquarePoint

SquarePoint includes the following components:

- DecaWave DW1000 UWB radio
- STM32F091CCU6 MCU
- RF switch

The MCU contains all the necessary code to run the DW1000 and the ranging
protocol.

### TotTag


  <img src="https://raw.githubusercontent.com/abiri/totternary/master/media/tottag_pcb.png" alt="TotTag" width="20%;" align="right">


TotTag includes:

- The SquarePoint module
- 3 UWB antennas
- nRF52840 BLE radio
- 3.3 V LDO
- Li-ion battery charger
- SD card holder
- microUSB connector including FTDI FT232R for debugging
- 3-axis accelerometer

TotTag is designed to be the tag to be localized in the system and connected
to a smartphone.


Software
--------

TotTernary contains many software layers that run at various levels of
the system.

#### SquarePoint

The core firmware that makes the SquarePoint module work
includes all of the logic to implement two way ToF ranging
on top of the DecaWave DW1000 UWB radio. The firmware architecture
supports multiple "applications", or ranging algorithms, that can
be selected at runtime.

#### TotTag

The TotTag code implements a BLE application
that uses the SquarePoint module as an I2C device and provides
a BLE service. It puts the TotTag hardware into TAG mode
and provides ranges over a BLE characteristic.

#### Phone and BLE

The tools in the `/phone` directory interact with TotTag and read data
across the BLE interface.

### Linux Development

This project requires the [GNU ARM Embedded Toolchain](https://developer.arm.com/open-source/gnu-toolchain/gnu-rm). Please be aware that the recent Ubuntu 18.04 (`bionic`) ships with an old version ([6.3.1](https://launchpad.net/ubuntu/bionic/+source/gcc-arm-none-eabi)) and will cause compile errors ('conflicting CPU Architecture'). We therefore strongly encourage you to either remain on Ubuntu 17.10 (`artful`) or directly install the newest version from the developers.
