#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""Deployment records: what was scheduled, on which TotTags, and in which time zone.

The browser tool writes one whenever it schedules a study, and the desktop dashboard does too, in the same format:
the DeploymentManifest in managementweb/packages/tottag-schema/src/manifest.ts, field for field, so a record from
either tool serves both. It is the only place the study's time zone is kept -- neither the TotTags nor their logs
store it -- so the analysis helpers find the record that matches a log to show its times on the study's clock.

A log matches a record when the deployment details in its header are byte for byte the ones the record says were
written. Failing that, it matches when its deployment started at the record's start time and the TotTag that wrote
it is one of the record's devices. The browser tool matches by the same rule.
"""

# PYTHON INCLUSIONS ---------------------------------------------------------------------------------------------------

try: from . import tottag_format
except ImportError: import tottag_format
import datetime, json, os, re, struct, time, unicodedata
import pytz


# CONSTANTS AND DEFINITIONS -------------------------------------------------------------------------------------------

MANIFEST_KIND = 'tottag.deployment-manifest'
MANIFEST_VERSION = 2
DAY_SECONDS = 86400
LABEL_LENGTH = 16

# experiment_details_t, 239 bytes: as written to a TotTag (after the command byte) and as a log's header carries it
DETAILS = struct.Struct('<IIIIBB' + ('6B' * tottag_format.MAX_NUM_DEVICES) + (f'{LABEL_LENGTH}s' * tottag_format.MAX_NUM_DEVICES) + 'B')

# The outcomes that leave a TotTag holding the deployment
WRITTEN = ('confirmed', 'unconfirmed')

# A record is a few kilobytes; anything far larger in a folder being searched is something else
MAX_RECORD_BYTES = 1 << 20

LOG_NAME = re.compile(r'^(.*)_(\d+)$')
WEEKDAYS = ('Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun')
MONTHS = ('Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec')


# TIME HELPERS --------------------------------------------------------------------------------------------------------

def _local(unix_seconds, timezone):
   zone = pytz.timezone(timezone) if isinstance(timezone, str) else timezone
   return datetime.datetime.fromtimestamp(unix_seconds, pytz.utc).astimezone(zone)

def utc_offset(timezone, unix_seconds):
   """How far `timezone` is ahead of UTC at that instant, in seconds, rounded to the minute as the browser tool does."""
   return round(_local(unix_seconds, timezone).utcoffset().total_seconds() / 60) * 60

def wall_clock(unix_seconds, timezone):
   """The 'YYYY-MM-DDTHH:MM' reading of an instant on a clock in `timezone`."""
   return _local(unix_seconds, timezone).strftime('%Y-%m-%dT%H:%M')

def time_of_day(seconds):
   """'HH:MM' for seconds past midnight, wrapped into one day."""
   wrapped = round(seconds) % DAY_SECONDS
   return f'{wrapped // 3600:02d}:{wrapped % 3600 // 60:02d}'

def format_date_time(unix_seconds, timezone):
   """'Sat, Oct 10, 2026, 7:00 AM', as the browser tool writes a time in a record's summary."""
   local = _local(unix_seconds, timezone)
   return (f'{WEEKDAYS[local.weekday()]}, {MONTHS[local.month - 1]} {local.day}, {local.year}, '
           f'{local.hour % 12 or 12}:{local.minute:02d} {"AM" if local.hour < 12 else "PM"}')

def clock_changes(timezone, start, end):
   """Each instant strictly inside [start, end) when the clocks in `timezone` change, found as the browser tool finds them."""
   if end <= start:
      return []
   changes, previous_at, previous, at = [], start, utc_offset(timezone, start), start
   while True:
      at += 3600
      probe = min(at, end - 1)
      offset = utc_offset(timezone, probe)
      if offset != previous:
         low, high = previous_at, probe
         while high - low > 60:
            middle = (low + high) // 2
            if utc_offset(timezone, middle) == previous:
               low = middle
            else:
               high = middle
         changes.append(high)
         previous = offset
      previous_at = probe
      if probe >= end - 1:
         return changes

def _daily_hours(details, timezone, at):
   """The local hours a TotTag with these details records at, as of `at`; None when it records around the clock."""
   if not details['use_daily_times']:
      return None
   offset = utc_offset(timezone, at)
   return {'start': time_of_day(details['daily_start_time'] + offset), 'end': time_of_day(details['daily_end_time'] + offset)}


# DEVICE HELPERS ------------------------------------------------------------------------------------------------------

def unpack_details(blob):
   """The fields of an experiment_details_t, or None when `blob` is too short to be one."""
   if not blob or len(blob) < DETAILS.size:
      return None
   fields = DETAILS.unpack_from(blob)
   count = tottag_format.MAX_NUM_DEVICES
   return {
      'start_time': fields[0],
      'end_time': fields[1],
      'daily_start_time': fields[2],
      'daily_end_time': fields[3],
      'use_daily_times': fields[4],
      'num_devices': min(fields[5], count),
      'uids': [list(fields[6 + 6 * i:12 + 6 * i]) for i in range(count)],
      'labels': [label.rstrip(b'\x00').decode('utf-8', errors='replace') for label in fields[6 + 6 * count:6 + 7 * count]],
      'terminated': fields[-1],
   }

