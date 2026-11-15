// Guards on the prose that describes firmware behaviour.
//
// This is the one category of drift none of the other mechanisms can see. The constant snapshot
// checks values, the grammar check checks a function, the parity suite checks the Python tool —
// but a sentence telling a researcher "the ISR has no debounce" is invisible to all three, and one
// such sentence survived a firmware fix by three weeks, advising people to expect a defect that had
// been repaired.
//
// Prose cannot be verified mechanically in general. Three things can be:
//
//   1. Every firmware symbol a message names still exists in firmware/src. A message that points at
//      `charger_suppressed_edges` is worthless once that counter is renamed.
//   2. Every number a message quotes comes from the extracted constants rather than being typed in.
//   3. Specific claims that were once wrong stay gone.
//
// The third is a ratchet, not a proof. It only stops the same error returning. That is still worth
// having, because the way this went wrong was not someone inventing a new falsehood — it was a true
// sentence becoming false while nobody was looking at it.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readdirSync, readFileSync, statSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join, resolve } from 'node:path';

import { FIRMWARE_ROOT } from '../tools/spec.mjs';

const HERE = dirname(fileURLToPath(import.meta.url));
const SOFTWARE_ROOT = resolve(HERE, '..', '..', '..', '..');
const FIRMWARE = join(SOFTWARE_ROOT, FIRMWARE_ROOT);
const HEALTH = readFileSync(join(HERE, '..', 'src', 'health.ts'), 'utf8');

/** Every .c/.h under firmware/src, concatenated. Big, but this runs once. */
function firmwareSource(): string {
  const parts: string[] = [];
  const walk = (directory: string) => {
    for (const entry of readdirSync(directory)) {
      const path = join(directory, entry);
      if (statSync(path).isDirectory()) walk(path);
      else if (entry.endsWith('.c') || entry.endsWith('.h')) parts.push(readFileSync(path, 'utf8'));
    }
  };
  walk(FIRMWARE);
  return parts.join('\n');
}

const FIRMWARE_TEXT = firmwareSource();

