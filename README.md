TotTernary
=========

<img src="https://raw.githubusercontent.com/lab11/totternary/master/media/tern_comic_1280.png" alt="TotTernary" width="25%" align="left">

*TotTernary* is a system for using ultra-wideband RF time-of-flight ranging to perform indoor ranging and localization.
It incorporates the *SquarePoint* module, containing the DecaWave DW1000 for UWB packet transmission and timestamping.
The module provides node-to-node ranges over an I2C interface which can then be sent further on using the radio on the mother board.

<br>

Name
----

The name **TotTernary** originates in the concatenation of *tot* ("a small (quantity)", demonstrating its small size) and *ternary* ("composed of three parts", stressing the number three in antenna and frequency diversity). It furthermore references one of the driving use cases of the project: the application of
ranging for caretaker-children ("toddlers") interaction studies.


Git Clone
---------

When cloning this repository, be absolutely sure to do

    git clone --recursive https://github.com/lab11/totternary.git

so that you get the submodules as well. All of the supporting
libraries and build tools are in submodules for the various
hardware platforms used in this project.


Hardware
--------

The **TotTernary** system is composed of several hardware pieces. At the core is the
*SquarePoint* module, a 30mm x 15mm PCB that encompasses all of the
core ranging hardware and software. *SquarePoint* is provided as an EAGLE board and allows for direct integration as a design block as part of the [Lab11 EAGLE library](https://github.com/lab11/eagle). The *TotTag* is one such
carrier board which incorporates the module and can be used to setup a tracking and localization system with mobile nodes. It includes the UWB antennas and a Bluetooth Low Energy (BLE) radio plus a battery charging circuit, as well as an SD card for logging and an accelerometer for the detection of movement and sensor fusion.
TotTag is able to provide ranges and position estimates directly to a mobile phone application in real-time and can be configured on-the-fly using a simple user interface over [Summon](https://github.com/lab11/summon).

### SquarePoint

SquarePoint, an EAGLE design block for UWB ranging, includes the following components:

- DecaWave DW1000 UWB radio
- STMicroelectronics STM32F091CCU6 MCU
- RF switch

The MCU contains all the necessary code to run the DW1000 and the ranging
protocol. This module provides a simple abstraction layer for quick integration with a carrier board over an I2C interface without having to worry about underlying implementation details.

### TotTag

  <img src="https://raw.githubusercontent.com/lab11/totternary/master/media/tottag_vE_front.jpg" alt="TotTag Front" width="40%;" align="right">

TotTag, a PCB which utilizes SquarePoint and provides a self-contained unit for interaction tracking, includes:

- The SquarePoint module
- 3 UWB antennas
- Nordic Semiconductors nRF52840 BLE radio
- 3.3 V LDO designed to be used with 4.2V LiPo batteries
- Battery charge management controller
- SD card holder
- microUSB connector including FTDI FT232R for debugging
- 3-axis accelerometer

TotTag is designed to be a node which can be used both as an anchor, a tag or a combination of both. It offers infrastructure-free and -based network structures and enables centimeter-accurate, multi-day deployments. Both schematic and board design are open-source and can be found in the [`hardware/tottag`](hardware/tottag) folder; a suitable 3-D printable case is provided in [`hardware/cad/tottag-case`](hardware/cad/tottag-case/).


Software
--------

TotTernary contains many software layers that run at various levels of
the system.

#### SquarePoint

Found in [`software/module`](software/module/), the core firmware that makes the SquarePoint module work
includes all of the logic to implement our custom ranging protocol on top of the DecaWave DW1000 UWB radio. It makes use of frequency and antenna diversity and leverages both one-way and two-way ToF ranging to achieve efficient and reliable ranging in various environments. The firmware architecture supports multiple "applications", or ranging algorithms, that can
be selected at runtime; currently, we officially support:

- *Standard*: the full protocol, using maximal diversity to achieve highly-reliable ranging measurements over 30 different channels.
- *Calibration*: automatically triggered by our calibration scripts, the app allows for automated device-specific calibration which is then applied for future measurements.
- *Tests*: implementation of one-way transmission and reception, this application is meant for reliability and connectivity tests.

#### TotTag

The TotTag code, situated at [`software/carrier`](software/carrier/), implements a BLE application
that uses the SquarePoint module as an I2C slave and provides
a BLE service. It exposes services to configure and enable the device, set the current time to enable accurate time stamping of ranging data as well as accessing data in real-time over a BLE characteristic.

#### Phone and BLE

The tools in the [`/software/phone`](software/phone/) directory interact with TotTag and read data
across the BLE interface. It uses the Summon app ([Google Play](https://play.google.com/store/apps/details?id=edu.umich.eecs.lab11.summon), [App Store](https://itunes.apple.com/us/app/summon-lab11/id1051205682)) to easily access and interact with the nodes. The website must be configured in the carrier code and can be hosted on a personal domain; we recommend the use of a link shortener to reduce the BLE advertisement length.

### Linux Development

This project requires the [GNU ARM Embedded Toolchain](https://developer.arm.com/open-source/gnu-toolchain/gnu-rm). Please be aware that the recent Ubuntu 18.04 (`bionic`) ships with an old version ([6.3.1](https://launchpad.net/ubuntu/bionic/+source/gcc-arm-none-eabi)) and will cause compile errors ('conflicting CPU Architecture'). We therefore strongly encourage you to either remain on Ubuntu 17.10 (`artful`) or directly install the newest version from the developers.
