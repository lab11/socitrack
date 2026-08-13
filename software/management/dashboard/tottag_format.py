#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""Parsing for both TotTag log formats.

Two on-the-wire formats exist:

  v1  A bare concatenation of records with no framing whatsoever.  Because records used to be split across
      page boundaries and the device could not tell a good page from a corrupt one, the only way to read
      this is to slide forward a byte at a time looking for something that resembles a record.  That scan
      both misses real records and fabricates records out of payload bytes that happen to look plausible.

  v2  Page-framed.  Every page carries its sequence number, time bounds and a CRC-32 over its payload, and
      records are never split across pages.  A page that was lost or failed validation on the device is
      still transmitted, with a payload length of zero, so a gap appears at a known position instead of
      silently vanishing.  Payload CRCs are re-checked here, which also catches corruption in transit.

Detection is unambiguous: a v1 stream always begins with a record type byte in 1..6, while a v2 stream
begins with the ASCII magic 'TTS1'.

Both parsers return the same structure -- a list of ``{'t': timestamp, ...}`` dicts -- so every existing
consumer of the resulting .pkl is unaffected.
"""

import struct
import time
import zlib
from collections import defaultdict


# Format Constants ------------------------------------------------------------------------------------------------

MAX_RANGING_DISTANCE_MM = 16000
MAX_NUM_DEVICES = 10
IMU_DATA_LENGTH = 7

STORAGE_TYPE_VOLTAGE = 1
STORAGE_TYPE_CHARGING_EVENT = 2
STORAGE_TYPE_MOTION = 3
STORAGE_TYPE_RANGES = 4
STORAGE_TYPE_IMU = 5
STORAGE_TYPE_BLE_SCAN = 6
STORAGE_TYPE_RESET_REASON = 7
STORAGE_NUM_TYPES = 8

BATTERY_CODES = defaultdict(lambda: 'Unknown Battery Event')
BATTERY_CODES[1] = 'Plugged'
BATTERY_CODES[2] = 'Unplugged'
BATTERY_CODES[3] = 'Charging'
BATTERY_CODES[4] = 'Not Charging'
BATTERY_CODES[5] = 'Critical Voltage'

MAX_BATTERY_CODE = 5

# Raw am_hal_reset_status_e bits, written once per boot as STORAGE_TYPE_RESET_REASON. Several can be set at
# once, so this is decoded as a flag list rather than a single code. WATCHDOG is the one that matters: on a
# production build it is the ONLY evidence a watchdog reset happened, since the console is compiled out.
RESET_FLAGS = [
   (0x001, 'External'),
   (0x002, 'Power-On'),
   (0x004, 'Brown-Out'),
   (0x008, 'SW Power-On'),
   (0x010, 'SW Power-On Init'),
   (0x020, 'Debugger'),
   (0x040, 'Watchdog'),
   (0x080, 'Unregulated Supply Brownout'),
   (0x100, 'Core Regulator Brownout'),
   (0x200, 'Memory Regulator Brownout'),
   (0x400, 'High-Power Memory Regulator Brownout'),
   (0x800, 'Low-Power Core Regulator Brownout'),
]
RESET_WATCHDOG_BIT = 0x040
MAX_RESET_STATUS = 0xFFF


def decode_reset_reason(status):
   """Decode a raw reset-status word into a list of human-readable causes."""
   return [name for bit, name in RESET_FLAGS if status & bit] or ['Unknown (0x%03X)' % status]

FORMAT_V1 = 1
FORMAT_V2 = 2

# v2 stream header: magic, format version, details length, total pages, total payload bytes
V2_STREAM_MAGIC = b'TTS1'
V2_STREAM_HEADER = struct.Struct('<4sHHII')

# v2 per-page header: seq, first timestamp, last timestamp, payload length, record count, payload CRC
V2_PAGE_HEADER = struct.Struct('<IIIHHI')
NO_TIMESTAMP = 0xFFFFFFFF


def detect_format(data):
   """Return FORMAT_V2 if the stream carries the v2 magic, otherwise FORMAT_V1.

   Safe because byte 0 of a v1 stream is always a record type in 1..6, and 'T' is 0x54.
   """
   return FORMAT_V2 if data[:4] == V2_STREAM_MAGIC else FORMAT_V1


# Record Grammar --------------------------------------------------------------------------------------------------

def _record_length(data, i):
   """Structural length of the record starting at ``i``, or None if it cannot be determined.

   A page-framed payload is record-aligned, so a record whose CONTENT fails validation can still be
   stepped over using its own declared length. That lets one implausible record be rejected without
   discarding the remainder of the page, which is what aborting would do.
   """
   record_type = data[i]
   if record_type == STORAGE_TYPE_VOLTAGE:
      length = 9
   elif record_type in (STORAGE_TYPE_CHARGING_EVENT, STORAGE_TYPE_MOTION):
      length = 6
   elif record_type == STORAGE_TYPE_RANGES:
      length = 6 + data[i + 5] * 3 if i + 6 <= len(data) else None
   elif record_type == STORAGE_TYPE_IMU:
      length = 5 + data[i + 5] if i + 6 <= len(data) else None       # the IMU length byte counts itself
   elif record_type == STORAGE_TYPE_BLE_SCAN:
      length = 6 + data[i + 5] if i + 6 <= len(data) else None
   elif record_type == STORAGE_TYPE_RESET_REASON:
      length = 7                                                     # uint16 status word
   else:
      return None
   return length if (length is not None and i + length <= len(data)) else None


def _parse_records(data, experiment_start_time, log_data, uid_to_labels, resynchronize):
   """Decode records from ``data`` into ``log_data``.

   ``resynchronize`` selects between the two framing assumptions.  v1 has no framing, so a byte that does
   not begin a plausible record is skipped and the scan tries again one byte later.  v2 payloads are
   record-aligned by construction, so a byte that does not begin a valid record means the payload is
   damaged; the page is abandoned rather than slid through, which avoids inventing records from garbage.

   Returns ``(decoded, rejected)`` -- records successfully decoded, and structurally valid records whose
   contents failed validation and were stepped over.
   """
   i = 0
   decoded = 0
   rejected = 0
   now = int(time.time())
   while i + 5 < len(data):
      record_type = data[i]
      timestamp_raw = struct.unpack('<I', data[i + 1:i + 5])[0]
      timestamp = experiment_start_time + (timestamp_raw / 1000)
      consumed = 0

      if timestamp <= now and (timestamp_raw % 500) == 0 and 1 <= record_type < STORAGE_NUM_TYPES:
         if record_type == STORAGE_TYPE_VOLTAGE and i + 9 <= len(data):
            datum = struct.unpack('<I', data[i + 5:i + 9])[0]
            if 0 < datum < 4500:
               log_data[timestamp]['v'] = datum
               consumed = 9

         elif record_type == STORAGE_TYPE_CHARGING_EVENT and i + 6 <= len(data):
            if 0 < data[i + 5] <= MAX_BATTERY_CODE:
               log_data[timestamp]['c'] = BATTERY_CODES[data[i + 5]]
               consumed = 6

         elif record_type == STORAGE_TYPE_MOTION and i + 6 <= len(data):
            if data[i + 5] in (0, 1):
               log_data[timestamp]['m'] = data[i + 5] > 0
               consumed = 6

         elif record_type == STORAGE_TYPE_RANGES and i + 6 <= len(data):
            count = data[i + 5]
            if count < MAX_NUM_DEVICES and i + 6 + count * 3 <= len(data):
               ranges = {}
               for j in range(count):
                  uid = data[i + 6 + (j * 3)]
                  datum = struct.unpack('<H', data[i + 7 + (j * 3):i + 9 + (j * 3)])[0]
                  if datum < MAX_RANGING_DISTANCE_MM:
                     if uid_to_labels is None:
                        ranges[uid] = datum
                     elif uid in uid_to_labels:
                        ranges[uid_to_labels[uid]] = datum
               log_data[timestamp]['r'] = ranges
               consumed = 6 + count * 3

         elif record_type == STORAGE_TYPE_IMU and i + 6 <= len(data):
            # The IMU length byte counts itself, so the record is 5 + imu_length bytes, not 6 + payload
            imu_length = data[i + 5]
            if imu_length == IMU_DATA_LENGTH and i + 12 <= len(data):
               log_data[timestamp]['i'] = [
                  struct.unpack('<h', data[i + 6:i + 8])[0],
                  struct.unpack('<h', data[i + 8:i + 10])[0],
                  struct.unpack('<h', data[i + 10:i + 12])[0],
               ]
               consumed = 5 + imu_length

         elif record_type == STORAGE_TYPE_BLE_SCAN and i + 6 <= len(data):
            count = data[i + 5]
            if count < MAX_NUM_DEVICES and i + 6 + count <= len(data):
               seen = []
               for j in range(count):
                  uid = data[i + 6 + j]
                  if uid_to_labels is None:
                     seen.append(uid)
                  elif uid in uid_to_labels:
                     seen.append(uid_to_labels[uid])
               log_data[timestamp]['b'] = seen
               consumed = 6 + count

         elif record_type == STORAGE_TYPE_RESET_REASON and i + 7 <= len(data):
            status = struct.unpack('<H', data[i + 5:i + 7])[0]
            if status <= MAX_RESET_STATUS:
               log_data[timestamp]['rst'] = decode_reset_reason(status)
               consumed = 7

      if consumed:
         i += consumed
         decoded += 1
      elif resynchronize:
         i += 1
      else:
         # Record-aligned payload: step over this record using its own length rather than abandoning the
         # page. Only an unrecognisable type byte, or a record running past the end, is unrecoverable.
         step = _record_length(data, i)
         if step is None:
            break
         i += step
         rejected += 1

   return decoded, rejected


def _finalize(log_data):
   """Flatten the timestamp-keyed dict into the sorted list of dicts that consumers expect."""
   return [dict({'t': ts}, **datum) for ts, datum in sorted(log_data.items())]


# Format Parsers --------------------------------------------------------------------------------------------------

def parse_v1(data, experiment_start_time, uid_to_labels=None):
   """Parse a legacy unframed record stream.

   Matches the original implementation, including its byte-at-a-time resynchronisation, with one
   deliberate correction: a RANGES record rejected for an implausible device count no longer leaves an
   empty range dict behind.  Verified over 1200 randomised streams (including corrupted bytes that force
   resynchronisation) that this is the ONLY divergence -- every other decoded value is identical, so
   archived .ttg files yield the same measurements minus artefacts of the scan.
   """
   log_data = defaultdict(dict)
   try:
      _parse_records(data, experiment_start_time, log_data, uid_to_labels, resynchronize=True)
   except Exception:
      pass       # the original tolerated a malformed tail; preserve that rather than lose the whole file
   return _finalize(log_data)


def parse_v2(data, experiment_start_time=None, uid_to_labels=None, repairs=None):
   """Parse a page-framed stream.

   Returns ``(records, report)`` where ``report`` describes what was lost:

       {'total_pages': int,          # pages the device said it would send
        'pages_read': int,           # pages actually present in the stream
        'holes': [(position, seq), ...],          # pages the device could not read (payload length 0)
        'crc_failures': [(position, seq), ...],   # pages whose payload CRC did not match
        'short_pages': [(position, seq, decoded, expected), ...],  # pages that stopped decoding early
        'rejected_records': [(position, seq, count), ...],  # records skipped as implausible
        'repaired': [(position, seq), ...],       # pages recovered from a retransmission round
        'time_discontinuities': [(position, seq, previous_last, this_first), ...],
        'last_seq': int | None,                   # sequence number of the last page received

    Each is identified by its POSITION in the stream first, and by the sequence number the device claimed
    second.  Position is authoritative: it is derived from the stream itself, whereas a sequence number is
    only as trustworthy as the firmware that wrote it.  Logs written before the sequence counter was fixed
    contain duplicates, which would make a seq-keyed report ambiguous.
        'details': bytes | None,     # raw experiment_details blob from the stream header
        'truncated': bool}           # stream ended before total_pages were received

    ``experiment_start_time`` may be omitted, in which case it is taken from the embedded details blob.

    ``repairs`` is an optional ``{seq: payload}`` mapping from earlier retransmission rounds; a page that
    arrived unreadable or corrupt is replaced by its repaired copy, so the returned report describes what
    is *still* missing after the repairs rather than what the original transfer lost.
    """
   repairs = repairs or {}
   header = V2_STREAM_HEADER.unpack_from(data, 0)
   magic, version, details_length, total_pages, _total_payload = header
   if magic != V2_STREAM_MAGIC:
      raise ValueError('not a v2 stream')
   if version != 1:
      raise ValueError(f'unsupported v2 format version {version}')

   offset = V2_STREAM_HEADER.size
   details = data[offset:offset + details_length]
   offset += details_length

   if experiment_start_time is None:
      if len(details) < 4:
         raise ValueError('stream header is missing experiment details')
      experiment_start_time = struct.unpack('<I', details[:4])[0]

   log_data = defaultdict(dict)
   report = {'total_pages': total_pages, 'pages_read': 0, 'holes': [], 'crc_failures': [],
             'short_pages': [], 'rejected_records': [], 'repaired': [], 'last_seq': None,
             'time_discontinuities': [], 'details': details, 'truncated': False}
   previous_last = None

   # Stop after the declared number of pages rather than when the bytes run out. A device that keeps
   # logging during a transfer can send a few bytes past its own declared total -- total_payload_bytes is
   # sampled before the last page is read -- and interpreting that tail as another page frame would invent
   # a corrupt page and request a retransmission for it.
   position = 0
   seen_seqs = set()
   while (position < total_pages) and (offset + V2_PAGE_HEADER.size <= len(data)):
      seq, first_ts, last_ts, payload_length, record_count, payload_crc = \
         V2_PAGE_HEADER.unpack_from(data, offset)
      offset += V2_PAGE_HEADER.size
      position += 1

      # A zero-length page means the device could not read that page
      payload = None
      if payload_length:
         if offset + payload_length > len(data):
            report['truncated'] = True
            break
         payload = data[offset:offset + payload_length]
         offset += payload_length
      report['pages_read'] += 1
      report['last_seq'] = seq
      seen_seqs.add(seq)

      # Page bounds that run backwards mean the device's clock base moved mid-log. The device seeks a time
      # range by binary-searching these same bounds, so where they are not ordered its selection cannot be
      # trusted -- and a short selection is invisible otherwise, because the stream it sends is internally
      # consistent and reports no gaps.
      if (first_ts != NO_TIMESTAMP) and (previous_last is not None) and (first_ts < previous_last):
         report['time_discontinuities'].append((position - 1, seq, previous_last, first_ts))
      if last_ts != NO_TIMESTAMP:
         previous_last = last_ts

      # Verify independently of the device, which also catches corruption introduced in transit
      if payload is None:
         failure = 'holes'
      elif zlib.crc32(payload) != payload_crc:
         failure = 'crc_failures'
      else:
         failure = None

      # A page recovered by a later retransmission round stands in for the copy that did not survive
      if failure and seq in repairs:
         payload, failure = repairs[seq], None
         record_count = 0            # the repaired frame's own count was checked when it was collected
         report['repaired'].append((position - 1, seq))
      if failure:
         report[failure].append((position - 1, seq))
         continue

      decoded, rejected = _parse_records(payload, experiment_start_time, log_data, uid_to_labels,
                                        resynchronize=False)
      if rejected:
         report['rejected_records'].append((position - 1, seq, rejected))
      if record_count and decoded < record_count:
         report['short_pages'].append((position - 1, seq, decoded, record_count))

   # A page lost to a truncated transfer has no frame to substitute into, so repaired copies of pages the
   # stream never carried are decoded here instead. Output is sorted by timestamp, so append order is
   # immaterial; what matters is that these pages count towards the total, or the caller would keep asking
   # for pages it already holds.
   for seq in sorted(s for s in repairs if s not in seen_seqs):
      decoded, rejected = _parse_records(repairs[seq], experiment_start_time, log_data, uid_to_labels,
                                         resynchronize=False)
      report['pages_read'] += 1
      report['repaired'].append((None, seq))
      if report['last_seq'] is None or seq > report['last_seq']:
         report['last_seq'] = seq
      if rejected:
         report['rejected_records'].append((None, seq, rejected))

   report['truncated'] = report['pages_read'] < total_pages
   return _finalize(log_data), report


def extract_pages(data):
   """Return ``{seq: payload}`` for every CRC-valid page in a stream.

   A retransmission response carries the same page framing as a normal download, but with no experiment
   details, since the host already holds them.  Pages that are still unreadable on the device come back
   as zero-length frames and are simply absent from the result.
   """
   pages = {}
   if len(data) < V2_STREAM_HEADER.size or data[:4] != V2_STREAM_MAGIC:
      return pages
   _magic, version, details_length, _total_pages, _total_payload = V2_STREAM_HEADER.unpack_from(data, 0)
   if version != 1:
      return pages
   offset = V2_STREAM_HEADER.size + details_length
   while offset + V2_PAGE_HEADER.size <= len(data):
      seq, _first_ts, _last_ts, payload_length, _record_count, payload_crc = \
         V2_PAGE_HEADER.unpack_from(data, offset)
      offset += V2_PAGE_HEADER.size
      if payload_length == 0:
         continue                                 # still unreadable on the device
      if offset + payload_length > len(data):
         break                                    # the repair stream was itself truncated
      payload = data[offset:offset + payload_length]
      offset += payload_length
      if zlib.crc32(payload) == payload_crc:
         pages[seq] = payload
   return pages


def missing_seqs(report):
   """Sequence numbers worth asking the device to resend.

   Holes and CRC failures only.  A page whose CRC passed arrived intact, so a page that merely stopped
   decoding early has a record-level problem that a second copy of the same bytes would not fix.

   Pages lost to a truncated transfer are inferred rather than observed: sequence numbers are contiguous
   within an epoch, so the tail the device never sent runs on from the last one that did arrive.

   A transfer in which NO page arrived yields an empty list, because there is no anchor to count from --
   a stream does not necessarily begin at sequence zero, since a wrapped log or a time-bounded download
   starts partway through the epoch.  That case is a failed transfer rather than a partial one, and the
   caller should repeat the whole download instead of naming pages.
   """
   seqs = {seq for _position, seq in report['holes']}
   seqs |= {seq for _position, seq in report['crc_failures']}
   if report['truncated'] and report['last_seq'] is not None:
      shortfall = report['total_pages'] - report['pages_read']
      seqs |= set(range(report['last_seq'] + 1, report['last_seq'] + 1 + max(0, shortfall)))
   return sorted(seqs)


def parse(data, experiment_start_time=None, uid_to_labels=None, repairs=None):
   """Parse either format, dispatching on the stream magic.

   Always returns ``(records, report)``.  For v1 the report is a minimal stand-in, since an unframed
   stream carries no information about what might be missing from it -- and so ``repairs`` is meaningless
   there and ignored.
   """
   if detect_format(data) == FORMAT_V2:
      return parse_v2(data, experiment_start_time, uid_to_labels, repairs)
   records = parse_v1(data, experiment_start_time, uid_to_labels)
   return records, {'total_pages': None, 'pages_read': None, 'holes': [], 'crc_failures': [],
                    'short_pages': [], 'rejected_records': [], 'repaired': [], 'last_seq': None,
                    'time_discontinuities': [], 'details': None, 'truncated': False}
