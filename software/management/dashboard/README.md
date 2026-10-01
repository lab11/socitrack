# Dashboard & Processing

## Timezone interpretation

- The timestamps stored in the processed log files (the `.pkl` files) are utc timestamps.
- The plotting functions in `processing.py` plots the time series according to the local timezone of the computer running the script. If you run the same script with the same file on computers from different timezones, the plot will look different. 

## Dashboard

The TotTag dashboard is a GUI for interacting with the tags.

To bring up the dashboard without installing it first, enter:
```bash
python3 tottag.py
```

If the dashboard has been installed, you can access it anywhere by entering:
```bash
tottag
```

### Disconnected state
To discover the devices, click the scan button.

To schedule a new experiment for a group of devices, the devices have to be placed on chargers, otherwise they will not be discovered by scanning.

Devices plugged into the computer over USB are discovered by the same scan and listed as `USB-Connected XX:XX:XX:XX:XX:XX`. They can be included in a deployment exactly like devices discovered over Bluetooth; the schedule is simply written to them over USB. A USB device running firmware that predates the USB UID command is listed as `USB-Connected Device` instead, and cannot be included in a deployment until its firmware is updated.

Once a deployment is scheduled, the schedule will be push to all devices.

### Connected state

Each device can be connected to individually.

In the connected state, any updates to the deployment only apply to the current connected device. 

To download the logs, the connected device  **needs to be on the charger**.

To view live ranging data, the connected device **should not be not on the charger**.

## Downloading logs

A log is offloaded over BLE as an unacknowledged notification stream, so the transport — not the
flash — is the only place a page is ever lost. The device holds the log until it is told otherwise,
so anything that did not survive the transfer can simply be asked for again.

After the stream ends, the tool re-parses what it has, works out which page sequence numbers are
missing, and asks the device to resend exactly those. It repeats until every still-missing page has
used up its own attempts:

```
MAX_PAGE_ATTEMPTS          6    attempts allowed for ONE page before it is given up on
DEVICE_RETRANSMIT_CAPACITY 256  pages the device can be asked for in a single round
MAX_REPAIR_ROUNDS          40   loop guard only, not the give-up policy
```

Retries are counted **per page**, not per round. A round that recovers most of what it asked for is
a bad but serviceable link and earns another round; a page asked for repeatedly that never arrives
is the only thing that indicates a real problem. Messages look like:

```
Requesting 256 of 930 missing page(s), round 1 (674 deferred to a later round)...
Requesting 256 of 674 missing page(s), round 2 (418 deferred to a later round)...
Giving up on 3 page(s) after 6 attempts each: [1841, 1902, 2210]
```

"deferred to a later round" is normal and not a loss: the device accepts at most
`NANDLOG_MAX_RETRANSMIT_PAGES` sequence numbers per round and silently discards any beyond that, so
the tool asks for no more than it can hold and keeps the remainder, with their attempts intact, for
the next round.

If a download fails outright, the error names what actually went wrong. A message about writing to a
directory means the filesystem; anything else means the transfer, and the log on the device is
untouched either way — reconnect and download again.

**The device must be on its charger to download.**

## Recovering logs from tags on pre-nandlog firmware

Firmware older than `cf7249d5` found its log at boot by pattern-matching page markers, and when that search
failed it wrote a fresh, empty metadata page and carried on. Such a tag reports that it has no log even
though the log is still on its flash. `legacy_recovery.py` gets it back from a raw dump of the flash.

It handles every firmware from April 2023 up to that commit. The changes in between are recognised from the
dump itself: the metadata layout, the record timestamps (Unix seconds before March 2024, milliseconds since
the start after), and the shorter bad-block reserve of Nov-Dec 2024. The report says which of each it found.
The only builds not covered are the transitional ones from March 1-18, 2024.

**Do this before flashing current firmware: that firmware reformats the flash.**

1. Flash the read-only recovery firmware (`firmware/tests/tools/legacy_log_recovery.c`). It reads every page
   and never programs or erases anything:
   ```
   cd software/firmware/tests
   make clean log_recovery            # revisions O and P, dumped over USB
   make clean log_recovery_segger     # any revision, dumped over a J-Link (M and N have no USB)
   ```
2. Take the dump and recover the log in one step:
   ```
   python3 legacy_recovery.py usb -o OUTDIR     # tag plugged in over USB
   python3 legacy_recovery.py rtt -o OUTDIR     # J-Link attached; needs JLinkRTTLoggerExe on the PATH
   ```
   Reading the whole flash takes a few minutes. Over RTT the tag dumps once per boot, so reset it before
   trying again.
