Hardware
========

TotTag Gen 2 (2023-present)
---------------------------

- `rev_i` is a fairly complete overhaul of the TotTag platform, shedding
many of the legacy IC selections from the long design history of this project
(some traced back to the original 2015 PolyPoint board).

   - The most significant changes are moving from a dual-MCU design to a single MCU,
based around the Ambiq Apollo4 family of chips, and upgrading to the newer
generation UWB transciever, ~~Decawave~~ Qorvo's DW3000 family.

   - `rev_i` is in many ways a first revision of a new board.

- `rev_k` fixed most of the major flaws in from `rev_i`, but introduced new
problems around the DW3000.

- `rev_l` hedged around continued DW3000 issues and replaced the integrated
design with three copies of Qorvo's pre-fab module. This is a temporary
patch to get working systems with the new transciever more quickly to enable
scale-up.

- `rev_m` has only trivial, cosmetic fixes over `rev_l`.

- `rev_n` (in progress) removes the pre-fab UWB modules, and returns to the
integrated UWB design, which re-opens access to antenna diversity and the
improved robustness of ranging performance.


TotTag Gen 1 (2020-2023)
------------------------

Revisions of the original TotTags from the SociTrack project that focused on
the deployability, scaling, and robustness of the caregiver-infant interaction
tracking application.

This resulted in a more tightly integrated design as well as one more
aggressively geared towards a "wireless only" approach to ease device
management at-scale.

For more details, see the documentation in the [tottag folder](tottag/).


SociTrack (and TotTag Gen 0) (2017-2020)
----------------------------------------

The SociTrack system consists of a single PCB which integrates both an
application board and a SquarePoint module. SquarePoint is available from the
[*Lab11 EAGLE* library](https://github.com/lab11/eagle) as a design block
(check version number).

The original tottags (i.e., rev_c and rev_d) follow this more modularized
design, keeping SociTrack's components as cleanly separable features, but
integrating into a single PCB dual microcontrollers and enhanced functionality
such as SD card logging, accelerometer information, USB connectivity, wireless
charging, debugging ports, and LEDs, as well as battery voltage sensing.


PolyPoint (2015) and SurePoint (2016-2017)
------------------------------------------

The PolyPoint system consists of several PCBs centered around TriPoint, the core
UWB ranging module.

1. **TriPoint**: A triangular module which contains all of the necessary hardware
for utilizing a DecaWave DW1000 UWB radio. It has castellated headers so it can
be soldered onto a carrier board to add indoor localization functionality to any
device.

2. **TriTag**: A portable carrier board designed to be used as an indoor
localization tag. Contains TriPoint on one side and a BLE radio on the other.
