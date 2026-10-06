#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Tests for radio_check.py, held to the web tool's radio check by a fixture both sides share.

   python3 test_radio_check.py

The fixture (managementweb/packages/tottag-schema/test/fixtures/radio-check-parity.json) holds scenarios and the web
tool's answers to them: fleets built to fail in one known way per device, and a live test as the raw Bluetooth payloads
four devices would stream. Each is replayed here through the Python port, which must reach the same verdicts, for the
same reasons, in the same words. When the web tool's rules change, `npm run parity:update` in that package rewrites
the fixture and this test fails until radio_check.py follows.
"""

import asyncio, json, math, os, re, sys, unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import radio_check
import tottag_format

FIXTURE = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'managementweb', 'packages', 'tottag-schema',
                       'test', 'fixtures', 'radio-check-parity.json')
CAMEL_EXCEPTIONS = {'coverage_a_to_b': 'coverageAtoB', 'coverage_b_to_a': 'coverageBtoA'}


def snake(name):
   return re.sub(r'(?<!^)(?=[A-Z])', '_', name).lower()

def camel(name):
   if name in CAMEL_EXCEPTIONS:
      return CAMEL_EXCEPTIONS[name]
   head, *rest = name.split('_')
   return head + ''.join(part[:1].upper() + part[1:] for part in rest)

def keys(value, convert):
   """The same structure with every dictionary key converted, and tuples as lists, as JSON holds them."""
   if isinstance(value, dict):
      return {convert(key): keys(item, convert) for key, item in value.items()}
   if isinstance(value, (list, tuple)):
      return [keys(item, convert) for item in value]
   return value

def summary_from_fixture(summary):
   return keys(summary, snake) if summary is not None else None

def positions_from_fixture(positions):
   return {uid: (x, y) for uid, x, y in positions}

def from_hex(text):
   return bytes.fromhex(text)


class Matches(unittest.TestCase):
   """Equality, with floats allowed the last bit or two a different order of operations can cost."""

   def assertSame(self, actual, expected, where='result'):
      if isinstance(expected, dict):
         self.assertIsInstance(actual, dict, where)
         self.assertEqual(sorted(actual), sorted(expected), f'{where}: different fields')
         for key in expected:
            self.assertSame(actual[key], expected[key], f'{where}.{key}')
      elif isinstance(expected, list):
         self.assertIsInstance(actual, list, where)
         self.assertEqual(len(actual), len(expected), f'{where}: {actual!r} against {expected!r}')
         for index, (a, e) in enumerate(zip(actual, expected)):
            self.assertSame(a, e, f'{where}[{index}]')
      elif isinstance(expected, float) or isinstance(actual, float):
         self.assertIsNotNone(actual, where)
         self.assertTrue(math.isclose(actual, expected, rel_tol=1e-9, abs_tol=1e-9), f'{where}: {actual!r} against {expected!r}')
      else:
         self.assertEqual(actual, expected, where)


@unittest.skipUnless(os.path.exists(FIXTURE), 'the web tool\'s parity fixture is not checked out beside the dashboard')
class ParityWithTheWebTool(Matches):

   @classmethod
   def setUpClass(cls):
      with open(FIXTURE) as file:
         cls.fixture = json.load(file)

   def test_every_scenario_is_judged_as_the_web_tool_judges_it(self):
      for scenario in self.fixture['analysis']:
         with self.subTest(scenario['name']):
            devices = [{'uid': d['uid'], 'label': d['label'], 'summary': summary_from_fixture(d['summary'])} for d in scenario['devices']]
            result = radio_check.analyse_radio(devices, positions_from_fixture(scenario['positions']))
            self.assertSame(keys(result, camel), scenario['expected'], scenario['name'])

   def test_a_live_test_streamed_over_bluetooth_is_recorded_and_judged_as_the_web_tool_does(self):
      live = self.fixture['live']
      recorded = []
      for stream in live['streams']:
         recorder = radio_check.LiveRadioRecorder(live['startTime'], live['uids'], live['labels'], stream['uid'])
         for at_ms, kind, payload in stream['events']:
            if kind == 'stats':
               recorder.add_stats(at_ms, tottag_format.decode_radio_stats(from_hex(payload)))
            else:
               ranges, truncated = tottag_format.decode_range_results(from_hex(payload))
               recorder.add_ranges(at_ms, ranges, truncated)
         recorded.append({'uid': stream['uid'], 'label': stream['label'], 'summary': recorder.summary(), 'truncated': recorder.truncated})
      self.assertSame(keys(recorded, camel), live['expected']['summaries'], 'live summaries')
      result = radio_check.analyse_radio([{key: device[key] for key in ('uid', 'label', 'summary')} for device in recorded],
                                         positions_from_fixture(live['positions']))
      self.assertSame(keys(result, camel), live['expected']['analysis'], 'live analysis')

   def test_radio_statistics_decode_to_the_same_counters(self):
      sample = self.fixture['live']['decodedStats']
      decoded = tottag_format.decode_radio_stats(from_hex(sample['payload']))
      expected = dict(sample['stats'])
      # Python names roles as the log reader does ('participant'), the web tool by the firmware enum (ROLE_PARTICIPANT)
      self.assertEqual(decoded.pop('role'), expected.pop('role').removeprefix('ROLE_').lower().replace('_', ' '))
      self.assertSame(keys(decoded, camel), expected, 'radio statistics')

   def test_the_commands_are_byte_for_byte_the_web_tool_s(self):
      start = self.fixture['commands']['start']
      command = tottag_format.encode_radio_test_start(start['startTime'], start['endTime'], [from_hex(eui) for eui in start['euis']])
      self.assertEqual(command.hex(), start['expected'])
      self.assertEqual(tottag_format.encode_radio_test_stop().hex(), self.fixture['commands']['stop']['expected'])


class Codec(unittest.TestCase):

   def test_a_start_command_the_firmware_would_refuse_is_refused_here_first(self):
      eui = bytes([1, 2, 3, 4, 5, 6])
      with self.assertRaisesRegex(ValueError, 'must end after it starts'):
         tottag_format.encode_radio_test_start(100, 100, [eui])
      with self.assertRaisesRegex(ValueError, 'at most'):
         tottag_format.encode_radio_test_start(100, 101 + tottag_format.RADIO_TEST_MAX_SECONDS, [eui])
      with self.assertRaisesRegex(ValueError, 'between 1 and'):
         tottag_format.encode_radio_test_start(100, 200, [])
      with self.assertRaisesRegex(ValueError, '6 bytes'):
         tottag_format.encode_radio_test_start(100, 200, [b'\x01\x02'])

   def test_radio_statistics_from_another_layout_or_cut_short_are_refused(self):
      data = bytearray(tottag_format.RADIO_STATS_STRUCT.size)
      data[0] = tottag_format.RADIO_STATS_VERSION + 1
      with self.assertRaisesRegex(ValueError, 'layout'):
         tottag_format.decode_radio_stats(bytes(data))
      with self.assertRaisesRegex(ValueError, 'bytes'):
         tottag_format.decode_radio_stats(bytes(10))

   def test_the_address_is_read_from_the_system_id_around_its_filler(self):
      self.assertEqual(tottag_format.eui_from_system_id(bytes([0x3e, 1, 2, 0xfe, 0xff, 3, 4, 5])), bytes([0x3e, 1, 2, 3, 4, 5]))


class Session(unittest.TestCase):
   """The parts of a live test that do not need a device: who is in it, and what a refusal looks like."""

   def test_a_test_needs_two_devices_and_adds_each_once(self):
      test = radio_check.LiveRadioTest()
      test.add('TotTag 02', 'AA:BB', eui=bytes([2, 1, 1, 1, 1, 1]))
      test.add('TotTag 02', 'AA:BB', eui=bytes([2, 1, 1, 1, 1, 1]))
      self.assertEqual(len(test.state()['devices']), 1)
      asyncio.run(test.start(300))
      self.assertEqual(test.state()['phase'], 'setup', 'a single device was started on a test with nobody to range to')

   def test_a_device_that_cannot_be_reached_is_marked_failed_and_the_others_still_start(self):
      test = radio_check.LiveRadioTest()
      reached = []

      async def connect(device, timeout=None, on_disconnect=None):
         return None if device['name'] == 'TotTag 0B' else FakeClient(reached, device['name'])
      test._connect = connect
      test._follow = lambda device, command: asyncio.sleep(0)
      test.add('TotTag 02', 'AA', eui=bytes([0x02, 1, 1, 1, 1, 1]))
      test.add('TotTag 0B', 'BB', eui=bytes([0x0b, 1, 1, 1, 1, 1]))
      test.add('TotTag 3E', 'CC', eui=bytes([0x3e, 1, 1, 1, 1, 1]))
      asyncio.run(test.start(300))
      state = test.state()
      self.assertEqual(state['phase'], 'running')
      self.assertEqual([device['status'] for device in state['devices']], ['restarting', 'failed', 'restarting'])
      # Every device reached was sent its clock and then the same command naming all three
      commands = [data for name, uuid, data in reached if uuid == tottag_format.BLE_MAINTENANCE_COMMAND_UUID]
      self.assertEqual(len(commands), 2)
      self.assertEqual(commands[0], commands[1])
      self.assertEqual(commands[0][9], 3)
      self.assertEqual([uuid for name, uuid, data in reached if name == 'TotTag 02'],
                       [tottag_format.BLE_TIMESTAMP_UUID, tottag_format.BLE_MAINTENANCE_COMMAND_UUID])
      self.assertEqual([d['uid'] for d in test.deployment()], [0x02, 0x0b, 0x3e])


   def test_a_device_is_followed_through_its_restart_a_lost_device_list_and_a_dropped_connection_to_the_end(self):
      clock = FakeClock(1_791_300_000)
      running = lambda ranged: stats_payload(ranged, tottag_format.RADIO_STATS_FLAG_TEST_RUNNING)
      waiting = stats_payload(0, tottag_format.RADIO_STATS_FLAG_TEST_RUNNING | tottag_format.RADIO_STATS_FLAG_TEST_WAITING)
      device = FakeDevice(clock, [
         [],                                              # the start command
         [stats_payload(0, 0)],                           # reached before its restart: tried again shortly
         [running(20), waiting, running(40), 'drop'],     # in the test; restarts and loses its list; drops
         [running(60), 'end'],                            # reconnected, until the test is over
      ])
      test = radio_check.LiveRadioTest(now=clock)
      test._connect = lambda b, timeout=None, on_disconnect=None: device.connect(on_disconnect) if b['name'] == 'TotTag 02' else none()
      test.add('TotTag 02', 'AA', eui=bytes([0x02, 1, 1, 1, 1, 1]))
      test.add('TotTag 0B', 'BB', eui=bytes([0x0b, 1, 1, 1, 1, 1]))
      seen = []
      original = test._set
      test._set = lambda b, **changes: (seen.append(changes['status']) if 'status' in changes and b['name'] == 'TotTag 02' else None, original(b, **changes))

      async def run():
         await test.start(300)
         await asyncio.gather(*test.tasks)
      with quick_session():
         asyncio.run(run())

      state = test.state()
      self.assertEqual(state['phase'], 'finished')
      self.assertEqual([b['status'] for b in state['devices']], ['finished', 'failed'])
      self.assertEqual(seen, ['starting', 'restarting', 'restarting', 'waiting', 'reconnecting'])
      self.assertEqual(state['devices'][0]['ranged_recent'], 1.0, 'the share carries across the reconnection')
      start = tottag_format.encode_radio_test_start(test.start_time, test.end_time, [bytes([0x02, 1, 1, 1, 1, 1]), bytes([0x0b, 1, 1, 1, 1, 1])])
      self.assertEqual(device.writes, [tottag_format.BLE_TIMESTAMP_UUID, tottag_format.BLE_MAINTENANCE_COMMAND_UUID,
                                      tottag_format.BLE_MAINTENANCE_COMMAND_UUID])
      self.assertEqual(device.commands, [start, start], 'the lost list is resent as the same test')
      summary = test.deployment()[0]['summary']
      self.assertEqual(summary['diagnostics']['samples'], 3)
      self.assertEqual([peer['uid'] for peer in summary['peers']], [0x0b])


   def test_a_device_whose_firmware_cannot_run_a_test_is_refused_without_being_sent_it(self):
      # Older firmware accepts the start command as an unknown one and carries on as it was, so it is never sent it
      test = radio_check.LiveRadioTest()
      reached = []
      async def connect(device, timeout=None, on_disconnect=None):
         return FakeClient(reached, device['name'], has_radio_stats=device['name'] != 'TotTag 49')
      test._connect = connect
      test._follow = lambda device, command: asyncio.sleep(0)
      test.add('TotTag 02', 'AA', eui=bytes([0x02, 1, 1, 1, 1, 1]))
      test.add('TotTag 49', 'BB', eui=bytes([0x49, 1, 1, 1, 1, 1]))
      test.add('TotTag 3E', 'CC', eui=bytes([0x3e, 1, 1, 1, 1, 1]))
      asyncio.run(test.start(300))
      devices = test.state()['devices']
      self.assertEqual([b['status'] for b in devices], ['restarting', 'failed', 'restarting'])
      self.assertEqual(devices[1]['message'], radio_check.TOO_OLD)
      self.assertNotIn('TotTag 49', [name for name, uuid, data in reached])

   def test_a_device_never_heard_from_after_its_restart_says_so_while_it_is_still_tried(self):
      clock = FakeClock(1_791_300_000)
      test = radio_check.LiveRadioTest(now=clock)
      attempts = []
      async def attach(device, command):
         attempts.append(clock.now)
         clock.now += 20
         return 'unreachable'
      test._attach = attach
      test.add('TotTag 49', 'BB', eui=bytes([0x49, 1, 1, 1, 1, 1]))
      test.start_time, test.end_time, test.phase = clock.now, clock.now + 60, 'running'
      test.devices[0].update(status='restarting')
      with quick_session():
         asyncio.run(test._follow(test.devices[0], b''))
      self.assertEqual(len(attempts), 3, 'it was tried until the test ended')
      self.assertEqual(test.state()['devices'][0]['message'], radio_check.NOT_HEARD)


class RecentShare(unittest.TestCase):
   """The share of recent rounds a device ranged in, which has to read 100% for a device ranging in every one."""

   def test_a_device_ranging_every_round_reads_all_of_them_wherever_the_reads_land_in_a_round(self):
      for offset in (0.0, 0.1, 0.26, 0.49):
         # Reads every 5.05 s, each counting the rounds finished by then: 10 or 11 between reads, never 2 a second
         history = [(t, math.floor((t + offset) * 2), math.floor((t + offset) * 2)) for t in [i * 5.05 for i in range(13)]]
         self.assertEqual(radio_check.ranged_share(history), 1.0, offset)

   def test_rounds_a_device_sat_out_count_against_it(self):
      history = [(t, int(t), int(t)) for t in range(0, 61, 5)]   # in only every other round
      self.assertAlmostEqual(radio_check.ranged_share(history), 0.5, places=2)

   def test_rounds_it_was_in_but_ranged_nobody_count_against_it(self):
      history = [(t, 2 * t, int(1.8 * t)) for t in range(0, 61, 5)]
      self.assertAlmostEqual(radio_check.ranged_share(history), 0.9, places=2)

   def test_too_little_history_means_nothing_yet(self):
      self.assertIsNone(radio_check.ranged_share([(0, 0, 0), (10, 20, 20)]))
      self.assertIsNone(radio_check.ranged_share([(0, 0, 0)]))


async def none():
   return None


class quick_session:
   """No waiting between a session's steps, for the length of a test."""
   NAMES = ('RESTART_SETTLE_S', 'RECONNECT_S', 'POLL_S')

   def __enter__(self):
      self.saved = {name: getattr(radio_check, name) for name in self.NAMES}
      for name in self.NAMES:
         setattr(radio_check, name, 0.001)

   def __exit__(self, *_):
      for name, value in self.saved.items():
         setattr(radio_check, name, value)