3. Keep the `.ttrd` dump. It is the complete contents of the flash, and it can be analysed again later with
   `python3 legacy_recovery.py recover DUMP.ttrd -o OUTDIR` after the tag has been wiped.

The outputs are named the way a download is named. `<label>_<start>.pkl` is loaded like any other log.
`<label>_<start>_recovery.txt` lists what was on the flash and which pages were used, and gives the reason
for every break in the log. The log is split wherever two pages cannot be shown to have been written one
after the other: a missing or damaged page, or a partial page written at shutdown. Each piece is decoded
on its own, so a break costs at most the one record that straddled it, and a record is never assembled
from unrelated bytes.

Options for the harder cases:
- `--start-time UNIX` gives the start time when no metadata page survived. Without it, record times are
  seconds since the deployment started.
- `--meta-page N` recovers the deployment described by a particular metadata page.
- `--include-damaged` also decodes pages that failed ECC. Each one is decoded on its own and marked in the
  report, because its bytes are only probably right.
- `--timestamps relative|absolute` and `--reserved-blocks N` override the detected timestamp encoding and
  reserve size, if the report shows that a detection was wrong.

`python3 test_legacy_recovery.py` checks all of this against simulated flash images laid out exactly as the
old firmware wrote them. Once `make` has been run in `firmware/tests/tools/legacy_log_recovery_sim`, the
test also runs the recovery firmware's own code against the nandlog flash simulator and fails if that code
changes a single byte of the flash.

## Processing

### Statistics

```python
from processing import *

# load the logs
A = load_data("E1.pkl")
B = load_data("Pat.pkl")
C = load_data("SB.pkl")

# 3.0 is the touching distance threshold (could be modified)
# the unit could be 'ft' or 'm'
get_daily_ranging_statistics(A, ["Pat","SB"], 3.0, unit='ft')
get_daily_ranging_statistics(B, ["E1","SB"], 3.0, unit='ft')
get_daily_ranging_statistics(C, ["E1","Pat"], 3.0, unit='ft')
```

### Plots
```python
from processing import *
A = load_data("07.pkl")
B = load_data("09.pkl")

# plot the voltages
get_voltage_time_series(A, "07")
get_voltage_time_series(B, "09")

# plot the ranges
get_ranging_time_series(A, "07","09")
get_ranging_time_series(B, "09","07")

get_motion_time_series(A, "07")
get_motion_time_series(B, "09")
```

### Get the on and off charger time
```python
from processing import *
A = load_data("12043_S1.pkl")
get_off_and_on_charger_times(A,"10043_S1",visualize=False)

# to show the marking on the voltage plot
get_off_and_on_charger_times(A,"10043_S1",visualize=True)
```

### Visualizing paired logs with alignment

To visualize events, an event log needs to be prepared. The event log have to follow the following format
 - A single line (no `ENTER`/`RETURN` within the line) per event.
 - For each line, always start with the Y-m-d H:i:s timestamp.

```
2024-04-01 08:33:11 SB enters the room. E1 is propped up on the window sill. Pat is laying flat on the table. 
2024-02-01 08:34:58 SB leaves the room. 
```

```python
from processing import *

# load the logs
A = load_data("E1.pkl")
B = load_data("Pat.pkl")
C = load_data("SB.pkl")

# visualize paired logs
visualize_ranging_pair_slider(A,B,"E1","Pat")
visualize_ranging_pair_slider(A,C,"E1","SB")
visualize_ranging_pair_slider(B,C,"Pat","SB")

# extract events from the event log
events  = extract_simple_event_log("annotation_pst.txt")

# visualize the events
visualize_ranging_pair_slider(A,B,"E1","Pat", events=events)
visualize_ranging_pair_slider(A,C,"E1","SB", events=events)
visualize_ranging_pair_slider(B,C,"Pat","SB", events=events)

# visualize the events within [start_timestamp, end_timestamp]
visualize_ranging_pair_slider(A,B,"E1","Pat", events=events, start_timestamp=1708619580, end_timestamp=1708623540)
visualize_ranging_pair_slider(A,C,"E1","SB", events=events, start_timestamp=1708619580, end_timestamp=1708623540)
visualize_ranging_pair_slider(B,C,"Pat","SB", events=events, start_timestamp=1708619580, end_timestamp=1708623540)
```