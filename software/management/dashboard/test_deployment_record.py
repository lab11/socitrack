#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Tests for deployment records: writing one when a deployment is scheduled, and finding the one a log belongs to.

   python3 test_deployment_record.py

A record must be interchangeable with the browser tool's (its DeploymentManifest), so these check its fields against
that interface and its values against the browser tool's own tests in tottag-schema/test/manifest.test.ts.
"""

import datetime, json, os, pickle, shutil, struct, sys, tempfile, unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tottag
import tottag_format
import deployment_record

S1 = [0x11, 0x22, 0x33, 0x44, 0x55, 0x66]
CG1 = [0x12, 0x22, 0x33, 0x44, 0x55, 0x66]
OTHER = [0x7A, 0x01, 0x02, 0x03, 0x04, 0x05]
START = 1788300000
WRITTEN_AT = datetime.datetime(2026, 9, 11, 21, 0, tzinfo=datetime.timezone.utc)


def details(start=START, end=START + 2 * 86400, daily=(13 * 3600, 3 * 3600), devices=((S1, b'S1'), (CG1, b'CG1')), timezone='America/Chicago'):
   """What the scheduling form hands to the Bluetooth thread."""
   blank = tottag.MAX_NUM_DEVICES - len(devices)
   return {'start_time': start, 'end_time': end, 'daily_start_time': daily[0] if daily else 0, 'daily_end_time': daily[1] if daily else 0,
           'use_daily_times': 1 if daily else 0, 'num_devices': len(devices), 'uids': [list(uid) for uid, _ in devices] + [[0] * 6] * blank,
           'labels': [label for _, label in devices] + [b''] * blank, 'devices': [tottag.usb_device_name(uid) for uid, _ in devices],
           'timezone': timezone}

def record_for(info, outcomes=('confirmed', 'confirmed'), written_at=WRITTEN_AT):
   packed = tottag.pack_experiment_details(info)
   results = [tottag.written_device(info, info['uids'][i], outcome, None if outcome == 'confirmed' else 'Timed out')
              for i, outcome in enumerate(outcomes)]
   return deployment_record.build_record(packed[1:], info['timezone'], results, written_at=written_at)

def write_log(folder, name, blob):
   """A .ttg holding only a header and its deployment details, which is all that finding its record reads."""
   with open(os.path.join(folder, name), 'wb') as file:
      file.write(tottag_format.V2_STREAM_HEADER.pack(tottag_format.V2_STREAM_MAGIC, 2, len(blob), 0, 0) + blob)


class FakeUsbTotTag:
   """Stores the deployment details written to it and reads them back, or reads back `stores` in their place."""

   def __init__(self, stores=None):
      self.stores, self.stored, self.reply = stores, None, b''

   def reset_input_buffer(self):
      self.reply = b''

   def write(self, data):
      if data[0] == tottag.MAINTENANCE_NEW_EXPERIMENT:
         self.stored = self.stores if self.stores is not None else bytes(data[1:])
      elif data == bytes([tottag.USB_GET_EXPERIMENT_COMMAND]):
         self.reply += struct.pack('<H', len(self.stored)) + self.stored

   def read(self, size):
      chunk, self.reply = self.reply[:size], self.reply[size:]
      return chunk


class TheRecordWritten(unittest.TestCase):

   # Every field of DeploymentManifest and DeviceManifestEntry in tottag-schema/src/manifest.ts, by JSON type. The
   # optional batteryMv, firmware, and hardware are left out, as the browser tool leaves them out when it has none.
   MANIFEST = {'kind': str, 'manifestVersion': int, 'writtenAt': str, 'name': (str, type(None)), 'deployment': dict,
               'devices': list, 'experimentDetailsHex': str, 'summary': str}
   DEPLOYMENT = {'timezone': str, 'start': str, 'end': str, 'startTime': int, 'endTime': int, 'durationDays': (int, float),
                 'dailyHours': (dict, type(None)), 'afterClockChange': list}
   DEVICE = {'name': str, 'eui': str, 'shortUid': str, 'label': str, 'outcome': str, 'detail': (str, type(None))}

   def assertFields(self, value, fields):
      self.assertEqual(set(value), set(fields))
      for key, kind in fields.items():
         self.assertIsInstance(value[key], kind, key)
         self.assertNotIsInstance(value[key], bool, key)

   def test_it_has_every_field_of_the_browser_tool_s_record_with_the_right_types(self):
      record = json.loads(json.dumps(record_for(details(), outcomes=('confirmed', 'failed'))))
      self.assertFields(record, self.MANIFEST)
      self.assertFields(record['deployment'], self.DEPLOYMENT)
      for device in record['devices']:
         self.assertFields(device, self.DEVICE)
      self.assertEqual(record['kind'], 'tottag.deployment-manifest')
      self.assertEqual(record['manifestVersion'], 2)
      self.assertEqual(record['writtenAt'], '2026-09-11T21:00:00.000Z')
      self.assertRegex(record['deployment']['start'], r'^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}$')
      self.assertEqual(set(record['deployment']['dailyHours']), {'start', 'end'})
      for change in record['deployment']['afterClockChange']:
         self.assertEqual(set(change), {'from', 'dailyHours'})

   def test_times_are_on_the_study_s_clock_as_the_browser_tool_writes_them(self):
      # 13:00 to 03:00 on the TotTag's clock is 8:00 AM to 10:00 PM in Chicago in September
      record = record_for(details(), outcomes=('confirmed', 'failed'))
      self.assertEqual(record['deployment']['timezone'], 'America/Chicago')
      self.assertEqual(record['deployment']['start'], '2026-09-01T17:00')
      self.assertEqual(record['deployment']['dailyHours'], {'start': '08:00', 'end': '22:00'})
      self.assertEqual(record['deployment']['durationDays'], 2)
      self.assertEqual(record['deployment']['afterClockChange'], [])
      self.assertIn('1 of 2 tags confirmed the deployment.', record['summary'])
      self.assertIn('1 tag did not take it.', record['summary'])
      self.assertTrue(record['summary'].endswith('Runs Tue, Sep 1, 2026, 5:00 PM to Thu, Sep 3, 2026, 5:00 PM (America/Chicago).'))

   def test_a_clock_change_during_the_deployment_is_spelled_out(self):
      autumn = details(start=1793361600, end=1793707200, daily=(12 * 3600, 1 * 3600), devices=((S1, b'S1'),))
      record = record_for(autumn, outcomes=('unconfirmed',))
      self.assertEqual(record['deployment']['dailyHours'], {'start': '07:00', 'end': '20:00'})
      self.assertEqual(record['deployment']['afterClockChange'], [{'from': '2026-11-01T01:00', 'dailyHours': {'start': '06:00', 'end': '19:00'}}])

   def test_each_tottag_is_named_as_the_browser_tool_names_it(self):
      record = record_for(details(devices=((OTHER, b'x'),)), outcomes=('confirmed',))
      self.assertEqual(record['devices'], [{'name': 'TotTag 01:7A', 'eui': '05:04:03:02:01:7A', 'shortUid': '0x7A', 'label': 'x',
                                            'outcome': 'confirmed', 'detail': None}])

   def test_it_carries_the_exact_bytes_written_to_each_tottag(self):
      info = details()
      packed = tottag.pack_experiment_details(info)
      device = FakeUsbTotTag()
      tottag.usb_write_experiment_details(device, packed)
      record, filename = tottag.scheduling_record(info, packed, [tottag.written_device(info, S1, 'confirmed', None)])
      self.assertEqual(bytes.fromhex(record['experimentDetailsHex']), device.stored)
      self.assertEqual(len(device.stored), 239)
      self.assertRegex(filename, r'^tottag-deployment-\d{4}-\d{2}-\d{2}\.json$')

   def test_a_tottag_that_stored_something_else_is_recorded_as_a_mismatch(self):
      info = details()
      packed = tottag.pack_experiment_details(info)
      with self.assertRaises(tottag.DetailsMismatch) as caught:
         tottag.usb_write_experiment_details(FakeUsbTotTag(stores=bytes(239)), packed)
      self.assertEqual(tottag.write_outcome(caught.exception)[0], 'mismatch')
      self.assertEqual(tottag.write_outcome(TimeoutError('timed out')), ('failed', 'Could not be written: timed out'))

   def test_no_record_is_offered_when_no_tottag_took_the_deployment(self):
      info = details()
      packed = tottag.pack_experiment_details(info)
      failed = [tottag.written_device(info, uid, 'failed', 'Timed out') for uid in (S1, CG1)]
      self.assertIsNone(tottag.scheduling_record(info, packed, failed))
      self.assertIsNotNone(tottag.scheduling_record(info, packed, failed[:1] + [tottag.written_device(info, CG1, 'unconfirmed', 'Not read back.')]))

   def test_its_default_name_is_the_one_the_browser_tool_gives(self):
      self.assertEqual(deployment_record.record_filename(['10051_CG1', '10051_TC'], 'America/Chicago', now=START), '10051-2026-09-01.json')
      self.assertEqual(deployment_record.record_filename(['Parent', 'Child'], 'Asia/Tokyo', now=START), 'tottag-deployment-2026-09-02.json')
      self.assertEqual(deployment_record.record_filename([], 'UTC', name='Family  Café #3', now=START), 'Family-Cafe-3-2026-09-01.json')

   def test_a_saved_record_loads_back_unchanged_and_other_files_are_passed_over(self):
      folder = tempfile.mkdtemp()
      self.addCleanup(shutil.rmtree, folder)
      record = record_for(details())
      deployment_record.save_record(record, os.path.join(folder, 'study.json'))
      with open(os.path.join(folder, 'other.json'), 'w') as file:
         json.dump({'kind': 'something else'}, file)
      with open(os.path.join(folder, 'broken.json'), 'w') as file:
         file.write('{')
      self.assertEqual(deployment_record.load_records(folder), [record])
      with self.assertRaises(ValueError):
         deployment_record.load_records(os.path.join(folder, 'other.json'))


class FindingALogsRecord(unittest.TestCase):

   def setUp(self):
      self.info = details()
      self.blob = tottag.pack_experiment_details(self.info)[1:]
      self.record = record_for(self.info)

   def test_a_log_whose_details_are_the_record_s_byte_for_byte_matches_it(self):
      self.assertIs(deployment_record.match_record([self.record], details=self.blob), self.record)

   def test_otherwise_the_same_start_time_and_a_listed_tottag_match(self):
      # The TotTag marks its stored copy canceled, so the bytes differ from those written
      canceled = self.blob[:-1] + b'\x01'
      self.assertIs(deployment_record.match_record([self.record], details=canceled, start_time=START, eui='66:55:44:33:22:12'), self.record)

   def test_a_different_start_time_or_an_unlisted_tottag_matches_nothing(self):
      other = tottag.pack_experiment_details(details(start=START + 60))[1:]
      self.assertIsNone(deployment_record.match_record([self.record], details=other, start_time=START + 60, eui='66:55:44:33:22:12'))
      self.assertIsNone(deployment_record.match_record([self.record], details=other, start_time=START, eui='05:04:03:02:01:7A'))
      self.assertIsNone(deployment_record.match_record([], details=self.blob, start_time=START, eui='66:55:44:33:22:12'))

   def test_the_newest_of_several_matching_records_wins(self):
      later = record_for(self.info, written_at=WRITTEN_AT + datetime.timedelta(hours=1))
      later['deployment']['timezone'] = 'America/New_York'
      self.assertIs(deployment_record.match_record([later, self.record], details=self.blob), later)

   def test_a_legacy_log_is_known_by_its_label_where_it_cannot_name_its_tottag(self):
      self.assertIs(deployment_record.match_record([self.record], start_time=START, label='CG1'), self.record)
      self.assertIsNone(deployment_record.match_record([self.record], start_time=START, label='Nobody'))

   def test_a_pkl_is_matched_through_the_ttg_saved_beside_it(self):
      folder = tempfile.mkdtemp()
      self.addCleanup(shutil.rmtree, folder)
      deployment_record.save_record(self.record, os.path.join(folder, 'study.json'))
      with open(os.path.join(folder, 'CG1.pkl'), 'wb') as file:
         pickle.dump([{'t': START + 10}], file)
      write_log(folder, f'CG1_{START}.ttg', self.blob[:-1] + b'\x01')
      # An earlier deployment's log under the same label, which the .pkl was not made from
      write_log(folder, f'CG1_{START - 86400 * 30}.ttg', tottag.pack_experiment_details(details(start=START - 86400 * 30))[1:])
      identity = deployment_record.log_identity(os.path.join(folder, 'CG1.pkl'), first_timestamp=START + 10)
      self.assertEqual((identity['start_time'], identity['eui'], identity['label']), (START, '66:55:44:33:22:12', 'CG1'))
      self.assertEqual(deployment_record.find_record(os.path.join(folder, 'CG1.pkl'), first_timestamp=START + 10), self.record)
      self.assertEqual(deployment_record.find_record(os.path.join(folder, f'CG1_{START}.ttg')), self.record)
      self.assertIsNone(deployment_record.find_record(os.path.join(folder, f'CG1_{START - 86400 * 30}.ttg')))


if __name__ == '__main__':
   unittest.main()
