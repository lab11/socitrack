# The Dashboard

Two tools configure badges and download data. They speak the same protocol and read the same logs, so
a badge does not care which one you use.

| | Desktop dashboard | Browser tool |
| --- | --- | --- |
| Install | `pip install -e .` | None |
| Runs on | macOS, Linux, Windows | Chrome, Edge, Opera on a computer |
| Connection | Bluetooth or USB | Bluetooth |
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

### Finding badges

**Scan for TotTags** lists every badge in range by ID.

A badge only appears if it is **awake**, which in practice means **on a charger**. An unplugged badge
outside its study window powers itself off and cannot answer. If a badge is missing from the list, the
charger is the first thing to check.

**Connect** to a badge to act on it. Everything below operates on the connected badge alone.

### Setting up a study

| Action | What it does |
| --- | --- |
| **Schedule New Pilot Deployment** | Configures a study: start and end, optional daily window, timezone, and the badges taking part (**maximum 10**). Pushes it to every badge listed. |
| **Get Scheduled Deployment Details** | Reads back what a badge actually stored. Worth checking on at least one badge. |
| **Cancel Scheduled Pilot Deployment** | Clears the schedule from the connected badge. One badge at a time. Does not delete recorded data. |

Set the **Deployment Timezone** before entering times — changing it reinterprets times already typed.

### Checking on a badge

| Action | What it does |
| --- | --- |
| **Get Device Details** | Firmware version, storage state, general health |
| **Retrieve Current Timestamp** | The badge's clock, in UTC and local time. A badge whose clock is unset has never been scheduled. |
| **Current Device Voltage** | Battery, in mV. Below 3750 mV a badge will not start recording. |
| **Activate Find my TotTag** | Plays a tune so you can find a badge |
| **Subscribe to Live Ranging Data** | Streams distances in real time. Requires the badge **off** its charger, since it has to be ranging. |

### Downloading

The badge must be **on its charger** to download.

| Action | What you get |
| --- | --- |
| **Download Deployment Log Files** | Data from the scheduled study. Normally what you want. |
| **Download Full Logs** | Everything on the badge, including earlier studies. |
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

Reading and analysing logs works in any browser. **Writing to a badge needs Web Bluetooth**, which
exists only in Chrome, Edge and Opera on a computer — the tool says so up front rather than failing at
the moment you need it. Opening a whole folder of logs at once needs the same browsers; elsewhere it
falls back to a file picker.

The browser tool does not install anything and does not upload your data anywhere — it runs entirely
in the page.

Its **Radio check** tab compares the badges of one deployment against each other to find one with a
weak receiver, a faulty antenna or a calibration that is off. See
[checking badges on the bench](troubleshooting.md#checking-badges-on-the-bench).

---

## Which tool wrote my log?

It does not matter. Both write the same `.ttg` format and both read logs written by the other. Where
the two must agree on a detail of the format, an automated test asserts it against the other's source
directly, so they cannot quietly drift apart.
