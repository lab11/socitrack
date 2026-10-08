// Tests for the app's pure logic.
//
// `features/` holds decisions, not rendering: which status a log gets, how restarts are explained,
// what the CSV contains. The project rule is that decisions live in tested code and only I/O sits
// at the edges — so although these modules live in the app rather than the schema package, they
// are covered here rather than left to be verified by looking at the screen.
//
// They run under node:test with no DOM, which is the practical proof that they are pure: anything
// that reached for a browser API would fail here rather than in front of a user.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join, resolve } from 'node:path';

import { buildReport, describeReboots, formatBytes, formatDuration, sortByUrgency } from '../src/features/deployment.ts';
import { canonicalLogName, labelForDevice } from '../src/features/logFilename.ts';
import { summaryCsv, summaryFilename } from '../src/features/exportCsv.ts';
import type { LoadedLog } from '../src/ports/logSource.ts';

const HERE = dirname(fileURLToPath(import.meta.url));
const FIXTURES = resolve(HERE, '..', '..', 'packages', 'tottag-schema', 'test', 'fixtures');

function loadFixture(name: string): LoadedLog {
  return { name, bytes: new Uint8Array(readFileSync(join(FIXTURES, name))), origin: 'file' };
}

// buildReport parses in a worker where one exists; under node:test there is none, so it falls back
// to parsing in place. Either way it is async now.
const boot = await buildReport(loadFixture('boot.ttg'), 0);
const watchdog = await buildReport(loadFixture('watchdog.ttg'), 1);

test('real device output parses and is classified', () => {
  // Against the real fixtures, not synthetic bytes: the fixtures are two runs cut from an actual
  // 3.9-day deployment, so this is the end-to-end claim the UI rests on.
  assert.equal(boot.error, null);
  assert.equal(watchdog.error, null);
  // boot.ttg carries a real 4.09 s time-base correction, so it is 'note' rather than 'ok'. That was
  // always in the data; the analysis only started saying so once a step was told apart from drift.
  assert.equal(boot.status, 'note');
  assert.deepEqual(boot.health!.anomalies.map((a) => a.code), ['time-base-step']);
  assert.equal(watchdog.status, 'warning', 'a log containing a watchdog reset should ask for attention');
});

test('a file that is not a log is reported, not thrown', async () => {
  // One corrupt file in a batch of twelve must not lose the other eleven.
  const junk = await buildReport({ name: 'notes.txt', bytes: new TextEncoder().encode('hello'), origin: 'file' }, 0);
  assert.equal(junk.status, 'error');
  assert.ok(junk.error && junk.error.length > 0, 'a failure must carry a reason to show the user');
});

test('worst logs sort first', () => {
  const ordered = sortByUrgency([boot, watchdog]);
  assert.equal(ordered[0]!.name, 'watchdog.ttg');
  // Someone importing twelve tags wants the broken one at the top, not in alphabetical order.
  assert.equal(ordered[1]!.name, 'boot.ttg');
});

test('every restart is explained rather than left as a bare count', () => {
  // A card reading "Restarts: 1" beside "Looks good" with nothing joining them invites the wrong
  // question. Both fixtures contain a restart, and both must say what kind.
  const bootHint = describeReboots(boot.health!);
  const watchdogHint = describeReboots(watchdog.health!);
  assert.ok(bootHint, 'boot.ttg has a restart and must describe it');
  assert.match(watchdogHint!, /from a stall/);
  assert.doesNotMatch(bootHint!, /stall/, 'a graceful boot is not a stall');
});

test('a log with no restarts says nothing rather than "0"', () => {
  const quiet = { ...boot.health!, reboots: [], watchdogResets: 0 };
  assert.equal(describeReboots(quiet), undefined);
});

test('the CSV has one row per log and a header that matches it', () => {
  const csv = summaryCsv([watchdog, boot]);
  const lines = csv.trimEnd().split('\n');
  assert.equal(lines.length, 3, 'header plus one row per log');
  const columns = lines[0]!.split(',').length;
  for (const line of lines.slice(1)) {
    assert.equal(line.split(',').length, columns, `row has a different column count: ${line}`);
  }
});

test('the CSV carries the numbers the screen showed', () => {
  // The pipeline this replaces printed text and scraped it with a regex, which could silently drop
  // a row. Assert the values travel intact instead.
  const rows = summaryCsv([watchdog]).trimEnd().split('\n');
  const header = rows[0]!.split(',');
  const values = rows[1]!.split(',');
  const field = (name: string) => values[header.indexOf(name)];

  assert.equal(field('file'), 'watchdog.ttg');
  assert.equal(field('format_version'), '2');
  assert.equal(Number(field('records')), watchdog.health!.recordCount);
  assert.equal(Number(field('watchdog_resets')), watchdog.health!.watchdogResets);
  assert.equal(Number(field('peers_seen')), watchdog.health!.rangingPeers.size);
  assert.equal(field('anomaly_codes'), watchdog.health!.anomalies.map((a) => a.code).join(' '));
});

