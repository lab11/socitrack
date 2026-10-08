#!/usr/bin/env node
// Writes the fixture that holds the Python dashboard's radio check to this package's.
//
//   node --experimental-strip-types tools/radio-check-parity.mjs            print the fixture to stdout
//   node --experimental-strip-types tools/radio-check-parity.mjs --write    write test/fixtures/radio-check-parity.json
//
// The scenarios live in test/radioParityFixture.ts. Rewrite the fixture after changing a rule, a threshold or a
// message in radioCheck.ts or liveRadio.ts, then make management/dashboard/test_radio_check.py pass again.

import { writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

import { buildRadioParityFixture } from '../test/radioParityFixture.ts';

const HERE = dirname(fileURLToPath(import.meta.url));
export const FIXTURE_PATH = join(HERE, '..', 'test', 'fixtures', 'radio-check-parity.json');

// Indented, but with every array of plain values on one line, so a changed answer shows as a readable diff
function format(value, indent = '') {
  if (Array.isArray(value)) {
    if (value.every((item) => item === null || typeof item !== 'object')) return JSON.stringify(value);
    return `[\n${value.map((item) => `${indent} ${format(item, `${indent} `)}`).join(',\n')}\n${indent}]`;
  }
  if (value && typeof value === 'object') {
    const entries = Object.entries(value).filter(([, item]) => item !== undefined);
    return `{\n${entries.map(([key, item]) => `${indent} ${JSON.stringify(key)}: ${format(item, `${indent} `)}`).join(',\n')}\n${indent}}`;
  }
  return JSON.stringify(value);
}

const text = `${format(buildRadioParityFixture())}\n`;
if (process.argv.includes('--write')) writeFileSync(FIXTURE_PATH, text);
else process.stdout.write(text);
