#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Tests for what a download leaves on disk, and for rebuilding its .pkl from the .ttg it saved.

   python3 test_downloads.py

Every download saves the raw stream as `<label>_<start>.ttg` beside the parsed `<label>.pkl`, and `tottag.py --convert`
writes from that .ttg exactly the .pkl the download wrote.
"""

import asyncio, contextlib, io, os, pickle, queue, shutil, struct, sys, tempfile, unittest, zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tottag
import tottag_format

START = 1788300000
PARENT = [0x02, 0x00, 0x42, 0xE5, 0x98, 0xC0]
CHILD = [0x3E, 0x00, 0x42, 0xE5, 0x98, 0xC0]
NURSE = [0x07, 0x00, 0x42, 0xE5, 0x98, 0xC0]


def details(labels=(b'Parent', b'Child', b'')):
   uids = [PARENT, CHILD, NURSE][:len(labels)] + [[0] * 6 for _ in range(tottag.MAX_NUM_DEVICES - len(labels))]
   return {'start_time': START, 'end_time': START + 86400, 'daily_start_time': 0, 'daily_end_time': 0, 'use_daily_times': 0,
           'num_devices': len(labels), 'uids': uids, 'labels': list(labels) + [b''] * (tottag.MAX_NUM_DEVICES - len(labels))}

def record(record_type, ms, body):
   return struct.pack('<HBI', len(body), record_type, ms) + body

def stream(blob):
   """A framed log of two pages: distances to the child and the nurse, and a battery reading."""
   pages = [[(500 * i, record(tottag_format.STORAGE_TYPE_RANGES, 500 * i, bytes([2, CHILD[0]]) + struct.pack('<H', 1200 + i) + bytes([NURSE[0]]) +
                                struct.pack('<H', 3000))) for i in range(20)],
            [(60_000, record(tottag_format.STORAGE_TYPE_VOLTAGE, 60_000, struct.pack('<I', 3950)))]]
   body = b''
   for seq, records in enumerate(pages):
      payload = b''.join(r for _, r in records)
      times = [ms for ms, _ in records]
      body += tottag_format.V2_PAGE_HEADER.pack(seq, min(times), max(times), len(payload), len(records), zlib.crc32(payload)) + payload
   return tottag_format.V2_STREAM_HEADER.pack(tottag_format.V2_STREAM_MAGIC, 2, len(blob), len(pages), 0) + blob + body

def blob_of(info):
   return tottag.pack_experiment_details(info)[1:]


class FakeBluetooth:
   """Just enough of a connected TotTag for a finished download to be saved."""
   address = 'C0:98:E5:42:00:02'
   async def stop_notify(self, _uuid):
      pass


class EveryDownloadSavesBoth(unittest.TestCase):

   def setUp(self):
      self.folder = tempfile.mkdtemp()
      self.addCleanup(shutil.rmtree, self.folder)

   def finish_download(self, info, data):
      comms = tottag.TotTagBLE(queue.Queue(), queue.Queue(), None)
      comms.connected_device = FakeBluetooth()
      comms.storage_directory = self.folder
      comms.downloading_log_file = True
      comms.data, comms.data_index, comms.data_details = bytearray(data), len(data), info
      with contextlib.redirect_stdout(io.StringIO()):
         asyncio.run(comms.download_logs_done())
      return comms.result_queue.get_nowait()

   def test_a_finished_download_saves_the_ttg_beside_the_pkl(self):
      info = details()
      data = stream(blob_of(info))
      self.assertEqual(self.finish_download(info, data), ('DOWNLOADED', True))
      self.assertEqual(sorted(os.listdir(self.folder)), ['Parent.pkl', f'Parent_{START}.ttg'])
      with open(os.path.join(self.folder, f'Parent_{START}.ttg'), 'rb') as file:
         self.assertEqual(file.read(), data, 'the .ttg is not the stream as the TotTag sent it')
      with open(os.path.join(self.folder, 'Parent.pkl'), 'rb') as file:
         records = pickle.load(file)
      self.assertEqual(records[0]['r'], {'Child': 1200, '7': 3000})

   def test_the_download_screen_no_longer_asks_whether_to_keep_the_raw_stream(self):
      self.assertFalse(hasattr(tottag.TotTagBLE(queue.Queue(), queue.Queue(), None), 'download_raw_logs'))
      with open(tottag.__file__, encoding='utf-8') as file:
         self.assertNotIn('Download Raw Unprocessed Data', file.read())


class RebuildingThePkl(unittest.TestCase):

   def setUp(self):
      self.downloaded, self.copied = tempfile.mkdtemp(), tempfile.mkdtemp()
      self.addCleanup(shutil.rmtree, self.downloaded)
      self.addCleanup(shutil.rmtree, self.copied)

   def download(self, info, from_uid=PARENT[0]):
      with contextlib.redirect_stdout(io.StringIO()):
         tottag.process_tottag_data(from_uid, self.downloaded, info, stream(blob_of(info)))

   def convert(self, name, as_name=None):
      source = os.path.join(self.copied, as_name or name)
      shutil.copy(os.path.join(self.downloaded, name), source)
      with contextlib.redirect_stdout(io.StringIO()):
         return tottag.convert_ttg_file(source)

   def assertSamePkl(self, downloaded, rebuilt):
      with open(downloaded, 'rb') as a, open(rebuilt, 'rb') as b:
         self.assertEqual(a.read(), b.read())

   def test_the_rebuilt_pkl_is_the_one_the_download_wrote(self):
      self.download(details())
      rebuilt = self.convert(f'Parent_{START}.ttg')
      self.assertEqual(rebuilt, os.path.join(self.copied, 'Parent.pkl'))
      self.assertSamePkl(os.path.join(self.downloaded, 'Parent.pkl'), rebuilt)
      self.assertEqual(sorted(os.listdir(self.copied)), ['Parent.pkl', f'Parent_{START}.ttg'], 'the .ttg was rewritten or another file added')

   def test_an_unlabeled_tottag_is_named_by_its_id_as_the_download_names_it(self):
      self.download(details(), from_uid=NURSE[0])
      self.assertSamePkl(os.path.join(self.downloaded, '7.pkl'), self.convert(f'7_{START}.ttg'))

   def test_a_label_loses_only_what_a_file_name_cannot_hold(self):
      self.download(details(('Mamá/Papá: 1'.encode('utf-8'), b'Child')))
      self.assertEqual(sorted(os.listdir(self.downloaded)), ['MamáPapá 1.pkl', f'MamáPapá 1_{START}.ttg'])
      self.assertSamePkl(os.path.join(self.downloaded, 'MamáPapá 1.pkl'), self.convert(f'MamáPapá 1_{START}.ttg'))

   def test_a_label_of_only_unsafe_characters_falls_back_to_the_id(self):
      self.download(details((b'../', b'Child')))
      self.assertEqual(sorted(os.listdir(self.downloaded)), ['2.pkl', f'2_{START}.ttg'])
      self.assertSamePkl(os.path.join(self.downloaded, '2.pkl'), self.convert(f'2_{START}.ttg'))

   def test_a_renamed_log_is_refused_rather_than_guessed_at(self):
      self.download(details())
      with self.assertRaisesRegex(ValueError, 'labeled "mystery"'):
         self.convert(f'Parent_{START}.ttg', as_name=f'mystery_{START}.ttg')

   def test_a_legacy_log_says_why_it_cannot_be_rebuilt(self):
      legacy = os.path.join(self.copied, f'Parent_{START}.ttg')
      with open(legacy, 'wb') as file:
         file.write(bytes([tottag_format.STORAGE_TYPE_VOLTAGE]) + struct.pack('<II', 0, 3950))
      with self.assertRaisesRegex(ValueError, 'carries no deployment details'):
         tottag.convert_ttg_file(legacy)


if __name__ == '__main__':
   unittest.main()