test('a value containing a comma is quoted', () => {
  // Labels are free text and a filename can contain anything. An unquoted comma shifts every later
  // column by one, which is the kind of corruption nobody notices until the analysis is wrong.
  const awkward = { ...boot, name: 'tag 3, caregiver "A".ttg' };
  const line = summaryCsv([awkward]).trimEnd().split('\n')[1]!;
  assert.ok(line.startsWith('"tag 3, caregiver ""A"".ttg"'), `not quoted correctly: ${line}`);
});

test('the export filename sorts chronologically and has no characters a filesystem dislikes', () => {
  const name = summaryFilename(new Date('2026-09-11T20:33:31Z'));
  assert.equal(name, 'tottag-deployment-summary-2026-09-11-20-33-31.csv');
  assert.doesNotMatch(name, /[:/\\]/);
});

test('durations and sizes read as a person would say them', () => {
  assert.equal(formatDuration(0), '—');
  assert.equal(formatDuration(90), '1m');
  assert.equal(formatDuration(3700), '1h 1m');
  assert.equal(formatDuration(90000), '1d 1h');
  assert.equal(formatBytes(512), '512 B');
  assert.equal(formatBytes(2048), '2 KB');
  assert.equal(formatBytes(5 * 1024 * 1024), '5.0 MB');
});

// --- Naming a log that came off a tag ----------------------------------------------------------
//
// The fixtures are cut from device 02's log in the 3.9-day deployment, so the details block in them
// is a real one: four devices labelled AE, 02, F3 and 3E. That makes these assertions a check
// against the firmware's actual output rather than against a hand-built block.

const details = boot.report!.details;

test('a downloaded log is named the way tottag.py would have named it', () => {
  // `{label}_{experiment start}.ttg` — the EXPERIMENT's start, not the download's, so downloading
  // the same tag twice overwrites one file instead of leaving two under different clock readings.
  const name = canonicalLogName({
    details,
    experimentStartTime: 1787839200,   // the fixture deployment's real start
    deviceUid: 0x02,
    fallback: 'unused.ttg',
  });
  assert.equal(name, '02_1787839200.ttg');
});

test('every device in the details block resolves to its own label', () => {
  assert.equal(labelForDevice(details, 0xae), 'AE');
  assert.equal(labelForDevice(details, 0xf3), 'F3');
  assert.equal(labelForDevice(details, 0x3e), '3E');
});

test('the name falls back rather than inventing one', () => {
  const fallback = 'C098E5420202_1757000000.ttg';
  // A device that is not in this deployment: nothing to name it after.
  assert.equal(canonicalLogName({ details, experimentStartTime: 1787839200, deviceUid: 0x99, fallback }), fallback);
  // A transport that could not say which device it spoke to.
  assert.equal(canonicalLogName({ details, experimentStartTime: 1787839200, deviceUid: undefined, fallback }), fallback);
  // No details block, which is what a truncated or v1 stream looks like.
  assert.equal(canonicalLogName({ details: null, experimentStartTime: 1787839200, deviceUid: 0x02, fallback }), fallback);
  // No usable experiment start.
  assert.equal(canonicalLogName({ details, experimentStartTime: 0, deviceUid: 0x02, fallback }), fallback);
  assert.equal(labelForDevice(new Uint8Array(4), 0x02), null);
});

test('an imported file keeps its bytes released and a downloaded one keeps them', async () => {
  // The distinction the save button rests on: a file is already on disk, so holding a second copy of
  // a dozen multi-megabyte logs in the heap buys nothing. A live offload exists nowhere else.
  assert.equal(boot.data, null);
  assert.equal(boot.saveName, 'boot.ttg');

  const live = await buildReport(
    { ...loadFixture('boot.ttg'), origin: 'bluetooth', deviceUid: 0x02, name: 'fallback.ttg' },
    99,
  );
  assert.ok(live.data instanceof Uint8Array);
  assert.equal(live.data!.length, live.bytes);
  assert.equal(live.saveName, '02_1787839200.ttg');
  // The card's heading shows the name it would be saved under, not the placeholder the transport
  // invented before anything had been parsed.
  assert.equal(live.name, live.saveName);
});