def format_eui(uid):
   """'C0:98:E5:42:01:05' for an ID stored least-significant byte first."""
   return ':'.join(f'{b:02X}' for b in reversed(uid))

UNSAFE_IN_FILE_NAMES = re.compile(r'[\\/:*?"<>|\x00-\x1f\x7f]')

def file_label(label):
   # Drops only what a file name cannot hold; must match fileLabel() in the browser tool's deployment.ts
   return UNSAFE_IN_FILE_NAMES.sub('', label).strip(' .')[:LABEL_LENGTH]


# WRITING A RECORD ----------------------------------------------------------------------------------------------------

def build_record(details_bytes, timezone, results, written_at=None, name=None):
   """The record of a deployment as written, in the browser tool's format.

   `details_bytes` is the experiment_details_t written to each TotTag, `timezone` the study's IANA zone, and `results`
   one {'uid', 'label', 'outcome', 'detail'} per TotTag written to: its ID least-significant byte first, the label it
   was given, and an outcome of 'confirmed', 'mismatch', 'unconfirmed', 'failed', or 'skipped'.
   """
   details = unpack_details(details_bytes)
   start, end = details['start_time'], details['end_time']
   stamp = (written_at or datetime.datetime.now(datetime.timezone.utc)).astimezone(datetime.timezone.utc)
   count = lambda *outcomes: sum(1 for result in results if result['outcome'] in outcomes)
   tags = lambda number: f'{number} tag{"" if number == 1 else "s"}'
   confirmed, failed, unconfirmed, skipped = count('confirmed'), count('failed', 'mismatch'), count('unconfirmed'), count('skipped')
   parts = [f'{confirmed} of {tags(len(results))} confirmed the deployment.']
   if failed:
      parts.append(f'{tags(failed)} did not take it.')
   if unconfirmed:
      parts.append(f'{tags(unconfirmed)} {"was" if unconfirmed == 1 else "were"} written but could not be read back to check.')
   if skipped:
      parts.append(f'{tags(skipped)} {"was" if skipped == 1 else "were"} left alone because {"it" if skipped == 1 else "they"} '
                   'still held an earlier deployment.')
   parts.append(f'Runs {format_date_time(start, timezone)} to {format_date_time(end, timezone)} ({timezone}).')
   days = round((end - start) / DAY_SECONDS, 4)
   return {
      'kind': MANIFEST_KIND,
      'manifestVersion': MANIFEST_VERSION,
      'writtenAt': stamp.strftime('%Y-%m-%dT%H:%M:%S.') + f'{stamp.microsecond // 1000:03d}Z',
      'name': (name or '').strip() or None,
      'deployment': {
         'timezone': timezone,
         'start': wall_clock(start, timezone),
         'end': wall_clock(end, timezone),
         'startTime': start,
         'endTime': end,
         'durationDays': int(days) if days == int(days) else days,
         'dailyHours': _daily_hours(details, timezone, start),
         'afterClockChange': [{'from': wall_clock(at, timezone), 'dailyHours': _daily_hours(details, timezone, at)}
                              for at in clock_changes(timezone, start, end)],
      },
      'devices': [{
         'name': f'TotTag {result["uid"][1]:02X}:{result["uid"][0]:02X}',
         'eui': format_eui(result['uid']),
         'shortUid': f'0x{result["uid"][0]:02X}',
         'label': result['label'],
         'outcome': result['outcome'],
         'detail': result.get('detail'),
      } for result in results],
      'experimentDetailsHex': bytes(details_bytes).hex(),
      'summary': ' '.join(parts),
   }

def record_filename(labels, timezone, name=None, now=None):
   """The name the browser tool gives a record by default: the study's name or its labels' shared prefix, and the day."""
   def safe(text):
      text = ''.join(c for c in unicodedata.normalize('NFD', text) if not '̀' <= c <= 'ͯ')
      return re.sub(r'[^A-Za-z0-9._-]+', '-', text).strip('-.')
   base = safe(name or '')
   labels = [label.strip() for label in labels if label.strip()]
   if not base and labels:
      prefix = labels[0]
      for label in labels[1:]:
         while not label.startswith(prefix):
            prefix = prefix[:-1]
      shared = safe(re.sub(r'[_\s-]+$', '', prefix))
      base = shared if len(shared) >= 2 else ''
   return f'{base or "tottag-deployment"}-{wall_clock(time.time() if now is None else now, timezone)[:10]}.json'

def save_record(record, path):
   """Write a record as the browser tool does: indented JSON, UTF-8."""
   with open(path, 'w', encoding='utf-8') as file:
      json.dump(record, file, indent=2, ensure_ascii=False)


# FINDING A LOG'S RECORD ----------------------------------------------------------------------------------------------

