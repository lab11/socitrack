# Device Behavior

Once a device leaves the charger, it runs entirely on its own. This page describes what it decides and
why, so that behavior you see in the field is interpretable rather than mysterious.

---

## The sounds

A device makes a sound every time it is plugged in or unplugged. There are only three sounds it can
make in normal use.

| Sound | Meaning |
| --- | --- |
| **Four rising tones** | Just placed on a charger. It has booted, seen the charger, and is healthy. |
| **Four falling tones** | Just taken off a charger. It has booted and is deciding whether to start recording. |
| **A short tune** | *Find my TotTag* — you asked it to identify itself from the dashboard. |

**Silence when you plug a device in means something is wrong.** See
[Troubleshooting](troubleshooting.md#a-device-makes-no-sound-on-the-charger).

A device restarts every time it is plugged in or unplugged — that is why you hear a sound on both
transitions. This is deliberate: the restart is what cleanly ends one recording session and begins the
next, and it is why a device put briefly on a charger mid-study leaves a visible seam in the data
rather than silently corrupting it.

---

## When a device records

A device records only when **all** of these are true:

1. It is **unplugged**.
2. It has a **valid scheduled deployment** that has not been canceled or marked finished.
3. Its clock is **set** — which happens when you schedule a deployment. A device that loses power (a dead
   or disconnected battery, or being switched off) loses its clock, and will not record until its clock
   is set again, which scheduling a deployment does.
4. The current time is **inside the study's start and end dates**.
5. If a **daily window** is configured, the current time of day is inside it.
6. Its battery is **above 3750 mV** when it starts. Once recording, it carries on until 3680 mV.

If any is false, it powers off rather than idling, and wakes when the situation could have changed:

| Situation | What it does |
| --- | --- |
| Before the study start | Sleeps, wakes at the start time |
| Outside a daily window | Sleeps, wakes at the next daily start |
| After the study end | Sleeps until plugged in |
| Battery below 3750 mV | Sleeps until plugged in |
| Plugged in | Stays awake, reachable, but **does not record** |

That last row is the one that costs people data. **A device on a charger is not recording.** It is
awake and will talk to the dashboard, but it is not ranging.

### Daily windows across midnight

A daily window of 22:00–06:00 is read as overnight rather than rejected. The firmware compares the
start and end times and handles the wrap.

---

## How devices find each other

There is no pairing step and no fixed roles.

When devices are switched on near each other, they listen briefly for an existing network. If they find
one, they join it. If they do not, one of them becomes the coordinator and the others join it. That
device keeps time for the group, and every device measures distance to every other device twice a
second.

The arrangement is not fixed. Devices that are separated and brought back together rejoin, and if the
coordinator leaves, the remaining devices form a new network. None of this requires you to do anything.

Two consequences are worth knowing:

- **Separate networks merge when they meet.** Devices switched on far apart can start out as two
  networks. Each coordinator keeps listening for others over Bluetooth while it runs, and when it finds
  one with a higher ID it hands over and joins that network. A device that hears another network's
  transmissions partway through a round also stops and rejoins. Either way, the groups become one
  network on their own.
- **A device does not need to hear the coordinator directly.** Every device in the network repeats the
  schedule in its own turn, and passes on join requests the coordinator could not hear, so a device at
  the far edge of a group stays in as long as it can hear one of the others.

### Antennas

Each device has three antennas. When it measures distances it uses all three in turn, and it keeps
track of how well each one has been receiving over the last few seconds. Everything else uses a single
antenna: listening for the schedule, asking to join, and the short status exchange at the end of
each round. For those, the device uses whichever antenna has been receiving best, and only moves to
another when that one is clearly better. A device that listens for a full half second without hearing
a schedule tries its next antenna. So a damaged antenna costs a device some measurements, but it
does not keep the device out of the network.

### Radio tests

The live radio check, in either the desktop dashboard or the browser tool, runs devices in a *radio test*:
a timed network among the devices it names, started over Bluetooth. A device restarts into the test and ranges even on its charger, and
plugging or unplugging it does not interrupt the test. It records nothing, and its stored deployment is
left alone. When the test ends, or the tool stops it, the device restarts into whatever it would
otherwise be doing. A test lasts at most an hour, although usually 2 minutes is enough — both tools offer
2 to 10 minutes, and default to 2.

---

## What gets recorded

Beyond distances, a device logs a few things that are useful when interpreting a study:

| Record | Cadence | Why you care |
| --- | --- | --- |
| **Distances** | Twice a second, to every device in range | The data |
| **Battery voltage** | Every 5 minutes | Shows whether a device died |
| **Charging events** | On every plug and unplug | Shows exactly when a device was off-duty |
| **Motion** | On change | Distinguishes "worn" from "left on a table" |
| **Accelerometer** | On motion | Records finer-grained activity data |
| **Restarts** | On every boot | Shows every interruption and its cause |
| **Timekeeping** | Every 5 minutes | Lets clock drift be measured and corrected |
| **Diagnostics** | Every 5 minutes | Internal health counters, firmware build, chip temperature |

The 5-minute housekeeping records are why a log is never completely empty, even for a device that spent
a day alone in a drawer. A stretch of log containing only housekeeping means the device was running but
had nobody to range with, which is itself a finding.

---

## Battery life

A device draws most of its power keeping the radio alive during ranging, so battery life depends
heavily on how much of the time it actually spends ranging.

In a week-long test, **ten devices ranging with each other continuously lasted 6 days** on a single
charge. Devices that spend time apart, or sleep outside a daily window, last longer still.

Battery voltage does not fall at a steady rate, so the drop over a few hours is a poor guide to how
long a device will last.

A full device sits near **4200 mV**. Below **3750 mV** it will not begin recording, and once recording
it powers off at **3680 mV**.

Even so, build overnight charging into any multi-day study, rather than letting devices run down at
unpredictable times. A device that powers off mid-study
leaves no record of the period it missed, whereas a scheduled overnight charge is visible in the log
as charging events and can be excluded cleanly in analysis.
