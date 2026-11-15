#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Tests for legacy_recovery.py, against flash images laid out exactly as the pre-nandlog firmware wrote them.

   python3 test_legacy_recovery.py                  # runs dumps through the recovery FIRMWARE's code, once built
   python3 test_legacy_recovery.py --no-harness     # only what the Python side can check alone

The harness is firmware/tests/tools/legacy_log_recovery_sim (build it with `make` there). With it, a scenario's
pages are loaded into the nandlog SPI simulator, legacy_log_recovery.c reads them out exactly as it would on a
tag, and the harness fails the run if the firmware programmed or erased anything. Without it, the same frame
stream is produced by a Python mirror of the firmware's encoder, which is also the only way to present what
the simulator cannot: uncorrectable ECC errors and the Winbond part's hardware LUT.

Every scenario is compared against the REFERENCE: what a perfect download of the same log would have decoded.
"""

import argparse, os, pickle, random, struct, subprocess, sys, tempfile, unittest, zlib
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import legacy_recovery as lr
import tottag_format

HARNESS = None
PARTS = {
   'W25N01GW':     dict(chip=1, page_size=2048, spare=64,  ppb=64, blocks=1024, reserved=40, id=b'\x00\xef\xba\x21'),
   'AS5F18G04SND': dict(chip=2, page_size=4096, spare=256, ppb=64, blocks=4096, reserved=80, id=b'\x8d\x00\x00\x00'),
}
TAG_UID = [0x11, 0x22, 0x33, 0x44, 0x55, 0x66]
PEERS = [0x21, 0x31, 0x41]
START_TIME = 1700000000
RECORD_KEYS = {1: 'v', 2: 'c', 3: 'm', 4: 'r', 5: 'i', 6: 'b'}


# THE OLD FIRMWARE, AS A WRITER ---------------------------------------------------------------------------------------

def make_details(start=START_TIME, hours=24):
   uids = [TAG_UID] + [[peer, 0x22, 0x33, 0x44, 0x55, 0x66] for peer in PEERS]
   labels = [b'CHILD', b'MOTHER', b'FATHER', b'SIBLING']
   uid_bytes = b''.join(bytes(u) for u in uids).ljust(60, b'\0')
   label_bytes = b''.join(label.ljust(16, b'\0') for label in labels).ljust(160, b'\0')
   return lr.DETAILS.pack(start, start + hours * 3600, 0, 0, 0, len(uids), uid_bytes, label_bytes, 0)


class OldFirmware:
   """Lays pages out the way storage.c before cf7249d5 did, and remembers what it meant to write."""

   def __init__(self, part, bad_blocks=(), reserved=None):
      g = PARTS[part]
      self.part, self.page_size, self.ppb, self.blocks = part, g['page_size'], g['ppb'], g['blocks']
      self.data_end = (g['blocks'] - (reserved or g['reserved'])) * g['ppb']
      self.max_payload = self.page_size - 4
      self.bad = set(bad_blocks)
      self.pages, self.sessions, self.records, self.page_spans = {}, [], [], {}
      self.cache, self.current = bytearray(), None

   def advance(self, page):
      page = (page + 1) % self.data_end
      while page // self.ppb in self.bad:
         page = ((page + self.ppb) % self.data_end) & ~(self.ppb - 1)
      return page

   def schedule(self, block, details):
      start = block * self.ppb
      while start // self.ppb in self.bad:
         start = ((start + self.ppb) % self.data_end) & ~(self.ppb - 1)
      self.pages[start] = (b'META' + details).ljust(self.page_size, b'\0')
      self.meta_page, self.current = start, start + 1
      self._new_session()

   def _new_session(self):
      self.cache = bytearray()
      self.sessions.append(bytearray())
      self.cache_offset = 0            # where the cache begins in the current session's stream

   def store(self, t_ms, record):
      session = len(self.sessions) - 1
      start = self.cache_offset + len(self.cache)
      self.records.append((session, start, start + len(record), t_ms, record[0]))
      self.cache += record
      while len(self.cache) >= self.max_payload:
         self._write(self.max_payload)
         self.current = self.advance(self.current)

   def _write(self, length):
      body = bytes(self.cache[:length])
      self.pages[self.current] = b'DA' + struct.pack('<H', length) + body + b'\xff' * (self.max_payload - length)
      session = len(self.sessions) - 1
      self.page_spans[self.current] = (session, len(self.sessions[session]), len(self.sessions[session]) + length)
      self.sessions[session] += body
      del self.cache[:length]
      self.cache_offset += length

   def shutdown(self):
      # STORAGE_TYPE_SHUTDOWN: the only path that wrote a partial page. Boot recovery then resumes after it
      if self.cache:
         self._write(len(self.cache))
         self.current = self.advance(self.current)
      self._new_session()

   def crash(self):
      # Anything still cached is lost, and the next boot resumes at the next unwritten page
      lost = len(self.cache)
      self.records = [r for r in self.records if not (r[0] == len(self.sessions) - 1 and r[2] > self.cache_offset)] if lost else self.records
      self._new_session()


def generate(writer, hours, seed=1, peers=PEERS, crash_at=(), shutdown_at=()):
   rng = random.Random(seed)
   steps = int(hours * 3600 * 2)
   motion = 0
   for step in range(steps):
      t = step * 500
      if step in shutdown_at:
         writer.shutdown()
      if step in crash_at:
         writer.crash()
      if step % 600 == 0:
         writer.store(t, struct.pack('<BII', 1, t, 3600 + rng.randint(0, 400)))
      if rng.random() < 0.9:
         chosen = rng.sample(peers, rng.randint(1, len(peers)))
         writer.store(t, struct.pack('<BIB', 4, t, len(chosen)) + b''.join(struct.pack('<BH', p, rng.randint(100, 9000)) for p in chosen))
      if rng.random() < 0.002:
         motion ^= 1
         writer.store(t, struct.pack('<BIB', 3, t, motion))
      if rng.random() < 0.01:
         seen = rng.sample(peers, rng.randint(0, len(peers)))
         writer.store(t, struct.pack('<BIB', 6, t, len(seen)) + bytes(seen))
      if rng.random() < 0.004:
         writer.store(t, struct.pack('<BIBhhh', 5, t, 7, rng.randint(-2000, 2000), rng.randint(-2000, 2000), rng.randint(-2000, 2000)))
      if rng.random() < 0.0005:
         writer.store(t, struct.pack('<BIB', 2, t, rng.randint(1, 5)))
   return writer


def make_early_details(start=START_TIME, hours=24, devices=4):
   # experiment_details_t before 23b8c90c (2023-10-30): no use_daily_times byte
   uids = ([TAG_UID] + [[peer, 0x22, 0x33, 0x44, 0x55, 0x66] for peer in PEERS] + [[0x51 + i, 1, 2, 3, 4, 5] for i in range(6)])[:devices]
   labels = ([b'CHILD', b'MOTHER', b'FATHER', b'SIBLING'] + [b'EXTRA%d' % i for i in range(6)])[:devices]
   uid_bytes = b''.join(bytes(u) for u in uids).ljust(60, b'\0')
   label_bytes = b''.join(label.ljust(16, b'\0') for label in labels).ljust(160, b'\0')
   return lr.DETAILS_BEFORE_DAILY_TIMES.pack(start, start + hours * 3600, 0, 0, len(uids), uid_bytes, label_bytes)


def generate_absolute(writer, hours, start=START_TIME, seed=3):
   """Records as firmware before March 2024 wrote them: RTC Unix seconds, types 1-4. Returns what they mean."""
   rng, expected, labels = random.Random(seed), defaultdict(dict), {0x21: 'MOTHER', 0x31: 'FATHER', 0x41: 'SIBLING'}
   for step in range(int(hours * 3600)):
      t = start + step
      if step % 300 == 0:
         mv = 3600 + rng.randint(0, 400)
         writer.store(step * 1000, struct.pack('<BII', 1, t, mv))
         expected[t]['v'] = mv
      if rng.random() < 0.9:
         chosen = rng.sample(PEERS, rng.randint(1, len(PEERS)))
         distances = [rng.randint(100, 9000) for _ in chosen]
         writer.store(step * 1000, struct.pack('<BIB', 4, t, len(chosen)) + b''.join(struct.pack('<BH', p, d) for p, d in zip(chosen, distances)))
         expected[t]['r'] = {labels[p]: d for p, d in zip(chosen, distances)}
      if rng.random() < 0.003:
         motion = rng.randint(0, 1)
         writer.store(step * 1000, struct.pack('<BIB', 3, t, motion))
         expected[t]['m'] = motion > 0
      if rng.random() < 0.001:
         code = rng.randint(1, 5)
         writer.store(step * 1000, struct.pack('<BIB', 2, t, code))
         expected[t]['c'] = tottag_format.BATTERY_CODES[code]
   writer.shutdown()                    # so that everything expected actually reached the flash
   return expected


def labels_for(details_blob):
   return lr.uid_labels(lr.unpack_details(details_blob))


def reference(writer, start=START_TIME, labels=None):
   """What a perfect download would decode: each boot's stream on its own, merged."""
   merged = defaultdict(dict)
   for session in writer.sessions:
      for record in tottag_format.parse_v1(bytes(session), start, labels):
         merged[record['t']].update({k: v for k, v in record.items() if k != 't'})
   return merged


