#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""The live radio check: badges ranging in a radio test, read over Bluetooth while it runs.

A radio test is started by a maintenance command naming every badge in it. Each badge restarts into the test, ranges
with the others for the time asked, logs nothing, and restarts back to normal when it ends. While it runs, each badge
notifies its ranges every round and answers reads of its radio counters.

Three layers, the first two pure so they can be tested without Bluetooth:
   - analyse_radio() judges every badge against the others, a line-for-line port of analyseRadio() in the web tool's
     schema package (radioCheck.ts). test_radio_check.py holds both to the same verdicts on a shared fixture.
   - LiveRadioRecorder reduces what one badge streams to the summary analyse_radio() reads, as LiveRadioRecorder in
     liveRadio.ts does.
   - LiveRadioTest runs a test with bleak on an asyncio event loop, following each badge through its restart into the
     test and any dropped connection after.
"""

import asyncio, collections, math, threading, time

try: from . import tottag_format
except ImportError: import tottag_format


# THRESHOLDS ---------------------------------------------------------------------------------------------------------
#
# Comparisons are against the MEDIAN OF THE OTHER BADGES, so that in a three-badge test one bad badge cannot drag the
# yardstick towards itself. Absolute floors catch a fleet that is bad as a whole. Keep in step with radioCheck.ts.

MINUTE_MS = 60_000
SCHEDULING_INTERVAL_US = 500_000
ROUNDS_PER_MINUTE = 60_000_000 // SCHEDULING_INTERVAL_US

PARTICIPATION_FAIL = 0.5
PARTICIPATION_CHECK = 0.85
RX_FAILURE_CHECK_EXCESS = 0.08
RX_FAILURE_FAIL_EXCESS = 0.2
ANTENNA_CHECK_SPREAD = 0.1
ANTENNA_FAIL_SPREAD = 0.25
ANTENNA_MIN_SAMPLES = 200
COVERAGE_CHECK_DROP = 0.15
COVERAGE_FAIL = 0.5
BIAS_CHECK_MM = 120
BIAS_FAIL_MM = 300
NOISE_CHECK_FACTOR = 2
NOISE_CHECK_MIN_MM = 50
ARM_LATE_CHECK = 0.01

# Most a minute's ranges are scaled up for notifications Bluetooth dropped; past this, the minute is not trusted
MAX_NOTIFICATION_SCALE = 3


def _js_round(value):
   # JavaScript's Math.round, which the web tool uses: halves go up, where Python's round() goes to even
   return math.floor(value + 0.5)

def _median(values):
   ordered = sorted(values)
   mid = len(ordered) >> 1
   return ordered[mid] if len(ordered) % 2 else (ordered[mid - 1] + ordered[mid]) / 2

def _percent(fraction):
   return f'{_js_round(fraction * 100)}%'

def format_percent(fraction):
   """A fraction as the web tool shows it, or a dash for none."""
   return '—' if fraction is None else _percent(fraction)

def format_mm(millimetres):
   return '—' if millimetres is None else f'{_js_round(millimetres)} mm'


# CROSS-BADGE ANALYSIS -----------------------------------------------------------------------------------------------

def _solve_biases(uids, residuals):
   """Least-squares per-badge offsets from link residuals: residual(a, b) ~ bias(a) + bias(b)."""
   index = {uid: i for i, uid in enumerate(uids)}
   n = len(uids)
   if n < 3 or len(residuals) < n:
      return None
   matrix = [[0.0] * (n + 1) for _ in range(n)]
   for a, b, residual in residuals:
      i, j = index[a], index[b]
      matrix[i][i] += 1; matrix[j][j] += 1
      matrix[i][j] += 1; matrix[j][i] += 1
      matrix[i][n] += residual; matrix[j][n] += residual
   # Gaussian elimination with partial pivoting; a near-zero pivot means the links cannot separate the badges' offsets
   for col in range(n):
      pivot = col
      for row in range(col + 1, n):
         if abs(matrix[row][col]) > abs(matrix[pivot][col]):
            pivot = row
      if abs(matrix[pivot][col]) < 1e-9:
         return None
      matrix[col], matrix[pivot] = matrix[pivot], matrix[col]
      for row in range(n):
         if row == col:
            continue
         factor = matrix[row][col] / matrix[col][col]
         for k in range(col, n + 1):
            matrix[row][k] -= factor * matrix[col][k]
   return {uid: matrix[i][n] / matrix[i][i] for i, uid in enumerate(uids)}


def analyse_radio(devices, positions=None):
   """Judge every badge against the others.

   ``devices`` is a list of {'uid', 'label', 'summary'}, the summary as LiveRadioRecorder.summary() makes it or None
   for a badge with no data. ``positions`` maps a badge's uid to its (x, y) in metres, for whichever badges have one.
   """
   positions = positions or {}
   notes = []
   is_loaded = lambda device: device['summary'] is not None and device['summary']['first_ms'] is not None and device['summary']['last_ms'] is not None
   loaded = [device for device in devices if is_loaded(device)]
   missing = len(devices) - len(loaded)
   if missing:
      notes.append(f"{missing} of the {len(devices)} selected badges {'has' if missing == 1 else 'have'} no log loaded, so "
                   f"{'it is' if missing == 1 else 'they are'} not judged and {'its' if missing == 1 else 'their'} links are missing from the others.")

   # The stretch every badge was on, in whole minutes, so no badge is penalised for rounds it was off for
   start = max(math.ceil(d['summary']['first_ms'] / MINUTE_MS) for d in loaded) if loaded else None
   end = min(math.floor(d['summary']['last_ms'] / MINUTE_MS) for d in loaded) if loaded else None
   minutes = max(0, end - start) if start is not None and end is not None else 0
   rounds = minutes * ROUNDS_PER_MINUTE
   if loaded and minutes < 2:
      notes.append('The badges were running together for less than two whole minutes, which is too short to judge ranging. Run the test for at least five.')
   if loaded and len(loaded) < 3:
      notes.append('With fewer than three badges there is no "rest of the fleet" to compare against, so only absolute limits apply, and distance offsets cannot be pinned to a single badge.')
   in_window = lambda minute: start is not None and end is not None and start <= minute < end

   # Per-link evidence from both ends
   coverage, link_minutes = {}, {}
   pair = lambda a, b: (a, b) if a < b else (b, a)
   for device in loaded:
      for peer in device['summary']['peers']:
         within = [m for m in peer['minutes'] if in_window(m[0])]
         coverage[(device['uid'], peer['uid'])] = (sum(m[1] for m in within) / rounds) if rounds else 0
         link_minutes.setdefault(pair(device['uid'], peer['uid']), []).extend(within)

   links = []
   uids = [device['uid'] for device in devices]
   for i in range(len(uids)):
      for j in range(i + 1, len(uids)):
         a, b = uids[i], uids[j]
         evidence = link_minutes.get(pair(a, b), [])
         median_mm = _median([m[2] for m in evidence]) if evidence else None
         # Median absolute deviation scaled to a standard deviation, which ignores the occasional wild range
         noise_mm = _median([m[3] for m in evidence]) * 1.4826 if evidence else None
         pa, pb = positions.get(a), positions.get(b)
         true_mm = math.hypot(pa[0] - pb[0], pa[1] - pb[1]) * 1000 if pa is not None and pb is not None else None
         links.append({
            'a': a, 'b': b,
            'coverage_a_to_b': coverage.get((a, b), 0),
            'coverage_b_to_a': coverage.get((b, a), 0),
            'median_mm': median_mm, 'noise_mm': noise_mm, 'true_mm': true_mm,
            'residual_mm': median_mm - true_mm if median_mm is not None and true_mm is not None else None,
         })

   with_residuals = [link for link in links if link['residual_mm'] is not None]
   biases = None
   if with_residuals:
      solved_uids = list(dict.fromkeys(uid for link in with_residuals for uid in (link['a'], link['b'])))
      biases = _solve_biases(solved_uids, [(link['a'], link['b'], link['residual_mm']) for link in with_residuals])
   if positions and not biases:
      notes.append("The positions entered do not let each badge's distance offset be told apart — it needs at least three badges with positions and ranges between them.")
   if not positions:
      notes.append('No positions entered, so distances are checked for consistency only. Enter where each badge sat to check accuracy as well.')

   # Each badge's own figures
   figures = {}
   for device in devices:
      summary = device['summary']
      loaded_here = is_loaded(device)
      rows = sum(m[1] for m in summary['rows_by_minute'] if in_window(m[0])) if loaded_here else 0
      diagnostics = summary['diagnostics'] if summary else None
      rx_total = diagnostics['rx_ok'] + diagnostics['rx_failed'] if diagnostics else 0
      antenna_rates = []
      if diagnostics:
         for antenna, ok in enumerate(diagnostics['rx_ok_by_antenna']):
            total = ok + diagnostics['rx_failed_by_antenna'][antenna]
            antenna_rates.append(diagnostics['rx_failed_by_antenna'][antenna] / total if total >= ANTENNA_MIN_SAMPLES else None)
      mine = [link for link in links if device['uid'] in (link['a'], link['b'])]
      my_coverage = [link['coverage_a_to_b'] if link['a'] == device['uid'] else link['coverage_b_to_a'] for link in mine]
      my_noise = [link['noise_mm'] for link in mine if link['noise_mm'] is not None]
      figures[device['uid']] = {
         'loaded': loaded_here,
         'participation': rows / rounds if loaded_here and rounds else None,
         'rx_failure_rate': diagnostics['rx_failed'] / rx_total if rx_total else None,
         'antenna_failure_rates': antenna_rates,
         'link_coverage': _median(my_coverage) if loaded_here and my_coverage else None,
         'noise_mm': _median(my_noise) if my_noise else None,
      }

   def others_median(uid, key):
      values = [f[key] for other, f in figures.items() if other != uid and f['loaded'] and f[key] is not None]
      return _median(values) if values else None

   results = []
   for device in devices:
      f = figures[device['uid']]
      diagnostics = device['summary']['diagnostics'] if device['summary'] else None
      if not f['loaded']:
         results.append({
            'uid': device['uid'], 'label': device['label'], 'verdict': 'missing', 'reasons': ['No log loaded for this badge.'],
            'participation': None, 'rx_failure_rate': None, 'antenna_failure_rates': [], 'link_coverage': None,
            'bias_mm': None, 'noise_mm': None, 'diagnostics': None, 'aborts': 0,
         })
         continue
      fails, checks = [], []

      if f['participation'] is not None and rounds:
         if f['participation'] < PARTICIPATION_FAIL:
            fails.append(f"Ranged in only {_percent(f['participation'])} of rounds. It was not taking part in the network for most of the test.")
         elif f['participation'] < PARTICIPATION_CHECK:
            checks.append(f"Ranged in {_percent(f['participation'])} of rounds, where a healthy network manages about 99%.")

      rx_others = others_median(device['uid'], 'rx_failure_rate')
      if f['rx_failure_rate'] is not None:
         excess = f['rx_failure_rate'] - (rx_others if rx_others is not None else 0)
         versus = f' against {_percent(rx_others)} for the other badges' if rx_others is not None else ''
         if excess > RX_FAILURE_FAIL_EXCESS:
            fails.append(f"{_percent(f['rx_failure_rate'])} of its ranging receives failed{versus}. A weak receiver or a damaged antenna or connection.")
         elif excess > RX_FAILURE_CHECK_EXCESS:
            checks.append(f"{_percent(f['rx_failure_rate'])} of its ranging receives failed{versus}.")

      rates = [(antenna, rate) for antenna, rate in enumerate(f['antenna_failure_rates']) if rate is not None]
      if len(rates) >= 2:
         worst, best = rates[0], rates[0]
         for entry in rates[1:]:
            if entry[1] > worst[1]: worst = entry
            if entry[1] < best[1]: best = entry
         spread = worst[1] - best[1]
         others = ' and '.join(_percent(rate) for antenna, rate in rates if antenna != worst[0])
         message = f'Antenna {worst[0] + 1} failed {_percent(worst[1])} of the receives through it, against {others} on the others.'
         if spread > ANTENNA_FAIL_SPREAD:
            fails.append(f'{message} That antenna, its switch, or its connection is suspect.')
         elif spread > ANTENNA_CHECK_SPREAD:
            checks.append(message)

      coverage_others = others_median(device['uid'], 'link_coverage')
      if f['link_coverage'] is not None and rounds:
         if f['link_coverage'] < COVERAGE_FAIL:
            fails.append(f"Ranged to a typical peer in only {_percent(f['link_coverage'])} of rounds.")
         elif coverage_others is not None and f['link_coverage'] < coverage_others - COVERAGE_CHECK_DROP:
            checks.append(f"Ranged to a typical peer in {_percent(f['link_coverage'])} of rounds, against {_percent(coverage_others)} for the other badges.")

      bias_mm = biases.get(device['uid']) if biases else None
      if bias_mm is not None:
         message = f"Reads about {_js_round(abs(bias_mm))} mm {'long' if bias_mm > 0 else 'short'} on every link. Its antenna delay calibration is off, or its antenna is damaged."
         if abs(bias_mm) > BIAS_FAIL_MM:
            fails.append(message)
         elif abs(bias_mm) > BIAS_CHECK_MM:
            checks.append(message)

      noise_others = others_median(device['uid'], 'noise_mm')
      if f['noise_mm'] is not None and noise_others is not None and f['noise_mm'] > NOISE_CHECK_MIN_MM and f['noise_mm'] > NOISE_CHECK_FACTOR * noise_others:
         checks.append(f"Its distances spread by about {_js_round(f['noise_mm'])} mm, against {_js_round(noise_others)} mm for the other badges.")

      if diagnostics:
         recoveries = diagnostics['wake_failures'] + diagnostics['irq_stuck']
         if recoveries:
            checks.append(f"The radio needed resetting {recoveries} time{'' if recoveries == 1 else 's'} ({diagnostics['wake_failures']} failed "
                          f"wake-up{'' if diagnostics['wake_failures'] == 1 else 's'}, {diagnostics['irq_stuck']} stuck interrupt{'' if diagnostics['irq_stuck'] == 1 else 's'}).")
         if rounds and diagnostics['rx_arm_late'] / rounds > ARM_LATE_CHECK:
            checks.append(f"Lost {diagnostics['rx_arm_late']} rounds to a receive it could not start in time. That points to firmware timing, not this badge's radio.")

      results.append({
         'uid': device['uid'], 'label': device['label'],
         'verdict': 'fail' if fails else ('check' if checks else 'pass'),
         'reasons': fails + checks,
         'participation': f['participation'], 'rx_failure_rate': f['rx_failure_rate'], 'antenna_failure_rates': f['antenna_failure_rates'],
         'link_coverage': f['link_coverage'], 'bias_mm': bias_mm, 'noise_mm': f['noise_mm'], 'diagnostics': diagnostics,
         'aborts': device['summary']['aborts'],
      })

   return {'window_start_minute': start, 'window_end_minute': end, 'devices': results, 'links': links, 'notes': notes}


def _millimetre(value):
   return _js_round(value * 1000) / 1000

def circle_positions(uids, radius_m):
   """Evenly around a circle of the given radius, starting due east, in metres as the web tool lays them out."""
   return {uid: (_millimetre(radius_m * math.cos(2 * math.pi * i / len(uids))), _millimetre(radius_m * math.sin(2 * math.pi * i / len(uids))))
           for i, uid in enumerate(uids)}

def line_positions(uids, spacing_m):
   """In a straight line at the given spacing, in metres."""
   return {uid: (_millimetre(i * spacing_m), 0.0) for i, uid in enumerate(uids)}


# RECORDING ONE BADGE ------------------------------------------------------------------------------------------------

class LiveRadioRecorder:
   """Everything one badge has streamed during a live test, reduced to a summary on request.

   Rounds are counted from the badge's own counter wherever it is available, because a notification Bluetooth dropped
   is a round the badge still ranged in; ranges per peer come from the notifications, scaled minute by minute for the
   ones that went missing. Times passed in are Unix milliseconds.
   """

   def __init__(self, start_time, deployment_uids, deployment_labels, self_uid):
      self.start_time = start_time
      self.start_ms = start_time * 1000
      self.deployment_uids = list(deployment_uids)
      self.deployment_labels = list(deployment_labels)
      self.self_uid = self_uid
      self.first_ms = self.last_ms = None
      self.notified_rounds, self.counted_rounds, self.peer_values = {}, {}, {}
      self.previous = None
      self.banked = {'rx_ok': 0, 'rx_failed': 0, 'wake_failures': 0, 'rx_arm_late': 0, 'tx_late': 0}
      self.banked_ok = [0] * tottag_format.NUM_XMIT_ANTENNAS
      self.banked_failed = [0] * tottag_format.NUM_XMIT_ANTENNAS
      self.samples = 0
      self.truncated = 0

   def _touch(self, at_ms):
      at = max(0, at_ms - self.start_ms)
      self.first_ms = at if self.first_ms is None else min(self.first_ms, at)
      self.last_ms = at if self.last_ms is None else max(self.last_ms, at)
      return at

   def add_ranges(self, at_ms, ranges, truncated=False):
      """One round's ranges, as notified."""
      at = self._touch(at_ms)
      if truncated:
         self.truncated += 1
      if not ranges:
         return
      minute = math.floor(at / MINUTE_MS)
      self.notified_rounds[minute] = self.notified_rounds.get(minute, 0) + 1
      for uid, millimetres in ranges.items():
         self.peer_values.setdefault(uid, {}).setdefault(minute, []).append(millimetres)

   def add_stats(self, at_ms, stats):
      """One read of the radio counters, which are cumulative since the badge booted into the test."""
      at = self._touch(at_ms)
      last = self.previous[1] if self.previous else None
      # A badge that restarted mid-test counts from zero again, so keep what it had reached
      restarted = last is not None and (stats['rx_ok'] < last['rx_ok'] or stats['rounds_ranged'] < last['rounds_ranged'])
      if restarted:
         for key in self.banked:
            self.banked[key] += last[key]
         for antenna in range(tottag_format.NUM_XMIT_ANTENNAS):
            self.banked_ok[antenna] += last['rx_ok_by_antenna'][antenna]
            self.banked_failed[antenna] += last['rx_failed_by_antenna'][antenna]
      # Rounds since the last read, spread over the time between the two reads
      from_ms = self.previous[0] if self.previous else 0
      rounds = stats['rounds_ranged'] - (last['rounds_ranged'] if last is not None and not restarted else 0)
      self._spread(rounds, from_ms, at)
      self.previous = (at, stats)
      self.samples += 1

   def _spread(self, rounds, from_ms, to_ms):
      if rounds <= 0:
         return
      if to_ms <= from_ms:
         minute = math.floor(to_ms / MINUTE_MS)
         self.counted_rounds[minute] = self.counted_rounds.get(minute, 0) + rounds
         return
      minute = math.floor(from_ms / MINUTE_MS)
      while minute * MINUTE_MS < to_ms:
         overlap = min(to_ms, (minute + 1) * MINUTE_MS) - max(from_ms, minute * MINUTE_MS)
         if overlap > 0:
            self.counted_rounds[minute] = self.counted_rounds.get(minute, 0) + rounds * (overlap / (to_ms - from_ms))
         minute += 1

   @property
   def latest_stats(self):
      return self.previous[1] if self.previous else None

   def summary(self):
      counted = self.samples > 0
      rows = sorted((minute, _js_round(value)) for minute, value in (self.counted_rounds if counted else self.notified_rounds).items())
      rows = [entry for entry in rows if entry[1] > 0]

      def scale(minute):
         notified = self.notified_rounds.get(minute, 0)
         return min(MAX_NOTIFICATION_SCALE, max(1, self.counted_rounds.get(minute, 0) / notified)) if counted and notified else 1

      peers = []
      for uid, minutes in self.peer_values.items():
         entries = []
         for minute in sorted(minutes):
            values = minutes[minute]
            centre = _median(values)
            entries.append((minute, _js_round(len(values) * scale(minute)), centre, _median([abs(value - centre) for value in values])))
         peers.append({'uid': uid, 'minutes': entries})

      latest = self.latest_stats
      diagnostics = None
      if latest:
         diagnostics = {
            'rx_ok': self.banked['rx_ok'] + latest['rx_ok'],
            'rx_failed': self.banked['rx_failed'] + latest['rx_failed'],
            'rx_ok_by_antenna': [count + self.banked_ok[i] for i, count in enumerate(latest['rx_ok_by_antenna'])],
            'rx_failed_by_antenna': [count + self.banked_failed[i] for i, count in enumerate(latest['rx_failed_by_antenna'])],
            'wake_failures': self.banked['wake_failures'] + latest['wake_failures'],
            'irq_stuck': 0,
            'rx_arm_late': self.banked['rx_arm_late'] + latest['rx_arm_late'],
            'tx_late': self.banked['tx_late'] + latest['tx_late'],
            'samples': self.samples,
         }
      return {
         'experiment_start_time': self.start_time,
         'deployment_uids': self.deployment_uids,
         'deployment_labels': self.deployment_labels,
         'self_uid': self.self_uid,
         'first_ms': self.first_ms,
         'last_ms': self.last_ms,
         'rows_by_minute': rows,
         'peers': peers,
         'diagnostics': diagnostics,
         'aborts': 0,
      }


