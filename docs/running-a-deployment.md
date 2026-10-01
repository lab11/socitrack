# Running a Deployment

This is the main workflow: configure badges, hand them out, collect them, download the data.

If your badges have never been set up, do [Getting Started](getting-started.md) first.

---

## Before you begin

**Charge every badge fully.** A badge whose battery is below **3750 mV** will not begin recording — it
powers off instead and waits. Charging overnight avoids a whole class of confusing failures.

**Put every badge on a charger** while you configure it. Badges only stay awake and reachable when
plugged in; an unplugged badge that is outside its study window powers itself off and will not appear
in a scan. This is deliberate — it is what makes the battery last — but it means configuration happens
on the charger, always.

**Know your badge IDs.** You will pick badges out of a list by the last two hex digits of their ID. If
you did not label the cases during setup, now is the time.

---

## Install the dashboard

The desktop dashboard is the usual tool:

```bash
cd socitrack/software/management
python3 -m pip install -e .
tottag
```

There is also a [browser-based tool](dashboard.md#the-browser-tool) that needs no installation but
requires Chrome, Edge or Opera on a computer. Both configure badges identically and read the same
logs; use whichever suits you.

---

## Step 1 — Schedule the deployment

With every badge on a charger:

1. **Scan for TotTags.** Badges appear by ID. Wait until all of them are listed — a badge that has
   just been plugged in can take a few seconds.
2. **Schedule New Pilot Deployment.**
3. Fill in:

| Field | Meaning |
| --- | --- |
| **Deployment Timezone** | The timezone your start and end times are written in. Set this first — it reinterprets the times you have already entered. |
| **Start / End Date and Time** | The outer bounds of the study. Nothing is recorded before the start or after the end. |
| **Daily Start / End Time** | Optional. Restricts recording to a window within each day — school hours, say. Leave unset to record continuously between the start and end dates. |
| **TotTags in Deployment** | The badges taking part. **Maximum 10.** |

4. **Schedule.** The configuration is pushed to every badge on the list.

A few things worth understanding:

- **The timezone is yours, not the badge's.** Pick the timezone the study happens in. Badges work in
  UTC internally and the dashboard converts.
- **Daily windows may cross midnight.** A daily window of 22:00 to 06:00 is understood as overnight,
  not as an error.
- **Every badge must be scheduled.** A badge that was not on the charger when you pressed Schedule
  does not know about the study and will not take part. Scan again and confirm the list.

Use **Get Scheduled Deployment Details** on a badge afterwards to read back what it actually stored.
It is worth doing on at least one badge, and on all of them if the study matters.

---

## Step 2 — Hand out the badges

Take them off the chargers.

Each badge plays a sound as it comes off the charger, and from that moment behaves on its own:

- **Inside the study window** — it starts ranging and recording.
- **Before the study starts** — it powers off and wakes itself at the start time.
- **Outside a daily window** — it powers off and wakes at the next daily start.
- **Battery too low** — it powers off and waits for a charger.

Badges find each other automatically. There is no pairing step and no fixed roles — the network forms
itself, and reforms if badges are separated and brought back together.

**Keep badges apart from their chargers during the study.** A badge put back on a charger stops
recording for as long as it is plugged in. That is the single most common way to lose data.

---

## Step 3 — During the study

Nothing is required. There is no live upload and no server; the data sits on each badge.

Two things are available if you need them, both requiring the badge in hand:

- **Subscribe to Live Ranging Data** shows distances in real time. The badge must be **off** its
  charger for this, since it has to be ranging.
- **Activate Find my TotTag** makes a badge chirp so you can find it.

---

## Step 4 — Collect and download

Put each badge back on its charger — this both stops recording and is required for downloading.

In the dashboard: **Connect** to a badge, then choose a download:

| Action | What you get |
| --- | --- |
| **Download Deployment Log Files** | Everything recorded during the scheduled study. This is normally what you want. |
| **Download Full Logs** | Everything on the badge, including data from before this study. |
| **Download Raw Unprocessed Data** | The raw `.ttg` stream exactly as the badge sent it, with no processing. |

Downloads take minutes rather than seconds and produce a `.ttg` file per badge, plus a processed
`.pkl` alongside it. See [Your Data](data.md) for what is inside.

### Watch the console while it downloads

Occasionally a page of the log does not survive the transfer. The tool notices, asks the badge to
resend, and tells you what it is doing:

```
Requesting 7 of 7 missing page(s), round 1...
```

That is normal and self-correcting. What is **not** normal is a message like:

```
WARNING: log from 31 is incomplete:
   transfer ended early: 8 of 10 pages received
```

If you see that, **download the badge again before wiping anything**. The data is still on the badge.
A second attempt usually succeeds. See
[Troubleshooting](troubleshooting.md#a-download-reported-missing-pages).

---

## Step 5 — Check what you got before you let the badges go

Downloading is the last moment when a problem is still fixable. Before you clear badges for the next
study, confirm that each log:

- covers the dates you expect;
- contains all the badges you expect, each seeing the others;
- has no warnings left unresolved from the download.

[Your Data](data.md#checking-a-log-is-complete) shows how to check each of these. Ten minutes here
saves a study.

---

## Cancelling or changing a deployment

**Cancel Scheduled Pilot Deployment** clears the schedule from a badge. Each badge must be cancelled
individually, on its charger.

To change a study's times or membership, cancel and schedule again. Recorded data is not deleted by
either action.
