#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Recover deployment logs from TotTags running the pre-nandlog firmware (anything before cf7249d5).

That firmware found its log at boot by pattern-matching page markers, and when the search failed it wrote a
fresh, empty metadata page and carried on -- so a tag can report that it has no log while every page of the log
is still on its flash. This tool works from a raw dump of the flash instead, taken by the read-only recovery
firmware in firmware/tests/tools/legacy_log_recovery.c, and rebuilds the log from the pages themselves.

   Flash the recovery firmware:   cd software/firmware/tests && make clean log_recovery          (revs O, P: USB)
                                                               make clean log_recovery_segger   (any rev: J-Link)
   Take a dump and recover it:    python3 legacy_recovery.py usb -o OUTDIR
                                  python3 legacy_recovery.py rtt -o OUTDIR
   Re-analyse an existing dump:   python3 legacy_recovery.py recover OUTDIR/<uid>_<time>.ttrd -o OUTDIR

A dump is saved before anything else is done with it. KEEP IT: it is the complete contents of the tag's flash,
it can be re-analysed with different options (or a better version of this tool) after the tag has been wiped,
and nothing else can bring that data back.

Outputs, named as the dashboard names a download:
   <label>_<start>.pkl             the recovered records, in the same form tottag.py writes
   <label>_<start>.ttg             the recovered payload bytes in log order, as a legacy download would have been
   <label>_<start>_recovery.txt    what was found on the flash, what was used, and what was not

How the old firmware laid out its log (storage.c before cf7249d5):
   * One "META" page, at the first page of a block, holding the experiment details.
   * Data pages from the next page on: 'D','A', a u16 payload length, then the payload. Records were cut at the
     page boundary regardless of where they ended, so records straddle pages.
   * Pages advance one at a time through the data region, which ends where the bad-block reserve begins, and
     wrap to page 0. Blocks listed as bad are skipped over.
   * Scheduling a deployment erased the whole data region first, so only one deployment's pages are expected.

That layout held from April 2023 until cf7249d5. What did change in that time is recognised from the dump itself:
the shape of the experiment details, how record timestamps were written, and the size of the Alliance part's
bad-block reserve. The report says which of each was found.

