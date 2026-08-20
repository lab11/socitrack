# nandlog

Append-only, power-fail-safe, CRC-validated logging directly onto raw SPI NAND, with no flash translation
layer.

It is built for devices that collect data for weeks and hand it over in one go: a battery-powered sensor, a
tag, a logger. It assumes writes are small and frequent, reads are rare and bulk, and power can be lost at any
instant without warning. It does not assume a filesystem, a heap, an RTOS, or that the part is healthy.

## What it does and does not do

**Does.** Stores opaque pages. Survives power loss at any point, including part-way through a program or an
erase. Detects corruption rather than serving it. Retires blocks the part refuses to write or erase, and
carries on. Recovers the write head after a reboot without a scan of the whole array. Serves bulk reads bounded
by timestamp, and re-serves individual pages a host reports it lost.

**Does not.** Interpret what is in a record — a record's length is a property of whoever wrote it, and the log
never parses one. Read a clock; timestamps are supplied, compared, and never interpreted. Allocate. Wear-level
beyond sweeping the head forward. Provide random-access update: a page is written once and never revised.

## Layering

Four seams, each of which exists because something concrete had to cross it.

```
        application
    ─────────────────────────  nandlog.h        the log's API: pages in, pages out
        nandlog.c              the log core: framing, CRCs, epochs, the write head, bad-block policy
    ─────────────────────────  nandlog_chip.h   what the log needs from a part
        chips/nandlog_chip_<PART>.c             one self-contained driver per part
    ─────────────────────────  nandlog_port.h   ten functions: two transfers, init, power, delays, log, fatal
        your nandlog_port.c    the only file you write
```

`nandlog_conf.h` sits beside all of it and is the only file you edit rather than write.

**The chip seam is at the SPI command level, not the page level.** That is deliberate, and it is what makes
the host simulator worth having: `sim/nandlog_port_sim.c` emulates a NAND part, and the real chip driver runs
on top of it unmodified. Addressing, status-register handshakes, busy-waiting and bad-block bookkeeping are
all shipping code under test, not a stub that agrees with itself.

## Porting it

1. **Write `nandlog_port.c`.** Ten functions. Two are SPI transfers; the rest are init, deinit, a write-protect
   pin, power, two delays, a log sink, and a fatal handler. `nandlog_port_fatal()` is called when the part
   stops responding and the library has exhausted its retries — a library has no business resetting the system
   it is embedded in, so what happens next is yours.
2. **Set `nandlog_conf.h`.** Seven values, all genuine choices: the largest page and spare area you will spend
   RAM on, and five policy knobs. Nothing is derived, and anything missing is a named compile error.
3. **Add a chip driver, if yours is not already there.** One `.c` file under `chips/`. State the geometry,
   include `nandlog_chip_common.h`, and implement nine functions. Two drivers are supplied; read either.

There is no build system. Add the sources to yours, as you would FatFS.

### Adding a part

```c
#define NANDLOG_CHIP_NAME                    "AS5F18G04SND"
#define NANDLOG_CHIP_PAGE_SIZE_BYTES         4096
#define NANDLOG_CHIP_SPARE_SIZE_BYTES        256
#define NANDLOG_CHIP_PAGES_PER_BLOCK         64
#define NANDLOG_CHIP_BLOCK_COUNT             4096
#define NANDLOG_CHIP_RESERVED_BLOCKS         80
#include "nandlog_chip_common.h"
```

Geometry lives in the driver and nowhere else — not in a board header, not in the configuration file — so
there is no second place for it to be wrong. `nandlog_chip_common.h` holds no command codes, no register
numbers and no bit patterns, and there is no shared implementation beneath it. **Each driver carries its own
copy of the mechanics it needs, even where two parts answer the same commands**, because that agreement is a
coincidence of history rather than a contract. Duplication is the price of a file that is complete on its own
and never has to be edited in sympathy with another.

## On-flash format

The array is divided into three regions.

| region | contents |
|---|---|
| metadata ring | the first 8 blocks: a ring of slots, each describing one epoch |
| log region | everything between the ring and the reserve |
| reserve | the top *N* blocks, held by the chip driver for bad-block management |

