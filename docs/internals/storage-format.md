TotTag Log Storage Redesign
===========================

Scope: `software/firmware/src/external/nandlog/` (the log core and its chip drivers),
`software/firmware/src/tasks/storage_task.c` and `src/tasks/storage_records.h` (the application's record
grammar), the BLE/USB/RTT offload paths, and the host-side parsers in `software/management/dashboard/` and
`software/managementweb/packages/tottag-schema/`.

> ⚠️ **A note on paths.** This document was written while the log lived in
> `src/peripherals/{src,include}/storage.*`, and most of §1–§13 still names files and line numbers from that
> layout. §14 records the extraction into `src/external/nandlog/`, which is now complete: **there is no
> `storage.c` in the tree.** Read every `storage.c` reference below §14 as `nandlog.c`, every
> `storage_page_header_t` as `nandlog_page_header_t`, and so on. The historical names are left in place
> because rewriting them would falsify the record of what was decided when; the mapping is in §14.6.

This document specifies a replacement for the on-flash log format and the recovery, flush, and offload
logic around it. The goal is to preserve the maximum amount of usable data across random reboots and
power loss, and to make any data that *is* lost appear as a bounded, clearly-marked hole rather than as
silent corruption of the entire remainder of the file.


0. Implementation status
------------------------

| Phase | What it delivers | Status |
|---|---|---|
| **0** | Acute bug fixes + unconditional on-demand erase-ahead | ✅ **In tree**, smoke-tested on rev P hardware — block crossings and repeated power cycles verified, 0 errors, re-validated after Phase 2 (§12). |
| **1** | New page format: epoch, seq, CRCs, metadata ring, binary-search recovery | ✅ **In tree**, hardware-verified (§12.7) — page format, CRC-32, record framing, metadata ring, epochs, binary-search recovery |
| **2** | `STORAGE_FLUSH_TIMEOUT_S` (120 s) time-bounded flush | ✅ **In tree**, smoke-tested on rev P hardware — partial-page writes and the timed flush verified in situ, 0 errors (§12.6) |
| **3** | Zero-CPU VCOMP brownout detection | ⚙️ **Implemented but gated off** — no current board can enable it |
| **4** | Page-framed wire format, per-page retransmission, host dual-format parser | ✅ **COMPLETE, HARDWARE VALIDATED.** Wire format verified on both BLE and USB (§12.10); time seek verified (§12.9) and later rebuilt after §15.4 found it losing data; host dual-format parser regression-tested against 137 MB of real logs. Per-page retransmission landed with a 3-round cap and is verified device-side (§12.12) and host-side (§12.13) |
| **5** | Watchdog, bounded BUSY spin, reset-reason logging, time anchor, ms timestamps | ✅ **COMPLETE.** Time anchors, ms timestamps and reset-reason logging are validated and stable across three multi-day runs. ⚠️ **The watchdog described in §11 is superseded** — it was rebuilt twice (§15.2) and the shipping design is per-task check-ins at `RESVAL` 8. **Finding the hang is CLOSED** — 778 device-hours, zero watchdog resets (§15.16), and the residual pet declines turned out to be a race in the watchdog's own bookkeeping rather than a stall (§17) |
| **6** | BLE notifications in place of indications | ✅ **COMPLETE, HARDWARE VALIDATED** (§12.17). 9x throughput |
| **7** | Extraction into the reusable `nandlog` library | ✅ **COMPLETE** (§14). Chip drivers, port layer and log core all moved out of `peripherals/`; `storage.c` no longer exists |
| **audit** | Firmware-wide audit: watchdog rebuild, reset diagnostics, seek correctness, `-O2` | ✅ **IN TREE** (§15). Validated by a 22-hour run (§15.6) and a 3.9-day run (§15.8) |
| **follow-up** | Charger event de-bounce (§15.9.5), WSF pool failure counting (§15.10) | ✅ **IN TREE AND VALIDATED** over 61.5 device-hours (§15.13): zero charger chatter on any device, storage clean, and zero watchdog resets |
| **follow-up** | Discovery-window guard (§15.11) | ✅ **IN TREE AND VALIDATED** over 61.5 device-hours (§15.13). BLE_SCAN records fell from ~110,000 per device per night to 60-660, ranging held full 2 Hz for every co-located pair, and the rejoin loop was exercised 13,139 times on one device. An earlier reading of this run as a yield regression was wrong; see §15.14 |
| **follow-up** | `STORAGE_TYPE_DIAGNOSTICS`, record framing on, IMU teardown off the BLE task (§15.15) | ✅ **IN TREE AND VALIDATED** over 640 device-hours in the framed format (§15.16). Near-miss counters reach the log instead of a console, and it was their *shape* that closed the last open question about the watchdog (§17). The change also introduced an anchor-recovery bug that the same run exposed (§15.16) |
| **follow-up** | Watchdog check-in race, latched startup grace (§17); framing-aware on-device storage suite, both simulator framings pinned (§18) | ✅ **IN TREE.** `make storage` passes all seven tests on hardware; the simulator runs 80 checks framed and 38 unframed, and the two are now genuinely different builds |
| **follow-up** | Host-tool parity: log saving in the web dashboard, retransmission repair, advertised-name matching (§19) | ✅ **IN TREE AND VALIDATED.** Four logs downloaded through the web dashboard are byte-identical to the same four downloaded through `tottag.py` |

**What this means today.** All four failure modes identified in §1 are now addressed on the device:

- **Memory corruption and false bad-block retirement** are gone (Phase 0), and the erase invariant is
  unconditional rather than inductive.
- **Power-loss exposure is bounded at 120 seconds** in every regime (Phase 2), replacing an exposure that
  was unbounded in *time* — up to ~38 hours for an idle tag.
- **A lost or corrupt page no longer desynchronizes the rest of the file** (Phase 1). Records are never
  split across pages, so every payload begins on a record boundary and the host stays aligned across a
  dropped page; per-page CRCs mean a torn page is detected and returned as an explicitly empty chunk rather
  than emitted as garbage.
- **Page selection is exact** (Phase 1 recovery, Phase 4 §8.2). Neither the write head nor a time-range
  boundary is inferred from pattern-matching payload bytes any more.
- **A silent hang is no longer unbounded** (Phase 5, rebuilt in §15.2). It was the last item in the loss
  budget with no ceiling on it: every other failure costs at most one flush window, but a wedged tag on day
  one lost the remaining six days of a deployment. The shipping watchdog resets **~167 s** after the last
  successful pet, and every one of the five real tasks must check in for a pet to happen at all, so a stall
  in any one of them is caught and named. The one named hang path — an unbounded spin on the flash BUSY bit —
  escalates in ~500 ms with the cause logged. Every reset also records its own cause and the firmware's own
  verdict on what stopped it (§12.21, §15.3), so a reboot is no longer inferred from a gap.

  **The hang itself is not fixed.** Three multi-day runs put it at a steady ~0.02–0.025 watchdog resets per
  device-hour, unchanged by the audit; what the audit changed is the cost, from ~1080 s of lost data per
  event to ~159 s (§15.8). The watchdog is containing a defect, not curing one.

What Phase 4 adds is **visibility on the host**, not resynchronisation: the wire format lets the host see
*which* page was lost and verify each page's CRC independently, and per-page retransmission lets it ask for
the missing ones again.

The wire format is in tree on both the BLE and USB/RTT transports, so a dropped page is reported as an
explicit gap at a known sequence number rather than being silently absent, and retransmission closes the
loop: the host names the missing sequence numbers and the device re-sends exactly those (§8.1.1).

**This is the point of no return for the on-the-wire format.** A device running this firmware emits v2, and
only the updated host can read it. Both halves are in place, and the host still reads legacy `.ttg` files,
but there is no partial state — the device speaks one format or the other.

**Release strategy.** Phases land incrementally on `master` rather than on a side branch, each one
smoke-tested on hardware before the next begins (§12). This keeps each change independently reviewable and
gives every phase a verified baseline to fall back to.

**No device is flashed for real data collection until every phase is complete and the deployment
qualification in §12.12 passes.** `master` therefore carries work that builds and is smoke-tested but is
deliberately *not* deployment-qualified; the gate is held by release discipline, not by branch topology.


1. Why
------

Deployments have produced log files that are corrupted, missing, or truncated. The causes are structural,
not incidental. Each is traced to specific code below.

### 1.1 There is no framing, so one bad page destroys the rest of the file

A page is `'D','A', uint16 length, payload…` (`storage.c` `write_page`). Records are cut at exactly
`MEMORY_NUM_DATA_BYTES_PER_PAGE` regardless of record boundaries, so records straddle pages. On readback,
`storage_retrieve_next_data_chunk` strips the 4-byte header and concatenates raw payloads into an
unframed byte stream.

The host therefore cannot know where records begin. `dashboard/parse.py:38` and `dashboard/tottag.py:145`
are byte-at-a-time heuristic resynchronizers that slide forward looking for a plausible
`(type ∈ 1..6, timestamp % 500 == 0)` pattern. Consequences:

- A single lost or unreadable page desynchronizes every subsequent record. **→ "corrupted"**
- The sliding scan fabricates records from payload bytes that coincidentally look valid.
- There is no CRC anywhere, on either side, so neither the device nor the host can distinguish good
  data from garbage.

*(Resolved in Phase 1 — see §11. Records are no longer split across pages, so a lost page costs only its
own records; per-page CRCs detect torn or corrupt pages. The host-side sliding scan remains until Phase 4
replaces the wire format, but it no longer has to recover from mid-record truncation.)*

### 1.2 Boot recovery of the write pointer is a heuristic that stale data defeats

`storage_init` locates the write head by scanning *block head pages* for the `"DA"` magic and declaring
the end of the log after two consecutive blocks whose first page lacks it. The arithmetic is correct in
the nominal case (it relies on the loop's post-increment cancelling a `- MEMORY_PAGES_PER_BLOCK`), but
the premise is fragile:

- The loop never consults `is_bad_block()`. Two adjacent bad or unreadable blocks read as end-of-log.
  **→ silent truncation**
- Any stale `"DA"` page ahead of the head sends the scan past the true end into old data. The head then
  lands on already-programmed pages. Programming over programmed NAND does not reliably set the
  write-failure bit; `write_page` sees the ECC read-back fail, concludes the block is bad, and marks
  good blocks bad while writing garbage. **→ "corrupted", plus BBM table exhaustion**
- Wraparound is inferred purely from "page 0 contains DA", which stale data also fakes.
- Nothing durable records the write pointer. The metadata page is written once and never updated.

*(Resolved in Phase 1 Step B — see §11. Recovery now selects the highest valid epoch from the metadata ring
and binary-searches for the head over a predicate that tests the epoch, so stale data cannot fool it and
bad blocks cannot terminate it early. Verified across repeated power cycles in §12.3.)*

### 1.3 Starting a new experiment erases the whole array before committing anything

`storage_store_experiment_details` called `erase_block(0, BBM_LUT_BASE_ADDRESS - MEMORY_PAGES_PER_BLOCK)`
— 4016 blocks serially at ~2–10 ms each, i.e. **8–40 seconds of blocking erase**, executed inside the BLE
ATT write callback (`maintenance_functionality.c:42`) against a 1000 ms supervision timeout. The phone
disconnects mid-erase and the client is likely to retry, triggering another full erase.

It is also not atomic. Power loss in that window leaves a partially-erased array with no metadata page.
On the next boot `start_page == -1`, so the firmware writes a fresh metadata page at page 0 with
`current_page = 1`, orphaning the previous log. **→ "missing"**. It also creates exactly the stale-data
condition that defeats §1.2.

*(Mitigated in Phase 0 — see §11; validated in §12.)*

### 1.4 An ungraceful reboot loses a full page of buffered data

`storage_flush(false)` is called after every record but only writes once the cache holds a full page.
Partial pages are written only via `storage_flush(true)`, reached from exactly one place:
`STORAGE_TYPE_SHUTDOWN` in `storage_task.c`. A brownout, watchdog, hard fault, or the SPI-retry-exhaustion
path (`system_reset(true)` inside `spi_read`/`spi_write`) discards everything buffered.

The exposure is unbounded in *time*, not just in bytes. While actively ranging a page fills in ~62 s, but
an idle tag accumulates only the 300-second battery record, so a page takes **~38 hours** to fill.
**→ "truncated"**

*(Resolved in Phase 2 — see §11. `STORAGE_FLUSH_TIMEOUT_S` bounds the exposure at 120 s in every regime,
and a partial write now advances the head so a page is never programmed twice. Verified in §12.6.)*

### 1.5 Two unguarded overflows that amplify everything above

- The page length field was used verbatim as a `memmove` size and as a loop bound in three other places,
  with no check. A corrupted `uint16` yielded transfers of up to 65535 bytes into a 4096-byte buffer.
- `storage_store` had no bound against `sizeof(cache)`. Because `storage_flush` early-returns while
  `is_reading` or once the array is full, `cache_index` could run off the end of `cache[8192]` directly
  into `starting_page`, `current_page`, `cache_index`, and `is_reading`, which follow it in BSS.

*(Both fixed in Phase 0 — see §11.)*

### 1.6 Non-causes, for the record

**Wear is not a problem.** One whole-array erase per experiment against SLC NAND's ~100k cycle endurance
is nowhere near the limit. What the full-array erase actually cost was atomicity and time. The design
below reduces erase counts by roughly 50× for a typical two-week deployment, but that is a side effect of
correctness, not the objective.


2. Design principles
--------------------

1. **Every page is independently valid.** A page carries enough information to be parsed, ordered, and
   verified without reference to any other page. Losing a page costs exactly that page.
2. **Nothing is inferred from content.** Ordering, epoch membership, and time bounds are explicit fields,
   never pattern-matched out of payload bytes.
3. **Every durable state transition is a single page program.** NAND page programs either complete or
   leave a CRC-invalid page. Anything that needs to be atomic is expressed as one page write.
4. **Corruption is reported, never silently skipped.** A page that fails CRC becomes an explicit hole in
   the offload stream with a known position and size.
5. **Bounded loss.** Time-bounded flushing caps data loss at a configured wall-clock interval regardless
   of data rate.


3. On-flash format
------------------

Two page types, distinguished by magic. All multi-byte fields little-endian. All CRCs are CRC-32
(IEEE 802.3 polynomial `0xEDB88320`, reflected, init `0xFFFFFFFF`, final XOR `0xFFFFFFFF`) so the host can
use `zlib.crc32` unmodified.

### 3.1 Data page

| Offset | Size | Field | Notes |
|---|---|---|---|
| 0 | 4 | `magic` | `'TTP1'` = `0x31505454` |
| 4 | 4 | `epoch` | experiment generation; monotonically increasing, never reset |
| 8 | 4 | `seq` | page index within the epoch, starting at 0 |
| 12 | 4 | `first_timestamp` | experiment-relative ms of the earliest record in this page |
| 16 | 4 | `last_timestamp` | experiment-relative ms of the latest record in this page |
| 20 | 2 | `payload_length` | valid payload bytes |
| 22 | 2 | `record_count` | number of complete records in the payload |
| 24 | 4 | `payload_crc` | CRC-32 over `payload[0 .. payload_length)` |
| 28 | 4 | `header_crc` | CRC-32 over bytes `[0 .. 28)` |
| 32 | … | `payload` | record-aligned; see §3.2 |

Payload capacity is `MEMORY_PAGE_SIZE_BYTES - 32`: **4064 bytes** on rev N/O/P, 2016 on rev M. That is a
0.8% header overhead versus the current 4-byte header.

Rationale for the individual fields:

- **`header_crc` separate from `payload_crc`.** Recovery must trust `epoch` and `seq` without reading and
  checksumming a full 4 KB page. A probe reads only the 32-byte header, which after the mandatory
  `COMMAND_PAGE_DATA_READ` (tRD ≈ 60 µs) costs ~6 µs of SPI at 48 MHz instead of ~683 µs. This is what
  makes the binary search in §5 cheap.
- **`epoch`.** Makes stale data from a previous experiment unmistakable. This single field eliminates the
  entire class of failures in §1.2 and §1.3.
- **`seq`.** Gives total ordering independent of physical page address, so bad-block skips, wraparound,
  and out-of-order recovery all stop mattering.
- **`first_timestamp` / `last_timestamp`.** Replace the content-pattern timestamp search in
  `storage_begin_reading` and `storage_retrieve_num_data_chunks` with an exact binary search over headers.
  The current scan looks for a `STORAGE_TYPE_VOLTAGE` byte followed by two timestamps that are both
  multiples of 500 and within `BATTERY_CHECK_INTERVAL_S` of each other — a pattern ordinary payload bytes
  can and do satisfy.
- **`record_count`.** Lets the host assert it parsed exactly what the device wrote.

### 3.2 Record framing: records are never split across pages

This is the single most important change for file integrity, and it is nearly free.

The largest record is IMU at `1 + 4 + 1 + MAX_IMU_DATA_LENGTH` = 46 bytes worst case (12 bytes in the
default build), and ranges at `1 + 4 + 1 + 3 × MAX_NUM_RANGING_DEVICES` = 36 bytes. If a record does not
fit in the remaining payload, the page is flushed and the record starts the next page. Average waste is
about half the maximum record, ~20 bytes out of 4064 — **0.5%**.

In exchange, `payload` always begins on a record boundary. Every page is a self-contained, independently
parseable unit, and the host needs no resynchronization logic at all. Record encoding within the payload
is unchanged from today (`type:u8`, `timestamp:u32`, type-specific body), so the existing per-type decoders
port over directly.

### 3.3 Metadata page

Written once per experiment into a dedicated metadata ring (§4.1).

| Offset | Size | Field | Notes |
|---|---|---|---|
| 0 | 4 | `magic` | `'TTM1'` = `0x314D5454` |
| 4 | 4 | `epoch` | the epoch this metadata describes |
| 8 | 4 | `log_start_page` | physical page of `seq == 0` for this epoch |
| 12 | 4 | `created_timestamp` | RTC timestamp at creation |
| 16 | 2 | `details_length` | `sizeof(experiment_details_t)`, currently 239 |
| 18 | 2 | `format_version` | 1 |
| 20 | 4 | `details_crc` | CRC-32 over the details blob |
| 24 | 4 | `header_crc` | CRC-32 over bytes `[0 .. 24)` |
| 28 | 4 | reserved | `0xFFFFFFFF` |
| 32 | 239 | `experiment_details` | `experiment_details_t`, unchanged layout |

`experiment_details_t` is deliberately left byte-identical to today so `pack_experiment_details` /
`unpack_experiment_details` on the host need no changes and the BLE/USB configuration commands are
unaffected.


4. Media layout
---------------

```
page 0                          ┌──────────────────────────┐
                                │  metadata ring           │  METADATA_RING_BLOCKS = 8 blocks (512 pages)
                                │  one meta page per        │
                                │  experiment, sequential   │
8 * 64 = 512                    ├──────────────────────────┤
                                │                          │
                                │  log data region          │  4016 - 8 = 4008 blocks
                                │  circular, epoch-tagged   │
                                │                          │
BBM_LUT_BASE_ADDRESS            ├──────────────────────────┤
                                │  bad-block management     │  BBM_NUM_RESERVED_BLOCKS = 80
MEMORY_MAX_PAGE_ADDRESS         └──────────────────────────┘
```

### 4.1 Metadata ring

Separating metadata from the log is what makes new-experiment creation atomic. Today the single metadata
page lives inside the circular buffer and `storage_init` takes the *first* `"META"` page it finds scanning
from page 0 — so two metadata pages cannot safely coexist, which is precisely why the old code had to
erase the entire array before writing a new one.

With a dedicated ring, metadata pages accumulate and **the highest valid `epoch` wins**. Old ones are
harmless.

- 8 blocks = 512 pages = 512 experiments before the ring wraps.
- Boot scan reads 512 × 32-byte headers ≈ **36 ms**. Acceptable; a linear scan keeps the logic trivial.
- On wrap, erase the oldest block and continue. Wear on these blocks is one erase per 64 experiments.

### 4.2 Erase-ahead

**The start-of-experiment erase alone cannot be relied on.** This was analysed specifically, and the
conclusion is that "everything ahead of the head is erased" is an *inductive* property — it holds only if
it held before and nothing broke it — and at least four things break it **permanently**:

| # | Break | Why it is not self-healing |
|---|---|---|
| A | **Head under-recovery.** The v1 boot search terminates on two consecutive block-head pages lacking `"DA"` and never consults `is_bad_block()`, so two adjacent unreadable blocks under-report the head. | The next experiment's erase is scoped to the *under-reported* head, so `[under-reported + margin, true head]` is never cleaned. Every later experiment inherits the dirt. |
| B | **Power loss mid-erase.** `erase_block` clears the old metadata block first (lowest page in the range). A loss just after that leaves no metadata page. | Next boot takes the `start_page == -1` branch, writes a fresh metadata page at page 0, and begins writing forward from block 0 into the partially-erased remains. |
| C | **Bad-block relocation.** `write_page` picks `next_block` by scanning forward for a non-bad block and calls `transfer_block()` into it with no erase and no cleanliness check. | A second, independent consumer of the invariant — and it runs precisely when the media is already misbehaving. |
| D | **Non-factory-erased or swapped flash part.** `is_first_boot()` gates on an OTP marker page and never erases the array. | A part whose marker is already set skips all initialization regardless of what the array contains. |

Each of these leaves a stray programmed block ahead of the head. Programming over it does not reliably set
the write-failure bit; the ECC read-back fails instead, `write_page` concludes the block is bad, and a
perfectly good block is retired while garbage is written. A margin of any fixed size does not help, because
A/B/C/D are unbounded in how far ahead they can leave dirt.

**Therefore: erase on demand, unconditionally.** Whenever `current_page` crosses into a new block, erase
the block `ERASE_AHEAD_BLOCKS` (default 2) ahead of the head, skipping known-bad blocks. This replaces an
inductive, history-dependent property with a local one that no prior state can invalidate: *a block is
always erased before its first page is programmed.*

The window is two blocks deep rather than one because the boot-time head search needs two consecutive
erased blocks to terminate correctly. The window is also rebuilt unconditionally at the end of
`storage_init`, since the head search is heuristic and can land anywhere.

Cost: one ~2 ms erase per 64 page writes — about 30 µs amortized per page write, invisible against the
~700 µs a page program already costs. At the `T = 120 s` ceiling of 720 pages/day that is roughly 11
erases per day.

Old-epoch data is *not* proactively erased. It stays readable until the head sweeps over it and
erase-ahead reclaims it. Because every page carries its `epoch`, stale data can never be mistaken for
current data.

### 4.3 Creating a new experiment

Ordered so that the commit is a single page program:

1. Choose `new_epoch = current_epoch + 1`.
2. Choose `log_start_page` = the block boundary following the current head (continues the wear sweep).
3. Erase `1 + ERASE_AHEAD_BLOCKS` blocks starting at `log_start_page`. ≈ **6 ms**.
4. **Write the metadata page.** This is the commit point.

Until step 4 lands, the device still boots into the old epoch with its log fully intact. After step 4, the
new epoch is live. A power loss at any point leaves exactly one of those two consistent states.

Total time: ~6 ms, versus 8–40 s today. The BLE supervision timeout problem disappears.


5. Boot recovery
----------------

```
1. Scan the metadata ring (512 header reads). Select the entry with the highest `epoch`
   whose `header_crc` and `details_crc` validate.
       → none found: fresh device. Initialize epoch 1 per §4.3.
2. Binary search the log region for the head.
       Predicate P(page) := page holds a valid header with epoch == current_epoch.
       P is monotone: true for every page below the head, false at and above it
       (unwritten pages read as 0xFF, failing the magic check).
       Skip known-bad blocks via is_bad_block() so they cannot break monotonicity.
3. head = highest page satisfying P. Read its header for `seq`.
   current_page = head + 1, next_seq = seq + 1.
4. Ensure erase-ahead invariant: erase forward from current_page as needed.
```

Cost: `log2(4008 × 64) ≈ 18` probes × ~100 µs = **~1.8 ms**, versus up to 4016 full-page reads
(≈ 3 seconds) today.

The head's own page is trusted only if `payload_crc` also validates. If it does not — the signature of a
power loss mid-program — the page is abandoned, `current_page` advances past it, and the offload stream
reports it as a hole. One page lost, nothing else.


6. Flush policy
---------------

A flush is triggered by any of:

| Trigger | Bound |
|---|---|
| Next record does not fit in the remaining payload | page-granular |
| Cache dirty for longer than `STORAGE_FLUSH_TIMEOUT_S` | wall-clock |
| Critical-voltage / brownout warning (§7) | best effort before power loss |
| Graceful shutdown (`STORAGE_TYPE_SHUTDOWN`) | — |

Every flush advances `current_page`. **A page is programmed exactly once.** The current code's partial
flush does not advance the head, which is safe today only because it is immediately followed by
`system_reset`; the invariant is now explicit rather than accidental.

### 6.1 `STORAGE_FLUSH_TIMEOUT_S`

A tunable `#define` in `app_config.h`, **default 120**.

Implementation requires no new task or software timer. `StorageTask` currently blocks on
`xQueueReceive(..., portMAX_DELAY)`; change the timeout to the remaining flush deadline and flush on
timeout expiry. Roughly a five-line change.

### 6.2 Cost analysis

Measured record sizes from the default build (`imu_enable_data_outputs(IMU_ACCELEROMETER | IMU_MOTION_DETECT,
500000)`, so IMU = 12 B at 2 Hz; ranges = 12 + 6N bytes at 2 Hz; battery = 9 B per 300 s):

| Peers | Data rate | Page fills naturally in |
|---|---|---|
| 2 | 48 B/s | 85 s |
| 5 | 66 B/s | 62 s |
| 10 | 96 B/s | 42 s |

Two properties govern the cost, and the second is counterintuitive:

1. **A flush only happens when there is dirty data.** You can never write more pages per day than you have
   records per day. When data is sparse, the record rate — not the timeout — is the binding constraint.
2. **A timeout longer than the natural page-fill time never fires at all**, and therefore costs exactly
   nothing.

Together these bound the **timer-driven** page rate at `86400 / T` pages per day. That is a ceiling on pages
written *because the timeout fired*, not a ceiling on pages written at all: once the data rate is high enough
to fill a page in less than `T`, page-fill takes over and the rate rises above it. The two regimes are
separated by the natural fill time in the table below, and the deployment measured in §15.8 straddles them —
it averaged **837 pages/day**, above the 720 figure, because roughly half of each day was spent ranging at
2 Hz where pages fill in ~185 s and the other half idle where only the timeout fires.

The array holds
`(4096 - 80) × 64 = ` **257,024 usable pages** (`BBM_LUT_BASE_ADDRESS`), of which 256,512 remain for log
data after the 8-block metadata ring:

| `T` | Pages/day ceiling | MB per 30 days | % of array/month | Array lasts |
|---|---|---|---|---|
| 60 s | 1440 | 177 MB | 16.8% | 178 days |
| 90 s | 960 | 118 MB | 11.2% | 267 days |
| **120 s (default)** | **720** | **88 MB** | **8.4%** | **356 days** |

The ceiling is what a device pays when it is dirty in every window but never fills a page — the
timer-dominated regime. A tag ranging to one peer once per minute sits squarely in it: at `T = 120` that
is at most 720 pages/day, so the array lasts **356 days**, and the realistic figure (the cache is clean
between the flush and the next record, so the effective period is `T + 60` s) is 480 pages/day and **535
days**. Both are an order of magnitude past the 30-day requirement.

Per scenario, MB written per 30 days of continuous operation (1004 MiB usable array):

| Scenario | Rate | Page fills in | Natural | `T=60` | `T=90` | `T=120` |
|---|---|---|---|---|---|---|
| Idle (battery record only) | 0.03 B/s | 38 h | 0.1 MB | 35 | 35 | 35 |
| Lone tag (IMU only, 0 peers) | 24 B/s | 169 s | 63 MB | 177 | 118 | 88 |
| 1 peer | 42 B/s | 97 s | 110 MB | 177 | 118 | 110 |
| 2 peers | 48 B/s | 85 s | 125 MB | 177 | 125 | 125 |
| 5 peers | 66 B/s | 62 s | 172 MB | 177 | 172 | 172 |
| 10 peers | 96 B/s | 42 s | 251 MB | 251 | 251 | 251 |

"Continuous operation" is a deliberate worst case: devices power off outside the configured experiment
window and honour `daily_start_time`/`daily_end_time`, so real deployments sit well below these figures.

**Why 120 s is the default.** Natural page-fill time is 85 s at two peers and shorter with more, so a 120 s
timeout **never fires at all in any multi-peer deployment** — the common case pays literally nothing. Even
in the timer-dominated sparse regime it leaves nearly a year of headroom, versus 178 days at 60 s. Against
today's behaviour it replaces an exposure window that is unbounded in time (38 hours for an idle tag) with
a flat two-minute bound.

Sixty seconds costs +41% at two peers and +182% for a lone tag with IMU running, buying ninety seconds of
additional durability. The value is a `#define`; all three are defensible if the tradeoff shifts.


7. Power-loss protection
------------------------

The flush timeout covers hard faults and watchdog resets, where there is no warning at all. Battery
depletion *can* be anticipated. This section covers how, under the constraint that **the power budget does
not permit leaving the ADC running continuously**.

### 7.1 No hardware comparator is usable on current boards

An earlier draft proposed the ADC window comparator with the internal repeating trigger timer. That
requires leaving the ADC in repeat mode rather than power-cycling it per reading as `battery.c` does today,
and is therefore **rejected on power grounds**.

The two remaining hardware options were investigated and neither works on current hardware:

**BOD comparators — cannot reach the battery.** Every BOD monitors an internal rail (VDD, VDDC, VDDF, VDDS,
VDDC_LV) at a fixed threshold, and `MCUCTRL->BODCTRL` offers only power-down control, not threshold
selection. BODH is 2.1 V, and `RSTGEN->CFG.BODHREN` carries an explicit warning that enabling it on a
1.8/1.9 V part "will cause a continual reset loop" — which is why `am_hal_reset.c:69-74` unconditionally
rejects `AM_HAL_RESET_BROWNOUT_HIGH_ENABLE`. There is no way to point a BOD at the battery or to choose a
Li-ion-relevant trip point. Dead end.

*(Should this path ever be revisited: `am_hal_reset.c:249-259` `am_hal_reset_interrupt_clear()` writes
`RSTGEN->INTEN` instead of `RSTGEN->INTCLR`, so it never clears the status and clobbers other enable bits.
Clear `RSTGEN->INTCLR` directly.)*

**VCOMP — right peripheral, wrong pin.** VCOMP is exactly the intended mechanism for this: a standalone
comparator with a programmable internal DAC reference (`LVLSEL`, nine steps from 0.58 V to 2.13 V), its own
`VCOMP_IRQn = 3`, no `PWRCTRL_DEVPWREN` bit (so it is not gated alongside the ADC and IOMs), sited in the
always-on base-platform block at `0x4000C000`. Datasheet §26.1 states it "measures a user-selectable
voltage at all times."

It cannot reach the battery divider on current boards. VCOMP's external inputs are **CMPIN0 (GPIO 10)** and
**CMPIN1 (GPIO 11)** only. `PIN_BATTERY_VOLTAGE` is GPIO 18, whose pinmux has no comparator function
(`ADCSE1`, `ANATEST2`, `I2S1_WS`, `GPIO`, UART flow control, `CT18`, `NCE18`, `OBSBUS2`, …). On revP,
GPIO 10 is already `PIN_RADIO_SPI_MISO`.

> **Hardware change required on a future board revision:** GPIO 11 is unused on revM, revN, revO, and revP.
> Tap the existing 510 kΩ / 187 kΩ battery divider to GPIO 11 in addition to GPIO 18. That enables
> `PSEL = VEXT2`, `NSEL = DAC`. At the divider ratio of 0.268, `LVLSEL = 0.97 V` corresponds to ≈ 3.62 V at
> the battery — a usable trip point between `BATTERY_EMPTY` (3500 mV) and `BATTERY_CRITICAL` (3680 mV).

**The driver is implemented and gated off** (`battery.c`). It compiles in automatically as soon as a board
revision defines `PIN_BATTERY_VOLTAGE_COMPARATOR`; no current revision does, so it is absent from every
shipping build. See §11 Phase 3.

One caveat remains even after the board change: **no VCOMP or ADC current spec exists in any datasheet in
this tree.** Table 42 has a single row (input voltage range) and Table 43 has no current row, and the tree
contains only the base Apollo4 datasheet, not Apollo4 Plus. The standby cost must be measured against the
13.6 µA deep-sleep baseline (`ISDS2-8RET`) before relying on it in a power-constrained deployment. VCOMP is
architecturally the right choice — it has no `PWRCTRL_DEVPWREN` bit, sits in the always-on base-platform
block, and the datasheet says it measures "at all times" — but that is an argument, not a measurement.

### 7.2 Battery detection logic is unchanged

**Decision: leave the existing battery monitoring exactly as it is.** `time_aligned_task.c:32-55` continues
to poll every `BATTERY_CHECK_INTERVAL_S` (300 s) and call `storage_flush_and_shutdown()` below
`BATTERY_CRITICAL`. No adaptive polling, no new `battery_event_t` member, no changes to `battery.c`.

Power-loss protection therefore rests entirely on `STORAGE_FLUSH_TIMEOUT_S` (§6.1), which bounds loss to
120 seconds in every regime — including instantaneous disconnection, which no detection scheme short of a
supercap would catch anyway. Against today's unbounded exposure (38 hours for an idle tag) that is the bulk
of the available improvement, obtained with no power cost and no new failure surface.

The VCOMP board note in §7.1 remains open as a future hardware option, not as work in this plan.


### 7.3 Record timestamps and the ranging time base

Records carry two different clocks. Ranging records are stamped with the *network* time the schedule
carries, so measurements line up across devices. Everything else is stamped with this device's own
experiment clock, converted to network time by an offset. The offset is derived state, and it went stale
in three ways:

1. **Reboot.** It was reset to zero at `StorageTask` start and only restored by the next ranging round
   that produced results. Everything logged in between landed on the local clock.
2. **Role change.** A master published its *own* clock as the network base, so the base moved with
   mastership, and the master then legitimately reset its offset to zero to match.
3. **Ranging dropout.** No results, no refresh.

The consequence is worse than wrong timestamps: a log holding two bases has pages whose `last_timestamp`
precedes their `first_timestamp`, and the time-range seek binary-searches exactly those bounds. On a real
log where the two devices' experiment start times differed by five hours, four of seven seek probes
returned the wrong page, one of them skipping 486 of 639 ranging records. This bites even when everything
is configured correctly — two UTC-synced devices still drift, and a backwards step of any size inverts a
page.

Four changes, deliberately layered so no single one has to be sufficient:

- **A backwards-stepping record starts a new page** (`storage_store_record`). No page can span a
  discontinuity, so `first <= last` always holds. During an active download the record is instead dropped
  with `cache_overflowed` set — new data loss in a narrow window, but preferable to corrupting the bounds,
  and consistent with how a page-full-during-read already behaves.
- **The seek verifies its own answer** (`seek_page_for_timestamp`). After the binary search it walks back
  while earlier pages still satisfy the bound, capped at 64 pages, then falls back to the whole range. The
  direction is deliberate: an earlier page costs transfer, a later one silently drops requested data.
- **The offset is recovered from a time anchor at boot** (`storage_recover_time_anchor`, §12.24). The
  anchor pairs an experiment timestamp with the raw RTC value at the moment it was written, so the offset
  is *derived* rather than inferred, and the derivation does not care how old the anchor is.

  ⚠️ *This replaced an earlier scheme, and the reasoning that justified it is worth keeping as a warning.*
  The original seeded from the newest **ranging** record via `storage_recover_last_ranging_timestamp()`,
  arguing: *"seeding is exact enough because the RTC does not survive a power cycle — any reboot that leaves
  a readable log is a warm reset, so the only error is the duration of the final write and the reboot."*
  **That last clause is false.** Up to `STORAGE_FLUSH_TIMEOUT_S` (120 s) of records sit unflushed in the RAM
  page cache at any instant and are lost on reboot, so the newest *readable* record is stale by that much —
  plus any hang, which a watchdog reset makes ~19 minutes. The error was neither bounded by the write
  duration nor self-correcting, and it compounded across reboots. It is the direct cause of the 73 s, 79.5 s
  and 1036 s network re-basings in §12.23.

  *Persisting the offset in the metadata ring* was considered and rejected on the original design, and the
  rejection still holds: the ring is crash-safe and wear is not a concern, but
  `storage_store_experiment_details()` also starts a new epoch, so it would need a new write path through
  the most safety-critical code, and writing often enough to stay current costs ~1440 flash programs a day.
  The anchor gets the same result as an ordinary log record.
- **The offset has one owner** (`app_get_time_offset` / `app_set_time_offset` in `app_tasks.c`). A master
  now publishes `app_get_experiment_time(app_get_time_offset())` rather than its own raw clock, so the base
  survives a change of mastership. Zero still means "my clock is the base", which is correct for the device
  that founds the network and for one that has not yet heard a schedule.

Host side, `parse_v2` reports `time_discontinuities` where page bounds run backwards. A *full* download is
unaffected, but a date-limited one may be missing data it asked for — and nothing else in the stream would
reveal that, since what the device sends is internally consistent and reports no gaps. Retransmission
cannot help: it repairs only what the device admitted it would send.

**Known limits.** Two devices that boot without hearing each other each define their own base; when they
merge, whichever wins mastership imposes its own, and the loser's earlier records stay behind. This makes
the base sticky, not globally unique. The role-change path is also untested — in every log captured so far
the instrumented device was always a participant.

8. Offload
----------

### 8.1 Wire format

Today the stream is `total_data_length:u32` + `experiment_details_t` + an unframed record soup. The new
stream carries page structure through to the host so corruption is localizable.

```
Header:
  u32   magic              'TTS1'
  u16   format_version     1
  u16   details_length     239
  u32   total_pages
  u32   total_payload_bytes
  u8[]  experiment_details

Then, per page:
  u32   seq
  u32   first_timestamp
  u32   last_timestamp
  u16   payload_length     0 indicates an unreadable page (a hole)
  u16   record_count
  u32   payload_crc
  u8[]  payload            payload_length bytes
```

A page that fails CRC on the device is still emitted, with `payload_length = 0`, so the host sees an
explicit gap at a known `seq` instead of silently missing data. The host re-verifies `payload_crc`
independently, which also catches corruption introduced in transit.

### 8.1.1 Per-page retransmission

**In scope.** Because every page carries `seq` and a CRC, the host can identify exactly which pages
arrived corrupt or not at all and ask for those specific pages again.

Add one command to both transports (`BLE_MAINTENANCE_RETRANSMIT_PAGES` alongside the existing
`BLE_MAINTENANCE_DOWNLOAD_LOG*` codes in `maintenance_functionality.c`, and a matching
`USB_RETRANSMIT_PAGES_COMMAND` in `usb_task.c`), carrying a count followed by that many `u32` sequence
numbers. The device responds with the same per-page framing as the main stream.

The device must therefore be able to seek to an arbitrary `seq`, which is the same binary search over page
headers already required by §8.2 — no additional mechanism.

Host side: `parse_v2` accumulates the set of missing or CRC-failed `seq` values, and the download driver
issues retransmission rounds until the set is empty or the retry cap is hit. **The retry cap is 3 rounds.**
Whatever remains after that is reported to the user as an explicit list of lost pages with their time
ranges, which is strictly more information than today's silent truncation.

**Settled design decisions.**

- *Pages are requested by sequence number, not by stream position.* Duplicate sequence numbers existed only
  in the intermediate firmware that was never deployed, so within a v2 log a sequence number is unique and
  is the natural identity of a page. Position is a property of a particular transfer; sequence is a
  property of the page.
- *The response reuses the same framing as a normal transfer* — a `storage_stream_header_t` followed by
  `storage_wire_page_t`-framed pages — with `details_length = 0`, since the host already holds the
  experiment details from the initial transfer. The host therefore parses a retransmission response with
  exactly the same code, passing the experiment start time explicitly.
- *A page that still cannot be read is answered with a zero-length frame*, the same as in the initial
  transfer. A page that fails twice is exactly as absent as it was the first time, and the host must be
  able to distinguish "still missing" from "never answered".
- *The device already has the lookup primitive.* `storage_retrieve_page_by_seq()` binary-searches the epoch
  by sequence number in roughly 18 probes, is verified against out-of-order requests, returns nothing
  rather than the nearest page for a sequence that was never written, and does not disturb a sequential
  read in progress (§12.12).

**As implemented.** `BLE_MAINTENANCE_RETRANSMIT_PAGES = 0x06`, with the same value reused as
`USB_RETRANSMIT_PAGES_COMMAND`, carries a count followed by that many `uint32_t` sequence numbers. The
request list lives in the storage layer (`storage_retransmit_*`) so both transports share one
implementation, and it is accumulated across commands, since one ATT payload holds at most 60 sequence
numbers. A pending list turns the next `DOWNLOAD_LOG` into a repair round on either transport.

Two details are worth recording, because both were decided against a simpler-looking alternative:

- *Only holes and CRC failures are re-requested.* A page whose CRC passed arrived intact, so a page that
  merely stopped decoding early has a record-level problem that a second copy of the same bytes cannot
  fix. Re-requesting it would burn a round to no effect.
- *Pages lost to a truncated transfer are inferred, not observed.* They have no frame in the stream at
  all, so there is nothing to report them by. Sequence numbers are contiguous within an epoch, so the
  missing tail runs on from the last page that did arrive — and a repaired copy of such a page has no
  frame to substitute into either, so it is decoded and counted separately (§12.13).

The device computes `total_payload_bytes` for a repair round by reading each requested page twice. The
host sizes its receive buffer from that figure, so an upper bound would make a short transfer
indistinguishable from a complete one — exactly the failure this whole design exists to eliminate.

### 8.2 Time-range selection

`storage_begin_reading` and `storage_retrieve_num_data_chunks` currently locate the start and end pages by
scanning payload bytes for a plausible voltage record. Replace both with a binary search over page headers
comparing against `first_timestamp` / `last_timestamp`. This is exact, cannot false-positive, and costs
~18 header probes instead of a linear scan of the whole log.

`storage_retrieve_num_data_bytes` becomes an exact sum rather than an estimate, since `payload_length` is
CRC-protected.

### 8.3 `.ttg` on disk

Today the `.ttg` is the raw record stream with the header stripped, so the only surviving metadata is the
filename `<label>_<start_time>.ttg` — which is why `parse.py` needs `experiment_start_time` as `argv[2]`.

The new `.ttg` is **the wire stream verbatim, header included**. Files become self-describing and
`experiment_start_time` becomes optional on the `parse.py` CLI (still required for legacy files).


9. Host-side changes
--------------------

Requirement: existing functionality must keep working on existing `.ttg` files.

### 9.1 Format detection

Legacy streams always begin with a record type byte in `1..6`. The new stream begins with `'T'` (`0x54`).
Detection is therefore unambiguous:

```python
def detect_format(data):
    return 2 if data[:4] == b'TTS1' else 1
```

### 9.2 Where format knowledge currently lives

There are **three** copy-pasted parsers, not one. Changing only `tottag.py` would leave the others
silently decoding the new format with the old grammar.

| File | Function | Action |
|---|---|---|
| `dashboard/tottag.py:145` | `process_tottag_data(from_uid, storage_directory, details, data, save_raw_file)` | ✅ dispatches through `tottag_format.parse()` |
| `dashboard/parse.py:8` | `process_tottag_data(data, experiment_start_time=None)` | ✅ dispatches; start time now optional, since v2 files are self-describing |
| `dashboard/experimental_tottag.py:127` | `process_tottag_data(...)` | **frozen as v1-only** (decided). Already divergent and stale — no `STORAGE_TYPE_BLE_SCAN`, hard-coded `data[i] > 5`, and a variable-length IMU convention via `load_imu_data.IMU_DATA_LEN` that conflicts with `IMU_DATA_LENGTH = 7`. Add a header comment saying so; it will not read v2 files. |
| `dashboard/segger_download.py:35` | — | ✅ reads the 4-byte discriminator first, then the remainder for whichever format it is, instead of a hard-coded 243 bytes. A v2 `.ttg` now retains its header so the file stays self-describing |
| `dashboard/tottag.py:294` | `data_callback` (BLE) | assumes the header fits in the first notification |
| `dashboard/tottag.py:311` | `data_callback_serial` (USB) | assumes a newline-terminated UID |

### 9.3 Plan

Add `dashboard/tottag_format.py` containing `detect_format`, `parse_v1` (the current grammar, moved
verbatim), and `parse_v2`. Both return the same `{timestamp: {t,v,c,m,r,b,i}}` dict, so every downstream
consumer — `processing.py:load_data`, `trim_pkl.py`, `load_imu_data.py`, the batch scripts — is unaffected.

Then make `tottag.py`, `parse.py`, and `segger_download.py` dispatch through it. Public signatures listed
in §9.2 stay stable. Note that `segger_download.py` and `quick_download_trigger.py` both `from tottag
import *`, so anything added at module scope in `tottag.py` leaks into their namespaces.

`parse_v2` gains something v1 could never have: it reports the `seq` and byte range of every hole.

Two details to preserve carefully in `parse_v1`:
- The IMU length byte *includes itself*, so the advance is `i += 5 + imu_length`, inconsistent with the
  `+6` used by other types.
- The bare `except Exception: pass` wrapping the parse loop silently truncates on a malformed tail. `parse_v2`
  should not inherit this; it should surface errors per page.


10. Migration
-------------

A clean break. New firmware writes only v2 and does not read v1 logs.

- On first boot after flashing, no `'TTM1'` page exists in the metadata ring, so the device initializes a
  fresh epoch 1 (§4.3) after erasing the ring and the initial log blocks — roughly 24 blocks, ~50 ms.
- Any v1 log still on the device at flash time is lost. This is accepted; download before updating.
- v1 `.ttg` files already on disk keep parsing through the host's v1 path indefinitely.


11. Implementation phases
-------------------------

**Phase 0 — acute fixes. ✅ COMPLETE, HARDWARE VALIDATED.** Independent of the redesign; reduces
corruption on currently-deployed firmware. Builds clean on revP and revM. All changes are in
`storage.c` / `storage.h` unless noted.
- `validated_page_length()` gates the page length field everywhere it is used as a size or loop bound:
  both `memmove`s in `storage_retrieve_next_data_chunk` (via `extract_page_payload()`), the loop bound in
  `storage_begin_reading`, and the loop bound plus both `log_data_size` accumulations in
  `storage_retrieve_num_data_chunks`
- `storage_store` bounds-checked against `sizeof(cache)` with a `cache_overflowed` latch so partial
  records are not emitted; the latch clears on the next successful full-page flush
- `storage_deinit` clears `is_initialized`
- new-experiment erase scoped to the range actually used plus a 16-block margin (`MEMORY_NUM_ERASE_MARGIN_BLOCKS`),
  ~160 ms instead of 8–40 s
- `erase_block` relocated above `write_page` (pure code motion; verified by identical binary size)
- **on-demand erase-ahead brought forward from Phase 1**, because the scoped erase alone cannot guarantee
  the array stays clean ahead of the head (§4.2). Three call sites:
  - `storage_flush` erases `ERASE_AHEAD_BLOCKS` ahead whenever the head enters a new block
  - `storage_init` rebuilds the window after the heuristic head search, and erases block 0 before writing
    a fresh metadata page in the `start_page == -1` branch — the exact landing spot for break B
  - `write_page` erases the bad-block relocation target before `transfer_block()` — break C

  With these in place the erase invariant is local and unconditional, so it no longer depends on the
  scoped erase, on head-recovery accuracy, or on the state the flash part was in when it arrived.

*Validation status: smoke-tested on hardware — see §12. Known untested paths are listed in §12.11.*

**Phase 1 — storage layer. ✅ COMPLETE, HARDWARE VALIDATED** (§12.7, §12.8).

*Step A (done, verified on rev P hardware):* 32-byte self-validating page header (`storage_page_header_t`)
with magic, epoch, seq, first/last timestamp, payload length, record count, and separate `header_crc` /
`payload_crc`. Software CRC-32 matching `zlib.crc32` (see below). Record framing via
`storage_store_record()`, which replaces the byte-append `storage_store()` and guarantees records are never
split across pages. `write_page()` now verifies the read-back header CRC rather than trusting the ECC flag
alone. Block-crossing and partial-page tests pass with 0 errors on the new format.

*Why software CRC-32:* the Apollo4 SECURITY engine requires a length that is a multiple of 4, and payloads
are record-aligned and therefore arbitrary. The only clean workaround — checksumming the whole fixed-size
payload region — would process ~9x more data than necessary in the timer-dominated regime this firmware
actually runs in (measured ~456 valid bytes per page, §12.6). The HAL's completion wait also busy-polls at
1 us granularity, so hardware saves cycles but not MCU-active time, and its result has no visible final XOR
so `zlib` compatibility is unverified. Measured software cost: **21 us on a real page against a ~2.9 ms page
program — 0.27 nA averaged over a day, 0.002% of deep-sleep current.**

*Step B (✅ hardware-verified — the re-run recorded in §12.3 exercises exactly this path):*

- **Media layout split.** The metadata ring occupies pages `[0, METADATA_RING_PAGES)`; the circular log now
  runs `[LOG_REGION_FIRST_PAGE, LOG_REGION_END_PAGE)`. Metadata living outside the log is what makes
  creating an experiment atomic — entries accumulate and the highest valid epoch wins, so the commit is one
  page program rather than a whole-array erase.
- **Page arithmetic centralised.** `log_wrap_page()`, `log_next_page()`, `log_next_block()` and
  `log_page_distance()` replace ~28 open-coded `% BBM_LUT_BASE_ADDRESS` expressions, so the region can be
  moved or resized in one place and no traversal can accidentally wrap into the metadata ring.
- **Real epochs.** `log_epoch` increments per experiment and is stamped into every page header, so stale
  data from a previous run can no longer be mistaken for current data (§4.2 break A is now closed on the
  read side as well as the write side).
- **Binary-search head recovery.** `recover_write_head()` replaces the two-consecutive-empty-blocks linear
  scan: ~18 probes over a monotone predicate instead of thousands of full-page reads, and stale data cannot
  fool it because the predicate tests the epoch.
- **`storage_store_experiment_details()` now returns `bool`** and logs when it refuses, instead of silently
  discarding the write (the sharp edge found in §12.7).
- **`storage_task.c` pass-through wrappers removed** — the dispatch switch calls `storage_store_record()`
  directly now that each helper was a single line.

*Time-range selection is now exact (§8.2, delivered early with Phase 4).* `storage_begin_reading()` and
`storage_retrieve_num_data_chunks()` binary-search the per-page header time bounds instead of scanning
payload bytes for something resembling a voltage record. The old scan could not distinguish a real record
from ordinary payload that happened to match the pattern, so it could select the wrong page and silently
drop or duplicate a span of the log — one of the mechanisms behind the original "missing data" reports.
Covered by a new time-range seek test, since every other test calls `storage_begin_reading(0, 0)` and skips
the seek entirely.

**Phase 2 — flush policy. ✅ COMPLETE, HARDWARE VALIDATED** (§12.6). Bounds
power-loss exposure at `STORAGE_FLUSH_TIMEOUT_S` (§6.1) in every regime.

- `STORAGE_FLUSH_TIMEOUT_S` added to `app_config.h`, **default 120 s**, with the capacity rationale inline.
- `StorageTask` now blocks on `xQueueReceive()` for the remaining lifetime of the oldest buffered byte
  rather than `portMAX_DELAY`, and writes a partial page when that expires. The deadline is armed when the
  cache first becomes dirty and **is never extended by later writes** — extending it would let a steady
  trickle postpone the flush indefinitely, which is exactly the idle-tag case the phase exists to fix.
- While the cache is clean the task still blocks forever, so a device with nothing to write adds **no
  wakeups** under tickless idle. The timer only runs when there is data at risk.
- New `storage_has_buffered_data()` exposes cache state so the deadline tracks the actual data at risk
  rather than a proxy. It also lets the timeout path re-arm instead of clearing when `storage_flush()`
  declines to write (download in progress, or array full).
- **Single-program-per-page is now guaranteed.** The partial-page path previously wrote without advancing
  the head or clearing the cache — safe only because it was immediately followed by `system_reset()`. Head
  advance and erase-ahead bookkeeping are factored into `advance_write_head()` and applied identically to
  full and partial writes, so a NAND page can never be programmed twice between erases.

*Incidental fix:* the `_TEST_NO_STORAGE` stub block had `void storage_init(void)` against `bool` in the
header, and `storage_begin_reading()` with one parameter against two. These mismatches predate this work
and meant the `ble_and_range`, `ble_reset`, and `ble_range_imu` test targets did not compile at all. Fixed
while adding the `storage_has_buffered_data()` stub.

**Phase 3 — zero-CPU brownout detection. ⚙️ IMPLEMENTED, GATED OFF.** A bare-register VCOMP driver in
`battery.c`, since the SDK has no `am_hal_vcomp`. Existing battery polling is untouched (§7.2); this is
purely additive.

- Gated on the board defining `PIN_BATTERY_VOLTAGE_COMPARATOR`. No current revision does, so it is absent
  from every shipping build. A future board enables it with one `#define` — no code change.
- `brownout_detection_init()` powers up VCOMP (`PWDKEY = 0x37`), selects the divider tap against the
  internal DAC, and enables the `OUTLOW` interrupt at `VCOMP_IRQn`.
- `am_vcomp_isr()` clears and **disables** the interrupt before dispatching. `OUTLOW` is level-sensitive,
  so leaving it armed past the first trip would produce an interrupt storm at exactly the moment the
  device needs its remaining power to finish a page program.
- Delivers the new `BATTERY_CRITICAL_VOLTAGE` event, appended to `battery_event_t` so existing values are
  unchanged. Both `battery_event_handler`s route it to the existing `APP_NOTIFY_BATTERY_EVENT`, which
  already means "flush and shut down" — no new notification type.
- `battery_monitor_has_brownout_detection()` lets callers query availability at runtime.

Verified by temporarily enabling the gate: builds clean on both the GPIO 11 and GPIO 10 paths, an
unsupported pin trips a `#error`, and `am_vcomp_isr` links as a strong symbol that overrides the weak
`am_default_isr` alias.

**Phase 4 — offload. ✅ COMPLETE, HARDWARE VALIDATED** (§12.10, §12.13, §12.14). Wire format (§8.1), per-page retransmission with a 3-round cap
(§8.1.1), header-based time seek (§8.2), and the host dual-format parser (§9). Couples to Phase 1 — the
wire format exposes the page format — so these should land together. *The header-based time seek in §8.2 was
later found to be unsound and replaced with an exact scan; see §15.4.*

**Phase 5 — watchdog. ✅ IN TREE AND HARDWARE-TESTED, BUT THE DESIGN BELOW IS SUPERSEDED.**

> ⚠️ **Read §15.2 before relying on anything in this subsection.** What is described below — a single pet
> from `TimeAlignedTask` every 300 s, `INTVAL`/`RESVAL` of 40/56, a ~1173 s reset window — is the *first* of
> three watchdog designs. It shipped, was measured (§12.20, §12.22, §12.27), and was then rebuilt twice
> because it could only ever catch a stall in the one task that held the pet.
>
> **The shipping design** is per-task check-ins: five tasks register, each checks in from a bounded wait in
> its own loop every `WATCHDOG_CHECKIN_INTERVAL_MS` (10 s), and `system_watchdog_pet()` pets the hardware
> only if *every* registered task is current within `WATCHDOG_CHECKIN_DEADLINE_MS` (60 s) — otherwise it
> declines and records which task is late. `INTVAL`/`RESVAL` are **4/8**, a reset window of ~167 s at the
> measured 20.94 s tick rather than ~1173 s. Every timing figure below is off by a factor of seven.
>
> The rest of this subsection is retained because the hazard analysis under "Three things to get right" —
> the arm site, the USB branch, the register widths — is still correct and still load-bearing.

The design below was implemented as written, with
two corrections to the hazard list that came out of tracing the power-off path — both recorded under "Three
things to get right" below, where the original claims were.

*What landed.*

- `system_watchdog_enable()` / `system_watchdog_disable()` / `system_watchdog_pet()` in `system.c`, declared in
  `system.h`. `AM_HAL_WDT_1_16HZ`, `AM_HAL_RESET_WDT_RESET_ENABLE`, and counts of 40/56 ticks — the table
  below proposed 28/40, revised after the tick was measured (§12.20).
- **Armed in `run_tasks()` immediately before the `xTaskCreateStatic` block**, past the last point at which
  the boot can still decide to sleep instead of run. See the corrected hazard #1 for why this placement, not
  `setup_hardware()`, is what actually closes the reset-loop hazard.
- **`am_hal_wdt_start(AM_HAL_WDT_MCU, false)` — never locked.** The HAL states `am_hal_wdt_stop()` "will not
  work if the WDT is already locked", so locking would make hazard #1 unfixable. Recorded because `bLock` is
  the kind of parameter that looks like hardening and is in fact the opposite here.
- `am_watchdog_isr()` in `system.c` alongside the other Ambiq ISRs. Clears the interrupt, **does not pet**,
  and reports which task last checked in and how long ago. It touches no FreeRTOS API beyond
  `xTaskGetTickCountFromISR()` and nothing in the storage layer — the wedged task may be mid-SPI transaction.
- `WATCHDOG_*` and `STORAGE_BUSY_*` in `app_config.h`, with the range guards as `#error` (hazard #3).
- `wait_until_not_busy()` bounded at `STORAGE_BUSY_TIMEOUT_POLLS`, then `print` + `system_reset(true)`.

*A limit worth knowing before relying on the ISR — **accepted, not a defect**.* Production builds define
neither `AM_DEBUG_PRINTF` nor `ENABLE_LOGGING`, so `print()` is a no-op macro and **the ISR compiles down to
nothing but the interrupt clear** — as does `print_reset_reason()` at boot, and as does the escalation
message in the bounded BUSY spin. On a deployed tag the watchdog still resets the device and still saves the
week; it just leaves no record of having done so. The diagnostic is a debug/bench affordance, exercised
through the `tests/Makefile` targets in §12.16.

**This was initially accepted as a limitation and has since been closed** — the overnight test needs two
devices logging simultaneously and only one console is available, so reading the reset cause out of the log
stopped being optional. See §12.21 for the `STORAGE_TYPE_RESET_REASON` record that now carries it.

**Why it is now justified rather than optional.** Deployments run about a week untouched, so a device that
wedges on day one loses six days. Everything else in the loss budget is bounded — a reboot costs at most
one flush window, the write head recovers by binary search, the time base is recovered from the log — and a
silent hang is the last failure mode that is not.

**The concrete hang path.** `wait_until_not_busy()` in `storage.c` is an unbounded spin on the flash BUSY
bit, called after every erase and every program:

```c
static void wait_until_not_busy(void)
{
   while ((read_register(STATUS_REGISTER_3) & STATUS_BUSY) == STATUS_BUSY)
      am_hal_delay_us(10);
}
```

If the flash never clears BUSY — chip fault, wedged SPI, a marginal supply on the flash — this never
returns. Worse, `am_hal_delay_us()` is a busy delay rather than a yield, and `StorageTask` runs above every
task but ranging (it was the highest, 5, when this was written; §12.4 explains why ranging now outranks it).
So a wedged flash starves BLE (3), the app task (2) and the time-aligned task (1). The device keeps its power
and stops being a device.

An earlier draft of this section argued that a return code would be preferable to a reset here, on the
grounds that a reset discards the RAM page buffer. That reasoning was wrong twice over. If the flash is
wedged the buffer cannot be written anyway, so there is nothing to preserve; and a firmware that keeps
ranging and answering BLE while logging nothing is a deployment that looks alive and produces no data. A
reset also re-runs `storage_init()` and so has a real chance of clearing a transient wedge, which a return
code never does.

**Design.**

| | |
|---|---|
| Clock | `AM_HAL_WDT_1_16HZ` — 16 s per tick; 1 Hz caps out near 255 s, too short |
| Pet | `TimeAlignedTask`, every 300 s (`BATTERY_CHECK_INTERVAL_S`) |
| Interrupt | ~28 ticks ≈ 450 s |
| Reset | ~40 ticks ≈ 640 s, about 2.1x the pet period |

⚠️ **The counts in this table are superseded twice over, and the 16 s tick is nominal only.** The datasheet
specifies LFRC frequency as typ 1024 Hz with **no min and no max** and states it "is used when short term
frequency accuracy is not important" — there is no tolerance to design against. Measured **20.94 s** per tick
on rev P, 24% slow, and the range guard now checks against a worst-case *fast* LFRC rather than the nominal
tick (§12.20).

| | interrupt | reset | reset window at 20.94 s/tick | where |
|---|---|---|---|---|
| as proposed here | 28 | 40 | 838 s | this table |
| second measurement | 40 | 56 | 1173 s | §12.20, §12.27 |
| **shipping** | **4** | **8** | **~167 s** | §15.2, `app_config.h` |

The shipping counts are small because the pet no longer waits on a 300 s task loop — check-ins arrive every
10 s from five tasks, so the reset window only has to clear the 60 s check-in deadline, not two battery
intervals.

*Why pet from `TimeAlignedTask`.* It is priority 1, the lowest real task. The pet therefore only happens if
every higher-priority task is still yielding, which makes it a genuine liveness signal rather than a proof
that a timer runs. Petting from a FreeRTOS timer callback or the idle task would be worse than useless: it
would cheerfully pet while `StorageTask` spins in the loop above, so the one hang that can be named would
go uncaught.

*Why the interrupt matters.* `am_hal_wdt_config_t` supports an interrupt before the reset. Firing it about
three minutes ahead gives an ISR the chance to log which task last checked in and what operation was in
flight, turning a mystery reboot into a diagnosable one — which is what matters when the device has been
unattended for a week and the console log is all there is.

Detection latency is up to about ten minutes. Against six days of lost data that is the right trade.

**Also bound the spin, and escalate deliberately.** Independently of the watchdog, cap
`wait_until_not_busy()` and, on persistent failure, call `system_reset()` with a logged reason. Same
outcome as letting the watchdog catch it, but the cause is recorded rather than inferred. The watchdog then
covers the hangs that have not been named.

*Implemented as a poll count, not a clock read.* `STORAGE_BUSY_TIMEOUT_MS` is 500, expressed as
`STORAGE_BUSY_TIMEOUT_POLLS` = 50 000 iterations of a 10 us delay. Each iteration also performs a status
register read over SPI, so **the count is a lower bound on the wall time waited, not an upper one** — real
elapsed time on a wedged part is more like a couple of seconds. That asymmetry is the right way round: the
bound must never fire during a legitimate operation (worst-case block erase ~10 ms, measured page program
~2.9 ms, so 500 ms is ~50x margin), and its upper end only has to stay well inside the ~1173 s watchdog
window, which it does by two orders of magnitude. Reading the RTC instead would be exact and would put a
clock read in a path taken after every program and every erase, for no benefit at either end of the range.

*The escalation is `system_reset(true)`, deliberately the immediate form.* `system_reset(false)` would queue
a flush that `StorageTask` would service by calling straight back into `wait_until_not_busy()` — the wedge
again, one level deeper. With the part not clearing BUSY the RAM page buffer cannot be written anyway, so
there is nothing the graceful form would save. This also matches how `spi_read()` and `spi_write()` already
escalate an exhausted retry count (`storage.c:233`, `:285`), which means the storage layer now has one
consistent answer to "the flash stopped responding" rather than two.

*Note which failure this does and does not add coverage for.* A wedged **SPI bus** was already handled —
`spi_read()`/`spi_write()` reset after four failed transfers. The gap this closes is narrower and nastier:
the bus works, the transfers succeed, and the part simply reports BUSY forever.

**Three things to get right, in order of how badly they bite.**

1. **Disable the WDT before `system_enter_power_off_mode()`** and re-enable on wake. A daily-times
   deployment sleeps for hours between windows, and `TimeAlignedTask` does not run during that; a live
   watchdog would reboot the device on a loop. This is the failure that would brick a deployment.

   *Corrected on implementation — the hazard is real but this framing of it is wrong in two ways.*

   **There is no wake to re-enable on.** `system_enter_power_off_mode()` ends in
   `am_hal_sysctrl_sleep(AM_HAL_SYSCTRL_SLEEP_DEEP)`, and its only production caller follows it immediately
   with `system_reset(true)` (`app_tasks.c:160`). The wake *is* a reboot, so `run_tasks()` re-arms the
   watchdog on the way back up and there is no re-enable to write. Further, that caller runs *before*
   `vTaskStartScheduler()`, so no live-WDT path reaches this function today at all. The
   `system_watchdog_disable()` at the top of it is a guard for a future caller reached from a running
   system, and is written as idempotent for exactly that reason — it is not what closes the hazard.

   **What closes the hazard is where the WDT is armed, and there are two branches to dodge, not one.**
   `run_tasks()` has three exits and only one may carry a live watchdog:

   | Branch | Pets? | |
   |---|---|---|
   | USB maintenance (`app_tasks.c:55-68`) | **no** — creates `UsbTask`/`UsbCdcTask`/`AppTaskMaintenance`, **no `TimeAlignedTask`** | must stay off |
   | `power_off` (`app_tasks.c:157-161`) | no — deep sleep for hours | must stay off |
   | ranging, or plugged-in BLE maintenance | yes — `TimeAlignedTask` is created in both variants | the only branch that may arm it |

   **The USB branch is a second way to brick a device and §11 did not name it.** A watchdog armed in
   `setup_hardware()` would reboot a plugged-in tag on a ~20 min loop *in the middle of a USB download* —
   the one situation where a person is watching, and where the reboot destroys the transfer. Arming
   between the `power_off` block and the `xTaskCreateStatic` block dodges both branches with one line.

   It also keeps every test target watchdog-free for free: tests have their own `main()` that calls
   `setup_hardware()` and never `run_tasks()`, and `test_storage.c` in particular spends minutes in loops
   with nothing that could pet.
2. **Set `AM_HAL_RESET_WDT_RESET_ENABLE`** and report the reset reason at boot. It is already printed, so a
   watchdog reboot becomes visible without new plumbing. Unlike the brownout enum, this flag *is* honoured
   by `am_hal_reset.c:76-79`.

   *Confirmed, with a redundancy and a caveat.* `am_hal_wdt_config()` already sets
   `RSTGEN->CFG_b.WDREN` from `bResetEnable` (`am_hal_wdt.c:110`), so the explicit
   `am_hal_reset_configure()` call is belt-and-braces; it is kept because it states the intent where a
   reader looks for it, and it is sequenced *after* `am_hal_wdt_config()` so the config write cannot clear
   it. `print_reset_reason()` already handles `bWDTStat` (`logging.c:88-89`) so no new plumbing was needed
   — but see the note above on that path being compiled out of production builds.
3. **Register width — verified, and there is a trap.** `WDT_CFG_RESVAL` and `WDT_CFG_INTVAL` are both
   **8-bit** fields (`apollo4p.h`: masks `0xff00` at bit 8 and `0xff0000` at bit 16), so the maximum count
   is 255 for each. That confirms the choices above:

   | clock | tick | max timeout | verdict |
   |---|---|---|---|
   | 1 Hz | 1 s | 255 s | **too short** for a 300 s pet period |
   | 1/16 Hz | 16 s | 4080 s (68 min) | proposed 40 ticks = 640 s, comfortably inside |

   The trap: `am_hal_wdt_config()` performs **no range validation**, and `_VAL2FLD` is defined as
   `(((uint32_t)(value) << field ## _Pos) & field ## _Msk)` — it *masks*. An out-of-range count is
   therefore silently truncated rather than rejected. Asking for 300 ticks yields 300 & 0xFF = 44, and
   asking for 256 yields **zero**. Both compile, both run, and neither reports anything. Whatever numbers
   are chosen must be asserted against 255 in our own code, because nothing below will do it.

   *Verified and implemented as `#error` rather than a runtime assert*, matching the house style at
   `battery.c:37`, and because a truncated watchdog count is a thing that must never reach a binary at all.
   `_Static_assert` was passed over: the build is `-std=c99`, where it is a GCC extension. Three guards in
   `app_config.h`: both counts ≤ 255; interrupt strictly before reset; and the reset window strictly greater
   than `2 * BATTERY_CHECK_INTERVAL_S`, so the configuration cannot drift into resetting on a single missed
   pet. The third guard is the one that will actually catch a future mistake — `BATTERY_CHECK_INTERVAL_S`
   lives four lines above the watchdog block and nothing else ties the two together.

   **All three were verified by deliberately breaking them**, since a guard that never fires is
   indistinguishable from a guard that cannot: `WATCHDOG_RESET_TICKS 300` (the doc's own example, which
   would silently become 44) trips the width guard; `20` trips both the ordering and the two-missed-pets
   guards; and a legal alternative (`WATCHDOG_INTERRUPT_TICKS 18`) still builds.

**A note on risk.** This is the one remaining change that can break a working deployment: a spurious reset
loop is strictly worse than the hang it prevents. It wants a session with the register widths checked and
the power-off interaction traced, not an implementation squeezed into the end of one.

*Post-implementation, the residual risk is narrower and has one specific shape.* Every path that could carry
a live watchdog was traced and each one either pets or bypasses the arm site:

- `TimeAlignedTask` pets at the top of its loop, before anything in the body can block, so the pet does not
  depend on `battery_monitor_get_level_mV()` or the storage queue succeeding.
- `storage_flush_and_shutdown()` (`time_aligned_task.c:55`) only *queues* `STORAGE_TYPE_SHUTDOWN` and the
  task loops on; it is `StorageTask` that flushes and resets, so the pet continues across a shutdown.
- `system_reset(false)` blocks its *calling* task on `portMAX_DELAY`, and the four callers are all
  `AppTask`/`RangingTask`, never `TimeAlignedTask` — so that path cannot silence the pet either. If the
  `StorageTask` leg of it wedges, the watchdog is exactly what catches it.
- `BLETask` blocks in `wsfOsDispatcher()` when idle and the offload path has no loops of its own since
  Phase 6, so a download cannot starve a priority-1 task. §12.19 offloaded 1.0 MB in 16 s against a ~1173 s
  window.

**The one untested assumption that matters** is that `vTaskDelay(300 s)` under `configUSE_TICKLESS_IDLE 2`
(the Ambiq implementation) really does wake on schedule every time, across hours. The 17.3-hour run in
§12.14 is indirect evidence — battery records are written from this same loop — but it was recorded with
nothing that punished a late wake, and now a wake later than the reset window reboots the device. **This is the
specific thing the overnight test in §12.16 exists to falsify, and it is why the watchdog should not go onto
a tag holding data anyone cares about until that test has run.**

**Phase 6 — BLE notifications. ✅ COMPLETE, HARDWARE VALIDATED** (§12.17). Not in the original plan; added
once per-page retransmission made unacknowledged delivery safe. 9x throughput.


12. Validation
--------------

Because phases land incrementally on `master` and no device is flashed for real data collection until the
work is complete, validation is split into a per-phase smoke test that must pass before the next phase
begins (this section) and a full deployment qualification deferred until every phase is in (§12.12).

This section is written as an experimental record: each experiment states the question it answers, the
method, the measured result, and the interpretation, followed by an explicit account of what the results
do *not* cover (§12.11).


### 12.1 Platform and method

| | |
|---|---|
| Board | TotTag rev P |
| MCU | Ambiq Apollo4 Plus (`AM_PART_APOLLO4P`), Arm Cortex-M4F |
| Log medium | External SPI NAND, 4096 B page + 256 B spare, 64 pages/block, 4096 blocks |
| Usable log area | 4016 blocks (80 reserved for bad-block management) = **257,024 pages = 1004 MiB** |
| Payload per page | 4092 B (v1 format: `'DA'` + `uint16` length + payload) |
| SPI | IOM at 48 MHz; on-chip NAND ECC enabled |
| Firmware | Phase 0 branch; `tests/Makefile` targets `storage` and `storage_reboot`, `AM_DEBUG_PRINTF` enabled |

**Log integrity is checked structurally, not by sampling.** Every page written by the harness carries a
4-byte `"PAGE"` magic, its own `uint32` index, and a body filled with `(uint8_t)index`. Read-back goes
through the *production* offload path — `storage_begin_reading()` → `storage_retrieve_num_data_chunks()` →
`storage_retrieve_next_data_chunk()` — and each page is checked for magic, length, index ordering, and the
complete body pattern. A page that is lost, truncated, reordered, or partially erased therefore fails
detectably rather than passing silently.

**Timing was measured with the Cortex-M4 DWT cycle counter.** Cycles-per-microsecond was calibrated
empirically against `am_hal_delay_us(10000)` rather than assuming a core clock, which keeps the figure
correct across MCU power-mode changes and doubles as a liveness check on the counter (DWT is gated without
a debugger on some parts). Measured 96 cycles/µs. To avoid perturbing the quantity under measurement, the
instrumentation printed only a *new* maximum, so after warmup it is nearly silent. The instrumentation was
temporary and has since been removed; it is recoverable from branch history if Phase 1 needs re-measuring.


### 12.2 Experiment 1 — erase-ahead correctness across block boundaries

**Question.** Does on-demand erase-ahead (§4.2) keep the region ahead of the write head erased as the log
advances across block boundaries?

**Method.** `make storage BOARD_REV=P` writes 200 tagged pages (3 × 64 + 8, guaranteeing three boundary
crossings), then reads all of them back and verifies each.

The harness exists because the effect is impractical to reach through a real deployment: two tags ranging
with each other generate ≈42 B/s, filling a 4092 B page every 97 s and therefore crossing a block boundary
only once per **1.75 hours**. The harness produces three crossings in under a second of flash activity.

**Result.**

```
Writing 200 pages of 4092 bytes (64 pages/block, 3 boundaries crossed)
  64 pages written (crossed a block boundary)
  128 pages written (crossed a block boundary)
  192 pages written (crossed a block boundary)
Read-back: device reports 201 chunks for 200 written pages (expect 201)
=== Block-crossing test PASSED: 200/200 pages verified, 0 errors ===
```

**Interpretation.** Erase-ahead is correct across boundaries. The chunk count of 201 matches the prediction
of 200 pages plus one empty chunk for the in-RAM cache, confirming the read path also accounts for pages
exactly.


### 12.3 Experiment 2 — write-head recovery and data survival across power cycles

**Question.** Boot-time head recovery in the v1 format is heuristic (§1.2), and `erase_ahead_of()` then
erases blocks past the recovered head. If recovery under-reports the head, live data is erased. Does
previously written data survive a power cycle, and does it keep surviving across repeated cycles?

This is the single Phase 0 path that can *destroy* data rather than merely fail to prevent corruption, and
is therefore the most important experiment here.

**Method.** `make storage_reboot BOARD_REV=P`. The log is self-describing, so no state is kept outside the
flash: each boot counts the pages already present, verifies **all** of them, appends 72 more (one full
block plus 8 pages, guaranteeing at least one boundary crossing per round), then halts awaiting a manual
power cycle. Repeated four times.

Because verification is cumulative, round *n* revalidates every page written by rounds 1…*n*−1.

**Result.**

| Round | Pages found | Verified | Errors | Total after round |
|---|---|---|---|---|
| 1 | 0 (empty log) | — | — | 72 |
| 2 | 72 | 72 | 0 | 144 |
| 3 | 144 | 144 | 0 | 216 |
| 4 | 216 | 216 | 0 | 288 |

**Re-run after Phase 2.** Phase 2 moved the write-head advance into `advance_write_head()`, which sits in
this path, so the experiment was repeated against the Phase 0 + Phase 2 firmware. Three further rounds
(0 → 72 → 144 → 216 pages) passed with 0 errors, confirming the refactor is behaviour-preserving.

**Re-run after Phase 1 Step B — the most significant repetition.** Step B replaced this path entirely:
recovery now scans the metadata ring for the highest valid epoch and binary-searches for the head, rather
than scanning linearly for two consecutive blocks without a data magic. Three rounds
(72 → 144 → 216 → 288 pages) passed with **0 errors**, the final round revalidating all 216 pages written
across every prior boot. This is the only test that exercises `find_newest_metadata()`,
`recover_write_head()`, and the `log_region_full` derivation on the recovery path.

*A test-quality problem surfaced here first.* The harness discards data it does not recognise and restarts,
which is convenient but converts a detection into a silent reset: after the Step A record-framing change, a
stale offset assumption (the tag moved from byte 0 to byte 5) made every boot classify its own data as
foreign, wipe, and report a clean round. Three consecutive "passing" rounds tested nothing, and the only
visible symptom was that the running page total never grew. The wipe path now prints a prominent warning
stating that the round verified nothing. **A self-healing test is indistinguishable from a passing one
unless the healing is loud.**

**Interpretation.** Head recovery and erase-ahead cooperate correctly across repeated power cycles, with no
progressive data loss — round 4 revalidated every page written across all prior boots.

*On reflashing between rounds:* the debugger connection is lost on power cycle, so the device was reflashed
before each round. This does not affect the result. J-Link programs the Apollo4's **internal MRAM** at
`0x00018000`; the log resides on a **separate external SPI NAND** the programmer never touches. The results
themselves confirm this — round 4 found 216 pre-existing pages after both a power cycle and a reflash.


### 12.4 Experiment 3 — storage-layer blocking time

**Question.** `erase_ahead_of_head()` runs in `StorageTask` at priority 5, above `RangingTask` at 4, and
the ranging protocol schedules on 500 ms intervals with microsecond slot precision. Does the added blocking
threaten ranging?

**Method.** DWT-based measurement as described in §12.1. Both `erase_ahead_of_head()` and `write_page()`
were instrumented: the erase figure is only interpretable next to the blocking the system already incurs on
every single page.

**Result.** Maxima over ≈488 `write_page` invocations and ≈12 `erase_ahead_of_head` invocations, across two
independent test builds and multiple boots:

| Operation | Max observed | Frequency | Amortized per page |
|---|---|---|---|
| `write_page` | 2855 – 2893 µs | every page | 2893 µs |
| `erase_ahead_of_head` (2 blocks) | 1946 – 1989 µs | every 64 pages | **30 µs** |

Derived single block erase ≈ **973 µs**, against a datasheet figure of typ 2 ms / max 10 ms.

**Interpretation.** Erase-ahead adds **≈1.05 % amortized overhead** to storage blocking, and its worst-case
single stall (1989 µs) is *below* the per-page cost already paid 64 × more often. It was never a new class
of risk.

One real issue did surface. Triggering the erase at the block boundary placed it in the same
`storage_flush()` activation as the page write that had just crossed it, concatenating both stalls into
~4.8 ms. Moving the trigger to the middle of each block (`ERASE_AHEAD_TRIGGER_PAGE`) gives the erase its own
activation, restoring worst-case contiguous blocking to a single page write — identical to pre-Phase-0
firmware. This measurement therefore also **retired a planned 1.75-hour ranging-timing experiment**: with no
increase in worst-case contiguous blocking, there is nothing left to observe.

**Revisited from the field (October 2026).** The conclusion above holds for every slot in a round, because those
are timed by the radio interrupt, which no task can delay. It missed the one moment that runs in a task: a
master's round start, from its wake-up timer through the ranging task to its first schedule copy. The schedule
tracing records (`ROUND_START`, `SCHEDULE_CATCH`) caught masters sending that copy about 3.7 ms late, each time right after filling a
page, because the round start handed storage a record and `StorageTask` then wrote the page before the ranging
task could transmit. Participants time their next wake-up from the round they caught, so each late round cost
the network the round after it. Ranging now runs at priority 5 and storage at 4: a page write that is under way
is preempted for the round start, and records wait in the queue meanwhile. The flash's SPI port is not the radio's
and its driver takes no critical sections, so preempting a page write mid-transfer is safe.

Timing values were reproducible to the microsecond across independent boots (2893 µs and 1946 µs observed
twice each), indicating both that the flash operations are highly deterministic and that the empirical
cycle calibration is stable.


### 12.5 Experiment 4 — conditional-compilation gate verification

**Question.** The VCOMP brownout driver (§7.1, Phase 3) is written but must be absent from every shipping
build, since no current board can support it.

**Method.** Symbol inspection with `nm`, plus differential builds with the gate forced on.

**Result.** There is a pitfall worth recording: `am_vcomp_isr` is declared as a **weak alias** in
`startup_gcc.c:68` and occupies vector slot 3, so the symbol is present in *every* build regardless of the
gate. Grepping for the name alone proves nothing. Symbol type and address are what distinguish the states:

| Gate | `nm bin/SociTrack.axf \| grep -i vcomp` |
|---|---|
| **OFF (shipping)** | `0002b82c W am_vcomp_isr` — weak, at the **same address as `am_default_isr`** |
| ON (forced, for verification) | `0003bef8 T am_vcomp_isr` — strong, own address, plus `brownout_detection_*` |

Reliable check: `nm … | grep "T am_vcomp_isr"` must be empty, or equivalently `grep brownout_detection`
must find nothing.

**Interpretation.** Gate verified off. Forcing it on additionally confirmed the driver compiles and links
on both the GPIO 11 (`CMPIN1`) and GPIO 10 (`CMPIN0`) paths, and that an unsupported pin trips a `#error`.


### 12.6 Experiment 5 — partial-page writes and the time-bounded flush (Phase 2)

**Question.** Phase 2 made `storage_flush(true)` advance the write head and clear the cache. Previously it
did neither, which was safe only because the sole caller reset the device immediately afterwards. With a
timer now able to fire that path mid-deployment, a head that fails to advance would re-program an
already-programmed NAND page. Does a partial page write correctly, and does the record stream survive
repeated partial-page seams?

**Method.** Two complementary tests.

*Synthetic* (`make storage`): write 3 full pages → force a 100-byte partial page → write 3 more full
pages → read back and verify all seven. Also asserts that `storage_has_buffered_data()` transitions
correctly at each step. This covers the full → partial → full transition.

*In situ*: a rev P device running the real firmware with `STORAGE_FLUSH_TIMEOUT_S` temporarily reduced to
10 s, logging for 60 s, then offloaded through the normal BLE path and parsed by the production host
parser. Because the entire log is under one page, every page written is a partial page — covering the
partial → partial transition the synthetic test does not reach.

**Result.** Synthetic test:

```
=== Partial-page flush test ===
  Wrote 3 full pages, then a 100-byte partial page
=== Partial-page flush test PASSED: 0 errors ===
```

In situ, 44 records recovered over exactly 60.0 s spanning 2–3 partial-page seams:

| Property | Observed |
|---|---|
| Records | 31 IMU + 11 motion + 2 voltage = **44** |
| Timestamps | strictly monotonic, **all** on the 500 ms grid |
| Payload | 456 B over 60 s = **7.6 B/s** |
| Battery | 4208 → 4215 mV (device on charge — plausible) |
| IMU | Z ≈ 2450 at rest (gravity), excursions to ±4000 while handled — physically sensible |
| Discontinuities | 4 gaps > 1 s, **every one bracketed by `motion=False` → `motion=True`** |

**Interpretation.** The record stream is continuous everywhere the device was producing data, and every
discontinuity has a semantic explanation *within the data itself* — the IMU is motion-gated, so it stops
reporting when the tag is still. No lost pages, no fabricated records, no desynchronisation across the
partial-page seams.

This also serves as the "read a realistic log" check in §12.12: mixed record types, real timestamps, several
partial-page boundaries, and a full round trip through the production host parser.

**Incidental finding worth carrying forward.** The measured rate of **7.6 B/s** is far below the 42 B/s
assumed in §6.2, because IMU logging is motion-gated rather than a constant 2 Hz. At 7.6 B/s a page fills
naturally in ~9 minutes, so for a mostly-stationary tag the **timer, not page-fill, is the binding
constraint** — the timer-dominated regime. The ceiling in §6.2 is rate-independent
(`86400 / STORAGE_FLUSH_TIMEOUT_S` pages/day), so the 88 MB/month figure at 120 s already bounds this case
correctly; but it does mean real deployments will sit at that ceiling rather than below it.


### 12.7 Experiment 6 — new page format end to end (Phase 1 Step A)

**Question.** Step A replaced the 4-byte `'DA'` page header with a 32-byte self-validating header carrying
CRC-32 over both header and payload, and replaced byte-append storage with record framing that never splits
a record across a page boundary. Does the log still write, recover, and read back correctly under the new
format, and does the record framing survive a round trip?

**Method.** `make storage BOARD_REV=P`, which now exercises three tests against the new format. The
block-crossing and partial-page tests were re-run unchanged in intent but reworked internally: each page is
now written as a single page-filling record, and verification unpacks the `[type][timestamp][data]` framing
rather than reading a raw blob. A third test was added after the first run (see below).

**Result.** All three passed:

```
=== Block-crossing test PASSED: 200/200 pages verified, 0 errors ===
=== Partial-page flush test PASSED: 0 errors ===
=== Experiment details round-trip PASSED ===
```

Page payload is now 4064 B rather than 4092 B, reflecting the 32-byte header — a 0.8% overhead for full
per-page validation and independent parseability.

**A silent-failure bug this surfaced.** The first run reported implausible experiment details. The cause was
not the storage layer: removing an obsolete test block also removed an incidental
`storage_enter_maintenance_mode()` call that the following block had been relying on, so
`storage_store_experiment_details()` ran outside maintenance mode. That function is wrapped entirely in
`if (in_maintenance_mode)` and **silently discards the write** — no return value, no diagnostic — so the
subsequent retrieve returned stale values that looked plausible.

The test previously only *printed* the retrieved details, which is why the swallowed write was invisible. It
now compares the retrieved struct against what was written and reports PASSED/FAILED.

This is a real sharp edge in production code, not just in tests: both the BLE and USB configuration paths
call `storage_store_experiment_details()`, and an incorrect mode would lose an experiment configuration
with no indication. Logged as a Step B fix — it should return `bool` and callers should check.

*Generalisable lesson worth recording: a test that prints rather than asserts will hide a total no-op
behind output that looks correct.*


### 12.8 Debugging note — three latent faults exposed by relocating the log (Phase 1 Step B)

Step B moved the log region to make room for the metadata ring at pages `[0, METADATA_RING_PAGES)`. Every
metadata write then failed with *"Unable to write experiment metadata to any slot in the ring"*. Three
distinct faults were involved, only the last of which was the actual cause, and the first two hypotheses
were shipped as fixes before being verified — each costing a flash cycle and, in one case, damaging the
device's bad-block table. **The diagnostic build that identified the real cause took one run.** Instrument
first when a failure is total and repeatable.

**Fault 1 — `erase_block()` re-asserts write protection on exit.** The metadata write lifted protection,
called `erase_block()` for the first slot of each block, and then programmed while the chip had been
silently re-protected. Real bug, fixed; not the cause of the total failure.

**Fault 2 — `add_bad_block()` on a failed metadata program.** Retiring a block because one program failed
is self-destructive in the metadata ring: a systematic fault walks the loop and retires the entire region.
It marked all 8 ring blocks bad, persistently. Removed.

**Fault 3 — recovery utility that destroyed what it was clearing.** `storage_reset_bad_block_table()`
erased the reserve using `erase_block()`, which calls `add_bad_block()` on failure, which **writes the
bad-block table back to flash**. The table returned *larger* (178 entries, exceeding the 80-block reserve,
marching through the reserve itself) instead of empty — while the function reported success, because it
printed unconditionally without verifying. It now issues raw block erases with no bookkeeping and verifies
by re-scanning for the `"BBM_"` marker.

**Why every ring slot was rejected.** With `bbm_index` inflated past the number of entries actually
written, the zero-filled tail of the table all read as block address **0** — a legitimate block address.
Placing the metadata ring at block 0 walked straight into that ambiguity. Empty entries are now `0xFF`
filled (`0xFFFFFFFF` can never match a block-aligned address), and an entry count exceeding the reserve is
rejected with a warning rather than acted on.

**The actual cause.** `storage_flush()` and `storage_store_record()` inferred "the log has wrapped and is
full" from `starting_page == current_page`. That held only while `starting_page` pointed at a metadata page
*preceding* the data. Step B redefined `starting_page` as the first **data** page, and epoch creation sets
both it and `current_page` to `new_log_start` — so the log reported itself full before a single page was
written, and every write was refused. Replaced with an explicit `log_region_full` flag set in
`advance_write_head()` when the head genuinely wraps onto the epoch start.

*Generalisable lesson: redefining the meaning of a shared variable silently breaks every invariant that was
piggybacking on its old semantics. Grepping for the identifier finds the reads; it does not find the
assumptions.*


### 12.9 Experiment 7 — exact time-range seek (Phase 4, §8.2)

**Question.** Time-range selection previously scanned payload bytes for a byte pattern resembling a voltage
record. Ordinary payload can match that pattern by coincidence, so the scan could select the wrong page and
silently drop or duplicate a span of the log — one of the mechanisms behind the original "missing data"
reports. Does the replacement, a binary search over the per-page header time bounds, land exactly?

**Method.** `test_time_range_seek()` writes 40 pages, each carrying one record stamped `500 x index` ms,
then seeks to a known time and asserts the first chunk returned is the page holding it. Every other test
calls `storage_begin_reading(0, 0)`, which short-circuits the seek, so without this the code path would
ship unexercised.

**Result.**

```
Sought 12000 ms; first page returned index 24 (ts 12000 ms), expected index 24 (ts 12000 ms)
Remaining chunks from that point: 17 (expected 17)
=== Time-range seek test PASSED ===
```

**A test-arithmetic error, caught on the first run.** The initial version chose a target *page* and computed
the seek time from it. `storage_begin_reading()` takes absolute whole seconds and converts with
`1000 * (t - experiment_start_time)`, so a relative time is always a multiple of 1000 ms while pages are
stamped every 500 ms — **only even page indices are addressable**. Choosing an odd page truncated the
half-second and the test failed while the firmware was behaving correctly. The test now derives the
expected page *from* the seek time, so the two cannot disagree.

The chunk count is also part of the pass criterion now rather than merely printed. That is the third
instance in this work of a printed-but-unasserted value concealing a defect (see §12.7 and §12.3); printed
values are no longer treated as verification.

*Residual API limitation, pre-existing and unchanged by this work:* seek resolution is one second, while
records are stamped every 500 ms. Half-second boundaries are not expressible.


### 12.10 Experiment 8 — page-framed offload on real hardware (Phase 4, §8.1)

**Question.** Does the page-framed wire format survive both transports intact, and does it alter the data?

**Method.** Two devices ranging to each other for several minutes, one on v2 firmware (AE) and one still on
v1 (F3), downloaded over BLE and USB. Cross-checking a v2 device against a v1 device is the strongest
available test: the two formats must yield identical measurements from the same physical event.

**Result.**

| Check | Outcome |
|---|---|
| Stream size vs declared header | exact match on both transports |
| Pages received | 5/5 and 3/3, no truncation |
| CRC failures / holes | none |
| Sequence numbers | strictly increasing |
| Records rejected or pages abandoned | none |
| Record types round-tripped | all six |
| **AE over BLE vs USB** | 319 shared range readings, **0 disagreements** |
| **AE (v2) vs F3 (v1)** | 319 paired timestamps, **319/319 identical, max delta 0 mm** |

**Interpretation.** The format alters no data and introduces no timestamp drift across firmware
generations. Framing overhead is the 16-byte stream header plus 20 bytes per page.

**Five defects surfaced by framing, all pre-existing and all previously invisible.** Each was silently
swallowed by v1's resynchronising scan, which skipped anything it could not decode:

1. **Motion/charger conflation.** `motion_code_t` carried four states through a record the format defines
   as boolean, so charger state was recorded as "in motion" in every log ever collected. Charger state now
   has its own record type — one the format always had and no firmware had ever written.
2. **Stranded USB tail.** `tud_cdc_write_flush()` was called by the code that *requested* a download, which
   returns before any data exists, so the final partial CDC buffer never left the device. v1 lost its tail
   too; with no declared length, a short log was indistinguishable from a complete one.
3. **RTC underflow.** `rtc_get_timestamp_diff_ms()` subtracted unsigned timestamps, so a reference in the
   future wrapped to ~2^32 and was then multiplied by 1000. Records written at boot legitimately predate
   the experiment start, so this fired routinely.
4. **Stale experiment epoch.** `experiment_start_time` was latched once at boot, so creating an experiment
   at runtime left every subsequent record timestamped against the *previous* experiment's start.
5. **Sequence numbers never incremented.** Every page in a session shared a sequence number; the value only
   changed across reboots. Retransmission would have requested the wrong pages.

*The generalisable point: a lenient parser does not prevent data loss, it conceals it. Five distinct
defects survived years of deployments because the reader silently discarded whatever it could not
interpret. Strict framing did not create these problems; it made them addressable.*


### 12.11 Threats to validity and untested paths

Stated explicitly so the results are not over-read.

**Limits of what was measured**

- **Fresh flash.** All timing was taken on an unworn part. NAND erase and program times degrade with
  cycling; the measured ~973 µs block erase sits well under the 10 ms datasheet maximum, so aged media
  could be several times slower. This is precisely why the erase was decoupled from the page write rather
  than declared safe on the strength of the measured value.
- **Reboot-while-idle only.** The power cycle in §12.3 occurs with the flash quiescent. Power loss *during*
  a page program or block erase is not covered; 72 pages write in ~216 ms, too fast to interrupt by hand.
- **Single device, single revision.** All results are from one rev P board. The `REVISION_ID < REVISION_N`
  path (rev M) has a different bad-block-management implementation and is compile-verified only.
- **Uniform page content.** The harness writes only full 4092 B pages of uniform data. Real logs contain
  mixed record types and a partial final page from the shutdown flush.
- **Small sample for erase timing** (≈12 invocations), though the observed spread was zero.

**Code paths not exercised**

- **Bad-block relocation** (`write_page` failure branch, which Phase 0 modified to erase the relocation
  target and re-assert write protection). Cannot be induced without failing media.
- **Memory-full wraparound** — the `block == (starting_page & 0xFFFFFFC0)` guard in `erase_ahead_of()`
  requires ~1 GB written to reach.
- **Power loss during the scoped new-experiment erase** — the window is ~160 ms rather than 8–40 s, but it
  is not atomic until Phase 1's epoch scheme.
- **Torn writes** (page program or block erase interrupted by power loss). Pre-existing behaviour that
  Phase 0 neither improves nor worsens; Phase 1's `header_crc` / `payload_crc` are what make a torn page
  detectable rather than silently wrong.


### 12.12 Experiment 9 — retransmission primitive

**Question.** Does `storage_retrieve_page_by_seq()` return the correct page regardless of request order,
refuse sequence numbers that were never written, and leave a sequential read undisturbed?

**Method.** `test_page_retransmission()` writes 40 pages, then requests seven of them deliberately out of
order (37, 0, 19, 5, 39, 1, 20) including both ends, so the binary search cannot pass through locality
alone. It then requests a sequence number well past the end, and finally resumes the sequential read that
was already in progress.

**Result. PASSED on first run, 0 errors.** Every out-of-order request returned the right page with a
matching header; a never-written sequence returned nothing rather than the nearest page; and the
interleaved sequential read continued from where it left off.

Returning the nearest page would have been the dangerous failure here — the host would have accepted a
wrong page as a successful repair, which is worse than the gap it was trying to fix.

**A note on the harness.** Changing the chunk count so an empty in-RAM buffer is no longer announced as a
page broke three existing tests immediately, each off by exactly one. That is the first behavioural change
in this work caught by the tests rather than by hardware inspection: the equivalent earlier defects
(motion values, the sequence counter, the stale epoch base) all reached the bench because nothing asserted
on them.


### 12.13 Experiment 10 — retransmission end to end (host)

**Question.** Does a repair round actually recover data, converge, and stop?

**Method.** Two harnesses, neither needing hardware. The first drives the parser directly over synthetic
streams containing holes, CRC failures, mid-payload cuts and CRC-valid short pages. The second drives the
dashboard's own repair state machine against a fake device that can be told to fail chosen pages a chosen
number of times, exercising the real `download_logs_done` loop.

**Result. 15 + 15 + 43 checks pass.** A page lost to a hole or a bad CRC is recovered and its records appear in
the output; a page that stays unreadable is still reported after the cap; a clean transfer and a legacy v1
stream both issue no request at all; and the 53-file v1 corpus parses byte-identically with and without
the repair machinery.

**One real defect, found by the truncation case.** Repaired copies of pages lost to a truncated transfer
were collected and then silently discarded, because `parse_v2` only walks frames the stream actually
carried and a truncated tail has none. The loop therefore re-requested the same pages every round and gave
up at the cap holding data it had already received. Repairs for unseen sequence numbers are now decoded
after the frames the stream did carry; the truncation case converges in one round instead of exhausting
three.

This is the second time a defect in this work surfaced only under a condition that is awkward to produce
on a bench — the first being the metadata-ring aliasing bug. Simulating the transport, rather than only
the storage layer, is what made it cheap to find.

**A third harness, on real device bytes.** The one v2 download available (`AE_1786449600.ttg`, 3 pages,
429 bytes, declared and actual size agreeing exactly) was damaged every way the transport can damage it —
each page in turn replaced by a hole, each page in turn corrupted by a single flipped bit, and the stream
truncated before each page boundary — and in every case the repair restored a decode byte-identical to the
undamaged original.

That exposed a limit worth stating plainly: **a transfer in which no page arrived cannot be repaired by
naming pages.** Sequence numbers are only inferable relative to one that did arrive, and a stream does not
necessarily begin at sequence zero, since a wrapped log or a time-bounded download starts partway through
the epoch. The host now distinguishes the two cases — a partial loss is repaired page by page, a total
loss repeats the whole download — rather than finalizing an empty log quietly.


### 12.14 Experiment 11 — overnight offload, and a v2-against-v1 cross-check

**Question.** Does the page-framed format hold up over a full deployment-scale transfer, and does it
reproduce what the old format recorded?

**Method.** Two devices ranged with each other overnight. One (AE) had been reflashed to v2, the other
(F3) was still running v1 — which was unplanned, and turned out to be the most useful control available:
the same 17.3-hour deployment recorded twice, once in each format.

**Result.**

| | AE (v2) | F3 (v1) |
|---|---|---|
| Transfer | 1,129,251 B over BLE | 1,118,978 B |
| Pages | 520, seqs 0–519 contiguous | n/a (unframed) |
| CRC failures | **0** | not detectable in v1 |
| Holes | **0** | not detectable in v1 |
| Record groups | 123,879 | 123,922 |

Of 123,862 timestamps carrying ranging data on both devices, **123,327 (99.57%) are identical and every
difference is at most 25 mm** — two-way ranging asymmetry, since each device computes its own estimate,
not a storage artefact.

**A real defect, from nine bytes.** AE's stream ran nine bytes past its own declared length: a voltage
record logged *during* the transfer, after `total_payload_bytes` had been sampled. Harmless at nine bytes,
but at twenty or more the host would have read the tail as a page frame, invented a corrupt page, and
requested a retransmission for a page that never existed. `parse_v2` now stops after `total_pages` frames
rather than when the bytes run out.

**BLE baseline.** 315 s for 1.13 MB = **3.6 KB/s (28.7 kbit/s)**, or ~68 ms per 244-byte indication. That
is far longer than any connection interval, which establishes that the transfer is round-trip-bound rather
than bandwidth-bound: every chunk costs a host write, a device indication and an ATT confirmation. This is
the number Phase 6 has to beat.


### 12.15 Debugging note — a boot loop introduced by the seeding change

Seeding the network time offset at boot (§7.3) put a flash read at the top of `StorageTask`, which runs
*before* the maintenance-mode setup at the bottom of the same function. `storage_exit_maintenance_mode()`
leaves the SPI peripheral in `AM_HAL_SYSCTRL_DEEPSLEEP`, so the new code was reading a sleeping IOM.

Only one of two devices boot-looped. `storage_recover_last_ranging_timestamp()` returns early when the
epoch is empty, *before touching the SPI at all*, so the device with no log booted normally and the device
with real data faulted. That asymmetry is the fingerprint.

Nothing caught it earlier: it is neither a compile-time nor a link-time error, and every unit test runs
inside maintenance mode, where the peripheral is already awake. The convention it violated is visible three
functions away — `storage_retrieve_experiment_details()` wraps its read in exactly the wake/restore pair
that was missing. The fix adds that pair, and restructures the function to a single exit path, since the
original returned from inside the loop and would otherwise have leaked the peripheral awake.

The lesson generalises past this bug: a new function that reads flash was reviewed for what it did to the
*contents* and not for what state the *hardware* had to be in to run at all. "Read-only" was true and
"safe" did not follow from it.


### 12.16 Remaining validation

**Per-phase smoke test** (must pass before the next phase begins)

- [x] **Read a realistic log.** Satisfied by the in-situ half of Experiment 5 (§12.6): 44 records of mixed
      type recovered over 60 s through the production BLE offload and host parser, with monotonic
      on-grid timestamps, physically sensible IMU and battery values, and every discontinuity explained by
      motion gating within the data itself.

      *An A/B byte-diff against `master` was considered and deliberately dropped.* The read-path changes
      (`validated_page_length()` and its five call sites) are already exercised by the 288 pages verified in
      §12.3; Phase 0 did not change the on-flash page format, so old and new firmware read identical bytes
      through identical code; and every one of those call sites is deleted in Phase 1. **If any intermediate state is ever deployed for real data collection, reinstate the byte-diff.**

**Phase 2 specifically**

- [x] **Timed flush fires and is bounded.** Confirmed in Experiment 5 — `storage_flush()` observed firing
      on a 10 s cadence whenever data was present, and the offloaded stream was continuous across every
      partial-page seam, so the head advanced correctly on each one.
- [x] **No flush when clean.** Confirmed: flushes occurred only when data was available, and the 25 s and
      12.5 s motion-gated quiet periods produced no writes.
- [x] **Deadline is not extended by a trickle — closed by §15.8.** Experiment 5 could not show it: its
      longest continuous data run was 8.5 s against a 10 s timeout, so the deadline was never under pressure.
      The 3.9-day deployment supplies the case directly. Roughly half of it is spent idle, writing nothing but
      the 300 s anchor-plus-voltage pair, and **every one of those pairs lands in its own page** — 525 to 588
      pages per device of 18 bytes each, with page intervals clustered at 299.4 s and a maximum of 419 s. An
      extended deadline would have accumulated several 300 s batches into one page and produced intervals of
      600 s and 900 s; there are between one and five above 300 s per device, and none above 419 s. The
      deadline is armed when the cache first becomes dirty and is not pushed out by later writes.
- [~] **Reboot loses at most one timeout window.** Accepted without direct measurement: the mechanism is
      the same one already demonstrated in §12.3 and §12.18, and the bound follows from
      `STORAGE_FLUSH_TIMEOUT_S`. Recorded as reasoned rather than measured. The original procedure was to
      repeat §12.3 with the timer active and confirm the data
      lost to a hard reset is bounded by `STORAGE_FLUSH_TIMEOUT_S` rather than a full page.

**Before any deployment** (run against combined Phase 0 + Phase 1 firmware)

- [~] **5-hour two-tag run with scheduled resets.** The uninterrupted half is covered by the 17.3-hour
      two-device run in Experiment 11 (§12.14), which also cross-validated the format against v1 at
      99.57% agreement. Still outstanding is the *reset* half: Tag A untouched as a control; Tag B hard-reset at
      roughly 1.5 h, 3 h and 4 h. At ~42 B/s a page fills every 97 s, so 5 hours is ~185 pages ≈ 2.9 blocks.
      Tag A must be gap-free; Tag B must be continuous across every reset seam.
- [x] **Reported vs. actual byte count.** Tested by Experiment 11 (§12.14), and it does *not* match
      exactly: a device that keeps logging during a transfer overshoots its own declared
      `total_payload_bytes`, because that figure is sampled before the last page is read. Resolved on the
      host, which now bounds the parse by `total_pages` rather than by the byte count. The page count is
      exact; the byte total is a lower bound.
- [x] **Download during an active experiment.** Demonstrated by Experiment 11 (§12.14): the overnight
      logs were offloaded while both devices were still logging, which is precisely how the declared-length
      overshoot above came to light.
- [x] **New-experiment timing over BLE** — confirmed effectively instantaneous, against 8–40 s before.
- [ ] ~~superseded~~ — must complete well under a second (was 8–40 s) with no
      disconnect, including on a device carrying a large existing log.
- [x] **Graceful shutdown** still flushes the partial page. Covered by Experiment 12 (§12.18): both
      devices were shut down onto their chargers, and both final partial pages are present and intact —
      AE's page 7 and F3's page 6, each holding the two records written after ranging stopped.
- [x] **Role promotion carries the time base forward** (§7.3). Tested with the two devices deliberately
      configured 600 s apart, ranging, separated until contact was lost, reunited, then charged one at a
      time. **No 600 s jump appeared in either log**, so the base survived promotion — the one path the
      refactor added that had never executed. What the test *did* expose is recorded in §12.18.
- [ ] ~~Role promotion~~ (superseded). Not tested by simply running two devices:
      role is elected by UID (`app_task_ranging.c:155`, higher wins), so the master holds the role from the
      start with an offset of zero, which behaves identically to the code before the refactor. The case
      that matters is a *promotion* — a device that learned an offset as a participant then becoming
      master. To produce it: configure the two devices with experiment start times a known distance apart
      (~60 s, so the offset is large enough to see; with synced clocks it would be near zero and a
      regression invisible), let them range until the participant has learned the offset, then power off
      the master so the participant promotes itself. Download the promoted device's log and inspect
      `time_discontinuities`: none means the base was carried forward, a jump of the configured size means
      it reverted to its own clock.
- [x] **Faster connection interval on a deployment-scale log** (§12.19). Measured at **1.95x** on a
      464-page, 1.0 MB log offloaded in 16 s — 62.98 KB/s, and 17.6x against the original baseline.
- [x] ~~**Both board revisions.**~~ **WON'T FIX** (§15.7 item 18) — no further rev M devices are being
      manufactured, so the `REVISION_ID < REVISION_N` path stays compile-verified only.
- [x] ~~**Re-measure blocking time on worn media.**~~ **WON'T FIX** (§15.7 item 18) — not going to happen,
      and the measured ~973 µs block erase already sits well under the 10 ms datasheet maximum.

**Phase 5 specifically** — all complete except the overnight acceptance run. They were run in the order
below, chosen so that each item's failure mode is worse than the last's and the first two need no data on
the device.

*Build target for all of the on-device tests below: `full`, from `software/firmware/tests`.* The watchdog is
armed in `run_tasks()`, which only `main.c` calls, so no `tests/peripherals/*` target arms it at all — and
`tests/Makefile` defines `AM_DEBUG_PRINTF`, which is what makes `print()` real and the ISR observable. The
production `make BOARD_REV=P` build cannot be used for any test that reads a console.

```sh
cd software/firmware/tests
make clean                                  # REQUIRED when switching targets, see below
make full BOARD_REV=P                       # build + flash over J-Link, then run
make full BOARD_REV=P BUILT_BY=eclipse      # build only, no flash — for checking a temporary edit compiles
make full BOARD_REV=P SEGGER_SERIAL=<sn>    # pick one probe when two devices are attached (overnight test)
```

Three things about this Makefile that will otherwise cost time:

- **`make clean` is required when switching targets.** The object rule (`tests/Makefile:432`) depends only
  on the source file, not on `CFLAGS`, and every target sets different defines. Going from `storage` (no
  `__USE_FREERTOS__`) to `full` (with it) silently relinks stale objects.
- **`make full` flashes as part of the target** — `program` is a dependency, not a separate step. Use
  `BUILT_BY=eclipse` to build without touching the device.
- **`full_exp` reports two pre-existing `-Wunused-function` warnings** (`seek_page_for_timestamp`,
  `validated_payload_length`), an artifact of its `_TEST_NO_EXP_DETAILS` combination gating out their call
  sites. Unrelated to Phase 5; `full` is clean.

⚠️ **The watchdog does not stop when a debugger halts the core, and the margin is now under three minutes.**
Nothing freezes the WDT counter on a breakpoint, so pausing anywhere for more than the reset window resets
the device mid-session and will look like a bug in whatever was being stepped through. At the shipping
`RESVAL` of 8 that window is **~167 s**, not the ~1173 s this section was originally written against — a
seven-fold reduction, and short enough that an ordinary pause to read a variable can trip it. Comment out
`system_watchdog_enable()` while single-stepping; "keep debug sessions short" is no longer good enough
advice.

- [x] **Range guards reject a truncating count.** Verified by deliberately breaking them, see §11 hazard #3.
- [x] **Builds clean on both revisions with no warnings**, in the production configuration (`print()`
      compiled out) *and* in the debug configuration with `AM_DEBUG_PRINTF`, with and without
      `__USE_FREERTOS__` — the ISR has a separate branch for the non-FreeRTOS case and `system.c` is
      compiled into test targets that do not define it. `am_watchdog_isr` confirmed to link as a strong
      symbol overriding the weak `am_default_isr` alias, the same check Phase 3 needed for `am_vcomp_isr`.
- [x] **The watchdog actually resets, and the ISR fires first — passed twice, and the tick is not 16 s.**
      Re-run against the 40/56 counts after the first run measured the tick at 28/40. Second run:
      `Last check-in was from startup, 825540 ms ago; reset in ~256 s`, reboot ~19 min after a ~2:30 pm
      start. Corrected for the 0.21% fast tick that is **823.8 s / 40 = 20.60 s per tick**, against 20.94 s
      from the first run — two independent measurements agreeing to 1.6%, and both ~24% slow. See §12.20.

      **The wall-clock reboot time is the important part of this second run.** 56 ticks x 20.60 s = 1154 s
      = 19.2 min, and the observed reboot was 19 min after start. That is the FreeRTOS tick and a wall clock
      agreeing on the same interval through completely independent references, which closes the last doubt
      about whether the tick might have been the thing that was wrong. It is not; the LFRC is slow.

      *One cosmetic defect this exposed:* the ISR's `reset in ~256 s` is computed from the nominal 16 s tick
      and is therefore wrong by the same 31%; the real gap was ~330 s. Now labelled as nominal in the print
      rather than stated as fact.

      **Counts have changed twice since this ran, so it needs repeating** — the table below is for the 40/56
      counts, and the shipping build is at **4/8**, so expect the ISR line at ~84 s and the reboot at ~167 s.
      Note also that suppressing "the pet" now means suppressing a *check-in*: any one of the five registered
      tasks failing to check in is enough to stop the pet, which is the property under test.
      **§15.2 records that the pre-reset interrupt does not fire at 4/8**, so the ISR line may be absent
      entirely; that is a known unresolved disagreement with the measurement below, not a new failure.
      Procedure: suppress one task's check-in, then watch the console for the reboot banner.

      ```sh
      # temporary edit: comment out the pet in src/tasks/time_aligned_task.c
      #    // system_watchdog_pet("TimeAlignedTask");
      cd software/firmware/tests && make clean && make full BOARD_REV=P
      # revert with:  git checkout ../src/tasks/time_aligned_task.c
      ```

      | | nominal 16 s tick | measured 20.94 s tick |
      |---|---|---|
      | ISR line (40 ticks) | 640 s | **~838 s** |
      | reboot (56 ticks) | 896 s | **~1173 s** |

      Expect the measured column. The check-in name will be `"startup"` rather than `"TimeAlignedTask"`
      precisely because the pet was removed, so that string doubles as proof the test did what it intended.

      **Also check the arm-time line**, which is new and makes the configuration self-verifying:
      `INFO: Watchdog armed -- WDT->CFG = 0x... (clksel 4, intval 40, resval 56)`. `clksel 4` is the LFRC
      1/16 Hz tap; anything else means the clock source is not what this design assumes. `intval`/`resval`
      not matching the configured counts would mean `am_hal_wdt_config()` silently truncated them.
- [x] **`bWDTStat` is set on the next boot — passed.** `Reset Reasons: Watch Dog Timer Reset,` observed in
      the boot banner after the forced reset. Confirms the reset arrived through `RSTGEN` rather than as a
      hard fault that happened to coincide, and that `AM_HAL_RESET_WDT_RESET_ENABLE` took effect.
- [x] **The bounded BUSY spin escalates — passed.** Confirmed on hardware: the escalation path is reached
      and the device resets with the cause logged. **This is not a test that the loop hangs — it cannot hang
      any more, that is the point of the change.** What is under test is the *escalation path*: that the loop
      terminates at its bound and that the `print` and `system_reset(true)` after it are reached. So the edit
      is not "make it spin forever"; it is "make the early return unreachable", after which the loop runs its
      full `STORAGE_BUSY_TIMEOUT_POLLS` and falls out of the bottom.

      Flip the comparison so the early return fires only when the part *is* busy — which, a few polls into a
      real operation, it never is:

      ```sh
      # temporary edit in src/external/nandlog/chips/nandlog_chip_<part>.c, wait_until_not_busy():
      #    if ((read_register(STATUS_REGISTER_3) & STATUS_BUSY) == STATUS_BUSY)   // == instead of !=
      #        return;
      cd software/firmware/tests && make clean && make full BOARD_REV=P
      # revert with:  git checkout ../src/external/nandlog/chips/
      ```

      *Path note: the spin now lives in each chip driver rather than in one `storage.c`, and the bound is
      `NANDLOG_BUSY_TIMEOUT_POLLS`, derived in `nandlog_chip_common.h` from `NANDLOG_BUSY_TIMEOUT_MS` and
      `NANDLOG_BUSY_POLL_INTERVAL_US` in `nandlog_conf.h`. Each fitted part needs the edit separately.*

      Expect `ERROR: Storage flash never cleared BUSY after 50000 polls (>= 500 ms); resetting` within a
      second or two of boot, since `nandlog_init()` reaches `wait_until_not_busy()` early,
      then a reboot loop — the correct outcome, and the reason to revert before doing anything else. The
      delay between the boot banner and that line is also the only direct read available on the real cost of
      50 000 polls, which §11 only bounds from below; worth noting if it is far from ~2 s.

      *Setting `STORAGE_BUSY_TIMEOUT_MS` to 0 also reaches the escalation and needs no source edit, but it
      makes the poll count 0 so the loop body never runs — it tests the `print` and the reset while skipping
      the loop that is the actual subject. Prefer the comparison flip.*
- [x] ⚠️ **A daily-times device sleeps through its off window without rebooting — passed.** The failure this
      guards against is the reset loop that would brick a deployment, and it is the reason the arm site is
      where it is. One power-off, one RTC wake, no watchdog reset. Procedure retained below.

      **The off window must exceed the reset window or the test passes vacuously** — a short off window
      proves nothing, because the RTC would win the race regardless. At the shipping 8-tick reset that window
      is **~167 s**, so **15 minutes is now comfortably decisive** where 30 was needed before. The bound has
      moved twice; size any repeat against the `WATCHDOG_RESET_TICKS` actually in the build rather than
      against a figure quoted here.

      No code edit needed if the experiment is configured over BLE the usual way: set `use_daily_times` with
      an off window ≥ 15 min that the device is *currently outside*, then reboot it and confirm exactly one
      `WARNING: Powering off...` followed by exactly one wake at the daily start time.

      ```sh
      cd software/firmware/tests && make clean && make full BOARD_REV=P
      ```

      If configuring by hand is easier, the `_USE_DEFAULT_EXP_DETAILS` block at `app_tasks.c:92-102` is the
      hook — but note **`daily_start_time` and `daily_end_time` are seconds since midnight**, not hours
      (`rtc_get_time_of_day()` returns `3600*h + 60*m + s`). The existing defaults of `1` and `23` are
      seconds-of-day 1 and 23, a 22-second window; harmless today only because that block leaves
      `use_daily_times` at 0 so they are never read. Setting `use_daily_times = 1` without also fixing those
      two values would produce a device that is awake for 22 seconds a day.

      Pass criterion is the boot banner: `Reset Reasons: Watch Dog Timer Reset` appearing here means the arm
      site regressed and the deployment hazard is live. Expect `SW Power-On Reset` instead.
- [~] **A USB maintenance session survives longer than the reset window — accepted as reasoned, not
      measured.** The original procedure asked for a download held open past the reset window, which is not
      reproducible: a transfer that fast cannot be stretched to 25 minutes, and forcing one would test the
      transfer rather than the hazard. The hazard itself is structural and settled by inspection: the USB
      branch of `run_tasks()` returns to `vTaskStartScheduler()` without ever reaching
      `system_watchdog_enable()`, so the watchdog is not merely unpetted in USB mode, it is never armed.
      There is no state in which a USB session can be reset by it.

      *If a cheap confirmation is ever wanted*, it does not need a download at all — plug in, leave the
      device idle in maintenance mode for 25 minutes, and confirm no reboot. Dwell time is the variable, not
      transfer size. Recorded here so the distinction is not lost if someone revisits this line.
- [ ] ~~superseded~~ ⚠️ **A USB maintenance session survives longer than the reset window.** The branch §11 originally missed, and the
      one where a spurious reset is most visible to a person. Plug in over USB, stay in maintenance mode for
      **>25 minutes** (comfortably past the ~1173 s reset window at the measured tick) with a download in
      flight, confirm no reset. Same build as above; the USB branch is
      selected at runtime by `usb_cable_connected()`, so nothing to configure.
- [x] ⚠️ **An extended run does not reset spuriously — RESOLVED, and the answer is better than "pass".**
      Run 1 (§12.22) found three watchdog resets and could not say whether they were hangs or spurious. Run 2
      (§12.27) was **3.94 days on four devices** and found eight watchdog resets — and the anchors measure
      them directly: outages of **1120–1457 s** against a 1159 s reset window. **They are genuine ~19-minute
      hangs, and the watchdog caught every one.** The 34 remaining reboots are the daily-times and charge
      cycles behaving as designed.

      So the acceptance criterion as originally written ("no `Watchdog` entry") is the wrong test: watchdog
      resets are not the defect, they are the mechanism reporting one. **What the run establishes is that the
      watchdog works and that a real hang exists at roughly 2 per device per 4 days**, costing ~19 minutes
      each. Finding that hang is now the open work, and it is a ranging/BLE-side problem rather than a storage one.

      **Superseded by §15.8.** Two further runs exist: the 22-hour run in §15.6 (zero watchdog resets, but
      only a one-in-seven result) and the 3.9-day run in §15.8, which is the multi-day repeat §15.6 asked for.
      §15.8 is the current answer, and it is that the reset *rate* is unchanged at ~0.02/device-hour while
      the *cost* per event fell from ~1080 s to ~159 s. The storage layer came through it with zero losses of
      any kind.


### 12.18 Experiment 12 — role promotion, and the limit of seeding

**Method.** Two devices configured with experiment start times 600 s apart, so any reversion to a local
clock would be unmistakable. Ranged, moved apart until contact was lost, brought back together, then
placed on chargers one at a time — a reboot for each, with ranging already stopped.

**Result — the refactor holds.** Neither log contains a 600 s jump. The network base survived the
separation, the reunion and the role changes between them. F3 reported no discontinuity at all.

**Result — seeding is weaker than claimed.** AE reported one discontinuity of 22.0 s at its post-reboot
page. §7.3 predicted a seeding error bounded by "the final write and the reboot", i.e. sub-second. That
was wrong. The error is the gap since the last *ranging* record, and ranging can stop long before the
reboot does:

| AE, immediately before the reboot | |
|---|---|
| Last ranging record | 35,775,000 |
| Last record of any type | 35,797,000 |
| First record after the reboot | 35,775,000 |

Ranging stopped when the peer went on its charger; AE kept logging voltage, charging and motion for a
further 22 s, then rebooted and seeded from the stale ranging timestamp. F3 escaped only because it
stopped first, so its gap was zero.

**Fix.** Seed from whichever is newer, the last ranging record or the newest timestamp of any type. The
latter is a floor on how far the clock has already advanced, so the seeded clock can no longer step behind
what is already in the log. Being late by the gap is closer to the truth than being early by it, and the
next ranging round corrects the remainder.

**Verification.** The identical procedure was repeated after the fix. AE reproduced the triggering
condition more severely than the run that failed — a 43.5 s gap between its last ranging record
(36,913,000) and its last record of any type (36,956,500), against 22 s before — and reported no
discontinuity. Page 6 ends at 36,956,500 and page 7 begins at 36,956,500, so the clock resumed exactly
where it stopped. Both logs: zero holes, zero CRC failures, not truncated.

That the fault condition was *stronger* on the passing run is what makes this a verification rather than a
coincidence: the fix was exercised, not avoided.

**Worth noting about the method.** The 600 s offset was chosen to make a reversion obvious, and it worked —
but the defect the test actually found was 22 s, which would have been invisible had the devices been
configured identically. Deliberately mis-configuring the fixture is what made a second, unrelated fault
legible.


### 12.17 Phase 6 — BLE notifications

Originally written as a handoff, because the measurement baseline was perishable and the work had not
started. Completed in the same session; kept in that form because the reasoning that made it a small
change rather than a rewrite is the useful part.

**Goal.** Replace ATT indications with notifications so the transfer stops paying a round trip per chunk.
Target: beat 3.6 KB/s on a log of comparable size (§12.14). Expect 5–15×, not 2× — the current design
loses time to the host's per-chunk write *and* the ATT confirmation, and removing both should allow
several notifications per connection event instead of one chunk per two round trips.

**Read these first.**

- `continueSendingLogData()` in `maintenance_functionality.c` — the whole state machine
- `data_callback()` in `tottag.py` for the receiving half.

**The per-chunk driver, now traced.** `bluetooth.c:203` handles `ATTS_HANDLE_VALUE_CNF` and calls
`continueSendingLogData()` from it. The transfer is therefore paced by the ATT *confirmation*, one chunk
per round trip. There is **no** host write per chunk — an earlier estimate in this document said there was,
and it was wrong; the host writes `DOWNLOAD_LOG` once to start and then only receives.

**Why the change is much smaller than it looks.** In `atts_ind.c:218-228`, Cordio fires the *same*
`ATTS_HANDLE_VALUE_CNF` event for a notification — but immediately after handing the PDU to L2CAP, rather
than on a peer confirmation, and only when `ATT_CCB_STATUS_FLOW_DISABLED` is clear. When buffers are
exhausted the callback is deferred until flow resumes. So switching `AttsHandleValueInd` to
`AttsHandleValueNtf` keeps the existing state machine intact and re-paces it against buffer availability,
which is exactly the pacing this phase needed — no separate WSF flow-control loop has to be written.

Consequences to handle:

- The characteristic must advertise NOTIFY. Check the properties and CCC in `maintenance_service.c`;
  it is presently set up for indications.
- The `repeat` path exists to re-send a chunk after `ATT_ERR_TIMEOUT`, which cannot occur for a
  notification. It becomes dead code rather than a hazard, and per-page retransmission now covers what it
  was protecting against.
- `bleak`'s `start_notify` handles either, selecting on the characteristic's properties, so the host may
  need no change at all — verify rather than assume.

**Where the time actually goes.** The negotiated connection interval is 15-30 ms
(`BLE_MIN/MAX_CONNECTION_INTERVAL_1_25_MS` = 12/24) with `BLE_CONNECTION_SLAVE_LATENCY = 9`. The observed
68 ms per chunk is close to two intervals at the 30 ms end, consistent with send-then-await-confirmation.
Two independent levers therefore exist, and they should be measured separately so the effect of each is
known: removing the round trip (notifications), and shortening the interval or dropping slave latency for
the duration of a transfer.

**Constraints already established.**

- The stream header and experiment details go as *two* transmissions: together they are 255 bytes, over
  the 244-byte ATT payload limit at the negotiated 247-byte MTU (§12.10).
- A repair round sends `details_length = 0` and no details indication (§8.1.1); `sent_details` is
  initialised to `retransmitting` to skip it. Any rewrite must preserve that.
- Pacing must come from WSF buffer availability, structurally like the `tud_cdc_write_available()` loop on
  USB. Notifications are unacknowledged: overrunning the buffer pool drops data silently.
- Check `BLE_CONNECTION_SLAVE_LATENCY = 9`. At 68 ms per chunk, latency may cost as much as the round
  trips do.

**Why this is now safe to attempt.** Retransmission is in place on both transports and verified against
real device bytes (§12.13), so a dropped notification is recoverable rather than fatal. That was the
precondition for giving up delivery confirmation, and it is the reason this phase was ordered last.

**Measure before and after on the same log**, and record throughput rather than elapsed time — the device
keeps logging, so a later download is not the same size.

**Correctness oracle.** Speed is the easy half; the hard half is proving nothing was lost — which a
stopwatch cannot see. The stream's own checks detect a dropped page but cannot say which one, and they
verify only that the device sent what it *claimed* to send. An earlier download is an independent check on
both. Because the log is append-only, any earlier download of a still-present log serves: a fresh
reference can be generated at any time, and nothing about this depends on the firmware that produced it.
The indication-era download is simply the one that already existed:

    ~/Downloads/newtest/reference/AE_1786449600_indications_reference.ttg
    sha256 92c87338928b9ba1e708018ce76a96a68ee57aeb0092a1cc3be80e93a2c00ea9
    520 pages, seqs 0-519, 1,129,251 bytes, zero CRC failures

A log is append-only, so a committed page never changes: every sequence number present in both downloads
must carry byte-identical payload. `software/management/dashboard/compare_downloads.py` checks that, and
distinguishes real loss from the log simply having grown:

    python3 compare_downloads.py <reference.ttg> <new_download.ttg>

It fails on a changed payload, on a page missing from within the candidate's own claimed range, on
truncation, and on any hole or CRC failure that retransmission did not repair. Verified to pass a file
against itself, and to fail on both a single flipped bit and a mid-transfer truncation.

Note that the dashboard names downloads `{label}_{start_time}.ttg`, so a new download of the same
deployment **overwrites the previous file**. The reference above is a copy kept outside that path.

Sequence: measure the indication baseline is already done (3.6 KB/s, §12.14) → switch to notifications →
re-download → run the comparison → only then report a speedup. A faster transfer that drops pages is a
regression, not an improvement.

**Outcome — correctness.** The switch required four coordinated edits, not a rewrite: the characteristic
property (`ATT_PROP_NOTIFY`), the registered CCC entry and the subscription test (both
`ATT_CLIENT_CFG_NOTIFY`), and seven `AttsHandleValueInd` calls. All three CCC points had to move together;
missing any one leaves the host subscribed while `data_requested` stays false, and the transfer would
start and then stall in silence.

The re-download was compared against the indication-era reference: **520 of 520 shared pages
byte-identical, no missing pages, no CRC failures, no retransmission round**, plus 40 new pages from
continued logging. This confirms the WSF buffer pool is deep enough for Cordio's deferred-callback
pacing to hold.

A dropped notification would not have gone *undetected* — the framing checks catch it, since missing bytes
desync the frame boundary and the next page then fails its CRC or overruns the buffer. What the comparison
adds is **attribution**: those checks report that something is wrong, but the sequence numbers read out of
misaligned bytes are meaningless, so the repair round would chase the wrong pages. Comparing against a
known-good download says exactly which pages differ, and does so without relying on the device's own
accounting of what it sent. `bleak` needed no host change; `start_notify` follows the characteristic's properties.

**Outcome — throughput. 9.0x.**

| | indications | notifications |
|---|---|---|
| Transfer | 1,129,251 B in 315 s | 1,130,426 B in 35 s |
| Throughput | 3.58 KB/s (28.7 kbit/s) | **32.30 KB/s (258.4 kbit/s)** |
| Per 244-byte packet | 68.1 ms | 7.6 ms |
| Packets per connection event | ~0.5 | 2 to 4 |

The two payloads differ by about 0.1%, so the elapsed times compare directly. Per-packet time falling from
68 ms to 7.6 ms is the round trip disappearing: the transfer went from one packet per two connection
intervals to several packets within each one, which is the mechanism the Cordio trace predicted.

**Dead code removed with the switch.** The `ATT_ERR_TIMEOUT` branch in the `ATTS_HANDLE_VALUE_CNF`
handler existed to re-send a chunk when the peer failed to confirm an indication. That timeout belongs to
the indication confirmation timer and cannot arise for a notification, so it is gone. The `repeat`
parameter of `continueSendingLogData()` is *not* dead and was kept: it still serves
`BLE_MAINTENANCE_DOWNLOAD_LOG_CONTINUE`, which resumes an interrupted transfer after a reconnect. That is
a different mechanism from per-page retransmission — resume mid-stream versus repair afterwards — and the
two are complementary.

**The interval lever, taken.** The central grants 30 ms with latency 0 for an idle link (it overrides the
requested latency of 9 outright). Four packets of 244 B per 30 ms event is 32.5 KB/s, against 32.3 KB/s
measured — so throughput is capped by packets per connection event, and that cap is buffer-bound rather
than airtime-bound: four packets is about 8 ms of airtime in a 30 ms window. `bluetooth_request_fast_connection()`
therefore asks for 15 ms for the duration of an offload and hands the link back afterwards. The central
accepts 15 ms, which is Apple's floor.

Two things learned in testing it:

- *Only one parameter update may be outstanding.* A second is rejected with `CMD_DISALLOWED`, and on a
  transfer short enough to finish before the first completes, that left the link stuck at the faster
  interval until disconnect. Updates are now serialized, with the completion handler applying whatever is
  wanted at that moment rather than what was wanted when the request went out.
- *The negotiation is not instant.* Measured at roughly 370 ms — the central applies the change at an
  agreed instant several connection events ahead. On a 10 KB log the whole transfer takes about 390 ms, so
  it finishes before the faster interval arrives and the change has no measurable effect. **The benefit
  only appears on transfers long enough to outlast the negotiation**, where 370 ms is a rounding error
  against tens of seconds. It has not yet been measured on a large log; doing so needs a deployment-scale
  log accumulated afresh, since the 520-page reference belongs to a previous epoch.

**Remaining headroom, and why it was left.** The connection interval is still 15-30 ms with slave latency
9 (§12.17). At 2-4 packets per event there is probably another 2x available by shortening the interval or
suspending slave latency for the duration of a transfer. That lever was deliberately not pulled at the
same time, so that the 9x could be attributed to notifications alone rather than to an unattributable
combination. Whether it is worth taking is a power question rather than a throughput one: a 17-hour
deployment log now offloads in 35 seconds, and the interval also governs idle connection cost.


### 12.19 Experiment 13 — the connection-interval lever

**Question.** The central grants 30 ms for an idle link. Does requesting 15 ms for the duration of an
offload deliver the 2x the packets-per-event arithmetic predicts?

**Result — correctness.** Byte-identical: 8 of 8 shared pages match the previous download, 5 new pages
from continued logging, no loss.

**Result — throughput. Not measurable on this log, and the reason is instructive.** The transfer was
10,454 bytes, about 52 notifications, roughly 390 ms end to end. The parameter update took about 370 ms to
take effect — a link-layer change is applied at an instant several connection events ahead, not
immediately — so around 49 of the 52 notifications went out at the old 30 ms interval. The transfer
finished before the faster interval arrived.

This is a property of the measurement, not a fault in the change: the benefit only exists on transfers
long enough to outlast the negotiation. Confirming it needs a deployment-scale log, which must be
accumulated afresh — the 520-page reference belongs to an earlier epoch and cannot be re-downloaded.

**A bug found and fixed.** On the first attempt the restore was rejected with `CMD_DISALLOWED` because the
original request was still outstanding, leaving the link at 15 ms until disconnect. Updates are now
serialized and both were observed applying cleanly (`15.00 ms, latency = 0`, then `30.00 ms, latency = 9`).

**A usability defect found.** The first attempt returned an empty log — `total_pages = 0` — because the
device was ranging and therefore not in maintenance mode, which is where reading is gated. The dashboard's
**Mode Switch** must be sent first; every earlier download had happened with the device on a charger, where
`StorageTask` enters maintenance mode on its own. The failure was silent: an empty download and a device
holding no data were indistinguishable, which is exactly the ambiguity this format exists to remove. The
firmware now warns explicitly when a download is requested with nothing readable.

**Follow-up on a deployment-scale log — 1.95x, as predicted.** A 15.5-hour two-device run produced 464
pages, 1,007,675 bytes, offloaded in 16 s:

| | throughput | vs baseline |
|---|---|---|
| Indications, 30 ms interval | 3.58 KB/s | — |
| Notifications, 30 ms interval | 32.30 KB/s | 9.0x |
| Notifications, 15 ms interval | **62.98 KB/s (503.8 kbit/s)** | **17.6x** |

The interval change alone is **1.95x**, against a prediction of "roughly 2x" derived from packets per
connection event. That the estimate held is worth recording: the model — throughput capped by a
buffer-bound number of packets per event, so halving the interval drains the same buffers twice as often —
appears to be the right one.

Both logs were perfect: contiguous sequence numbers, zero CRC failures, zero holes, zero discontinuities,
and a declared size matching the actual byte count *exactly* on both, the overshoot of §12.14 being absent
because logging had stopped before the transfer. With both devices now on v2, the run also serves as a
v2-against-v2 cross-check: of 110,514 common ranging timestamps, 110,297 agree exactly (99.80%), with a
maximum difference of 27 mm.

**An observation not yet explained.** Successive boots seeded offsets of +251,940 ms and -243,490 ms — a
sign flip of about four minutes each way, which is large for two devices that should be closely synced. It
is causing no harm (the clamp keeps the log monotonic and no discontinuity was reported), but it suggests
the RTC shifts across a reflash. If a download ever returns less than expected after reflashing, a shifted
clock narrowing the requested date window is the first thing to check.


### 12.20 Experiment 14 — the watchdog fires, and the LFRC is 24% slow

**Question.** Does the watchdog interrupt-then-reset sequence work on real hardware, and is the 1/16 Hz
watchdog tick really 16 seconds? The second half is the assumption every number in the §11 parameter table
rests on, and the one a silently truncated register field would have broken invisibly.

**Method.** Phase 5 firmware, `full` target on rev P, pet commented out of `TimeAlignedTask` so the window
runs to completion. Console over SWO.

**Result — the mechanism passes, the timing does not.**

```
ERROR: Watchdog expiring! Last check-in was from startup, 587530 ms ago; reset in ~192 s
...
Reset Reasons: Watch Dog Timer Reset,
```

Everything structural is confirmed: the interrupt fires ahead of the reset, `am_watchdog_isr` overrides the
weak alias and runs, the check-in name is `startup` (proving the pet really was removed rather than the
window merely being long), the reset follows, and `bWDTStat` is set so it arrived through `RSTGEN`.

**But the interrupt fired at 587.5 s where the design says 448 s — 31% long.** Per tick that is
587530 / 28 = **20.98 s against a nominal 16 s**, implying an LFRC at about 780 Hz rather than 1024 Hz,
roughly **24% slow**. And 587.5 s is a *lower* bound: `watchdog_last_checkin_ticks` is sampled in
`system_watchdog_enable()`, which runs before `vTaskStartScheduler()`, and `xTickCount` does not advance
until the scheduler starts — so pre-scheduler boot time is not counted and the true interval is longer
still. The `reset in ~192 s` in that line is the printed nominal constant, not a measurement; the real gap
to the reset was nearer 252 s.

**Which clock is lying — settled from the port source, not inferred.** A 31% discrepancy could in principle
be a fast FreeRTOS tick rather than a slow watchdog clock. It is not:

- The tick does not come from SysTick. `FreeRTOSConfig.h:82,95-97` selects
  `configOVERRIDE_DEFAULT_TICK_CONFIGURATION 1` with `AM_FREERTOS_USE_STIMER_FOR_TICK`,
  `configSTIMER_CLOCK_HZ 32768` and `configSTIMER_CLOCK AM_HAL_STIMER_XTAL_32KHZ` — so **the tick is derived
  from the 32.768 kHz crystal**, the same reference class as the RTC.
- `port.c:1281` sets `ulTimerCountsForOneTick = configSTIMER_CLOCK_HZ / configTICK_RATE_HZ` = 32768 / 100 =
  **327**, truncating 327.68. One tick is therefore 327/32768 = 9.979 ms of real time but is accounted as
  10 ms by `portTICK_PERIOD_MS`, so the tick runs **0.21% fast** — a known, bounded, crystal-referenced
  error, in the wrong direction and three orders of magnitude too small to explain 31%.
- The tickless path carries its sub-tick remainder forward in `g_lastSTimerVal` (`port.c:986-987`) rather
  than discarding it, so the error does not accumulate over the many sleep cycles this test performs.

Correcting the reported figure by that 0.21% gives a real interval of **586.3 s**, so the watchdog tick is
**20.94 s against a nominal 16 s**. The watchdog clock is unambiguously the outlier.

*Nominal is itself ambiguous in the SDK*, which is worth knowing before trusting any derived number:
`am_hal_stimer.h:141` labels the LFRC tap `AM_HAL_STIMER_LFRC_1KHZ` (1000 Hz), while
`am_hal_clkgen.h:135` documents `LFRC / 2 = 512 Hz` (implying 1024 Hz). Against 1024 Hz the measured rate is
**782 Hz**; against 1000 Hz it is **764 Hz**. Either way the part is running **~24% slow**.

**What was ruled out as a firmware cause.** This is silicon behaviour, not a misconfiguration:

- **Nothing writes the LFRC control register.** `grep LFRCCTRL` over the entire apollo4p HAL returns no
  writes, and the register has no trim field to write — `apollo4p.h:1793-1797` gives it exactly two bits,
  `LFRCOUT` (disable output) and `LFRCPWD` (power down). There is no software frequency knob on this part.
- **No clock trim is loaded at init.** `am_hal_pwrctrl.c` contains no trim handling, so whatever calibration
  exists is applied in hardware from factory settings and the LFRC is in its reset state.
- **The clock selection is applied correctly, not truncated.** `WDT_CFG_CLKSEL` is a 3-bit field at [24:26]
  (`apollo4p.h:78770`), and `AM_HAL_WDT_1_16HZ` is 4, so it fits — this is not another instance of the
  hazard #3 masking trap. `apollo4p.h:78776` documents the setting as "1/16th Hz **LFRC** clock", confirming
  the source.
- **The SDK expects the LFRC to need calibrating.** CLKGEN exposes *uncalibrated* LFRC taps as a separate
  family (`AM_HAL_CLKGEN_CLKOUT_ULFRC_*`, "uncal LFRC") alongside the calibrated ones, and provides CLKOUT
  divider taps down to 0.0009 Hz whose evident purpose is measurement. A part whose LFRC needed no
  characterisation would not ship with that apparatus.

**The last open hypothesis — that the rate might not be constant — is now closed.** It was the one that
would have changed the remedy rather than just the numbers: the first two measurements ran with the CPU in
deep sleep for the overwhelming majority of their duration, and RC oscillators are sensitive to supply and
temperature, so a rate that tracked duty cycle would mean no fixed pair of counts could ever be correct.

*Method.* Rather than lower the counts — which would have tripped the `#error` guards, since anything at or
below 50 ticks fails the two-missed-pets check — the **prescaler** was changed from `AM_HAL_WDT_1_16HZ` to
`AM_HAL_WDT_1HZ`. That is 16x faster off the same oscillator, so the counts and every guard stayed exactly
as shipped and each run took about a minute instead of twenty. The pet was left in place: at a ~72 s reset
window against a 300 s pet period the device resets after the first pet regardless, which also makes the
interval measured from a real check-in rather than from arm time before the scheduler starts.

*Result — reboot at 72 s in both sleep modes.* Deep sleep and `AM_HAL_SYSCTRL_SLEEP_NORMAL` gave the same
interval, against the ~56 s that a nominal 1024 Hz LFRC would have produced. **The rate does not depend on
duty cycle**, so a single pair of counts is valid and Option 2 below is not needed.

*And it is a third measurement at a different divider*, which rules out a prescaler-specific fault as well:
72 s / 56 ticks = 1.286 s per tick implies **796 Hz**, against 782 Hz and 795 Hz from the two 1/16 Hz runs.

| run | prescaler | ticks | implied LFRC |
|---|---|---|---|
| 1 | 1/16 Hz | 28 | 782 Hz |
| 2 | 1/16 Hz | 40 | 795 Hz |
| 3 | 1 Hz, deep sleep | 56 | ~796 Hz |
| 4 | 1 Hz, normal sleep | 56 | ~796 Hz |

Four measurements, two prescalers, two sleep states: **~790 Hz, about 23% below the 1024 Hz nominal, with
under 2% spread.** The `WATCHDOG_TICK_MIN_S` guard assumes a part could run at 1365 Hz (1.33x nominal);
every observation here sits at 0.77x, so the guard is comfortably conservative rather than marginal.

*If a permanent per-unit number is ever wanted anyway*, the LFRC can be measured on-device against the
crystal with no extra hardware: clock a free TIMER from `AM_HAL_TIMER_CLOCK_LFRC` (`am_hal_timer.h:84`;
timers 0, 2 and 3 are taken by the buzzer, radio wake-up and BLE scanning, so 1 is free) and gate it against
the RTC, which is on the 32.768 kHz crystal (`rtc.c:82-83`). Do not sample the RTC from the watchdog ISR to
do this: `rtc_get_time_of_day()` keeps a shared `static am_hal_rtc_time_t`, so an ISR that preempts a
task-context RTC read would tear it.

**What the datasheet says — and it is the answer to the whole question.** From
`doc/datasheets/Apollo4-SoC-Datasheet.pdf`:

| Symbol | Parameter | Test Conditions | Min | Typ | Max | Unit |
|---|---|---|---|---|---|---|
| `FLFRC` | LFRC frequency | | **–** | **1024** | **–** | Hz |

**There is no minimum and no maximum.** Ambiq specifies a typical value and nothing else, so there is no
tolerance to be inside or outside of — a 24% deviation violates no published figure, because no figure is
published. §6.3 states the intent in as many words: *"The low power LFRC, with a nominal frequency of
1024 Hz, is used when short term frequency accuracy is **not important**."* The part is behaving as
documented; the design's assumption that 1/16 Hz meant 16 s was the defect.

Two corroborating details from the same document. §11.1 describes the WDT as clocked "by one of four
selectable prescalers of the always active low-power LFRC clock", confirming there is no alternative source
to switch to — every WDT prescaler inherits this tolerance. And the revision history for 0.7.1 (Oct 2020)
records *"CLKGEN: Removed references to digital calibration of XT/LFRC, **auto-calibration of LFRC** and
other functions which have been deprecated"* — LFRC auto-calibration was documented on this family and then
withdrawn, which independently confirms the HAL finding that no calibration knob exists.

**What the errata say.** Both lists were searched (`Apollo4-Errata-List.pdf` and
`Apollo4-Plus-Errata-List.pdf`): **there is no LFRC erratum and no WDT erratum in either.** Nothing suggests
this part is faulty — which, combined with the missing tolerance spec, closes the question. The measured
782 Hz is ordinary silicon.

*The errata search did turn up something worth having, in the opposite direction.* Both lists name the
watchdog as the prescribed recovery for silicon-level CPU hangs, which is independent justification for
Phase 5 that §11 does not cite:

- **ERR107** (base Apollo4) — an RTC clock-domain-crossing fault causes an **APB bus hang**, observed in
  "about 20% of devices tested", requiring "some type of reset, e.g., watchdog reset, to recover". *Not
  applicable here:* it is marked "fixed in Apollo4 Plus", and every board revision in this tree builds
  against `AM_PART_APOLLO4P` (`Makefile:12-14`, unconditional on `BOARD_REV`). Worth noting anyway that the
  firmware already carries its other mitigation — the `RTCSTAT` read at `rtc.c:143`, comment and all.
- **ERR111** (Apollo4 Plus, all revisions) — an MSPI/AXI b-response deadlock causes a CPU hang that is
  "recoverable with the use of the Watchdog Timer". *Not applicable here:* it requires MSPI traffic, and this
  firmware uses none — the log flash is on the IOM (`am_hal_iom_*`), and `grep -rl MSPI src/` is empty.

Neither bites today, but both confirm that a silent CPU hang with no software cause is a documented
possibility on this family and that the vendor's answer to it is exactly the mechanism Phase 5 adds.

**Interpretation — the error is in the safe direction, and that is the problem.** A slow LFRC makes both
windows *longer*: interrupt at ~588 s, reset at ~840 s instead of 448 s and 640 s. That is more margin
against a spurious reset and only costs detection latency, ~14 minutes instead of ~10.7. Against six days of
lost data that remains obviously the right trade, so **nothing is broken on this part.**

The concern is the opposite direction on a *different* part or at a different temperature. If an LFRC can be
24% slow it can plausibly run fast, and the windows scale with it:

| LFRC | tick | reset window (40 ticks) | margin over the 300 s pet |
|---|---|---|---|
| 780 Hz (measured, this part) | 21.0 s | 840 s | 2.8x |
| 1024 Hz (nominal, assumed) | 16.0 s | 640 s | 2.1x |
| 1270 Hz (as fast as this part is slow) | 12.2 s | **488 s** | **1.6x** |

At 488 s, **two consecutive missed pets — 600 s — would reset the device.** The third `#error` guard in
`app_config.h` asserts the reset window exceeds `2 * BATTERY_CHECK_INTERVAL_S`, but it does so against the
*nominal* 16 s tick, which is now known not to be the real number. The guard is checking a figure the
hardware does not honour.

**Four follow-up questions on the clock tree, answered from the register set and the HAL.** Each of these
was a candidate for "the LFRC is fine and something else is wrong". None of them survives, but the checking
narrowed the problem usefully and one of them produced a permanent change.

*1. Does the WDT clock configuration explicitly target the LFRC, rather than defaulting to a 32.768 kHz
crystal expectation?* **It targets the LFRC, and there is no crystal expectation available to default to.**
`WDT_CFG_CLKSEL` is a 3-bit field enumerating exactly five values — `OFF`, `128HZ`, `16HZ`, `1HZ`, `1_16HZ`
— and every one of them is documented as an "LFRC clock" (`apollo4p.h:78771-78777`). The watchdog has no
crystal input at all, so no mis-selection is possible in either direction. The one nearby mux that *does*
choose between crystal and LFRC is `CLKGEN_OCTRL_OSEL`, and it is explicitly the **RTC's** source select
(`RTC_XT` / `RTC_LFRC`, `apollo4p.h:48589-48592`); `rtc_init()` sets it to XT (`rtc.c:82-83`) and it has no
bearing on the watchdog. **This is now self-verifying rather than argued:** `system_watchdog_enable()` reads
`WDT->CFG` back after configuring and prints the raw word plus decoded `clksel`/`intval`/`resval`. Since
`am_hal_wdt_config()` masks rather than validates (hazard #3), a register readback is the only place the
truth is visible, and it would catch a silent truncation as well as a wrong clock.

*2. Is the LFRC explicitly kept alive, or can power management gate the low-frequency clock tree?* **It
cannot be gated by any state this firmware enters.** This was worth checking because gating would make the
watchdog run slow or stall — the direction actually observed. Three independent confirmations: the datasheet
says the LFRC "is always enabled" (§6.3) and describes the WDT's source as "the always active low-power LFRC
clock" (§11.1); in SDS3, the deepest documented state and far deeper than anything FreeRTOS enters, "LFRC is
on (HFRC and XTAL are off)" (§3.7.2.4.7); and in software, nothing anywhere writes the only two bits that
could do it — `CLKGEN->LFRCCTRL` holds just `LFRCOUT` (disable output) and `LFRCPWD` (power down), and a grep
across the whole HAL and `src/` finds no writes to either. `am_hal_pwrctrl_low_power_init()` does write
`CLKGEN->MISC`, but every gate it sets is high-frequency — CM4 DAXI, GFX, GFX AXI, APB DMA CPU, ETM TRACE,
HFRC_FUNC (`am_hal_pwrctrl.c:2207-2213`). The low-frequency tree is untouched.

*3. Does the Ambiq port's tickless mode have any effect?* **Not logically, and not on the measurement.** The
reference is unaffected because the tick is crystal-derived independently of sleep state. The watchdog clock
is unaffected because `am_hal_sysctrl_sleep()` contains no reference to `LFRC`, `CLKGEN`, `OCTRL` or `XTAL`
at all — entering deep sleep does not reconfigure clocks, it sets the sleep-deep bit and manages SIMOBUCK
state. **The one channel that remains open is physical rather than logical**, and it is worth stating
precisely because it is the only surviving mechanism for a duty-cycle-dependent rate: §6.3 notes the LFRC
"also supplies clocks for SIMO buck regulator in low power mode (32 kHz)", and `am_hal_sysctrl_sleep()`
branches on `PWRCTRL->VRSTATUS_b.SIMOBUCKST`. An RC oscillator's frequency depends on its supply, and the
buck's operating point differs between sleep and active. Nothing in the source can settle whether that moves
the rate by a measurable amount. **Settled empirically instead: the sleep-mode comparison above found the
same 72 s interval in deep sleep and in normal sleep, so this channel is not active on this part.**

*4. If all else fails, is there a more accurate clock guaranteed to be running?* **For the hardware watchdog,
no — and for the system, yes, but taking it would give up the thing that makes a watchdog worth having.**
The WDT cannot be pointed anywhere else; see question 1. Elsewhere the crystal is not merely available but
already running: the XT is enabled precisely when a module uses it (§6.4), and here both the RTC and the
FreeRTOS STIMER tick use it, and RTC/STIMER counters keep running from the 32 kHz XTAL through the sleep
states in use. A TIMER can be clocked from it directly (`AM_HAL_TIMER_CLOCK_XT_DIV2` … `_DIV128`).

But anything built on a TIMER or an RTC alarm is a **software** watchdog: it needs an ISR to run in order to
do anything. The hardware WDT's defining property is that `RSTGEN` resets the part with no CPU involvement
whatsoever, so it still fires when the CPU is wedged with interrupts masked, in a hard fault, or spinning in
`am_hal_delay_us()` at priority 5 — which is precisely the hang §11 was written to catch, and precisely the
silicon hang ERR107 and ERR111 describe. A crystal-based timer would be accurate and useless in exactly the
cases that matter. **So the crystal's only real use here is to calibrate the LFRC rather than replace it**,
which is Option 2 below and is now its sole remaining justification.

There are two ways to respond, and they differ in kind rather than degree.

**Option 1 — absorb the tolerance with margin.** Raise `WATCHDOG_RESET_TICKS` to 56 and
`WATCHDOG_INTERRUPT_TICKS` to 40, so the window survives an LFRC as fast as this one is slow: 672 s at
12.2 s/tick (still clear of 600 s), 896 s nominal, ~1176 s ≈ 20 min on this part. Both stay inside the 255
tick ceiling and all three guards still pass. Two lines, no new code paths. It does not make the window
*known* — it makes it wide enough that not knowing is survivable, and it pays for that in detection latency
on the one axis where this design has always had slack.

**Option 2 — measure the LFRC at boot and derive the counts.** Using the TIMER-against-RTC method described
above, compute the actual tick period once at startup and pick `ui32ResetValue` to hit a *wall-clock* target
(say 700 s) instead of a nominal one. This converts an unbounded ±25% silicon tolerance into a window that is
correct on every individual unit, and the measured frequency is a useful per-unit QA datum in its own right —
an LFRC well outside spec becomes visible at manufacturing rather than as a mystery reboot in the field.
Costs ~40 lines, a boot-time measurement gate (~1–10 s depending on the resolution wanted), and it makes the
watchdog configuration data-dependent, which is a real downside for something whose whole job is to be
dependable. The `#error` guards would also have to become runtime checks, losing the compile-time property
that hazard #3 argued for.

**Option 1 applied, and Option 2 is now closed rather than deferred.** `WATCHDOG_INTERRUPT_TICKS` 28 →
**40**, `WATCHDOG_RESET_TICKS` 40 → **56**. The sleep-mode comparison above showed the rate is constant, so
margin is the proportionate answer and a boot-time calibration would be machinery in the path of a safety
net for no gain. Option 2 stays documented only as the thing to reach for if a future part turns out to
drift with workload, which this one does not.

**The guard was strengthened at the same time, and this is the durable part of the fix.** A new
`WATCHDOG_TICK_MIN_S` (12 s — the nominal less 25%, an LFRC as fast as this part is slow) replaces the
nominal tick in the two-missed-pets check:

```c
#if ((WATCHDOG_RESET_TICKS * WATCHDOG_TICK_MIN_S) <= (2 * BATTERY_CHECK_INTERVAL_S))
#error "The watchdog reset window must tolerate two missed pets even with the LFRC at its fastest plausible rate"
#endif
```

The check now runs against the direction that actually reboots a deployment rather than against a figure the
datasheet declines to guarantee. **Verified by restoring the original 28/40 counts: they pass at the nominal
tick and fail here.** So the configuration that shipped into this experiment is one the build would now
reject — which is what the guard is for, and what it could not do while it trusted 16 s.

New windows: 640 s / 896 s nominal, ~840 s / ~1176 s on the measured part, and 480 s / 672 s if an LFRC runs
25% fast — still clear of the 600 s that two missed pets would need. Detection latency on this part becomes
~20 min, against a bound of six days of lost data.

Whichever counts are chosen, the overnight test in §12.16 is now more important than it looked, because it
is the only item that exercises the pet against the real LFRC rate over many cycles.


### 12.21 Reset-reason logging — making a reboot visible without a console

**Why it stopped being optional.** §11 accepted that a production build leaves no trace of a watchdog reset:
`print()` and the boot banner are compiled out, so the only evidence was on a console. That was tolerable
until the overnight two-device test (§12.16), where only one debugger connection is available — with no way
to read the second device's console, an unexplained gap in its log could not be told apart from a watchdog
reboot, which is precisely the thing the test exists to detect.

**Device side.** A new `STORAGE_TYPE_RESET_REASON` (= 7) carrying a `uint16` of the raw
`am_hal_reset_status_e` bits latched by RSTGEN. Written **once per boot** from `StorageTask`, not at reset
time, because the reason only becomes loggable after storage is up and the time base has been recovered —
which is the same prologue that already seeds the ranging offset. `setup_hardware()` captures the status
into a static as it reads it for the banner (`system.c`), and `system_get_reset_reason()` hands it over.

Two deliberate choices. **Every boot, not just notable ones:** the record doubles as an explicit marker for
every reset seam, which was previously inferred from a timestamp gap — useful well beyond the watchdog.
**Raw bits, not a decoded code:** several causes can be latched at once, so decoding on the device would
have to pick a winner; the host renders the full set instead.

*Known limitation:* a device stuck in a genuine boot loop may reset before the page is flushed, so the
absence of a record is not proof that no reset occurred. Presence is proof; absence is not.

**Host side** (`software/management/dashboard/tottag_format.py`). Records carry no length field — the host
derives each record's size from its type — so **a new type is a wire-format change that the parser must
learn in the same commit**, or every record after the first reset record desynchronizes. Three coordinated
edits: the type constant with `STORAGE_NUM_TYPES` bumped 7 → 8 (it bounds the validator's range check), a
`length = 7` arm in `_record_length()` so a rejected record can still be stepped over, and a decoder that
emits `'rst'` as a list of cause strings. The key is `'rst'` rather than `'r'`, which ranges already uses.

*Verified by round-trip against the real parser*, six checks: a watchdog record decodes to `['Watchdog']`;
**records either side of it still decode, proving no framing desync**; simultaneous causes decode as a list;
a zero status word is kept rather than dropped, since RSTGEN latching nothing is legitimate; an undefined
status word is stepped over and the following record still parses; and the type sits inside the validator's
range. All six pass.

**Two consumers not updated at the time, one since resolved.** `software/analysis/tottag.py` is the legacy
v1-only path (§9.3) and never sees a v2 record. `software/managementweb/packages/tottag-schema` snapshotted
`STORAGE_NUM_TYPES: 7` and went stale, and its `npm run drift:update` generator was itself broken — first by
`cannot resolve identifier 'sizeof'` in `MEMORY_NUM_DATA_BYTES_PER_PAGE`, and then, once the `nandlog`
extraction removed the whole `MEMORY_*` family and the EVB board revision, by
`revision REVISION_APOLLO4_EVB not found in boards/revisions.h`. **Both are fixed and the package is
current**; see §16, which also records what it now does with the reset and anchor records.


### 12.22 Experiment 15 — the overnight run, and a watchdog result the log cannot explain

**Method.** Four devices (02, 3E, AE, F3), ranging, ~18 hours overnight, downloaded the following morning.
Pass criterion from §12.16: no `'rst'` record containing `Watchdog` on any device.

**Result — FAILED. AE took three watchdog resets.**

| device | records | span | reset records |
|---|---|---|---|
| 02 | 34,045 | 18.02 h | 2 x SW Power-On |
| 3E | 20,746 | 17.94 h | 2 x SW Power-On |
| **AE** | 29,767 | 18.41 h | **SW Power-On, 3 x Watchdog, SW Power-On** |
| F3 | 34,768 | 17.97 h | 2 x SW Power-On |

AE's watchdog resets were at 20:28:47, 02:48:40 and 07:55:00 — spaced 3 h 44, 6 h 20 and 5 h 06 apart, so
not a periodic effect.

**§12.21 earned its place on its first outing.** AE is the device that produced **no console output at all**
during download — no discontinuity warnings, nothing anomalous. By every signal that existed before this
run, AE was the cleanest of the four. Without the reset-reason record the conclusion would have been "four
devices, no problems", and the one device that reset three times would have been the one that looked best.

**What the data establishes.**

- *Not a starved pet in the obvious sense.* Battery voltage records are written from the same
  `TimeAlignedTask` loop body that pets, so their spacing is a direct proxy for pet spacing. It is a
  metronome — 299.0 to 299.5 s against a nominal 300 — all night, on both sides of every reset, no gap.
- *Not a brownout.* AE ran 4215 → 3921 mV and never approached `BATTERY_NOMINAL`, let alone
  `BATTERY_CRITICAL`. It had the second-healthiest battery of the four.
- *Not correlated with application state.* All three resets happened while AE was BLE-scanning rather than
  ranging, which looked like a lead until it was quantified: AE spent 75% of the night in that state, so
  three of three landing there has p ≈ 0.43. **Rejected as coincidence rather than reported as a pattern.**

**What the data cannot establish, and why that is the important part.** The question that matters is whether
the watchdog caught a genuine hang — in which case it did exactly its job, and AE was wedging three times a
night — or fired spuriously on a live device. **The log cannot distinguish these, by construction.**

On every boot `StorageTask` re-seeds the time offset from the newest timestamp recovered from flash, so
experiment-time is forced to continue from where the log left off. Any real time between the last flushed
record and the reboot is therefore erased. Both stories produce indistinguishable logs:

- *Genuine hang:* device wedges shortly after its last flush, the watchdog fires ~19.3 min later, the reboot
  seeds the clock back to the last flushed record. The 19 minutes vanish.
- *Spurious reset:* device resets moments after its last flush, the reboot seeds to the same place. Nothing
  vanishes because nothing was lost.

The apparent 40–118 s between the last pet and each reset record is a seeding artifact, not a measurement.
Comparing final timestamps across devices does not resolve it either: AE finished within 11 s of 3E and F3,
which would seem to rule out ~57 min of erased time — except that AE resumed ranging shortly before download,
and `storage_write_ranging_data()` overwrites the offset with the network's, silently re-correcting any
accumulated lag as a forward jump that the discontinuity check does not flag.

**The one change that would settle it: log the raw RTC.** The RTC is on the 32.768 kHz crystal and keeps
running across a reset, so it is the only clock in the system the seeding does not touch. Pairing it with an
experiment timestamp makes the erased interval directly computable, as the difference between the two.

**✅ Done — as `STORAGE_TYPE_TIME_ANCHOR` (§12.24), and as a standalone record rather than a field on the
reset record**, because a periodic anchor answers this question *and* measures per-device clock drift *and*
makes an offset re-basing visible, where a field on the reset record would only have answered this one.
§12.26 measured it working: 279,080 ms of log time against 279 s of RTC time across a reboot.

**So this section's question is answerable on the next run, and partly answered already:** §12.23's
cross-log arithmetic put AE's resets at genuine ~17-minute hangs by deriving the lag from a *second device's*
log. The anchor removes the need for that indirection — the device that hung now records the evidence
itself.

**Unrelated finding — the other three devices' discontinuities are not watchdog-related.** They are the
network-time-base phenomenon left open in §12.19, and this run adds two details. 3E and 02 jump backwards at
*the same absolute experiment time* (36,915,000 ms, by 79.5 s and 73.0 s), which makes it a network-wide
event rather than a per-device fault. And 02's second jump (1036 s at page 138) corresponds to no reset
record at all, confirming these come from `app_set_time_offset()` during ranging rather than from reboot
seeding. F3's five jumps are all exactly 0.5 s — one timestamp quantum — and are the `time_moved_backwards`
page commit in `storage_store_record()` behaving as designed.

*Also unexplained, and smaller:* AE's first reset (16:44:53, SW Power-On, 23 min after boot) has no obvious
cause. `system_reset(false)` from `verify_app_configuration()` failing to bring BLE up produces exactly that
signature and is the first thing to check if it recurs.


### 12.23 Experiment 16 — the backward time jumps: cause, blast radius, and why CRC would not help

**Question.** The overnight download reported backward page boundaries on three of four devices. Are these
transient corruption — a bit flip in a schedule packet, which error-checking would fix — and how much data do
they actually damage?

**Method.** `parse_v2()` keys records by timestamp and sorts, which destroys exactly what this needs: the
physical write order, and any records whose timestamps collide. A separate order-preserving walker was
written over the same page frames. It reproduces the download tool's reported discontinuities exactly, and
recovers **39,779 records from 02 against `parse_v2`'s 34,045**. Plots of voltage and pairwise ranges
against *both* axes are in `newtest/plots/`.

**Finding 1 — not a bit flip. The clock does not snap back; it steps and stays.** After each jump, time
continues forward from the *new* base and takes exactly the jump's own duration to regain the pre-jump
value: 02's 73 s jump recovers 153 records later over 73.0 s of log time, 3E's 79.5 s over 79.5 s, and 02's
1036 s over 1037.0 s. A corrupted timestamp would be one bad value followed by immediate recovery. This is a
persistent re-basing. **A CRC or sanity check on the schedule packet would not prevent any of it.**

**Finding 2 — the mechanism is the network join, working as designed.** Every jump lands on the first
`RANGES` record after a stretch with no ranging at all (`partners before: {}` in all three cases), and lands
on a page boundary because `storage_store_record()`'s `time_moved_backwards` check commits the page when it
sees one. `scheduler.c:88` is the cause: a participant sets
`offset = data_timestamp − app_get_experiment_time(0)`, adopting the master's clock wholesale, and the
logged timestamp *is* the master's broadcast time. The jump is the instant a device stops keeping its own
time and starts keeping the network's. It is not an error condition; it is the join.

**Finding 3 — the network time base is otherwise excellent.** A range between two devices is one physical
measurement logged at both ends, so agreement between the two logs is a direct test of shared time:

| link | ms-stamps in common | range values agreeing within 100 mm |
|---|---|---|
| 02↔3E | 99.8% | 99.9% |
| 02↔F3 | 99.8% | 99.4% |
| 3E↔F3 | 100.0% | 100.0% |
| AE↔F3 | 99.9% | 100.0% |

The devices agree to the millisecond for essentially the whole log. The jumps are a join transient in an
otherwise tightly synchronised network — the strongest argument that the base mechanism is sound and only
its edge case needs attention.

**Finding 4 — the blast radius is small, and it is not the ranging data.**

| device | jump | window as % of log | records re-written | records actually lost |
|---|---|---|---|---|
| 02 | 73.0 s | 0.11% | 155 | **0** |
| 3E | 79.5 s | 0.12% | 167 | **0** |
| 02 | 1036.0 s | 1.60% | 307 | **82, all BLE-scan** |

"Lost" means an exact `(timestamp, type)` collision, where a timestamp-keyed consumer overwrites one record
with another. **No ranging, voltage, motion or IMU record was lost to any jump.** In the `.ttg` itself
nothing is lost at all — write order preserves every record; the loss is introduced downstream by keying on
time.

**The 3E/02 "simultaneous" jump is a coincidence of a shared clock, not a shared fault.** All four devices
carry the same `experiment_start_time` (1786622400) and their RTCs agree, so a device that has not yet joined
runs a clock derived straight from its own RTC — and two such devices necessarily agree. That is why both
logs show their last pre-jump record at exactly 36,915,000 ms. They then joined 6.5 s apart and each snapped
to whatever the master's clock read at *its own* join instant, which is exactly the 6.5 s between their
landing points (36,842,000 vs 36,835,500). **Different targets are the expected result of joining at
different moments, not evidence of a corrupted value.**

**Finding 5 — the root cause is stale seeding, and it is a real bug.** RTC drift cannot explain a 75 s step:
all four devices were programmed together and verified equal to 1–2 s, and the logs corroborate it — at the
one instant where two devices were both still on their own clocks, 02 and 3E agreed to inside the 500 ms
quantum. The step is ~100x larger than any plausible drift, so the clocks are not the problem.

The problem is what `StorageTask` seeds from. On boot it sets
`app_set_time_offset(seed − app_get_experiment_time(0))`, where `seed` is the newest timestamp **recovered
from flash**. But up to `STORAGE_FLUSH_TIMEOUT_S` (120 s) of records sit unflushed in the RAM page cache at
any moment and are lost on reboot, so **the seed is systematically stale, and the device's clock is set that
far behind real time.** The observed 73 s and 79.5 s sit squarely inside that 0–120 s window. If the device
then becomes master, the whole network inherits the lag, and every reboot adds another helping.

Stated plainly: *seeding treats "the last record that reached flash" as "now", and it is not.*

**Finding 6 — this explains the 1036 s jump, and in doing so resolves §12.22.** 02's second jump is at
21:21, and its partner set changes across it: it was ranging with F3 until 20:57:38, went quiet, and
re-joined at 21:03:57 (its new clock) against **AE**. F3 — highest UID, so the default master — was absent
from ranging for 105 min ending 20:57, which left AE as master. And AE watchdog-reset at 20:28:47, in the
middle of that absence.

So 02 adopted the clock of a device that had just been through a watchdog reset. Predicting the lag from
AE's own log: its last pet was at 20:27:02 and the reset window is 56 x 20.69 s = 1159 s, so the watchdog
fired at ~20:46:21; the reboot then seeded from AE's last flushed record at 20:28:40, leaving AE's clock
**1054 s behind**. The jump 02 actually recorded was **1036 s** — a 1.7% match on a figure derived
independently from AE's log.

**That arithmetic is decidable evidence about §12.22.** A spurious reset would have cost AE only the flush
staleness — seconds to ~2 minutes — and 02 would have stepped back by about that much. A step of 1036 s can
only come from AE having genuinely lost ~17 minutes of real time. **AE's watchdog resets caught real hangs;
the watchdog was doing its job.** This is inference rather than direct measurement, and the time anchor
below makes the same question answerable outright next time, but it is much stronger than anything available
from the timestamps alone.

**Recommendation — an audit record, not a CRC.** The requested error-checking would guard a mechanism that
is not failing. What is missing is *visibility*: `app_set_time_offset()` can move the entire log's time base
and leaves no trace, so the host can only infer it from a backward page boundary — and a *forward* re-basing
is completely invisible. Two cheap changes, in priority order:

1. **Log the raw RTC timestamp** — in the reset record at minimum, ideally periodically. The RTC is on the
   crystal and is the one clock seeding never touches, so it makes both the erased-time question here and
   the hang-versus-spurious question in §12.22 answerable by arithmetic. **One change closes both.**
2. **Emit a record whenever the offset moves by more than a threshold**, carrying the old and new values.
   That turns a silent re-basing into something the host can report and a date-limited query can account for.

**Finding 7 — the record loss is not clock drift and not the device's fault.** Every logged timestamp is
rounded with `500 * (t / 500)`, and the ranging round interval is `SCHEDULING_INTERVAL_US` = 500 ms. **The
quantum and the period are the same number**, so any jitter that pushes two consecutive rounds into one
bucket produces a collision. Measured on F3: 19,891 consecutive range gaps of exactly 500 ms, and **31 of
exactly 0 ms** — those 31 are two rounds sharing a timestamp.

And the colliding records are genuinely different measurements, not duplicates:

| device | range collisions | identical (harmless) | **different (real loss)** |
|---|---|---|---|
| 02 | 6 | 0 | **6** |
| 3E | 31 | 1 | **30** |
| AE | 28 | 0 | **28** |
| F3 | 31 | 2 | **29** |

e.g. at ms 38,575,000 device 02 holds `{F3: 189, AE: 253, 3E: 229}` and then `{F3: 195, AE: 253, 3E: 224}` —
a real second round, discarded.

**The device stores both.** Write order preserves every record; nothing is lost in the `.ttg`. The loss is
introduced entirely by the host, in `_finalize()`'s `log_data[timestamp]` dict, where the later record
overwrites the earlier. It is also not caused by the jumps: AE has zero backward steps and still loses 196
records this way, and of 02's 354 only 82 come from its jump.

*Two independent fixes, and only one of them is needed for correctness:*

1. **Host (required).** Stop keying by timestamp. The dict conflates "records at the same instant" with
   "one record", which is right for joining a voltage to a motion reading and wrong for two ranging rounds.
   Keying by `(timestamp, type)` with a list value, or simply returning records in write order, loses
   nothing and needs no firmware change — the data is already in every archived `.ttg`.
2. **Device (optional).** The 500 ms rounding predates this work and is presumably deliberate for
   grid-alignment. It could be dropped to ms resolution, which would remove the collisions at the source,
   but it changes the on-flash meaning of every timestamp and is not worth doing for a 0.03–0.16% effect if
   the host is fixed.


### 12.24 The time anchor — making the clock recoverable

Implemented in response to §12.22 and §12.23, both of which came down to the same missing fact: every
timestamp in the log is RTC + network offset, the offset can be re-based wholesale by one ranging round, and
nothing recorded what the offset was. A log therefore could not say what time it really was.

**`STORAGE_TYPE_TIME_ANCHOR` (= 8).** Payload is the raw `rtc_get_timestamp()` — 9 bytes with the header.
**The offset is deliberately not stored, because it is derivable:** every other record's timestamp is
RTC + offset, and this one pins the RTC, so the host computes
`lag = (rtc − experiment_start_time) − experiment_ms/1000` and reports it directly. Storing the offset as
well would add a field that could disagree with the arithmetic.

Emitted in three places, each answering a different question:

- **At boot**, immediately after the reset record — reveals how stale the seed was, which is the §12.23
  Finding 5 bug, and how much real time a reset consumed, which is the §12.22 question.
- **Once per `TimeAlignedTask` loop** (300 s) — a standing real-time reference through the whole log, so
  per-device RTC drift becomes directly measurable instead of being inferred. 9 bytes per 300 s is 0.03 B/s
  against a measured ~42 B/s.
- **Whenever the offset moves by more than `TIME_BASE_CHANGE_THRESHOLD_MS`** (2 s), written *before* the new
  offset is adopted so the record carries the last timestamp on the old base. This is the audit record: it
  makes a re-basing an explicit event rather than something inferred from a backward page boundary — and it
  catches **forward** re-basings, which are completely invisible today because only backward steps are
  flagged.

*Verified* by six round-trip checks against the real parser: lag arithmetic, no framing desync of the
records either side, structural length known when content is rejected, an implausible RTC stepped over
cleanly, coexistence with the reset record, and the type inside the validator's range. All four existing
`.ttg` files still parse to byte-identical record counts.

**Offset recovery, which is what the anchor was really for.** `StorageTask` now calls
`storage_recover_time_anchor()` and computes
`offset = anchor_experiment_ms − (anchor_rtc − experiment_start_time) * 1000`. The distinction that matters:
the old path recovered a **timestamp** and forced the clock to read it, which is only correct if the log ends
at the present moment; the new path recovers an **offset**, and an offset does not decay — reading it back
after 120 s of unflushed cache, or after a 19-minute hang, yields the same value as reading it immediately.
Staleness stops contaminating the clock, and stops compounding across reboots.

**The old scheme is gone, not kept as a fallback.** New firmware never appends to a log written before
anchors existed — a flash is a clean break — so a fallback would only be dead code that could silently
reintroduce the bug. `storage_recover_last_ranging_timestamp()` and its `last_ranging_timestamp_in_page()`
helper are deleted along with it. No anchor now means an empty log or a fresh epoch, where there is no
network base to recover and the offset correctly stays at zero.

*The storage test followed the implementation* — `test_ranging_timestamp_recovery` is replaced by
`test_time_anchor_recovery`, covering an empty log, a log holding no anchors (which must report nothing
rather than fall back to some other record), anchors spread across pages with the newest winning, trailing
records not displacing the answer, and — the property the whole design rests on — **two reads separated by
more writes returning the same anchor, so staleness cannot shift the recovered offset.**

*Expected effect on the §12.23 jumps:* a device that never introduces lag never becomes a lagged master, so
a joining device never inherits one. With RTCs agreeing to 1–2 s the join step should fall from 73/79.5/1036 s
to under the 2 s anchor threshold — and any residual is now reported rather than inferred.

**Millisecond timestamps (§12.23 Finding 7).** The `500 * (t / 500)` snap is gone from all ten write sites.
Resolution is now bounded by the RTC itself, which is `1000 * seconds + 10 * hundredths` — **10 ms, not 1 ms**
— so 50x finer than before. Two consequences worth stating precisely:

- **Ranging collisions are eliminated.** Rounds are 500 ms apart, so a collision would need two within 10 ms.
  The old failure needed only an interval 1 ms under 500, which is why there were 31 of them on F3 in a night.
- **Cross-device timestamps still match exactly.** This was the thing to be careful about, and it is safe:
  `schedule_phase_get_timestamp()` returns `schedule_packet.experiment_time_ms`, the identical integer broadcast
  by the master, so every device in a round stamps a ranging record with the same value at any resolution.
  Only per-device records (voltage, motion, IMU, BLE) use each device's own clock, and those never needed to
  align across devices.
- *It does not eliminate every collision.* Motion and BLE-scan records are event-driven and two can still
  land in the same 10 ms. Expect roughly a 50x reduction, not zero, so a host that assumes timestamp
  uniqueness is still making an assumption the format does not guarantee.

**One hard dependency, and a latent bug found on the way.** `tottag_format.py` gated every record on
`(timestamp_raw % 500) == 0`, which would have rejected **every record** written by the new firmware. That
check is a v1 resynchronisation heuristic — v1 has no framing, so a byte-at-a-time scan needs the test; v2 is
record-aligned and CRC-covered and does not. It is now conditional on `resynchronize`, so v1 keeps it and v2
does not, which reads both old and new logs.

Separately, the device's own `stored_record_length()` did not know types 7 or 8 and returned 0 for them,
which **breaks out of the boot scan** at the first reset or anchor record. That was already latent from the
reset record in §12.21 — it degraded seeding rather than corrupting anything — but anchors every 300 s would
have made it hit constantly. Both types are now in the table.

**A misleading name, corrected.** `schedule_packet.epoch_time_unix` is assigned from
`app_get_experiment_time()`, which returns **milliseconds** — the field never held a Unix second timestamp
despite its name, which is why sub-second ranging timestamps were possible all along. The logs confirm it:
36,915,000 for an experiment 10.25 h old, with consecutive range records exactly 500 apart. Renamed to
`experiment_time_ms` with a comment. The struct is packed and sent over the air, but a field *name* is not
on the wire, so this is a source-only change with no wire-format impact.

*Verified:* six further round-trip checks — off-grid timestamps parse under v2, two ranging rounds 10 ms
apart no longer collide, v1 still rejects off-grid and still accepts on-grid, the offset-recovery arithmetic
is exact at 0 s / 120 s / 1159 s of staleness, and the host lag decode is unchanged. All four existing `.ttg`
files still parse to identical record counts. Clean on rev P and rev M, and the storage test target builds.


### 12.25 Console output — separating the expected from the alarming

The download console is read by whoever is offloading data, who is often not the person who wrote the
firmware. Every line it prints is therefore a claim that something needs attention, and a line that fires
during normal operation trains people to ignore all of them.

**The case that prompted this.** The first time a device joins a ranging network it adopts the master's
clock, so any residual difference between two devices' RTCs shows up as a small backward page boundary. That
is normal operation, and F3's five 0.5 s steps — one timestamp quantum each — were reported under a
`WARNING` heading alongside genuine 73 s and 1036 s re-basings, with nothing distinguishing them.

**What changed.** `time_discontinuities` are now split at `BENIGN_TIME_STEP_MS` (2000 ms), which mirrors the
firmware's own `TIME_BASE_CHANGE_THRESHOLD_MS` — the two thresholds mean the same thing and should not
drift apart. Steps below it print as `INFO: … expected clock adjustment(s)` and **only under `--debug`**;
steps at or above it keep the `WARNING` and the note about date-limited downloads. Against the overnight
logs this silences F3 entirely and leaves 02's and 3E's genuine re-basings reported.

Retransmission recovery moved behind the same flag. `Recovered N page(s) by retransmission` describes the
system working exactly as designed; it is reassuring to an engineer and unsettling to everyone else.

**Deliberately still loud**, because each one means real data is missing or untrustworthy: unreadable pages,
CRC failures that survived retransmission, truncated transfers, pages that stopped decoding early, and
backward steps large enough to make a time-bounded selection wrong.

**Two candidates for the same treatment, not yet acted on:**

- **`rejected_records`** — structurally valid records whose contents failed a plausibility check. A handful
  per log is normal; it is currently collected but never printed, which is the right default. If it is ever
  surfaced it belongs behind `--debug`.
- **Reset records (§12.21)** — now that every boot writes one, a routine `SW Power-On` appears at the start
  and end of every log. If a tool ever reports reset causes, those two are expected and only `Watchdog` or a
  brownout deserves prominence. Worth deciding before the record gets surfaced rather than after.


### 12.26 Experiment 17 — pre-flight for the second overnight run

**Why a pre-flight at all.** The first overnight ran on firmware with none of §12.24's changes, and the
change set since touches the write path, the boot path and the host parser at once. One failure mode in
particular would have produced an 18-hour log that parsed to *zero* records: the host's
`(timestamp_raw % 500) == 0` gate rejecting every record written without the snap. That is worth 30 minutes
to rule out.

**Test A — `make storage BOARD_REV=P`.** All suites pass, including the rewritten
`test_time_anchor_recovery`: newest anchor recovered across pages (159,500 ms / rtc 1,700,000,119), trailing
records leaving it unchanged, and **a later read returning the same anchor** — the staleness-invariance the
whole design rests on. Block-crossing (200/200 pages), partial-page flush, time-range seek, timestamp-jump
page framing, retransmission and experiment-details round-trip all pass with 0 errors.

*Coverage note:* Test A deliberately does **not** cover millisecond timestamps. The 500 ms snap lived in the
`storage_write_*` queue helpers, and the storage test calls `storage_store_record()` directly — it uses
those helpers zero times. That gap is exactly what Test B exists for.

**Test B — two devices, ~15 minutes of live ranging, two boots each.** Every check passes:

| check | result | previous firmware |
|---|---|---|
| off-grid timestamps | **98.6% / 98.7%** | 0% (all snapped) |
| achieved granularity | **10 ms**, as predicted from `1000*s + 10*hundredths` | 500 ms |
| cross-device ms-stamp agreement | **1711/1711 = 100.0%**, all within 100 mm | 99.8–100% at 500 ms |
| backward time steps | **0, of any size** | 8 across 3 devices |
| ranging-record collisions | **0** | 6–31 per device per night |
| console output | **silent** | warnings on 3 of 4 devices |

**The anchors give the first direct measurement of the clock, and it is the result the change was made for.**
Decoded lag — `(rtc − experiment_start_time) − experiment_ms/1000` — runs **−0.1 s to −1.2 s** on both
devices. The sign and size are exactly what correct behaviour looks like: `rtc` is stored to whole seconds
while the experiment timestamp carries hundredths, so truncation alone accounts for up to −1 s. **Against
−73 s, −79.5 s and −1036 s of accumulated lag in the previous run, this is the bug closed.**

*And it survived a reboot, which is the specific thing that used to break it.* On 02 the last anchor before
the second boot reads 21,228,070 ms / rtc 1,786,730,027 and the boot anchor after it reads 21,507,150 ms /
rtc 1,786,730,306 — **279,080 ms of log time against 279 s of RTC time.** The reboot consumed no
unaccounted time and introduced no step, where the old scheme would have discarded up to
`STORAGE_FLUSH_TIMEOUT_S` of it.

**What this does not yet prove.** Both devices booted fresh, together, minutes apart, so their clocks had
little opportunity to diverge — the network-join case with two genuinely separated devices is still the
overnight run's job. Cleared to proceed.


### 12.27 Experiment 18 — the 4-day deployment: the watchdog question answered, and an RTC anomaly

**Method.** Four devices (02, 3E, AE, F3), daily-times schedule, **3.94 days** each. Offload under a minute
per device. Console output only under `--debug`.

| device | pages | records | reboots | of which watchdog |
|---|---|---|---|---|
| 02 | 1879 | 190,932 | 8 | 0 |
| 3E | 1795 | 164,917 | 10 | 2 |
| AE | 1721 | 152,708 | 14 | 4 |
| F3 | 1894 | 264,347 | 10 | 2 |

**Finding 1 — the §12.22 question is answered, directly and not by inference: the watchdog resets are
genuine hangs.** Anchors are written every ~300 s, so the anchor interval spanning a reset measures the
outage. Across all eight watchdog resets:

| device | anchor gap across the reset | unaccounted time |
|---|---|---|
| 3E | 1133 s, 1120 s | −0.4 s, +0.0 s |
| AE | 1453, 1156, 1455, 1457 s | −0.1, −0.6, −0.8, −0.3 s |
| F3 | 1127 s, 1131 s | −0.5 s, −0.3 s |

The gaps are **1120–1457 s**, against a reset window of 56 x 20.69 s ≈ **1159 s**. An anchor lands at most
300 s before a hang begins, so a genuine hang predicts a gap of 1159–1459 s — which is exactly the observed
range. `TimeAlignedTask` stopped for ~19 minutes, the watchdog fired, the device rebooted. **The watchdog is
earning its keep.** By contrast the 34 `SW Power-On` resets are the daily-times wake/sleep cycle and the
charge cycle working as designed, so the §12.16 daily-times item now has four days of incidental validation.

*The "unaccounted" column is the other half of the result.* It compares RTC seconds against log seconds
across each reboot, and lands within ±0.9 s every time — the RTC's own 1-second quantisation. **The offset
recovery of §12.24 holds across 42 reboots**, including eight that followed a 19-minute hang, where the old
scheme would have silently erased that time and re-exported it as a backward jump on a peer.

**Finding 2 — the time jumps are effectively gone.** One backward step ≥ 2 s in 3.94 days across four
devices, against eight in a single night before. And that one is **fully explained by its own anchors**,
which is what they were built for:

```
p206  r       ms=24670620
p206  ANCHOR  ms=24688330   rtc=1786758688   <- written BEFORE the offset changed
p207  r       ms=24671120                    <- the re-based record
p207  ANCHOR  ms=24671910   rtc=1786758689   <- periodic anchor, new base
```

Two anchors one RTC-second apart bracket the change: F3's own clock read 16.42 s ahead of the network and
snapped down to it. Magnitude, direction and true time are all recoverable from the log alone.

**Finding 3 — a regression the millisecond change introduced, small but real.** Removing the 500 ms snap
made sub-quantum disagreements visible to `storage_store_record()`, whose `time_moved_backwards` check
commits a page on *any* backward step. Steps of 10–40 ms now occur — a ranging record carries the master's
broadcast time while its neighbours carry the device's own clock read — and each one forces a partial page:

| device | small backward steps | as % of pages |
|---|---|---|
| 3E | 1 | 0.06% |
| 02 | 6 | 0.3% |
| F3 | 16 | 0.8% |
| AE | **126** | **7.3%** |

The cost is extra flash wear and extra pages to transfer, not lost data. *Cheap fix if it is worth it:* give
the check a tolerance and, below it, clamp the record's timestamp to `page_last_timestamp` instead of
committing. Monotonicity and the `first <= last` header invariant both hold, at a cost of ≤40 ms of accuracy
on those records. Not applied — 7.3% on the worst device is tolerable, and the fix touches the most
safety-critical function in the storage layer.

**Finding 4 — the RTC reads ~70 s fast at offload on every device, and the logs cannot measure it.** Every
timestamp in the log derives from the RTC, so the log is self-consistent with a wrong RTC and shows nothing.
The file mtime is no help either: it sits 21 h after the last record, which is the delay before downloading,
not clock error. What the logs *can* settle is narrower and still useful:

- **The RTC and the FreeRTOS tick agree to better than ~30 ppm.** One `TimeAlignedTask` loop is 30000 ticks,
  and `port.c` uses `ulTimerCountsForOneTick = 32768/100 = 327` (truncated from 327.68), so an interval is
  30000 x 327/32768 = **299.3774 s** of real time rather than 300. Measured across 1112–1134 in-session
  anchor intervals per device: **299.369 / 299.382 / 299.376 / 299.380 s.** The RTC measures the tick's
  known 0.208%-fast period correctly.
- **That rules out a differential fault** — most importantly the RTC silently falling back to the LFRC,
  which at 24% slow (§12.20) would have been unmissable.
- **It cannot rule out a common-mode crystal error**, because both counters divide the same 32.768 kHz
  crystal. If the crystal is fast, both are, and comparing them is blind to it.

**RESOLVED — and my per-reboot hypothesis was wrong.** A single-device control session run without any
reboot, 11:28 to 09:55 the next day (22.45 h), drifted **15-16 s**. That is **186-198 ppm**, and it predicts
**63-67 s** over the 3.938-day deployment. Measured per-device drift:

| device | drift | reboots | implied rate |
|---|---|---|---|
| 02 | 66 s | 8 | 194 ppm |
| AE | 66 s | **14** | 194 ppm |
| 3E | 72 s | 10 | 212 ppm |
| F3 | 81 s | 10 | 238 ppm |

**02 and AE drifted identically at 66 s with 8 versus 14 reboots, which refutes the per-reboot step
outright.** It is a rate error: ~194-238 ppm, mean ~210 ppm, i.e. **~18 s/day fast**. The 44 ppm spread
between units is ordinary crystal tolerance sitting on top of a common ~200 ppm bias, and the control session
independently reproduces the same rate on a device that never rebooted.

*What that means.* The common-mode bias is a design-level frequency error — every unit is fast by about the
same amount, which per-unit crystal variation cannot produce. The usual cause is load capacitance below the
crystal's rated `CL`, which makes it oscillate fast; the direction matches. Apollo4 exposes no RTC trim
(§12.20 found XT/LFRC calibration was deprecated and removed from the family documentation), so there is no
software knob.

*Three ways to respond, none of them urgent.* Fix the load capacitance at the next respin, which is the only
real fix. Or correct on the host: the log now carries a `TIME_ANCHOR` every ~300 s, and the host knows true
time at download, so a single linear fit across the anchors corrects every timestamp in the file to within
the crystal's short-term stability — **the anchors make this possible where before there was nothing to fit
against.** Or accept it: ~18 s/day matters only when correlating against an external record, and never for
the network's internal consistency, which is maintained by the offset mechanism rather than by absolute
accuracy.

*One corollary worth stating:* the FreeRTOS tick shares this crystal, so it is fast by the same ~200 ppm on
top of its own +2080 ppm truncation error (§12.27 Finding 4 above). Anything tick-derived —
`STORAGE_FLUSH_TIMEOUT_S`, `BATTERY_CHECK_INTERVAL_S` — is correspondingly short. At 0.23% that is immaterial,
but it is why the anchor interval measures 299.38 s rather than 300.

### 12.28 Closing changes — tolerance, resolution, reporting, and one non-problem

**A. The `time_moved_backwards` tolerance (closes §12.27 Finding 3).** `storage_store_record()` now clamps a
backward step of at most `STORAGE_TIMESTAMP_TOLERANCE_MS` (250 ms) forward to `page_last_timestamp` instead of
committing the page. The 10-40 ms steps that cost AE 126 partial pages are writer disagreement — one record
carrying the ranging master's broadcast time next to one carrying the device's own clock read — not a moved
time base, and 250 ms separates them cleanly from the one genuine 17.2 s re-basing in the same run. The header
invariant (`first <= last`) and monotonicity both still hold; the cost is at most 250 ms of accuracy on a
clamped record, and only when the alternative was a nearly-empty page.

*Covered by the storage test*, which now writes three records whose middle one steps back by half the
tolerance: they must produce **one** page, and a fourth page in the count means the tolerance was not applied.

*Corrected 2026-10-08: the record keeps its own timestamp.* The clamp's cost was not just accuracy. The
clamped records are ranges: a range is stamped with its round's start, which every device in the round shares,
but written ~30 ms later when the round ends, so a motion, voltage or diagnostics record stamped in between
pushed it forward. Analysis matches ranges across devices by exact timestamp, and a 5.2 h, ten-device production
run had 73 rows (~0.02%) that no longer matched their round. `nandlog_store_record()` now stores a sub-tolerance
step as stamped, and the page header advertises its **earliest and latest** record rather than its first and last
written. `first <= last` still holds and a date-limited read still selects correctly, since the seek checks every
header against both bounds; records within a page are no longer guaranteed to be in time order (readers sort), and
neighbouring pages can overlap by up to the tolerance, which both readers leave out of their time-discontinuity
reports. The storage test now also checks that the stepped-back record keeps its stamp and that the page spans it,
and the host simulation test checks that a read ending between the two records still includes the page.

**B. Sub-second `rtc` (closes an open question, and it was better than free).** The anchor's payload changed
from `rtc_get_timestamp()` (whole seconds) to the device's own **un-offset** experiment clock in milliseconds.
Three things improve at once and nothing costs more — the record is still 9 bytes:

- **Resolution goes from 1 s to 10 ms**, the RTC's real granularity (`1000*s + 10*hundredths`).
- **The offset becomes exact.** The record's timestamp is the network clock and the payload is the local clock
  *at the same instant, from a single RTC read* — so their difference carries no sampling error at all, where
  before two separate reads plus second-truncation bounded recovery at ~1.5 s.
- **Recovery got simpler**, not more complex: `offset = network_ms - local_ms`, with no reference to
  `experiment_start_time` at all, so `app_get_experiment_start_time()` is no longer needed by the boot path.

Absolute wall time is still recoverable as `experiment_start_time + local_ms/1000`, and the host republishes
it as `'rtc'` so downstream consumers keep the shape they had, alongside a new exact `'offset'` in ms.

**C. Reset causes surfaced to the operator (closes an open question).** Under `--debug` the download now
prints any reset whose cause is not `SW Power-On`, with timestamps. Routine graceful resets appear at both
ends of every log by construction, so reporting those would be the noise §12.25 exists to avoid; a watchdog
or a brownout is the device reporting a fault even though the transfer succeeded, and that is worth a line.

**D. The host merging question — there was no problem to fix.** The concern was that a timestamp collision
might make the host choose between a motion record and a BLE-scan record. It cannot: `log_data[timestamp]` is
a dict keyed by *record type*, so **a single row already holds a range, a motion flag, a BLE scan and a
voltage simultaneously.** Demonstrated directly against the real parser — four records of four types at one
timestamp decode to one row:

```
{'t': ..., 'r': {2: 150, 62: 400}, 'm': True, 'b': [174, 243], 'v': 3900}
```

So motion and BLE-scan never compete, and no prioritisation is needed or possible between them. **The only
overwrite risk is two records of the SAME type at the same millisecond**, and millisecond resolution removed
the one case that mattered: ranging rounds are 500 ms apart, so they can no longer collide at 10 ms
granularity. Measured on the deployment logs: **zero same-type collisions.** Motion-versus-motion remains
theoretically possible, and there "last wins" is the correct answer anyway, since a motion record is a state
flag and the later one is the current state.

**E. `nandlog`.** The chip and port layers now live in `src/external/nandlog` (`nandlog_chip_w25n01.c`,
`nandlog_chip_w25n02.c`, `nandlog_port.c`), separating the media driver from the log format. The record and
page logic — `storage_store_record()`, the page header, the epoch ring, erase-ahead, the offload wire format —
is still in `storage.c`, so all of §12.28's changes landed there. The extraction is in progress rather than
complete.


13. Open questions
------------------

**§15.7 is the live list.** This section is kept for the questions that predate the firmware-wide audit and
are still open, plus a record of what closed them.

1. **Route the battery divider to GPIO 11 whenever boards are next revised.** (§7.1) One trace. The firmware
   side is already written and gated (§11 Phase 3), so enabling it is a one-line `#define` in that
   revision's `pinout.h`. Not urgent — no board order is planned — but it cannot be retrofitted, so it
   should ride along with whatever the next respin is for.
2. **Measure VCOMP standby current** before relying on it, if and when such a board exists (§7.1). No spec
   exists in any datasheet in this tree.
3. **The RTC runs ~210 ppm fast (~18 s/day) and it is a hardware matter.** (§12.27 Finding 4) Measured, cause
   understood, no software fix available. Route load capacitance against the crystal's rated `CL` at the next
   respin; until then either accept ~18 s/day of absolute error or fit a linear correction across the
   `TIME_ANCHOR` records on the host. Does not affect the network's internal consistency.
4. **~~Find the hang.~~** CLOSED. Four multi-day runs, 778 device-hours, zero watchdog resets against an
   expectation of 17.5 (§15.16); the near-miss counters of §15.15 then closed the residual question too,
   by showing the declines were a race in the watchdog's own arithmetic rather than a stall (§17).
5. **Is a reboot the right response to a charger transition?** Now §15.7 item 7.
6. **~~Should the reset-reason record be surfaced to the operator?~~** Done (§12.28 C) — non-`SW Power-On`
   causes print under `--debug`.
7. **~~Finish the `nandlog` extraction.~~** Done (§14). The log format, epoch ring, erase-ahead and wire
   format all live in `src/external/nandlog/` and `storage.c` no longer exists.
8. **~~Decide whether to turn record framing on.~~** Done (§15.15) — on, alongside the diagnostics record
   whose addition is exactly the change framing makes safe.

*Resolved earlier:* metadata ring size fixed at 8 blocks; `experimental_tottag.py` frozen as v1-only;
per-page retransmission in scope with a 3-round retry cap (§8.1.1); battery detection unchanged (§7.2);
`STORAGE_FLUSH_TIMEOUT_S` = 120 s; on-demand erase-ahead brought forward into Phase 0 (§4.2).

14. Extraction into a reusable library (nandlog)
------------------------------------------------

The core of this work -- append-only, power-fail-safe, CRC-validated logging directly on raw SPI NAND with
no FTL -- is not specific to this project, and is being separated so other projects can use it. Decisions
taken up front:

- **Name `nandlog`.** `strata` was preferred but is already taken by other software; `nandlog` is dull and
  unambiguous, which for a storage library is the right trade.
- **Lives at `src/external/nandlog/`**, alongside `decadriver`, `segger` and `tinyusb`, picked up with one
  `-I` and one `VPATH` line. **No build system of its own** -- header and source files only, in the manner
  of FatFS.
- **TotTag consumes it; it is not forked.** There is exactly one copy of the code. `storage.c` is being
  reduced to the log core, and everything that cannot be shared moves into TotTag-side adapters.
- **Pages only, at least for now.** Record framing is deliberately excluded: today's records derive their
  length from their type, which forces any parser to know every type. A generic layer is possible with an
  explicit length field, at a cost of two bytes per record. **Implemented in §14.10, off by default.**

### 14.1 The layering, and what it cost to find it

Three boundaries had to be cut, and each one was discovered by trying to cross it:

**Platform.** `spi_read`/`spi_write` turned out to be the only functions touching the Apollo4 IOM, so the
seam was already almost in the right place. `nandlog_port.h` declares ten functions -- two transfer
primitives, init/deinit, write-enable, power, two delays, log and fatal -- and `nandlog_port.c` implements
them for this board. The 186 lines of SPI code moved verbatim, so the change was pure code motion,
confirmed by 1721 pages comparing byte-identical across the refactor.

*2026-10-08:* the board's port now lives in `src/peripherals/src/storage.c`, outside the library, so that
`src/external/nandlog` can be the upstream repository unmodified, whose own `nandlog_port.c` is a template.

`nandlog_port_fatal()` is worth calling out: the bounded busy-wait added in Phase 5 called `system_reset()`
directly, and a library has no business resetting the system it is embedded in. The host now decides.

**Application.** `storage.c` knew far more about TotTag than expected: it enumerated all eight record types
in a length table, computed whether a deployment was currently active (device counts, termination flags,
daily schedules) and called `storage_disable()` on its own judgement, and converted wall-clock times to
experiment-relative milliseconds by reading the metadata blob. All of that now lives in
`src/tasks/storage_records.h`. The log stores an opaque blob, is *told* whether it is enabled, and only
ever compares timestamps -- never interprets them.

The record-type dependency was replaced by `storage_read_recent_page()`, a record-agnostic backwards
iterator over pages. Its contract makes two things explicit that had been implicit in the loop it replaced:
a zero return means *skip this page*, because a bad block is not the end of the log, while `end_of_epoch`
is the signal to stop.

**Configuration.** `nandlog_conf.h` is the integrator's file, in the role of FatFS's `ffconf.h`. Geometry
genuinely varies by board here -- revM is 2048/1024/64 where revN, revO and revP are 4096/4096/256 -- so it
is injected rather than fixed. The no-hardware stubs moved out to `storage_stub.c`, and the core now carries
no board-presence conditional at all: it keys off a single `NANDLOG_HAS_HARDWARE` macro the integrator
defines.

### 14.2 Two defects worth recording

**An `#if` on an undefined macro silently evaluates to zero.** Removing `#include "app_tasks.h"` from
`storage.h` left `REVISION_ID` and `REVISION_APOLLO4_EVB` undefined, so
`#if REVISION_ID != REVISION_APOLLO4_EVB` became `0 != 0` and **the entire storage layer compiled as its
no-hardware stubs**. It compiled clean, linked clean, and produced a firmware that would have run normally
and logged nothing. Only the test suite caught it, and every failure it reported was a downstream symptom.

A permanent `#error` guard now checks the macro is defined before it is tested. It has since caught two
further mistakes -- the wrong header (`pinout.h`, not `revisions.h`, defines `REVISION_ID`) and an ordering
slip where the guard itself preceded the include that defines the macro. Decoupling by deleting includes is
more dangerous than it looks, precisely because the failure is silent.

**A contract change that spanned two functions.** Moving the wall-clock conversion out of the log changed
the meaning of the timestamp arguments to *both* `storage_begin_reading()` and
`storage_retrieve_num_data_chunks()`. Only the first was updated, so an absolute timestamp (~1.79e9, read
as milliseconds) landed twenty days past every page and the end bound silently stopped biting. A
date-bounded download returned four days of data instead of two, internally consistent and reporting no
loss. A date-bounded BLE download was the only thing that could have caught it.

The API is now arranged so that cannot recur: `storage_begin_reading()` resolves **both** bounds, and
`storage_retrieve_num_data_chunks()` takes no argument and only reports over the span already established.
The split -- a function that accepted an `ending_timestamp` and silently discarded it while a different
function did that seek -- is what let one bound be converted and the other forgotten.

### 14.3 Where it stands

| | |
|---|---|
| Platform coupling behind `nandlog_port.h` | done |
| Application coupling behind `storage_records.h` | done |
| Configuration behind `nandlog_conf.h` | done |
| EVB path removed | done |
| Stubs behind `NANDLOG_HAS_HARDWARE` | done -- kept inline in `nandlog.c` rather than split to a second file |
| Chip-variant split | done -- one self-contained driver per part, geometry included (§14.5) |
| Files physically moved into `src/external/nandlog/` | done -- the log core is now `nandlog.c`/`nandlog.h` (§14.6) |
| RAM simulator and fault injection | done (§14.9) |
| Reference Python parser, `DESIGN.md` | done (§14.9) |

`storage.c` and `storage.h` contain zero application symbols and no board headers. Verified by the full
storage test suite, a date-bounded BLE download, and builds of revM, revN, revO and revP.

### 14.4 The chip-variant split

Thirteen `#if REVISION_ID < REVISION_N` conditionals remained in `storage.c`, and they were not cosmetic --
they were two different NAND drivers sharing a log layer:

- **Bad-block management differs structurally.** revM uses the chip's *hardware* BBM LUT
  (`lba`/`pba` pairs, `COMMAND_READ_BBM_LUT`); revN and later use a *software* table persisted to a
  reserved page. Different types, different state, different reserved-block counts (40 versus 80).
- Different device-ID reads, OTP base addresses, and ECC/BUF status-register bit patterns.
- `read_page_with_spare_data()` does not exist on revM at all.
- The variant code was **interleaved inside `storage_init()`, `is_first_boot()` and
  `storage_reset_bad_block_table()`**, not confined to whole functions, so splitting it meant designing the
  chip API rather than moving text.

`storage.c` now contains **zero `REVISION_ID` conditionals**, and is 1463 lines down to 1055. Selection
follows the IMU precedent, on `$(REVISION)` in both Makefiles.

**The interface.** `nandlog_chip.h` declares nine functions:

    nandlog_chip_probe()            -- is the part there and answering? mutates nothing
    nandlog_chip_init()             -- configure registers, load or first-boot-build the bad-block table
    nandlog_chip_low_power(sleep)
    nandlog_chip_read_page(buffer, page)
    nandlog_chip_write_page(data, page)
    nandlog_chip_erase_block(page)
    nandlog_chip_is_bad_block(page)
    nandlog_chip_mark_bad_block(page)
    nandlog_chip_reset_bad_blocks()

Four decisions are worth recording, because each one removed something from the log rather than relocating
it:

- **Every mutating call gates write protection itself.** The log had been opening and closing protection
  around its own sequences, with `write_page()` re-opening it mid-loop after `erase_block()` had closed it.
  Those bit patterns (`0b01111110` / `0b00000010`) are chip knowledge, and the nesting was a standing
  invitation to leave the array unlocked. The log now contains no status-register access at all. It costs
  two register writes per erase, against a multi-millisecond erase.
- **`chip_is_first_boot()` and `chip_read_page_with_spare()` are not in the interface.** Both were listed as
  candidates, but their only caller is the first-boot factory scan, which is itself chip-specific and now
  runs inside `nandlog_chip_init()`. Exporting them would have exported two functions nothing calls, one of
  which revM cannot implement.
- **`chip_configure()` and `chip_load_bad_blocks()` collapsed into `nandlog_chip_init()`**, with
  `nandlog_chip_probe()` split off. The split falls where `_MANUFACTURING_TEST_` needs it: that build wants
  "is the chip present?" and nothing else.
- **The ranged erase stayed in the log**, as `erase_page_range()`, because its wrap semantics are a property
  of the log region rather than of the chip. The chip erases exactly one block and reports success; the log
  decides that a failure means the block should be retired.

**Files, and how a mismatch is caught.** Both were reworked immediately afterwards and are described in
§14.5. The first cut kept a shared `nandlog_chip_w25n.c` beneath two variant files, and put the reserved-block
count in each board's `pinout.h` with a `_Static_assert` in the driver to catch disagreement.

**Two behaviours deliberately changed**, both on revM, which has no hardware to test against:

- `storage_reset_bad_block_table()` used to clear the in-RAM LUT on every part. On revM that is wrong: the
  chip is still remapping every block the LUT names, so a cleared RAM copy would have the driver write
  through remapped addresses believing them good, until the next boot re-read the LUT. revM now erases the
  spare blocks, leaves the table alone, and says plainly that a hardware LUT cannot be cleared.
- The revN+ bad-block table had no bound on insertion -- `bad_block_lookup_table[bbm_index++]` in both the
  factory scan and `add_bad_block()`, against a 256-entry array. Reaching that point means the array is
  beyond saving, but overrunning the table would have corrupted whatever followed it. It now refuses and
  logs.

**One pre-existing quirk preserved, not fixed.** On revM, unused hardware LUT entries read as zero, so
`is_bad_block()` matches block 0 against an empty entry and always reports it bad. The effect is that the
metadata ring loses its first 64 of 512 slots on that revision. It is faithful to the previous behaviour and
was left alone; worth fixing if revM hardware ever comes back into use.

**Verification.** Clean builds of revM, revN, revO and revP, all zero warnings. `tests/peripherals/test_storage.c`
compiles unchanged -- `storage.h` did not move a byte, so the public API is identical. The destructive
on-device suite (`make storage BOARD_REV=P`) has **not** been run.

### 14.5 One driver per part, owning its own geometry

The first cut was built on the observation that revM and revN+ answered the same commands, and shared those
mechanics through `nandlog_chip_w25n.c`. Naming the files after the actual parts is what exposed the mistake:
they are a Winbond **W25N01GWZEIG** and an Alliance Memory **AS5F18G04SND**, different vendors entirely. The
command sets agree by coincidence of history, not by contract, and the next part added may share none of it.
A "family" layer sitting under two drivers encodes that coincidence as structure.

So the shared file is gone. Each driver now carries its own command set, status-register handling, busy-wait,
page primitives and write-protect gating, duplicated in full. The cost is real -- roughly 110 lines exist
twice -- and it is the right cost: each file is complete on its own, and neither is ever edited in sympathy
with the other. §14.2's second defect was a change half-applied across two functions; the guard against that
here is not sharing, it is that there is nothing to keep in step.

    chips/nandlog_chip_common.h          -- boilerplate every driver needs, whatever the vendor
    chips/nandlog_chip_W25N01GWZEIG.c    -- revM,  hardware BBM LUT
    chips/nandlog_chip_AS5F18G04SND.c    -- revN+, software BBM table

`nandlog_chip_common.h` contains no command codes, register numbers or bit patterns, and no implementation.
It is the contract: a driver states its geometry, includes the header, and the header checks the declaration
is complete and derives the addressing that follows.

**Geometry moved out of the board headers and into the drivers.** `pinout.h` had been carrying page size,
block count, spare size, reserved blocks and the device ID for all four revisions. That is a description of a
chip living in a file about a board, in four copies, none of which the driver could enforce beyond asserting
agreement after the fact. All of it now lives in the driver that uses it, stated once:

    #define NANDLOG_CHIP_NAME              "AS5F18G04SND"
    #define NANDLOG_CHIP_PAGE_SIZE_BYTES   4096
    ...
    #include "nandlog_chip_common.h"

Adding a part is therefore **one new .c file and one line in the build**. Nothing outside that file states
anything about the part it describes, so there is no longer a second place to get it wrong.

**Which means geometry is now a runtime value.** The log had been sizing buffers and computing its region
from compile-time constants that came from `pinout.h`. It now asks the chip: `nandlog_chip_geometry()` returns
page size, spare size, pages per block, block count and reserved blocks, and `storage.c` resolves its region
from that in `resolve_geometry()` at the top of `storage_init()` -- before the probe, so even the
`_MANUFACTURING_TEST_` early return leaves it valid. The former macros survive as file-static variables under
the same names in lower case, so the body of the log reads as it did.

**No dynamic allocation, and none introduced.** Every buffer that was static is still static, sized instead by
`NANDLOG_MAX_PAGE_SIZE_BYTES` in `nandlog_conf.h` -- the integrator's RAM budget, which is the one thing that
genuinely is the host's to decide. This is safe in the direction that matters: a part whose page exceeds the
budget is a compile error inside its own driver, while a budget set too high costs unused RAM and nothing
else. The application follows the same rule; only the arithmetic became runtime, through
`storage_data_bytes_per_page()`.

**The contract fails loudly, and says which part failed.** A driver that omits a geometry macro gets an
`#error` naming the macro -- not an `#if` silently reading zero, which is §14.2's first defect exactly. Beyond
that the header asserts the page and spare fit the budget, that pages-per-block is a power of two (the log
masks addresses to find blocks, which only works if it is), and that a part does not reserve more blocks than
it has. Each message carries `NANDLOG_CHIP_NAME`. All four were verified by compiling deliberately broken
declarations:

    #error "A chip driver must define NANDLOG_CHIP_RESERVED_BLOCKS before including nandlog_chip_common.h"
    static assertion failed: "TESTPART has a larger page than NANDLOG_MAX_PAGE_SIZE_BYTES allows for"
    static assertion failed: "TESTPART must have a power-of-two number of pages per block"

**`nandlog_conf.h` trimmed to what is genuinely a choice.** It is the one file an integrator edits, so
anything derivable was moved out of their sight: `STORAGE_BUSY_TIMEOUT_POLLS` is computed in
`nandlog_chip_common.h` from the two values they do set, and `NANDLOG_MAX_PAGE_WITH_SPARE_SIZE_BYTES` and
`MEMORY_NUM_ERASE_MARGIN_BLOCKS` turned out to have no callers at all and were deleted, along with
`SEED_SEARCH_MAX_PAGES` in `storage.c`, left behind when the seed search became the metadata ring. Seven
values remain, each a real decision. Omitting one is a named `#error` from `nandlog_chip.h`, which every
translation unit in the library already includes, rather than an undeclared identifier at whichever use site
the compiler happened to reach first.

**Verification.** Clean builds of revM, revN, revO and revP, zero warnings each. `test_storage.c` compiles and
its symbols resolve against the built objects. **The on-device storage suite was run twice on revP hardware
and passed both times.**

### 14.6 The log core moves out, and what stayed behind

The expectation going in was that a library would have to be carved out of `storage.c`. It did not: the
application coupling had already gone to `storage_records.h` in the earlier pass, and grepping the file for
application concepts turned up three log strings saying "experiment details" and nothing else. All 1084 lines
were library. The move was therefore the other way round -- the file went into `src/external/nandlog/`
wholesale as `nandlog.c`/`nandlog.h`, and a new, thin `storage.c` was written to be the application interface.

Four things stayed behind, and they are the whole of what a general-purpose log should not have known:

| | why it is the application's |
|---|---|
| `_TEST_NO_STORAGE` stubs | "no part is fitted in this build" is a build-mode decision. The library now keys off `NANDLOG_HAS_HARDWARE`, which `nandlog_conf.h` derives from the host's own flag -- the one place the two vocabularies meet |
| `_TEST_IMU_DATA`, 3 sites | a capture test that wants a download ignoring its time bounds |
| `_MANUFACTURING_TEST_`, 1 site | identify the part, leave the log down |
| the `storage_*` names | 200+ call sites; the shim forwards, so none of them changed |

Two of the three `_TEST_IMU_DATA` sites turned out to need nothing from the library. `begin_reading(0, 0)`
already skips both seeks, and `retrieve_next_data_chunk`'s test path differs only in testing `current_page`
where the normal path tests `last_reading_page` -- which `begin_reading` has just set equal to it. The third
needed `nandlog_epoch_page_count()` and `nandlog_is_reading()`, both reasonable accessors in their own right.
`nandlog_probe()` was added for the manufacturing path, replacing an `#ifdef` that used to sit in the middle
of `storage_init()` and return early out of it.

**One coupling only the move exposed.** `NANDLOG_TIMESTAMP_TOLERANCE_MS` -- how far a record's timestamp may
step backwards before the page is committed -- was defined in `src/app/app_config.h` and reached the log
through `app_tasks.h`. It is log policy, not application policy, and nothing else used it. It now lives in
`nandlog_conf.h`, with `storage.h` aliasing the old name so the test that references it is unchanged.

**Symbols.** `storage_*` became `nandlog_*` and `STORAGE_*` became `NANDLOG_*` throughout the library,
including the configuration names (`MEMORY_NUM_BLOCK_ERRORS_BEFORE_REMOVAL` and friends). `storage.h` carries
typedefs and defines mapping the old names onto the new, so the application compiles untouched and the seam
is visible in one place rather than spread across 200 call sites.

**The port's revision conditional is gone.** Both functions it wrapped were replaced by the splitting
versions, with `spi_read` switching to the `num_reads`-computed-up-front idiom `spi_write` already used, so a
zero-length read still issues its one empty transaction exactly as the smaller-page path did. The library now
contains no reference to a board revision anywhere.

**Verification.** Clean builds of revM, revN, revO and revP, zero warnings each, compiled under
`_TEST_NO_STORAGE`, `_TEST_IMU_DATA`, both together, and `_MANUFACTURING_TEST_`. **The on-device suite was
re-run after the move and passed.** The stubs were briefly split to a `nandlog_stub.c`; that file was folded
back into `nandlog.c`, since one conditional in one file reads better than two files that are each half empty.

One behaviour worth knowing: under `_TEST_IMU_DATA`, `storage_retrieve_num_data_chunks()` now reports the
full region when the log has wrapped, where the old code reported zero. No other path changed, and that test
mode never fills the array.

### 14.7 State the log keeps, and what it is for

Written down because the set had accreted rather than been designed, and one member turned out to be doing
nothing at all.

| | what it does | when it is set |
|---|---|---|
| `is_initialized` | makes `nandlog_init()` idempotent and `nandlog_deinit()` safe to call unpaired | init / deinit |
| `in_maintenance_mode` | holds the part powered across a run of operations instead of waking and sleeping it around each one. Gates roughly fourteen `nandlog_port_power()` pairs. Also the precondition for storing metadata and for reading | enter / exit maintenance mode |
| `is_reading` | a read is open: serves the `retrieve_*` calls and refuses writes for the duration | `begin_reading` sets it to `in_maintenance_mode`; cleared by `end_reading` and on the last page |
| `disabled` | the caller has asked the log to stop accepting records; reads still work | `nandlog_disable()` |
| `log_region_full` | the head has wrapped onto the epoch's first page, so every usable page is spent. Refuses further writes | `advance_write_head`, recomputed at init, cleared by a new epoch |
| `starting_page` / `current_page` | the epoch's first page and the write head | init, `store_metadata`, `advance_write_head` |
| `reading_page` / `last_reading_page` | the span an open read still has to cover, both bounds resolved up front | `begin_reading` |
| `log_data_size` | payload bytes across that span | computed by `retrieve_num_data_chunks`, read by `retrieve_num_data_bytes` |
| `cache` / `cache_index` | the page being assembled in RAM | `store_record`, cleared on commit |
| `page_first_timestamp` / `page_last_timestamp` / `page_record_count` | the header fields being accumulated for that page | `store_record`, cleared on commit |
| `log_epoch` / `next_page_seq` / `metadata_ring_page` | the current generation, its next sequence number, and the ring slot describing it | init, `store_metadata`, commit |

**`cache_overflowed` was removed.** It was set in three places -- a record larger than a page, and a record
arriving while reading or while full -- and read in none, in the library or anywhere above it. It recorded
that data had been dropped and then told nobody. Either it should have been reportable or it should not have
existed; nothing depended on it, so it went. If dropped-record visibility is wanted later it should be a
counter with an accessor, not a flag.

Two couplings in that table are worth noticing rather than leaving implicit. `is_reading = in_maintenance_mode`
means `nandlog_begin_reading()` silently does nothing outside a session -- which is defensible, but it is a
precondition expressed as an assignment. And `nandlog_retrieve_num_data_bytes()` is only meaningful after
`nandlog_retrieve_num_data_chunks()` has run, because the first computes what the second returns: the same
shape of two-function contract that caused §14.2's second defect.

### 14.8 Shrinking the exposed surface

The public API went from 29 functions to 24, and the application-side shim disappeared entirely.

**`storage.c` and `storage.h` are gone.** They had held 25 forwarding calls to justify three that did
something. Removing `_TEST_IMU_DATA` took two of those three away, and the third -- the `_MANUFACTURING_TEST_`
probe -- turned out not to need a conditional at all: only `manufacturing_validation.c` is compiled with that
flag and it can simply call `nandlog_probe()`. So the whole layer went, and the application calls `nandlog_*`
directly. What the shim had really been buying was avoiding a rename, which is not a reason to keep a file.

**`_TEST_IMU_DATA` removed codebase-wide.** Seven sites. Five were storage: a download that ignored its time
bounds. **Two were not** -- `app_task_ranging.c` used it to skip `imu_clear_interrupts()` on revM and to run
the IMU at 100 kHz with the gyroscope enabled instead of the accelerometer at 500 kHz. Both now take the
normal-operation branch, which changes what the `ble_range_imu` and `full_exp` test targets do; the flag was
dropped from both in `tests/Makefile`.

**`retrieve_num_data_chunks()` and `retrieve_num_data_bytes()` became `nandlog_read_span()`.** The second was
only meaningful after the first, because the first computed what the second returned -- structurally the same
two-function contract behind §14.2's second defect, and the second such contract to be removed for the same
reason. One call now fills both, either pointer may be NULL, and `log_data_size` stopped being state that
outlives a call.

**`retrieve_next_data_chunk()` folded into `retrieve_next_page()`,** whose `header` argument is now optional.
They walked the same span; one returned the framing metadata and one discarded it, which is what a NULL
argument expresses. Only the test suite used the payload-only form.

**`nandlog_is_reading()` and `nandlog_epoch_page_count()` are internal again.** They had been made public for
one `_TEST_IMU_DATA` override, and outlived it by one commit. `cache_overflowed` went in the same pass, for
being written three times and read never (§14.7).

**Verification.** Clean builds of revM, revN, revO and revP, plus `nandlog.c` under `_TEST_NO_STORAGE` and
`_MANUFACTURING_TEST_`. `test_storage.c` compiles and links. **The on-device suite has not been re-run since
this pass and should be: it changed every call site the tests use.**

### 14.9 The simulator, the parser, and the 2x download

**A download had been doing two passes over the log, and the flag that hid it is gone.** The BLE offload came
back noticeably slower after §14.8, and the cause was `_TEST_IMU_DATA` -- not the storage sites, but the fact
that under that flag `storage_retrieve_num_data_chunks()` returned `log_page_distance()` and **skipped the
byte-summing pass entirely**, with `total_data_length` computed arithmetically. The `full_exp` target carried
the flag. Removing it moved that build from one read of the log to two, which is the whole of the 2x.

The pass itself is not new and not wrong -- the stream header's `total_payload_bytes` is what a host sizes its
receive buffer from. What was wrong is that it was unconditional. `nandlog_read_span()` now skips it when
`num_bytes` is NULL, so a caller that only needs the page count does not pay for a second read of the log. The
page count was always pure arithmetic. Whether the exact figure is worth the traffic is now the caller's
decision, which is where it belongs.

**And checking whether the total could simply be dropped found a bug.** The dashboard sizes both its receive
buffer and its serial read loop from `total_payload_bytes`:

    self.data_length = (V2_STREAM_HEADER.size + details_len +
                        total_pages * V2_PAGE_HEADER.size + total_payload)
    while self.data_index < self.data_length:      # blocks until satisfied

So the figure has to be exact in *both* directions -- and it was not. `validated_payload_length()` checked the
page header but not the payload CRC, while `extract_page_payload()` on the send path checks both and serves a
rotted page as a zero-length gap. A page with a good header and a bad payload was therefore counted and then
not delivered, leaving a host waiting for bytes that would never arrive. It now verifies the payload too,
which costs nothing in flash traffic because the page is already in RAM. The host tests assert exact equality,
and the two that exercise it -- the torn page and the rotted payload -- are precisely the cases that failed
before the fix.

This also settles why the `_TEST_IMU_DATA` fast path appeared to work: `chunks * data_bytes_per_page` is exact
only when every page is completely full, which in a fixed-rate IMU capture it always was. It was never a
general shortcut.

**The simulator implements `nandlog_port.h` in RAM.** Because the porting seam is at the SPI command level, it
emulates a part rather than stubbing a log, and the real chip driver runs on it unmodified -- addressing,
status-register handshakes, busy-waiting and bad-block bookkeeping are all shipping code under test. NAND
semantics are honoured rather than approximated: erase sets bits, programming may only clear them, and an
operation cut short leaves exactly the prefix that made it. Getting that wrong would make the simulator agree
with a buggy log.

Fault injection covers unwritable blocks, unerasable blocks, mid-program and mid-erase power loss, bit rot,
and a part that never clears BUSY. Nine host tests, 32 checks, clean under ASan and UBSan, in milliseconds:
round trip, metadata, write-head recovery across a reboot, block retirement, a torn page, a rotted payload, a
disabled log, reads refused outside a session, and the busy timeout ending in `nandlog_port_fatal()`.

Three of the nine failed first time. Two were test bugs -- faults injected into blocks the run never reached,
and corruption written to bytes that were already zero, which is a no-op when the only thing flash can do
without an erase is clear bits. The third was the byte-total finding above. That ratio is the argument for the
simulator: none of the three would have been visible on hardware without deliberately staging conditions that
take hours to arrange.

**The parser is a second implementation, not a convenience.** `tools/nandlog_parse.py` reads both a raw image
and an offload stream, and verifies every CRC with `zlib.crc32` -- which is why the device's CRC-32 was chosen
to be byte-identical to it. Validated against an image the simulator produced with one page deliberately
rotted: it verified 13 of 14 pages, recovered the metadata slot and the epoch's timestamp span, and isolated
the corrupt page by sequence number as a payload CRC mismatch and a gap in the sequence. A reader written
independently of the writer is the only thing that can catch the two agreeing on something wrong.

**`DESIGN.md` is written**, covering the layering and why each seam is where it is, how to port the library,
how to add a part, the on-flash format, the failure table, offload, testing, and the two deliberate omissions
(record framing and wear levelling).

**Verification.** Clean builds of revM, revN, revO and revP; the revM driver also compiles for the host. Host
suite passes 32/32 under sanitizers. The reference parser round-trips a simulator image.

### 14.10 Optional record framing, and a page that announces itself

The one thing deliberately excluded at the outset (§14 preamble) is now available, off by default.

**The encoding is a flat little-endian `uint16`.** It began as one byte with `0xFF` escaping to a wide length,
on the assumption that records are small. They are not: most of this application's records carry more than 255
bytes, so the escape would have fired most of the time and cost three bytes where two would do. A variable-
width length is only worth its complexity when the short form is the common case, and here it is the rare one.

    [data length: 2][record type: 1][timestamp: 4][data]

**What it costs in API: nothing.** `nandlog_store_record()` keeps its signature, and no other function
changes. Inside, two things move: the record-length arithmetic gains the prefix, and the "larger than a page"
check follows it. The switch is a single knob in `nandlog_conf.h`, defaulted rather than `#error`-guarded,
precisely so the host suite can build both settings from one source tree.

**A page announces its own framing for free.** The question was what that would cost, and the answer is zero
bytes -- not a flags field, not a spare bit, but the magic itself: `TTP1` for opaque records, `TTP2` for
framed ones. The four bytes a reader examines to decide whether it has found a page at all are the same four
that tell it how to read one. `page_header_valid()` accepts either, since which format a page is in is a
question for whoever parses the payload and not for the log.

The benefit over announcing it once in the metadata slot is that a log written across a firmware change parses
correctly **page by page**. An epoch that begins unframed and continues framed is not a special case; it is
just pages, each saying what it is.

**On the wire it has to be said differently,** because a page frame in an offload stream carries no magic. The
stream header's `format_version` does it: 1 opaque, 2 framed.

### 14.11 Both readers understand both formats

`tools/nandlog_parse.py` and the TotTag dashboard now read either.

**The dashboard was the reason the default is off**, and it no longer has to be. It rejected any
`format_version` but 1 outright, in three places. It now accepts 2 as well, and `_strip_framing()` turns a
framed payload back into the layout version 1 already used -- records back to back -- returning alongside it
the boundaries the device declared.

Those boundaries are the point, and they are used in **both** directions. A record that decodes is advanced
past by its declared length rather than by what the decoder consumed, so the two cannot silently drift apart.
A record that does not decode is stepped over exactly. Before framing, the dashboard had to infer a record's
length from its type: a type it did not recognise ended the page, because there was no way to know where the
next record began.

That is measurable. Feeding the same 48 records through both formats with one record of an unknown type
inserted halfway:

| | records recovered |
|---|---|
| unframed | 6 of 12 timestamps -- the page ends at the unknown record |
| framed | 12 of 12, with the unknown record reported as one rejection |

With no unknown record present the two decode **identically**, which is the property that matters most: this
is one format read two ways, not two formats.

A framed payload whose prefixes do not describe it is refused rather than half-decoded. In practice the
payload CRC catches damage first, which is the right order -- corruption is a storage question, framing a
parsing one -- but the check is there for a payload that checksums and is still malformed, which would mean a
firmware bug rather than a bad block.

**Verification.** The host suite builds and runs both settings from one source tree: 32 checks unframed, 74
framed, clean under ASan and UBSan. The framed run writes records of 0, 1, 7, 200, 255, 256, 900 and 1000
bytes, walks every page back byte by byte, and checks each count against what the page header advertised. Both
builds dump an image and the reference parser reads both, recovering 105 records of five types and five sizes
from the framed one knowing nothing about the application. On the dashboard side, the equivalence, unknown-
record and malformed-framing cases above all pass. `nandlog.c` compiles for ARM with framing on and off, and
revM, revN, revO and revP all build clean.


15. Firmware-wide audit — the watchdog, the diagnostics, and the seek
---------------------------------------------------------------------

The storage work above was scoped to storage. This section covers a review of everything around it -- the
scheduler, the peripheral drivers, the watchdog and the build -- prompted by a run in which every device was
taking watchdog resets that the log could not account for (§12.27 left this as the largest open item). Three
things came out of it: the watchdog was rebuilt, reboots were made self-describing, and the time-range seek was
found to be losing data on a class of log the device routinely produces.

Two of the changes in this section were **regressions of my own making**, both caught in the field rather than
at the desk. They are recorded in full, because each one was a category of mistake rather than a typo.

### 15.1 What the audit found

Reading the whole tree against the Apollo4 datasheet and the FreeRTOS port turned up defects in five
categories. The ones that mattered:

| | defect | why it mattered |
|---|---|---|
| 1 | `write_page()` looped `while (!success)`, and three "walk to the next good block" loops were unbounded | all in `StorageTask` at the highest task priority, never yielding; a part that stops accepting programs turns them into a hard spin that only the watchdog can end |
| 2 | `nandlog` had no mutual exclusion, yet is called from four tasks | one page cache, one transfer buffer, and chip reads that span several SPI transactions with chip-select held across them |
| 3 | `rtc.c` used `mktime`/`gmtime` on every record's timestamp path | neither is reentrant, and both were reached from four tasks; also 39 kB of code for arithmetic |
| 4 | storage payload rings were 20 deep behind a 60-deep queue | a slot could be recycled while its queue entry was still outstanding, writing the newest payload under an older record's timestamp |
| 5 | `battery_monitor_get_level_mV()` was called concurrently from the BLE task and the time-aligned task | shared ADC state; the loser got 0 mV, which the time-aligned task read as a flat battery and shut down on |
| 6 | the BLE Device Manager callback answered a failed start by starting again, from inside the callback | an unbounded software loop at BLE-task priority, starving everything below it |
| 7 | `AttsHandleValueNtf` was called from the ranging task and the app task | ATT, L2CAP and the HCI queues are not reentrant and belong to the BLE task |
| 8 | the IMU's IOM interrupt was enabled, at a *higher* priority than the handler issuing its blocking transfers | its first act was `am_hal_iom_interrupt_clear()`, and a blocking transfer detects completion by reading `INTSTAT.CMDCMP` -- so it could wipe the completion flag out from under the transfer waiting on it. The 5x retry loops in `spi_read`/`spi_write` were papering over a race the driver created against itself |

All are fixed. Of these, #2 is worth a note: it is reachable, but every path that would actually corrupt a
transfer requires an active BLE connection, which the runs in question did not have. It was not the cause of
the resets, and the earlier claim that it was has been withdrawn.

### 15.2 The watchdog — three designs, two of them wrong

**The original.** A single call to `system_watchdog_pet()` from `TimeAlignedTask`, the *lowest*-priority task,
once per 300 s, petting the hardware directly. Any task above it that stopped yielding killed the device, and
the only artifact was a reset-reason record saying "watchdog". That is what §12.27 could not explain.

**Second design — pet from the interrupt.** Tasks *check in*; the watchdog's own pre-reset interrupt evaluates
whether every registered task is current and pets only if so. This catches a stall in any task rather than only
in whichever one happens to own the pet, and it measures time with the free-running STIMER counter rather than
the FreeRTOS tick, so a dead tick cannot make a frozen system look healthy.

It also depended on an interrupt that did not arrive. Ten watchdog resets across four devices came back with
the diagnostic field holding the boot marker -- proving the breadcrumb register survives a watchdog reset and
that *no handler had written to it*. The handler was correctly named and linked (overriding the weak alias),
the NVIC line was enabled, `CFG.INTEN` and `WDTIEREN.WDTINT` were both set, and after the first failure it was
moved to NVIC level 1, above the FreeRTOS masking ceiling, so that no critical section could hide it. It still
never ran.

**That produced the worse regression.** With the interrupt as the *sole* petter, nothing petted at all, and
every device fell into an unconditional hardware reset loop at `RESVAL` -- one reboot roughly every 197 s, on
charger and off, independent of what the application was doing. The apparent simultaneity across devices was
the loop's own fixed period: all four armed within milliseconds of each other at experiment start and stayed in
lockstep, drifting apart only as fast as their LFRCs differ.

*This does not agree with §12.20, and the disagreement is unresolved.* That experiment observed the same
handler firing and printing, on a build with `INTVAL`/`RESVAL` of 28/56 at NVIC level 7. The build that failed
used 4/8, first at level 7 and then at level 1. Whether a small `INTVAL` is the difference was not determined,
because the third design made it moot. An earlier claim in this work that "the watchdog interrupt has never
worked in this firmware" was wrong and is withdrawn -- §12.20 is direct evidence against it.

**Third design — pet from the check-in.** `system_watchdog_pet()` records the calling task's check-in and then
makes the same decision the interrupt was making: pet only if *every* registered task is current, otherwise
decline and record which one is late. It keeps the property that made the redesign worth doing while depending
only on code known to run. It also removes the need for a separate stopped-clock check: if the 32 kHz clock
behind the tick dies, no task is scheduled, nothing checks in, and nothing pets. The interrupt handler is
retained doing exactly the same thing, in case it ever does arrive, but nothing depends on it.

Five tasks are monitored. Four check in from a bounded wait in their own loop; the BLE task is the interesting
one. A quiet radio is normal -- advertising alone raises no host events, so the WSF dispatcher can legitimately
block for minutes while ranging -- so counting BLE traffic would reset a healthy device. Instead a periodic WSF
timer drives the check-in, which is a stronger test than intended: the timer is backed by a FreeRTOS software
timer whose callback sets the event that wakes the dispatcher, so the check-in only happens if the BLE task,
the WSF dispatcher, the FreeRTOS timer service *and* the tick are all alive.

Recovery time fell as a side effect of the retuning, from `RESVAL` 56 to 8:

| | watchdog resets | per device-hour | mean silence before reset | run time lost |
|---|---|---|---|---|
| §12.27, 4-day | 8 over 317 device-hours | 0.025 | 1080 s | 3.03% |
| second 4-day | 10 over 394 device-hours | 0.025 | 214 s | 0.60% |
| §15.6, 22-hour | **0 over 77 device-hours** | **0** | — | **0%** |

### 15.3 Reset diagnostics — and a latch that cried wolf

The hardware status says only *that* the device stopped. The firmware now records *what* stopped it, in the
four bits above the status word in the existing `STORAGE_TYPE_RESET_REASON` record -- no on-flash format change
-- written to `MCUCTRL->SCRATCH0`, which survives every reset short of removing power. Fourteen codes: five
per-task stall codes, a multiple-task code, hard fault, stack overflow, assertion, allocation failure, two
storage codes, a stopped-clock code, and a boot marker.

**The boot marker is the part that earned its keep.** Every boot re-arms the field with "nothing recorded a
cause" rather than clearing it. Reading that back proves the register survived *and* that no handler wrote to
it -- which is what separated "the interrupt did not run" from "the breadcrumb was lost", and is the entire
reason §15.2's second design was diagnosed rather than guessed at.

**The latch was wrong, and the 22-hour run exposed it.** `system_record_diagnostic()` was first-writer-wins, so
a stall code, once written, persisted for the rest of the run. All four devices reported a stalled task, none
took a single watchdog reset, and BLE records continued to within four seconds of a reset that was actually the
charger being plugged in. One task being briefly late -- possibly hours earlier, and recovered -- was being
reported as the cause of an unrelated reboot. A stall code now describes an *unresolved* condition: it is
withdrawn on the next successful pet. Fault codes are never withdrawn, since whatever recorded one is not
coming back to clear it.

That transient lateness is real and remains **open**: something made at least one monitored task more than 60 s
late, at least once per device per 19 hours, without ever reaching the reset window. The latch is why it cannot
be placed in time. A decline counter exposed over the live-stats characteristic would settle it without
touching the on-flash format.

### 15.4 The time-range seek was losing data

`seek_page_for_timestamp()` binary-searched page headers on the assumption that page time bounds rise
monotonically with sequence number. §12.23 established that they do not: adopting a new network time base steps
the clock backwards, and the log commits a page at that point, so a boundary lands exactly there. Across it the
search predicate is not monotone.

A first attempt bounded the violation with a backward scan from the search result. A regression test written
for it **passed with the fix disabled**, which is how the attempt was found to be wrong on two counts: the test
exercised the end bound, which extends past a discontinuity naturally and cannot lose data, and a bounded
backward scan cannot be made sound because the runs between discontinuities are hundreds of pages long -- any
window cheap enough to scan is too small to be correct.

Retargeted at the *start* bound, the same test showed the real severity. With the original seek:

```
FAIL: a date-limited read starting across a backward time step returned no pages at all
```

**Zero pages** -- worse than the offload tool's "may be missing data". `nandlog_begin_reading()` receives
`page_count` from a failed search and starts reading at the write head.

The binary search is replaced by an exact scan of every page header in the epoch, taking the earliest page that
could hold data at or after a start bound and the latest that could hold data at or before an end bound. It
costs one header read per page, happens once when a download opens, and is the same order of work
`nandlog_read_span()` already does for an exact byte total. Both answers err *outward*: shipping a little extra
for the host to filter beats dropping data that was asked for. The monotonicity assumption is gone rather than
bounded. The test is in the host suite, using the 17210 ms step from the field logs.

### 15.5 Optimisation level

The firmware had always been built `-O0`. On a battery-powered device CPU time is charge, so this was a power
defect as much as a performance one. Now `-O2`, with `-fno-strict-aliasing`, which is not optional here: the
tree reads structures out of byte buffers by pointer cast in about 180 places -- page and metadata headers out
of the flash transfer buffer, ranging packets out of the radio receive buffer, ATT payloads out of GATT
attributes -- and every one is a strict-aliasing violation that `-O2` would otherwise licence the compiler to
reason around.

| | flash | `nandlog.o` instructions |
|---|---|---|
| `-O0` | 268.7 kB | 3362 |
| `-O2` | 201.7 kB | 1990 |

`-25%` and `-41%`. Safety checks: `am_hal_delay_us` lives in the prebuilt `libam_hal.a`, so delay timing does
not shift; every spin and poll loop was audited for `volatile`, since a missing qualifier is harmless at `-O0`
and an infinite loop at `-O2` (thirteen flags, all already correct, plus one ISR-written flag that was not);
`-fno-omit-frame-pointer` is kept deliberately for a walkable stack while a hang is still open.

`-flto`, `-ffast-math` and `-fsingle-precision-constant` were considered and declined; see §15.7.

### 15.6 Experiment 19 — the 22-hour verification run

**Question.** With the watchdog rebuilt, the unbounded loops bounded, the drivers serialised and the build at
`-O2`, does a full-length deployment still take watchdog resets?

**Method.** Four devices, revP, 19.27 h of continuous unplugged operation inside a single experiment window
(`use_daily_times = 0`), ranging at 2 Hz throughout. Offloaded over BLE and audited page by page.

**Result.**

| | 02 | 3E | AE | F3 |
|---|---|---|---|---|
| uptime | 19.27 h | 19.27 h | 19.27 h | 19.35 h |
| pages advertised / delivered | 1156 / 1156 | 1157 / 1157 | 1156 / 1156 | 1156 / 1156 |
| gaps / failed CRC | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| records | 277 275 | 277 285 | 278 609 | 277 292 |
| RANGE per minute | 119.7 | 119.6 | 119.5 | 119.2 |
| **watchdog resets** | **0** | **0** | **0** | **0** |
| 300 s cadence captured | 101% | 101% | 101% | 101% |
| backward time steps | 1 (0.5 s) | 2 (≤0.5 s) | 1 (0.4 s) | 0 |

4625 pages, 1 110 461 records, **zero gaps, zero CRC failures, zero watchdog resets**. Ranging held 2 Hz for
19 hours on every device, within 0.4% of each other. The 300 s time-aligned cadence is fully captured -- the
101% is the extra per-boot anchor -- against 96-97% in the previous run, where the shortfall was the reset
outages.

The backward steps are all ≤0.5 s and all at low sequence numbers (pages 1 and 7), i.e. initial network
formation rather than mid-run, against steps of up to 17.2 s in §12.23. Each still forces a page commit, since
0.3-0.5 s exceeds the 250 ms tolerance, and each carries its recovering anchor.

**Confidence, stated honestly.** The previous rate of 0.025 watchdog resets per device-hour predicts about 2.0
events in 77 device-hours, so observing zero has a one-in-seven chance of happening even if nothing had been
fixed. The run *definitively* rules out the §15.2 reset loop, which would have produced roughly 1400 reboots;
it is consistent with the intermittent stall being fixed but does not establish it. A multi-day run is what
would.

### 15.7 Open items

One list, renumbered continuously, and an item moves between sections rather than staying where it was
with a strike-through. Everything that had closed by the previous revision is in "Closed" below.

**Open.**

1. **There is no power measurement.** The battery record cannot substitute — over 19 hours the four devices
   dropped 19, 18, 12 and 0 mV from 4.21–4.23 V, the flat top of the lithium curve. Any further power work
   should start with a bench current measurement, because the remaining levers are architectural rather than
   compiler flags: the ~3.5 s BLE stop/start and `ranging_begin` churn in `verify_app_configuration()`, and
   BLE scanning at a 10% duty cycle.
2. **The WSF pool headroom figures are unmeasured.** The counter that matters — allocation failures — read
   zero across 640 device-hours and is trustworthy. The high-water marks in §15.16's logs were uninitialised
   stack, because `WsfBufGetPoolStats()` takes a pool INDEX and was being passed a COUNT despite its own
   documentation saying otherwise. Fixed; the next deployment measures them for the first time.
3. **The web dashboard is not finished.** Built and validated: the app shell, deployment configuration with
   read-back and a manifest, Web Bluetooth transport, log import and analysis, log download with
   retransmission repair, and saving a downloaded `.ttg` (§16, §19). Not built: a **Web Serial** transport,
   and a **log-review view** that surfaces what `analyseDeployment()` already computes. It also does not
   offer live ranging, a device-details panel, or a date-bounded download, all of which `tottag.py` has
   (§19.4).
4. **Standing instruction: re-run `make storage` on hardware after any change to the record format or the
   payload layout,** and pin what a test is testing. It is the only test covering `recover_time_anchor()`,
   and §18 is what happens when it is skipped — plus the discovery that a configuration covered only by a
   default is not covered at all.

**Hardware, next respin.**

5. **Route the battery divider to GPIO 11.** (§7.1) One trace; the firmware side is written and gated.
6. **Measure VCOMP standby current** before relying on it, if such a board exists (§7.1). No datasheet in
   this tree gives one.
7. **The RTC runs ~210 ppm fast (~18 s/day).** (§12.27 Finding 4) Cause understood, no software knob; route
   load capacitance against the crystal's rated `CL`. Until then, accept ~18 s/day or fit a linear
   correction across the `TIME_ANCHOR` records on the host.

**A question rather than a defect.**

8. **Plug and unplug reboot the tag.** Every graceful reset in every run is a charger transition, by way of
   `storage_flush_and_shutdown()` in the `APP_NOTIFY_BATTERY_EVENT` handler. It costs 3.7 s of records and
   works reliably, so this is a design question: is a reboot the intended response to picking a tag up off
   its charger?

**Closed.**

9. **~~Find the hang~~** (§15.16, §17). 778 device-hours across three runs with **zero watchdog resets**
   against an expectation of 17.5; the 6.7-day qualification run alone puts P(zero | unchanged) at about
   6 x 10^-7. The residual 8–16 pet declines per device per 6.7 days were not a stall either: they were a
   race in `watchdog_find_stalled_tasks()`, which sampled the clock outside the critical section guarding
   the check-in stamps, so a task checking in mid-evaluation left a stamp ahead of `now` and an unsigned
   subtraction wrapped it to 49 days. **This was the last open item from the original problem statement.**
10. **~~Run the on-device storage suite against the framed format~~** (§18). All seven tests pass. The four
    that failed first time were the test file, not the firmware: `TEST_RECORD_DATA_BYTES` had never been
    adjusted for the framing prefix, so its page-filling record was two bytes too large and was correctly
    dropped whole. `recover_time_anchor()` — the §15.16 fix, and the reason this item existed — passed.
11. **~~The simulator's two framings~~** (§18.3). One of the two builds had inherited a default that flipped
    in §15.16 and had been a second framed build ever since. Both are pinned explicitly; 80 checks framed,
    38 unframed.
12. **~~A downloaded log cannot be saved from the web dashboard~~** (§19.2). It writes the stream verbatim
    under the same filename `tottag.py` would have chosen, and four real downloads are byte-identical to the
    Python tool's.
13. **~~The web dashboard never asks for lost pages back~~** (§19.3). It now runs the same three repair
    rounds `tottag.py` does — and both tools now SAVE the repaired stream rather than the transfer that
    lost the pages (§19.3.1), which neither of them did before.
14. **~~`tottag.py` could no longer find a tag over BLE~~** (§19.1). The firmware began advertising
    `TotTag-XX`; the Python scanner matched the name by equality. Both tools match by prefix now, and a
    parity test reads the firmware source and fails if either stops.
15. **~~A multi-day run~~** — four of them (§15.8, §15.12, §15.13, §15.16).
16. **~~Transient task lateness~~** — the counter is in the log (§15.15), which is what made §17 findable.
17. **~~`battery_monitor_get_suppressed_edge_count()` unreadable from a log~~** — in the log (§15.15).
    Deliberately *not* on the live-stats characteristic: that service is what a researcher's tooling reads,
    and a debug counter does not belong in it.
18. **~~Measure whether the WSF pools run dry~~** — failure count, largest failed request and per-pool
    high-water marks are all in the log (§15.15). The failure count is trustworthy; the high-water marks are
    item 2.
19. **~~Decide whether to turn record framing on~~** — on (§15.15). Pages carry `'TTP2'` and a reader can
    step over a record type it has never heard of. Validated over 640 device-hours (§15.16).
20. **~~The IMU teardown runs on the BLE task~~** — handed off to the app task (§15.15).
21. **~~The 500 ms `NETWORK_FOUND` timer~~** — fixed and validated (§15.11, §15.13).
22. **~~Confirm the charger de-bounce~~** — four charging records per device over 61.5 device-hours
    (§15.13), and four again over 640 device-hours (§15.16), against AE's 19,677 in §15.8.

**Won't fix, deliberately and permanently.**

23. **`configASSERT` cannot be enabled, so `RESET_DIAGNOSTIC_ASSERT` is permanently dead.**
    `vTaskStepTick()` asserts `configASSERT( uxSchedulerSuspended )` and Ambiq's tickless implementation
    calls it from the idle task with the scheduler running, so the assert fires on an ordinary wake-up.
    Enabling it means forking `vPortSuppressTicksAndSleep()`, which is not worth one diagnostic code. The
    consequence to remember: **thirteen of the fourteen reset diagnostics can fire, not fourteen**, so an
    assertion failure would present as a hard fault or as "nothing recorded a cause".
24. **A duplicate time anchor on every warm boot** (§15.12). `StorageTask`'s boot anchor and
    `TimeAlignedTask`'s first-pass anchor land in the same 10 ms RTC tick and produce a byte-identical
    record. 9 bytes per reboot, and the host merges the pair. Cosmetic; not being fixed.
25. **rev M hardware validation and worn-media blocking time.** No further rev M devices are being
    manufactured, so the `REVISION_ID < REVISION_N` path stays compile-verified only, and re-measuring
    erase/program timing on aged media is not going to happen. Both are removed from the qualification list
    rather than left to look outstanding.
26. **`-flto`, `-ffast-math`, `-fsingle-precision-constant`.** LTO's gain is limited because the HAL and BSP
    are prebuilt archives, while its risk sits exactly where this firmware has already lost time — weak-alias
    vector overrides, `__attribute__((optimize("O0")))`, section placement and `--gc-sections`.
    `-ffast-math` licences reassociation over the floating-point constants the ranging timing derives from.
    `-fsingle-precision-constant` silently retypes double literals, and `DW_PREAMBLE_LENGTH_US` is a
    double-precision constant expression feeding `DW_SFD_TO` and `RECEIVE_EARLY_START_US`. The correct
    equivalents are deliberate `f` suffixes at chosen sites, not a global switch.

### 15.8 Experiment 20 — the 3.9-day run: the storage work closes, the hang does not

**Question.** §15.6 observed zero watchdog resets in 77 device-hours and said honestly that this had a
one-in-seven chance of happening even if nothing had been fixed. §15.7 item 2 asked for a multi-day run to
give that result weight. This is it.

**Method.** Four devices (02, 3E, AE, F3), revP, ranging, `use_daily_times` off, offloaded over BLE on
2026-08-31. 02, 3E and F3 ran 87.0 h each; AE was powered up 7.9 h earlier and ran 94.9 h, for **356
device-hours** in total. Files are `newtest/{02,3E,AE,F3}_1787839200.ttg`, stream format version 1
(`NANDLOG_RECORD_FRAMING` is 0 in this build).

#### The storage layer: nothing was lost, by any measure available

| | 02 | 3E | AE | F3 |
|---|---|---|---|---|
| pages advertised / delivered | 3036 / 3036 | 3029 / 3029 | 3045 / 3045 | 3058 / 3058 |
| holes / CRC failures / short pages | 0 / 0 / 0 | 0 / 0 / 0 | 0 / 0 / 0 | 0 / 0 / 0 |
| sequence gaps / duplicates | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| records | 612,699 | 623,179 | 620,474 | 619,102 |
| backward steps in write order | **0** | 1 (0.58 s) | **0** | 1 (2.5 s) |
| 300 s cadence captured | 101.5% | 100.9% | 101.3% | 100.9% |

**12,168 pages and 2,475,454 records with zero losses of any kind.** Sequence numbers are contiguous from 0
with no duplicates, no page failed its CRC, no page decoded short of its advertised `record_count`, and no
transfer truncated. The cadence figures exceed 100% because of the extra per-boot anchor; the underlying
300 s heartbeat is fully captured on every device.

Three independent cross-checks say the same thing from outside the format:

- **Cross-device agreement.** A range between two devices is one physical measurement logged at both ends.
  Across all six links, 99.4–99.8% of ranging timestamps appear in both logs, and of those **99.9–100% agree
  within 100 mm**. The network's shared clock is intact for essentially the whole deployment.
- **Host round-trip.** For all four devices the `.pkl` row count equals the number of *distinct timestamps*
  in the `.ttg` exactly. Nothing is lost between the wire and the analysis product.
- **Same-type collisions.** 6 to 36 per device, and **every one is an anchor or a charging record** — zero
  ranging collisions, which is what §12.28 D predicted once millisecond timestamps landed.

#### Reboots: 25 in total, and every one is explained

| device | reboots | `SW Power-On` | watchdog |
|---|---|---|---|
| 02 | 9 | 5 | **4** |
| 3E | 5 | 5 | 0 |
| AE | 10 | 7 | **3** |
| F3 | 5 | 5 | 0 |

The 22 `SW Power-On` resets are not mysterious and not the daily-times cycle: **each one is preceded by a
charging record exactly 3.7 s earlier**, and they occur at the same wall-clock instant on all four devices.
Plugging or unplugging the charger reboots the tag, by way of `storage_flush_and_shutdown()` in the
`APP_NOTIFY_BATTERY_EVENT` handler. Every one costs **exactly 3.7 s** of records, on all 22 — a flat,
reproducible number that is the graceful flush-and-reset path working.

*The "3.7 s earlier" is load-bearing, not decorative.* Every boot also writes its current charge state as one
of its first records, so a charging record lands on the **same millisecond** as every reset of every kind,
watchdog resets included. Keying on that instead of on the preceding transition labels all seven watchdog
resets as charger events — which is exactly what the host analysis did on its first pass over this data
(§16.2).

**Offset recovery is exact across all 25.** Comparing the log clock against the device's own un-offset clock
across every reboot gives **+0.00 s unaccounted, every time**. §12.24's offset recovery, which §12.27
validated across 42 reboots, holds across 25 more including seven that followed a hang.

#### The watchdog: rate unchanged, damage down sevenfold

| | watchdog resets | device-hours | per device-hour | mean silence before reset | run time lost |
|---|---|---|---|---|---|
| §12.27, 4-day | 8 | 317 | 0.025 | 1080 s | 3.03% |
| second 4-day | 10 | 394 | 0.025 | 214 s | 0.60% |
| §15.6, 22-hour | 0 | 77 | 0 | — | 0% |
| **§15.8, 3.9-day** | **7** | **356** | **0.020** | **159 s** | **0.087%** |

**This is the result the run exists to report, and it is half good news.** The previous rate of 0.025 predicts
8.9 events in 356 device-hours; seven were observed. The stall is not fixed, and §15.6's zero was the
one-in-seven. What the rebuild did fix is the cost: `RESVAL` 56 → 8 took the mean outage from 1080 s to 159 s
and the fraction of run time lost from 3.03% to **0.087%**, a 35-fold reduction. Total data lost to hangs
across the whole deployment is **18.6 minutes**.

**The resets are not spread out. All seven fall inside one three-hour window** (2d 04:49 to 2d 07:49) on two
of the four devices; 3E and F3 ran 87 hours each without one. Treating this as a Poisson process at
0.02/device-hour is therefore wrong — it is a burst, and the burst has a context.

#### Two distinct failure signatures, and the diagnostics separate them

**02 — `stalled: BLETask`, four times in 41 minutes.** The silence before each reset is 84.6, 96.5, 28.9 and
76.1 s: **all under `STORAGE_FLUSH_TIMEOUT_S`**, so what was lost is the unflushed RAM page and nothing more.
The device kept logging normally right up to the reset, which is exactly what a single-task stall should look
like — BLETask stopped checking in, `system_watchdog_pet()` declined and named it, and ~167 s later the
hardware reset a device that was otherwise healthy. **The watchdog and the diagnostic both did precisely their
job, and this is the first time the log has named the stalled task.**

The context is the part worth keeping. Bucketing the window in ten-minute bins:

```
              02                3E             AE              F3
2d 4:00   rng 420 pF3      rng 0 p-       rng 0 p-       rng 420 p2     <- 02<->F3 only, at 1/3 rate
2d 5:20   rng 218 pF3      rng 0 p-       rng 0 p-       rng 237 p2     <- all four resets in here
2d 5:30   rng 1042 p3E,F3  rng 1023 p2    rng 0 p-       rng 29 p2      <- network re-forms
2d 5:40   rng 1205 p3E     rng 1205 p2    rng 0 p-       rng 0 p-       <- resets stop
```

02's stalls occur only while it is ranging with a single peer and only about a third of rounds are returning
results, and they stop within minutes of the network re-forming at full rate. §15.7 item 5 already names the
BLE stop/start churn in `verify_app_configuration()` as an all-day cost; this says it is also a correctness
hazard in the partial-network state. See §15.9 for the mechanism.

**AE — no cause recorded, three times over three hours.** The silence is 299.3, 283.1 and 245.3 s: **all well
past the 120 s flush window**, so AE stopped executing rather than merely stopping BLE. And the diagnostic
came back holding the boot marker, which by §15.3's design means something specific: **no task ever declined
a pet.** A declined pet is what records a stall code, and recording requires some monitored task to call
`system_watchdog_pet()`. If nothing calls it, nothing is recorded.

So AE's signature is *every* monitored task stopping at once — a stopped tick, a stopped scheduler, a task
above them all spinning without yielding, or an interrupt source consuming the core. §15.2 chose deliberately
not to write a separate stopped-clock code on the grounds that a dead clock schedules nothing and therefore
pets nothing; the consequence, which is worth stating plainly, is that **the boot marker surviving a watchdog
reset now *is* the stopped-everything signature**, and it cannot distinguish between the causes listed above.

*Two candidate causes are ruled out by the build, not by argument.* `configCHECK_FOR_STACK_OVERFLOW` was **2**
for this run, so `vApplicationStackOverflowHook()` was live and would have recorded
`RESET_DIAGNOSTIC_STACK_OVERFLOW`. And there is no heap: `configSUPPORT_DYNAMIC_ALLOCATION` is **0**, so
`pvPortMalloc` is not compiled, no `malloc` exists anywhere on the device, and the BLE stack allocates from a
static `WsfBufInit` pool instead. `RESET_DIAGNOSTIC_MALLOC_FAILED` and `vApplicationMallocFailedHook()` are
therefore unreachable by construction — see §15.9.

#### Two device-level findings unrelated to storage

**AE emitted 19,677 spurious `NotCharging` records at ~2 Hz.** Confined to 01:07–03:55, before the other three
devices joined and while AE was unplugged and discharging; it stops at AE's 03:55 reboot and never returns.
The other three devices logged 11 to 19 charging records for the entire deployment. `charging_status_changed()`
in `battery.c` re-reads the pin, re-arms the opposite edge and then fires the callback **unconditionally**,
with no comparison against the last reported state and no debounce, so a chattering charge-status pin becomes
one record per edge. §15.9 covers the fix and why this may not be a separate finding from AE's stalls at all.

**Half of every log is an idle device.** Motion-gating means all four devices spent ~50% of the deployment
neither ranging nor BLE-scanning, writing only the 300 s anchor-plus-voltage pair — each in its own 4 KB page,
525 to 588 such pages per device. This is the §6.2 "idle" row behaving exactly as documented (~1.15 MB/day,
against ~356 days of array life) and it is not a defect, but it does mean roughly a third of the pages in a
real deployment file carry 18 bytes each.

#### Verdict

**The storage redesign is done.** Every failure mode §1 was written about is closed, and this run exercised
the closed versions for 356 device-hours across 12,168 pages without a single lost, corrupt, reordered,
truncated or misattributed byte. The remaining open work is the hang, and it is a ranging/BLE defect that the
storage layer is now good enough to have caught, measured and localised to one task on one device.


### 15.9 What the 3.9-day run says about the hang

§15.8 is the evidence; this is what follows from it. Everything here is a hypothesis with a named test, not a
result.

**1. `verify_app_configuration()` runs at 2 Hz whenever the device is scanning.** The rate is set by a
timer, not by traffic: `am_timer03_isr` raises `APP_NOTIFY_NETWORK_FOUND` every
`BLE_SCANNING_TIMER_TICK_RATE_HZ / 2` — **500 ms** — restarted by `ble_discovery_handler()` on the first
discovery of each window. The handler for that notification then calls `bluetooth_stop_scanning()`
unconditionally at the top, writes a BLE-scan record, and calls `verify_app_configuration()` at the bottom,
which restarts scanning whenever the device is not ranging or is master.

*(A first draft of this section said "once per received advertisement". That is wrong: in the participant
path `ble_discovery_handler()` only accumulates peers into `discovered_devices` and clears the timer, and it
raises the notification directly only in the master path, for a higher-UID master. The rate is the timer's.
The conclusion is unchanged, but the mechanism is a fixed 2 Hz rather than traffic-proportional, which
matters — it means the cost does not fall when the radio is quiet.)*

**One BLE-scan record in the log is one execution of that path**, which makes the log a direct count of it:
312,419 executions on device 02 over 87 hours. Each one can enter a `vTaskDelay(10)` polling loop up to
`BLE_ADV_TIMEOUT_MS`, and on failure a `bluetooth_reset()` plus a `BLE_INIT_TIMEOUT_MS` loop, and on failure
of *that* a `system_reset(false)`.

A full scan stop-and-restart twice a second is the regime the device is in exactly when it is discovering
peers but not successfully ranging with them — which is the state 02 was in for the whole window in which it
took four BLETask stalls. **This is the single most likely mechanism and it is cheap to confirm:** count
executions and BLE state transitions and expose the counter over the live-stats characteristic, alongside the
decline counter §15.7 item 1 already asks for.

**2. There is no "reduced ranging rate", and the distinction matters.** `SCHEDULING_INTERVAL_US` is fixed at
500 ms and the scheduler runs every round unconditionally while `is_running`. A `RANGES` record is written
only when `ranging_results[0]` is non-zero — `handle_range_computation_phase()` guards the store on it. So
"420 records per 10 minutes instead of 1205" is not a slower schedule; it is **~65% of rounds returning no
ranges at all** at an unchanged 2 Hz. Any reproduction has to force round *failure*, not round spacing.

**3. Round failure and network loss are separated by one counter.** `fix_network_errors()` increments
`empty_round_timeout` only when a round yields both no detected devices *and* no ranging results; anything
detected resets it. At `MAX_EMPTY_ROUNDS_BEFORE_STATE_CHANGE` (6 rounds, 3 s) it clears `is_running`, which
raises `APP_NOTIFY_NETWORK_LOST` → `verify_app_configuration()` → scanning restarts → an advertisement arrives
→ `APP_NOTIFY_NETWORK_FOUND` → scanning stops → `ranging_begin()`. **A link good enough for BLE and marginal
for UWB drives that cycle indefinitely**, which is the reproduction.

*Three ways to force it, cheapest first.*

- **Shrink the counter.** Set `MAX_EMPTY_ROUNDS_BEFORE_STATE_CHANGE` to 1 so a single empty round drops the
  network. Two tags at ordinary desk range lose enough rounds to churn the cycle many times a minute instead
  of once an hour, which compresses §15.8's four-hour window into minutes. One `#define`, no RF work, and it
  amplifies exactly the loop under suspicion.
- **Attenuate UWB but not BLE.** BLE at 2.4 GHz reaches much further than the DW3000 link in practice, and
  revP carries them on separate antennas, so absorber over the UWB antenna alone — or simply moving the tags
  to the edge of UWB range in a room where BLE still carries — produces the real thing rather than a
  simulation of it. Slower to set up and harder to reproduce exactly, but it is the actual field condition.
- **Drop results in software.** A debug-only `#define` that discards `compute_ranges()` output on a fraction
  of rounds gives a deterministic, dial-able failure rate. Most faithful to "65% of rounds empty" and the
  right choice if the first option turns out to change the timing too much to be representative.

**4. The charging storm and AE's undiagnosed stalls may be one finding.**
`charging_status_changed()` runs at `NVIC_configKERNEL_INTERRUPT_PRIORITY`, above every task, and does real
work: a pin read, an `am_hal_gpio_pinconfig()` register write to flip the edge, a `signal_charge_complete()`
tristate update, and a callback into `app_notify()`. At 2 Hz that is invisible. At a few kHz it is the core.

The chain is speculative but every link is observable in the data: **AE is the only device that showed
charge-pin chatter, and the only device whose watchdog resets recorded no cause.** A charging record is
written by `AppTask`, so a storm fast enough to starve `AppTask` **stops producing records** — which means the
absence of charging records during AE's stalls is what the hypothesis predicts, not evidence against it. And
total task starvation by an ISR is precisely the signature §15.8 describes: no task runs, so no task declines
a pet, so the boot marker survives.

*The test is the fix.* Edge-filter the battery events (below), re-run multi-day, and see whether AE's
undiagnosed resets go with them. If they do, the two findings were one. If they do not, the field has been
narrowed to a stopped tick or a spinning high-priority task, and a GPIO interrupt counter over live-stats will
separate those.

**5. Edge-filtering the battery events. ✅ IMPLEMENTED.** Both ISRs re-read the pin, re-arm the opposite
edge, then call the callback with the state they just read. Neither compared against the state it last
reported, so a bouncing pin produced one event per edge. All three changes below are in tree.

- **Latch and compare (required).** `signal_change_accepted()` in `battery.c` holds a `charger_signal_t` per
  signal, with `reported` initialised to `-1` so the first observation of each always reports — which
  preserves the existing behaviour of recording the charge state once at boot. The callback fires only when
  the freshly read state differs from the latch. This alone turns 19,677 records into the two or three real
  transitions, because chatter re-reports the same state, and it cannot suppress a genuine transition,
  since a genuine transition differs from the latch by definition.
- **Debounce in time (recommended).** A latch alone still lets a pin oscillating between two *different*
  states run the ISR at full rate, and it is that rate — not the record count — that competes with the
  scheduler. A change accepted within `BATTERY_EVENT_DEBOUNCE_MS` (250 ms) of the previous one is suppressed,
  timed off the free-running STIMER so the check is ISR-safe and lock-free.

  **The window defers rather than drops, which is the part that makes it safe.** A suppressed change leaves
  the latch untouched, so the change is still outstanding and the next edge reports it once the window has
  passed. The hole that leaves — a pin that goes quiet immediately after a suppressed edge — is closed by
  `battery_monitor_poll_charger_state()`, called once per loop from `TimeAlignedTask`, which reconciles the
  latches against the pins and emits anything outstanding. Worst-case latency for a real transition is
  therefore one 300 s loop rather than unbounded, and no transition can be lost.
- **Fold the deferred value correctly.** `pending_battery_event` was a single `volatile` slot behind a
  notification *bit*, so two events arriving before the app task ran left only the last — a fast
  plug-then-unplug logged only the unplug. It is replaced in **both** `app_task_ranging.c` and
  `app_task_maintenance.c` by three latches: plugged state, charging state, and a one-shot critical-voltage
  flag, each claimed under `AM_CRITICAL` so an interrupt landing mid-read cannot have its event dropped.
  Charging state is written before plug state so a transition and the charge state it caused appear in that
  order.

The edge re-arming is unchanged and still runs on *every* edge, reported or not, so the pin stays armed for
the next real transition. Dual-edge interrupts are unavailable on this part by errata, so that flip is
load-bearing; the bug was the unconditional callback beside it.

`battery_monitor_get_suppressed_edge_count()` exposes how many edges the de-bounce discarded. A healthy
deployment records single digits of charging events in a whole log, so **any non-zero value is a bouncing
pin** — which makes it the direct test of whether AE's hardware is at fault, and the measurement that decides
whether §15.9 item 4's charger-storm hypothesis for AE's undiagnosed stalls holds.

**6. Two of the fourteen diagnostic codes can never fire, and the fix for one of them found a real bug.**
**RESOLVED — see §15.10.**

`RESET_DIAGNOSTIC_MALLOC_FAILED` and its `vApplicationMallocFailedHook()` were unreachable **by
construction, not by configuration**. `FreeRTOSConfig.h` sets `configSUPPORT_DYNAMIC_ALLOCATION 0`, so
`pvPortMalloc()` is not compiled at all; no `heap_*.c` is in the build; nothing in `src/` calls `malloc`
outside the host-only `nandlog` simulator; and the BLE stack allocates from the fixed `WsfBufInit` pool in
`ble_task.c` rather than from a heap. Turning on `configUSE_MALLOC_FAILED_HOOK` would have changed nothing —
there is no allocator to fail.

*An earlier note here recommended enabling that hook. That was wrong and is withdrawn.* The enum member has
instead been repointed at the allocator this firmware actually has: the WSF buffer pools. §15.10 records
what looking into that turned up, which is more than a dead diagnostic.

`RESET_DIAGNOSTIC_ASSERT` remains dead for a different and more recoverable reason: `configASSERT` is
disabled (§15.7 item 3). That one becomes live the moment the tickless-idle assert is dealt with.

*By contrast, `RESET_DIAGNOSTIC_STACK_OVERFLOW` was live for this run* —
`configCHECK_FOR_STACK_OVERFLOW` is 2 — so a stack overflow is ruled out as the cause of AE's undiagnosed
stalls rather than merely unobserved.


### 15.10 The WSF buffer pools, and a download that can stop forever

Chasing the dead `RESET_DIAGNOSTIC_MALLOC_FAILED` code from §15.9 item 6 turned up a real defect on the
offload path. **It is not the hang, it is not what §15.8 observed, and it has never happened in any recorded
run.** What landed is therefore observability and nothing else; the recovery that was written for it was
removed, and why is recorded below because the reasoning generalises.

**There is no heap, but there is an allocator.** Every BLE packet, event and inter-layer message comes from
five fixed pools declared in `ble_task.c`: 8 x 16 B, 4 x 32 B, 6 x 64 B, 14 x 280 B, 8 x 424 B, forty buffers
in total. `WsfBufAlloc()` walks them for one large enough and returns NULL when there is none.

**A NULL is reported to nobody.** `AttsHandleValueNtf()` is `void`. Following it down: `attsHandleValueIndNtf()`
wraps the send in `if ((pMsg = WsfMsgAlloc(...)) != NULL)` and, on failure, falls off the end with `pktSent`
still `FALSE` and no callback made. The caller cannot tell a sent notification from a dropped one. On a
production build the stack's own `WSF_TRACE_WARN` is compiled out, so there is not even a console line.

**And the log download clocks itself off exactly that notification.** The chain is:

```
continueSendingLogData()  ->  AttsHandleValueNtf()  ->  packet queued
                                        |
                          ATTS_HANDLE_VALUE_CNF  ->  attProtocolCallback()  ->  continueSendingLogData()
```

The confirmation for one chunk is what asks for the next. So a single failed allocation ends the transfer:
no packet, therefore no confirmation, therefore nothing ever calls `continueSendingLogData()` again.

**Nothing on either side notices.**

- *The device stays healthy.* `BLETask` goes back to blocking in `wsfOsDispatcher()`, which is its normal
  idle state. Its watchdog check-in is a WSF timer that allocates nothing, so it keeps checking in and the
  watchdog correctly does not fire. There is no fault to detect — the device is simply no longer being asked
  to send anything.
- *The host waits forever.* `tottag.py` has **no download timeout at all**. `download_log_continuation()`
  exists and issues `MAINTENANCE_DOWNLOAD_LOG_CONTINUE`, but its only caller is `reconnect_to_tottag()` — it
  is reached after a BLE *disconnect*, not after a stall. A connection that stays up and goes quiet is a
  hang, and the only recovery is for a person to notice and disconnect.
- *Retransmission cannot help.* It repairs pages the device admitted it would send. A stream that stops
  mid-transfer never advertises the rest.

**Is it reachable?** The tightest pool is the 8 x 16 B one, and the notification path needs one small message
plus one ~280 B packet buffer per chunk. Sustained pressure comes from the download itself at one chunk per
connection interval, and from the 2 Hz scan stop/start churn in §15.9 item 1, each cycle of which generates
HCI commands and DM events out of the same pools. Whether the two together are enough has never been
measured, which is the point: **nothing in this firmware could have told you either way.**

#### Scope, honestly stated

**The catastrophic consequence is offload-only.** Allocation failures can happen at any time — every received
advertisement allocates a `DM_SCAN_REPORT_IND`, every HCI event allocates, and the 2 Hz scan stop/start churn
in §15.11 allocates twice a second all day. But everywhere except the download, a dropped allocation is
**self-healing**, because the thing that would have used it is driven by an external event that repeats: a
missed advertisement is re-sent by the peer a second later, a missed scan report costs one discovery. Only the
download turns a dropped allocation into an unrecoverable state, and it does so because it is the one path
that is *self-clocked* — its own output is its only input.

**So this cannot be behind the §15.8 stalls.** Those happened during ordinary ranging with no BLE connection
open and no download in flight. This section describes a real defect on a path that was not being exercised.

**And it has never been observed.** Every download in every run recorded here completed. The pools have
never been measured under load, which is the actual gap.

#### What was implemented — observability only

**1. Count the failures (`bluetooth.c`, both Makefiles).** `WSF_OS_DIAG` and `WSF_BUF_STATS` are defined in
the build, a `WsfBufDiagRegister()` callback counts every allocation failure with the largest failed request
size, and `bluetooth_get_buffer_stats()` / `bluetooth_print_buffer_stats()` expose that alongside per-pool
high-water marks. The stats print at the end of every download. **This adds no behaviour** — a counter on a
path that currently does nothing at all.

> *`WSF_OS_DIAG` does not build as shipped.* `wsf_os.h` declares `extern wsfHandlerId_t WsfActiveHandler` and
> `WsfBufAlloc()` reads it on the failure path, but nothing in this SDK's FreeRTOS port ever defines it, so
> enabling the feature fails to link with `undefined reference to WsfActiveHandler`. It is defined in
> `bluetooth.c` and left at `WSF_INVALID_TASK_ID`: keeping it current would mean patching the dispatcher, and
> which handler lost the allocation matters far less than that one was lost.

> ⚠️ *`WSF_BUF_STATS` changes `sizeof(wsfBufPool_t)` from 12 bytes to 16, and that type is private to
> `wsf_buf.c` while `g_pui32BufMem` in `ble_task.c` is sized by a hand-written formula that reserves
> `WSF_BUF_POOLS * 16` for it.* The formula was written against the stats-on size, so enabling stats consumes
> slack that was already there and nothing shrinks — but that is luck, not design, and the reservation is now
> exact. The `WsfBufInit()` return check that guards it previously only called `print()`, which is a no-op in
> production; it now also records a diagnostic, so a pool array that silently shrank would show up as a
> logged fault rather than as an intermittent BLE problem.

**2. Repoint the dead diagnostic.** The failure callback calls
`system_record_diagnostic(RESET_DIAGNOSTIC_MALLOC_FAILED)`. The code now means what its name always implied,
against the allocator this firmware actually has, and an exhaustion followed by a reset survives into the
reset record where §12.21 reports it.

#### What was implemented and then removed — the recovery

A first attempt added a WSF stall timer that re-sent the last chunk when no confirmation arrived. **It was
removed before any test run**, for three reasons that are worth keeping:

- **The retry can corrupt the stream.** `repeat` re-sends `previous_buffer`, and the host's `data_callback`
  appends every chunk at `self.data_index` with **no de-duplication**. If the notification actually went out
  and only its confirmation was delayed, the resent chunk is appended twice and every page frame after it is
  misaligned. Truncation — the failure being fixed — is reported cleanly by `parse_v2`; a duplicated chunk
  silently destroys the rest of the file. The cure was worse than the disease.
- **The existing `DOWNLOAD_LOG_CONTINUE` path has the same flaw**, so it is not a foundation to build on. It
  works today only because its one caller runs after a *disconnect*, where the last chunk was probably lost.
  A correct resume needs the host to tell the device where it got to — a protocol change, made deliberately.
- **It changes BLE behaviour to fix something never observed**, immediately before a multi-day run whose
  entire purpose is to characterise a different BLE fault. Behaviour changes on the path under investigation
  are noise in that experiment.

*The narrow window in which the retry would have been safe is worth recording, because it argues the timer
was nearly right.* `attsHandleValueIndNtf()` issues `ATTS_HANDLE_VALUE_CNF` immediately after
`attL2cDataReq()` — locally, with no over-the-air acknowledgement — so on a healthy transfer the confirmation
is microseconds behind the send, and a multi-second timeout could essentially only fire when the packet was
never queued. The exception is `ATT_CCB_STATUS_FLOW_DISABLED`, where the callback is deferred to a pending
list and would arrive *after* a retry had already gone out. That is the corruption path, and it is enough.

#### How to demonstrate the defect

```sh
cd software/firmware/tests
make clean && make full BOARD_REV=P EXTRA_DEFINES=-DBLE_TEST_STARVE_BUFFERS_AT_CHUNK=50
```

Just before chunk 50 of the next download, every pool is drained largest-first so that no subsequent request
can be satisfied, which makes the following `AttsHandleValueNtf()` fail for real rather than by simulation.
Expected: the transfer stops dead at chunk 50, the host waits with no error, and the console shows the
failure counter incrementing. **The device must be reset afterwards** — there is no recovery, which is the
point of the demonstration. `EXTRA_DEFINES` is new in both Makefiles; the gate defaults to 0 and compiles to
nothing, verified by its strings being absent from a default build.

### 15.11 The 500 ms `NETWORK_FOUND` timer, and what it is really doing

§15.9 item 1 named this as the leading mechanism behind the `stalled: BLETask` resets. Tracing it properly
turns up something more concrete than a suspicion: **more than half of every deployment log is a duplicate.**

**How it is wired.** `BLE_SCANNING_TIMER_NUMBER` (timer 3) is configured once in `AppTaskRanging` as
`AM_HAL_TIMER_FN_UPCOUNT` with `ui32Compare0 = BLE_SCANNING_TIMER_TICK_RATE_HZ / 2`, enabled, and never
stopped. The Apollo4 register documentation is explicit that UPCOUNT is a *repeating* mode — "this mode is
run up counter generating a pulse on CMP... Timer repeats for TMR_LMT iterations" — and `ui32PatternLimit`
is left at the HAL default of 0. So `am_timer03_isr` fires **every 500 ms, from boot, forever**, regardless
of whether anything was discovered.

**What it was evidently meant to do.** Two pieces of the discovery handler only make sense if the timer were
one-shot: `devices_found` is set on the first discovery of a window and cleared by the handler, and that same
first discovery calls `am_hal_timer_clear()` to restart the count. Read together, the intent is plainly
*"once something is discovered, wait 500 ms to collect the rest of the batch, then act on it"* — a settling
window armed by the first discovery. A free-running counter does not implement that.

**What it actually does, every 500 ms.** The `APP_NOTIFY_NETWORK_FOUND` handler:

1. calls `bluetooth_stop_scanning()` unconditionally;
2. may call `ranging_begin()` off whatever is currently in `discovered_devices`;
3. writes a `BLE_SCAN` record from `discovered_devices` and `num_discovered_devices`;
4. calls `verify_app_configuration()`, which restarts scanning whenever the device is not ranging.

And `num_discovered_devices` is **never reset to zero** — it is only ever assigned 1 on a fresh discovery or
incremented. So once it is non-zero it stays non-zero, and step 3 re-emits the last peer set indefinitely.

**The logs say exactly this, unambiguously.** Across the four §15.8 devices:

| device | BLE_SCAN records | identical to predecessor | empty |
|---|---|---|---|
| 02 | 312,419 | **311,427 (99.7%)** | 1 |
| 3E | 314,495 | **314,342 (100.0%)** | 3 |
| AE | 328,248 | **322,655 (98.3%)** | 20,164 |
| F3 | 314,115 | **313,690 (99.9%)** | 1 |

The inter-record gap histogram is 500 ms with 490/510 shoulders — the RTC's 10 ms quantisation around a
500 ms period, not a traffic-driven distribution. And AE's 20,164 empty records are the same mechanism seen
from the other side: a device with `num_discovered_devices` still at its initial zero, dutifully logging an
empty scan result twice a second.

**So: yes, a bug, and its side effects are the interesting part.**

- **Log volume.** BLE_SCAN is the single largest record type in every file — 51% of 02's records, roughly
  2.5 MB of a 6.9 MB download — and essentially all of it is the same value repeated. Removing it would cut
  a deployment file by about a third and shorten every offload proportionally.
- **A real BLE stop/start cycle at 2 Hz.** `bluetooth_stop_scanning()` no-ops when not scanning, but when the
  device *is* scanning — which is most of the time it is not ranging — it issues a genuine `DmScanStop()`,
  the `DM_SCAN_STOP_IND` comes back, and `verify_app_configuration()` then issues `DmScanStart()`. Two HCI
  command/event round trips per second, every second, through the same WSF pools as §15.10.
- **`AppTask` woken twice a second forever**, on a battery-powered device, to do work that is almost always
  redundant.
- **Ranging decisions taken on stale data.** Step 2 can call `ranging_begin()` against a peer list left over
  from an earlier window. In a degraded network — precisely the state 02 was in for its four stalls — that is
  a 2 Hz join-attempt loop against peers that may no longer be there.

#### The fix, as applied

**The guard goes in the ISR, not the handler.** `am_timer03_isr()` now notifies only when `devices_found` is
set — that is, only when a discovery actually opened a window. Testing the flag *is* the missing one-shot
behaviour: the flag is raised by the first advertisement of a window and lowered once the window has been
processed, and that same first advertisement already calls `am_hal_timer_clear()` so the 500 ms is measured
from it. Guarding at the ISR rather than in the handler matters because an unconditional notification wakes
the app task twice a second forever, and the wake-up is most of the cost.

The master path is untouched: a master that discovers a higher-UID master notifies directly from
`ble_discovery_handler()` and never depended on this timer.

`num_discovered_devices` needed no separate fix. The handler now only runs after a fresh window, and a fresh
window always begins in the `!devices_found` branch, which resets the count to 1 — so the stale-list problem
disappears as a consequence rather than needing its own change.

**A slow configuration check was kept, because removing a poll should not silently remove what it was
incidentally covering.** The BLE state machine is push-driven — `DM_ADV_STOP_IND` and `DM_SCAN_STOP_IND`
retry three times and then `escalate_to_app_task()` raises `APP_NOTIFY_VERIFY_CONFIGURATION` — so the 2 Hz
poll was never the primary recovery path. But a state that goes wrong with *no* DM event to announce it was
previously noticed within 500 ms, and would otherwise now wait for the 300 s check from `TimeAlignedTask`.
The ISR therefore raises `APP_NOTIFY_VERIFY_CONFIGURATION` every `BLE_CONFIG_VERIFY_WINDOWS` (120) idle
windows — once a minute — which is a 120x reduction against the old rate while staying five times more
responsive than the 300 s fallback. An `#error` guard stops that constant being set low enough to reinstate
the churn.

**The timer's hardware configuration is unchanged.** `ui32Compare0` is now written as
`(BLE_SCANNING_TIMER_TICK_RATE_HZ / 1000) * BLE_DISCOVERY_WINDOW_MS` so the 500 ms has a name instead of
being a bare `/ 2`; at the 6 MHz tick rate both expressions evaluate to exactly 3,000,000, so the timer
behaves identically and only the ISR's decision to notify changed.

#### Expected behaviour after the change

| Device state | Before | After |
|---|---|---|
| Ranging (scanning off, no discoveries) | 2 records/s | **none** |
| Scanning, peers present | 2 records/s, ~all duplicates | one record per discovery window, so roughly **1/s** at the 100 ms / 1000 ms scan duty cycle |
| Scanning, nothing in range | 2 empty records/s | **none** |
| Idle configuration check | every 500 ms | every 60 s |

A `BLE_SCAN` record finally means what its name says — *the peers seen in one scan window* — rather than a
periodic re-statement of the last thing seen. On a file like 02's this should take ~312k BLE records to
somewhere in the 50-80k range, shrinking a deployment file by roughly a third and shortening every offload
proportionally. That is an estimate from the scan duty cycle, not a prediction; the measured value from the
next run is what settles it.

The second-order effects are the reason for doing it: the 2 Hz `DmScanStop()`/`DmScanStart()` HCI round trip
goes away, the app task stops waking twice a second, and `ranging_begin()` stops being called against stale
peer lists — which is the part that plausibly bears on §15.8's `stalled: BLETask` resets.

#### A preemption race in the same handler, fixed alongside it

Looking at the handler closely enough to guard it surfaced a second defect that had nothing to do with the
timer. `ble_discovery_handler()` runs on the **BLE task at priority 3**, against the app task's **2** — it is
reached from `deviceManagerCallback()` on `DM_SCAN_REPORT_IND`, which the WSF dispatcher calls in task
context, not from an interrupt. So it does not merely interleave with the `NETWORK_FOUND` handler; it
**preempts** it outright, at any instruction.

The handler read the live `discovered_devices` and `num_discovered_devices` throughout, and cleared
`devices_found` in the middle. Three consequences, worst last:

- A discovery landing mid-handler takes the `!devices_found` branch once the flag has been cleared, which
  sets `num_discovered_devices = 1` and **overwrites slot 0** while the handler is still reading it.
- The ranging decisions — the master-demotion scan, the `ranging_device_located` scan, the highest-UID
  search — could each see a different set from the one before it.
- **`num_discovered_devices` was read twice for the log record**: once as the loop bound that fills
  `ble_scan_results`, and again as the length passed to `storage_write_ble_scan_results()`. A count that grew
  between those two reads publishes slots the loop never wrote — so the record reports whatever a *previous*
  window happened to leave in that array as peers seen in this one. That is not a lost record or a duplicate
  one; it is a **fabricated** one, and nothing downstream could tell.

**The fix is to claim the set once.** The handler now copies `discovered_devices` and
`num_discovered_devices` into locals and clears `devices_found`, all inside `AM_CRITICAL` — which masks
interrupts and therefore also prevents the context switch that the higher-priority BLE task would need — and
every read afterwards uses the copy. 70 bytes of stack against the app task's 8 KB.

Clearing the flag inside that same critical section, at the top rather than at the end, is what makes the
window coherent: one window produces one decision and one record, and a discovery arriving from that point
on opens the *next* window and restarts the timer instead of half-joining a set whose decisions are already
taken. A peer landing during the scan stop is therefore acted on 500 ms later rather than immediately, and
is never lost.

*This race predates every change in §15 and is independent of the timer bug*, but it was only reachable when
a discovery coincided with the handler — and the handler used to run twice a second, forever. Fixing the
timer alone would have made it rarer without making it impossible.

**What this does not do.** It does not prove the scan churn was behind the `stalled: BLETask` resets. It
removes the mechanism; whether the stalls go with it is what the run measures.


### 15.12 Experiment 21 — the 30-minute bench check on the discovery-window and charger fixes

**Method.** Four devices, revP, ~30 minutes, `1788298380`. Deliberately hands-on rather than a deployment:
02, 3E and AE were plugged and unplugged twice each to exercise the charger path, and F3 was left untouched
as a control. This is the pre-flight §15.11 and §15.9 item 5 asked for, not a validation run.

**Storage integrity, all four devices: 61 pages, 10,592 records, nothing lost.** Zero holes, zero CRC
failures, zero short pages, zero sequence gaps, zero duplicates, no truncation, no decode breaks. The `.pkl`
row count equals the distinct-timestamp count exactly on every device.

#### The discovery-window guard works, and the size of the effect is the headline

| | 4-day run (§15.8) | this run |
|---|---|---|
| BLE_SCAN records during active ranging | **2.00 / s** | **0.00 / s** |
| BLE_SCAN as a share of all records while ranging | **50.2%** | **0.1–0.2%** |
| total record rate while ranging | ~4.0 / s | ~1.5–2.0 / s |
| consecutive-identical BLE_SCAN records | 99.7–100% | **0** |
| BLE_SCAN records per device, whole run | ~3,600 per 30 min | **3 to 4** |

Every surviving record lands at a network-formation moment and carries a *different* peer set:

```
02   0:00:15  peers=[AE]           <- first discovery after boot
02   0:00:27  peers=[F3, 3E]       <- after the 0:00:26 charger reset
02   0:00:52  peers=[F3, 3E, AE]   <- full network found in one window
02   0:14:01  peers=[F3]           <- after the 0:14:01 charger reset
```

All four fall inside windows where that device was not ranging, which is the behaviour
`verify_app_configuration()` always intended: scanning is off while ranging, so there is nothing to
discover and nothing to report. **No peer UID outside the four real devices appears in any record**, which
is the check the snapshot fix exists to pass.

**The signal that disappeared was never a signal.** It is worth being precise, because "half the records are
gone" invites the question of what was lost. While a device is ranging, scanning is off, so
`discovered_devices` cannot be refreshed — the old firmware was re-emitting a stale set twice a second. The
4-day log shows this directly: at ms 190,065,670 device 02 logged `ble=[AE, F3, 3E]` while the ranging record
340 ms later read `{F3: 4763}` and F3 was its only peer. The old record was not a weaker observation than the
new one; **it was an assertion about the present made from a set last updated minutes earlier.** What was
removed is a fabrication, not a measurement.

#### The charger de-bounce works

| device | plug/unplug actions | charging records | chatter |
|---|---|---|---|
| 02 | 2 plug, 2 unplug | 11 | **0** |
| 3E | 2 plug, 2 unplug | 11 | **0** |
| AE | 2 plug, 2 unplug | 11 | **0** |
| F3 | none (control) | **1** | **0** |

Eleven is exactly right and worth decomposing, because the number looks high until it is: one boot-status
record at first boot, then per plug a `Charging` and a `Plugged`, per unplug an `Unplugged`, and one
boot-status record after each of the four charger-triggered resets. Not one extra edge was reported. Against
AE's 19,677 `NotCharging` records in §15.8, and against F3's single record here, the latch is doing its job.

Every reset is preceded by its charging record 3.6–3.7 s earlier — the same flat, reproducible graceful
flush-and-reset cost as the 4-day run — and the host analysis classifies four of each device's five reboots
as charger-caused, correctly leaving the initial boot as "other".

#### Everything else the run happens to prove

- **Ranging held exactly 2.00 records/s of active time on all four devices** (2.003, 2.003, 2.002, 2.001),
  with 500 ms dominating the interval histogram. Essentially every round produced a result, against 0.94/s
  averaged over the 4-day run and 0.67/s in its degraded window. Every gap over 5 s aligns with a charger
  window, where the device is in maintenance mode and not ranging.
- **All six links: 100.00% of ranging timestamps present in both logs, 100.00% agreeing within 100 mm**,
  against 99.4–99.8% before. Pairwise record counts are exactly symmetric on every link.
- **Zero backward time steps, zero backward page bounds, zero ranging collisions.** Offsets moved 0–170 ms
  over the run and the device clock was monotonic everywhere; the anchor interval reads 299.37–299.38 s.
- **Zero watchdog resets** — which proves nothing at this length. Two device-hours against a historical
  0.02/device-hour predicts 0.04 events, so the absence is expected whether or not anything is fixed.

#### Two minor findings

**A duplicate time anchor on every warm boot.** `StorageTask` writes a boot anchor as its first act, and
`TimeAlignedTask` runs its work immediately on its first pass and writes another. On a warm boot the two land
in the same 10 ms RTC tick and produce a **byte-identical** record — same experiment timestamp, same local
clock. Four to five per device here, one per reboot; the first boot of an experiment does not show it. It
costs 9 bytes and the host merges the pair, so it is cosmetic. Fixable by having `TimeAlignedTask` skip its
first anchor, or by making `storage_write_time_anchor()` drop one that matches the previous.

**`battery_monitor_get_suppressed_edge_count()` cannot be read from a log.** It exists, and it is the
measurement that would separate "AE's pin does not bounce" from "AE's pin bounces and the de-bounce hid it" —
but it is only reachable over a console or a debugger, so a deployment cannot report it. That gap matters
specifically for §15.9 item 4: this run cannot say whether AE's hardware is at fault, only that the log is
now clean either way. Surfacing it over the live-stats characteristic, alongside the decline counter §15.7
item 1 already wants, would close it.

#### What this run does and does not establish

It establishes that both changes do what they were written to do, that neither broke the storage layer, the
clock, the network or the offload, and that the network is healthy at full rate on this bench. It establishes
nothing about the `stalled: BLETask` resets — 30 minutes is two device-hours, and the mechanism under
suspicion needs a degraded network to appear at all. **Cleared to proceed to the overnight run.**


### 15.13 Experiment 22 — the overnight run

**Method.** Four devices, revP, **15.38 h** (61.5 device-hours), `1788301320`, unattended overnight. First
full-length run with the discovery-window guard (§15.11), the charger de-bounce (§15.9.5) and the WSF
counters (§15.10).

**Physical layout, which is the whole key to reading this log** and was supplied by the operator after a
first analysis went wrong without it:

| | placement |
|---|---|
| 3E + F3 | side by side in room 1 |
| 02 | alone in room 2 |
| AE | in the operator's pocket, moving with them, infrequently |
| at t ≈ 5:00 | **3E carried from room 1 to room 2**, next to 02 |

#### The result

**Zero watchdog resets in 61.5 device-hours.** Two resets per device: the initial boot and the plug-in at
download, both `SW Power-On`, each preceded by its charging record 3.7 s earlier.

At the historical 0.0225/device-hour this run predicts 1.38 events, so zero has probability e^-1.38 ≈ **25%**
on its own. Combined with §15.6's 77 device-hours the two zero-reset runs total 138.5 device-hours against an
expectation of 3.12, so **P(zero across both | nothing fixed) ≈ 4.4%**. That is the first result in this
project with real weight behind it.

**The charger de-bounce is validated.** Exactly **four** charging records per device — one boot-status
record, `Charging` + `Plugged` at pickup, one boot-status record after the reset that caused. **Zero chatter
on any device across 61.5 device-hours, AE included**, the device that produced 19,677 `NotCharging` records
in §15.8.

**Storage integrity, again, is total.** 1,768 pages and 260,555 records with zero holes, zero CRC failures,
zero short pages, zero sequence gaps, zero duplicates, no truncation. Clocks monotonic everywhere, offsets
within ±1.5 s over 15.4 h (≈27 ppm relative), one backward step in the whole run. Every `.pkl` matches its
distinct-timestamp count.

**The duplicate-record spam is gone.** 02 logged 87 BLE_SCAN records over 15.4 h, 3E 60, F3 660, against
roughly 110,000 apiece at the old 2 Hz.

#### The ranging data matches the physical layout, measurement for measurement

| link | records | % of 2 Hz | explanation |
|---|---|---|---|
| 3E–F3 | 39,733 | 35.9% | co-located rooms 1, until 3E was moved at 5:00 |
| 02–3E | 77,939 | 70.4% | co-located room 2, from 5:00 to the end |
| 02–AE, 02–F3, 3E–AE, AE–F3 | ~6,800 each | ~6% | setup and pickup only, plus AE's walk-bys |

Every link is exactly symmetric between the two devices' logs, and **whenever two devices were physically
together they ranged at the full 2 Hz continuously** — 602–603 records per 5-minute bin, for 4.5 h and then
8 h without interruption. The 21.9% figure for total pairwise capture is not a loss; it is what a
correct log looks like when two of four devices are in another room. The proximity signal was captured in
full.

**The transitions are visible in three independent channels at once:**

- **3E recorded exactly two motion records all night**: `MOVING` at 5:00:22.670 and `still` at 5:00:27.900 —
  a single 5.2-second carry. Motion records are edge-triggered, so one pick-up-and-set-down is exactly two
  records. The 3E–F3 link ends and the 02–3E link begins in the same 5-minute bin.
- **AE recorded 208 motion records**, all in bursts around 04:38, 04:39, 04:47 and 04:55 — a person moving
  with it in a pocket, which is what produces many state transitions rather than two.
- **AE's ranging comes in exactly the brief episodes the operator described:**

```
 0:00 .. 0:28   28 min   3235 records   peers=[02, 3E, F3]   setup, all four together
 0:44 .. 0:46    2 min     69 records   peers=[3E, F3]       walk-by past room 1
 4:55 .. 4:56    1 min     10 records   peers=[3E, F3]       walk-by
 5:00 .. 5:02    2 min    118 records   peers=[3E, F3]       in room 1, collecting 3E
14:52 .. 15:23   31 min   3682 records  peers=[02, 3E, F3]   morning pickup
```

  Three transient contacts of 1–2 minutes, each listing only the room-1 pair, and the 5:00 episode is the
  operator standing in room 1 at the moment 3E was picked up. Nothing else in the night.

**A first pass at this section read the partition as a regression caused by §15.11. That was wrong and is
withdrawn.** The error was reasoning about a four-device network from the logs alone while assuming a static
co-located layout, and then treating the absence of motion records as proof that nothing had moved — when
edge-triggered motion means a single deliberate carry leaves almost no trace. Two claims made in that pass
are corrected in §15.14 below, because both were wrong about the code and not merely about the deployment.

#### AE spent thirteen hours in the exact condition §15.9 predicted would break BLE, and nothing broke

This is the most useful thing the run produced, and it was not designed for.

AE sat in a pocket, in BLE range of the network and out of UWB range of it, for roughly 13 hours. It logged
**13,139 BLE_SCAN records at a hard 3.52–3.57 s period** — discover, attempt to join, lose the contention,
drop out, rediscover — 13,139 times. That is precisely the "marginal UWB, solid BLE" reproduction §15.9 item
3 proposed as the way to force the `stalled: BLETask` fault, and it ran for half a day.

**It produced no stall, no reset, and no lost storage.** Combined with the zero-reset result above, this is
the strongest evidence yet that the stall is either fixed or was never in this loop. It does not settle
which: the discovery-window guard reduced how often `verify_app_configuration()` runs, so the churn AE
experienced is not identical to what a pre-change device would have seen in the same position.

### 15.14 Three corrections to §15.13's first reading, all about the code

**1. A scanning master does not need the timer, and never did.** The first pass claimed that a master
surrounded by idle devices never raises `APP_NOTIFY_NETWORK_FOUND` under the guard, and offered 3E's zero
BLE_SCAN records over eight hours as evidence. Both halves are wrong.

`ble_discovery_handler()`'s master branch calls `xTaskNotifyFromISR(APP_NOTIFY_NETWORK_FOUND)` **directly**.
It does not set `devices_found`, does not touch the timer, and is completely unaffected by the guard, which
only gates the timer ISR. A master still gets its notification the instant it sees what it is looking for.

And what it is looking for is narrow *by design*: the filter admits only a peer advertising `ROLE_MASTER`
with a higher UID, and the master's sole action in the handler is to demote itself to participant if it finds
one. Filter and action match exactly. A master already schedules a network; devices join it by discovering
*it*, driven by their own scanning, so there is nothing for a master to do about an idle neighbour. 3E's zero
records mean it saw no higher-UID master in eight hours, which is correct behaviour, not a missed event.

**2. The rejoin loop is intact, and the retry rate is set by the empty-round timeout rather than by the
guard.** The first pass claimed a joining device now retries roughly 100x less often. The loop is:

```
ranging_begin(PARTICIPANT) -> verify stops scanning -> contention fails, no results
  -> fix_network_errors() counts empty rounds -> at MAX_EMPTY_ROUNDS_BEFORE_STATE_CHANGE (6 rounds = 3 s)
  -> is_running = false -> APP_NOTIFY_NETWORK_LOST -> verify starts scanning
  -> a discovery sets devices_found and clears the timer -> 500 ms -> NETWORK_FOUND -> ranging_begin(...)
```

Step 7 is a genuine fresh discovery, so the guard's condition is satisfied every cycle and the loop turns
freely. Cycle time is 3 s of empty rounds plus discovery latency plus the 500 ms window — **and AE's measured
period is 3.52–3.57 s, which matches that arithmetic and not the guard.** Before the change the cycle was the
same 3 s floor, because once `ranging_begin()` has been called `ranging_active()` is true and the handler's
join branch is skipped regardless of how often the timer fires. The guard costs at most the discovery
latency, on the order of half a second in three and a half.

What the guard genuinely removed is the case where `NETWORK_FOUND` fired with **no fresh discovery** and the
handler called `ranging_begin()` against a stale peer list. For a device that is out of UWB range — 02 alone
in room 2 during the first phase, which discovered something only about 13 times an hour — the old firmware
would have attempted a join twice a second against peers it could not reach. More attempts at an impossible
join is not lost data.

**3. And the `empty_round_timeout` reset is correct, not a trap.** A first reading called it a hazard: a
participant that hears the master but never wins the subscription contention resets `empty_round_timeout`
every round, so it stays `ranging_active()` indefinitely and never rescans over BLE. Tracing the ranging
protocol shows that staying in is exactly what it should do.

`schedule_phase_rx_complete()` searches every received schedule packet for the device's own address and sets
`scheduled_slot = UNSCHEDULED_SLOT` when it is absent. It then calls `subscription_phase_begin()`, which for
an unscheduled device transmits a SUBSCRIPTION request at a **random offset inside the 1000 µs subscription
window** — `rand() % (SUBSCRIPTION_TIMEOUT_US - 100)`. The master, in slot 0, listens for those and calls
`schedule_phase_add_device()`, which puts the requester in the next round's schedule.

So an unscheduled participant **retries the join on every single round, at 2 Hz, with a fresh random backoff**,
for as long as it can hear the master. Randomised offsets are the collision-resolution mechanism, and
repeated attempts are how it is supposed to work.

Dropping out to rescan would be strictly worse on both counts. It is **seven times slower** — 3 s of empty
rounds plus BLE rediscovery plus the 500 ms window, roughly 3.5 s per attempt, against 500 ms per attempt for
a device that stays in — and it lands back in precisely the same contention with no advantage. There is
nothing BLE rediscovery can tell a device that is already receiving the master's schedule.

`empty_round_timeout` is therefore guarded on exactly the right condition. `fix_network_errors()` requires
**neither** a detected device **nor** a ranging result before it counts a round as empty, which is the test
for "I cannot hear anything" — the network is gone or out of range, and BLE rediscovery genuinely is the only
way back. It is not a test for "I am failing to join", because failing to join is a state the protocol
already handles better on its own.


### 15.15 Putting the near-misses in the log, and turning record framing on

Four open items closed together, because they turned out to be one change. §15.7 items 1, 3 and 4 all
described the same defect from different angles — **a condition the firmware recovered from left no trace
anywhere a deployment could read it.** The watchdog decline counter, the charger chatter counter and the WSF
buffer failure counter were each reachable only from a console or a debugger, which is to say not reachable
at all on a tag that has been in a pocket for four days.

The obvious answer was a live-stats characteristic. That was rejected: the live-stats service is what a
researcher's tooling reads during a deployment, and a counter that only matters while debugging does not
belong there. **The log is the right place**, because the question is always asked after the fact.

#### `STORAGE_TYPE_DIAGNOSTICS` (= 9)

A 69-byte fixed payload written once per `TimeAlignedTask` loop, so once per ~299.4 s:

| field | bytes | what it answers |
|---|---|---|
| `watchdog_declines` | 2 | how many pets were refused because some task was late |
| `watchdog_late_episodes[5]` | 5 | **which** task, and how many distinct episodes — indexed by `watchdog_task_t` |
| `charger_suppressed_edges` | 2 | charger interrupts the de-bounce discarded as chatter |
| `wsf_alloc_failures` | 2 | BLE buffer allocations that returned NULL |
| `wsf_largest_failed_length` | 2 | how big the request that failed was |
| `wsf_pool_high_water[5]` | 5 | peak simultaneous allocations per pool |
| `wsf_pool_capacity[5]` | 5 | pool sizes, so headroom is readable without a schema lookup |
| `master_cycle_failures` | 1 | times this device ran a network as master and heard nobody |
| `firmware_revision` | 4 | leading eight hex digits of the git commit the firmware was built from |
| `status_flags` | 1 | bit 0 TempCo supported, bit 1 TempCo trims applied, bit 2 built with uncommitted firmware changes, bit 3 diagnostic build, bit 4 TempCo switched off at build time |
| `temperature_c` | 1 | chip temperature, signed °C; -128 before the first reading |
| `radio_rx_ok`, `radio_rx_failed` | 4 + 4 | ranging slots decoded and lost — the receive-sensitivity metric. Since October 2026 a lost slot counts only if its sender was transmitting that round (some device heard it); before, a device still scheduled but gone counted against every listener |
| `radio_tx_late`, `radio_rx_arm_late` | 2 + 2 | delayed transmits and receives programmed after their slot; a late receive aborts the round |
| `radio_isr_over_budget`, `radio_isr_warm_max_us` | 2 + 2 | radio interrupts past `RADIO_ISR_BUDGET_US`, and the longest once warmed up |
| `radio_irq_stuck` | 2 | times the radio interrupt line stayed asserted and the radio was silenced |
| `radio_wake_max_us`, `radio_wake_failures` | 2 + 2 | worst wake-up against `RADIO_WAKEUP_SAFETY_DELAY_US`, and wake-ups that needed a radio reset |
| `storage_records_dropped` | 2 | records discarded because the storage queue was full |
| `stack_free_words[6]` | 12 | least free stack ever seen, in words: each `watchdog_task_t` task, then the timer service; `0xFFFF` for a task not running |
| `ble_resets` | 1 | Bluetooth controller restarts by the self-check |
| `nand_bad_blocks` | 2 | retired flash blocks, factory-marked and grown |

The receive counts split by antenna were in this record for one test and came back out. They answer a bench
question, so the live radio check now reads them over Bluetooth (`BLE_LIVE_STATS_RADIO_CHAR`, laid out as
`ble_radio_stats_t`) rather than every deployment carrying them.

Every counter is **cumulative since boot and saturating**. Cumulative because a reboot then partitions them,
which lets the host attribute a near-miss to a particular boot; saturating because a wrapped diagnostic
counter reads as healthy, which is the worst thing a diagnostic can do.

Three details worth recording:

- **Episodes, not observations.** `watchdog_find_stalled_tasks()` runs on every check-in from every task, so
  a single 70-second stall would otherwise be counted five or six times by whichever tasks happened to look.
  A per-task `was_late` edge makes the count mean "how many times did this task go late", which is the
  question. `declines` deliberately stays an observation count, because it measures how long the system spent
  refusing to pet.
- **Gathered in `StorageTask`, not at the call site.** `storage_write_diagnostics()` only enqueues a type and
  a timestamp; the payload is assembled in the dispatch switch. That avoids adding another payload ring — the
  thing §15.1 defect 4 was about — and samples the counters as late as possible before the write.
- **The counter updates run under `AM_CRITICAL`.** `watchdog_find_stalled_tasks()` is reached from both a
  task and the pre-reset ISR, and a torn read-modify-write on a diagnostic counter is indistinguishable from
  the fault it exists to measure.

#### `STORAGE_TYPE_RADIO_ABORT` (= 10)

Written only by a diagnostic build (`make DIAGNOSTIC=1`), once for each radio receive that could not be armed
before its slot. A 19-byte payload, timestamped with the round it belongs to:

| field | bytes | what it answers |
|---|---|---|
| `phase` | 1 | 1 ranging (the round is abandoned), 2 status (the status exchange ends early) |
| `slot` | 1 | slot within that phase |
| `schedule_size` | 1 | devices in the round's schedule |
| `late_us` | 2 | signed µs past the arm deadline when the attempt was made |
| `isr_elapsed_us` | 2 | how long the radio interrupt had already been running; `0xFFFF` if unmeasured |
| `isr_events` | 1 | radio events that interrupt had serviced so far |
| `since_temperature_ms` | 2 | since the last 10 s temperature refresh; `0xFFFF` if none |
| `trigger` | 1 | the radio event that interrupt was handling: 1 a frame sent, 2 a frame received, 3 an empty receive window, 4 an undecodable frame; 0 unknown |
| `isr_entry_us` | 2 | when that interrupt started, µs after the round's reference; `0xFFFF` if unmeasured |
| `event_to_isr_us` | 2 | signed µs from the triggering frame's radio timestamp to the interrupt starting; `-32768` for an event with no timestamp |
| `asleep_us` | 2 | how long the processor had slept when this interrupt woke it, to the 30.5 µs of the system timer; 0 if the interrupt arrived as the processor was going to sleep; `0xFFFF` if it was awake |
| `wake_to_isr_us` | 2 | from the processor waking to this interrupt starting, which is the FreeRTOS port's bookkeeping with interrupts masked; `0xFFFF` if it was awake |

A small `isr_elapsed_us` with a positive `late_us` means the interrupt started late; a large one means it ran long.
The last three fields say which. `event_to_isr_us` includes the rest of the frame's air time after its timestamp,
so it is steady for a given frame and only a change in it matters: a larger value than usual means something
held the interrupt off, while a usual value with a later `isr_entry_us` means the event itself came late, for
instance a peer transmitting late. Where a delay comes from the processor sleeping, `asleep_us` and
`wake_to_isr_us` show it. The processor deep-sleeps whenever FreeRTOS's tickless idle finds nothing to run, so
a frame can arrive while it is asleep, or while it is on its way in or out with interrupts masked.

The October 2026 runs found every abort to have the same shape: a received frame, a handler that took a steady
435 µs, and an interrupt that started 120–147 µs after the frame's timestamp. Slots are 650 µs apart and a
receive must be armed 152 µs before its slot, which leaves 498 µs from one frame to the next arm, so the
interrupt has to start within about 63 µs of the frame (`late_us` ≈ `event_to_isr_us` − 62). That limit assumed
every handler takes 435 µs; the radio timing records later showed interrupts that succeed often start later, so
handlers that succeed run somewhat faster.

**Cause and fix.** A two-hour run with the fields above settled it. All 1,658 aborts across ten devices were
interrupts that arrived as the processor was going to sleep (`asleep_us` 0), after which the port's wake-up
bookkeeping held them off a further ~58 µs (`wake_to_isr_us`). FreeRTOS's tickless idle masks interrupts from
before it reprograms the tick until after it has accounted for the sleep. A radio frame landing in that window
waited it out, about 2,500–3,000 times per device in two hours, and about 7% of those cost the round. Interrupts
that found the processor genuinely asleep or awake did not miss. Since then, the idle task sleeps through
`system_idle_sleep()` (`portSUPPRESS_TICKS_AND_SLEEP` in `FreeRTOSConfig.h`). While the ranging radio is awake,
it sleeps lightly with the tick left running, masking interrupts only for the few instructions that check there
is nothing to run. It deep-sleeps as before the rest of the time. The radio is awake for about 48 ms of each
500 ms round, drawing at least 16 mA, so the processor's extra light-sleep current (180 µA against 14–47 µA in
deep sleep, Apollo4 datasheet Table 31) adds at most about 6 µA on average. That is probably less in practice,
because the tickless bookkeeping it avoids ran the processor at around 1 mA for a good part of every gap.

#### Schedule tracing: `STORAGE_TYPE_SCHEDULE_CATCH` (= 11), `STORAGE_TYPE_ROUND_START` (= 12), `STORAGE_TYPE_SESSION_END` (= 13)

Also written only by a diagnostic build. Together they show how each device finds each round's schedule, so a
round lost network-wide can be put down either to participants opening their receivers late or to the master
starting a round early, and a dropped network can be put down to its actual cause.

**`SCHEDULE_CATCH`** is written by a participant for every wake-up from its timer, timestamped with the round it
eventually joined. 20 bytes:

| field | bytes | what it answers |
|---|---|---|
| `first_copy` | 1 | sequence number of the first schedule copy decoded: 0–1 from the master, 2–4 relayed; `0xFF` if the network was lost first |
| `rounds_missed` | 1 | rounds that went by before a copy was decoded; `0xFF` if the schedule timestamps went backwards |
| `lead_us` | 4 | signed µs the receiver opened before the expected round's first copy, from the radio clock; negative when late |
| `timer_to_task_us` | 2 | wake-up timer firing to the ranging task running |
| `wake_us` | 2 | radio wake-up; `0xFFFF` if the radio had to be reset |
| `rx_errors` | 1 | frames heard but not decodable before the schedule |
| `other_frames` | 1 | decodable frames before the schedule that were not one |
| `first_error_us` | 2 | receiver on to the first undecodable frame; `0xFFFF` if none |
| `carrier_offset_cppm` | 2 | the decoded copy's carrier offset, in hundredths of a ppm as the DW3000 reports it |
| `wake_correction_us` | 2 | signed µs of head start this wake-up was armed with beyond `RADIO_WAKEUP_SAFETY_DELAY_US`: what the device had learned, plus any one-round allowance for a late master, saturating |
| `timer_latency_us` | 2 | the wake-up timer's compare match to its interrupt running |

A record for a wake-up that never found the network (`first_copy` `0xFF`) is stamped with the moment the
network was given up on, not the round it was waiting for, which is seconds earlier, so a log's timestamps keep
running forwards.

For a missed round, `lead_us` is measured against the round that was missed, assuming the master kept its
500 ms period. Every build, not just the diagnostic one, makes this same measurement after each timed
wake-up and uses it to adjust the next: the participant nudges its wake-up so its receiver opens
`RADIO_WAKEUP_TARGET_LEAD_US` ahead of the round, learning whatever its own wake-up, interrupt and timer
latencies add up to. The first test of the tracing found participants opening about 0.9 ms after the
master's first copy under the fixed 2400 µs margin. A round then survived only if a relay caught the second
copy, which made about every other round fail. `wake_correction_us` shows what each device learned, which
settled at 1055–1152 µs and tracks each device's own `wake_us`; it starts from
`RADIO_WAKEUP_CORRECTION_INITIAL_US` (1100 µs) so the first rounds after joining are caught too. A settled
participant whose receiver suddenly opens more than `RADIO_WAKEUP_LATE_MASTER_US` (600 µs) further ahead than
usual takes the master to have sent that round late. It learns nothing from that round and aims its next
wake-up at where the round should have been, for up to `RADIO_WAKEUP_LATE_MASTER_ROUNDS` (4) late rounds in a
row before it follows the master's new timing. Without that allowance, each late master round cost every
participant the next one.

Each measurement is taken from the wake-up it belongs to. A wake-up from sleep always starts a fresh one,
and a participant whose ranging then aborts still learns from the schedule it caught that round, because it
listens straight through to the next round with no timed wake-up to measure. A radio error discards the
measurement, since the radio may have been reset. A measurement spanning more than one missed round is
ignored, because it also carries the master's period error over every round in between. Before this, a run of
aborted rounds left one measurement pending for about 11 rounds, and the correction learned roughly 1.1 ms of
apparent lateness from it.

A late receiver with no `rx_errors` was simply late. `rx_errors` around the time of the first
copies, or carrier offsets that are larger just after a wake-up than after a long listen, point to a radio
that had not settled.

**`ROUND_START`** is written by the master for every round as the round ends, once everything it records is
known, and is timestamped with that round. Earlier builds wrote it at the start of the next round instead, so a
record written in between, such as the 5-minute diagnostics, could carry a later timestamp and put a backward step
in the log. 11 bytes:

| field | bytes | what it answers |
|---|---|---|
| `timer_to_task_us` | 2 | wake-up timer firing to the ranging task running |
| `wake_us` | 2 | radio wake-up; 0 if the radio was already awake, `0xFFFF` if it had to be reset |
| `timer_to_transmit_us` | 2 | wake-up timer firing to the first schedule copy being sent |
| `flags` | 1 | 0x01 second copy could not be armed, 0x02 computed, 0x04 abandoned on an error, 0x08 join request heard, 0x10 join request relayed |
| `schedule_size` | 1 | devices in the round's schedule |
| `devices_ranged` | 1 | ranges the master computed |
| `timer_latency_us` | 2 | the wake-up timer's compare match to its interrupt running |

The master's timer has a fixed period, so a change in `timer_to_transmit_us` from one round to the next moves
its first copy by the same amount against what participants expect.

**`SESSION_END`** is written by every device each time its ranging scheduler stops. 23 bytes:

| field | bytes | what it answers |
|---|---|---|
| `reason` | 1 | 1 stopped by the application (e.g. a higher-ID master found), 2 search timed out, 3 collision, 4 as master heard nobody; 0 unknown |
| `role` | 1 | `schedule_role_t` when it stopped; `ROLE_IDLE` means it never joined |
| `schedule_size` | 1 | devices in the last schedule it knew |
| `collision_phase`, `collision_type`, `collision_source` | 3 | for a collision: the `scheduler_phase_t` the frame arrived in, its message type, and the byte after its header (the sender, for every type but a ranging packet) |
| `collision_at_us` | 2 | how far into the round the colliding frame arrived |
| `session_ms` | 4 | how long the run lasted |
| `rounds_ranged`, `schedules_heard` | 4 | rounds computed while scheduled, and schedules decoded |
| `join_requests_sent`, `join_requests_heard` | 4 | join requests this device sent while unscheduled, and those that reached it as master |
| `listen_errors` | 2 | undecodable frames while listening for a schedule |
| `stalls` | 1 | times no round completed for `RANGING_ROUND_STALL_TIMEOUT_MS` |

A device that cannot join shows a run of `search timeout` records whose `schedules_heard` and
`join_requests_sent` say whether it heard the network at all. Their cost, about 50 bytes a second per device,
is why they are confined to the diagnostic build.

#### `STORAGE_TYPE_RADIO_TIMING` (= 14)

Written only by a diagnostic build, once a minute and at the end of each ranging session, by every device that
armed a receive in that time. It shows how close the receives that **succeeded** came to their deadlines,
which the abort records cannot: those only show the ones that missed. Each count is a delayed receive armed
in time straight after a received frame, the case every abort so far has been. 32 bytes:

| field | bytes | what it answers |
|---|---|---|
| `arms` | 2 | receives armed in time straight after a received frame |
| `after_sleep` | 2 | of those, ones whose interrupt had to wake the processor first |
| `during_sleep_entry` | 2 | of those, ones whose interrupt arrived as the processor was going to sleep |
| `slack_min_us` | 2 | signed µs to spare at the closest one; `0x7FFF` if none |
| `slack_under_25_us` | 2 | how many had less than 25 µs to spare |
| `event_to_isr_min_us`, `event_to_isr_max_us` | 4 | fastest and slowest from the frame's radio timestamp to the interrupt starting; the minimum is `0xFFFF` if none |
| `event_to_isr_counts` | 14 | how many fell in each of `STORAGE_RADIO_TIMING_BANDS` (7) bands: below 40 µs, then 5 µs wide, the last from 65 µs up |
| `wake_to_isr_max_us` | 2 | longest from the processor waking to a radio interrupt starting |
| `antenna` | 1 | antenna, from 0, in use for single-antenna exchanges when the record was written |
| `antenna_changes` | 1 | times that choice moved during the minute |

**Antenna choice.** Ranging uses all three antennas every round. Listening for schedules, the join window and
the status exchange each use one, which used to be antenna 0. A device whose antenna 0 was damaged could range
on its other two but rarely heard a schedule, so it rarely joined. Now the radio driver keeps each antenna's
recent ranging receives (`RADIO_ANTENNA_WINDOW_RECEIVES`, about the last 512 per antenna, counted only from
senders that were transmitting). It moves the single-antenna exchanges to another antenna only when that one
fails at least `RADIO_ANTENNA_SWITCH_MARGIN_PCT` (15) points less, with `RADIO_ANTENNA_MIN_RECEIVES` (64) on each
to judge by. A device that listens for `SCHEDULE_LISTEN_WINDOW_US` (one round) without decoding a schedule moves
to the next antenna. The master starts its schedule copies on its chosen antenna and rotates from there. The
live radio counters (`ble_radio_stats_t`, now version 2) carry the antenna in use and how often it has moved.

With the processor kept out of deep sleep while the radio is awake, `after_sleep` and `during_sleep_entry`
should stay near zero. Any aborts that remain are then the handler's own length, and the frame-to-interrupt
bands show how much margin is left.

#### Record framing, turned on

`NANDLOG_RECORD_FRAMING` is now **1**, so pages carry `'TTP2'`, the stream declares format version 2, and
every record is prefixed with its own `uint16` length.

**Adding a record type is exactly the change framing exists to make safe, so the two landed together.**
Under the unframed format a payload is walked by deriving each record's length from its type, which means a
reader that does not know a type cannot get past it — and every reader has to learn a new type in the same
commit or silently lose the remainder of every page that contains one. That is not a hypothetical: it is
precisely how the web schema package went stale against types 7 and 8 (§16), and since diagnostics, anchors
and reset records are all written at boot, the damage would have started at page 0.

Cost is two bytes per record — about **1.2%** of the payload at the record mix measured in §15.13 — against a
reader that can step over anything it has never heard of. The property is now under test in both readers:
a framed stream carrying an unknown type decodes **the records either side of it and reports the unknown one**,
where the same stream unframed loses everything after it.

Verified: the `nandlog` simulator passes **76 checks in both framings**, revM/N/O/P and the `full`,
`full_exp` and `storage` test targets all build clean, and the four existing unframed `.ttg` files still
parse to identical record counts through the Python reader.

#### The IMU teardown moved off the BLE task

§15.7 item 6. `app_allow_downloads()` called `imu_deinit()` and `imu_init()` directly, from an ATT write
callback on the BLE task at priority 3 — which preempts the app task at 2, and the app task is the only
context that services the IMU (`imu_read_accel_data`, `imu_read_in_motion`, `imu_clear_interrupts`). `imu.c`
has no serialisation of its own, so the teardown could land mid-transaction.

**Latent rather than live**: the only route in is an ATT write to `BLE_MODE_SWITCH_CHAR`, which is registered
only under `_REMOTE_MODE_SWITCH_ENABLED`, and only the `full_exp` test target sets that. Fixed anyway, so
enabling the feature does not also enable the race. `app_allow_downloads()` now latches the request and
raises `APP_NOTIFY_ALLOW_DOWNLOADS`; the app task does the work in its own context. This is the same hand-off
the battery events and the BLE notifications already use, and it costs one scheduling latency — bounded by
the BLE task yielding, and far shorter than the connection interval the host needs for its next command.

#### Cost, measured against the real logs

| | |
|---|---|
| record on the wire | 5 header + 23 payload + 2 framing = **30 bytes** |
| cadence | one per `TimeAlignedTask` loop, so **288.6/day** |
| storage | **8.7 kB/day**, 35 kB over a four-day run |
| against §15.13's overnight logs | **+0.53% to +2.44%** of payload (2.44% on AE, the device with the least data) |
| extra NAND pages in the idle regime | **zero** |
| CPU | ~500 instructions plus one record store per 300 s: **~5 µs per 300 s**, a duty cycle around 2 x 10^-6 % |

The zero-extra-pages result is the one worth understanding. §15.8 found that an idle device already burns a
whole 4096-byte NAND page every 300 s to store an 18-byte anchor-and-voltage pair, because the flush timeout
commits it — 525 to 588 such pages per device over four days. There are 4,000 bytes of slack in each. The
diagnostics record consumes 30 of them and the page count does not move. In the active-ranging regime pages
fill naturally, so 30 bytes per 300 s against ~22 B/s of ranging data is 0.45% more pages: 1.4 extra pages
across the whole overnight run.

Download time is likewise nothing: 35 kB at the measured 63 kB/s is half a second.

#### Why periodic rather than only on change

Writing a record only when a counter moves is the obvious economy, and it is the wrong shape here for two
reasons.

**A periodic record proves the counter was zero. A change-driven one cannot.** Absence of a record would
have to mean both "nothing went wrong" and "the record is not being written" — older firmware, a broken
path, a record type the reader does not know. That conflation is the exact failure this project keeps
walking into: §12.22's AE "produced no console output at all during download... By every signal that existed
before this run, AE was the cleanest of the four", while it was in fact resetting three times a night. A
standing sample every 300 s is a continuous assertion that the mechanism is alive and the answer is zero.

**And on a bad run, change-driven is the more expensive of the two.** Periodic sampling costs O(time);
change-driven costs O(events), and the event rate is unbounded. AE's charger chatter in §15.8 was 19,677
suppressed edges — under a naive write-on-change that is 19,677 records, against 288 a day for periodic.
The regime where the counters matter most is precisely the regime where change-driven blows up, and fixing
that needs rate limiting, which is a periodic sample by another name.

So the trigger stays periodic. If 8.7 kB/day ever becomes worth reclaiming, the lever is the **rate** — one
record per N loops instead of every loop — which keeps the standing proof and the bounded cost while
trading only resolution.

#### What the host does with it

`tottag_format.py` decodes the record as `'diag'`, and `tottag.py` prints any non-zero counter under
`--debug` — nothing at all when the run is clean, which is the §12.25 rule. `analyseDeployment()` exposes the
same figures as `nearMisses` and raises `watchdog-near-miss`, `charger-pin-chatter` or `wsf-pool-exhausted`,
plus a quieter `wsf-pool-tight` when a pool peaked with one buffer or fewer to spare.

**Both readers sum the last record of each boot, and getting that wrong was the first implementation.**
The counters reset at every reboot, so reading the final record alone reports only what happened since the
last restart — on a log with five reboots, most of the run is discarded. Boot boundaries come from the reset
records, which are unambiguous, with a counter going *backwards* as the backstop for the case §12.21 warns
about: a device that resets before its reset record reaches flash. A log with no diagnostics records at all
reports `samples: 0` rather than a row of zeros, so firmware predating the record type stays
distinguishable from firmware that measured nothing.

One consequence to keep in mind when reading a total: the counters **saturate** rather than wrap, so a value
pegged at 65535 (or 255 for a per-task episode count) means "at least that many" and the sum across boots
becomes a lower bound. That is the right failure direction, but it is a lower bound.

One incidental fix in the extractor: struct dimensions can now resolve against **enum members**, not just
`#define`s. `uint8_t late_episodes[WATCHDOG_NUM_TASKS]` is ordinary C and the extractor could not read it.


### 15.16 Experiment 23 — the 6.7-day qualification run, and the bug the framing change introduced

**Method.** Four devices, revP, **6.67 days** (160.1 h span, **640 device-hours**), `1788908400`, with
`use_daily_times` on: awake 14:00-03:00 UTC, so roughly 13 h of every 24. The first deployment recorded in
the framed (`'TTP2'`) format. Two days had one device carried in a pocket while the stationary nodes were
periodically moved; two days were entirely stationary.

#### The result the run was for

**Zero watchdog resets in 640 device-hours.** All 39 resets across the four devices are `SW Power-On`, and
30 of them land on the daily wake at exactly `15:00:03` past each day boundary — the RTC alarm firing on
`daily_start`, to the second.

Running the arithmetic properly: at §15.8's 0.0225/device-hour this run alone predicts **14.4 events**, so
P(zero) = e^-14.4 ≈ **6 x 10^-7**. Together with §15.6 and §15.13 that is **778 device-hours with no watchdog
reset against an expectation of 17.5**. **The hang is closed.** Whatever §15.8 was seeing, it is gone, and
the §15.2 rebuild plus the §15.11 discovery-window fix are between it and this run.

**And the near-miss counters located the thing §15.3 could never place.** §15.7 item 9 added them for exactly
this and they worked on their first deployment:

| device | pet declines | late episodes |
|---|---|---|
| 02 | 11 | Ranging 10, Storage 6, BLE 1 |
| 3E | 12 | Ranging 12, Storage 6 |
| AE | 8 | Ranging 8 |
| F3 | 16 | Ranging 16 |

That is roughly one episode per device per 10-20 hours, against §15.3's estimate of "once per device per 19
hours" derived two runs ago from a latch that could not say when or which. **It is predominantly
`RangingTask`, with `StorageTask` second.** None came within reach of the reset window. Open item 1 stops
being "something, somewhere" and becomes a named task.

**Everything else the run measured.** 12,421 pages and 1,514,547 records with **zero holes, zero CRC
failures, zero short pages, zero sequence gaps, zero duplicates, no truncation**. 2,009,035 pairwise ranging
measurements with 99.72-99.93% of timestamps shared between both ends and 99.12-100% agreeing within 100 mm.
Zero WSF buffer allocation failures and zero suppressed charger edges on any device across the whole run --
§15.7 items 10 and 11 answered, both negative. RTC drift measured against true time at download: **156, 167,
177 and 200 ppm** (mean 175), independently confirming §12.27's 194-238 ppm finding on a 6.7-day baseline.

#### The bug: turning framing on broke boot-time anchor recovery

**This is a defect I introduced in §15.15 and did not catch.**

`nandlog_read_recent_page()` is, by design, "deliberately record-agnostic" -- it hands back the raw payload,
framing prefixes and all. `last_time_anchor_in_page()` in `storage_records.h` walked that payload with
`stored_record_length()`, which derives a record's length from its type byte and knows nothing about the
2-byte length prefix. When framing went on, the library, both host readers and the wire format all learned
about the prefix. **That walker did not.**

So at every boot the walk reads a length prefix as a type byte, steps to an arbitrary offset, and continues
through garbage -- occasionally landing on a byte that looks like `STORAGE_TYPE_TIME_ANCHOR` and reading
eight arbitrary bytes as a (network, local) pair. The recovered offset is then applied to every record
timestamp until a ranging round overwrites it.

Reproduced exactly against the deployment logs, by running the firmware's own walker over the real page it
would have read:

| device | page it recovered from | offset the misparse yields | offset actually seen in the log |
|---|---|---|---|
| 02 | 656 | **855,548,654 ms** | 855,548,654 ms |
| 3E | 644 | **452,895,466 ms** | 452,895,466 ms |

Bit-for-bit. The correct framed walk of the same two pages gives **+306 ms** and **-595 ms**.

**Blast radius: 2,197 records of 1,514,547 (0.145%)**, but the shape matters more than the size. The
duration is however long the device goes without a ranging round to correct it, and the device most likely
to go without one is the isolated one:

| device | affected records | duration | offset |
|---|---|---|---|
| 02 | 608 | 5.0 min | +9.9 days |
| 3E | 607 + 199 | 5.0 min + 74.8 min | +5.2 days, +9.5 h |
| AE (pocket) | 783 | **92.5 min** | **+9.5 h** |
| F3 | 0 | — | — |

F3 escaped because it holds the highest UID and is usually master, so its offset is legitimately zero and
nothing it recovered could make it worse. AE -- the device whose movement is the actual scientific signal --
carried timestamps 9.5 hours in the future for an hour and a half.

**Why nothing caught it.** `test_time_anchor_recovery` exists and covers exactly this function, but it is a
hardware target (`make storage`) and was not re-run after the framing change. The `nandlog` simulator passed
76 checks in both framings, which is what I relied on -- but its framed-walk test **hand-rolled its own
walk**, so it proved the writer framed correctly and could not notice that a second walker elsewhere read the
same page as if it were unframed. A test that reimplements the thing under test cannot catch a
reimplementation being wrong.

#### The fix, in three parts

1. **One walker.** `nandlog_framed_next_record()` is now the only implementation of the framing layout.
   `last_time_anchor_in_page()` calls it, and the simulator's framed-walk test calls it instead of
   hand-rolling, so the 76 checks now cover the code the firmware runs.
2. **Keyed off the page, not the build.** The walker takes `framed` from the page header's own magic
   (`NANDLOG_PAGE_MAGIC_FRAMED`) rather than from `NANDLOG_RECORD_FRAMING`. A reader cannot fall out of step
   with the writer again, and a build can read a log the other kind of build wrote.
3. **A plausibility gate.** `time_anchor_is_plausible()` rejects an anchor from the future -- impossible,
   since the local clock is the RTC and runs through both a reboot and a daily power-off -- and any offset
   beyond `STORAGE_MAX_PLAUSIBLE_OFFSET_MS` (1 hour). The offset is only ever the difference between two
   devices' RTCs measuring the same experiment start, which is seconds; 22 s was the worst legitimate value
   in this run. A rejected anchor leaves the offset at zero and logs why, turning a silent misdating into a
   visible refusal. **This is the part that would have made the bug harmless even unfixed.**

*A second defect the run surfaced, already corrected:* `bluetooth_get_buffer_stats()` called
`WsfBufGetPoolStats(pool_stats, num_pools)` treating the second argument as a count. It is a **pool index**,
and the SDK's own doc comment ("numPool / Number of pool elements") says otherwise. Passing the count took
the `poolId >= wsfBufNumPools` early return, which sets `bufSize` and leaves everything else untouched, so
the logged high-water marks were uninitialised stack -- stable across a whole deployment because the stack
layout at that call site repeats. 528 diagnostics records reported peaks of `{253,184,0,53,120}` against
pools that hold `{8,4,6,14,8}`. The allocation-failure count is tracked independently and was unaffected, so
"zero failures" stands; the headroom figures from this run do not.


16. The web schema package
--------------------------

`software/managementweb/packages/tottag-schema` is the format knowledge behind the browser-based replacement
for `software/management`. It is pure: constants, parsing and validation, no I/O. It exists separately from
the Python dashboard because a browser cannot run the Python, and it is drift-checked against `firmware/src`
so that it cannot quietly disagree with the device.

It had gone stale in the way this whole document exists to prevent — silently, and with its own checker
broken so that nothing said so. §12.21 recorded the first half of that; the `nandlog` extraction in §14 made
it worse by deleting the entire `MEMORY_*` family and the EVB board revision that the extractor's revision
scan depended on. The state on 2026-09-01 was: `STORAGE_NUM_TYPES` snapshotted as 7 against a firmware value
of 9, `MEMORY_NUM_DATA_BYTES_PER_PAGE` as 4092 against a real v2 payload capacity of 4064, the v2 wire format
recorded as an unreleased proposal, and `npm run drift:update` failing outright.

**A reader built on that snapshot could not have read a single file from §15.8.** With
`NANDLOG_RECORD_FRAMING` at 0 a payload is walked by deriving each record's length from its type, so a reader
that stops at type 7 desynchronises at the first `RESET_REASON` or `TIME_ANCHOR` record — which, since both are
written at boot, is page 0.

§16 records what the package now knows and what it does with it.

### 16.1 What was repointed

| | was | is |
|---|---|---|
| Storage constants | `peripherals/include/storage.h`, the `MEMORY_*` family | `external/nandlog/nandlog_conf.h` and `nandlog.h` |
| Flash geometry | per board revision, from `boards/rev{M,N,O,P}/pinout.h` | **per fitted part**, from each chip driver — geometry belongs to the part, and `nandlog` identifies it at runtime |
| Board revisions | `EVB, M, N, O, P` | `M, N, O, P` — EVB was deleted from the firmware, and its absence is what broke the extractor |
| Record types | `storage_data_type_t` in `storage.h`, 7 values | in `tasks/storage_records.h`, 9 values |
| Page payload capacity | 4092 (v1 `'DA'` header) | 4064, **derived from the measured `nandlog_page_header_t`** rather than from a literal |
| v2 format | prose transcribed from this document under `V2_SPEC_PENDING` | extracted from `nandlog.h` and drift-checked |
| Watchdog, reset diagnostics | absent | `watchdog_task_t`, `reset_diagnostic_t` and the full `WATCHDOG_*` configuration |

Three checks were added that nothing had before, each for a failure the audit could have had:

- **A revision the firmware defines but the spec omits is now an error**, not just the reverse. The
  old check could only notice a revision disappearing, which is the less dangerous direction — a new
  board with different flash would have been silently absent.
- **The per-task stall codes are asserted contiguous with `watchdog_task_t`.** The firmware asserts
  this statically; the host decoder derives task names from the offset rather than transcribing them,
  so a reordering would rename every stall in every archived log with nothing failing.
- **The struct extractor's body pattern was wrong** in a way that only showed once `nandlog.h`
  declared four packed structs in a row: `[\s\S]*?` anchored at the first `typedef struct` in the
  file and spanned forward to reach the requested name, merging the fields of everything in between.

### 16.2 What it now does with the data

The reader (`src/log.ts`) handles both stream versions and both record framings, verifies every page
CRC independently of the device, and reports holes, CRC failures, short decodes, rejected records,
backward page bounds and truncation by position and sequence number. It returns records **in write
order** rather than merged by timestamp, because merging silently drops the older of two records of
the same type; the merged shape is still available for `.pkl` consumers, with a test pinning what it
costs.

The analysis (`src/health.ts`) exists because §15.8 was done by hand. Every finding in it that a
person had to notice is now a check — page integrity, reboot causes, cadence capture against 299.38 s
rather than 300, per-peer ranging, page fill, and the charging-event storm. Three are worth naming
because they are not obvious from any single field:

- **The shape of a stall.** Silence shorter than `STORAGE_FLUSH_TIMEOUT_S` means one task stalled
  while the rest of the system kept logging; silence longer than it means the device stopped
  executing. Together with the diagnostic that separates `single-task` from `whole-system`, and the
  two cost very different amounts of data. Neither signal says it alone.
- **The outage, measured on the clock the reboot does not touch.** The anchors bracketing a reset
  give real elapsed time regardless of what the offset did — the thing the anchor was added for in
  §12.24. On the committed fixture it reads 182.0 s, matching the hand measurement.
- **Whether the charger caused a reboot.** Every boot writes its charge state, so a charging record
  at the reset's own millisecond accompanies *every* reset including watchdog ones. Keying on that
  labelled all seven watchdog resets as charger events on the first attempt; the transition that
  caused a reboot appears *before* it.

`compareLink` scores the link between two devices from both ends, which is the only check on shared
time that no single file can make.

### 16.3 Validation

Two contiguous runs of pages from device 02's §15.8 log are committed as fixtures — seq 0–7 (boot,
with its reset reason and first anchor) and seq 1416–1425 (spanning the `stalled: BLETask` watchdog
reset) — with only the stream header's page count rewritten. 26 kB and 28 kB. Real device bytes
matter here more than volume: before them, both this package and the firmware were derived from the
same prose, so agreement between them proved nothing.

The suite is 85 tests, and the full gate — drift extraction, typecheck, tests, build — is green. Page
CRCs, record-count agreement per page, deliberate payload corruption, truncation, retransmission
repair, and every reset-status decode are covered. `npm run drift` is now the first thing that runs,
and a failure there should be read as a red build rather than as a chore.

**Cross-validated against the Python parser on all 27 MB.** The TypeScript reader was run over the
four full §15.8 files and compared against the figures derived independently with
`tottag_format.py`. Every number matches exactly:

| | 02 | 3E | AE | F3 |
|---|---|---|---|---|
| pages delivered / advertised | 3036/3036 | 3029/3029 | 3045/3045 | 3058/3058 |
| records | 612,699 | 623,179 | 620,474 | 619,102 |
| integrity clean | yes | yes | yes | yes |
| reboots / watchdog | 9 / **4** | 5 / 0 | 10 / **3** | 5 / 0 |
| run time lost | 0.091% | 0% | 0.242% | 0% |
| 300 s cadence captured | 100.4% | 100.3% | 100.2% | 100.3% |
| median anchor interval | 299.38 s | 299.38 s | 299.38 s | 299.38 s |
| pages/day | 837 | 835 | 770 | 843 |
| heartbeat-only pages | 525 | 525 | 588 | 570 |

Every one of 02's four watchdog resets classifies as `single-task` with `stalled: BLETask`, and every
one of AE's three as `whole-system` with no cause — the split §15.8 arrived at by hand. Per-reset
outages measured from the anchors read 182.0 / 218.8 / 149.9 / 315.6 s on 02 and 458.7 / 463.2 /
506.4 s on AE, matching the hand figures to the centisecond. Link agreement across all six pairs is
99.4–99.8% shared and 99.9–100% agreeing.

The anomaly detector fires on exactly the three devices it should and stays quiet on the fourth:
`charging-event-storm` on AE alone, `time-rebased` on F3 alone (its single 2.5 s step), and
`watchdog-reset` on 02 and AE. **Two independent implementations, in two languages, from two people's
reading of the format, agreeing to the record on 2.48 million records** is the strongest statement
available that the reader is right — and it is only possible because real device output exists to
run both against.


---

## 17. The watchdog near-misses: a race in the watchdog's own bookkeeping

§15.16's 6.7-day run closed the hang — 640 device-hours, zero watchdog resets — but left 47 declined
pets behind, spread over four devices and every one of them naming `RangingTask`. §15.3 had already
seen the same rate and guessed at a real ranging stall. It was not one. The counters were reporting a
defect in the code that reads them.

### 17.1 What the log rules out

The diagnostics records make each decline locatable to within one 300 s sample, and two properties of
that data are decisive.

**Every decline is isolated.** `watchdog_declines` increments once per evaluation in which any task is
late, and an evaluation runs on *every* check-in by *any* task — so during a genuine 60 s stall the
four healthy tasks would keep evaluating at ~0.3 Hz each, plus the pre-reset ISR, and the counter
would climb by tens. Across all 47 events, on all four devices, it never once climbed by more than
one. Whatever the condition was, it was true at exactly one evaluation and false at the next.

**The log shows no stall at all.** In 42 of the 47 five-minute windows containing a decline, the
largest gap between *any* two consecutive records in the whole window is 0.50 s — the unbroken 2 Hz
ranging cadence, ~602 records per window. `StorageTask` checks in at the top of the same loop that
writes those records, so a device writing every 500 ms cannot have had a storage task 60 seconds
silent. The five exceptions are windows with 7–330 records instead of 602: devices out of network
range, which is a ranging fact, not a scheduling one.

So the tasks reported as stalled were demonstrably running normally at the moment they were reported.

### 17.2 The race

`watchdog_find_stalled_tasks()` sampled the clock *outside* the critical section that reads the
check-in stamps:

```c
const uint32_t now = watchdog_now(), deadline = WATCHDOG_MS_TO_STIMER(WATCHDOG_CHECKIN_DEADLINE_MS);
AM_CRITICAL_BEGIN
for (uint32_t task = 0; task < WATCHDOG_NUM_TASKS; ++task)
{
   const bool late = watchdog_registered[task] && ((now - watchdog_last_checkin[task]) > deadline);
```

Between that read and the loop, a higher-priority task can preempt and check in — stamping
`watchdog_last_checkin[]` with a value *newer than the `now`* the interrupted task is about to compare
against. The subtraction is unsigned, so a stamp a few microseconds in the future does not read as a
small negative age; it wraps to roughly 4.29 billion ticks, or 49 days. The task is declared stalled,
its episode counter ticks, the pet is declined, and the next evaluation — reading a fresh `now` —
finds everything healthy. One decline, one episode, self-clearing. Exactly the signature in the data.

The window is a handful of instructions, but `system_init()` calls `am_hal_cachectrl_disable()`, so
those instructions are fetched from MRAM uncached and the window is on the order of a microsecond
rather than tens of nanoseconds. At ~10 evaluations/s and a victim check-in rate of a few Hz that
predicts a couple of false reports per device-day. Measured: 47 over 24 device-days, or 2.0 per
device-day.

### 17.3 Why the victims are exactly who they are

The model makes a sharp prediction: a task can only be falsely accused if it is able to *preempt* the
task doing the evaluating, and if it checks in often enough to land inside that window.

| task | priority | check-in rate | can falsely accuse | observed episodes |
|---|---|---|---|---|
| StorageTask | 5 | per queued record, ~2–4 Hz | everyone | 16 |
| RangingTask | 4 | per radio notification, ~5–10 Hz | BLE, App, TimeAligned | **30** |
| BLETask | 3 | 0.1 Hz | App, TimeAligned | 1 |
| AppTask | 2 | 0.1 Hz | TimeAligned only | 0 |
| TimeAlignedTask | 1 | 0.1 Hz | nobody — cannot be a victim | 0 |

The two fast check-in tasks account for 46 of 47. The ratio between them, 30:16, tracks their check-in
rates rather than their priorities — `RangingTask` wakes on every radio event within a round, not once
per round. The three slow tasks, checking in 50–100× less often, contribute one event between them,
and the one task that can never preempt an evaluator contributes none.

The confirmation is in the degraded windows. Device 3E's two out-of-range windows (7 and 8 records in
300 s, so `StorageTask` checking in only on its 10 s timeout) produced declines in which
`RangingTask` incremented and `StorageTask` did not — the victim distribution tracking the check-in
rate even as the rate collapses. Device AE, the pocket device, never once accused `StorageTask`.

### 17.4 The fix

Two changes, both in `watchdog_find_stalled_tasks()`:

```c
const int32_t deadline = (int32_t)WATCHDOG_MS_TO_STIMER(WATCHDOG_CHECKIN_DEADLINE_MS);
AM_CRITICAL_BEGIN
const uint32_t now = watchdog_now();                        // sampled with interrupts already masked
for (uint32_t task = 0; task < WATCHDOG_NUM_TASKS; ++task)
{
   const int32_t age = (int32_t)(now - watchdog_last_checkin[task]);
   const bool late = watchdog_registered[task] && (age > deadline);
```

Moving the sample inside the critical section closes the race: no context switch can occur between the
read and the comparisons. The signed age is the belt to that braces — a stamp from the future reads as
a small negative age instead of an enormous positive one, which stays correct for any elapsed time up
to 2^31 ticks (18.2 hours), far beyond the 60 s deadline. Either change alone would have suppressed
all 47 events; both are worth having, because the unsigned wrap was the kind of arithmetic that is one
refactor away from being wrong again.

One adjacent defect was fixed alongside it. The startup-grace test,
`(watchdog_now() - watchdog_armed_at) < WATCHDOG_MS_TO_STIMER(WATCHDOG_STARTUP_GRACE_MS)`, is
unlatched, and the STIMER wraps every ~36.4 hours — so a boot that ran that long would re-enter a
120 s window in which the watchdog pets unconditionally, once per wrap, forever. The daily-times
deployment reboots every ~10 hours so it was never reachable there, but a continuous deployment would
hit it on day two. It is now latched: once the grace elapses it never returns.

### 17.5 What this costs and what it was worth

Nothing was wrong with the firmware being watched. The hang closed in §15.16 on 640 device-hours of
evidence and it stays closed; the 47 declines were never evidence against it, and the seven genuine
watchdog resets in §15.8 were genuine — that path works, which is why this was never visible as a
fault. What it did cost was a decline in the hardware pet each time, one out of the eight ticks
available before a reset, so the margin was never close to spent.

The more useful lesson is about the diagnostics themselves. The counters added in §15.15 were what
made this findable: without `watchdog_declines` and the per-task episode counters in the log, the
symptom was invisible, and with them the diagnosis came from the *shape* of the counter data — one
decline per episode, never two — rather than from reading the ranging code, which is where the two
previous attempts at this question went. Instrumentation that records near-misses pays for itself the
first time the near-miss turns out to be the instrument.


---

## 18. Running `make storage` after the framing change

§15.7 item 4 said to re-run the on-device storage suite after any change to the record format,
because it is the only test covering `recover_time_anchor()` and skipping it is how §15.16's anchor
bug reached a 6.7-day deployment. Run at last, it failed four of its seven tests — and the failure
was the test, not the firmware. Recording it because the way it failed is more useful than the fix.

### 18.1 What the device reported

```
Read-back: device reports 0 chunks for 200 written pages (expect 200)
=== Block-crossing test FAILED: 0/200 pages verified, 0 errors ===

  ERROR: 1 chunks reported, expected 7 (a missing page means the head did not advance)
  ERROR: page 0 length 107, expected 4064
=== Partial-page flush test FAILED: 2 errors ===

  Sought 12000 ms; first page returned index 4294967295 (ts 0 ms), expected index 24
=== Time-range seek test FAILED ===

  ERROR: seq 37 returned nothing   [... every requested sequence ...]
=== Page retransmission test FAILED: 8 errors ===
```

Timestamp jump, time anchor recovery, and experiment details passed. The split is the diagnosis: every
failing test builds its pages with `write_tagged_page()`, and every passing one writes small records of
its own. Nothing was being written at all.

`107` is the tell. The partial-page test buffers 100 bytes and got back a 107-byte page: 100 bytes of
data, 5 for the record type and timestamp, and **2 for the framing length prefix**. The one chunk that
came back was the partial page — the only page in that test not written by `write_tagged_page()`.

### 18.2 The cause

The suite sizes its page-filling record as

```c
#define TEST_RECORD_DATA_BYTES   (nandlog_data_bytes_per_page() - 5)
```

which was right while a record was `[type:1][timestamp:4][data]`. With `NANDLOG_RECORD_FRAMING` on it
is `[length:2][type:1][timestamp:4][data]`, so that record is two bytes larger than a page, and
`nandlog_store_record()` does the correct thing with it:

```c
const uint32_t record_length = prefix_length + 1 + sizeof(timestamp) + data_length;
if (record_length > data_bytes_per_page)
   return;                       // dropped whole: records are never split across pages
```

The firmware refused to store it, which is exactly right — the alternative is a record split across a
page boundary, which is the corruption class this whole redesign exists to eliminate. But it is dropped
**silently**, so every test built on the wrong figure measured an empty log and reported it as a storage
failure. Four tests, all pointing at the storage layer, all caused by two bytes of arithmetic in the
test file. The comment above the constant had even been updated to mention framing; the expression under
it had not.

The fix makes the suite framing-aware the way the rest of the tree already is: the overhead is computed
rather than assumed, and every place that reached into a payload at a hard-coded offset now walks it
with `nandlog_framed_next_record()` — the same single shared walker introduced in §15.16, which is what
makes "where does the first record start" a question with one answer instead of five.

### 18.3 The second finding: half the simulator was not running

The obvious question is why the host simulator, which runs in both framings on every build, did not
catch this. It compiles two binaries, and the Makefile's comment explains the intent:

> Both settings of `NANDLOG_RECORD_FRAMING` are built and run: it is an on-flash format switch, so the
> one that is off by default is exactly the one that would otherwise rot.

But only one of the two passed `-DNANDLOG_RECORD_FRAMING=1`; the other inherited `nandlog_conf.h`'s
default. **When §15.16 flipped that default from 0 to 1, the unframed half silently became a second
framed build,** and the suite went on printing two passing runs of what was now one configuration. Both
binaries printed `record framing on` and nobody read the header line, including me — this document's
§15.16 claim of "76 checks in both framings" was 76 checks in the same framing, twice.

Both are now pinned explicitly, so neither depends on a default that can move underneath it. The
unframed configuration, untested since the switch, passes: 38 checks against the framed build's 80, the
difference being `test_framed_records_walk_back` and nothing else — every other test runs in both.

### 18.4 The test that would have caught it

Added to the simulator, where it runs on the host in both framings in milliseconds:

```c
const uint32_t overhead = (NANDLOG_RECORD_FRAMING ? NANDLOG_FRAMING_LENGTH_BYTES : 0) + 5;
nandlog_store_record(7, 1000, big, capacity - overhead);
CHECK(read_all_pages() == 1, "a record sized to fill a page did not produce exactly one page");
// one byte more has nowhere to go: records are never split, so there is no partial page either
nandlog_store_record(7, 1000, big, capacity - overhead + 1);
CHECK(read_all_pages() == 0, "a record one byte too large for a page was stored anyway");
```

It asserts the arithmetic itself rather than any consequence of it, which is the only form that would
have failed on the day the default flipped rather than four months later on a bench.

### 18.5 What this says about the open item

§15.7 item 4 was written as "re-run the hardware test after a format change". That was the right
instruction and it found something. But the more durable half of the lesson is the one §18.3 carries:
a configuration that is only covered when a default happens to point at it is not covered. The
instruction is now: **pin what a test is testing.** A suite that reports two passing runs is worth
exactly as much as the difference between them, and the only way to know there is a difference is to
make the build say so.


---

## 19. Two dashboards, one set of behaviours

The web dashboard was built to replace `tottag.py`, and until now the only thing pinning the two together
was the schema package's parity test — which compares *constants*, not behaviour. Downloading the same four
tags through both tools is the first end-to-end comparison, and it found one break, one gap, and a list of
differences worth stating out loud.

### 19.1 The break: `tottag.py` could no longer find a tag

The firmware now advertises its short UID in the local name, so a chooser can tell tags apart:

```c
memcpy(adv_local_name, adv_name_prefix, sizeof(adv_name_prefix));   // "TotTag"
adv_local_name[sizeof(adv_name_prefix)] = '-';
adv_local_name[sizeof(adv_name_prefix) + 1] = hex_digits[(uid[0] >> 4) & 0x0F];
adv_local_name[sizeof(adv_name_prefix) + 2] = hex_digits[uid[0] & 0x0F];
```

`tottag.py` matched that name by equality:

```python
if device_info[1].local_name == 'TotTag':
```

Against firmware advertising `TotTag-3E`, that condition is never true and the scan finds **nothing** over
BLE. USB-serial discovery, which matches on VID/PID, was unaffected — which is exactly the kind of partial
breakage that gets misread as a flat battery or a range problem. The web dashboard was already filtering
with `namePrefix: 'TotTag'` and was unaffected.

Both tools match by prefix now, and the parity test reads `bluetooth.c` directly:

```ts
const prefix = [...chars[1]!.matchAll(/'(.)'/g)].map((m) => m[1]).join('');
assert.equal(prefix, 'TotTag');
assert.match(BLUETOOTH_C, /adv_local_name\[sizeof\(adv_name_prefix\)\] = '-';/,
  'the firmware no longer appends a UID; equality matching would be safe again');
assert.doesNotMatch(TOTTAG_PY, /local_name == /,
  'tottag.py is matching the advertised name by equality, which finds no device');
```

That last assertion is the useful one. It fails on the *shape* of the mistake rather than on a particular
name, so it catches the next tool that decides to compare the whole string.

### 19.2 Saving a log, and proving the two tools produce the same file

The web dashboard could offload a log and could analyse it, but had nowhere to put it: the bytes were parsed
and then dropped. It now writes them verbatim, and names the file the way `tottag.py` does —
`{label}_{experiment start}.ttg`, where the label comes from the deployment's own details block and the
timestamp is the EXPERIMENT's start rather than the download's, so downloading a tag twice overwrites one
file instead of leaving two under different clock readings. The one fact the log cannot supply is which of
the devices listed in the details block wrote it, and that comes from the tag's EUI over the transport.

**The four logs from §15.16, downloaded through both tools, are byte-identical:**

| | 02 | 3E | AE | F3 |
|---|---|---|---|---|
| bytes | 6,552,021 | 6,061,571 | 5,415,588 | 6,557,116 |
| `cmp` against the Python download | identical | identical | identical | identical |
| filename derived independently | `02_1788908400.ttg` | `3E_1788908400.ttg` | `AE_1788908400.ttg` | `F3_1788908400.ttg` |

Two transports — `bleak` against a native Bluetooth stack, and Web Bluetooth inside a browser sandbox — two
independent implementations of the offload protocol, 24.5 MB, and not one byte of difference. §16.3 compared
the two *readers* on the same bytes; this compares the two *transports*, which is the half that was still
taken on trust.

Because a live offload takes minutes and leaves no file behind, the page also has to stop the user losing
one: the raw stream is retained only for a log that exists nowhere else, the card says so until it is
written, **Clear** is disabled while anything is unsaved, a dismissed save dialog does not count as saved,
and `beforeunload` guards the tab. An imported file releases its bytes after parsing, because it is already
on disk.

### 19.3 The gap: nobody was asking for the lost pages back

`tottag.py` runs up to `MAX_REPAIR_ROUNDS` (3) rounds of retransmission after a download, re-parsing the
original stream with every repair gathered so far and asking for whatever is still missing. The web
dashboard had every piece of this — `retransmitPages()` in the transport, `missingSeqs()` and
`extractPages()` and a repairs-aware `parseV2()` in the schema — and wired none of them together. It
downloaded once and reported the holes on the card.

It now runs the same loop, with the same round cap, and makes the same distinction: a partial loss names the
pages it wants, while a transfer in which *no* page arrived has no sequence number to count from and so
repeats the whole download instead.

### 19.3.1 What gets saved: the repaired stream, in both tools

Both tools originally kept the repaired pages *beside* the stream rather than in it. `tottag.py` wrote
`self.original_stream` to the `.ttg` and passed `self.repairs` separately to the parser, so the recovered
pages reached the `.pkl` and never the log file; the web dashboard, whose `.ttg` is its only output, would
have lost them entirely when the tab closed. Either way a file that had been repaired reported holes again
on re-import — holes the tool had already gone and fixed.

Both now **merge before saving**. The merged stream is the artefact of record:

```ts
export function mergeRepairs(stream: Uint8Array, repairs: ReadonlyMap<number, Uint8Array>): Uint8Array
```

What makes this safe is that a retransmission response carries the same page framing as a download, so each
repaired page arrives as a complete frame — seq, both timestamps, record count, payload length and CRC-32,
already verified against its payload before the frame is accepted. Substituting the **whole frame** means no
field is ever recomputed and a header cannot end up disagreeing with the payload beneath it.

The rules, identical in both languages:

- a frame is replaced only where the original is genuinely bad — absent payload, or a CRC that does not
  check — so a page that arrived intact keeps its original bytes;
- repairs for sequence numbers the stream never carried are appended in sequence order, which is how a
  transfer that stopped partway through gets its tail back, since a missing page leaves nothing to splice
  over;
- with nothing to merge the input is returned unchanged, so a clean download stays byte-for-byte what the
  tag sent and §19.2's identity is untouched;
- the only bytes that do not survive are any the device sent past its own declared page total, which
  neither reader treats as a page.

Each repair round merges and then re-parses the stream as it now stands, so the decision to stop is made
against the log in hand. The two implementations were checked across the language boundary on the same
damaged fixture: **26,003 bytes, CRC-32 `0x34af08e3`, byte-identical, and both readers parse the result
with zero CRC failures and zero holes.**

The dispatching `parse()` now takes repairs too, so a repair loop written against it has the same shape in
both languages; it silently did nothing there until the parameter existed. Neither download path needs it
any more — the stream repairs itself — but a caller reading an unmerged transfer alongside a set of
recovered pages still can.

### 19.4 What still differs, on purpose or not yet

| | `tottag.py` | web dashboard |
|---|---|---|
| Transport | BLE (`bleak`) and USB serial | Web Bluetooth only — Web Serial is §15.7 item 3 |
| Scan | Enumerates every tag in range | Browser chooser; **no API can enumerate**, so a batch deployment is one gesture per tag |
| Live ranging subscription | Yes | No |
| Device details, timestamp, battery | Three separate buttons | `readSnapshot()` exists in the transport, no UI uses it |
| Date-bounded download | Start/end pickers, or full | Full log only, though the transport takes a range |
| Deployment daily times | Entered as LOCAL time, converted using the start date's UTC offset | Entered as UTC, labelled and explained as such |
| Processed output | A `.pkl` beside the `.ttg`, which is written only when "Download Raw Unprocessed Data" is ticked | The `.ttg` is the only artefact, and is always offered |
| Deployment read-back | — | Written config is read back and diffed, and a manifest records what each tag confirmed |
| Log analysis | Console warnings under `--debug` | Per-device cards with anomalies, integrity and near-misses |

The daily-times row is the one to watch: both tools store the same thing — UTC seconds of day — but they ask
for different things. A researcher who types `09:00` into `tottag.py` and `09:00` into the web dashboard does
not get the same deployment unless they happen to be at UTC. Both are defensible; having both is not, and
whichever convention wins should win in both tools.