Because records straddle pages, two pages can only be joined if one directly followed the other when they were
written. Wherever that cannot be shown -- a missing or damaged page, a partial page written at shutdown -- the
payload is split into separate segments that are decoded independently. A record straddling the break is lost,
cleanly, rather than being glued to unrelated bytes and decoded as something it never was.
"""

# PYTHON INCLUSIONS ---------------------------------------------------------------------------------------------------

try: from . import tottag_format
except ImportError: import tottag_format
from collections import defaultdict
import argparse, datetime, os, pickle, struct, subprocess, sys, time, zlib


# CONSTANTS AND DEFINITIONS -------------------------------------------------------------------------------------------

# The frame stream sent by legacy_log_recovery.c; see "Wire Format" there
FRAME_SYNC = b'TTRC'
FRAME_HEADER = struct.Struct('<BBH')
FRAME_OVERHEAD = len(FRAME_SYNC) + FRAME_HEADER.size + 4
FRAME_MAX_PAYLOAD = 8 + 8192
FRAME_INFO, FRAME_LUT, FRAME_PAGE, FRAME_PROGRESS, FRAME_END, FRAME_ERROR = 1, 2, 3, 4, 5, 6
PAGE_FLAG_UNCORRECTABLE = 0x01
INFO_FRAME = struct.Struct('<HBB4sIIIII6s3sB32s')
PAGE_FRAME_HEADER = struct.Struct('<IBBH')
PROGRESS_FRAME = struct.Struct('<IIII')
PROTOCOL_VERSION = 1

CHIP_NAMES = {0: 'unknown', 1: 'W25N01GW', 2: 'AS5F18G04SND'}
CHIP_W25N01GW = 1
TRANSPORT_NAMES = {0: 'host simulation', 1: 'USB', 2: 'RTT'}
REVISION_NAMES = {0x13: 'M', 0x14: 'N', 0x15: 'O', 0x16: 'P'}

# The old on-flash layout
META_MAGIC = b'META'
DATA_MAGIC = b'DA'
BBM_MAGIC = b'BBM_'
DATA_HEADER_BYTES = 4
BBM_TABLE_ENTRIES = 256
W25N_LUT_ENTRIES = 20
W25N_LUT_ADDRESS_MASK = 0x3FF

# experiment_details_t took three shapes over the old firmware's life, all read here: without use_daily_times before
# 23b8c90c (2023-10-30), then with it, then with is_terminated too from 383cb1b2 (2025-04-22). The last two differ
# only in a trailing byte that the zero padding after them makes harmless, so one decoder serves both
MAX_NUM_DEVICES = 10
MAX_LABEL_LENGTH = 16
_DEVICE_TABLES = str(6 * MAX_NUM_DEVICES) + 's' + str(MAX_LABEL_LENGTH * MAX_NUM_DEVICES) + 's'
DETAILS = struct.Struct('<IIIIBB' + _DEVICE_TABLES + 'B')
DETAILS_BEFORE_DAILY_TIMES = struct.Struct('<IIIIB' + _DEVICE_TABLES)

# How a record's timestamp was written. Firmware from 1fdd7646/de82dea1 (March 2024) on stored milliseconds since the
# deployment started, rounded to 500 ms; before that it stored the RTC's Unix seconds. (The three weeks of builds
# between them used transitional encodings that are not supported.) Either is recognised from the data itself
TIMESTAMPS_RELATIVE = 'relative'
TIMESTAMPS_ABSOLUTE = 'absolute'
TIMESTAMP_DESCRIPTIONS = {
   TIMESTAMPS_RELATIVE: 'milliseconds since the deployment started (firmware from March 2024 on)',
   TIMESTAMPS_ABSOLUTE: 'Unix seconds from the RTC (firmware from before March 2024)',
}
ERA_SAMPLE_BYTES = 256 * 1024

# The Alliance part's reserve was 20 blocks in the firmware of 1aedc941..d535a3a3 (Nov-Dec 2024), and 80 from then
# on. That firmware also never applied its bad-block table after a reboot (it reloaded the entries but not their count)
EARLY_ALLIANCE_RESERVED_BLOCKS = 20

# Anything before this is not a real deployment start (2017-07-14)
EARLIEST_PLAUSIBLE_START = 1500000000

TOTTAG_USB_VID = 0x1209
TOTTAG_USB_PID = 0x2828
RTT_CONTROL_BLOCK_ADDRESS = '0x1005FFA0'
DEFAULT_JLINK_DEVICE = 'AMA4B2KK-KBR'      # as software/firmware/Jtag.mk flashes with
IDLE_TIMEOUT_S = 60


# FRAME STREAM --------------------------------------------------------------------------------------------------------

class FrameParser:
   """Incremental parser for the recovery frame stream.

   Tolerates a stream that starts part-way through a frame, and bytes lost or corrupted in transit: anything
   that does not pass its CRC is skipped a byte at a time until the next sync word that does.
   """

   def __init__(self):
      self.buffer = bytearray()
      self.bad_frames = 0

   def feed(self, data):
      self.buffer += data
      frames, i = [], 0
      while True:
         j = self.buffer.find(FRAME_SYNC, i)
         if j < 0:
            i = max(i, len(self.buffer) - (len(FRAME_SYNC) - 1))
            break
         if j + len(FRAME_SYNC) + FRAME_HEADER.size > len(self.buffer):
            i = j
            break
         frame_type, flags, length = FRAME_HEADER.unpack_from(self.buffer, j + len(FRAME_SYNC))
         if length > FRAME_MAX_PAYLOAD:
            self.bad_frames += 1
            i = j + 1
            continue
         end = j + FRAME_OVERHEAD + length
         if end > len(self.buffer):
            i = j
            break
         body = bytes(self.buffer[j + len(FRAME_SYNC):end - 4])
         if zlib.crc32(body) != struct.unpack_from('<I', self.buffer, end - 4)[0]:
            self.bad_frames += 1
            i = j + 1
            continue
         frames.append((frame_type, flags, body[FRAME_HEADER.size:]))
         i = end
      del self.buffer[:i]
      return frames

   @property
   def pending_bytes(self):
      return len(self.buffer)


class Page:
   __slots__ = ('page', 'data', 'uncorrectable', 'status_register_3', 'reads')

   def __init__(self, page, data, uncorrectable, status_register_3, reads):
      self.page, self.data, self.uncorrectable = page, data, uncorrectable
      self.status_register_3, self.reads = status_register_3, reads


class Dump:
   """One complete pass over the flash, as the recovery firmware reported it."""

   def __init__(self):
      self.info, self.lut, self.pages, self.end, self.errors = None, None, {}, None, []
      self.last_progress = None

   @property
   def total_pages(self):
      return self.info['block_count'] * self.info['pages_per_block'] if self.info else 0


def decode_info(payload):
   fields = INFO_FRAME.unpack(payload[:INFO_FRAME.size])
   (version, revision, chip, chip_id, page_size, spare_size, pages_per_block, block_count, reserved_blocks, uid,
    status_registers, transport, firmware) = fields
   return {
      'protocol_version': version, 'hw_revision': revision, 'chip': chip, 'chip_id': chip_id,
      'page_size': page_size, 'spare_size': spare_size, 'pages_per_block': pages_per_block,
      'block_count': block_count, 'reserved_blocks': reserved_blocks, 'uid': list(uid),
      'status_registers': list(status_registers), 'transport': transport,
      'firmware': firmware.split(b'\0', 1)[0].decode(errors='replace'),
   }


def decode_progress(payload):
   scanned, sent, uncorrectable, elapsed_ms = PROGRESS_FRAME.unpack(payload[:PROGRESS_FRAME.size])
   return {'pages_scanned': scanned, 'pages_sent': sent, 'pages_uncorrectable': uncorrectable, 'elapsed_ms': elapsed_ms}


def read_dump(data):
   """Decode a dump file. Returns ``(dump, notes)``.

   Every INFO frame begins a new pass, which is what a tag that was reset part-way through a dump produces. The
   last pass that reached its END frame is the one used; failing that, the last pass, with a note saying so.
   """
   parser = FrameParser()
   passes, current = [], None
   for frame_type, flags, payload in parser.feed(data):
      if frame_type == FRAME_INFO:
         current = Dump()
         current.info = decode_info(payload)
         passes.append(current)
      elif current is None:
         continue                      # frames from before the first INFO belong to no pass we can place
      elif frame_type == FRAME_LUT:
         current.lut = payload
      elif frame_type == FRAME_PAGE:
         page, status_register_3, reads, _ = PAGE_FRAME_HEADER.unpack_from(payload)
         current.pages[page] = Page(page, payload[PAGE_FRAME_HEADER.size:], bool(flags & PAGE_FLAG_UNCORRECTABLE),
                                    status_register_3, reads)
      elif frame_type == FRAME_PROGRESS:
         current.last_progress = decode_progress(payload)
      elif frame_type == FRAME_END:
         current.end = decode_progress(payload)
      elif frame_type == FRAME_ERROR:
         current.errors.append(payload.decode(errors='replace'))

   notes = []
   if parser.bad_frames:
      notes.append(f'{parser.bad_frames} damaged frame(s) in the stream were skipped')
   if parser.pending_bytes > len(FRAME_SYNC):
      notes.append(f'the stream ends part-way through a frame ({parser.pending_bytes} bytes discarded)')
   if not passes:
      raise ValueError('no recovery frames found; is this a dump from legacy_log_recovery firmware?')
   complete = [candidate for candidate in passes if candidate.end is not None]
   dump = complete[-1] if complete else passes[-1]
   if len(passes) > 1:
      notes.append(f'the stream holds {len(passes)} passes over the flash (the tag was reset during a dump); '
                   f'using the last {"complete " if complete else ""}one')
   if dump.end is None:
      scanned = dump.last_progress['pages_scanned'] if dump.last_progress else 0
      notes.append(f'INCOMPLETE DUMP: it stops after about {scanned} of {dump.total_pages} pages; pages beyond that '
                   'are missing, not erased')
   if dump.info['protocol_version'] != PROTOCOL_VERSION:
      notes.append(f'dump uses protocol version {dump.info["protocol_version"]}; this tool expects {PROTOCOL_VERSION}')
   return dump, notes


# THE OLD ON-FLASH LAYOUT ---------------------------------------------------------------------------------------------

def _details(start_time, end_time, daily_start_time, daily_end_time, use_daily_times, num_devices, uids, labels, terminated):
   return {
      'start_time': start_time, 'end_time': end_time,
      'daily_start_time': daily_start_time, 'daily_end_time': daily_end_time,
      'use_daily_times': use_daily_times, 'num_devices': num_devices,
      'uids': [list(uids[i * 6:(i + 1) * 6]) for i in range(MAX_NUM_DEVICES)],
      'labels': [labels[i * MAX_LABEL_LENGTH:(i + 1) * MAX_LABEL_LENGTH] for i in range(MAX_NUM_DEVICES)],
      'terminated': terminated,
   }


def _structure_score(details):
   # How well a decoding fits what the scheduling tool always wrote: a device count, then exactly that many
   # non-zero UIDs and printable labels, and every unused slot zero-filled
   num = details['num_devices']
   score = 1 if 1 <= num <= MAX_NUM_DEVICES else 0
   used = min(num, MAX_NUM_DEVICES)
   score += all(any(uid) for uid in details['uids'][:used])
   score += all(not any(uid) for uid in details['uids'][used:])
   score += all(not any(label) for label in details['labels'][used:])
   score += all(all(32 <= c < 127 for c in label.rstrip(b'\0')) for label in details['labels'][:used])
   score += details['use_daily_times'] in (0, 1)
   return score


def unpack_details(blob):
   """Decode a META page's details in whichever of the old layouts fits them."""
   blob = bytes(blob).ljust(DETAILS.size, b'\0')
   current = _details(*DETAILS.unpack(blob[:DETAILS.size]))
   start, end, daily_start, daily_end, num, uids, labels = DETAILS_BEFORE_DAILY_TIMES.unpack(blob[:DETAILS_BEFORE_DAILY_TIMES.size])
   earliest = _details(start, end, daily_start, daily_end, 0, num, uids, labels, 0)
   if _structure_score(earliest) > _structure_score(current):
      earliest['layout'] = 'before daily times (firmware before 2023-10-30)'
      return earliest
   current['layout'] = 'with daily times'
   return current


