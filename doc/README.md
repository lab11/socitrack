TotTag Overview
===============

TotTag is a wearable platform for social interaction tracking. The project grew
out of a MSc thesis in collaboration with Lab11 (Prof. Prabal Dutta) at the
University of California, Berkeley, and the Computer Engineering Group (Prof.
Lothar Thiele) at ETH Zurich. The project thesis is available under
[this link](https://pub.tik.ee.ethz.ch/students/2018-HS/MA-2018-36.pdf).


Documentation
-------------

Here you can find detailed documentation on how to get started using the TotTag.

If you find errors or think something could be made clearer, please go ahead
and [create a new issue](https://github.com/lab11/socitrack/issues) so we can keep
track of it and fix it.


Table of Contents
-----------------

Getting started

- **[Glossary](Glossary.md)** - Definitions, pictures of components, etc.
- **[Setting Up a Development Environment](Setup.md)** - Software and tools you
  will need to develop for and program the TotTag.
- **[Firmware: building, provisioning and flashing](../software/firmware/README.md)** -
  Assigning a new device its ID and getting firmware onto it. If you find a TotTag in
  an unknown state, this is a good place to start.
- **[Running a deployment](../software/management/dashboard/README.md)** - Scheduling
  an experiment and offloading logs with the Python dashboard, or with the
  [browser-based tool](../software/managementweb/README.md).

Reference

- **[Storage and Logging Design](Storage_Redesign.md)** - How the on-device log is
  structured, how it is offloaded, and the reasoning behind both. The reference for
  anything touching the `.ttg` format.
- **[Apollo4 Hardware Testing](AP4_Testing.md)** - Bring-up checks for a new board.
- **[Updating the BLE Controller Firmware](AP4_BLE_Firmware_Update.md)** - Reflashing
  the Cooper BLE controller.
- **[Updating the IMU Firmware](BNO055_Firmware_Update.md)** - Reflashing the BNO055.

Older generations

- **[archive/](archive/)** - Documentation for hardware and software the project no
  longer uses, kept for devices that predate the Apollo4 platform refresh.
