// The guess registry.
//
// Anything in this package that is estimated, assumed, placeholder, or not yet verifiable against
// the firmware or against real data gets an entry here and is referenced from the code that relies
// on it, via `guess()`. `guesses.test.ts` fails the build if either half is missing: a `guess()`
// call with no entry, or an open entry nothing references.
//
// The purpose is not documentation. It is to stop a placeholder from quietly becoming load-bearing.
// If you cannot fill in `blocks` and `resolvedBy`, the thing you are about to write is not a guess,
// it is a decision, and it belongs somewhere else.
//
// A resolved entry keeps its `what` and gains a `resolution` stating what closed it. They are not
// deleted: the registry's value is partly as a record of which assumptions turned out to be wrong,
// and `v2-format-unreleased` in particular was wrong in an instructive way.

export type GuessStatus =
  /** Unverified and in use. Any behaviour depending on it is provisional. */
  | 'open'
  /** Verified against the firmware or real data. Kept for history; no longer referenced. */
  | 'resolved'
  /** Deliberately accepted as permanently unknowable, with a stated consequence. */
  | 'accepted';

export interface GuessEntry {
  /** What is being assumed, in one sentence. */
  readonly what: string;
  /** Why the assumption is needed at all. */
  readonly why: string;
  /** What is unreliable, wrong, or unbuildable for as long as this stays open. */
  readonly blocks: string;
  /** The specific observation or artefact that would close this. */
  readonly resolvedBy: string;
  readonly status: GuessStatus;
  /** For a resolved or accepted entry: what actually closed it, and what the answer was. */
  readonly resolution?: string;
  /** Firmware or tool locations this concerns, as `path:line` where known. */
  readonly references?: readonly string[];
}

