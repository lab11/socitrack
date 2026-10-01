# Troubleshooting

Problems in roughly the order you are likely to meet them.

---

## A badge makes no sound on the charger

A healthy badge plays **four rising tones** within a second or two of being placed on a charger.

1. **Is it charging at all?** Reposition it on the pad. Wireless charging alignment is fussier than it
   looks.
2. **Is the firmware loaded?** A badge that has never been flashed does nothing. Redo
   [Getting Started](getting-started.md).
3. **Was it flashed for the right board revision?** Firmware built for the wrong revision compiles
   cleanly and then drives the wrong pins — a badge that looks dead is the usual symptom. Check the
   letter silkscreened on the board and reflash with the matching `BOARD_REV`.
4. **Is the battery completely flat?** Leave it on the charger for an hour and try again.

---

## A badge doesn't appear when I scan

**It is almost always the charger.** Badges only stay awake and reachable while plugged in. An
unplugged badge that is outside its study window powers itself off and cannot answer a scan.

If it is on a charger and still missing:

- Give it longer. A badge that has just been plugged in restarts first.
- Confirm it is charging — listen for the rising tones.
- Move closer. Scanning is Bluetooth, so a few metres and a clear path.

---

## A badge recorded nothing, or far less than the others

Work through these in order.

**1. Was it part of the study?** Use **Get Scheduled Deployment Details** on that badge. A badge that
was not on its charger when you pressed Schedule never learned about the study.

**2. Was it on a charger during the study?** Look for charging events in its log. A badge on a charger
is awake but **not recording**. This is the most common cause by a wide margin.

**3. Did its battery run out?** Check the voltage trace. Below 3750 mV a badge stops recording.

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

A badge seen in nearly every round had a good link. One seen in half had a marginal link — real data,
but weaker. One absent entirely was never in the network with this badge.

**5. Did two separate networks form?** If badges are switched on in different places, two groups can
form independent networks that never merge. The signature is two clusters that each range internally
but never with each other. Taking all badges off their chargers in the same place prevents it.

---

## One badge is consistently weaker than the others

If a badge ranges well with close neighbours but drops out with distant ones — while other badges hold
those same distances fine — that badge has a weaker radio link than its peers.

The test that separates "far away" from "weak radio" is to compare **links at the same distance**. If
badge A holds 99% of rounds with a peer at 2.4 m but only 58% with another peer at the same 2.4 m,
distance is not the explanation.

This is usually a hardware matter — antenna connection or per-board calibration — rather than anything
configurable. Worth identifying before a real study, because a weak badge that becomes the network's
coordinator degrades the whole group rather than only itself.

---

## A download reported missing pages

Lines like this during a download are **normal**:

```
Requesting 7 of 7 missing page(s), round 1...
```

The tool noticed a page did not survive the transfer and asked the badge to resend it. It is
self-correcting and nothing is lost.

This is **not** normal:

```
WARNING: log from 31 is incomplete:
   transfer ended early: 8 of 10 pages received
```

**Download the badge again before clearing it.** The data is still on the badge — an incomplete
download means the transfer failed, not that the recording did. A second attempt usually succeeds.

If it persists, keep the badge as-is and note the message, including the sequence numbers. The data
remains recoverable.

---

## A badge restarted unexpectedly

Restarts are listed in the log:

```python
import datetime
for r in records:
    if 'rst' in r:
        print(datetime.datetime.fromtimestamp(r['t']), r['rst'])
```

**`SW Power-On`** is normal and expected. A badge deliberately restarts every time it is plugged in or
unplugged, so these should match your charging routine. Several per day in a study where badges are
charged nightly is exactly right.

**Anything mentioning `Watchdog`** is a genuine fault — the firmware stopped responding and recovered
itself. The entry names the part that stalled, for example `Watchdog + stalled: BLETask`. An isolated
one is survivable; several in a short window on the same badge is worth reporting, with the log.

Other causes you may see — `hard fault`, `stack overflow`, `assertion failed` — are firmware faults.
Keep the log and report it.

---

## The clock is wrong

Badges run roughly **160–190 ppm slow**, about **15 seconds per day**. Over a week that is about a
minute and a half against wall clock.

All badges in a deployment share a network clock, so they stay consistent **with each other** even as
the group drifts against real time. Cross-badge analysis is unaffected; only alignment against an
external record is.

Scheduling a deployment sets the clock, so the drift restarts from zero at the start of each study.
The 5-minute housekeeping records carry the badge's own clock alongside the network clock, so drift
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