def load_records(*paths):
   """Every deployment record in the given files and folders, searching a folder's own .json files but not its subfolders.

   A file named outright that is not a record is an error; anything else in a folder that is not one is passed over.
   """
   records = []
   for path in paths:
      path = os.path.expanduser(os.fspath(path))
      named = not os.path.isdir(path)
      files = [path] if named else [os.path.join(path, f) for f in sorted(os.listdir(path)) if f.lower().endswith('.json')]
      for file in files:
         try:
            if not named and os.path.getsize(file) > MAX_RECORD_BYTES:
               continue
            with open(file, encoding='utf-8') as handle:
               record = json.load(handle)
            usable = isinstance(record, dict) and record.get('kind') == MANIFEST_KIND and \
                     isinstance(record.get('deployment'), dict) and isinstance(record['deployment'].get('timezone'), str)
         except (OSError, ValueError):
            if named:
               raise
            continue
         if usable:
            records.append(record)
         elif named:
            raise ValueError(f'{file} is not a TotTag deployment record')
   return records

def read_details(path):
   """The deployment details in a .ttg's header, or None for a log that carries none."""
   with open(path, 'rb') as file:
      header = file.read(tottag_format.V2_STREAM_HEADER.size)
      if len(header) < tottag_format.V2_STREAM_HEADER.size or header[:4] != tottag_format.V2_STREAM_MAGIC:
         return None
      length = tottag_format.V2_STREAM_HEADER.unpack(header)[2]
      details = file.read(length)
   return details if length and len(details) == length else None

def companion_ttg(pkl_path, first_timestamp=None):
   """The .ttg a download saved beside a .pkl: `<label>_<start>.ttg` for `<label>.pkl`.

   Each download replaces the .pkl but keeps every .ttg, so where there are several the one whose deployment started
   last before the .pkl's first record is the one it came from, or else the newest.
   """
   folder = os.path.dirname(os.path.abspath(pkl_path))
   label = os.path.splitext(os.path.basename(pkl_path))[0]
   candidates = []
   for name in os.listdir(folder):
      named = LOG_NAME.match(name[:-4]) if name.lower().endswith('.ttg') else None
      if named and named.group(1) == label:
         candidates.append((int(named.group(2)), os.path.join(folder, name)))
   started = [candidate for candidate in candidates if first_timestamp is not None and candidate[0] <= first_timestamp]
   if started:
      return max(started)[1]
   return max(candidates, key=lambda candidate: os.path.getmtime(candidate[1]))[1] if candidates else None

def log_identity(path, first_timestamp=None):
   """What a log says about the deployment it came from: {'details', 'start_time', 'eui', 'label'}, None where it cannot say.

   A .ttg's header carries the deployment details its TotTag held, and its name the TotTag's label and the deployment's
   start time. A .pkl carries neither, so for one the .ttg saved beside it is read instead.
   """
   path = os.fspath(path)
   if path.lower().endswith('.pkl'):
      ttg = companion_ttg(path, first_timestamp)
      if ttg is None:
         return {'details': None, 'start_time': None, 'eui': None, 'label': os.path.splitext(os.path.basename(path))[0]}
      path = ttg
   stem = os.path.splitext(os.path.basename(path))[0]
   named = LOG_NAME.match(stem)
   label, start_time = (named.group(1), int(named.group(2))) if named else (stem, None)
   details = read_details(path)
   unpacked, eui = unpack_details(details), None
   if unpacked:
      start_time = unpacked['start_time']
      for uid, device_label in list(zip(unpacked['uids'], unpacked['labels']))[:unpacked['num_devices']]:
         name = device_label or str(uid[0])
         if label in (name, file_label(name) or str(uid[0])):
            eui = format_eui(uid)
   return {'details': details, 'start_time': start_time, 'eui': eui, 'label': label}

def match_record(records, details=None, start_time=None, eui=None, label=None):
   """The record a log belongs to, or None: the newest whose details are the log's byte for byte, or else the newest that
   started when the log's deployment did and lists the TotTag that wrote it.

   Where the log cannot name that TotTag's ID (a legacy log carries no details), its label stands in for it.
   """
   newest = lambda found: max(found, key=lambda record: str(record.get('writtenAt', ''))) if found else None
   exact = [record for record in records if details and str(record.get('experimentDetailsHex', '')).lower() == bytes(details).hex()]
   if exact or start_time is None:
      return newest(exact)
   def lists_device(record):
      for device in record.get('devices', []):
         if eui is not None and str(device.get('eui', '')).upper() == eui.upper():
            return True
         if eui is None and label is not None:
            short_uid = str(int(device['shortUid'], 16)) if re.fullmatch(r'0x[0-9A-Fa-f]{1,2}', str(device.get('shortUid', ''))) else None
            if label in (device.get('label'), file_label(device.get('label') or ''), short_uid) and label:
               return True
      return False
   return newest([record for record in records if record['deployment'].get('startTime') == start_time and lists_device(record)])

def find_record(log_path, records=None, first_timestamp=None):
   """The deployment record a .pkl or .ttg belongs to, or None.

   `records` is a record file, a folder of them, or records already loaded; by default the log's own folder is searched.
   """
   if records is None:
      records = load_records(os.path.dirname(os.path.abspath(log_path)))
   elif isinstance(records, (str, os.PathLike)):
      records = load_records(records)
   elif isinstance(records, dict):
      records = [records]
   return match_record(records, **log_identity(log_path, first_timestamp))