def compare(recovered, expected):
   """Returns (wrong, missing): (t, key) pairs decoded differently or not at all."""
   got = defaultdict(dict)
   for record in recovered:
      got[record['t']].update({k: v for k, v in record.items() if k != 't'})
   wrong = [(t, k) for t, datum in got.items() for k, v in datum.items() if expected.get(t, {}).get(k) != v]
   missing = [(t, k) for t, datum in expected.items() for k in datum if k not in got.get(t, {})]
   return wrong, missing


def keys_touching(writer, lost_pages, start=START_TIME):
   """(t, key) of every record with at least one byte on a lost page: exactly what losing those pages must cost."""
   spans = [writer.page_spans[p] for p in lost_pages]
   return {(start + r[3] / 1000, RECORD_KEYS[r[4]]) for r in writer.records
           if any(r[0] == s and r[1] < e and b < r[2] for s, b, e in spans)}


# THE RECOVERY FIRMWARE'S OUTPUT --------------------------------------------------------------------------------------

def frame(frame_type, flags, payload):
   body = struct.pack('<BBH', frame_type, flags, len(payload)) + payload
   return lr.FRAME_SYNC + body + struct.pack('<I', zlib.crc32(body))


def python_dump(part, pages, uncorrectable=(), lut=None):
   """The frame stream legacy_log_recovery.c would send for these pages (a mirror of its encoder)."""
   g = PARTS[part]
   total = g['blocks'] * g['ppb']
   info = lr.INFO_FRAME.pack(1, 0x16, g['chip'], g['id'], g['page_size'], g['spare'], g['ppb'], g['blocks'], g['reserved'],
                             bytes(TAG_UID), b'\0\0\0', 0, b'python mirror'.ljust(32, b'\0'))
   stream = bytearray(frame(lr.FRAME_INFO, 0, info))
   if g['chip'] == lr.CHIP_W25N01GW:
      stream += frame(lr.FRAME_LUT, 0, lut if lut is not None else b'\xff' * 80)
   sent = 0
   for page in sorted(set(pages) | set(uncorrectable)):
      data = pages.get(page, b'').ljust(g['page_size'], b'\xff')
      bad = page in uncorrectable
      if not bad and data == b'\xff' * g['page_size']:
         continue
      stream += frame(lr.FRAME_PAGE, lr.PAGE_FLAG_UNCORRECTABLE if bad else 0,
                      lr.PAGE_FRAME_HEADER.pack(page, 0x20 if bad else 0, 4 if bad else 1, 0) + data)
      sent += 1
   stream += frame(lr.FRAME_END, 0, lr.PROGRESS_FRAME.pack(total, sent, len(uncorrectable), 0))
   return bytes(stream)


