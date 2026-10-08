#!/usr/bin/env node
// Print every unresolved assumption, for a human deciding what to measure next.
//
// Taken from the sibling A3EM dashboard, which has the same problem and solved it the same way. The
// guess registry already enforces itself in tests; what it lacked was a way to READ it without
// opening a source file, which is what makes it usable in a status meeting rather than only in CI.

import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const source = readFileSync(join(dirname(fileURLToPath(import.meta.url)), '..', 'src', 'guesses.ts'), 'utf8');

// Read the registry as text rather than importing it: this must run with no build step and no
// type-stripping flag, so it works from any shell.
const entries = [...source.matchAll(/^ {2}'([a-z0-9-]+)': \{\n([\s\S]*?)\n {2}\},$/gm)].map(([, id, body]) => {
  const field = (name) => {
    const match = new RegExp(`^ {4}${name}:\\s*\\n?((?:\\s*'(?:[^'\\\\]|\\\\.)*'\\s*\\+?\\s*\\n?)+)`, 'm').exec(body);
    if (!match) return '';
    return [...match[1].matchAll(/'((?:[^'\\]|\\.)*)'/g)]
      .map((m) => m[1].replace(/\\'/g, "'"))
      .join('')
      .replace(/\s+/g, ' ')
      .trim();
  };
  const status = /status: '(\w+)'/.exec(body)?.[1] ?? 'unknown';
  const references = [...body.matchAll(/'([^']*:\d+)'/g)].map((m) => m[1]);
  return { id, status, what: field('what'), why: field('why'), blocks: field('blocks'), resolvedBy: field('resolvedBy'), references };
});

if (entries.length === 0) {
  process.stderr.write('could not read src/guesses.ts — the registry format has changed\n');
  process.exit(1);
}

const wrap = (text, width, indent) => {
  const words = text.split(' ');
  const lines = [];
  let line = '';
  for (const word of words) {
    if ((line + word).length > width) { lines.push(line.trimEnd()); line = ''; }
    line += `${word} `;
  }
  if (line.trim()) lines.push(line.trimEnd());
  return lines.map((l, i) => (i === 0 ? l : indent + l)).join('\n');
};

const open = entries.filter((entry) => entry.status === 'open');
const resolved = entries.filter((entry) => entry.status !== 'open');

process.stdout.write(`\n${open.length} open, ${resolved.length} settled\n`);
for (const entry of open) {
  process.stdout.write(`\n\x1b[1m${entry.id}\x1b[0m\n`);
  process.stdout.write(`  what       ${wrap(entry.what, 92, '             ')}\n`);
  process.stdout.write(`  blocks     ${wrap(entry.blocks, 92, '             ')}\n`);
  process.stdout.write(`  resolve by ${wrap(entry.resolvedBy, 92, '             ')}\n`);
  if (entry.references.length) process.stdout.write(`  see        ${entry.references.join(', ')}\n`);
}
if (resolved.length) {
  process.stdout.write('\nSettled:\n');
  for (const entry of resolved) process.stdout.write(`  ${entry.status.padEnd(9)} ${entry.id}\n`);
}
process.stdout.write('\n');