/** The message text of every anomaly, with template expressions stripped. */
function anomalyMessages(): Map<string, string> {
  const found = new Map<string, string>();
  // Structure-agnostic on purpose. Messages are written as a single literal, as a multi-line
  // concatenation, and — since era detection — as a conditional that picks between two wordings.
  // Matching a specific SHAPE meant a message the pattern could not read was silently exempt from
  // every check below, which is how two of them escaped the first time. So: take everything from
  // `message:` to the end of the anomalies.push() call and collect every string literal in it.
  const pattern = /code: '([a-z0-9-]+)',\n\s*message:([\s\S]*?)\n\s*\}\);/g;
  for (const match of HEALTH.matchAll(pattern)) {
    const literals = [...match[2]!.matchAll(/`([^`]*)`|'((?:[^'\\]|\\.)*)'|"((?:[^"\\]|\\.)*)"/g)].map(
      (m) => m[1] ?? m[2] ?? m[3] ?? '',
    );
    found.set(match[1]!, literals.join(' ').replace(/\$\{[^}]*\}/g, ' ').replace(/\s+/g, ' ').trim());
  }
  return found;
}

const MESSAGES = anomalyMessages();

test('every anomaly has a message this test can read', () => {
  // If the extraction pattern stops matching, every assertion below silently passes on an empty set.
  assert.ok(MESSAGES.size >= 13, `only found ${MESSAGES.size} anomaly messages; the extractor has gone stale`);
  for (const [code, text] of MESSAGES) {
    assert.ok(text.length > 40, `${code} has a suspiciously short message`);
  }
});

test('every firmware symbol named in a message still exists in the firmware', () => {
  // Identifiers a reader could go and grep for. Restricted to shapes that are unambiguously firmware
  // symbols so ordinary prose does not trip it.
  const FIRMWARE_SYMBOL = /\b([a-z][a-z0-9]*(?:_[a-z0-9]+){2,}|(?:TimeAligned|Storage|App|BLE|Ranging)Task)\b/g;
  const missing: string[] = [];
  for (const [code, text] of MESSAGES) {
    for (const match of text.matchAll(FIRMWARE_SYMBOL)) {
      const symbol = match[1]!;
      if (!FIRMWARE_TEXT.includes(symbol)) missing.push(`${code} names '${symbol}', which is not in firmware/src`);
    }
  }
  assert.deepEqual(missing, [], 'a message points at a firmware symbol that no longer exists');
});

test('messages do not hardcode a number the constants already carry', () => {
  // A quoted figure that is not interpolated is a figure that cannot drift with the firmware. These
  // are the values most likely to be written out by hand.
  const FORBIDDEN: Array<[RegExp, string]> = [
    [/\b300 s\b/, 'the heartbeat period — interpolate BATTERY_CHECK_INTERVAL_S'],
    [/\b250 ms\b/, 'the charger de-bounce — interpolate BATTERY_EVENT_DEBOUNCE_MS'],
    [/\b120 s\b/, 'the flush timeout — interpolate STORAGE_FLUSH_TIMEOUT_S'],
    [/\b168 s\b/, 'the watchdog window — interpolate WATCHDOG_RESET_WINDOW_S'],
    [/\b2000 ms\b/, 'the time-base threshold — interpolate TIME_BASE_CHANGE_THRESHOLD_MS'],
  ];
  const offences: string[] = [];
  for (const [code, text] of MESSAGES) {
    for (const [pattern, why] of FORBIDDEN) {
      if (pattern.test(text)) offences.push(`${code}: hardcodes ${pattern.source} (${why})`);
    }
  }
  assert.deepEqual(offences, []);
});

test('corrected claims stay corrected', () => {
  // Each entry is a claim that was WRONG in a shipped version of this file, with the firmware
  // evidence that settled it. A regression here means the wrong sentence came back.
  const RETIRED: Array<[string, RegExp, string]> = [
    [
      'charging-event-storm',
      /no debounce|no comparison against the last reported state/i,
      'battery.c:signal_change_accepted debounces and compares against the last reported state',
    ],
    [
      'charger-pin-chatter',
      /kept them out of the log|records look clean/i,
      'battery_monitor_poll_charger_state flushes deferred changes, so the transition does reach the log',
    ],
    [
      'pages-lost',
      /exactly the pages retransmission can recover/i,
      'nandlog_retrieve_retransmit_page_locked returns a zero-length frame for a page that is still unreadable',
    ],
    [
      'rtc-restarted',
      /which the RTC cannot do while it is running/i,
      'live_stats_functionality.c:31 writes the RTC from the BLE characteristic with no guard against an active run',
    ],
  ];
  for (const [code, wrong, evidence] of RETIRED) {
    const text = MESSAGES.get(code);
    assert.ok(text, `anomaly ${code} has gone; if it was renamed, move this assertion rather than deleting it`);
    assert.doesNotMatch(text, wrong, `${code} has regressed to a claim the firmware contradicts — ${evidence}`);
  }
});

test('the claims that replaced them are still being made', () => {
  // The mirror of the previous test: a correction that is merely deleted is not a correction.
  assert.match(MESSAGES.get('charger-pin-chatter')!, /deferred rather than dropped/i);
  assert.match(MESSAGES.get('charger-pin-chatter')!, /reconcile/i);
  assert.match(MESSAGES.get('pages-lost')!, /damaged on the way over|transit/i);
  assert.match(MESSAGES.get('pages-lost')!, /unreadable again|flash content itself/i);
  assert.match(MESSAGES.get('charging-event-storm')!, /charger_suppressed_edges/);
  assert.match(MESSAGES.get('cadence-shortfall')!, /tick divisor truncates/i);
  assert.match(MESSAGES.get('watchdog-near-miss')!, /health evaluation/i);
  assert.match(MESSAGES.get('rtc-restarted')!, /not guarded against an active deployment/i);
});

test('the watchdog message explains only the stall shapes it found', () => {
  // It used to describe what a whole-system stall means on every log, including ones containing
  // none, which reads as a warning about something that did not happen.
  const source = HEALTH.slice(HEALTH.indexOf("const watchdogResets"), HEALTH.indexOf("code: 'watchdog-reset'"));
  assert.match(source, /shapes\.has\('single-task'\)/);
  assert.match(source, /shapes\.has\('whole-system'\)/);
  assert.doesNotMatch(
    MESSAGES.get('watchdog-reset')!,
    /A whole-system shape means/,
    'the unconditional whole-system explanation has come back',
  );
});
