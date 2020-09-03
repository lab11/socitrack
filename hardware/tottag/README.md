Hardware Revision History
=========================

The original localization platform was developed by the Lab11 research group,
primarily by @bpkempke, @bradjc, and @ppannuto.

The TotTag hardware was developed @abiri during his Master's Thesis work when
he was a visiting scholar with the Lab11 research group.

## Rev G
[Rev G Issue.](https://github.com/lab11/totternary/issues/7)
 - Add detachable battery connector
 - Add power switch
 - Adapt PCB shape for mass-production case

## Rev F

[Rev F Issue.](https://github.com/lab11/totternary/issues/4)
 - Add RTC

## Rev E

[Rev E Issue.](https://github.com/lab11/totternary/issues/3)

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