def details_are_plausible(details):
   return (1 <= details['num_devices'] <= MAX_NUM_DEVICES and
           EARLIEST_PLAUSIBLE_START <= details['start_time'] < details['end_time'])


def decode_absolute_seconds(data, uid_to_labels=None):
   """Records from firmware before March 2024, whose timestamps are the RTC's Unix seconds.

   The same record layouts the v1 parser reads, with the same byte-at-a-time resynchronisation, but only the four
   record types that firmware had, and a timestamp that is plausible only as a real date.
   """
   log_data, now, i = defaultdict(dict), int(time.time()), 0
   while i + 5 < len(data):
      record_type = data[i]
      timestamp = struct.unpack_from('<I', data, i + 1)[0]
      consumed = 0
      if EARLIEST_PLAUSIBLE_START <= timestamp <= now and 1 <= record_type <= tottag_format.STORAGE_TYPE_RANGES:
         if record_type == tottag_format.STORAGE_TYPE_VOLTAGE and i + 9 <= len(data):
            datum = struct.unpack_from('<I', data, i + 5)[0]
            if 0 < datum < 4500:
               log_data[timestamp]['v'] = datum
               consumed = 9
         elif record_type == tottag_format.STORAGE_TYPE_CHARGING_EVENT and i + 6 <= len(data):
            if 0 < data[i + 5] <= tottag_format.MAX_BATTERY_CODE:
               log_data[timestamp]['c'] = tottag_format.BATTERY_CODES[data[i + 5]]
               consumed = 6
         elif record_type == tottag_format.STORAGE_TYPE_MOTION and i + 6 <= len(data):
            if data[i + 5] in (0, 1):
               log_data[timestamp]['m'] = data[i + 5] > 0
               consumed = 6
         elif record_type == tottag_format.STORAGE_TYPE_RANGES and i + 6 <= len(data):
            count = data[i + 5]
            if count < MAX_NUM_DEVICES and i + 6 + count * 3 <= len(data):
               ranges = {}
               for j in range(count):
                  uid = data[i + 6 + j * 3]
                  datum = struct.unpack_from('<H', data, i + 7 + j * 3)[0]
                  if datum < tottag_format.MAX_RANGING_DISTANCE_MM:
                     if uid_to_labels is None:
                        ranges[uid] = datum
                     elif uid in uid_to_labels:
                        ranges[uid_to_labels[uid]] = datum
               log_data[timestamp]['r'] = ranges
               consumed = 6 + count * 3
      i += consumed or 1
   return [dict({'t': t}, **datum) for t, datum in sorted(log_data.items())]


