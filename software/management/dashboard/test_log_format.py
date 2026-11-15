#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Tests for how tottag_format reads a page-framed log's clock.

   python3 test_log_format.py
"""

import os, struct, sys, unittest, zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tottag_format

START = 1791460800
RANGE = bytes([1, 0x0b, 0xdc, 0x05])
RESET = bytes([0x08, 0xe0])        # AE's real reset record from a restart


def record(record_type, ms, body):
   return struct.pack('<HBI', len(body), record_type, ms) + body

def stream(pages):
   """A framed log of the given pages, each a list of (type, ms, body), with headers advertising each page's earliest and latest record."""
   details = struct.pack('<I', START) + bytes(235)
   body = b''
   for seq, records in enumerate(pages):
      payload = b''.join(record(*r) for r in records)
      times = [ms for _, ms, _ in records]
      body += tottag_format.V2_PAGE_HEADER.pack(seq, min(times), max(times), len(payload), len(records), zlib.crc32(payload)) + payload
   return tottag_format.V2_STREAM_HEADER.pack(tottag_format.V2_STREAM_MAGIC, 2, len(details), len(pages), 0) + details + body


class Clock(unittest.TestCase):

   def test_a_step_that_begins_at_a_restart_is_the_device_s_own_clock(self):
      # The restart's reset record opens its page 8.9 s behind the network time the page before ended on; the last
      # page steps back again with no restart, which is a moved time base
      log = stream([[(tottag_format.STORAGE_TYPE_RANGES, 530_858_000, RANGE), (tottag_format.STORAGE_TYPE_RANGES, 530_858_500, RANGE)],
                    [(tottag_format.STORAGE_TYPE_RESET_REASON, 530_849_600, RESET), (tottag_format.STORAGE_TYPE_RANGES, 530_850_000, RANGE)],
                    [(tottag_format.STORAGE_TYPE_RANGES, 530_840_000, RANGE)]])
      _records, report = tottag_format.parse(log)
      self.assertEqual([step[3] for step in report['time_discontinuities']], [530_849_600, 530_840_000])
      self.assertEqual([step[3] for step in report['restart_steps']], [530_849_600])


if __name__ == '__main__':
   unittest.main()
