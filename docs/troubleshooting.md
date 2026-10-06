# Troubleshooting

Problems in roughly the order you are likely to meet them.

---

## A device makes no sound on the charger

A healthy device plays **four rising tones** within a second or two of being placed on a charger.

1. **Is it charging at all?** Reposition it on the pad. Wireless charging alignment is fussier than it
   looks.
2. **Is the firmware loaded?** A device that has never been flashed does nothing. Redo
   [Getting Started](getting-started.md).
3. **Was it flashed for the right board revision?** Firmware built for the wrong revision compiles
   cleanly and then drives the wrong pins — a device that looks dead is the usual symptom. Check the
   letter silkscreened on the board and reflash with the matching `BOARD_REV`.
4. **Is the battery completely flat?** Leave it on the charger for an hour and try again.

---

## A device doesn't appear when I scan

**It is almost always the charger.** Devices only stay awake and reachable while plugged in. An
unplugged device that is outside its study window powers itself off and cannot answer a scan.

If it is on a charger and still missing:

- Give it longer. A device that has just been plugged in restarts first.
- Confirm it is charging — listen for the rising tones.
- Move closer. Scanning is Bluetooth, so a few metres and a clear path.

---

## A device recorded nothing, or far less than the others

Work through these in order.

**1. Was it part of the study?** Use **Get Scheduled Deployment Details** on that device. A device that
was not on its charger when you pressed Schedule never learned about the study.

**2. Was it on a charger during the study?** Look for charging events in its log. A device on a charger
is awake but **not recording**. This is the most common cause by a wide margin.

**3. Did its battery run out?** Check the voltage trace. Below 3750 mV a device stops recording.

**4. Was it within range?** Count how many rounds each peer appears in:

```python
import collections
peers = collections.Counter()
rounds = 0
for r in records:
    if 'r' in r:
        rounds += 1
        peers.update(r['r'].keys())
for uid, n in sorted(peers.items()):
    print(hex(uid), f'{100*n/rounds:.0f}% of rounds')
```

A device seen in nearly every round had a good link. One seen in half had a marginal link — real data,
but weaker. One absent entirely was never in the network with this device.

**5. Did two separate networks form?** If devices are switched on in different places, two groups can
form independent networks that never merge. The signature is two clusters that each range internally
but never with each other. Taking all devices off their chargers in the same place prevents it.

---

## One device is consistently weaker than the others

If a device ranges well with close neighbours but drops out with distant ones — while other devices hold
those same distances fine — that device has a weaker radio link than its peers.

The test that separates "far away" from "weak radio" is to compare **links at the same distance**. If
device A holds 99% of rounds with a peer at 2.4 m but only 58% with another peer at the same 2.4 m,
distance is not the explanation.

This is usually a hardware matter — antenna connection or per-board calibration — rather than anything
configurable. Worth identifying before a real study, because a weak device that becomes the network's
coordinator degrades the whole group rather than only itself.

### Checking devices on the bench

Both tools do this comparison for you, live over Bluetooth: **Live Radio Check** in the desktop
dashboard, or the **Radio check** tab in the browser tool.

1. Put the devices on their chargers, or leave them running a deployment, so they can be reached.
2. In the desktop dashboard, **Scan for TotTags**, then choose **Live Radio Check** and tick each device.
   In the browser tool, open **Radio check → Live over Bluetooth** and add each device. Devices the browser
   has seen before can be added all at once.
3. Choose how long to run, from 5 minutes to an hour, and start the test. Each device restarts into a
   *radio test* and starts ranging, on its charger or off it.
4. Put them on a table where they can all see each other, at least half a metre (2 feet) apart. The test carries on
   while you move them. Note where each one sits if you can.

Verdicts appear once every device has a full minute of data and settle over the next few. A radio test
logs nothing and leaves a device's deployment as it was. When it ends, or when you stop it, each device
restarts into whatever it would otherwise be doing. A device that was running a deployment picks it up
again, with a gap in its log for the test. A device that is off its charger without one switches off. A
device needs firmware with radio-test support. The browser tool also needs a browser with Web Bluetooth
(Chrome or Edge).

The browser tool can also run the same check on the downloaded logs of a deployment that already ran:
choose **From downloaded logs** and open every device's log.

Both tools apply the same rules. Each device is judged against the others and gives a **Pass**, **Check** or **Fail** with the
reason:

- **Receives failed** well above the other devices' rate means a weak receiver, antenna or connection.
- **One antenna failing far more than the other two.** Each round cycles through all three antennas, so
  they should match; a mismatch names the faulty one.
- **Ranged in few rounds** means the device was not taking part in the network.
- **Distance offset**, with positions entered, means a device that reads long or short on every link, so
  its calibration is off.

The **Links** grid shows how often each pair ranged. A device that is weak on every link is the device; a
single weak pair is more likely something between them.

---

## A download reported missing pages

Lines like this during a download are **normal**:

```
Requesting 7 of 7 missing page(s), round 1...
```

The tool noticed a page did not survive the transfer and asked the device to resend it. It is
self-correcting and nothing is lost.

This is **not** normal:

```
WARNING: log from 31 is incomplete:
   transfer ended early: 8 of 10 pages received
```

**Download the device again before clearing it.** The data is still on the device — an incomplete
download means the transfer failed, not that the recording did. A second attempt usually succeeds.

If it persists, keep the device as-is and note the message, including the sequence numbers. The data
remains recoverable.

---

## A device restarted unexpectedly

Restarts are listed in the log:

```python
import datetime
for r in records:
    if 'rst' in r:
        print(datetime.datetime.fromtimestamp(r['t']), r['rst'])
```

**`SW Power-On`** is normal and expected. A device deliberately restarts every time it is plugged in or
unplugged, so these should match your charging routine. Several per day in a study where devices are
charged nightly is exactly right.

**Anything mentioning `Watchdog`** is a genuine fault — the firmware stopped responding and recovered
itself. The entry names the part that stalled, for example `Watchdog + stalled: BLETask`. An isolated
one is survivable; several in a short window on the same device is worth reporting, with the log.

Other causes you may see — `hard fault`, `stack overflow`, `assertion failed` — are firmware faults.
Keep the log and report it.

---

## The clock is wrong

Devices run roughly **160–190 ppm slow**, about **15 seconds per day**. Over a week that is about a
minute and a half against wall clock.

All devices in a deployment share a network clock, so they stay consistent **with each other** even as
the group drifts against real time. Cross-device analysis is unaffected; only alignment against an
external record is.

Scheduling a deployment sets the clock, so the drift restarts from zero at the start of each study.
The 5-minute housekeeping records carry the device's own clock alongside the network clock, so drift
can be measured and corrected after the fact. See [Your Data](data.md#clock-drift).

---

## Still stuck

Collect, before asking:

- the `.ttg` files (not just the `.pkl`),
- the console output from the download,
- the board revision and roughly when the firmware was flashed,
- what you expected to see and what you saw instead.

The `.ttg` is the one that matters. It is the original record and almost every question can be
answered from it.
