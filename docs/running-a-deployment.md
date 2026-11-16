# Running a Deployment

This is the main workflow: configure devices, hand them out, collect them, download the data.

If your devices have never been set up, do [Getting Started](getting-started.md) first.

---

## Before you begin

**Charge every device fully.** A device whose battery is below **3750 mV** will not begin recording — it
powers off instead and waits. Charging overnight avoids a whole class of confusing failures.

**Put every device on a charger** while you configure it. Devices are only awake and reachable when
plugged in; an unplugged device that is outside its study window powers itself off and will not appear
in a scan. This is deliberate — it is what makes the battery last — but it means configuration happens
on the charger, always.

**Know your device IDs.** You will pick devices out of a list by the end of their ID; the browser tool
shows the last two bytes, as **TotTag 00:02**. If you did not label the cases during setup, now is the
time.

---

## Install the dashboard

The desktop dashboard is the usual tool. It lives in the
[tottag-dashboard](https://github.com/hedgetechllc/tottag-dashboard) repository, separate from the
firmware:

```bash
git clone https://github.com/hedgetechllc/tottag-dashboard
cd tottag-dashboard/Python
python3 -m pip install -e .
tottag
```

There is also a [browser-based tool](dashboard.md#the-browser-tool) at
[app.tottag.net](https://app.tottag.net/). It is a web page, so there is
nothing to install, but it needs Chrome, Edge, or Opera on a computer to reach the devices. Both tools
configure devices identically and read the same logs; use whichever suits you.

---

## Step 1 — Schedule the deployment

With every device on a charger:

1. **Scan for TotTags.** Devices appear by ID. Wait until all of them are listed — a device that has
   just been plugged in can take a few seconds to show up.
2. **Schedule New Pilot Deployment.**
3. Fill in:

| Field | Meaning |
| --- | --- |
| **Deployment Timezone** | The time zone the study happens in. Every time you enter uses this clock, and changing it reinterprets the times already entered. |
| **Start / End Date and Time** | The outer bounds of the study. Nothing is recorded before the start or after the end. |
| **Daily Start / End Time** | Optional. Restricts recording to a window within each day, in the study's local time: school hours, say. Leave unset to record continuously between the start and end dates. |
| **TotTags in Deployment** | The devices taking part. **Maximum 10.** |

4. **Schedule.** The configuration is pushed to every device on the list, and read back over either USB or
   Bluetooth to confirm each one stored it.
5. **Save the deployment record** when the dashboard asks. Keep it in the folder you will download the
   logs to: it is the only place the study's time zone is kept, and both tools read from it to show
   times, charts, and daily figures on the study's clock.

A few things worth understanding:

- **The time zone is the study's, not yours.** Pick the time zone where the devices will be deployed,
  even if you are configuring them from somewhere else, and enter every time as a clock on the wall
  there would show it.
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

On **Set up a deployment**, give the study a name if you like, choose its time zone, dates, and daily
hours, then **Add a tag** for each device. As each one is added, the page reads its battery, its clock,
and any deployment it still holds, and the summary beside the form lists anything that needs
attention before writing. **Write to tags** writes each device in turn and reads it back, so every
device is checked, not just one. Save the **deployment record** it produces: it lists exactly what each
device confirmed it stored, with times in the study's time zone. Keep it with the logs, for the same
reason as above.

### Before you schedule over an earlier study

**Scheduling a device starts a fresh log.** Nothing is erased from the device's memory, but whatever
it recorded for the previous study can no longer be downloaded with either tool. Download every device
from the previous study first ([Step 4](#step-4--collect-and-download)). The browser tool knows which
studies' data it has saved, and will not write over one it has not seen saved unless you say the
data is safe elsewhere.

---

## Step 2 — Hand out the devices

Take them off the chargers.

Each device plays a sound as it comes off the charger, and from that moment behaves as follows:

- **Inside the study window** — it starts ranging and recording.
- **Before the study starts** — it powers off and wakes itself at the start time.
- **Outside a daily window** — it powers off and wakes at the next daily start.
- **Battery too low** — it powers off and waits for a charger.

Devices find each other automatically. There is no pairing step and no fixed roles — the network forms
itself, and reforms if devices are separated and brought back together.

**Charge devices overnight, not during study hours.** A device on its charger does not range, so nothing
is recorded while it is plugged in; taken off the charger, it starts again as described above. Overnight
charging is part of any multi-day study. Leaving a device on its charger during study hours is the most
common way to lose data.

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
| **Download Full Logs** | The whole of the current log, ignoring the study's dates: everything recorded since the device was last scheduled or canceled. Earlier studies cannot be downloaded. |

Downloads take minutes rather than seconds and produce a `.ttg` file per device, plus a processed
`.pkl` alongside it; the browser tool saves only the `.ttg` file. See [Your Data](data.md) for
what is inside.

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

## Step 5 — Check what you got before reusing the devices

Downloading is the last moment when a problem is still fixable. Before you clear devices for the next
study, confirm that each log:

- covers the dates you expect;
- contains all the devices you expect, each seeing the others;
- has no warnings left unresolved from the download.

[Your Data](data.md#checking-a-log-is-complete) shows how to check each of these, and the browser tool's
**What happened** tab shows them all at once. Ten minutes here saves a study.

---

## Canceling or changing a deployment

**Cancel Scheduled Pilot Deployment** clears the schedule from a device. Each device must be canceled
individually, on its charger. This is only necessary if you want to cancel a deployment that has not
yet occurred or has started but not yet ended. This is not necessary for simply storing or reusing
the devices in a new study.

To change a study's times or membership, schedule again; canceling first is not necessary.

Neither canceling nor scheduling erases a device's memory, but **both start a fresh log**: anything the
device recorded before can no longer be downloaded with either tool. Download it first. In the browser
tool, canceling is on the **Check a tag** page, which says whether the device's data has been saved
before it asks you to confirm.
