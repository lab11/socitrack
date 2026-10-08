# TotTag Web

Browser-based deployment configuration and data processing for TotTag, alongside the Python
desktop tool in `software/management`. Both are maintained: they read the same `.ttg` logs and
speak the same BLE maintenance protocol, so a device does not care which one configured it.

Where the two must agree on a constant, `packages/tottag-schema/test/python-tool-parity.test.ts`
asserts it against the Python source directly. That test is the reason a change to one tool's
retransmission policy fails the build until the other is updated to match.

## Layout

```
packages/tottag-schema/   pure format knowledge: constants, parsing, analysis. No I/O.
  src/constants.ts          firmware facts, read from the drift-checked snapshot
  src/guesses.ts            the guess registry
  src/crc32.ts              CRC-32 as the firmware and zlib both compute it
  src/log.ts                v1 and v2 stream readers, retransmission bookkeeping
  src/health.ts             what a parsed log says about the deployment
  test/fixtures/            real device output, cut from a 3.9-day deployment
app/                      React + TypeScript + Vite.
  src/ports/                interfaces: what a log source is, independent of transport
  src/adapters/             the only modules allowed to touch a browser API
  src/features/             pure decisions: status, restart wording, CSV. Tested under node:test.
  src/components/           rendering only, no decisions
  src/styles/               hand-written design tokens, no CSS framework
```

The dependency runs one way only. `app/` imports from `packages/`; nothing in `packages/` may
import a browser or Node API. If a function there needs bytes, it takes a `Uint8Array` argument.

## Commands

```
npm run dev            start the dev server on http://localhost:5173
npm run ci             drift check, typecheck, tests, build — the whole gate
npm run drift          re-extract firmware constants and confirm they still match the snapshot
npm run drift:update   regenerate the snapshot after an intentional firmware change (-w @tottag/schema)
npm test               node:test, no test framework dependency
npm run open-items -w @tottag/schema
                       print every unresolved assumption and what would settle it
```

**Browser support.** Reading logs and building a deployment work anywhere. Writing to a tag needs
Web Bluetooth, which exists only in Chrome, Edge and Opera on a computer — the app says so up front
rather than letting it surface at the moment someone needs it. Opening a whole folder at once and
choosing where an export goes need the File System Access API, same browsers; both degrade to a file
picker and a plain download elsewhere.

Requires Node >= 22.6 for `--experimental-strip-types`, which is how the tests run TypeScript with
no build step and no dev dependency in the schema package.

## Documentation

User documentation is at [lab11.github.io/socitrack](https://lab11.github.io/socitrack/) — see
[The Dashboard](../../docs/dashboard.md#the-browser-tool) for installing and using this tool.

How this package keeps itself honest against the firmware — drift detection, the guess registry,
record-grammar parity, and the development history — is written up in
[the web tool architecture notes](../../docs/internals/web-tool-architecture.md).
