TotTag Revision History
=======================

The original localization platform was developed by the Lab11 research group,
primarily by @bpkempke, @bradjc, and @ppannuto.

The original TotTag hardware (rev_c, rev_d) was developed @abiri during his
Master's Thesis work when he was a visiting scholar with the Lab11 research
group.

Current TotTag hardware is maintained by @ppannuto and @corruptbear.


## Rev N
[Rev N updates](https://github.com/lab11/socitrack/issues/58)
__Note: The main board is derived from Rev K, and integrates AP4BP Rev-B back on-board.__
 - Re-integrate AP4BP
    - Remove duplicate concepts (LEDs, TagConnect headers, etc)
    - Match stackup (choose module's 0.6mm for microvias; update 50Ω for all RF)
 - Update IMU to BNO086
 - Update UWB components
    - Generally update design to match new Qorvo app note APH301, bigger things...
       - Revise layout of power rails
       - Update via fencing spacing to lambda/20
       - Remove soldermask from transmission lines
    - Switch from DW3210 (QFN) to DW3110 (WLCSP aka BGA)
       - WCSNG group reports BGA more reliable part
       - Next-gen part QM33110W [released; no stock; pin compatible; update when able] is BGA only
    - Revert from TCXO to XTAL (new rec'd components available)
    - Update key passives to 0603 C0G Low-ESL variants
    - Add Qorvo LNA
       - This rev hedges and only adds to RF1 trace; later rev will move to RFC if fruitful
       - This rev does not add optional band-pass filter
       - Also adds VR3, another copy of the low-noise LDO
  - DFM work
    - Shrink top-metal traces on AP4 BGA perimeter to 5 mil to better match trace 75% of ball pad diameter guidance
    - Add dedicated clean areas for mouse bite tabs
    - Update some passives to Worthington's "ideal" smd component footprint
    - Swap IOM's for DW and 9DOF to ease routing
    - Remove most microvias not on BGA pads


## Rev M + AP4BP Rev-B
[Rev L Issues / Rev M updates](https://github.com/lab11/socitrack/issues/49)
 - Fix silkscreen error on switch label

## Rev L + AP4BP Rev-B
[Prior rev's issues / updates](https://github.com/lab11/socitrack/issues/46)
 - Fixes first-rev issues from the AP4BP module.
 - Rips out our UWB in favor of DWM3000 pre-fab modules for Qorvo as a hedge
   to facilitate more certain-to-work UWB in the short term (but sacrificing
   diversity and robustness [and adding cost]).

## Rev K + AP4BP Rev-A
[Rev I Issues / Rev K Updates](https://github.com/lab11/socitrack/issues/44)
 - (n.b., there is no Rev J to avoid I/J confusion)
 - Move to Apollo4 Blue Plus (Blue has too many silicon bugs)
    - Split this to a castellated module for separation of hw design concerns
 - Move to TCXO for DW3000 (bad idea; will be removed next rev)

## Rev I -- **Major Revision**
 - Refresh part selection to replace 2015 parts with improved modern versions
 - Major change: Apollo4 Blue as sole MCU
 - Major change: DW3000 replaces DW1000 as UWB transceiver
 - Really, most stuff changed except the wireless charging

## Rev H
[Rev G Issues / Rev H Updates](https://github.com/lab11/socitrack/issues/9)
 - Isolates high-bandwidth accelerometer to dedicated SPI bus
 - Revises wireless charging design for robustness
 - Resolve RTC design issues
 - DFM cleanups

## Rev G
[Rev F Issues / Rev G Updates](https://github.com/lab11/totternary/issues/7)
 - Add detachable battery connector
 - Add power switch
 - Adapt PCB shape for mass-production case

## Rev F

[Rev E Issues / Rev F Updates.](https://github.com/lab11/totternary/issues/4)
 - Add RTC

## Rev E

[Rev D Issues / Rev E Updates.](https://github.com/lab11/totternary/issues/3)

Rev E updates:
 - Major performance improvements
   - Revise routing, part layout to improve RF performance
   - Capacitor layout around nRF to fix crystal stability issues
 - Add optional switching regulator bypass

## Rev D

[Rev D Issue.](https://github.com/lab11/totternary/issues/2)

Rev D updates:
 - Add low battery indication
 - Testpoints for buses
 - Fix reset signal for nRF52840
 - Several PCB improvements

## Rev C

[Rev C](../../tritag/rev_c/)

Revision C was the first of the 'tottag' family of revisions (though not yet
named as such). It converted the previous hardware modularity into Eagle design
blocks, and was the first single-board revision. It added several features
(storage, sensors) that were not part of the tritag design.

Rev C Major Highlights:
  - Modularize (design block) ranging design
  - Update BLE to nRF52840
  - Add SD Card
  - Add Accelerometer
  - Update charging design
  - Integrate FTDI

## Older revisions

Prior iterations of this platform were developed for the "Polypoint" project,
which later became the "Surepoint" project.

The tottag hardware is largely an evolution of the
[tritag](https://github.com/lab11/polypoint/tree/master/pcb/tritag) module
from these projects.