# RUNNING A TEST OVER BLUETOOTH --------------------------------------------------------------------------------------

STATUS_LABELS = {
   'ready': 'Ready', 'starting': 'Starting', 'restarting': 'Restarting', 'waiting': 'Resending list',
   'running': 'Testing', 'reconnecting': 'Reconnecting', 'finished': 'Finished', 'failed': 'Failed',
}

RESTART_SETTLE_S = 2.5      # a badge restarting into the test is unreachable for a couple of seconds
START_GRACE_S = 30          # a badge still outside the test this long after the start is taken to have refused it
POLL_S = 5
RECONNECT_S = 2
CONNECT_TIMEOUT_S = 8
RECENT_S = 60               # how far back the share of rounds a badge is ranging in looks
RECENT_MIN_S = 15           # and how much history it needs before it means anything

TOO_OLD = "Its firmware is too old to run a radio test. Update it, then try again."
NOT_HEARD = 'Not heard from since it restarted into the test. Still trying to reach it.'


def ranged_share(history):
   """Share of the recent rounds in which a badge produced ranges, from its own counters read over time.

   ``history`` is ``[(seconds, rounds_scheduled, rounds_ranged), ...]``, oldest first, all from one boot. Dividing rounds
   by the time between two reads is no good: a read lands anywhere within a round, so a window of a few seconds holds a
   whole round more or less than its length suggests, and a perfect badge reads anywhere from 1.8 to 2.2 rounds a
   second. The badge's count of the rounds it took part in has no such slop, so that is the yardstick whenever it
   agrees with the clock to within a round; when it falls further short, the badge sat out rounds and the clock decides.
   """
   if len(history) < 2:
      return None
   (t0, scheduled0, ranged0), (t1, scheduled1, ranged1) = history[0], history[-1]
   if t1 - t0 < RECENT_MIN_S:
      return None
   by_clock = (t1 - t0) * 1_000_000 / SCHEDULING_INTERVAL_US
   scheduled = scheduled1 - scheduled0
   expected = scheduled if abs(scheduled - by_clock) <= 1.5 else max(1, _js_round(by_clock))
   return min(1.0, max(0.0, (ranged1 - ranged0) / expected)) if expected > 0 else None


