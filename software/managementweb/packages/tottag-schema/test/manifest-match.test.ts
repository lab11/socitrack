// Reading a deployment record back and deciding which logs it set up.
//
// The record is the only place a study's time zone survives, so these rules decide whether a
// researcher sees their data on the study's clock or on whatever clock the computer happens to be on.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

import { buildManifest, matchManifest, normalizeEuiText, parseManifest, MANIFEST_KIND, type StudyRecord } from '../src/manifest.ts';
import { encodeExperimentDetails, type DeploymentConfig } from '../src/deployment.ts';
import { parse } from '../src/log.ts';

const HERE = dirname(fileURLToPath(import.meta.url));
const boot = parse(new Uint8Array(readFileSync(join(HERE, 'fixtures', 'boot.ttg'))));
const bootDetails = boot.report.details!;
const hex = (bytes: Uint8Array) => Array.from(bytes).map((b) => b.toString(16).padStart(2, '0')).join('');

/** The smallest record the desktop tool is guaranteed to write. */
const minimal = (overrides: Record<string, unknown> = {}) => JSON.stringify({
  kind: MANIFEST_KIND,
  deployment: { timezone: 'America/Chicago', startTime: 1787839200 },
  devices: [{ eui: 'c0:98:e5:42:02:02' }],
  ...overrides,
});

const parsed = (text: string): StudyRecord => {
  const result = parseManifest(text);
  assert.ok(result.ok, result.ok ? '' : result.reason);
  return result.record;
};

test('a record this tool wrote reads back with everything matching needs', () => {
  const config: DeploymentConfig = {
    startTime: 1_788_300_000, endTime: 1_788_300_000 + 86400, timezone: 'Europe/London',
    useDailyTimes: false, dailyStartTime: 0, dailyEndTime: 0,
    devices: [{ eui: Uint8Array.from([0x0a, 0x00, 0x42, 0xe5, 0x98, 0xc0]), label: 'Child' }],
  };
  const bytes = encodeExperimentDetails(config);
  const manifest = buildManifest(config, bytes, [{ eui: config.devices[0]!.eui, label: 'Child', outcome: 'confirmed' }],
    new Date('2026-09-11T21:00:00Z'), 'Family 7');
  const record = parsed(JSON.stringify(manifest));
  assert.equal(record.timezone, 'Europe/London');
  assert.equal(record.startTime, config.startTime);
  assert.equal(record.endTime, config.endTime);
  assert.equal(record.name, 'Family 7');
  assert.deepEqual(record.devices, [{ eui: 'c098e542000a', label: 'Child' }]);
  assert.equal(record.detailsHex, hex(bytes));
});

test('a record with only the required fields is enough, and unknown fields are ignored', () => {
  const record = parsed(minimal({ somethingNew: { nested: true }, manifestVersion: 99 }));
  assert.equal(record.name, null);
  assert.equal(record.endTime, null);
  assert.equal(record.detailsHex, null);
  assert.deepEqual(record.devices, [{ eui: 'c098e5420202', label: null }]);
});

test('addresses are read however they were written', () => {
  assert.equal(normalizeEuiText('C0:98:E5:42:02:02'), 'c098e5420202');
  assert.equal(normalizeEuiText('c098e5420202'), 'c098e5420202');
  assert.equal(normalizeEuiText('c0-98-e5-42-02-02'), 'c098e5420202');
  assert.equal(normalizeEuiText('c0:98:e5:42:02'), null, 'five parts is not an address');
  assert.equal(normalizeEuiText('TotTag 02:02'), null);
  assert.equal(normalizeEuiText(42), null);
});

test('a file that is not a usable record says why, in plain words', () => {
  const reason = (text: string) => {
    const result = parseManifest(text);
    assert.equal(result.ok, false);
    return result.ok ? '' : result.reason;
  };
  assert.match(reason('not json'), /not a deployment record/);
  assert.match(reason(JSON.stringify({ kind: 'something.else' })), /not a deployment record/);
  assert.match(reason(minimal({ deployment: { startTime: 1 } })), /time zone/);
  assert.match(reason(minimal({ deployment: { timezone: 'Mars/Olympus', startTime: 1 } })), /does not recognize/);
  assert.match(reason(minimal({ deployment: { timezone: 'UTC' } })), /when the deployment started/);
  assert.match(reason(minimal({ devices: undefined })), /does not list its tags/);
  assert.match(reason(minimal({ devices: [{ label: 'no address' }] })), /address of any tag/);
});

