# Developer Documentation

For people working on the firmware or the management tools. If you are running a study, you want the
[user documentation](../) instead.

| Document | Covers |
| --- | --- |
| [Development environment](development-environment.md) | Toolchain, source checkout, verifying a build |
| [Storage format](storage-format.md) | The authoritative description of the `.ttg` log format, and the reasoning behind its design |
| [Web tool architecture](web-tool-architecture.md) | How the browser tool keeps its format knowledge in step with the firmware |
| [Board bring-up](board-bring-up.md) | Checks for a newly assembled board |
| [Bluetooth controller firmware](ble-controller-firmware.md) | Updating the Bluetooth controller, which runs its own separate image |
| [IMU firmware](imu-firmware.md) | Updating the BNO055 |

## Where the authoritative answers live

Documentation drifts; code does not. When these disagree with the source, the source is right — and
the following are the places worth reading directly:

| Question | Read |
| --- | --- |
| What is a constant's value? | `software/firmware/src/app/app_config.h` |
| How does a ranging round work? | `software/firmware/src/tasks/ranging/` — one file per phase |
| When does a badge record or sleep? | `software/firmware/src/tasks/app_tasks.c` |
| What is in a log record? | `software/management/dashboard/tottag_format.py` |
| What does a build target do? | `software/firmware/Makefile` and `Jtag.mk` |

Several constants are duplicated between the firmware, the Python tool and the TypeScript package.
Those duplications are covered by automated parity tests in
`software/managementweb/packages/tottag-schema/test/` — a change on one side fails the build until the
other follows.

## Archived documentation

[`../archive/`](../archive/) holds documentation for hardware and software the project no longer uses,
kept for badges that predate the Apollo4 platform refresh. Nothing there applies to current hardware.