class LiveRadioTest:
   """One live radio test among the badges added to it, run as tasks on an asyncio event loop.

   Everything that touches Bluetooth runs on that loop; the GUI thread reads state() and deployment(), which take a
   lock, and calls start() and stop() through asyncio.run_coroutine_threadsafe().
   """

   def __init__(self, now=time.time):
      self.now = now
      self.lock = threading.Lock()
      self.badges = []
      self.phase = 'setup'
      self.start_time = self.end_time = None
      self.tasks = []

   def add(self, name, ble_device, eui=None, label=None):
      """Add a badge found by the scan. ``eui`` (6 bytes, low byte first) is read from the badge when not given."""
      with self.lock:
         if self.phase != 'setup' or any(badge['name'] == name for badge in self.badges):
            return
         self.badges.append({'name': name, 'device': ble_device, 'eui': bytes(eui) if eui else None,
                             'label': label, 'status': 'ready', 'message': None, 'stats': None, 'ranged_recent': None,
                             'previous': None, 'history': collections.deque(), 'recorder': None})

   def state(self):
      with self.lock:
         return {
            'phase': self.phase, 'start_time': self.start_time, 'end_time': self.end_time,
            'badges': [{key: badge[key] for key in ('name', 'label', 'status', 'message', 'stats', 'ranged_recent')} |
                       {'uid': badge['eui'][0] if badge['eui'] else None, 'truncated': badge['recorder'].truncated if badge['recorder'] else 0}
                       for badge in self.badges],
         }

   def deployment(self):
      """The test as analyse_radio() takes it: every badge, with its recording standing in for a log."""
      with self.lock:
         return [{'uid': badge['eui'][0], 'label': badge['label'] or f"{badge['eui'][0]:02X}",
                  'summary': badge['recorder'].summary() if badge['recorder'] else None}
                 for badge in self.badges if badge['eui']]

   def _set(self, badge, **changes):
      with self.lock:
         badge.update(changes)

   def _over(self):
      return self.phase == 'finished' or (self.end_time is not None and self.now() >= self.end_time)

   async def _connect(self, badge, timeout=CONNECT_TIMEOUT_S, on_disconnect=None):
      from bleak import BleakClient, BleakScanner
      # A badge that has just restarted has to be seen advertising again before the stack will connect to it. Badges
      # are known by their Bluetooth address, which macOS hides unless asked, as the dashboard's own scan does
      device = await BleakScanner.find_device_by_address(getattr(badge['device'], 'address', badge['device']), timeout=timeout,
                                                         cb={'use_bdaddr': True})
      if device is None:
         return None
      client = BleakClient(device, disconnected_callback=on_disconnect)
      await client.connect(timeout=timeout)
      return client if client.is_connected else None

   @staticmethod
   def _supports_radio_test(client):
      # Firmware from before radio tests accepts the start command as an unknown one and carries on as it was, so
      # whether a badge can run one shows only in whether it has the radio statistics to read during it
      try:
         return client.services.get_characteristic(tottag_format.BLE_RADIO_STATS_UUID) is not None
      except Exception:
         return True   # cannot tell from here; the badge's own answers decide

   async def start(self, duration_s):
      """Start every badge on the same test, then follow each one into it."""
      with self.lock:
         if self.phase != 'setup' or len(self.badges) < 2:
            return
         self.phase = 'starting'
      for badge in self.badges:
         if badge['eui'] is None:
            try:
               client = await self._connect(badge)
               if client:
                  self._set(badge, eui=tottag_format.eui_from_system_id(await client.read_gatt_char(tottag_format.BLE_SYSTEM_ID_UUID)))
                  await client.disconnect()
            except Exception:
               pass
            if badge['eui'] is None:
               self._set(badge, status='failed', message='Could not be reached to read its address.')
      euis = [badge['eui'] for badge in self.badges if badge['eui']]
      with self.lock:
         self.start_time = int(self.now())
         self.end_time = self.start_time + int(duration_s)
      command = tottag_format.encode_radio_test_start(self.start_time, self.end_time, euis)
      uids = [eui[0] for eui in euis]
      labels = [next(b['label'] or f'{b["eui"][0]:02X}' for b in self.badges if b['eui'] == eui) for eui in euis]

      for badge in self.badges:
         if self.phase != 'starting':
            return   # stopped while the others were being started
         if badge['eui'] is None:
            continue
         self._set(badge, status='starting')
         try:
            client = await self._connect(badge)
            if client is None:
               raise IOError('Could not be reached.')
            if not self._supports_radio_test(client):
               try: await client.disconnect()
               except Exception: pass
               raise IOError(TOO_OLD)
            try:
               # Clock first, so every badge agrees on when the test began
               await client.write_gatt_char(tottag_format.BLE_TIMESTAMP_UUID, int(self.now()).to_bytes(4, 'little'), True)
               await client.write_gatt_char(tottag_format.BLE_MAINTENANCE_COMMAND_UUID, command, True)
            except Exception:
               raise IOError('The badge refused the test. Its clock may not be set, or its firmware may be too old to run one.')
            finally:
               try: await client.disconnect()
               except Exception: pass
         except Exception as error:
            self._set(badge, status='failed', message=str(error) or 'Could not be started.')
            continue
         self._set(badge, status='restarting', recorder=LiveRadioRecorder(self.start_time, uids, labels, badge['eui'][0]))
         self.tasks.append(asyncio.ensure_future(self._follow(badge, command)))
      with self.lock:
         if self.phase == 'starting':
            self.phase = 'running' if any(badge['status'] != 'failed' for badge in self.badges) else 'finished'

   async def stop(self):
      """End the test early: each badge restarts into whatever it would otherwise be doing."""
      with self.lock:
         if self.phase not in ('starting', 'running'):
            return
         self.phase = 'finished'
      for task in self.tasks:
         task.cancel()
      stop = tottag_format.encode_radio_test_stop()
      for badge in self.badges:
         if badge['status'] == 'failed' or badge['eui'] is None:
            continue
         try:
            client = await self._connect(badge, timeout=4)
            if client:
               await client.write_gatt_char(tottag_format.BLE_MAINTENANCE_COMMAND_UUID, stop, True)
               await client.disconnect()
         except Exception:
            pass   # restarting anyway at its scheduled end, or out of reach
         self._set(badge, status='finished')

   async def _follow(self, badge, command):
      """Keep one badge connected for the rest of the test: reconnect after its restart and after any drop."""
      try:
         await asyncio.sleep(RESTART_SETTLE_S)
         while not self._over():
            if await self._attach(badge, command) == 'failed':
               return
            if badge['previous'] is None and self.now() >= self.start_time + START_GRACE_S:
               self._set(badge, message=NOT_HEARD)
            await asyncio.sleep(RECONNECT_S)
      finally:
         with self.lock:
            if badge['status'] != 'failed':
               badge['status'] = 'finished'
            if self.phase == 'running' and all(b['status'] in ('finished', 'failed') for b in self.badges):
               self.phase = 'finished'

   async def _attach(self, badge, command):
      """One connection to a badge in the test, polled until it drops or the test ends."""
      dropped = asyncio.Event()
      loop = asyncio.get_running_loop()
      client = None
      try:
         client = await self._connect(badge, on_disconnect=lambda _client: loop.call_soon_threadsafe(dropped.set))
         if client is None:
            return 'unreachable'
         if not self._supports_radio_test(client):
            self._set(badge, status='failed', message=TOO_OLD)
            return 'failed'
         recorder = badge['recorder']
         def on_ranges(_sender, data):
            ranges, truncated = tottag_format.decode_range_results(data)
            with self.lock:
               recorder.add_ranges(self.now() * 1000, ranges, truncated)
         try:
            await client.start_notify(tottag_format.BLE_RANGES_UUID, on_ranges)
         except Exception:
            pass   # rounds still count from the badge's own counter; only distances go missing
         while not self._over() and not dropped.is_set():
            try:
               stats = tottag_format.decode_radio_stats(await client.read_gatt_char(tottag_format.BLE_RADIO_STATS_UUID))
            except Exception:
               stats = None   # a read that fails on a live connection is retried at the next poll; a dropped one reconnects
            now = self.now()
            if stats is None:
               pass
            elif stats['test_waiting']:
               # Restarted without its badge list: send the same test again, which it takes without restarting
               self._set(badge, status='waiting')
               try: await client.write_gatt_char(tottag_format.BLE_MAINTENANCE_COMMAND_UUID, command, True)
               except Exception: pass
            elif not stats['test_running']:
               # Reached before its restart into the test, which a badge flushing a deployment's log can be slow to make
               if badge['previous'] is None and now < self.start_time + START_GRACE_S:
                  return 'early'
               self._set(badge, status='failed', message='This badge is not running the test. It may have restarted out of it, or refused it.')
               return 'failed'
            else:
               with self.lock:
                  history = badge['history']
                  if history and (stats['rounds_scheduled'] < history[-1][1] or stats['rounds_ranged'] < history[-1][2]):
                     history.clear()   # restarted, so its counters began again from zero
                  history.append((now, stats['rounds_scheduled'], stats['rounds_ranged']))
                  while len(history) > 2 and now - history[1][0] >= RECENT_S:
                     history.popleft()
                  badge['ranged_recent'] = ranged_share(list(history))
                  badge['previous'] = (now, stats)
                  recorder.add_stats(now * 1000, stats)
                  badge.update(status='running', message=None, stats=stats)
            try:
               await asyncio.wait_for(dropped.wait(), POLL_S)
            except asyncio.TimeoutError:
               pass
         return 'dropped'
      except asyncio.CancelledError:
         raise
      except Exception:
         return 'dropped'
      finally:
         if client is not None:
            try: await client.disconnect()
            except Exception: pass
         if not self._over() and badge['status'] != 'failed':
            self._set(badge, status='restarting' if badge['previous'] is None else 'reconnecting')
