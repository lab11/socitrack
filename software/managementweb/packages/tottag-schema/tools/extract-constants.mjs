#!/usr/bin/env node
// Extracts the firmware facts declared in spec.mjs into a JSON snapshot.
//
//   node tools/extract-constants.mjs            print the snapshot to stdout
//   node tools/extract-constants.mjs --write    write tools/constants.snapshot.json
//
// Every lookup is required. A constant named in the spec that cannot be found in the firmware is a
// hard error, not a missing key, because the failure mode this whole mechanism exists to prevent is
// a checker that silently stops checking.

import { readFileSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join, resolve } from 'node:path';

import {
  FIRMWARE_ROOT, REVISIONS, CHIPS, DEFINES, PER_CHIP_DEFINES, ENUMS,
  STRUCTS, BLE_UUIDS, SCALE_FACTORS, RECORD_GRAMMAR,
} from './spec.mjs';

const HERE = dirname(fileURLToPath(import.meta.url));
const PACKAGE_ROOT = resolve(HERE, '..');
const SOFTWARE_ROOT = resolve(PACKAGE_ROOT, '..', '..', '..');
const SNAPSHOT_PATH = join(HERE, 'constants.snapshot.json');

class ExtractionError extends Error {}

const fail = (message) => { throw new ExtractionError(message); };

// Blocks the log reserves for its metadata ring. This is a `static` in nandlog.c rather than a
// header constant, so it cannot be extracted by the #define scanner; it is duplicated here only to
// derive usable log capacity, and `constants.test.ts` pins it against the firmware source directly.
const METADATA_RING_BLOCKS = 8;

// --- Source access ---------------------------------------------------------------------------

const sourceCache = new Map();

