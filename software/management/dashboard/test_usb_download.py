#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Tests for downloading a log over USB, against a stand-in for the device's serial port.

   python3 test_usb_download.py

The device answers each download request with its ID, the length of its deployment details, and a page-framed log.
Firmware before nandlog 8a88cad declares a date-limited log one page short, so these check that the whole log still
arrives, that nothing is left in the link afterwards, and that the file is always named for the device it came from.
"""

import os, queue, struct, sys, unittest, zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tottag
import tottag_format

DETAILS_BYTES = struct.calcsize('<IIIIBB' + ('6B' * tottag.MAX_NUM_DEVICES) + ((str(tottag.MAX_LABEL_LENGTH) + 's') * tottag.MAX_NUM_DEVICES) + 'B')
AE = bytes([0xAE, 0x11, 0x22, 0x33, 0x44, 0x55])
B02 = bytes([0x02, 0x11, 0x22, 0x33, 0x44, 0x55])
EUI_PREAMBLE = tottag.EUI_LEN + 1 + 2      # the ID, its newline and the details length, sent before the device sizes its log


def page(seq, payload):
   return tottag_format.V2_PAGE_HEADER.pack(seq, 1000 * seq, 1000 * seq + 500, len(payload), 1, zlib.crc32(payload)) + payload

def stream(pages, declared_payload=None, details=True):
   """A page-framed log as the device sends it; `declared_payload` stands in for a header that miscounts."""
   body = b''.join(page(seq, payload) for seq, payload in pages)
   total = sum(len(payload) for _, payload in pages) if declared_payload is None else declared_payload
   blob = bytes(DETAILS_BYTES) if details else b''
   return tottag_format.V2_STREAM_HEADER.pack(tottag_format.V2_STREAM_MAGIC, 2, len(blob), len(pages), total) + blob + body

def response(uid, log):
   return uid + b'\n' + struct.pack('<H', DETAILS_BYTES) + log


class FakePort:
   """Answers each download request with the next scripted response, after anything already waiting in the link.

   `stall` holds back everything after the ID and details length for that many reads, which come back empty as a
   real port's do when its timeout passes: the time a device spends sizing a long log before streaming it.
   """

   def __init__(self, responses, waiting=b'', stall=0):
      self.responses = list(responses)
      self.buffer = bytearray(waiting)
      self.held = b''
      self.stall = stall
      self.is_open = True

   def reset_input_buffer(self):
      self.buffer.clear()

   def write(self, data):
      if data[:1] == bytes([tottag.MAINTENANCE_DOWNLOAD_LOG]):
         reply = self.responses.pop(0)
         self.buffer += reply[:EUI_PREAMBLE]
         self.held = reply[EUI_PREAMBLE:]

   def read(self, size):
      if not self.buffer and self.held:
         if self.stall:
            self.stall -= 1
            return b''
         self.buffer += self.held
         self.held = b''
      chunk = bytes(self.buffer[:size])
      del self.buffer[:size]
      return chunk

   def readline(self):
      end = self.buffer.find(b'\n')
      return self.read(len(self.buffer) if end < 0 else end + 1)


class UsbDownload(unittest.TestCase):

   def receiver(self, port, repair_round=0, address=None):
      comms = tottag.TotTagBLE(queue.Queue(), queue.Queue(), None)
      comms.connected_device = port
      comms.repair_round = repair_round
      if address is not None:
         port.address = address
      return comms

   def test_a_log_declared_a_page_short_still_arrives_whole_and_leaves_nothing_behind(self):
      pages = [(0, b'\x01' * 300), (1, b'\x02' * 300), (2, b'\x02\x00\x07' + b'\x00' * 14)]
      log = stream(pages, declared_payload=600)        # the last page's 17 bytes left out, as old firmware does
      port = FakePort([response(AE, log)])
      comms = self.receiver(port)
      comms.data_callback_serial()
      self.assertEqual(bytes(comms.data[:comms.data_index]), log)
      self.assertEqual(port.buffer, bytearray(), 'bytes were left in the link')
      self.assertEqual(port.address.split(':')[-1], 'ae')
      _records, report = tottag_format.parse(bytes(comms.data[:comms.data_index]), 0, None)
      self.assertEqual(tottag_format.missing_seqs(report), [], 'a repair round would have been asked for')

   def test_bytes_left_over_from_an_earlier_response_do_not_rename_the_device(self):
      repair = stream([(2, b'\x02\x00\x07' + b'\x00' * 14)], details=False)
      port = FakePort([response(AE, repair)], waiting=b'\x02\x00\x07\x4a\x48\x30\x01\x08\xe0\x01\x00\x02\x4a\x48\x30\x01\x00')
      comms = self.receiver(port, repair_round=1, address=':'.join(f'{c:02x}' for c in reversed(AE)))
      comms.data_callback_serial()
      self.assertEqual(port.address.split(':')[-1], 'ae')
      self.assertEqual(bytes(comms.data[:comms.data_index]), repair)

   def test_a_repair_round_from_a_different_device_is_ignored(self):
      repair = stream([(2, b'\x00' * 17)], details=False)
      port = FakePort([response(B02, repair)])
      comms = self.receiver(port, repair_round=1, address=':'.join(f'{c:02x}' for c in reversed(AE)))
      comms.data_callback_serial()
      self.assertEqual(port.address.split(':')[-1], 'ae', 'the download was renamed')
      self.assertEqual(comms.data_index, 0, 'another device\'s pages were kept')

   def test_a_long_log_that_takes_many_read_timeouts_to_start_still_downloads(self):
      log = stream([(0, b'\x01' * 300), (1, b'\x02' * 300)])
      port = FakePort([response(AE, log)], stall=20)        # a minute of 3-second timeouts
      comms = self.receiver(port)
      comms.data_callback_serial()
      self.assertEqual(bytes(comms.data[:comms.data_index]), log)

   def test_a_log_that_never_starts_ends_the_download_rather_than_the_thread(self):
      port = FakePort([response(AE, stream([(0, b'\x01' * 300)]))], stall=10 ** 9)
      comms = self.receiver(port)
      saved, tottag.STREAM_START_TIMEOUT_S = tottag.STREAM_START_TIMEOUT_S, 0.05
      try:
         comms.data_callback_serial()
      finally:
         tottag.STREAM_START_TIMEOUT_S = saved
      self.assertEqual(comms.data_index, 0)
      self.assertEqual(comms.command_queue.get_nowait(), 'DOWNLOAD_DONE')

   def test_a_device_that_does_not_identify_itself_yields_nothing(self):
      port = FakePort([b'\x02\x00'])
      comms = self.receiver(port)
      comms.data_callback_serial()
      self.assertEqual(comms.data_index, 0)


if __name__ == '__main__':
   unittest.main()