export const GUESSES = {
  'v2-format-unreleased': {
    what:
      'The v2 page, metadata, and offload wire layouts follow doc/Storage_Redesign.md exactly: TTP1/TTM1/TTS1 magics, ' +
      '32-byte headers, CRC-32 (IEEE 802.3, reflected, init 0xFFFFFFFF, final XOR 0xFFFFFFFF), and the field order given there.',
    why:
      'The v2 parser was being written before any firmware emitted v2. Only Phase 0 of the redesign had landed, so ' +
      'there was nothing in firmware/src to extract these from and no real file to check them against.',
    blocks:
      'Every v2 code path. The parser may compile, typecheck, and pass its own round-trip tests while disagreeing with ' +
      'the firmware that eventually ships, because both sides are currently derived from the same prose.',
    resolvedBy:
      'Firmware Phase 1 landing, at which point these move from V2_SPEC_PENDING in tools/spec.mjs into extracted ' +
      'constants and become drift-checked like everything else. A single real v2 .ttg file would also close it.',
    status: 'resolved',
    resolution:
      'Closed twice over: the format shipped, and four real 3.9-day .ttg files now parse against it. Every literal ' +
      'was correct, but leaving them as transcribed prose was still the wrong call — the constants sat unchecked ' +
      'while the firmware moved underneath them (STORAGE_NUM_TYPES 7 -> 9, page payload 4092 -> 4064) and the ' +
      'extractor that would have caught it was itself broken. They are extracted and drift-checked now. The lesson ' +
      'is that "correct when written" is not a property a constant keeps on its own.',
    references: ['firmware/src/external/nandlog/nandlog.h', 'doc/Storage_Redesign.md:16'],
  },

  'imu-accel-scale-unknown-revision': {
    what:
      'The accelerometer scale for a log whose pages are all small: 1/100 m/s^2 on revM (BNO055, UNIT_SEL=0) ' +
      'versus 1/256 on revN/O/P (BNO08x, Q8). A factor of 2.56 that the record itself does not carry.',
    why:
      'A .ttg records neither the board revision nor the fitted flash part, and the IMU record body is three ' +
      'bare int16 values with no in-band signal separating the two encodings.',
    blocks:
      'Converting IMU samples to m/s^2 for a log in which no page exceeds 2016 payload bytes. Everything else ' +
      'is now determined — see `inferHardware()`.',
    resolvedBy:
      'PARTIALLY RESOLVED 2026-09-11 from firmware/Makefile: the SAME `REVISION == M` test selects both the ' +
      'flash driver (W25N01GWZEIG 2048-byte page vs AS5F18G04SND 4096-byte page) and the IMU driver ' +
      '(imu_bno055.c vs imu.c). The flash part therefore implies the IMU, and the part shows up in the log as ' +
      'page payload capacity — 2016 vs 4064 bytes. Any page over 2016 bytes proves the 4096-byte part, hence ' +
      'revN/O/P, hence Q8. Verified against both real fixtures, including one whose largest page is 3222 ' +
      'bytes — short of full, still decisive. The inference is ONE-WAY: small pages prove nothing, because a ' +
      '4096-byte part that flushed on the time-based flush looks identical, so those logs still need the ' +
      'revision from outside the file. Fully resolved by the metadata page recording the revision, or by the ' +
      'user stating it at import.',
    status: 'open',
    references: [
      'firmware/Makefile:236',
      'firmware/Makefile:247',
      'firmware/src/peripherals/src/imu.c:245',
      'firmware/src/peripherals/src/imu_bno055.c:569',
    ],
  },

  'motion-record-charger-codes': {
    what:
      'MOTION record bodies in existing files are only ever 0 or 1, never 2 (NOT_ON_CHARGER) or 3 (ON_CHARGER), ' +
      'because store_motion_change takes a bool parameter and C coerces any nonzero value to 1.',
    why:
      'The reader must decide whether a body of 2 or 3 is impossible-therefore-corrupt or meaningful-therefore-decodable. ' +
      'That decision changes how resynchronisation behaves on damaged pages.',
    blocks:
      'Charger-transition reporting, and the strictness of MOTION record validation.',
    resolvedBy:
      'Counts from the real .ttg corpus confirming no file contains a MOTION body outside {0,1}, and the firmware ' +
      'either committing to the charger codes or deleting them.',
    status: 'resolved',
    resolution:
      'The firmware deleted them: motion_code_t is now exactly { NOT_IN_MOTION, IN_MOTION }, and the 3.9-day ' +
      'four-device corpus contains 11,598 MOTION records, all 0 or 1. A body outside {0,1} is corruption, and the ' +
      'reader rejects it. Charger transitions are reported through CHARGING_EVENT records instead.',
    references: ['firmware/src/tasks/app_tasks.h:13'],
  },

  'ble-download-throughput': {
    what:
      'The 62980 B/s figure used to size the BEFORE-YOU-START hint. One measurement, on one link, on one host.',
    why:
      'A transfer that has not begun has nothing to measure, so the button still needs a number to say whether ' +
      'this is a ten-second job or a ten-minute one.',
    blocks:
      'Only the pre-transfer hint. Everything shown DURING a download now comes from ThroughputMeter, which ' +
      'measures the link in front of it — so a slow host is reported as slow rather than assumed to match the ' +
      'one this figure came from.',
    resolvedBy:
      'Largely retired 2026-09-11 by measuring at runtime instead. The remaining use is a pre-flight hint, ' +
      'which does not warrant a measurement campaign; it would be fully resolved by feeding back the median ' +
      'measured rate across real transfers once there are any. The original figure was 62.98 kB/s on a ' +
      '464-page 1.0 MB log offloaded in 16 s.',
    status: 'open',
    references: ['doc/Storage_Redesign.md:2051', 'src/throughput.ts'],
  },

  'legacy-charging-event-records': {
    what:
      'No .ttg file contains a STORAGE_TYPE_CHARGING_EVENT (type 2) record, because no firmware path in the current ' +
      'tree writes one.',
    why:
      'The Python parser decodes type 2 with a value mapping, which implies some firmware once wrote it. If older ' +
      'firmware did, the reader must handle those files; if not, type 2 is a resynchronisation false-positive risk.',
    blocks:
      'Whether the permissive reader treats type 2 as valid.',
    resolvedBy: 'Counting type-2 records across the real .ttg corpus.',
    status: 'resolved',
    resolution:
      'Emphatically wrong. The corpus holds 19,762 type-2 records across four devices — and 19,677 of those are one ' +
      'device emitting BATTERY_NOT_CHARGING at ~2 Hz for 2.8 hours from an unconditional charge-status ISR. Type 2 is ' +
      'not only valid but is the single noisiest record type in the corpus, so `analyseDeployment` raises the ' +
      '`charging-event-storm` anomaly above 100 of them.',
    references: ['firmware/src/peripherals/src/battery.c:132', 'doc/Storage_Redesign.md:15'],
  },

  'experiment-duration-limit': {
    what:
      'The maximum deployment duration, and whether the Python GUI\'s 21-day cap was a technical limit.',
    why:
      'The configuration builder needs an upper bound on the duration field, and needs to say whether that ' +
      'bound is a hard technical one or a soft advisory, because those warrant different wording.',
    blocks: 'Nothing further. Settled by measurement and by an explicit decision to adopt the measured limit.',
    resolvedBy:
      'MEASURED 2026-09-11 by compiling the exact firmware arithmetic. The ceiling is 4294966 elapsed seconds ' +
      '(49.71025 days): rtc_get_timestamp_diff_ms computes 1000 * (now - start) + 10 * hundredths in uint32, ' +
      'and NANDLOG_NO_TIMESTAMP (0xFFFFFFFF) is a sentinel a real timestamp must stay below. Note the naive ' +
      'UINT32_MAX / 1000 is one second too generous — the sub-second term costs the last second. The 21-day ' +
      'figure was NOT this constraint: it left 28.71 days of headroom, and no duration check exists anywhere ' +
      'in firmware/src. DECIDED 2026-09-11: both tools adopt 49 whole days, and the 21-day cap was deleted ' +
      'from tottag.py. A cross-tool parity test now pins the two to the same number.',
    status: 'resolved',
    references: [
      'firmware/src/peripherals/src/rtc.c:151',
      'firmware/src/tasks/storage_records.h:68',
      'firmware/src/external/nandlog/nandlog.h:13',
    ],
  },

  'watchdog-tick-rate': {
    what:
      'The LFRC drives the watchdog at 20.94 s per tick rather than the nominal 16 s, so the shipping ' +
      'WATCHDOG_RESET_TICKS of 8 is a ~167 s window rather than a 128 s one.',
    why:
      'Reading a gap in a log requires knowing how long a hang can persist before the watchdog ends it. That number ' +
      'is not derivable from the firmware: the datasheet gives the LFRC a typical frequency with no minimum and no ' +
      'maximum, and states outright that it is for use "when short term frequency accuracy is not important".',
    blocks:
      'Classifying a silence as a caught hang versus something else, and any statement to the user about how much ' +
      'time a watchdog reset cost. Both are off by whatever the real tick rate is.',
    resolvedBy:
      'Per-unit measurement, or an anchor-derived measurement from a log that contains a watchdog reset. Two ' +
      'measurements on one revP board agreed to 1.6% (20.94 s and 20.60 s), both ~24% slow, and the anchor gaps ' +
      'across seven watchdog resets in §15.8 are consistent with it — but that is one board family, and an LFRC ' +
      'with no specified tolerance is not something to extrapolate across parts.',
    status: 'open',
    references: ['firmware/src/app/app_config.h:53', 'doc/Storage_Redesign.md:2107'],
  },
} as const satisfies Record<string, GuessEntry>;

export type GuessId = keyof typeof GUESSES;

/**
 * Marks a value as depending on an unverified assumption.
 *
 * Returns the value unchanged. Its entire purpose is to be statically greppable, so that
 * `guesses.test.ts` can prove the registry and the code agree.
 */
export function guess<T>(id: GuessId, value: T): T {
  return value;
}
