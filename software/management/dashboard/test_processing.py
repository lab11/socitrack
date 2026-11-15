#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Tests for the analysis helpers in processing.py: the study's time zone, and the daily ranging statistics.

   python3 test_processing.py

The batch scripts 1_/2_convert_to_csv.py read the printed statistics back, so the format is checked against the same
patterns they use.
"""

import contextlib, datetime, io, os, pickle, re, shutil, sys, tempfile, unittest, warnings
os.environ.setdefault('MPLBACKEND', 'Agg')

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import processing
import deployment_record
import tottag_format
import test_deployment_record

CHICAGO_5PM = 1788300000     # Tue, Sep 1, 2026, 22:00 UTC


def write_pkl(folder, name, records):
   path = os.path.join(folder, name)
   with open(path, 'wb') as file:
      pickle.dump(records, file)
   return path

def quietly(function, *args, **kwargs):
   output = io.StringIO()
   with contextlib.redirect_stdout(output), warnings.catch_warnings():
      warnings.simplefilter('ignore')
      result = function(*args, **kwargs)
   return result, output.getvalue()


class PlotTimes(unittest.TestCase):

   def test_a_timestamp_is_placed_where_the_study_s_clock_read_it(self):
      self.assertAlmostEqual(processing.plot_times([CHICAGO_5PM], 'America/Chicago')[0],
                             processing.mdates.date2num(datetime.datetime(2026, 9, 1, 17, 0)), places=9)
      self.assertAlmostEqual(processing.plot_times([CHICAGO_5PM + 0.5], 'Asia/Tokyo')[0],
                             processing.mdates.date2num(datetime.datetime(2026, 9, 2, 7, 0, 0, 500000)), places=9)

   def test_the_clock_change_is_followed(self):
      # 06:30 UTC on Nov 1 is 1:30 AM CDT; an hour later the clocks have gone back, and it is 1:30 AM CST
      before = 1793514600
      self.assertEqual(list(processing.local_times([before, before + 3600], 'America/Chicago').strftime('%H:%M')), ['01:30', '01:30'])

   def test_an_explicit_zone_wins_and_otherwise_the_record_s_is_used(self):
      frame = processing.pd.DataFrame()
      frame.attrs['timezone'] = 'Europe/London'
      self.assertEqual(processing.study_timezone(frame, 'Asia/Tokyo'), 'Asia/Tokyo')
      self.assertEqual(processing.study_timezone(frame), 'Europe/London')

   def test_with_no_record_this_computer_s_zone_is_used_and_said_once(self):
      frame = processing.pd.DataFrame()
      zone, said = quietly(processing.study_timezone, frame)
      self.assertEqual(zone, processing.machine_timezone())
      self.assertEqual(len(said.splitlines()), 1)
      self.assertIn("this computer's time zone", said)
      self.assertEqual(quietly(processing.study_timezone, frame)[1], '')


class LoadingALog(unittest.TestCase):

   def setUp(self):
      self.folder = tempfile.mkdtemp()
      self.addCleanup(shutil.rmtree, self.folder)
      self.pkl = write_pkl(self.folder, 'CG1.pkl', [{'t': test_deployment_record.START + 10, 'v': 3900}])

   def test_the_zone_comes_from_the_record_that_matches_the_log(self):
      info = test_deployment_record.details(timezone='Australia/Sydney')
      record = test_deployment_record.record_for(info)
      deployment_record.save_record(record, os.path.join(self.folder, 'study.json'))
      test_deployment_record.write_log(self.folder, f'CG1_{test_deployment_record.START}.ttg', bytes.fromhex(record['experimentDetailsHex']))
      data, said = quietly(processing.load_data, self.pkl)
      self.assertEqual(data.attrs['timezone'], 'Australia/Sydney')
      self.assertEqual(said, '')
      self.assertEqual(quietly(processing.load_data, self.pkl, timezone='UTC')[0].attrs['timezone'], 'UTC')

   def test_records_kept_elsewhere_can_be_named(self):
      elsewhere = tempfile.mkdtemp()
      self.addCleanup(shutil.rmtree, elsewhere)
      record = test_deployment_record.record_for(test_deployment_record.details(timezone='Asia/Tokyo'))
      deployment_record.save_record(record, os.path.join(elsewhere, 'study.json'))
      test_deployment_record.write_log(self.folder, f'CG1_{test_deployment_record.START}.ttg', bytes.fromhex(record['experimentDetailsHex']))
      self.assertEqual(quietly(processing.load_data, self.pkl, records=elsewhere)[0].attrs['timezone'], 'Asia/Tokyo')

   def test_without_a_record_it_says_so_in_one_line(self):
      data, said = quietly(processing.load_data, self.pkl)
      self.assertIsNone(data.attrs['timezone'])
      self.assertEqual(len(said.splitlines()), 1)
      self.assertIn('no deployment record matches CG1.pkl', said)


class DailyStatistics(unittest.TestCase):

   def setUp(self):
      self.folder = tempfile.mkdtemp()
      self.addCleanup(shutil.rmtree, self.folder)

   def statistics(self, records, targets, timezone):
      data, _ = quietly(processing.load_data, write_pkl(self.folder, '10051_TC.pkl', records), timezone=timezone)
      return quietly(processing.get_daily_ranging_statistics, data, targets, 3)[1]

   def test_minutes_come_from_the_half_second_grid(self):
      self.assertEqual([processing.whole_minutes(n) for n in (0, 59, 60, 119, 120, 7200)], [0, 0, 1, 1, 1, 60])
      self.assertEqual(processing.grid_minutes(90), 0.75)

   def test_a_target_out_of_range_all_day_has_zero_minutes(self):
      # Ten minutes of distances to CG1, all within touching distance, and none to CG2 until the next day
      records = [{'t': CHICAGO_5PM + 0.5 * i, 'r': {'10051_CG1': 600}} for i in range(1200)] + [{'t': CHICAGO_5PM + 86400, 'r': {'10051_CG2': 600}}]
      printed = self.statistics(records, ['10051_CG1', '10051_CG2'], 'America/Chicago')
      self.assertIn('   Statistics to r.10051_CG1:\n      Minutes in Range: 10\n      Minutes in Touching Distance: 10\n', printed)
      self.assertIn('   Statistics to r.10051_CG2:\n      Minutes in Range: 0\n      Minutes in Touching Distance: 0\n', printed)

   def test_days_split_at_midnight_on_the_study_s_clock(self):
      # 11 PM to 1 AM in Chicago is two days there, but one day, Sep 2, in UTC
      start = CHICAGO_5PM + 6 * 3600
      records = [{'t': start + 0.5 * i, 'r': {'10051_CG1': 600}} for i in range(4 * 3600)]
      self.assertEqual(re.findall(r'Ranging Statistics on (\S+):', self.statistics(records, ['10051_CG1'], 'America/Chicago')), ['09/01/2026', '09/02/2026'])
      self.assertEqual(re.findall(r'Ranging Statistics on (\S+):', self.statistics(records, ['10051_CG1'], 'UTC')), ['09/02/2026'])

   def test_the_printed_statistics_still_read_into_the_batch_scripts(self):
      records = [{'t': CHICAGO_5PM + 0.5 * i, 'r': {'10051_CG1': 600 + i, '10051_CG2': 2000}} for i in range(500)]
      printed = self.statistics(records, ['10051_CG1', '10051_CG2'], 'America/Chicago')
      # The patterns 2_convert_to_csv.py reads with, which take whole minutes
      lines = [line.strip() for line in printed.splitlines()]
      self.assertEqual([m.group(1) for m in map(re.compile(r'Ranging Statistics on (\d{2}/\d{2}/\d{4}):').match, lines) if m], ['09/01/2026'])
      self.assertEqual([int(m.group(1)) for m in map(re.compile(r'Minutes in Range: (\d+)$').match, lines) if m], [4, 4, 4])
      self.assertEqual([int(m.group(1)) for m in map(re.compile(r'Minutes in Touching Distance: (\d+)$').match, lines) if m], [3, 0, 3])
      # And the one 1_convert_to_csv.py reads with
      targets = re.findall(r'Statistics to (r\.\d+_CG\d|Either of \[.*\]):', printed)
      self.assertEqual(targets, ['r.10051_CG1', 'r.10051_CG2', "Either of ['10051_CG1', '10051_CG2']"])


class PlotFixes(unittest.TestCase):

   def setUp(self):
      self.folder = tempfile.mkdtemp()
      self.addCleanup(shutil.rmtree, self.folder)

   def load(self, name, records):
      return quietly(processing.load_data, write_pkl(self.folder, name, records), timezone='America/Chicago')[0]

   def test_the_pair_view_scales_to_the_longer_of_the_two_logs(self):
      near = self.load('A.pkl', [{'t': CHICAGO_5PM + i, 'r': {'B': 3000}} for i in range(600)])
      far = self.load('B.pkl', [{'t': CHICAGO_5PM + i, 'r': {'A': 1000}} for i in range(600)])
      quietly(processing.visualize_ranging_pair_slider, near, far, 'A', 'B', unit='m')
      self.assertAlmostEqual(processing.plt.gcf().axes[0].get_ylim()[1], 3.0)
      processing.plt.close('all')

   def test_the_charger_detection_uses_the_peak_width_it_is_given(self):
      readings = [{'t': CHICAGO_5PM + 60 * i, 'v': 4000 + (200 if (i // 100) % 2 else 0) - abs((i % 100) - 50)} for i in range(600)]
      data = self.load('V.pkl', readings)
      narrow = quietly(processing.get_off_and_on_charger_times, data, 'V', peak_width=5, window_size=3, visualize=False)[1]
      wide = quietly(processing.get_off_and_on_charger_times, data, 'V', peak_width=50, window_size=3, visualize=False)[1]
      self.assertGreater(len(narrow.splitlines()), len(wide.splitlines()))
      self.assertIn('2026-09-01 17:51:00 CDT TAKEN OFF CHARGER', narrow)


if __name__ == '__main__':
   unittest.main()
