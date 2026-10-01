TotTag Firmware
===============

Firmware for the Ambiq Apollo4, containing the UWB ranging protocol, the Bluetooth maintenance
interface, and the on-board NAND log.

**Flashing instructions, build commands and timing constants are documented at
[lab11.github.io/socitrack](https://lab11.github.io/socitrack/):**

- [Getting Started](../../docs/getting-started.md) — programmer, cables, assigning a badge its ID, flashing
- [Reference](../../docs/reference.md#build-commands) — every build target and what it does
- [Device Behaviour](../../docs/device-behavior.md) — what the firmware decides at runtime and why

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
