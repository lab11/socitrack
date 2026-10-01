# nandlog on-device tests

Three test applications that answer questions the host simulator cannot. The simulator covers everything
above the SPI wire; these cover the wire, the silicon, and the clock.

| test | what it gates | writes to the part? |
|---|---|---|
| `test_nandlog_partial_read.c` | column-addressed and short page reads — **shipping code that has never run on silicon** | one scratch page |
| `test_nandlog_page_copy.c` | `nandlog_chip_copy_page()` — **shipping code that has never run on silicon** | three scratch blocks |
| `test_nandlog_ecc_marker.c` | whether bad-block marking in the spare area is possible at all — **gates a future redesign, nothing shipping** | four scratch blocks |

All three work only in the **top blocks of the reserve**, which the log never places a page in. None of them
touches the log region, the metadata ring, or the chip driver's bad-block marker page. They are still write
tests: run them on a development board, not on a device holding data you want.

Run them in order. If test 1 fails, tests 2 and 3 tell you nothing, because both read results back through
the primitive test 1 is checking.


## Building them under SociTrack

The tests are written against the SociTrack firmware's `setup_hardware()`, `print()` and `am_hal_delay_us()`,
because that is where the hardware is. Four changes to `software/firmware/tests/Makefile`:

**1. Add this directory to `VPATH`,** next to the two nandlog lines that are already there:

```make
VPATH += ../src/external/nandlog
VPATH += ../src/external/nandlog/chips
VPATH += ../src/external/nandlog/tests          # <-- add
```

`INCLUDES` already has `-I../src/external/nandlog`, and that is the only include path these need.

**2. Add the three targets,** alongside `storage:`:

```make
nandlog_reads: TARGET = TestNandlogReads
nandlog_reads: SRC += test_nandlog_partial_read.c nandlog_hw_test.c
nandlog_reads: $(CONFIG) $(CONFIG)/test_nandlog_partial_read.o $(CONFIG)/nandlog_hw_test.o $(CONFIG)/$$(TARGET).bin program

nandlog_copy: TARGET = TestNandlogCopy
nandlog_copy: SRC += test_nandlog_page_copy.c nandlog_hw_test.c
nandlog_copy: $(CONFIG) $(CONFIG)/test_nandlog_page_copy.o $(CONFIG)/nandlog_hw_test.o $(CONFIG)/$$(TARGET).bin program

nandlog_marker: TARGET = TestNandlogMarker
nandlog_marker: SRC += test_nandlog_ecc_marker.c nandlog_hw_test.c
nandlog_marker: $(CONFIG) $(CONFIG)/test_nandlog_ecc_marker.o $(CONFIG)/nandlog_hw_test.o $(CONFIG)/$$(TARGET).bin program
```

**3. Add them to `.PHONY`** and to the `all:` error message, next to `storage`.

**4. Check that `AM_DEBUG_PRINTF` is on** — it already is in this Makefile — because the output *is* the
result. Watch it over SWO or the Segger RTT console, whichever you normally use.

Then:

```
cd software/firmware/tests
make nandlog_reads BOARD_REV=P
```

A note on timing: every figure these print comes from the DWT cycle counter, calibrated at startup against
`am_hal_delay_us(10000)` rather than against an assumed core clock. If the first line says
`WARNING: DWT is not counting`, every timing number afterwards is a fiction — DWT is gated without a debugger
on some parts, so attach one.


## Test 1 — `nandlog_reads`

Five parts. **1b and 1c are the ones that matter.**

- **1a** short reads from offset 0 agree with a whole-page read.
- **1b** reads from ten different non-zero columns land where they claim. If the 16-bit column address goes
  out in the wrong byte order, this is what catches it — and it prints what a byte-swapped address *would*
  have read, so a failure names its own cause.
- **1c** the spare-area marker byte reads the same whether you address its column directly or drag the whole
  page along in front of it. This is run over **every block on the part**.
- **1d** a page header reads correctly on its own, and the write head still recovers across a re-init — the
  boot path is now entirely header-only reads.
