# Your Data

A download gives you two files per badge:

| File | What it is |
| --- | --- |
| `<name>_<start>.ttg` | The raw log, byte for byte as the badge sent it. **This is your archival copy.** |
| `<name>.pkl` | The same data parsed into Python objects, ready for analysis. |

Keep the `.ttg` files. They are the original record and can be re-parsed at any time; the `.pkl` is a
convenience that can always be regenerated. If a badge's data ever looks wrong, the `.ttg` is what
tells you whether the problem is in the recording or in the processing.

---

## Reading a log in Python

```python
import pickle
records = pickle.load(open('02.pkl', 'rb'))
```

You get a list of dictionaries, one per instant, sorted by time. Every record has a `t` (a Unix
timestamp) plus whichever fields were recorded at that moment:

| Key | Contents |
| --- | --- |
| `r` | **Distances.** `{badge_id: millimetres}` — the main data |
| `v` | Battery voltage, mV |
| `c` | Charging event — `'Plugged'`, `'Unplugged'`, `'Charging'`, `'Not Charging'` |
| `m` | Motion — `True` when moving |
| `i` | Accelerometer, `[x, y, z]` |
| `b` | Other badges seen over Bluetooth |
| `rst` | A restart, with its cause as a list of strings |
| `rtc`, `offset`, `lag` | Timekeeping — see [clock drift](#clock-drift) |
| `diag` | Internal health counters, which firmware build wrote the log, the chip temperature, and radio health |

So the distances over time to badge `0x3E`:

```python
series = [(r['t'], r['r'][0x3E]) for r in records if 'r' in r and 0x3E in r['r']]
```

The bundled `processing.py` has helpers for plotting pairs and overlaying annotated events.

---

## How accurate is a distance?

Between two badges with a clear path, agreement is excellent. Across a ten-badge deployment, every
pair agreed with itself from both directions to **within 1 mm** of the median.

That is precision, not accuracy — it says the two badges computed the same number, not that the number
is the true distance. In practice:

- **Clear line of sight**: a few centimetres, stable.
- **Through a body**: reads longer, because the signal travels around rather than through. A badge on
  someone's chest facing away from another person is the common case.
- **Through a wall**: reads longer and varies far more. In one deployment a through-wall pair showed
  roughly **20× the spread** of a same-desk pair at the same nominal distance.

The spread is the useful signal. A pair with a stable distance has a clear path; a pair whose distance
jumps around has something between them. Compute the standard deviation over a short window and treat
high-variance stretches as "obstructed" rather than as noise to be smoothed away.

### One caveat

**A missing reading is not a distance of zero.** If two badges could not complete an exchange, there
is simply no entry for that pair at that instant. Absence usually means far apart or obstructed, but
it can also mean one badge was charging, asleep, or had left the network. Check for charging events
and restarts before reading too much into a gap.

---

## Checking a log is complete

Worth doing before you release the badges.

**1. The download reported no warnings.** A clean download prints nothing unusual. Lines about
retransmission rounds are normal and self-correcting. A `WARNING: log from ... is incomplete` is not —
download that badge again.

**2. The dates cover your study.**

```python
import datetime
ts = [r['t'] for r in records]
print(datetime.datetime.fromtimestamp(ts[0]), '->', datetime.datetime.fromtimestamp(ts[-1]))
```

**3. Every badge saw every other badge.**

```python
import collections
peers = collections.Counter()
for r in records:
    if 'r' in r:
        peers.update(r['r'].keys())
print({hex(k): v for k, v in peers.items()})
```

In a ten-badge study each log should list nine peers. A badge missing entirely was never in the
network. A badge with far fewer readings than the others was on the edge of range — real data, but
treat it as a weaker measurement.

**4. Restarts are explained.**

```python
for r in records:
    if 'rst' in r:
        print(datetime.datetime.fromtimestamp(r['t']), r['rst'])
```

`SW Power-On` entries are normal — a badge restarts on every plug and unplug, so these should line up
with your charging routine. Anything mentioning `Watchdog` is a fault; see
[Troubleshooting](troubleshooting.md#a-badge-restarted-unexpectedly).

---

## Gaps in the data

Not every gap is a problem, and the kind of gap tells you which.

| Gap | Meaning |
| --- | --- |
| Distances stop, housekeeping continues every 5 min | The badge was running but had nobody in range. Normal when people separate. |
| Everything stops, then a `Plugged` event | On a charger or a USB cable. Recording is suspended while plugged in. |
| Everything stops, then a restart | Powered off — outside the study window, or flat battery. Check the voltage before the gap. |
| Everything stops with no explanation | Investigate. Start with [Troubleshooting](troubleshooting.md). |

The 5-minute housekeeping cadence is what makes the first two distinguishable, so it is worth checking
which you have before concluding a badge failed.

---

## Clock drift

Badges keep time with a crystal that is not perfect. Measured across deployments, they run roughly
**160–190 parts per million slow** — about **15 seconds per day**.

Every badge in a deployment follows the same network clock, so **badges agree with each other** to
within a few parts per million. The drift is almost entirely common to the whole group.

This matters only if you are aligning TotTag data against an external source — video, an observer's
notes, another instrument. Over a week, expect roughly a minute and a half of offset against wall
clock. The `rtc`, `offset` and `lag` fields in each 5-minute housekeeping record carry the badge's own
clock alongside the network clock, so the drift can be measured and corrected after the fact rather
than estimated.

---

## The raw format

The `.ttg` file is a page-framed stream: a header, then a sequence of pages each carrying its own
sequence number, timestamps and CRC. The CRC is what lets the tools detect a page damaged in transfer
and ask the badge to resend it.

You rarely need this level of detail. If you are writing your own reader, the authoritative
description is in the [storage format notes](internals/storage-format.md), and the two existing
readers — `software/management/dashboard/tottag_format.py` and
`software/managementweb/packages/tottag-schema/src/log.ts` — are kept in step with the firmware by an
automated check.