function readFirmware(relativePath) {
  if (sourceCache.has(relativePath)) return sourceCache.get(relativePath);
  const absolute = join(SOFTWARE_ROOT, FIRMWARE_ROOT, relativePath);
  let text;
  try {
    text = readFileSync(absolute, 'utf8');
  } catch (error) {
    fail(`cannot read ${FIRMWARE_ROOT}/${relativePath}: ${error.message}`);
  }
  // Strip block and line comments so a commented-out definition is never mistaken for a live one.
  const stripped = text.replace(/\/\*[\s\S]*?\*\//g, ' ').replace(/\/\/[^\n]*/g, '');
  sourceCache.set(relativePath, stripped);
  return stripped;
}

// --- Numeric evaluation ----------------------------------------------------------------------

// The firmware writes constants as small C expressions: `(1 + (3 * 10))`, `(32*1000)`, `(-1000)`,
// `(MEMORY_PAGE_SIZE_BYTES - 4)`. Rather than embed a C parser, resolve identifiers against
// already-extracted values and evaluate the arithmetic subset, refusing anything else.
function evaluateNumeric(expression, resolve_, context) {
  let resolved = expression.replace(/\b([A-Za-z_]\w*)\b/g, (_, identifier) => {
    const value = resolve_(identifier);
    if (value === undefined) fail(`${context}: cannot resolve identifier '${identifier}' in '${expression}'`);
    if (typeof value !== 'number') fail(`${context}: identifier '${identifier}' is not numeric`);
    return `(${value})`;
  });

  // Tokenise rather than pattern-match the whole string, so that permitting hex literals does not
  // accidentally permit bare identifiers made of hex letters (`add`, `beef`, `dec`).
  const NUMBER_OR_OPERATOR = /^(?:0[xX][0-9a-fA-F]+|0[bB][01]+|\d+(?:\.\d+)?[uUlL]*|[()+\-*/]|\s+)/;
  for (let rest = resolved; rest.length > 0; ) {
    const token = NUMBER_OR_OPERATOR.exec(rest);
    if (!token) fail(`${context}: refusing to evaluate non-arithmetic expression '${expression}' (at '${rest.slice(0, 24)}')`);
    rest = rest.slice(token[0].length);
  }
  // Integer suffixes are valid C but not valid JavaScript.
  resolved = resolved.replace(/(\d)[uUlL]+\b/g, '$1');
  let value;
  try {
    value = Function(`"use strict"; return (${resolved});`)();
  } catch (error) {
    fail(`${context}: cannot evaluate '${expression}': ${error.message}`);
  }
  if (typeof value !== 'number' || !Number.isFinite(value)) {
    fail(`${context}: '${expression}' did not evaluate to a finite number`);
  }
  return value;
}

// --- #define ---------------------------------------------------------------------------------

function findDefineBody(source, name) {
  // Match `#define NAME <body>` with continuation lines. The lookahead prevents a short name from
  // matching a longer one that starts with it. An immediately-following `(` — with no intervening
  // space — marks a function-like macro, whose value depends on its arguments and so cannot be
  // snapshotted as a constant.
  const pattern = new RegExp(
    `^[ \\t]*#[ \\t]*define[ \\t]+${name}(?![A-Za-z0-9_])(\\()?([^\\n]*(?:\\\\\\n[^\\n]*)*)`,
    'm',
  );
  const match = pattern.exec(source);
  if (!match) return undefined;
  if (match[1]) fail(`${name} is a function-like macro; it cannot be snapshotted as a constant`);
  return match[2].replace(/\\\n/g, ' ').trim();
}

// Values are written into `sink` as they are extracted, so a constant declared in terms of an
// earlier one resolves. This makes spec order significant, which the tests assert.
function extractDefines(entries, source, resolve_, label, sink) {
  for (const entry of entries) {
    const body = findDefineBody(source(entry), entry.name);
    if (body === undefined) fail(`${label}: #define ${entry.name} not found in ${entry.file}`);
    if (body === '') fail(`${label}: #define ${entry.name} has an empty body`);
    sink[entry.name] = evaluateNumeric(body, resolve_, `${label} ${entry.name}`);
  }
  return sink;
}

// --- enum ------------------------------------------------------------------------------------

function extractEnum(spec, resolve_) {
  const source = readFirmware(spec.file);
  // Both `typedef enum { ... } name;` and `enum name { ... };` appear in this codebase.
  const typedefPattern = new RegExp(`typedef\\s+enum\\s*\\{([^}]*)\\}\\s*${spec.name}\\s*;`);
  const namedPattern = new RegExp(`enum\\s+${spec.name}\\s*\\{([^}]*)\\}`);
  const match = typedefPattern.exec(source) ?? namedPattern.exec(source);
  if (!match) fail(`enum ${spec.name} not found in ${spec.file}`);

  const members = {};
  let next = 0;
  let sawExplicit = false;

  for (const raw of match[1].split(',')) {
    const text = raw.trim();
    if (!text) continue;
    const assignment = /^([A-Za-z_]\w*)\s*=\s*(.+)$/.exec(text);
    if (assignment) {
      sawExplicit = true;
      const localResolve = (identifier) =>
        Object.prototype.hasOwnProperty.call(members, identifier) ? members[identifier] : resolve_(identifier);
      next = evaluateNumeric(assignment[2], localResolve, `enum ${spec.name}`);
      members[assignment[1]] = next;
    } else {
      if (!/^[A-Za-z_]\w*$/.test(text)) fail(`enum ${spec.name}: cannot parse member '${text}'`);
      members[text] = next;
    }
    next += 1;
  }

  if (Object.keys(members).length === 0) fail(`enum ${spec.name} parsed as empty`);
  return { members, hasExplicitValues: sawExplicit };
}

// --- struct ----------------------------------------------------------------------------------

const C_TYPE_SIZES = {
  uint8_t: 1, int8_t: 1, char: 1, bool: 1,
  uint16_t: 2, int16_t: 2,
  uint32_t: 4, int32_t: 4, float: 4,
  uint64_t: 8, int64_t: 8, double: 8,
};

function extractStruct(spec, resolve_) {
  const source = readFirmware(spec.file);
  // The body is matched as "no braces" rather than as a lazy any-run. A lazy run anchored at the
  // first `typedef struct` in the file happily spans several declarations to reach the requested
  // name, which silently merges the fields of every struct in between — nandlog.h declares four in
  // a row, so this was not hypothetical. No struct in scope has a nested brace.
  const pattern = new RegExp(
    `typedef\\s+struct\\s*(?:__attribute__\\s*\\(\\([^)]*\\)\\)\\s*)*\\{([^{}]*)\\}\\s*${spec.name}\\s*;`,
  );
  const match = pattern.exec(source);
  if (!match) fail(`struct ${spec.name} not found in ${spec.file}`);

  // The layout below assumes no compiler padding. Every struct on the wire is declared packed, so
  // verify that rather than assume it; an unpacked struct would make every computed offset wrong.
  const declaration = source.slice(match.index, match.index + match[0].length);
  if (!/__attribute__\s*\(\(\s*__?packed__?\s*\)\)/.test(declaration)) {
    fail(`struct ${spec.name} is not declared packed; computed offsets would be unreliable`);
  }

  const fields = [];
  let offset = 0;

  for (const statement of match[1].split(';')) {
    const text = statement.trim().replace(/\s+/g, ' ');
    if (!text) continue;

    const typeMatch = /^((?:unsigned |signed |const )*[A-Za-z_]\w*)\s+(.+)$/.exec(text);
    if (!typeMatch) fail(`struct ${spec.name}: cannot parse declaration '${text}'`);
    const [, cType, declarators] = typeMatch;
    const elementSize = C_TYPE_SIZES[cType];
    if (elementSize === undefined) fail(`struct ${spec.name}: unsupported type '${cType}'`);

    for (const declarator of declarators.split(',')) {
      const text2 = declarator.trim();
      const nameMatch = /^([A-Za-z_]\w*)((?:\s*\[[^\]]+\])*)$/.exec(text2);
      if (!nameMatch) fail(`struct ${spec.name}: cannot parse declarator '${text2}'`);

      const dimensions = [...nameMatch[2].matchAll(/\[([^\]]+)\]/g)].map((d) =>
        evaluateNumeric(d[1].trim(), resolve_, `struct ${spec.name} field ${nameMatch[1]}`),
      );
      const count = dimensions.reduce((a, b) => a * b, 1);
      const size = elementSize * count;

      fields.push({ name: nameMatch[1], cType, elementSize, dimensions, size, offset });
      offset += size;
    }
  }

  if (spec.expectedSize !== undefined && offset !== spec.expectedSize) {
    fail(`struct ${spec.name}: computed size ${offset} but spec expects ${spec.expectedSize}`);
  }
  return { fields, size: offset };
}