class FakeClock:
   def __init__(self, now):
      self.now = now

   def __call__(self):
      return self.now


def stats_payload(rounds_ranged, flags):
   return tottag_format.RADIO_STATS_STRUCT.pack(tottag_format.RADIO_STATS_VERSION, 12, 2, flags, 0, rounds_ranged, rounds_ranged,
                                                rounds_ranged * 6, 0, *[rounds_ranged * 2] * 3, *[0] * 3, 0, 0, 0, 1900, 0)


class FakeDevice:
   """A device's side of a live test, scripted one connection at a time; every read moves the clock on five seconds."""

   def __init__(self, clock, connections):
      self.clock, self.connections = clock, list(connections)
      self.writes, self.commands = [], []

   async def connect(self, on_disconnect):
      return FakeLiveClient(self, self.connections.pop(0), on_disconnect) if self.connections else None


class FakeLiveClient:
   def __init__(self, device, reads, on_disconnect):
      self.device, self.reads, self.on_disconnect, self.notify = device, list(reads), on_disconnect, None

   async def start_notify(self, uuid, callback):
      self.notify = callback

   async def read_gatt_char(self, uuid):
      self.device.clock.now += 5
      item = self.reads.pop(0)
      if item == 'drop':
         self.on_disconnect(self)
         raise IOError('disconnected')
      if item == 'end':
         self.device.clock.now += 3600
         raise IOError('gone')
      if self.notify:
         self.notify(None, bytearray([1, 0x0b, 0xdc, 0x05]))
      return bytearray(item)

   async def write_gatt_char(self, uuid, data, response=False):
      self.device.writes.append(uuid)
      if uuid == tottag_format.BLE_MAINTENANCE_COMMAND_UUID:
         self.device.commands.append(bytes(data))

   async def disconnect(self):
      pass


class FakeClient:
   def __init__(self, log, name, has_radio_stats=True):
      self.log, self.name = log, name
      self.services = FakeServices(has_radio_stats)

   async def write_gatt_char(self, uuid, data, response=False):
      self.log.append((self.name, uuid, bytes(data)))

   async def disconnect(self):
      pass


class FakeServices:
   def __init__(self, has_radio_stats):
      self.has_radio_stats = has_radio_stats

   def get_characteristic(self, uuid):
      return object() if self.has_radio_stats or uuid != tottag_format.BLE_RADIO_STATS_UUID else None


if __name__ == '__main__':
   unittest.main()
