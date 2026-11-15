# The Dashboard

Two tools can be used to configure devices and download data. They speak the same protocol
and read the same logs, so a device does not care which one you use.

| | Desktop dashboard | Browser tool |
| --- | --- | --- |
| Install | `pip install -e .` | None: open [lab11.github.io/socitrack/app](https://lab11.github.io/socitrack/app/) |
| Runs on | macOS, Linux, Windows | Chrome, Edge, Opera on any computer |
| Connection | Bluetooth or USB | Bluetooth or USB |
| Radio check | Live over Bluetooth | Live over Bluetooth, or from downloaded logs |
| Analysis | Plotting helpers in Python (`processing.py`) | Built in: a timeline, charts, daily figures, and comparing two devices |

The browser tool is convenient on machines on which you cannot install software, and
it is the only one that reads logs without Python and without installing any local
software.

---

## The desktop dashboard

### Installing

```bash
cd socitrack/software/management
python3 -m pip install -e .
```

Then run `tottag` from any directory. Dependencies — `bleak`, `matplotlib`, `numpy`, `pandas`,
`pyserial`, `scipy`, and others — install automatically.

### Finding devices

**Scan for TotTags** lists every device in range by ID.

A device only appears if it is **awake**, which in practice means **on a charger**. An unplugged device
outside its study window powers itself off and cannot answer. If a device is missing from the list, the
charger is the first thing to check.

**Connect** to a device to act on it. Everything below operates on the connected device alone.

### Setting up a study

| Action | What it does |
| --- | --- |
| **Schedule New Pilot Deployment** | Configures a study: start and end, optional daily window, timezone, and the devices taking part (**maximum 10**). Pushes it to every device listed. |
| **Get Scheduled Deployment Details** | Reads back what a device actually stored. Worth checking on at least one device. |
| **Cancel Scheduled Pilot Deployment** | Clears the schedule from the connected device. One device at a time. |

Set the **Deployment Timezone** before entering times — changing it reinterprets times already typed.

Scheduling and canceling both start a fresh log on the device. Nothing is erased from its memory, but
whatever it recorded before can no longer be downloaded with either tool, so **download a device before
scheduling or canceling it**.

### Checking on a device

| Action | What it does |
| --- | --- |
| **Get Device Details** | Firmware version, storage state, general health |
| **Retrieve Current Timestamp** | The device's clock, in UTC and local time. The clock is set when a deployment is scheduled, and lost whenever the device loses power. |
| **Current Device Voltage** | Battery, in mV. Below 3750 mV, a device will not start recording. |
| **Activate Find my TotTag** | Plays a tune so you can find a device |
| **Subscribe to Live Ranging Data** | Streams distances in real time. Requires the device **off** its charger, since it has to be ranging. |

### Checking devices' radios

**Live Radio Check** runs several devices in a short radio test and compares them, to find one with a
weak receiver, a faulty antenna, or a calibration that is off. It needs no deployment and downloads
nothing. It is available after a scan has found at least two devices over Bluetooth, while you are **not**
connected to one. Disconnect first if you are. Put the devices on their chargers before scanning, so the
scan can find them; a device running a deployment can be reached too.

1. Select the devices to test (up to 10) and choose how long to run: 2 to 10 minutes, 2 by default.
2. Optionally choose a **Layout** — a circle of a given radius, or a line at a given spacing, in feet.
   The **Position** column shows where each device should be placed. With positions, the check can tell
   whether one device reads long or short on every link.
3. **Start Test**. Each device restarts into the test and starts ranging.
4. Take the devices off their chargers, and set them out in the positions shown if you chose a layout.
   Unplugging a device does not interrupt the test. Do it promptly: the verdicts cover the whole test, so
   if setting out takes more than half a minute or so, choose a longer test.

The table updates every second with each device's status, the share of the last minute's rounds it
ranged in, and its receive failures, overall and per antenna. The share counts from a device's first
range, so the coordinator is not marked down for the rounds it ran alone before anyone joined. The
antenna it is currently using to hear the schedule is shown in brackets. A device not yet in the
network shows its role as *searching*. Distances are shown in feet and inches. The grid below it shows
how often each device ranged to each other device. Within the first minute each device gets a **Pass**,
**Check**, or **Fail** label, with the reasons listed underneath, and the verdicts update every 15
seconds. They firm up once every device has a minute of data; until then, a note says the run is still
too short to judge.

Scanning, connecting, and scheduling are locked while a test runs. **Stop Test** ends it early. Either
way, each device restarts back to whatever it was doing, even if the dashboard has been closed. See
[checking devices on the bench](troubleshooting.md#checking-devices-on-the-bench) for what each verdict
means.

### Downloading

The device must be **on its charger** to download.

| Action | What you get |
| --- | --- |
| **Download Deployment Log Files** | Data from the scheduled study. Normally what you want. Every download saves two files per device: the raw `.ttg` log, your archival copy, and a `.pkl` parsed for Python. See [Your Data](data.md). |
| **Download Full Logs** | The whole of the current log, ignoring the study's dates: everything recorded since the device was last scheduled or canceled. Earlier studies cannot be downloaded. |

Downloads take minutes. Watch the console — see
[Running a Deployment](running-a-deployment.md#watch-the-console-while-it-downloads) for what the
messages mean.

### Analysis helpers

`processing.py` in the dashboard directory has plotting helpers:

```python
from processing import *

A = load_data("02.pkl")
B = load_data("3E.pkl")
visualize_ranging_pair_slider(A, B, "Parent", "Child")

events = extract_simple_event_log("annotations.txt")
visualize_ranging_pair_slider(A, B, "Parent", "Child", events=events)
```

Timestamps inside a `.pkl` are in UTC. The plots, and the daily statistics from
`get_daily_ranging_statistics`, show them on the **study's clock**: in the time zone the study was
scheduled in, with each day running from midnight to midnight there. The same file looks the same
wherever it is plotted. Times in an events file are read on that same clock. On a day the clocks change,
the plots follow the clock, so an hour repeats or is skipped. Minutes in the daily statistics are rounded
to the nearest whole minute.

That time zone is kept in only one place: the **deployment record**, a `.json` file saved when a study is
scheduled. Neither the devices nor their logs store it. The desktop dashboard asks where to save the record
as soon as the devices have been written, and the browser tool offers one too; a record from either tool
works. **Save it in the folder you download the logs to**, because that is where `load_data` looks. It
reads the `.ttg` saved next to the `.pkl` and picks the record holding exactly the deployment details the
device stored, or failing that, one that started at the same time and lists that device.

If the record is somewhere else, point to it, or give the time zone yourself. Every plotting and
statistics helper also takes `timezone=`:

```python
A = load_data("02.pkl", records="~/studies/family-10051")
A = load_data("02.pkl", timezone="America/Chicago")
get_daily_ranging_statistics(A, ["Parent"], 3, timezone="UTC")
```

With no record and no time zone given, times are shown in the local time zone of the machine running
them, and a one-line note says so.

---

## The browser tool

Open **[lab11.github.io/socitrack/app](https://lab11.github.io/socitrack/app/)**. It is an ordinary web
page: there is nothing to install, and it does not upload your data anywhere — everything happens in
the page, on your computer.

Reading and analyzing logs works in any browser. **Connecting to a device needs Web Bluetooth or Web
Serial**, which exist only in Chrome, Edge, and Opera on a computer; the tool says so up front rather
than failing at the moment you need it. Opening a whole folder of logs at once needs the same browsers;
elsewhere it falls back to a file picker. What you enter is kept in the browser between visits.

It has four pages:

| Page | What it does |
| --- | --- |
| **Set up a deployment** | Name, time zone, dates, and daily hours, all in the study's local time, and the devices taking part. Shows each device's battery and any deployment it still holds as it is added, warns before writing over a study whose data has not been saved, and writes and reads back every device. Produces a **deployment record** of what each device confirmed. |
| **Check a tag** | One device's battery, clock, firmware, and the deployment it holds, read back. **Buzz** to find it; **cancel** its deployment, with a warning if its data has not been saved. |
| **Download & review** | Download a device's log over Bluetooth or USB, or open `.ttg` files, and see for each one the dates it covered against those scheduled, which of the other devices it measured distances to, and anything that needs attention, explained in plain language. Opened with its deployment record, it adds a timeline, charts, and daily figures (see below). |
| **Radio check** | See below. |

Devices are named by the last two parts of their ID, such as **TotTag 00:0A**, matching the end of the
ID on the device's label.

Its **Radio check** page compares devices against each other to find one with a weak receiver, a faulty
antenna, or a calibration that is off. Like the desktop dashboard's **Live Radio Check**, it can run a
short live test over Bluetooth, with no deployment and nothing to download. It can also read the logs of
a deployment that already ran. See
[checking devices on the bench](troubleshooting.md#checking-devices-on-the-bench).

### Reviewing a deployment

Open the **deployment record**, the `.json` saved when the devices were set up by either tool, together
with the logs: drop it in with them, browse for it, or keep it in the same folder and open the whole
folder. Every time on the page is then shown in the study's own time zone, and a note at the top says
which. Without a matching record, the page uses the time zone on the setup page, says so, and offers
**Add the deployment record**. Logs from more than one deployment are grouped, with a choice of which to
show.

Each deployment has five tabs:

| Tab | What it shows |
| --- | --- |
| **Tags** | One card per device: the dates it covered, which devices it measured distances to, and anything that needs attention |
| **What happened** | A short verdict; a timeline of every device's scheduled hours, recording, charging, missed time, restarts, and clock corrections, with each restart explained; an hour-by-day grid of when each device was near others, recording alone, charging, or missing scheduled time; battery and chip temperature; and a health table |
| **Charts** | One device at a time: battery, with charging and restarts marked; distance to a chosen device, minute by minute; moving or still; how many devices it heard over Bluetooth; and the accelerometer |
| **Time together** | Daily minutes in range and within touching distance, and the average distance, for chosen devices. Exports to CSV. See [Your Data](data.md#daily-figures) |
| **Compare two tags** | Device A measuring B and B measuring A, across the whole deployment and in a full-resolution window from 10 seconds to an hour |

Drag across a chart to zoom in (**Show the whole deployment** undoes it), and hover for exact values.
Charging comes from each device's own plugged and unplugged records, not from the shape of its battery
curve.

Settings shared by every tab: distances in feet or meters; **Leave out distances over** (30 ft by
default); **Touching distance is within** (3 ft by default); and **Add a notes file**, a text file of
`YYYY-MM-DD HH:MM:SS what happened` lines on the study's clock, drawn on every chart, with **Move them
by … seconds** to line them up. Lines in any other form are skipped, and the page says how many. Unlike
the setup page, nothing here is kept after the page closes.

---

## Which tool wrote my log?

It does not matter. Both write the same `.ttg` format and both read logs written by the other. Where
the two must agree on a detail of the format, an automated test asserts it against the other's source
directly, so they cannot quietly drift apart.