def firmware_dump(part, pages):
   """The frame stream the recovery firmware's own code produces, run over the SPI simulator."""
   with tempfile.TemporaryDirectory() as directory:
      image, stream = os.path.join(directory, 'image.bin'), os.path.join(directory, 'stream.ttrd')
      with open(image, 'wb') as file:
         for page, data in sorted(pages.items()):
            file.write(struct.pack('<II', page, len(data)) + data)
      result = subprocess.run([HARNESS, part, image, stream, ''.join(f'{b:02x}' for b in reversed(TAG_UID))],
                              capture_output=True, text=True)
      if result.returncode != 0:
         raise AssertionError(f'harness failed:\n{result.stdout}\n{result.stderr}')
      assert 'flash unchanged' in result.stdout
      with open(stream, 'rb') as file:
         return file.read()


def dump_of(part, pages, **kwargs):
   # The harness is used whenever it can present the scenario; the mirror covers what the simulator cannot
   if HARNESS and not kwargs:
      return firmware_dump(part, pages)
   return python_dump(part, pages, **kwargs)


def recover(stream, **options):
   dump, notes = lr.read_dump(stream)
   return lr.Recovery(dump, **options), notes


# SCENARIOS -----------------------------------------------------------------------------------------------------------

class CleanLogs(unittest.TestCase):

   def test_a_log_that_wraps_around_the_end_of_the_array_is_recovered_whole(self):
      details = make_details()
      writer = OldFirmware('W25N01GW')
      writer.schedule(978, details)                  # six blocks from the end of the data region
      generate(writer, hours=10)
      self.assertTrue(any(p < writer.meta_page for p in writer.pages), 'scenario must wrap')
      recovery, _ = recover(dump_of('W25N01GW', writer.pages))
      wrong, missing = compare(recovery.records, reference(writer, labels=labels_for(details)))
      self.assertEqual((wrong, missing), ([], []))
      self.assertEqual(len(recovery.segments), 1)
      self.assertEqual(recovery.meta_page, writer.meta_page)

   def test_shutdowns_leave_partial_pages_that_split_segments_without_losing_anything(self):
      details = make_details()
      writer = OldFirmware('W25N01GW')
      writer.schedule(40, details)
      generate(writer, hours=4, shutdown_at=(5000, 11000, 20000))
      recovery, _ = recover(dump_of('W25N01GW', writer.pages))
      wrong, missing = compare(recovery.records, reference(writer, labels=labels_for(details)))
      self.assertEqual((wrong, missing), ([], []))
      self.assertEqual(len(recovery.segments), 4)

   def test_an_empty_meta_page_written_after_a_failed_boot_is_not_mistaken_for_the_deployment(self):
      details = make_details()
      writer = OldFirmware('W25N01GW')
      writer.schedule(300, details)
      generate(writer, hours=3)
      writer.pages[0] = b'META'.ljust(writer.page_size, b'\0')
      recovery, _ = recover(dump_of('W25N01GW', writer.pages))
      self.assertEqual(recovery.meta_page, writer.meta_page)
      wrong, missing = compare(recovery.records, reference(writer, labels=labels_for(details)))
      self.assertEqual((wrong, missing), ([], []))
      self.assertIn('not a usable deployment', recovery.report())

   def test_a_crash_costs_at_most_the_record_it_interrupted(self):
      # Pages from before and after an ungraceful reboot sit side by side and cannot be told apart, so the record
      # cut short by the crash is decoded against the next boot's bytes. That is what the old firmware's own
      # download did; the test pins the damage to one record per crash rather than pretending there is none
      details = make_details()
      writer = OldFirmware('W25N01GW')
      writer.schedule(10, details)
      generate(writer, hours=4, crash_at=(7000, 19000))
      recovery, _ = recover(dump_of('W25N01GW', writer.pages))
      wrong, missing = compare(recovery.records, reference(writer, labels=labels_for(details)))
      self.assertLessEqual(len(wrong), 2)
      self.assertLessEqual(len(missing), 2 * 2)


