# The Dashboard

Two tools configure devices and download data. They speak the same protocol and read the same logs, so
a device does not care which one you use.

| | Desktop dashboard | Browser tool |
| --- | --- | --- |
| Install | `pip install -e .` | None |
| Runs on | macOS, Linux, Windows | Chrome, Edge, Opera on a computer |
| Connection | Bluetooth or USB | Bluetooth |
| Radio check | Live over Bluetooth | Live over Bluetooth, or from downloaded logs |
| Analysis helpers | Yes — plotting, event overlays | No |

Use the desktop dashboard unless you have a reason not to. The browser tool is convenient on a machine
you cannot install software on, and it is the only one that reads logs without Python.

---

## The desktop dashboard

### Installing

```bash
cd socitrack/software/management
python3 -m pip install -e .
```

Then run `tottag` from any directory. Dependencies — `bleak`, `matplotlib`, `numpy`, `pandas`,
`pyserial`, `scipy` and others — install automatically.

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
| **Cancel Scheduled Pilot Deployment** | Clears the schedule from the connected device. One device at a time. Does not delete recorded data. |

Set the **Deployment Timezone** before entering times — changing it reinterprets times already typed.

### Checking on a device

| Action | What it does |
| --- | --- |
| **Get Device Details** | Firmware version, storage state, general health |
| **Retrieve Current Timestamp** | The device's clock, in UTC and local time. A device whose clock is unset has never been scheduled. |
| **Current Device Voltage** | Battery, in mV. Below 3750 mV a device will not start recording. |
| **Activate Find my TotTag** | Plays a tune so you can find a device |
| **Subscribe to Live Ranging Data** | Streams distances in real time. Requires the device **off** its charger, since it has to be ranging. |

### Checking devices' radios

**Live Radio Check** runs several devices in a short radio test and compares them, to find one with a
weak receiver, a faulty antenna or a calibration that is off. It needs no deployment and downloads
nothing. It is available after a scan has found at least two devices over Bluetooth, while you are **not**
connected to one. Disconnect first if you are.

1. Tick the devices to test (up to 10) and choose how long to run: 2 to 10 minutes, 2 by default.
2. Optionally choose a **Layout** — a circle of a given radius, or a line at a given spacing, in feet —
   and set the devices out in the order listed, at least 2 feet apart. The **Position** column shows where
   each one goes. With positions, the check can tell whether one device reads long or short on every link.
3. **Start Test**. Each device restarts into the test and starts ranging, on its charger or off it.

The table updates every second with each device's status, the share of the last minute's rounds it
ranged in, and its receive failures, overall and per antenna. The share counts from a device's first range, so
the coordinator is not marked down for the rounds it ran alone before anyone joined. The antenna it is currently using to hear
the coordinator is shown in brackets. A device not yet in the network shows its role as *searching*. Distances are shown in feet and inches. The grid below it shows how often each device ranged to each other device. Within
the first minute each device gets a **Pass**, **Check** or **Fail**, with the reasons listed underneath,
and the verdicts update every 15 seconds. They firm up once every device has a minute of data; until
then a note says the run is still too short to judge. The verdicts follow the same rules, in the same words, as the browser tool's radio check.

A device whose firmware predates radio tests is marked **Failed** at the start and left alone. One that
cannot be reached after restarting into the test says so, and is still tried until the test ends.

Scanning, connecting and scheduling are locked while a test runs. **Stop Test** ends it early. Either
way, each device restarts back to whatever it was doing, even if the dashboard has been closed. See
[checking devices on the bench](troubleshooting.md#checking-devices-on-the-bench) for what each verdict
means.

### Downloading

The device must be **on its charger** to download.

| Action | What you get |
| --- | --- |
| **Download Deployment Log Files** | Data from the scheduled study. Normally what you want. |
| **Download Full Logs** | Everything on the device, including earlier studies. |
| **Download Raw Unprocessed Data** | The `.ttg` stream with no processing applied. |

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

Timestamps inside a `.pkl` are UTC, but these plots render in the **local timezone of the machine
running them**. The same file plotted in two timezones produces two different-looking figures. Fix the
timezone explicitly if that matters.

---

## The browser tool

```bash
cd socitrack/software/managementweb
npm install
npm run dev
```

Then open the address it prints. Node 22.6 or newer is required.

Reading and analysing logs works in any browser. **Writing to a device needs Web Bluetooth**, which
exists only in Chrome, Edge and Opera on a computer — the tool says so up front rather than failing at
the moment you need it. Opening a whole folder of logs at once needs the same browsers; elsewhere it
falls back to a file picker.

The browser tool does not install anything and does not upload your data anywhere — it runs entirely
in the page.

Its **Radio check** tab compares devices against each other to find one with a weak receiver, a faulty
antenna or a calibration that is off. Like the desktop dashboard's **Live Radio Check**, it can run a
short live test over Bluetooth, with no deployment and nothing to download. It can also read the logs of
a deployment that already ran. See
[checking devices on the bench](troubleshooting.md#checking-devices-on-the-bench).

---

## Which tool wrote my log?

It does not matter. Both write the same `.ttg` format and both read logs written by the other. Where
the two must agree on a detail of the format, an automated test asserts it against the other's source
directly, so they cannot quietly drift apart.