test('a byte-order mark and a start time written as text do not stop a record being read', () => {
  const record = parsed(`﻿${minimal({ deployment: { timezone: 'UTC', startTime: '1787839200' } })}`);
  assert.equal(record.startTime, 1787839200);
});

test('a log matches the record whose experiment details it carries exactly', () => {
  const other = parsed(minimal({ deployment: { timezone: 'Asia/Tokyo', startTime: 1 }, experimentDetailsHex: 'ab'.repeat(239) }));
  const mine = parsed(minimal({ deployment: { timezone: 'America/Denver', startTime: 2 }, experimentDetailsHex: hex(bootDetails).toUpperCase() }));
  const match = matchManifest({ details: bootDetails, experimentStartTime: boot.experimentStartTime, tag: null }, [other, mine]);
  assert.equal(match?.index, 1);
  assert.equal(match?.by, 'details');
  assert.equal(match?.record.timezone, 'America/Denver');
});

test('otherwise it matches on the start time and its own tag being listed', () => {
  // boot.ttg came from the tag ending 02:02 in a deployment that started at 1787839200.
  const record = parsed(minimal());
  const own = Uint8Array.from([0x02, 0x02, 0x42, 0xe5, 0x98, 0xc0]);
  assert.equal(matchManifest({ details: bootDetails, experimentStartTime: 1787839200, tag: own }, [record])?.by, 'start-and-tag');
  assert.equal(matchManifest({ details: bootDetails, experimentStartTime: 1787839200, tag: 0x02 }, [record])?.by, 'start-and-tag',
    'the last byte alone is enough when that is all that is known');
});

test('a different start, an unlisted tag, or an unknown tag is not a match', () => {
  const record = parsed(minimal());
  const own = Uint8Array.from([0x02, 0x02, 0x42, 0xe5, 0x98, 0xc0]);
  const elsewhere = Uint8Array.from([0x03, 0x02, 0x42, 0xe5, 0x98, 0xc0]);
  assert.equal(matchManifest({ details: bootDetails, experimentStartTime: 1787839201, tag: own }, [record]), null);
  assert.equal(matchManifest({ details: bootDetails, experimentStartTime: 1787839200, tag: elsewhere }, [record]), null);
  assert.equal(matchManifest({ details: bootDetails, experimentStartTime: 1787839200, tag: null }, [record]), null);
  // Different details bytes on the record do not count against a start-and-tag match.
  const withOtherBytes = parsed(minimal({ experimentDetailsHex: '00'.repeat(239) }));
  assert.equal(matchManifest({ details: bootDetails, experimentStartTime: 1787839200, tag: own }, [withOtherBytes])?.by, 'start-and-tag');
  assert.equal(matchManifest({ details: null, experimentStartTime: null, tag: own }, []), null);
});

test('where several records match the same way, the newest wins', () => {
  const older = parsed(minimal({ writtenAt: '2026-08-01T12:00:00Z', deployment: { timezone: 'Asia/Tokyo', startTime: 1787839200 } }));
  const newer = parsed(minimal({ writtenAt: '2026-08-20T12:00:00Z', deployment: { timezone: 'Europe/Paris', startTime: 1787839200 } }));
  const undated = parsed(minimal({ deployment: { timezone: 'UTC', startTime: 1787839200 } }));
  assert.equal(newer.writtenAt, Date.parse('2026-08-20T12:00:00Z') / 1000);
  assert.equal(undated.writtenAt, null);
  const log = { details: bootDetails, experimentStartTime: 1787839200, tag: 0x02 };
  assert.equal(matchManifest(log, [older, newer, undated])?.record.timezone, 'Europe/Paris');
  assert.equal(matchManifest(log, [newer, older])?.record.timezone, 'Europe/Paris', 'whatever order they were opened in');
  const exactOld = parsed(minimal({ writtenAt: '2026-08-01T00:00:00Z', deployment: { timezone: 'Asia/Tokyo', startTime: 1 }, experimentDetailsHex: hex(bootDetails) }));
  const exactNew = parsed(minimal({ writtenAt: '2026-08-02T00:00:00Z', deployment: { timezone: 'America/Denver', startTime: 1 }, experimentDetailsHex: hex(bootDetails) }));
  assert.equal(matchManifest(log, [exactNew, exactOld])?.record.timezone, 'America/Denver');
});

test('every write outcome either tool records is accepted, since outcomes do not affect matching', () => {
  for (const outcome of ['confirmed', 'mismatch', 'unconfirmed', 'failed', 'skipped', 'something-new']) {
    assert.ok(parseManifest(minimal({ devices: [{ eui: 'c0:98:e5:42:02:02', outcome }] })).ok, outcome);
  }
});