- **1e** boot cost, and a header read against a whole-page read.

**Why this one is not optional.** A wrong column byte order does not fail loudly. It reads `0xFF` from the
wrong place, the factory bad-block scan concludes every block is good, and the log goes on working until it
writes into a block the manufacturer had already condemned. The simulator cannot catch it, because it decodes
the column address the same way the driver encodes it.

**One caveat the test prints for itself:** if the part reports no factory-bad blocks at all, 1c compared
`0xFF` against `0xFF` and proved nothing. 1b is then the only evidence, which is why there are ten offsets
in it rather than one.

**Report back:** the PASS/FAIL line, the factory bad-block count, and the whole of 1e.


## Test 2 — `nandlog_copy`

Four parts, and it asks to be run twice with a power cycle in between.

- **2a** a copied page is the source page, and reads back without an ECC error. The test deliberately leaves
  a *different* page in the cache register first, so a part that ignores the copy sequence and programs
  whatever the register holds produces a wrong page rather than an empty one — and the test says which.
- **2b** the copy did not drag the source's spare area across. **This is the subtle one.** If it did, a
  relocated page could carry a non-`0xFF` byte into a destination block's marker position, and a later scan
  would retire a perfectly good block.
- **2c** a run of eight pages including an erased one, which catches a stale cache register.
- **2d** what it costs against a read and a write back, per page and per block.

**Report back:** the PASS/FAIL line and 2d's timing table. Then **power-cycle and run it again** — it checks
the previous run's copied page on startup, before erasing anything.

**What the result means.** If 2a fails, set `NANDLOG_CHIP_PAGE_COPY` to 0 in `nandlog_conf.h` before
deploying; the log falls back to reading and writing each page and everything else still works. If only 2b
fails, the copy works but the driver has to clear the destination's marker byte after each relocation — tell
me and I will add it.


## Test 3 — `nandlog_marker`

This gates nothing that ships. It answers the single question that decides whether nandlog can delete its
persisted bad-block apparatus — the 80-block reserve, the marker page, the reload path, and the "table
reports an implausible count, ignore it" branch — and mark a retired block in its own spare area instead.

It talks to the chip directly rather than through the driver, because it has to turn ECC off and program a
single byte at a column address, and no driver function exposes either. The command codes at the top of the
file are for the Alliance AS5F18G04SND; change them for another part.

| step | question |
|---|---|
| 0 | did a marker from a previous run survive the power cycle? |
| 1 | what are the status registers as shipped, and is ECC on? |
| 2 | **can ECC be turned off and back on?** If not, stop — spare-area marking is impossible on this part |
| 3 | with ECC off, does a `0x00` byte programmed at the spare's column actually take? |
| 4 | with ECC back on, is that byte visible — or does the page now report an uncorrectable error? |
| 5 | the NOP question: can the spare be programmed *after* the main array has been? |
| 6 | how many blocks does the factory mark bad, and **how long does a full-array scan take?** |

**Step 4 has three possible outcomes and all three are informative:**

- the marker reads back as `0x00` with ECC on → the redesign works exactly as drawn;
- the marker is invisible but the page reports an uncorrectable error → still usable, because the scan
  already treats an unreadable page 0 as a bad block;
- the marker is invisible and the page reads clean → **negative result**, a retired block would be
  indistinguishable from a good one after a reboot, and the persisted table has to stay.

**Step 5 matters more than it looks.** A block gets retired because it has *just refused a program*, so the
marker often has to go into a page that already holds data. If the part allows only one partial program per
page, it cannot.

**Step 6 prints the deciding number.** A scheme with no persisted table rebuilds it from the spare area on
every boot, and that scan is what it costs. Weigh it against the 80 blocks — 2% of the array — it would let
us reclaim.

**Report back: every line from step 0 onwards.** Most of this test has no pass/fail verdict; the values are
the answer. Then **power-cycle and run it again** — the marker block is deliberately left programmed at the
end of a run so the next boot's step 0 has something to find, and a marker that does not survive a power
cycle is no marker at all.
