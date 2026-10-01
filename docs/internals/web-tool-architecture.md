# Web Tool Architecture

Design notes for `software/managementweb`. This is developer documentation: how the browser tool keeps
its understanding of the log format in step with the firmware, and the history of how it was built.

For using the tool, see [The Dashboard](../dashboard.md#the-browser-tool).

---

## The three enforcement mechanisms

These exist because each one corresponds to a way this kind of project has gone wrong before.

### 1. Drift detection against the firmware

`firmware/src` is the source of truth for every format decision. `packages/tottag-schema/tools/spec.mjs`
declares exactly which firmware facts this codebase depends on and where each lives;
`extract-constants.mjs` reads them out of the C source and writes `constants.snapshot.json`, which is
committed. `src/constants.ts` reads that snapshot and never hardcodes a value.

Three distinct failures, but only one of them is a test:

| Failure | Caught by |
|---|---|
| Firmware changes a value we depend on | `committed snapshot matches the current firmware` (a test) |
| Firmware renames or deletes something | `extract()` throws and exits 1 |
| The extractor stops extracting something | `extract()` reconciles its own output against spec.mjs and exits 1 |

The last two are **structural, not tested**. `extract()` will not return a snapshot that is missing
anything spec.mjs declares, so softening an individual lookup — which is how it nearly happened once
— produces an incomplete snapshot the extractor refuses to emit. That removes the failure mode
rather than watching for it, and there is no test to disable alongside the check.

Verified by doing it: softening a lookup *and* renaming the firmware constant leaves the committed
snapshot untouched and exits 1.

### 1a. Provenance of every constant

`src/provenance.ts` classifies every export of `constants.ts` as either snapshot-backed or
host-only, and `provenance.test.ts` enforces **both directions**. Anything exported that is not
extracted must appear in `HOST_ONLY_CONSTANTS` with a reason; anything in the ledger that is no
longer exported fails too.

The second direction is the one that is easy to leave out and matters more. Without it, a number
typed into the host because it was needed sits alongside extracted firmware facts and is
indistinguishable from one. Categories are `derived` (must name its inputs, re-derived in a test),
`external-standard`, `measured`, `legacy-format`, and `mirrors-unextractable-source`.

### 2. The guess registry

`src/guesses.ts` holds one entry per placeholder, estimate, or unverified assumption, each stating
what it blocks and what specific observation would close it. Code that depends on one wraps the
value in `guess('id', value)`.

`guesses.test.ts` enforces both directions: a `guess()` call with no registry entry fails, and an
`open` entry that nothing references fails. The second catches an assumption that was silently
removed, leaving the registry claiming something is provisional when it is not.

Guesses whose consuming code has not been written yet are listed in `PENDING_IMPLEMENTATION` with
the increment that will consume them named. That list must shrink to empty, and a separate test
fails if it names a guess that no longer exists.

### 3. Record-grammar parity with the firmware

The constant snapshot cannot see a function, and the record grammar is now a function in three
places: `stored_record_length()` in the firmware (it walks a page in `recover_time_anchor()`),
`recordLength()` here, and `_record_length()` in the Python tool.

A length disagreement is not a one-record error — the reader advances by the length it computed, so
a wrong length desynchronises every record after it. `extract-constants.mjs` therefore parses the
firmware's switch into the snapshot, and `record-grammar.test.ts` drives `recordLength()` with
synthetic records and compares behaviour rather than text.

The extractor recognises exactly the two shapes the firmware uses and **fails on anything else**,
deliberately: a grammar parser that falls back to a guess produces a reader that runs and lies.

### 4. Parity checks against the Python tool

`python-tool-parity.test.ts` reads `software/management/dashboard/*.py` and asserts each line of the
disagreement audit, in two flavours:

- `AGREES` — the Python tool and the firmware match, and this package matches both.
- `DIVERGES` — they are known to disagree, and the assertion pins the divergence as it stands today.

**A `DIVERGES` test failing is good news.** It means someone fixed the Python tool and the audit
needs updating. It does not mean this package is wrong. The Python tool is read, never written.

## Increments

### Increment 0 — enforcement machinery (complete)

Workspace, CI gate, constant extraction and drift detection, guess registry, Python parity audit.
No parsing code yet, deliberately: the machinery that will judge the parser exists before the parser
does.

Verified by deliberate breakage rather than by assertion:

- Changed `MAX_VALID_RANGE_MM` in the firmware from 32000 to 40000 -> drift test failed; restored.
- Renamed `MAX_NUM_RANGING_DEVICES` -> extraction threw and named the missing constant; restored.
- Neutered the extractor to skip missing constants, renamed `DEVICE_TIMEOUT_SECONDS`, regenerated
  and committed the now-narrower snapshot -> tests 1, 2, 4 and 5 all passed, and only
  `every spec entry is present in the snapshot` caught it. Restored.
- Added a `guess()` call for an unregistered id -> failed, naming file and line. Removed.
- Added a `PENDING_IMPLEMENTATION` entry for a nonexistent guess -> failed. Removed.

41 tests, 0 failures. The firmware tree was left exactly as it was found.

### Increment 0.5 — repointing at the firmware as it actually is (complete)

**Everything in increment 0 worked and the package still went stale, which is worth understanding
before trusting the machinery again.**

Between increment 0 and now, the firmware extracted its log into `src/external/nandlog` and deleted
the whole `MEMORY_*` family, the `peripherals/include/storage.h` header they lived in, and the EVB
board revision. Two of the three enforcement mechanisms did their job and said nothing useful,
because the third had already failed: `extract-constants.mjs` could not run at all
(`revision REVISION_APOLLO4_EVB not found`), so the drift test never got as far as comparing
anything. A broken extractor and a stale snapshot are indistinguishable from the outside.

By the time it was caught, the committed snapshot claimed `STORAGE_NUM_TYPES: 7` against a firmware
value of 9, and `MEMORY_NUM_DATA_BYTES_PER_PAGE: 4092` against a real v2 payload capacity of 4064.
**A reader built on it could not have decoded page 0 of any current file**: with record framing off,
a payload is walked by deriving each record's length from its type, and types 7 (`RESET_REASON`) and
8 (`TIME_ANCHOR`) are both written at every boot.

The lesson, recorded because it changes how the gate should be read: **a red extractor is a red
build, not a chore.** `npm run drift` failing is the same severity as a failing test, and the
temptation to defer it is exactly how a checker stops checking.

What changed:

- `REVISIONS` drops EVB, and the extractor now also fails if `boards/revisions.h` defines a revision
  the spec does **not** list — the reverse direction, which nothing checked.
- Flash geometry moved from `PER_REVISION` to `PER_CHIP`, because it belongs to the fitted part
  rather than the board, and `nandlog` identifies the part at runtime. Payload capacity per page is
  derived from the *measured* `nandlog_page_header_t`, so it cannot drift away from the header.
- The v2 format's magics, header layouts and policy constants moved out of `V2_SPEC_PENDING`, where
  they were prose transcribed from a design document, into extracted and drift-checked facts.
- Added the watchdog configuration and `reset_diagnostic_t` / `watchdog_task_t`, with a check that
  the per-task stall codes stay contiguous with the task list. A reordering there would silently
  rename every stall in every archived log.
- The struct extractor's body pattern was `[\s\S]*?`, which anchored at the first `typedef struct`
  in a file and spanned forward to reach the requested name — merging the fields of every struct in
  between. `nandlog.h` declares four in a row, so this was not hypothetical.

### Increment 1 — the readers (complete)

Blocked on real `.ttg` sample data, which now exists: a 3.9-day four-device deployment
(`doc/Storage_Redesign.md` §15.8). Two contiguous runs of pages from device 02 are committed under
`test/fixtures/`, with only the stream header's page count rewritten.

- `src/log.ts` — `parseV1`, `parseV2`, format detection, retransmission bookkeeping
  (`missingSeqs`, `extractPages`), and `parseExperimentDetails`. Handles both stream versions:
  unframed (what ships) and per-record framed.
- `src/health.ts` — `analyseDeployment` and `compareLink`.

**Two deliberate departures from the Python tool**, both settled by what the real corpus showed:

1. **Records come back in write order.** The Python parser keys `log_data[timestamp]`, which merges
   different record types at one instant correctly but silently drops the older of two records of
   the *same* type. `mergeByTimestamp()` reproduces the old shape for `.pkl` consumers, and its test
   pins how much that costs.
2. **The v1 500 ms grid heuristic is v1-only.** Current firmware stores the RTC's 10 ms resolution,
   so applying it to a v2 page rejects essentially every record.

### Increment 2 — what a log says about the deployment (complete)

`analyseDeployment` exists because the 3.9-day audit was done by hand, and everything it found is
something a person had to notice. Each is now a check:

| It reports | Because |
|---|---|
| Holes, CRC failures, short pages, sequence gaps, truncation | The v2 format was built so a loss has a known size and position; not surfacing that wastes it |
| Every reboot, with its cause and the firmware's own verdict | A reboot used to be inferred from a gap |
| The **shape** of a watchdog stall | Silence under the flush timeout means one task stalled while the rest kept logging; silence past it means the device stopped. Very different costs, and neither signal says it alone |
| The **outage**, from the anchors bracketing the reset | Measured on the device's own clock, which the reboot does not touch — the thing the time anchor was added for |
| Whether the charger caused a reboot | Plug and unplug both reset the tag. Every boot also writes its charge state, so the record at the reset's own millisecond is *not* the cause; the one before it is |
| Network-relative clock drift, and whether the RTC restarted | The offset absorbs drift silently, and a backward step in the device's own clock means power was actually removed |
| 300 s cadence capture, against 299.38 s | The FreeRTOS tick divisor truncates, so checking against 300 reports a shortfall on a healthy device |
| A charging-event storm | One device emitted 19,677 `NotCharging` records at 2 Hz from an unconditional ISR |
| Page fill and pages/day | Half a real deployment is an idle tag spending a 4 kB page on 18 bytes every 300 s |

`compareLink` scores the link between two devices from both ends. A range is one physical
measurement logged twice, so it is the only check on shared time that a single file cannot make.

### Increment 2.5 — repointing after the ranging rewrite (complete)

**The gate was red for three days and nobody noticed, which is the finding.**

`2f7ab50f` "Scheduler critical fixes" introduced `RANGING_ROUNDS_PER_SECOND` and expressed
`MAX_EMPTY_ROUNDS_BEFORE_STATE_CHANGE` in terms of it. The extractor could not resolve the new
identifier, so `npm run ci` failed at its first step from that commit onward.

Increment 0.5 had already recorded the lesson — "a broken extractor must be treated as a red build,
not as a chore to defer" — and it happened again anyway. The mechanism was not at fault: an audit of
all ~90 spec entries found that one constant was the *only* thing wrong, and every other extracted
fact still matched. What was missing is that nothing runs the gate when firmware changes. By
deliberate decision that stays manual: a firmware contributor who has never opened this directory
should not get a red CI run they cannot interpret. **Run `npm run ci` when you touch `firmware/src`.**

Also in this increment:

- The record grammar is extracted and asserted (mechanism 3 above). Verified by breaking it three
  ways: changing a length, adding a record type with no length rule, and rewriting the function into
  a shape the parser does not recognise. The first is caught by the drift test, and then by the
  grammar test once the snapshot is regenerated; the other two fail extraction outright.
- Six transport and health constants added to the spec ahead of the transports that need them, so
  the numbers are drift-checked before anything depends on them. `BLE_MAINTENANCE_MAX_SEQS_PER_WRITE`
  is the important one: the firmware applies `MIN(count, 60)` and silently ignores the excess, so a
  host that sends more loses the remainder with no error. `STORAGE_QUEUE_MAX_NUM_ITEMS` was
  considered and **dropped** — at the time no diagnostics counter recorded a queue overflow, so it
  was not observable from a log. (The diagnostics record now carries `storage_records_dropped`.)
- **A stale claim in `health.ts` was corrected.** The charger-storm message told the reader the
  charge-status ISR had "no comparison against the last reported state and no debounce". Both were
  fixed in firmware by `626b34bc`, and the message was not, so it spent that time advising
  researchers to expect a defect that no longer existed. Prose about firmware behaviour is the one
  thing none of the mechanisms above can see; the replacement is pinned to
  `BATTERY_EVENT_DEBOUNCE_MS` so at least the number cannot drift.
- **The 21-day deployment limit was measured and is not what it is believed to be.** See
  `experiment-duration-limit` in the guess registry: the format limit is 49.71 days, and 21 days
  leaves 28.71 days of headroom. No duration check exists anywhere in `firmware/src`.

### Increment 3 — the app shell and the import path (complete)

React + TypeScript + Vite, hand-written design tokens, no CSS framework. Works offline in every
browser including Safari, because file import is the one path that must not depend on Chromium.

- `ports/logSource.ts` — what a log source is, with no idea what a file or a radio is. Web Serial
  and Web Bluetooth become adapters behind this rather than a rewrite of whatever consumed files.
- `adapters/` — the only two modules that touch a browser API: the file reader and the downloader.
  Both use the widely-supported API rather than the better one, deliberately.
- `features/` — status classification, restart wording, CSV generation. Pure, and tested under
  `node:test` without a DOM, which is the practical proof that they are pure.

Verified by rendering it and looking at it, not by assuming:

| Found by looking | Fixed |
|---|---|
| "1 need attention" | Subject-verb agreement on the count |
| "All of it" and "—" rendered in a monospace face, reading as code literals | Mono now applies only to values that are actually numeric |
| Six stats auto-fitting into a 4 + 2 wrap, which reads as a layout accident | Fixed 3 x 2 grid |
| A card reading "Restarts: 1" beside "Looks good", with nothing joining them | Every restart now says what kind it was: "1 from a stall", "1 ordinary" |

Checked against the real fixtures end to end: both parse, the watchdog run is classified as needing
attention and the clean run is not, and the exported CSV carries the same numbers the screen shows.

### Increment 3.5 — corrections from a prose audit (complete)

Every user-facing message was re-checked against the current firmware. Five were wrong:

| Message | Was | Firmware says |
|---|---|---|
| `charger-pin-chatter` | de-bounce "kept them out of the log, so the records look clean" | `battery_monitor_poll_charger_state()` flushes deferred changes each heartbeat, so the transition DOES reach the log, just stamped late |
| `pages-lost` | "exactly the pages retransmission can recover" | `nandlog_retrieve_retransmit_page_locked` returns a zero-length frame for a page still unreadable — only transit corruption is recoverable |
| `cadence-shortfall` | "the expected 300 s heartbeat" | it measures against 299.38 s, so it quoted a number it was not using |
| `watchdog-near-miss` | "each is a stall" | a decline is one health evaluation; a stall spanning several counts more than once |
| `rtc-restarted` | "which the RTC cannot do while it is running" | `live_stats_functionality.c:31` writes the RTC from a BLE characteristic with no guard against an active run |

`watchdog-reset` also explained what a whole-system stall means on logs containing none; it now
describes only the shapes present.

`prose.test.ts` is the guard that came out of this. It cannot verify prose in general, but it can
check three things mechanically: every firmware symbol a message names still exists; no message
hardcodes a number the constants carry; and claims that were once wrong stay gone. That last is a
ratchet rather than a proof — it only stops the same error returning — which is the right shape,
because the failure was not someone inventing a falsehood but a true sentence going stale unread.

### Increment 3.6 — the File System Access upgrade (complete)

Import and export now feature-detect. Where `showDirectoryPicker` exists, "open a whole folder"
appears and reads every `.ttg` in one gesture; where `showSaveFilePicker` exists, the user chooses
where an export goes. Neither is available in Safari or Firefox, so `<input type="file">` and a Blob
object-URL download remain the floor. Verified both ways in Chromium, the fallback by deleting the
three entry points before any app code runs.

### Increment 4 and 5 — configuring a deployment, and writing it over Bluetooth (complete)

`packages/tottag-schema/src/deployment.ts` holds the configuration model, its validation and the
239-byte encoder; `manifest.ts` builds the record of what was deployed; `app/src/adapters/webBluetooth.ts`
is the transport.

**The short-UID check is the one that matters.** The ranging protocol and the log format both
identify a peer by ONE byte — the low byte of its EUI. `compute_ranges()` writes it as the single-byte
id in a RANGES record, the BLE scan list stores `discovered_devices[i][0]`, and the per-peer range
filters key on `details->uids[i][0]`. Two tags in a deployment that share that byte produce data
nothing can separate afterwards, so it is an error that blocks the write, not a warning. It is easy
to miss by eye because the UI shows addresses most-significant byte first and the byte that matters
is the one printed last — so each row also shows `ends XX`.

**The manifest** records what each tag CONFIRMED, not what was sent: every device is read back after
writing and decoded by the same parser that reads a log, so a device that silently stored something
different shows as a mismatch rather than being assumed away. It carries the exact 239 bytes as hex,
so it can be compared or replayed without this app.

**Web Bluetooth shapes what is possible**, in two ways worth knowing:

- There is no way to enumerate nearby devices. `requestDevice()` opens a browser-controlled chooser
  and returns one device. Ten tags therefore cost ten dialogs on first use — a page cannot scan and
  list them the way the Python tool does. `getDevices()` returns already-granted devices with no
  dialog, which is what makes later runs cheap, so pairing is worth doing once and reusing.
- The MAC address is never exposed; `device.id` is an opaque per-origin string. Identity comes from
  GATT System ID (0x2A23) instead, which `bluetooth_init()` populates. The Python tool split the BLE
  address, which is why that approach does not port.

The download reassembler counts bytes from the stream header rather than stopping at the 0xFF
completion marker, because a final data chunk could legitimately be one byte of 0xFF; stopping on
the marker alone would truncate a file roughly once in every 256 unlucky pages.

### USB-connected tags over Web Serial

`app/src/adapters/webSerial.ts` is a second `TagTransport`, so a tag plugged into the computer can be
added to a deployment, written, read back, buzzed and downloaded exactly like one reached over
Bluetooth. It shows as `USB-Connected XX:XX:XX:XX:XX:XX`, matching the desktop tool, because the two
are reached differently and must not look the same.

A serial port carries no identity: `getInfo()` gives only the vendor and product ids, which every
TotTag shares, and the USB serial-number string is a fixed model name. The EUI is asked for with
`USB_GET_UID_COMMAND`, so the handle is the EUI itself and reconnecting means opening each granted
port and asking which tag it is. Firmware older than that command cannot be identified, and so cannot
be put in a deployment over USB. The adapter refuses it rather than guessing.

Over USB the download reply is the UID, a newline and a u16 details length, followed by the same
stream Bluetooth delivers, with no completion marker. A retransmission request replaces the device's
list rather than adding to it, so each round asks for at most 255 pages in one write.
`app/test/webSerial.test.ts` runs the adapter against an emulated tag that splits every reply into
odd-sized chunks.

### Increment 5.5 — lessons from the sibling A3EM dashboard (complete)

Three ideas taken from `a3em-dashboard/Web`, which had solved the same problems first.

**Firmware era detection** (`src/firmwareEra.ts`) is the one that fixed a real defect. Messages
describing device BEHAVIOUR were asserting current firmware on every log, including logs written
before the behaviour existed — the charger-storm message described a de-bounce that firmware before
`626b34bc` did not have. A3EM's method, adopted wholesale: **detect from evidence in the data, never
from a version string**, because pinning behaviour to release numbers makes every new build look
unrecognised and fall back to a stale entry.

The addition is a third state. Absence of evidence is not evidence of absence: DIAGNOSTICS records
are written once per TimeAlignedTask pass, so a ten-minute log from current firmware contains none
and would otherwise look ancient. Evidence is `yes` / `no` / `unknown`, and only periodic records can
be concluded absent — a log with no reboot has no reset record, which says nothing about firmware.

**This immediately found something worth knowing: both committed fixtures predate the current
firmware.** They report stream format 1 — unframed — and contain no diagnostics records at all. So
the framed-record path and the entire near-miss section have never run against real data. Pinned in
`firmware-era.test.ts` so that committing a newer capture fails the test, which is the moment to
re-run the analysis and see what was only ever true of synthetic bytes.

**Parsing in a Web Worker** (`app/src/lib/`). The fixtures are 26 KB and parse instantly, which is
why this was easy to skip; a real deployment file is megabytes and would lock the page. One worker,
shared and kept alive, with an in-place fallback where workers are unavailable.

**`npm run open-items`** prints every unresolved assumption and what would settle it. The registry
already enforced itself in tests; what it lacked was a way to read it without opening a source file.

### Increment 5.6 — measuring the link instead of assuming it (complete)

Download progress now comes from `ThroughputMeter`, which measures the transfer in front of it. The
hardcoded 63 kB/s was one reading on one link, and an old laptop's Bluetooth stack and a new one's
differ by more than that figure's implied margin.

Three things keep it from jittering: a 1.5 s warm-up, because the first moments include MTU exchange
and a connection-parameter update and a rate computed from them is alarming and then halves; a
sliding window rather than a running total, because a total average cannot fall and would keep
promising the original rate as a link degrades; and an explicit "measuring…" state, because a number
produced before there is evidence for it is how a progress bar earns distrust.

The constant survives only as the pre-flight hint on the button, where there is nothing to measure
yet, and the guess is narrowed to that.

### Increment 5.7 — first contact with real hardware (complete)

Three defects, found by someone actually trying it.

**The download pane only appeared once a log was already open.** An earlier edit matched the compact
variant of the import panel and not the empty-state one, so the feature was invisible in exactly the
state a person starts in.

**"Could not reach that tag" for a tag just chosen from the picker.** `connect()` went through
`navigator.bluetooth.getDevices()`, which several Chrome versions gate behind
`chrome://flags/#enable-web-bluetooth-new-permissions-backend`. A `BluetoothDevice` from
`requestDevice()` stays usable for the life of the page, so it is now cached by id and reused; the
chooser also returns a live connection rather than one that is immediately dropped and re-established.
Where a reconnect genuinely cannot work — a tag added before a page reload, on a browser without
`getDevices()` — the message says that specifically, because it needs a different action from the
user than "move it closer".

**All tags advertised the same name**, so the browser's chooser showed a list of identical entries
and on macOS there is no MAC to fall back on. Fixed in firmware (below); the app's filter became a
`namePrefix` match, which matches both the new names and the old.

Also added: **Buzz**, per tag, on demand. Never automatic — these are worn by children in a study,
and a device that makes a noise nobody asked for is a device that gets switched off.

### Firmware fixes made from this work

Built clean for revM, revN, revO and revP, with no new warnings.

| File | Fix |
|---|---|
| `peripherals/src/bluetooth.c` | `WsfBufGetPoolStats()` takes a POOL INDEX; the SDK header documents it as a pool COUNT. Passing the count made every call take the `poolId >= wsfBufNumPools` early return, so the logged buffer figures were uninitialised stack — stable across a whole deployment because the stack layout at that call site repeats. Now called once per pool. |
| `peripherals/src/bluetooth.c` | The advertised name carries the short UID (`TotTag-3E`), so a host chooser can tell tags apart. Discovery now matches the name PREFIX — without that change tags would stop recognising each other, since the old check compared the whole name and its exact length. |
| `management/dashboard/tottag.py` | `pack_datetime` normalises daily times with `% 86400`. Without it the value left 0..86399 in both directions: negative east of UTC, where `struct.pack` raised outright, and above 86400 west of it, where nothing complained at all. |

The last one had already cost data. A 14:00–03:00 Central window packed an end of 97200 (27:00);
`rtc_get_time_of_day()` never reaches that, so the device recorded 14:00 to midnight instead —
**10 hours a day rather than the intended 13**, for the whole of `AE_1788908400.ttg`.

### Not yet built

Increments 4-6: deployment configuration, device transports (Web Bluetooth / Web Serial), and the
per-record proximity export that the daily-statistics workflow actually consumes.