// --- 128-bit BLE UUID ------------------------------------------------------------------------

function extractUuid(spec) {
  const body = findDefineBody(readFirmware(spec.file), spec.name);
  if (body === undefined) fail(`BLE UUID ${spec.name} not found in ${spec.file}`);

  const bytes = body.split(',').map((part) => {
    const text = part.trim();
    if (!/^0[xX][0-9a-fA-F]{1,2}$/.test(text)) fail(`BLE UUID ${spec.name}: unexpected byte '${text}'`);
    return Number.parseInt(text, 16);
  });
  if (bytes.length !== 16) fail(`BLE UUID ${spec.name}: expected 16 bytes, found ${bytes.length}`);

  // The firmware stores 128-bit UUIDs little-endian; the canonical text form is big-endian.
  const hex = bytes.slice().reverse().map((b) => b.toString(16).padStart(2, '0')).join('');
  return `${hex.slice(0, 8)}-${hex.slice(8, 12)}-${hex.slice(12, 16)}-${hex.slice(16, 20)}-${hex.slice(20)}`;
}

// --- Record grammar --------------------------------------------------------------------------

// The length of a log record as a function of its type. This is the third copy of that knowledge:
// the firmware needs it to walk a page in `recover_time_anchor()`, the web reader needs it to parse
// one, and the Python tool needs it too. The constant drift check does not cover a function, so
// without this the three could diverge silently -- and a length disagreement does not corrupt one
// record, it desynchronises every record after it in the page.
//
// The parser below recognises exactly the two shapes the firmware currently uses. Anything else is
// a hard failure rather than a best-effort guess, because a grammar that is silently wrong is worse
// than one that is missing: the reader would keep running and produce plausible nonsense.
function extractRecordGrammar(source, resolve_) {
  const signature = /uint32_t\s+stored_record_length\s*\([^)]*\)\s*\{/.exec(source);
  if (!signature) fail('stored_record_length() not found in tasks/storage_records.h');

  // Take the function body by brace matching, so a later function in the file cannot be absorbed.
  let depth = 0;
  let end = -1;
  for (let i = signature.index + signature[0].length - 1; i < source.length; i += 1) {
    if (source[i] === '{') depth += 1;
    else if (source[i] === '}') {
      depth -= 1;
      if (depth === 0) { end = i; break; }
    }
  }
  if (end < 0) fail('stored_record_length(): unbalanced braces');
  const body = source.slice(signature.index + signature[0].length, end);

  const grammar = {};
  let pending = [];
  let sawDefault = false;

  for (const rawLine of body.split('\n')) {
    const line = rawLine.trim();
    if (!line) continue;

    const caseLabel = /^case\s+(STORAGE_TYPE_[A-Z0-9_]+)\s*:\s*(.*)$/.exec(line);
    if (caseLabel) {
      pending.push(caseLabel[1]);
      if (!caseLabel[2]) continue;
    }
    if (/^default\s*:/.test(line)) { sawDefault = true; pending = []; continue; }
    if (/^switch\s*\(/.test(line) || line === '{' || line === '}') continue;

    const returned = /^return\s+(.+?);$/.exec(caseLabel ? caseLabel[2] : line);
    if (!returned) {
      if (pending.length) fail(`stored_record_length(): unrecognised statement under ${pending.join(', ')}: '${line}'`);
      continue;
    }
    if (!pending.length) continue;   // the default arm's return
    const expression = returned[1].trim();

    // Shape 1: a fixed-size record, e.g. `9` or `5 + sizeof(storage_diagnostics_t)`.
    const sizeofResolved = expression.replace(
      /sizeof\s*\(\s*([A-Za-z_]\w*)\s*\)/g,
      (_, name) => {
        const size = resolve_(`sizeof(${name})`);
        if (size === undefined) fail(`stored_record_length(): cannot resolve sizeof(${name})`);
        return `(${size})`;
      },
    );
    if (!/payload|offset|length/.test(sizeofResolved)) {
      const bytes = evaluateNumeric(sizeofResolved, resolve_, 'stored_record_length');
      for (const type of pending) grammar[type] = { kind: 'fixed', bytes };
      pending = [];
      continue;
    }

    // Shape 2: a record whose length is carried in a byte of its own body, e.g.
    //   ((offset + 6) <= length) ? (6 + (payload[offset + 5] * COMPRESSED_RANGE_DATUM_LENGTH)) : 0
    //   ((offset + 6) <= length) ? (5 + payload[offset + 5]) : 0
    const variable = new RegExp(
      '^\\(\\(offset \\+ (\\d+)\\) <= length\\) \\? ' +
      '\\((\\d+) \\+ \\(?payload\\[offset \\+ (\\d+)\\]( \\* ([A-Za-z_]\\w*|\\d+))?\\)?\\) : 0$',
    ).exec(sizeofResolved);
    if (!variable) {
      fail(`stored_record_length(): unrecognised length expression for ${pending.join(', ')}: '${expression}'`);
    }
    const scaleToken = variable[5];
    let scale = 1;
    if (scaleToken !== undefined) {
      scale = /^\d+$/.test(scaleToken) ? Number(scaleToken) : resolve_(scaleToken);
      if (scale === undefined) fail(`stored_record_length(): cannot resolve scale '${scaleToken}'`);
    }
    for (const type of pending) {
      grammar[type] = {
        kind: 'counted',
        base: Number(variable[2]),
        countOffset: Number(variable[3]),
        scale,
        guardBytes: Number(variable[1]),
      };
    }
    pending = [];
  }

  if (pending.length) fail(`stored_record_length(): case labels with no return: ${pending.join(', ')}`);
  if (!sawDefault) fail('stored_record_length(): no default arm, so an unknown type has no defined length');
  if (Object.keys(grammar).length === 0) fail('stored_record_length(): parsed as empty');
  return grammar;
}


// --- Spec coverage ---------------------------------------------------------------------------

/**
 * Every declared spec entry must be present in the produced snapshot.
 *
 * This runs INSIDE extract() rather than in a test, which is the point. A test can be deleted, and
 * more importantly the individual `fail()` calls above can be softened one at a time — a rushed
 * "if (body === undefined) continue;" turns a hard error into a quietly narrower snapshot, and
 * every downstream check then passes on less than it used to. Reconciling the finished snapshot
 * against the spec catches that no matter which lookup was loosened, because the output is missing
 * a key regardless of how it came to be missing.
 *
 * Deliberately not recoverable and not configurable: a partial snapshot has no legitimate use.
 */
function assertCoversSpec(snapshot) {
  const missing = [];
  const require_ = (present, path) => { if (!present) missing.push(path); };

  for (const entry of DEFINES) require_(entry.name in snapshot.defines, `defines.${entry.name}`);
  for (const chip of CHIPS) {
    require_(snapshot.perChip[chip.key] !== undefined, `perChip.${chip.key}`);
    for (const entry of PER_CHIP_DEFINES) {
      require_(snapshot.perChip[chip.key]?.[entry.name] !== undefined, `perChip.${chip.key}.${entry.name}`);
    }
  }
  for (const entry of ENUMS) require_(entry.name in snapshot.enums, `enums.${entry.name}`);
  for (const entry of STRUCTS) require_(entry.name in snapshot.structs, `structs.${entry.name}`);
  for (const entry of BLE_UUIDS) require_(entry.name in snapshot.uuids, `uuids.${entry.name}`);
  for (const entry of SCALE_FACTORS) require_(entry.name in snapshot.scaleFactors, `scaleFactors.${entry.name}`);
  for (const revision of REVISIONS) require_(revision in snapshot.revisionIds, `revisionIds.${revision}`);
  require_(Object.keys(snapshot.recordGrammar ?? {}).length > 0, 'recordGrammar');

  if (missing.length) {
    fail(
      `the snapshot is missing ${missing.length} spec entr${missing.length === 1 ? 'y' : 'ies'}: ` +
      `${missing.join(', ')}. Extraction must produce every entry spec.mjs declares; a partial ` +
      'snapshot silently narrows every check built on it.',
    );
  }
}

// --- Snapshot --------------------------------------------------------------------------------

export function extract() {
  sourceCache.clear();

  // Verify the revision list and IDs still match boards/revisions.h before anything depends on it.
  const revisionSource = readFirmware('boards/revisions.h');
  const revisionIds = {};
  for (const revision of REVISIONS) {
    const symbol = `REVISION_${revision}`;
    const body = findDefineBody(revisionSource, symbol);
    if (body === undefined) fail(`revision ${symbol} not found in boards/revisions.h`);
    revisionIds[revision] = evaluateNumeric(body, () => undefined, `revision ${symbol}`);
  }
  // A revision the firmware defines but the spec omits is as much a drift as the reverse: it means a
  // new board exists that nothing here knows how to select geometry or an IMU scale for.
  for (const [, symbol] of [...revisionSource.matchAll(/^[ \t]*#[ \t]*define[ \t]+(REVISION_[A-Z0-9_]+)/gm)]) {
    const revision = symbol.slice('REVISION_'.length);
    if (!REVISIONS.includes(revision)) {
      fail(`boards/revisions.h defines ${symbol}, which spec.mjs REVISIONS does not list`);
    }
  }

  // Constants resolve against each other and against a handful of values the firmware inherits
  // from the Ambiq SDK or from sizeof(). Those are listed explicitly so an unexpected dependency
  // surfaces as an error rather than being quietly satisfied.
  const known = {
    'sizeof(int16_t)': 2,
    DWT_PLEN_128: 0x04, DWT_PAC8: 0, DWT_BR_6M8: 1, DWT_SFD_DW_16: 1,
  };
  const defines = {};
  const perChip = {};
  const enums = {};

  // Enum members resolve too, because a struct dimension declared as an enum count is ordinary C --
  // `uint8_t late_episodes[WATCHDOG_NUM_TASKS]` is the case that forced this. Defines win on a name clash,
  // since that is the order the C preprocessor would apply them in.
  const resolve_ = (identifier) => {
    if (Object.prototype.hasOwnProperty.call(defines, identifier)) return defines[identifier];
    if (Object.prototype.hasOwnProperty.call(known, identifier)) return known[identifier];
    for (const members of Object.values(enums)) {
      if (Object.prototype.hasOwnProperty.call(members.members, identifier)) return members.members[identifier];
    }
    return undefined;
  };

  // `sizeof(int16_t)` survives comment stripping but not the identifier regex, so pre-substitute it.
  const readWithSizeof = (entry) => readFirmware(entry.file).replace(/sizeof\s*\(\s*int16_t\s*\)/g, '2');

  extractDefines(DEFINES, readWithSizeof, resolve_, 'define', defines);

  for (const spec of ENUMS) enums[spec.name] = extractEnum(spec, resolve_);

  const structs = {};
  for (const spec of STRUCTS) structs[spec.name] = extractStruct(spec, resolve_);

  // `sizeof(storage_diagnostics_t)` appears verbatim in the record grammar, so make every measured
  // struct size resolvable under that spelling rather than special-casing the one that needs it.
  for (const [name, layout] of Object.entries(structs)) known[`sizeof(${name})`] = layout.size;

  // Flash geometry belongs to the fitted part, not the board revision: `nandlog` identifies the part
  // at runtime and each driver declares its own geometry before including nandlog_chip_common.h.
  for (const chip of CHIPS) {
    const entries = PER_CHIP_DEFINES.map((entry) => ({ ...entry, file: chip.file }));
    perChip[chip.key] = {};
    const chipResolve = (identifier) => perChip[chip.key][identifier] ?? resolve_(identifier);
    extractDefines(entries, readWithSizeof, chipResolve, `chip ${chip.key}`, perChip[chip.key]);

    // The page header is what separates a page's physical size from its usable payload, and getting
    // that wrong is silent: a reader would simply mis-slice every page. Derive it here, from the
    // measured struct rather than from a literal, so the snapshot carries the number a parser
    // actually needs and it cannot drift away from the header it describes.
    const geometry = perChip[chip.key];
    geometry.PAYLOAD_BYTES_PER_PAGE = geometry.NANDLOG_CHIP_PAGE_SIZE_BYTES - structs.nandlog_page_header_t.size;
    geometry.TOTAL_PAGES = geometry.NANDLOG_CHIP_PAGES_PER_BLOCK * geometry.NANDLOG_CHIP_BLOCK_COUNT;
    geometry.LOG_CAPACITY_BYTES =
      (geometry.NANDLOG_CHIP_BLOCK_COUNT - geometry.NANDLOG_CHIP_RESERVED_BLOCKS - METADATA_RING_BLOCKS) *
      geometry.NANDLOG_CHIP_PAGES_PER_BLOCK * geometry.PAYLOAD_BYTES_PER_PAGE;

    if (geometry.NANDLOG_CHIP_PAGE_SIZE_BYTES > defines.NANDLOG_MAX_PAGE_SIZE_BYTES) {
      fail(`chip ${chip.key}: page size exceeds NANDLOG_MAX_PAGE_SIZE_BYTES, which the firmware asserts against`);
    }
  }

  // The firmware asserts this statically (`system.h`), and the host relies on it to turn the four
  // diagnostic bits of a reset record into a task name. Re-check it here so a reordering that
  // slipped past the C assert cannot reach a decoder.
  const stallBase = enums.reset_diagnostic_t.members.RESET_DIAGNOSTIC_STALL_TIME_ALIGNED;
  const taskCount = enums.watchdog_task_t.members.WATCHDOG_NUM_TASKS;
  if (stallBase + taskCount !== enums.reset_diagnostic_t.members.RESET_DIAGNOSTIC_STALL_MULTIPLE) {
    fail('reset_diagnostic_t per-task stall codes are no longer contiguous with watchdog_task_t');
  }

  // Record lengths, parsed out of the firmware's own page walker. Extracted after structs and
  // defines because it resolves `sizeof(storage_diagnostics_t)` and COMPRESSED_RANGE_DATUM_LENGTH.
  const recordGrammar = extractRecordGrammar(readFirmware(RECORD_GRAMMAR.file), resolve_);

  // Every record type the firmware can write must have a length rule, or a reader walking a framed
  // page would stop at the first one it met. SHUTDOWN is the exception: it is a queue message that
  // triggers a flush, never a stored record, and STORAGE_NUM_TYPES is the count.
  for (const [name, value] of Object.entries(enums.storage_data_type_t.members)) {
    if (name === 'STORAGE_TYPE_SHUTDOWN' || name === 'STORAGE_NUM_TYPES') continue;
    if (!recordGrammar[name]) {
      fail(`storage_data_type_t defines ${name} (=${value}) but stored_record_length() gives it no length`);
    }
  }
  for (const name of Object.keys(recordGrammar)) {
    if (!(name in enums.storage_data_type_t.members)) {
      fail(`stored_record_length() handles ${name}, which storage_data_type_t does not define`);
    }
  }

  const uuids = {};
  for (const spec of BLE_UUIDS) uuids[spec.name] = { uuid: extractUuid(spec), implemented: spec.implemented };

  const scaleFactors = {};
  for (const spec of SCALE_FACTORS) {
    const body = findDefineBody(readFirmware(spec.file), spec.name);
    if (body === undefined) fail(`scale factor ${spec.name} not found in ${spec.file}`);
    scaleFactors[spec.name] = evaluateNumeric(body, () => undefined, `scale factor ${spec.name}`);
  }

  const snapshot = {
    $comment: 'Generated by tools/extract-constants.mjs from firmware/src. Do not hand-edit; run `npm run drift:update`.',
    revisionIds,
    defines,
    perChip,
    enums,
    structs,
    recordGrammar,
    uuids,
    scaleFactors,
  };

  // Last thing before anything can use it.
  assertCoversSpec(snapshot);
  return snapshot;
}

export function readSnapshot() {
  return JSON.parse(readFileSync(SNAPSHOT_PATH, 'utf8'));
}

export { SNAPSHOT_PATH, ExtractionError };

if (process.argv[1] && resolve(process.argv[1]) === resolve(fileURLToPath(import.meta.url))) {
  try {
    const snapshot = extract();
    const json = `${JSON.stringify(snapshot, null, 2)}\n`;
    if (process.argv.includes('--write')) {
      writeFileSync(SNAPSHOT_PATH, json);
      process.stderr.write(`wrote ${SNAPSHOT_PATH}\n`);
    } else {
      process.stdout.write(json);
    }
  } catch (error) {
    process.stderr.write(`extraction failed: ${error.message}\n`);
    process.exit(1);
  }
}
