# Running a Deployment

This is the main workflow: configure devices, hand them out, collect them, download the data.

If your devices have never been set up, do [Getting Started](getting-started.md) first.

---

## Before you begin

**Charge every device fully.** A device whose battery is below **3750 mV** will not begin recording — it
powers off instead and waits. Charging overnight avoids a whole class of confusing failures.

**Put every device on a charger** while you configure it. Devices only stay awake and reachable when
plugged in; an unplugged device that is outside its study window powers itself off and will not appear
in a scan. This is deliberate — it is what makes the battery last — but it means configuration happens
on the charger, always.

**Know your device IDs.** You will pick devices out of a list by the last two hex digits of their ID. If
you did not label the cases during setup, now is the time.

---

## Install the dashboard

The desktop dashboard is the usual tool:

```bash
cd socitrack/software/management
python3 -m pip install -e .
tottag
```

There is also a [browser-based tool](dashboard.md#the-browser-tool) at
[lab11.github.io/socitrack/app](https://lab11.github.io/socitrack/app/). It is a web page, so there is
nothing to install, but it needs Chrome, Edge or Opera on a computer to reach the devices. Both tools
configure devices identically and read the same logs; use whichever suits you.

---

## Step 1 — Schedule the deployment

With every device on a charger:

1. **Scan for TotTags.** Devices appear by ID. Wait until all of them are listed — a device that has
   just been plugged in can take a few seconds.
2. **Schedule New Pilot Deployment.**
3. Fill in:

| Field | Meaning |
| --- | --- |
| **Deployment Timezone** | The time zone the study happens in. Every time you enter is on this clock, and changing it reinterprets the times already entered. |
| **Start / End Date and Time** | The outer bounds of the study. Nothing is recorded before the start or after the end. |
| **Daily Start / End Time** | Optional. Restricts recording to a window within each day, in the study's local time: school hours, say. Leave unset to record continuously between the start and end dates. |
| **TotTags in Deployment** | The devices taking part. **Maximum 10.** |

4. **Schedule.** The configuration is pushed to every device on the list.

A few things worth understanding:

- **The timezone is yours, not the device's.** Pick the timezone the study happens in, and enter every
  time as a clock on the wall there would show it. Both tools convert for the devices.
- **If the clocks change during the study**, a device cannot follow the change, so its daily window
  moves by an hour on one side of it. The browser tool points this out and asks which side should keep
  the hours you entered, or whether to widen the window to cover both.
- **Daily windows may cross midnight.** A daily window of 22:00 to 06:00 is understood as overnight,
  not as an error.
- **Every device must be scheduled.** A device that was not on the charger when you pressed Schedule
  does not know about the study and will not take part. Scan again and confirm the list.

Use **Get Scheduled Deployment Details** on a device afterwards to read back what it actually stored.
It is worth doing on at least one device, and on all of them if the study matters.

### In the browser tool

On **Set up a deployment**, give the study a name if you like, choose its time zone, dates and daily
hours, then **Add a tag** for each device. As each one is added the page reads its battery, its clock
and any deployment it still holds, and the summary beside the form lists anything that needs
attention before writing. **Write to tags** writes each device in turn and reads it back, so every
device is checked, not just one. Save the **deployment record** it produces: it lists exactly what each
device confirmed it stored, with times in the study's time zone.

### Before you schedule over an earlier study

**Scheduling a device starts a fresh log on it.** Nothing is erased from its memory, but whatever it
recorded for the previous study can no longer be downloaded with either tool. Download every device
from the previous study first ([Step 4](#step-4--collect-and-download)). The browser tool knows which
studies' data it has saved, and will not write over one it has not seen saved unless you say the
data is safe elsewhere.

---

## Step 2 — Hand out the devices

Take them off the chargers.

Each device plays a sound as it comes off the charger, and from that moment behaves on its own:

- **Inside the study window** — it starts ranging and recording.
- **Before the study starts** — it powers off and wakes itself at the start time.
- **Outside a daily window** — it powers off and wakes at the next daily start.
- **Battery too low** — it powers off and waits for a charger.

Devices find each other automatically. There is no pairing step and no fixed roles — the network forms
itself, and reforms if devices are separated and brought back together.

**Keep devices apart from their chargers during the study.** A device put back on a charger stops
recording for as long as it is plugged in. That is the single most common way to lose data.

---

## Step 3 — During the study

Nothing is required. There is no live upload and no server; the data sits on each device.

Two things are available if you need them, both requiring the device in hand:

- **Subscribe to Live Ranging Data** shows distances in real time. The device must be **off** its
  charger for this, since it has to be ranging.
- **Activate Find my TotTag** makes a device chirp so you can find it.

---

## Step 4 — Collect and download

Put each device back on its charger — this both stops recording and is required for downloading.

In the dashboard: **Connect** to a device, then choose a download:

| Action | What you get |
| --- | --- |
| **Download Deployment Log Files** | Everything recorded during the scheduled study. This is normally what you want. |
| **Download Full Logs** | Everything on the device, including data from before this study. |
| **Download Raw Unprocessed Data** | The raw `.ttg` stream exactly as the device sent it, with no processing. |

Downloads take minutes rather than seconds and produce a `.ttg` file per device, plus a processed
`.pkl` alongside it. See [Your Data](data.md) for what is inside.

### Watch the console while it downloads

Occasionally a page of the log does not survive the transfer. The tool notices, asks the device to
resend, and tells you what it is doing:

```
Requesting 7 of 7 missing page(s), round 1...
```

That is normal and self-correcting. What is **not** normal is a message like:

```
WARNING: log from 31 is incomplete:
   transfer ended early: 8 of 10 pages received
```

If you see that, **download the device again before wiping anything**. The data is still on the device.
A second attempt usually succeeds. See
[Troubleshooting](troubleshooting.md#a-download-reported-missing-pages).

---

## Step 5 — Check what you got before you let the devices go

Downloading is the last moment when a problem is still fixable. Before you clear devices for the next
study, confirm that each log:

- covers the dates you expect;
- contains all the devices you expect, each seeing the others;
- has no warnings left unresolved from the download.

[Your Data](data.md#checking-a-log-is-complete) shows how to check each of these. Ten minutes here
saves a study.

---

## Cancelling or changing a deployment

**Cancel Scheduled Pilot Deployment** clears the schedule from a device. Each device must be cancelled
individually, on its charger.

To change a study's times or membership, schedule again; cancelling first is not necessary.

Neither cancelling nor scheduling erases a device's memory, but **both start a fresh log**: anything the
device recorded before can no longer be downloaded with either tool. Download it first. In the browser
tool, cancelling is on the **Check a tag** page, which says whether the device's data has been saved
before it asks you to confirm.
