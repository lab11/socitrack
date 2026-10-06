# Reference

Limits and figures, with where each comes from. Anything marked *measured* was taken from real
deployment logs rather than calculated.

---

## Limits

| | Value | Why |
| --- | --- | --- |
| Badges per deployment | **10** | `MAX_NUM_RANGING_DEVICES` |
| Distance measurements | **2 per second**, every pair | `SCHEDULING_INTERVAL_US` = 500 ms |
| Maximum recorded distance | **32 m** | `MAX_VALID_RANGE_MM`; both reading tools use the same limit |
| Badge name length | 16 characters | `EUI_NAME_MAX_LEN` |
| Dropped from a network after | 60 seconds unheard | `DEVICE_TIMEOUT_SECONDS` |

---

## Battery

| Threshold | Value | Meaning |
| --- | --- | --- |
| Full | 4200 mV | A fully charged badge |
| Will not start recording below | **3750 mV** | `BATTERY_NOMINAL` |
| Critical | 3680 mV | Treated as empty |

*Measured* discharge, over every continuous unplugged stretch of a six-day deployment:

| Condition | Rate | Runway from full |
| --- | --- | --- |
| Ranging steadily | 13–17 mV/hour | 26–34 hours |
| Mostly alone | 2.6–4.1 mV/hour | 110–170 hours |

Plan on a day of continuous recording per charge.

---

## How long can a deployment run?

*Measured* log growth, for a badge ranging continuously:

| Deployment size | Log growth |
| --- | --- |
| 4 badges | 2.7 MB/day |
| 10 badges | 5.7 MB/day |

More badges means more distances per round, so the log grows roughly in proportion.

Storage depends on the board revision:

| Revision | Flash | 4 badges | 10 badges |
| --- | --- | --- | --- |
| **N, O, P** | 8 Gbit (~1074 MB) | ~390 days | ~190 days |
| **M** | 1 Gbit (~134 MB) | ~49 days | ~24 days |

Raw capacity; usable is somewhat lower after spare areas and bad-block reserve. In practice **battery
is the binding constraint, not storage** — except on revision M in a large deployment, where storage
starts to matter.

---

## Accuracy

*Measured* across a ten-badge deployment: every pair agreed with itself, measured from both ends, to
**within 1 mm** of the median. That is precision between badges, not absolute accuracy.

| Condition | Typical spread |
| --- | --- |
| Clear path, same room | ~15–40 mm |
| Through a wall | ~300 mm |

Spread is the useful discriminator — a stable distance means a clear path; a varying one means
something in between. See [Your Data](data.md#how-accurate-is-a-distance).

---

## Clocks

*Measured*: badges run **160–190 ppm slow**, about **15 seconds per day**, consistent across devices
and deployments.

All badges in a deployment share a network clock and stay within a few ppm **of each other**. The
drift is common-mode, so it affects alignment against external records but not cross-badge analysis.
Scheduling a deployment resets the badge clock.

---

## Hardware

| | |
| --- | --- |
| Microcontroller | Ambiq Apollo4 |
| UWB radio | Qorvo/Decawave DW3000 |
| Bluetooth | On-board controller, separate firmware |
| Storage | On-board NAND flash |
| Board revisions | **M, N, O, P** — silkscreened on the board |
| Programming connector | Tag-Connect pads labelled **XA2** |
| J-Link target | `AMA4B2KK-KBR` |

Firmware must be built for the matching revision. The wrong one builds cleanly and drives the wrong
pins.

---

## Build commands

From `software/firmware`:

| Command | Effect |
| --- | --- |
| `make BOARD_REV=P` | Build |
| `make BOARD_REV=P flash` | Build and flash |
| `make ID=c0:98:e5:42:00:XX UID` | Assign a badge its permanent ID (once ever) |
| `make BOARD_REV=P flashb` | Flash the Bluetooth controller firmware, then the application |
| `make clean` | Required when switching revisions or build switches |
| `make BOARD_REV=P TEMPCO=0` | TempCo switched off: factory regulator voltages, no 10 s temperature refresh |
| `make BOARD_REV=P DIAGNOSTIC=1` | Diagnostic build: logs every late radio receive and times radio interrupts, at some cost in power and log space. Not for deployments |
| `SEGGER_SERIAL=...` | Choose among several attached programmers |

From `software/firmware/tests` — diagnostic builds that exercise one subsystem and print over SEGGER
RTT:

```
make ranging      make storage     make bluetooth    make imu      make rtc
make battery      make buzzer      make button       make led      make usb
make system       make logging     make full
```

`make full` is the complete application plus console output and radio timing instrumentation. It is a
diagnostic build, not a deployment build — it powers up debug hardware a release build leaves off.

---

## Timing constants

Protocol timing lives in `src/app/app_config.h` and is deliberately **not** overridable at build time.
Every badge on a network shares one slot grid, so a value that differs between two badges does not
test a tighter grid — it breaks the round. Change it in the header and reflash the whole fleet.

| Constant | Value | What it sets |
| --- | --- | --- |
| `SCHEDULING_INTERVAL_US` | 500000 | Round period — the 2 Hz measurement rate |
| `RANGING_BROADCAST_INTERVAL_US` | 650 | One ranging slot |
| `RANGE_STATUS_BROADCAST_PERIOD_US` | 600 | One status slot |
| `SCHEDULE_RESEND_INTERVAL_US` | 800 | Spacing of the repeated schedule broadcasts |
| `SUBSCRIPTION_BROADCAST_PERIOD_US` | 1500 | Window for a new badge to join |

`SCHEDULING_INTERVAL_US` must divide one second exactly — several timeouts are counted in rounds per
second, and a value that does not divide evenly silently shortens all of them. A compile-time check
enforces this.

---

## Developer documentation

- [Development environment](internals/development-environment.md)
- [Storage format](internals/storage-format.md) — the authoritative log format description
- [Board bring-up](internals/board-bring-up.md)
- [Bluetooth controller firmware](internals/ble-controller-firmware.md)
- [IMU firmware](internals/imu-firmware.md)
