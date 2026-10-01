# Device Behaviour

Once a badge leaves the charger it runs entirely on its own. This page describes what it decides and
why, so that behaviour you see in the field is interpretable rather than mysterious.

---

## The sounds

A badge makes a sound every time it is plugged in or unplugged. There are only three sounds it can
make in normal use.

| Sound | Meaning |
| --- | --- |
| **Four rising tones** | Just placed on a charger. It has booted, seen the charger, and is healthy. |
| **Four falling tones** | Just taken off a charger. It has booted and is deciding whether to start recording. |
| **A short tune** | *Find my TotTag* — you asked it to identify itself from the dashboard. |

**Silence when you plug a badge in means something is wrong.** See
[Troubleshooting](troubleshooting.md#a-badge-makes-no-sound-on-the-charger).

A badge restarts every time it is plugged in or unplugged — that is why you hear a sound on both
transitions. This is deliberate: the restart is what cleanly ends one recording session and begins the
next, and it is why a badge put briefly on a charger mid-study leaves a visible seam in the data
rather than silently corrupting it.

---

## When a badge records

A badge records only when **all** of these are true:

1. It is **unplugged**.
2. It has a **valid scheduled deployment** that has not been cancelled or marked finished.
3. Its clock is **set** — which happens when you schedule a deployment.
4. The current time is **inside the study's start and end dates**.
5. If a **daily window** is configured, the current time of day is inside it.
6. Its battery is **above 3750 mV**.

If any is false, it powers off rather than idling, and wakes when the situation could have changed:

| Situation | What it does |
| --- | --- |
| Before the study start | Sleeps, wakes at the start time |
| Outside a daily window | Sleeps, wakes at the next daily start |
| After the study end | Sleeps until plugged in |
| Battery below 3750 mV | Sleeps until plugged in |
| Plugged in | Stays awake, reachable, but **does not record** |

That last row is the one that costs people data. **A badge on a charger is not recording.** It is
awake and will talk to the dashboard, but it is not ranging.

### Daily windows across midnight

A daily window of 22:00–06:00 is read as overnight rather than rejected. The firmware compares the
start and end times and handles the wrap.

---

## How badges find each other

There is no pairing step and no fixed roles.

When badges are switched on near each other, they listen briefly for an existing network. If they find
one, they join it. If they do not, one of them becomes the coordinator and the others join it. That
device keeps time for the group, and every badge measures distance to every other badge twice a
second.

The arrangement is not fixed. Badges that are separated and brought back together rejoin, and if the
coordinator leaves, the remaining badges form a new network. None of this requires you to do anything.

Two consequences are worth knowing:

- **Badges switched on far apart may form separate networks.** Two groups that never hear each other
  during start-up can end up as two independent networks, and they do not merge on their own. Taking
  all badges off their chargers in the same place avoids it.
- **A badge needs to hear the coordinator**, not just its neighbours. A badge at the far edge of a
  group may drop in and out even though it is close to several others.

---

## What gets recorded

Beyond distances, a badge logs a few things that are useful when interpreting a study:

| Record | Cadence | Why you care |
| --- | --- | --- |
| **Distances** | Twice a second, to every badge in range | The data |
| **Battery voltage** | Every 5 minutes | Shows whether a badge ran out |
| **Charging events** | On every plug and unplug | Shows exactly when a badge was off-duty |
| **Motion** | On change | Distinguishes "worn" from "left on a table" |
| **Accelerometer** | On motion | Finer-grained activity |
| **Restarts** | On every boot | Shows every interruption and its cause |
| **Timekeeping** | Every 5 minutes | Lets clock drift be measured and corrected |
| **Diagnostics** | Every 5 minutes | Internal health counters, firmware build, chip temperature |

The 5-minute housekeeping records are why a log is never completely empty, even for a badge that spent
a day alone in a drawer. A stretch of log containing only housekeeping means the badge was running but
had nobody to range with — which is itself a finding.

---

## Battery life

A badge draws most of its power keeping the radio alive during ranging, so battery life depends
heavily on how much of the time it actually has company.

Measured across a six-day deployment, over every stretch a badge spent continuously off its charger:

| Condition | Discharge | Usable runway |
| --- | --- | --- |
| Ranging steadily with other badges | 13–17 mV/hour | **roughly 26–34 hours** |
| Mostly alone, out of range of others | 2.6–4.1 mV/hour | 110–170 hours |

The usable range is about 450 mV — a full badge sits near 4200 mV and stops recording at 3750 mV.

Plan for **a day of continuous recording per charge**, and treat the longer figures as what you get
when badges spend much of the study apart rather than as something to rely on. A daily window extends
life roughly in proportion to how much of the day it excludes.

Below **3750 mV** a badge will not begin recording. Below **3680 mV** it treats the battery as
critical.

If a study runs longer than a charge, build charging into the protocol — overnight, say — and accept
the gap rather than letting badges die at unpredictable times. A badge that powers off mid-study
leaves no record of the period it missed, whereas a scheduled overnight charge is visible in the log
as charging events and can be excluded cleanly in analysis.
