TotTag Firmware
===============

Firmware for the Ambiq Apollo4, containing the UWB ranging protocol, the Bluetooth maintenance
interface, and the on-board NAND log.

**Flashing instructions, build commands, and timing constants are documented at
[lab11.github.io/socitrack](https://lab11.github.io/socitrack/):**

- [Getting Started](https://lab11.github.io/socitrack/getting-started.html) — programmer, cables, assigning a device its ID, flashing
- [Reference](https://lab11.github.io/socitrack/reference.html#build-commands) — every build target and what it does
- [Device Behavior](https://lab11.github.io/socitrack/device-behavior.html) — what the firmware decides at runtime and why

Layout
------

| Path | Contents |
| --- | --- |
| `src/app/` | `app_config.h` — every tunable constant, including the protocol timing grid |
| `src/tasks/` | FreeRTOS tasks: ranging scheduler, storage, Bluetooth, application |
| `src/tasks/ranging/` | The ranging protocol itself, one file per round phase |
| `src/peripherals/` | Hardware drivers — radio, NAND, IMU, battery, buzzer, LEDs, RTC |
| `src/boards/rev<X>/` | Per-revision pinouts. **M, N, O, P** are supported. |
| `src/external/` | Vendored dependencies: DW3000 driver, nandlog, TinyUSB, SEGGER RTT |
| `AmbiqSDK/` | Ambiq SDK and the Cordio Bluetooth stack |
| `tests/` | Per-subsystem diagnostic builds, plus `make full` for the instrumented application |

Quick build
-----------

```bash
make BOARD_REV=P flash     # substitute the letter silkscreened on your board
```

`make clean` first when switching revisions — object files are not revision-aware.

The build ends with a memory-usage table in which `MCU_TCM` always reads 100%. That is expected: the
linker gives whatever TCM is left over to the stack, so the region is full by construction rather than
out of room.
