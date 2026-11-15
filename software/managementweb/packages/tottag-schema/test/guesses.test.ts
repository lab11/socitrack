// Enforcement of the guess registry.
//
// The failure mode this prevents is a placeholder quietly becoming load-bearing: a value nobody
// verified, used by code nobody flagged, shipped to someone who assumed it was measured.
//
// Both directions are checked. An unregistered `guess()` call means an assumption entered the code
// without being declared. An `open` registry entry nothing references means either the assumption
// was silently removed — and the entry is now lying about what is provisional — or it was never
// wired up in the first place.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readdirSync, readFileSync, statSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join, relative, resolve } from 'node:path';

import { GUESSES } from '../src/guesses.ts';

const HERE = dirname(fileURLToPath(import.meta.url));
const SRC = resolve(HERE, '..', 'src');
const REGISTRY_FILE = join(SRC, 'guesses.ts');

function sourceFiles(directory: string): string[] {
  const out: string[] = [];
  for (const entry of readdirSync(directory)) {
    const path = join(directory, entry);
    if (statSync(path).isDirectory()) out.push(...sourceFiles(path));
    else if (entry.endsWith('.ts')) out.push(path);
  }
  return out;
}

/** Every `guess('id', ...)` call site outside the registry itself. */
function callSites(): { id: string; file: string; line: number }[] {
  const found: { id: string; file: string; line: number }[] = [];
  for (const file of sourceFiles(SRC)) {
    if (file === REGISTRY_FILE) continue;
    const lines = readFileSync(file, 'utf8').split('\n');
    lines.forEach((text, index) => {
      for (const match of text.matchAll(/\bguess\s*\(\s*['"]([^'"]+)['"]/g)) {
        found.push({ id: match[1]!, file: relative(SRC, file), line: index + 1 });
      }
    });
  }
  return found;
}

test('every guess() call site has a registry entry', () => {
  const unregistered = callSites()
    .filter((site) => !(site.id in GUESSES))
    .map((site) => `${site.file}:${site.line} references unregistered guess '${site.id}'`);

  assert.deepEqual(
    unregistered,
    [],
    'add an entry to src/guesses.ts describing what it blocks and what would resolve it',
  );
});

test('every open registry entry is referenced by at least one call site', () => {
  const referenced = new Set(callSites().map((site) => site.id));
  const orphaned = Object.entries(GUESSES)
    .filter(([id, entry]) => entry.status === 'open' && !referenced.has(id))
    .map(([id]) => id);

  // An open entry with no call site is allowed only while the code that will consume it does not
  // exist yet. Increment 1 landing removed four of the six original exemptions -- two by wiring
  // them up and two by resolving them outright against real deployment data -- which is what this
  // list is for: it shrinks, and the shrinking is visible.
  if (orphaned.length > 0) {
    const pending = orphaned.filter((id) => PENDING_IMPLEMENTATION.has(id));
    const genuinelyOrphaned = orphaned.filter((id) => !PENDING_IMPLEMENTATION.has(id));
    assert.deepEqual(
      genuinelyOrphaned,
      [],
      'open guesses with no call site and no PENDING_IMPLEMENTATION exemption: either wire them up, ' +
        'mark them resolved, or delete them',
    );
    assert.ok(pending.length > 0);
  }
});

/**
 * Guesses whose consuming code has not been written yet.
 *
 * This set must shrink to empty. Each entry needs the increment that will consume it named, so the
 * exemption cannot become permanent by inattention.
 */
const PENDING_IMPLEMENTATION = new Set([
  'imu-accel-scale-unknown-revision',  // consumed by: increment 5, IMU unit display (inferHardware() covers the decidable half)
]);

test('every PENDING_IMPLEMENTATION exemption is a real open guess', () => {
  // Stops the exemption list outliving the guesses it exempts.
  const stale = [...PENDING_IMPLEMENTATION].filter(
    (id) => !(id in GUESSES) || GUESSES[id as keyof typeof GUESSES].status !== 'open',
  );
  assert.deepEqual(stale, [], 'PENDING_IMPLEMENTATION lists guesses that are resolved or gone');
});

test('every registry entry states what it blocks and what would resolve it', () => {
  const incomplete: string[] = [];
  for (const [id, entry] of Object.entries(GUESSES)) {
    if (entry.what.trim().length < 20) incomplete.push(`${id}.what is too short to be meaningful`);
    if (entry.why.trim().length < 20) incomplete.push(`${id}.why is too short to be meaningful`);
    if (entry.blocks.trim().length < 20) incomplete.push(`${id}.blocks is too short to be meaningful`);
    if (entry.resolvedBy.trim().length < 20) incomplete.push(`${id}.resolvedBy is too short to be meaningful`);
  }
  assert.deepEqual(incomplete, []);
});