**Epochs.** Every generation of the log has a number. Starting a new one writes a fresh metadata slot naming
the epoch and the page its sequence 0 lives at; the previous generation's pages stay on the part but stop
being reachable, because reads only ever cover the current epoch. A page carrying a stale epoch is therefore
unmistakable rather than merely unexpected, which is what makes recovery after a reboot decidable.

**Pages.** Each is self-describing and self-validating: a header with the magic, epoch, sequence number, the
timestamps of its first and last records, its payload length and record count, then a CRC over the payload and
a CRC over the header. The two checksums are separate on purpose — the header CRC lets the log trust the
length and sequence fields before it has read the payload, which is what makes a binary search over the log
possible at all.

**Recovery.** On boot the log reads the ring, takes the highest valid epoch, and binary-searches the log region
for the first page that does not belong to it. That is the write head, found in log(n) reads rather than a
scan. The last page of the epoch supplies the sequence number to continue from.

**Erase-ahead.** A rolling window of erased blocks is kept ahead of the head, topped up from the middle of each
block, so that committing a page never waits on an erase.

## Failure, and what is done about it

| failure | response |
|---|---|
| program reports failure | retried; then the block is retired and any pages already written in it are moved |
| written page does not read back with a valid header | same — the header CRC makes this exact rather than ECC-dependent |
| erase reports failure | the block is retired |
| power lost mid-program | the torn page fails its CRC on the next boot and is treated as a gap |
| power lost mid-erase | the partially erased block reads as a gap and is rewritten or retired |
| payload CRC mismatch on read | that page is reported as zero-length: a gap, not the end of the log |
| part stops responding | bounded busy-wait, then `nandlog_port_fatal()` |
| bad-block table implausible | ignored rather than acted on, and rebuilt |

The consistent rule: **a page that cannot be trusted is a gap, and a gap is not the end of the log.** Reads
step over it. Offload reports it as a zero-length page carrying its sequence number, so a host can tell "still
missing" from "never answered" and ask for it again.

## Offload

`nandlog_begin_reading()` resolves both time bounds up front — deliberately, because an earlier version
resolved one and silently discarded the other, and a date-bounded download returned four days of data instead
of two while reporting no loss. Both bounds are established in one call so neither can be forgotten.

A stream is a header, the caller's metadata blob, then each page framed with its sequence number, timestamps,
length, record count and payload CRC. `tools/nandlog_parse.py` is the reference reader for both this and a raw
image dump. The device computes CRC-32 with the IEEE 802.3 polynomial precisely so the host can verify it with
`zlib.crc32` and nothing bespoke.

`nandlog_read_span()` reports how many pages a read will yield and, optionally, how many payload bytes.
**The byte total costs a read of every page in the span** — it doubles the flash traffic of a download — so
pass `NULL` unless the figure is needed exactly. It validates page headers but not payload CRCs, so it is an
upper bound: a page whose payload has rotted still contributes its advertised length. That is the safe
direction for a host sizing a buffer.

## Testing

```
cd sim && make          # host build: the real log and chip driver over a RAM-backed part
```

The simulator honours NAND semantics rather than approximating them: erase sets bits, programming may only
clear them, and a program cut short leaves exactly the prefix that made it. It injects unwritable and
unerasable blocks, mid-operation power loss, bit rot, and a part that never clears BUSY. Getting those wrong
would make the simulator agree with a buggy log.

Host tests do not replace on-device testing. They cannot say anything about the real part's timing, its ECC,
or the board. What they cover is everything above the SPI wire, in milliseconds, with faults that are
impractical to stage on hardware.

## Deliberate omissions

**Record framing.** The log stores pages, not records. Adding an explicit length field would let a generic
reader walk records without knowing the application's types, at a cost of about one byte per record — 11–17%
on the payloads this was built for. Worth revisiting; not free.

**Wear levelling.** The head sweeps forward and wraps. For a log that fills over weeks that is enough, and
anything cleverer would need a mapping table, which is the FTL this exists to avoid.