def uid_labels(details):
   """Short UID -> label, exactly as tottag.process_tottag_data builds it."""
   labels = defaultdict(lambda: 'Unknown')
   for i in range(details['num_devices']):
      label = details['labels'][i].decode(errors='replace').rstrip('\x00')
      labels[int(details['uids'][i][0])] = label if label else str(details['uids'][i][0])
   return labels


def format_time(timestamp):
   return datetime.datetime.fromtimestamp(timestamp, datetime.timezone.utc).strftime('%Y-%m-%d %H:%M:%S UTC')


def format_uid(uid):
   return ':'.join(f'{b:02X}' for b in reversed(uid))


class Segment:
   """Pages known to have been written one directly after another, so their payloads join into one stream."""

   def __init__(self, first_page, reason):
      self.pages, self.payload, self.damaged, self.reason = [first_page], bytearray(), False, reason
      self.records, self.first_time, self.last_time = 0, None, None


class Recovery:
   """Everything reconstructed from one dump, plus an account of how."""

   def __init__(self, dump, meta_page=None, start_time=None, include_damaged=False, reserved_blocks=None, timestamps=None):
      self.dump, self.info = dump, dump.info
      self.include_damaged, self.forced_timestamps = include_damaged, timestamps
      if self.info['chip'] not in CHIP_NAMES or not self.info['page_size']:
         raise ValueError('the dump does not identify a supported flash part: ' + '; '.join(dump.errors or ['no geometry']))

      self.pages_per_block = self.info['pages_per_block']
      self.page_size = self.info['page_size']
      self.max_payload = self.page_size - DATA_HEADER_BYTES
      self._choose_reserve(reserved_blocks)
      self.data_end = (self.info['block_count'] - self.reserved_blocks) * self.pages_per_block
      self._find_bad_blocks()
      self._classify_pages()
      self._choose_experiment(meta_page, start_time)
      self._build_segments()
      self._decode()

   # Where the data region ended ---------------------------------------------------------------------------------------

   def _choose_reserve(self, reserved_blocks):
      # Nothing but a bad-block table page ever lived in the reserve, so log pages found in the part of it that
      # the early Alliance layout still used as data are conclusive: this tag ran that firmware
      self.reserved_blocks, self.reserve_source = self.info['reserved_blocks'], 'as the old firmware reserved for this part'
      if reserved_blocks is not None:
         self.reserved_blocks, self.reserve_source = reserved_blocks, 'from --reserved-blocks'
      elif self.info['chip'] != CHIP_W25N01GW and self.reserved_blocks > EARLY_ALLIANCE_RESERVED_BLOCKS:
         low = (self.info['block_count'] - self.reserved_blocks) * self.pages_per_block
         high = (self.info['block_count'] - EARLY_ALLIANCE_RESERVED_BLOCKS) * self.pages_per_block
         if any(low <= n < high and page.data[:2] in (DATA_MAGIC, META_MAGIC[:2]) for n, page in self.dump.pages.items()):
            self.reserved_blocks = EARLY_ALLIANCE_RESERVED_BLOCKS
            self.reserve_source = ('found log pages where later firmware kept its reserve, so this tag ran the '
                                   'firmware of Nov-Dec 2024 (1aedc941..d535a3a3)')

   # Bad blocks ------------------------------------------------------------------------------------------------------

   def _find_bad_blocks(self):
      # 'skip' is every block the old writer stepped over; 'exclude' is those whose contents cannot be trusted
      self.skip_blocks, self.exclude_blocks, self.bbm_source = set(), set(), 'none found'
      if self.info['chip'] == CHIP_W25N01GW:
         if self.dump.lut and len(self.dump.lut) >= 4 * W25N_LUT_ENTRIES:
            self.bbm_source = "the part's hardware LUT"
            for i in range(W25N_LUT_ENTRIES):
               lba, pba = struct.unpack_from('>HH', self.dump.lut, 4 * i)
               # The old firmware matched on the masked LBA alone, so an EMPTY entry (all zero) made it treat
               # block 0 as bad too and step over it. That is mirrored for ordering, but an empty entry says
               # nothing about block 0's contents, so it is not excluded
               self.skip_blocks.add(lba & W25N_LUT_ADDRESS_MASK)
               if lba or pba:
                  self.exclude_blocks.add(lba & W25N_LUT_ADDRESS_MASK)
      elif self.reserved_blocks == EARLY_ALLIANCE_RESERVED_BLOCKS:
         # That firmware reloaded its table's entries but not their count, so after any reboot it skipped nothing.
         # Skipping nothing here is what matches the log it actually wrote
         self.bbm_source = 'not used (the firmware of that era ignored its own table after a reboot)'
      else:
         # The table page the old firmware would have loaded: the highest block of the reserve carrying the marker
         for block in range(self.info['block_count'] - 1, self.info['block_count'] - self.reserved_blocks - 1, -1):
            page = self.dump.pages.get(block * self.pages_per_block)
            if page and not page.uncorrectable and page.data[:4] == BBM_MAGIC:
               count = struct.unpack_from('<I', page.data, 4)[0]
               entries = struct.unpack_from(f'<{BBM_TABLE_ENTRIES}I', page.data, 8)
               self.bbm_source = f'the table at page {page.page} ({count} entries)'
               if count > BBM_TABLE_ENTRIES:
                  self.bbm_source += f', IMPLAUSIBLE, only the first {BBM_TABLE_ENTRIES} used'
               for entry in entries[:min(count, BBM_TABLE_ENTRIES)]:
                  self.skip_blocks.add(entry // self.pages_per_block)
                  self.exclude_blocks.add(entry // self.pages_per_block)
               break
      # Blocks in the reserve are outside the data region anyway
      self.skip_blocks = {b for b in self.skip_blocks if b * self.pages_per_block < self.data_end}
      self.exclude_blocks = {b for b in self.exclude_blocks if b * self.pages_per_block < self.data_end}

   def successor(self, page):
      """The page the old writer moved to after this one (storage_flush's advance, bad blocks included)."""
      following = (page + 1) % self.data_end
      for _ in range(self.info['block_count']):
         if following // self.pages_per_block not in self.skip_blocks:
            break
         following = ((following + self.pages_per_block) % self.data_end) & ~(self.pages_per_block - 1)
      return following

   def follows(self, previous, page):
      # The second test covers pages written inside a block the writer would otherwise have skipped, which
      # happens on the Winbond part when the log starts in block 0
      return page == self.successor(previous) or (page == previous + 1 and page // self.pages_per_block == previous // self.pages_per_block)

   # Classification --------------------------------------------------------------------------------------------------

   def _classify_pages(self):
      self.kinds, self.reserved_pages = {}, 0
      for number, page in self.dump.pages.items():
         if number >= self.data_end:
            self.reserved_pages += 1
            continue
         if page.data[:4] == META_MAGIC:
            kind = 'meta'
         elif page.data[:2] == DATA_MAGIC:
            length = struct.unpack_from('<H', page.data, 2)[0]
            kind = 'data' if 1 <= length <= self.max_payload else 'bad-length'
         else:
            kind = 'unknown'
         self.kinds[number] = kind

   def payload_of(self, number):
      page = self.dump.pages[number]
      length = struct.unpack_from('<H', page.data, 2)[0]
      if not 1 <= length <= self.max_payload:
         length = self.max_payload
      return page.data[DATA_HEADER_BYTES:DATA_HEADER_BYTES + length], length == self.max_payload

   # Which deployment ------------------------------------------------------------------------------------------------

   def _choose_experiment(self, meta_page, start_time):
      self.metas = []
      for number in sorted(n for n, kind in self.kinds.items() if kind == 'meta'):
         page = self.dump.pages[number]
         details = unpack_details(page.data[4:4 + DETAILS.size])
         excluded = number // self.pages_per_block in self.exclude_blocks
         self.metas.append((number, details, details_are_plausible(details) and not page.uncorrectable and not excluded))

      self.meta_page, self.details = None, None
      if meta_page is not None:
         if meta_page not in self.dump.pages or self.kinds.get(meta_page) != 'meta':
            raise ValueError(f'page {meta_page} is not a META page in this dump')
         self.meta_page = meta_page
         self.details = unpack_details(self.dump.pages[meta_page].data[4:4 + DETAILS.size])
      else:
         plausible = [(details['start_time'], -number, number, details) for number, details, ok in self.metas if ok]
         if plausible:
            _, _, self.meta_page, self.details = max(plausible)

      self.start_time = start_time if start_time is not None else (self.details['start_time'] if self.details else None)
      self.labels = uid_labels(self.details) if self.details else None

      # Where the log begins: straight after its META page, or, without one, after the longest stretch of the
      # data region that holds no data page, which is the space the writer had not reached yet
      data_pages = sorted(n for n, kind in self.kinds.items() if kind in ('data', 'bad-length')
                          and n // self.pages_per_block not in self.exclude_blocks)
      if self.meta_page is not None:
         self.log_start = self.meta_page
      elif data_pages:
         gaps = [((data_pages[(i + 1) % len(data_pages)] - data_pages[i]) % self.data_end, data_pages[(i + 1) % len(data_pages)])
                 for i in range(len(data_pages))]
         self.log_start = max(gaps)[1] - 1
      else:
         self.log_start = 0

   # Segments --------------------------------------------------------------------------------------------------------

   def _build_segments(self):
      candidates = sorted(n for n in self.kinds if n // self.pages_per_block not in self.exclude_blocks)
      in_order = [n for n in candidates if n > self.log_start] + [n for n in candidates if n <= self.log_start]
      self.segments, self.excluded = [], defaultdict(list)
      for number in sorted(self.kinds):
         if number // self.pages_per_block in self.exclude_blocks and self.kinds[number] != 'unknown':
            self.excluded['in a bad block'].append(number)

      segment, previous, previous_full, skipped = None, None, False, None
      for number in in_order:
         kind, page = self.kinds[number], self.dump.pages[number]
         if number == self.meta_page:
            continue
         reason = None
         if kind == 'meta':
            reason = 'another META page'
         elif kind == 'unknown':
            reason = 'not a data page'
         elif (page.uncorrectable or kind == 'bad-length') and not self.include_damaged:
            reason = 'uncorrectable ECC error' if page.uncorrectable else 'impossible length field'
         if reason:
            self.excluded[reason].append(number)
            segment, skipped = None, (skipped or f'after page {number}, not used ({reason})')
            continue

         damaged = page.uncorrectable or kind == 'bad-length'
         payload, full = self.payload_of(number)
         # A damaged page is never joined to its neighbours: its bytes are only probably right, and joining would
         # let an error in them corrupt a record that is otherwise intact
         joins = (segment is not None and not damaged and not segment.damaged and previous_full and self.follows(previous, number))
         if not joins:
            segment = Segment(number, self._why_new_segment(previous, previous_full, number, skipped, damaged))
            segment.damaged = damaged
            self.segments.append(segment)
         else:
            segment.pages.append(number)
         skipped = None
         segment.payload += payload
         previous, previous_full = number, full and not damaged

   def _why_new_segment(self, previous, previous_full, number, skipped, damaged):
      if damaged:
         return 'damaged page, decoded on its own'
      if previous is None:
         return skipped or 'start of the log'
      if skipped:
         return skipped
      if self.dump.pages[previous].uncorrectable or self.kinds[previous] == 'bad-length':
         return 'after a damaged page'
      if not self.follows(previous, number):
         blank, page = 0, self.successor(previous)
         while page != number and blank < self.data_end:
            blank, page = blank + 1, self.successor(page)
         return f'after {blank} erased page(s) in the middle of the log; the record straddling the gap is lost'
      if not previous_full:
         return 'after a partial page (written at shutdown), so nothing is lost'
      return 'break'

   # Decoding --------------------------------------------------------------------------------------------------------

   def _decode_with(self, era, payload):
      if era == TIMESTAMPS_ABSOLUTE:
         return decode_absolute_seconds(payload, self.labels)
      return tottag_format.parse_v1(payload, self.start_time if self.start_time is not None else 0, self.labels)

   def _choose_timestamps(self):
      # Decode a sample both ways. Wrong-era bytes almost never pass the other era's timestamp test (a 500 ms grid
      # on one side, a real calendar date on the other), so the right one wins by a wide margin
      sample, total = [], 0
      for segment in self.segments:
         if total >= ERA_SAMPLE_BYTES:
            break
         sample.append(bytes(segment.payload[:ERA_SAMPLE_BYTES - total]))
         total += len(sample[-1])
      self.era_counts = {era: sum(len(self._decode_with(era, chunk)) for chunk in sample)
                         for era in (TIMESTAMPS_RELATIVE, TIMESTAMPS_ABSOLUTE)}
      if self.forced_timestamps:
         return self.forced_timestamps
      return TIMESTAMPS_ABSOLUTE if self.era_counts[TIMESTAMPS_ABSOLUTE] > self.era_counts[TIMESTAMPS_RELATIVE] else TIMESTAMPS_RELATIVE

   def _decode(self):
      merged = defaultdict(dict)
      self.timestamps = self._choose_timestamps()
      for segment in self.segments:
         records = self._decode_with(self.timestamps, bytes(segment.payload))
         segment.records = len(records)
         if records:
            segment.first_time, segment.last_time = records[0]['t'], records[-1]['t']
         for record in records:
            merged[record['t']].update({key: value for key, value in record.items() if key != 't'})
      self.records = [dict({'t': t}, **datum) for t, datum in sorted(merged.items())]

   # Outputs ---------------------------------------------------------------------------------------------------------

   @property
   def device_label(self):
      # 'Unknown' for a tag missing from its own deployment, as process_tottag_data names it
      uid = self.info['uid']
      return self.labels.get(uid[0], 'Unknown') if self.labels is not None else format_uid(uid).replace(':', '')

   @property
   def base_name(self):
      return f'{self.device_label}_{self.start_time if self.start_time is not None else "unknown"}'

   def payload_stream(self):
      return b''.join(bytes(segment.payload) for segment in self.segments)

   def write(self, directory, notes=()):
      os.makedirs(directory, exist_ok=True)
      base = os.path.join(directory, self.base_name)
      with open(base + '.ttg', 'wb') as file:
         file.write(self.payload_stream())
      with open(base + '.pkl', 'wb') as file:
         pickle.dump(self.records, file, protocol=pickle.HIGHEST_PROTOCOL)
      report = self.report(notes)
      with open(base + '_recovery.txt', 'w') as file:
         file.write(report)
      return base, report

   def report(self, notes=()):
      info, out = self.info, []
      add = out.append
      add('TotTag legacy log recovery')
      add('==========================')
      add('')
      add(f'Tag:            {format_uid(info["uid"])}  (hardware rev {REVISION_NAMES.get(info["hw_revision"], "?")} build, '
          f'{CHIP_NAMES.get(info["chip"], "unknown")} flash)')
      add(f'Dump:           {info["firmware"]}, over {TRANSPORT_NAMES.get(info["transport"], "?")}')
      end = self.dump.end
      if end:
         add(f'                {end["pages_scanned"]} pages read, {end["pages_sent"]} not erased, '
             f'{end["pages_uncorrectable"]} uncorrectable, {end["elapsed_ms"] / 1000:.0f} s')
      for note in notes:
         add(f'NOTE:           {note}')
      for error in self.dump.errors:
         add(f'FIRMWARE ERROR: {error}')
      add(f'Data region:    pages 0-{self.data_end - 1} ({self.reserved_blocks}-block reserve {self.reserve_source})')
      add(f'Bad blocks:     from {self.bbm_source}'
          + (f': {sorted(self.exclude_blocks)}' if self.exclude_blocks else ''))
      add('')

      add('Deployment')
      add('----------')
      if not self.metas:
         add('No META page was found. The start time and the device labels are not on the flash.')
      for number, details, plausible in self.metas:
         mark = '->' if number == self.meta_page else '  '
         if plausible or number == self.meta_page:
            add(f'{mark} META at page {number}: {format_time(details["start_time"])} to {format_time(details["end_time"])}, '
                f'{details["num_devices"]} device(s)')
         else:
            add(f'{mark} META at page {number}: not a usable deployment (empty or damaged; the old firmware wrote an '
                'empty one whenever it could not find its log)')
      if self.details:
         for i in range(self.details['num_devices']):
            uid = self.details['uids'][i]
            label = self.details['labels'][i].decode(errors='replace').rstrip('\x00')
            add(f'      {format_uid(uid)}  {label or "<unlabeled>"}{"  <- this tag" if uid[0] == info["uid"][0] else ""}')
      if self.details:
         add(f'Details layout: {self.details["layout"]}')
      if self.start_time is None and self.timestamps == TIMESTAMPS_RELATIVE:
         add('WARNING: no start time is known, so record times are SECONDS SINCE THE DEPLOYMENT STARTED rather than '
             'Unix times. Pass --start-time if it is known from elsewhere.')
      elif not self.details:
         add(f'Start time {self.start_time} taken from --start-time. Labels are unknown, so ranges are keyed by short UID.')
      add('')

      add('Log')
      add('---')
      used = sum(len(segment.pages) for segment in self.segments)
      add(f'Log begins after page {self.log_start}. {used} data page(s) used, in {len(self.segments)} segment(s).')
      for reason, numbers in sorted(self.excluded.items()):
         add(f'Not used, {reason}: {len(numbers)} page(s)' + (f' {numbers[:12]}{" ..." if len(numbers) > 12 else ""}'))
      if self.reserved_pages:
         add(f'{self.reserved_pages} non-erased page(s) in the bad-block reserve (tables and remapped blocks), not log data.')
      add('')
      for index, segment in enumerate(self.segments[:60]):
         span = (f'{self._when(segment.first_time)} to {self._when(segment.last_time)}' if segment.records else 'no records')
         add(f'  segment {index + 1:3d}: pages {segment.pages[0]}-{segment.pages[-1]} ({len(segment.pages)}), '
             f'{segment.records} records, {span}')
         add(f'               {segment.reason}')
      if len(self.segments) > 60:
         add(f'  ... and {len(self.segments) - 60} more')
      add('')

      add('Result')
      add('------')
      add(f'Record times:   {TIMESTAMP_DESCRIPTIONS[self.timestamps]}'
          + (' (from --timestamps)' if self.forced_timestamps else '')
          + f'; a sample decoded {self.era_counts[TIMESTAMPS_RELATIVE]} records read the newer way and '
            f'{self.era_counts[TIMESTAMPS_ABSOLUTE]} the older way')
      add(f'{len(self.records)} records recovered' + (f', {self._when(self.records[0]["t"])} to {self._when(self.records[-1]["t"])}'
                                                      if self.records else ''))
      if self.details and self.records:
         outside = sum(1 for r in self.records if not self.details['start_time'] <= r['t'] <= self.details['end_time'])
         if outside:
            add(f'{outside} record(s) fall outside the scheduled deployment window; treat them with suspicion.')
      if len(self.segments) > 1:
         add('Each segment boundary can cost the one record that straddled it. The .ttg joins all segments end to end,')
         add('so re-parsing it may decode a false record at each boundary; the .pkl was decoded segment by segment.')
      return '\n'.join(out) + '\n'

   def _when(self, t):
      return format_time(t) if (self.start_time is not None or self.timestamps == TIMESTAMPS_ABSOLUTE) else f'+{t:.1f} s'


# ACQUISITION ---------------------------------------------------------------------------------------------------------

class ProgressPrinter:
   def __init__(self):
      self.parser, self.total, self.finished, self.errors = FrameParser(), None, False, []

   def feed(self, data):
      for frame_type, _flags, payload in self.parser.feed(data):
         if frame_type == FRAME_INFO:
            info = decode_info(payload)
            self.total = info['block_count'] * info['pages_per_block']
            print(f'Tag {format_uid(info["uid"])}: {CHIP_NAMES.get(info["chip"], "unknown")} flash, '
                  f'{self.total} pages, recovery firmware {info["firmware"]}')
         elif frame_type in (FRAME_PROGRESS, FRAME_END):
            progress = decode_progress(payload)
            if frame_type == FRAME_PROGRESS and progress['pages_scanned'] == self.total:
               continue                  # END follows with the same numbers
            # Every report is shown: they come every few seconds, and a burst of them is itself worth seeing
            percent = 100.0 * progress['pages_scanned'] / self.total if self.total else 0
            print(f'\r  {progress["pages_scanned"]}/{self.total} pages read ({percent:.0f}%), '
                  f'{progress["pages_sent"]} with data, {progress["pages_uncorrectable"]} uncorrectable   ',
                  end='', flush=True)
            if frame_type == FRAME_END:
               print()
               self.finished = True
         elif frame_type == FRAME_ERROR:
            self.errors.append(payload.decode(errors='replace'))
            print(f'\nTag reports: {self.errors[-1]}')
            self.finished = True


def dump_path(directory):
   os.makedirs(directory, exist_ok=True)
   return os.path.join(directory, f'dump_{int(time.time())}.ttrd')


def rename_dump(path):
   """Name the dump after the tag it came from, now that the stream has said which one that is."""
   try:
      with open(path, 'rb') as file:
         dump, _ = read_dump(file.read())
   except ValueError:
      return path
   named = os.path.join(os.path.dirname(path), f'{format_uid(dump.info["uid"]).replace(":", "")}_{os.path.basename(path)[5:]}')
   os.replace(path, named)
   return named


def acquire_usb(port, directory, first_page=0, page_count=0):
   import serial, serial.tools.list_ports
   if port is None:
      found = [p.device for p in serial.tools.list_ports.comports() if p.vid == TOTTAG_USB_VID and p.pid == TOTTAG_USB_PID]
      if not found:
         raise RuntimeError('no TotTag found on USB. Is the recovery firmware flashed, and the cable plugged in?')
      if len(found) > 1:
         raise RuntimeError(f'several TotTags are plugged in ({", ".join(found)}); choose one with --port')
      port = found[0]
   path = dump_path(directory)
   print(f'Reading the flash over {port} into {path}')
   progress = ProgressPrinter()
   with serial.Serial(port, timeout=0.5, write_timeout=5) as device, open(path, 'wb') as out:
      try:
         device.dtr = True               # the firmware stops sending while DTR is down; pyserial raises it on open
      except OSError:
         pass                            # not a real serial line (a pty in testing); nothing to raise
      time.sleep(0.2)
      device.reset_input_buffer()
      device.write(b'D' + struct.pack('<II', first_page, page_count))
      device.flush()
      last_data = time.time()
      while not progress.finished:
         chunk = device.read(max(1, device.in_waiting))
         if chunk:
            out.write(chunk)
            progress.feed(chunk)
            last_data = time.time()
         elif time.time() - last_data > IDLE_TIMEOUT_S:
            raise TimeoutError(f'the tag sent nothing for {IDLE_TIMEOUT_S} s; what arrived is saved in {path}')
   return rename_dump(path)


def acquire_rtt(directory, jlink_device, speed, logger):
   path = dump_path(directory)
   logger = logger or ('JLinkRTTLogger.exe' if os.name == 'nt' else 'JLinkRTTLoggerExe')
   command = [logger, '-Device', jlink_device, '-If', 'SWD', '-Speed', str(speed),
              '-RTTAddress', RTT_CONTROL_BLOCK_ADDRESS, '-RTTChannel', '1', path]
   print(f'Reading the flash over RTT into {path}\n  {" ".join(command)}')
   log_path = path + '.jlink.log'
   progress, offset, last_data = ProgressPrinter(), 0, time.time()
   with open(log_path, 'w') as log:
      process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=log, stderr=subprocess.STDOUT)
      try:
         while not progress.finished:
            time.sleep(0.2)
            if os.path.exists(path):
               with open(path, 'rb') as file:
                  file.seek(offset)
                  chunk = file.read()
               if chunk:
                  offset += len(chunk)
                  progress.feed(chunk)
                  last_data = time.time()
            if process.poll() is not None:
               raise RuntimeError(f'{logger} exited early; see {log_path}')
            if time.time() - last_data > IDLE_TIMEOUT_S:
               raise TimeoutError(f'nothing arrived for {IDLE_TIMEOUT_S} s. The tag dumps once per boot: reset it '
                                  f'and try again. J-Link output is in {log_path}')
      finally:
         # The logger stops on a key press
         try:
            process.communicate(b'\n', timeout=10)
         except (subprocess.TimeoutExpired, OSError, ValueError):
            process.kill()
   named = rename_dump(path)
   os.replace(log_path, named + '.jlink.log')
   return named


# TOP-LEVEL FUNCTIONALITY ---------------------------------------------------------------------------------------------

def recover(path, directory, meta_page=None, start_time=None, include_damaged=False, reserved_blocks=None, timestamps=None):
   with open(path, 'rb') as file:
      dump, notes = read_dump(file.read())
   recovery = Recovery(dump, meta_page=meta_page, start_time=start_time, include_damaged=include_damaged,
                       reserved_blocks=reserved_blocks, timestamps=timestamps)
   base, report = recovery.write(directory, notes)
   print()
   print(report)
   print(f'Wrote {base}.pkl, {base}.ttg and {base}_recovery.txt')
   return recovery


def main(argv=None):
   parser = argparse.ArgumentParser(description='Recover logs from TotTags running pre-nandlog firmware, via the '
                                                'read-only legacy_log_recovery firmware')
   commands = parser.add_subparsers(dest='command', required=True)

   def add_recovery_options(sub):
      sub.add_argument('-o', '--output', default='.', help='directory for the dump and the recovered files')
      sub.add_argument('--meta-page', type=int, help='use the deployment described by the META page at this page')
      sub.add_argument('--start-time', type=int, help='deployment start (Unix seconds), when no META page survives')
      sub.add_argument('--include-damaged', action='store_true',
                       help='also decode pages with uncorrectable ECC errors or impossible lengths, each in isolation')
      sub.add_argument('--reserved-blocks', type=int,
                       help='size of the bad-block reserve the old firmware used, if the detected one is wrong')
      sub.add_argument('--timestamps', choices=(TIMESTAMPS_RELATIVE, TIMESTAMPS_ABSOLUTE),
                       help='how record times were written, if the detected encoding is wrong')

   usb = commands.add_parser('usb', help='dump a tag over USB (hardware revisions O and P), then recover it')
   usb.add_argument('--port', help='serial port; found automatically when exactly one TotTag is plugged in')
   add_recovery_options(usb)

   rtt = commands.add_parser('rtt', help='dump a tag over a J-Link (any revision), then recover it')
   rtt.add_argument('--jlink-device', default=DEFAULT_JLINK_DEVICE, help=f'J-Link device name (default {DEFAULT_JLINK_DEVICE})')
   rtt.add_argument('--speed', type=int, default=4000, help='SWD speed in kHz')
   rtt.add_argument('--logger', help='path to JLinkRTTLoggerExe, if it is not on the PATH')
   add_recovery_options(rtt)

   again = commands.add_parser('recover', help='recover from a dump taken earlier')
   again.add_argument('dump', help='a .ttrd file')
   add_recovery_options(again)

   args = parser.parse_args(argv)
   try:
      if args.command == 'usb':
         path = acquire_usb(args.port, args.output)
      elif args.command == 'rtt':
         path = acquire_rtt(args.output, args.jlink_device, args.speed, args.logger)
      else:
         path = args.dump
      print(f'Dump saved as {path}. Keep it: it is the only complete copy of this tag\'s flash.')
      recover(path, args.output, args.meta_page, args.start_time, args.include_damaged, args.reserved_blocks, args.timestamps)
   except (RuntimeError, TimeoutError, ValueError, OSError) as error:
      print(f'\nERROR: {error}', file=sys.stderr)
      return 1
   return 0


if __name__ == '__main__':
   sys.exit(main())