class DamagedLogs(unittest.TestCase):

   def _lost_pages_scenario(self, part='W25N01GW'):
      details = make_details()
      writer = OldFirmware(part)
      writer.schedule(100, details)
      generate(writer, hours=6)
      data_pages = sorted(p for p in writer.pages if p != writer.meta_page)
      n = len(data_pages)
      self.assertGreater(n, 100)
      lost = [data_pages[n // 10], data_pages[n // 10 + 1], data_pages[n // 2], data_pages[(4 * n) // 5]]
      return details, writer, lost

   def test_missing_pages_cost_only_the_records_that_touched_them(self):
      details, writer, lost = self._lost_pages_scenario()
      pages = {p: d for p, d in writer.pages.items() if p not in lost}
      recovery, _ = recover(dump_of('W25N01GW', pages))
      wrong, missing = compare(recovery.records, reference(writer, labels=labels_for(details)))
      self.assertEqual(wrong, [], 'nothing may be decoded that was not written')
      self.assertTrue(set(missing) <= keys_touching(writer, lost), 'only records on lost pages may go missing')
      self.assertEqual(len(recovery.segments), 4)

   def test_uncorrectable_pages_are_left_out_by_default_and_decoded_in_isolation_on_request(self):
      details, writer, lost = self._lost_pages_scenario()
      stream = dump_of('W25N01GW', writer.pages, uncorrectable=lost)
      expected = reference(writer, labels=labels_for(details))

      recovery, _ = recover(stream)
      wrong, missing = compare(recovery.records, expected)
      self.assertEqual(wrong, [])
      self.assertTrue(set(missing) <= keys_touching(writer, lost))
      self.assertIn('uncorrectable ECC error: 4 page(s)', recovery.report())

      included, _ = recover(stream, include_damaged=True)
      wrong, missing_included = compare(included.records, expected)
      self.assertEqual(wrong, [])
      self.assertLess(len(missing_included), len(missing))
      self.assertTrue(any(segment.damaged for segment in included.segments))

   def test_without_a_meta_page_the_log_is_still_found_and_ordered(self):
      details = make_details()
      writer = OldFirmware('W25N01GW')
      writer.schedule(978, details)
      generate(writer, hours=10)
      pages = {p: d for p, d in writer.pages.items() if p != writer.meta_page}

      recovery, _ = recover(dump_of('W25N01GW', pages), start_time=START_TIME)
      self.assertIsNone(recovery.meta_page)
      wrong, missing = compare(recovery.records, reference(writer, labels=None))
      self.assertEqual((wrong, missing), ([], []))
      self.assertEqual(len(recovery.segments), 1)

      relative, _ = recover(dump_of('W25N01GW', pages))
      wrong, missing = compare(relative.records, reference(writer, start=0, labels=None))
      self.assertEqual((wrong, missing), ([], []))
      self.assertIn('SECONDS SINCE THE DEPLOYMENT STARTED', relative.report())


class OlderFirmware(unittest.TestCase):

   def test_logs_from_before_march_2024_use_absolute_timestamps_and_the_older_details_layout(self):
      writer = OldFirmware('W25N01GW')
      writer.schedule(500, make_early_details())
      expected = generate_absolute(writer, hours=8)
      recovery, _ = recover(dump_of('W25N01GW', writer.pages))
      self.assertEqual(recovery.timestamps, lr.TIMESTAMPS_ABSOLUTE)
      self.assertIn('before daily times', recovery.details['layout'])
      self.assertEqual(recovery.details['num_devices'], 4)
      self.assertEqual(recovery.device_label, 'CHILD')
      wrong, missing = compare(recovery.records, expected)
      self.assertEqual((wrong, missing), ([], []))
      self.assertGreater(recovery.era_counts[lr.TIMESTAMPS_ABSOLUTE], 50 * max(1, recovery.era_counts[lr.TIMESTAMPS_RELATIVE]))

   def test_newer_logs_are_not_mistaken_for_older_ones(self):
      writer = OldFirmware('W25N01GW')
      writer.schedule(500, make_details())
      generate(writer, hours=8)
      recovery, _ = recover(dump_of('W25N01GW', writer.pages))
      self.assertEqual(recovery.timestamps, lr.TIMESTAMPS_RELATIVE)
      self.assertGreater(recovery.era_counts[lr.TIMESTAMPS_RELATIVE], 50 * max(1, recovery.era_counts[lr.TIMESTAMPS_ABSOLUTE]))

   def test_the_details_layout_is_identified_for_every_device_count(self):
      for devices in range(1, 11):
         early = lr.unpack_details(make_early_details(devices=devices))
         self.assertIn('before daily times', early['layout'], devices)
         self.assertEqual(early['num_devices'], devices)
         self.assertEqual(early['labels'][0].rstrip(b'\0'), b'CHILD')
      for use_daily in (0, 1):
         blob = bytearray(make_details())
         blob[16] = use_daily
         current = lr.unpack_details(bytes(blob))
         self.assertEqual(current['layout'], 'with daily times')
         self.assertEqual((current['use_daily_times'], current['num_devices']), (use_daily, 4))


class BadBlocks(unittest.TestCase):

   def test_alliance_bad_block_table_is_honoured_and_stale_data_in_the_block_is_ignored(self):
      details = make_details()
      writer = OldFirmware('AS5F18G04SND', bad_blocks={205})
      # A block that would not erase keeps whatever an earlier deployment left in it
      for i in range(64):
         writer.pages[205 * 64 + i] = b'DA' + struct.pack('<H', writer.max_payload) + bytes(random.Random(i).randrange(256) for _ in range(writer.max_payload))
      writer.pages[writer.data_end] = (b'BBM_' + struct.pack('<I', 1) + struct.pack('<256I', 205 * 64, *([0] * 255))).ljust(writer.page_size, b'\0')
      writer.schedule(203, details)
      generate(writer, hours=12)
      self.assertTrue(any(p // 64 > 205 for p in writer.pages if p < writer.data_end), 'scenario must cross the bad block')

      recovery, _ = recover(dump_of('AS5F18G04SND', writer.pages))
      wrong, missing = compare(recovery.records, reference(writer, labels=labels_for(details)))
      self.assertEqual((wrong, missing), ([], []))
      self.assertEqual(len(recovery.segments), 1, 'the pages either side of a bad block were written consecutively')
      self.assertEqual(recovery.exclude_blocks, {205})

   def test_the_early_alliance_layout_is_recognised_from_the_pages_themselves(self):
      # Firmware of Nov-Dec 2024 kept only a 20-block reserve and, after a reboot, never applied its table
      details = make_details()
      writer = OldFirmware('AS5F18G04SND', reserved=20)
      writer.pages[4076 * 64] = (b'BBM_' + struct.pack('<256I', 4070 * 64, *([0] * 255))).ljust(writer.page_size, b'\0')
      writer.schedule(4068, details)
      generate(writer, hours=36)
      used_blocks = {p // 64 for p in writer.pages}
      self.assertTrue({4070, 4075, 0} <= used_blocks, 'scenario must use the early reserve region and wrap')

      recovery, _ = recover(dump_of('AS5F18G04SND', writer.pages))
      self.assertEqual(recovery.reserved_blocks, 20)
      self.assertEqual(recovery.exclude_blocks, set())
      wrong, missing = compare(recovery.records, reference(writer, labels=labels_for(details)))
      self.assertEqual((wrong, missing), ([], []))
      self.assertEqual(len(recovery.segments), 1)
      self.assertIn('Nov-Dec 2024', recovery.report())

   def test_winbond_lut_is_honoured_including_the_empty_entry_quirk_for_block_zero(self):
      # The old firmware matched LUT entries on their LBA alone, so its empty entries made it step over block 0
      details = make_details()
      writer = OldFirmware('W25N01GW', bad_blocks={0, 2})
      lut = struct.pack('>HH', 2, 1000) + b'\0' * (4 * 19)
      writer.schedule(978, details)
      generate(writer, hours=24)
      used_blocks = {p // 64 for p in writer.pages}
      self.assertTrue({983, 1, 3} <= used_blocks and not {0, 2} & used_blocks, 'scenario must wrap past blocks 0 and 2')

      recovery, _ = recover(dump_of('W25N01GW', writer.pages, lut=lut))
      wrong, missing = compare(recovery.records, reference(writer, labels=labels_for(details)))
      self.assertEqual((wrong, missing), ([], []))
      self.assertEqual(len(recovery.segments), 1)
      self.assertEqual(recovery.exclude_blocks, {2})


class Stream(unittest.TestCase):

   def test_damaged_frames_and_an_interrupted_earlier_pass_are_survived(self):
      details = make_details()
      writer = OldFirmware('W25N01GW')
      writer.schedule(20, details)
      generate(writer, hours=3)
      clean = dump_of('W25N01GW', writer.pages)

      # A pass cut short by a reset, line noise, and one page frame with a corrupted byte
      interrupted = clean[:len(clean) // 3]
      damaged = bytearray(clean)
      target = sorted(p for p in writer.pages if p != writer.meta_page)[10]
      at = damaged.find(struct.pack('<I', target) + b'\x00\x01\x00\x00DA')
      self.assertGreater(at, 0)
      damaged[at + 100] ^= 0xFF
      stream = interrupted + b'\x00garbage TTRC\x07\x00\xff\xff' + bytes(damaged)

      dump, notes = lr.read_dump(stream)
      self.assertIsNotNone(dump.end)
      self.assertNotIn(target, dump.pages)
      self.assertTrue(any('damaged frame' in note for note in notes))
      self.assertTrue(any('2 passes' in note for note in notes))
      recovery = lr.Recovery(dump)
      wrong, missing = compare(recovery.records, reference(writer, labels=labels_for(details)))
      self.assertEqual(wrong, [])
      self.assertTrue(set(missing) <= keys_touching(writer, [target]))

   def test_an_incomplete_dump_says_so(self):
      details = make_details()
      writer = OldFirmware('W25N01GW')
      writer.schedule(20, details)
      generate(writer, hours=1)
      stream = dump_of('W25N01GW', writer.pages)
      end = stream.rfind(lr.FRAME_SYNC)
      _, notes = lr.read_dump(stream[:end])
      self.assertTrue(any('INCOMPLETE DUMP' in note for note in notes))


class Mirror(unittest.TestCase):

   def test_the_python_mirror_describes_the_same_flash_as_the_firmware(self):
      # The scenarios that only the mirror can present are only as good as the mirror, so hold it to the firmware
      if not HARNESS:
         self.skipTest('needs the firmware harness')
      for part in PARTS:
         writer = OldFirmware(part)
         writer.schedule(30, make_details())
         generate(writer, hours=2)
         firmware, _ = lr.read_dump(firmware_dump(part, writer.pages))
         mirror, _ = lr.read_dump(python_dump(part, writer.pages))
         for key in ('chip', 'page_size', 'spare_size', 'pages_per_block', 'block_count', 'reserved_blocks', 'uid'):
            self.assertEqual(firmware.info[key], mirror.info[key], key)
         self.assertEqual(sorted(firmware.pages), sorted(mirror.pages))
         for number, page in firmware.pages.items():
            self.assertEqual((page.data, page.uncorrectable, page.reads), (mirror.pages[number].data, False, 1))
         self.assertEqual((firmware.lut is None), (mirror.lut is None))
         self.assertEqual({k: v for k, v in firmware.end.items() if k != 'elapsed_ms'},
                          {k: v for k, v in mirror.end.items() if k != 'elapsed_ms'})


class Outputs(unittest.TestCase):

   def test_files_are_named_and_shaped_as_the_dashboard_writes_them(self):
      details = make_details()
      writer = OldFirmware('W25N01GW')
      writer.schedule(20, details)
      generate(writer, hours=1)
      recovery, notes = recover(dump_of('W25N01GW', writer.pages))
      with tempfile.TemporaryDirectory() as directory:
         base, report = recovery.write(directory, notes)
         self.assertEqual(os.path.basename(base), f'CHILD_{START_TIME}')
         with open(base + '.pkl', 'rb') as file:
            self.assertEqual(pickle.load(file), recovery.records)
         with open(base + '.ttg', 'rb') as file:
            self.assertEqual(file.read(), bytes(writer.sessions[0]))
         self.assertIn(f'META at page {writer.meta_page}', report)
         self.assertIn('<- this tag', report)


if __name__ == '__main__':
   arguments = argparse.ArgumentParser()
   arguments.add_argument('--harness', help='path to legacy_log_recovery_sim (found automatically once built)')
   arguments.add_argument('--no-harness', action='store_true', help='use only the Python mirror of the encoder')
   known, rest = arguments.parse_known_args()
   default = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'firmware', 'tests', 'tools',
                          'legacy_log_recovery_sim', 'legacy_log_recovery_sim')
   HARNESS = None if known.no_harness else (known.harness or (default if os.path.exists(default) else None))
   print(f'Recovery firmware harness: {HARNESS or "not built, using the Python mirror of its encoder only"}')
   unittest.main(argv=[sys.argv[0]] + rest, verbosity=2)
