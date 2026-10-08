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

MAX_EXPERIMENT_ELAPSED_SECONDS = 4294966
MAX_DEPLOYMENT_DAYS = 49
MAX_DEPLOYMENT_SECONDS = 4233600
MAX_RANGING_DISTANCE_MM = 32000
MAX_NUM_DEVICES = 10
IMU_DATA_LENGTH = 7
BENIGN_TIME_STEP_MS = 2000
TIMESTAMP_TOLERANCE_MS = 250

STORAGE_TYPE_VOLTAGE = 1
STORAGE_TYPE_CHARGING_EVENT = 2
STORAGE_TYPE_MOTION = 3
STORAGE_TYPE_RANGES = 4
STORAGE_TYPE_IMU = 5
STORAGE_TYPE_BLE_SCAN = 6
STORAGE_TYPE_RESET_REASON = 7
STORAGE_TYPE_TIME_ANCHOR = 8
STORAGE_TYPE_DIAGNOSTICS = 9
STORAGE_TYPE_RADIO_ABORT = 10
STORAGE_TYPE_SCHEDULE_CATCH = 11
STORAGE_TYPE_ROUND_START = 12
STORAGE_TYPE_SESSION_END = 13
STORAGE_TYPE_RADIO_TIMING = 14
STORAGE_NUM_TYPES = 15

BATTERY_CODES = defaultdict(lambda: 'Unknown Battery Event')
BATTERY_CODES[1] = 'Plugged'
BATTERY_CODES[2] = 'Unplugged'
BATTERY_CODES[3] = 'Charging'
BATTERY_CODES[4] = 'Not Charging'
BATTERY_CODES[5] = 'Critical Voltage'

MAX_BATTERY_CODE = 5

_VERBOSE = False


def set_verbose(enabled):
   """Show diagnostics that are expected during normal operation (the --debug flag)."""
   global _VERBOSE
   _VERBOSE = bool(enabled)


def is_verbose():
   return _VERBOSE

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

# Tasks the watchdog monitors in the order watchdog_task_t declares them
WATCHDOG_TASK_NAMES = ['TimeAlignedTask', 'StorageTask', 'AppTask', 'BLETask', 'RangingTask']

# STORAGE_TYPE_DIAGNOSTICS payload: counters describing how close the firmware came to a fault without
# reaching one, then which firmware wrote the log, TempCo and the chip temperature, radio health, records lost
# to a full queue, stack headroom, and recoveries. Counters are cumulative since boot and saturate rather than
# wrap, so a reboot partitions them.
DIAGNOSTICS_NUM_POOLS = 5
DIAGNOSTICS_STRUCT = struct.Struct('<H5sHHH5s5sBIBbIIHHHHHHHH6HBH')
DIAGNOSTICS_NUM_STACKS = 6
DIAGNOSTICS_STACK_NAMES = WATCHDOG_TASK_NAMES + ['TimerService']
DIAGNOSTICS_FLAG_TEMPCO_AVAILABLE = 0x01
DIAGNOSTICS_FLAG_TEMPCO_APPLIED = 0x02
DIAGNOSTICS_FLAG_FIRMWARE_MODIFIED = 0x04
DIAGNOSTICS_FLAG_DIAGNOSTIC_BUILD = 0x08
DIAGNOSTICS_FLAG_TEMPCO_DISABLED = 0x10
DIAGNOSTICS_STACK_UNMONITORED = 0xFFFF
DIAGNOSTICS_TEMPERATURE_UNKNOWN = -128

# STORAGE_TYPE_RADIO_ABORT payload, written only by a diagnostic build: one radio receive that could not be armed
# before its slot. A ranging-phase abort costs the whole round; a status-phase one only cuts that exchange short.
RADIO_ABORT_STRUCT = struct.Struct('<BBBhHBHBHhHH')
RADIO_ABORT_PHASES = {1: 'ranging', 2: 'status'}
RADIO_ABORT_UNMEASURED = 0xFFFF
RADIO_ABORT_TRIGGERS = {0: None, 1: 'tx done', 2: 'rx frame', 3: 'rx timeout', 4: 'rx error'}
RADIO_ABORT_NO_EVENT_TIME = -32768

# STORAGE_TYPE_SCHEDULE_CATCH: a participant's timed wake-up, from its receiver opening to the first schedule copy it
# decoded. lead_us is how long before the expected round's first copy the receiver opened, negative when it opened late
SCHEDULE_CATCH_STRUCT = struct.Struct('<BBiHHBBHhhH')
SCHEDULE_CATCH_NONE = 0xFF
SCHEDULE_CATCH_UNMEASURED = 0xFFFF

# STORAGE_TYPE_ROUND_START: the master's start of each round and how it went, written as the round ends
ROUND_START_STRUCT = struct.Struct('<HHHBBBH')
ROUND_START_FLAG_SECOND_COPY_FAILED = 0x01
ROUND_START_FLAG_COMPUTED = 0x02
ROUND_START_FLAG_ABANDONED = 0x04
ROUND_START_FLAG_JOIN_HEARD = 0x08
ROUND_START_FLAG_JOIN_RELAYED = 0x10

# STORAGE_TYPE_SESSION_END: why one run of the ranging scheduler ended, and what it saw while it lasted
SESSION_END_STRUCT = struct.Struct('<BBBBBBHIHHHHHB')
SESSION_END_REASONS = {0: 'unknown', 1: 'stopped', 2: 'search timeout', 3: 'collision', 4: 'heard nobody'}
SCHEDULE_ROLES = {10: 'idle', 11: 'master', 12: 'participant', 13: 'asleep', 14: 'master ineligible'}
SCHEDULER_PHASES = {0: 'schedule', 1: 'subscription', 2: 'ranging', 3: 'status', 4: 'computation', 5: 'unscheduled',
                    6: 'ranging error', 7: 'radio error', 8: 'collision'}
PACKET_TYPES = {0x80: 'ranging', 0x81: 'schedule', 0x82: 'status', 0x83: 'join request'}

# STORAGE_TYPE_RADIO_TIMING: how close a minute of receives came to their deadlines
RADIO_TIMING_BANDS = 7
RADIO_TIMING_FIRST_US = 40
RADIO_TIMING_STEP_US = 5
RADIO_TIMING_STRUCT = struct.Struct('<HHHhHHH7HHBB')

# A live radio test, run over Bluetooth rather than read from logs
BLE_SYSTEM_ID_UUID = '00002a23-0000-1000-8000-00805f9b34fb'
BLE_TIMESTAMP_UUID = 'd68c3154-a23f-ee90-0c45-5231395e5d2e'
BLE_RANGES_UUID = 'd68c3156-a23f-ee90-0c45-5231395e5d2e'
BLE_RADIO_STATS_UUID = 'd68c3159-a23f-ee90-0c45-5231395e5d2e'
BLE_MAINTENANCE_COMMAND_UUID = 'd68c3162-a23f-ee90-0c45-5231395e5d2e'
BLE_MAINTENANCE_START_RADIO_TEST = 0x07
BLE_MAINTENANCE_STOP_RADIO_TEST = 0x08
BLE_MAINTENANCE_RADIO_TEST_HEADER_LEN = 10
RADIO_TEST_MAX_SECONDS = 3600
EUI_LEN = 6
NUM_XMIT_ANTENNAS = 3
RADIO_STATS_STRUCT = struct.Struct('<BBBBHIIII3I3IHHHHHBB')
RADIO_STATS_VERSION = 2
RADIO_STATS_FLAG_TEST_RUNNING = 0x01
RADIO_STATS_FLAG_TEST_WAITING = 0x02
SYSTEM_ID_EUI_OFFSETS = (0, 1, 2, 5, 6, 7)


def encode_radio_test_start(start_time, end_time, euis):
   """The maintenance command that starts a radio test among ``euis`` (6-byte, low byte first), which must include
   the device it is written to. Times are Unix seconds; send every device the same two."""
   if not 1 <= len(euis) <= MAX_NUM_DEVICES:
      raise ValueError(f'A radio test needs between 1 and {MAX_NUM_DEVICES} devices, not {len(euis)}.')
   if end_time <= start_time:
      raise ValueError('A radio test must end after it starts.')
   if end_time - start_time > RADIO_TEST_MAX_SECONDS:
      raise ValueError(f'A radio test can run for at most {RADIO_TEST_MAX_SECONDS // 60} minutes.')
   if any(len(eui) != EUI_LEN for eui in euis):
      raise ValueError(f'Every device address must be {EUI_LEN} bytes.')
   return struct.pack('<BIIB', BLE_MAINTENANCE_START_RADIO_TEST, int(start_time), int(end_time), len(euis)) + \
          b''.join(bytes(eui) for eui in euis)


def encode_radio_test_stop():
   """The maintenance command that ends a radio test early."""
   return bytes([BLE_MAINTENANCE_STOP_RADIO_TEST])


def decode_radio_stats(data):
   """A read of the radio statistics characteristic: the device's radio counters since it booted."""
   data = bytes(data)
   if len(data) < RADIO_STATS_STRUCT.size:
      raise ValueError(f'Radio statistics are {RADIO_STATS_STRUCT.size} bytes; this device sent {len(data)}.')
   (version, role, schedule_size, flags, seconds_left, rounds_scheduled, rounds_ranged, rx_ok, rx_failed,
    *rest) = RADIO_STATS_STRUCT.unpack_from(data)
   if version != RADIO_STATS_VERSION:
      raise ValueError(f'This device reports radio statistics in layout {version}, which this tool does not read.')
   ok_by_antenna = list(rest[:NUM_XMIT_ANTENNAS])
   failed_by_antenna = list(rest[NUM_XMIT_ANTENNAS:2 * NUM_XMIT_ANTENNAS])
   tx_late, rx_arm_late, isr_over_budget, wake_max_us, wake_failures, antenna, antenna_changes = rest[2 * NUM_XMIT_ANTENNAS:]
   return {
      'role': SCHEDULE_ROLES.get(role, str(role)),
      'schedule_size': schedule_size,
      'test_running': bool(flags & RADIO_STATS_FLAG_TEST_RUNNING),
      'test_waiting': bool(flags & RADIO_STATS_FLAG_TEST_WAITING),
      'test_seconds_left': seconds_left,
      'rounds_scheduled': rounds_scheduled,
      'rounds_ranged': rounds_ranged,
      'rx_ok': rx_ok,
      'rx_failed': rx_failed,
      'rx_ok_by_antenna': ok_by_antenna,
      'rx_failed_by_antenna': failed_by_antenna,
      'tx_late': tx_late,
      'rx_arm_late': rx_arm_late,
      'isr_over_budget': isr_over_budget,
      'wake_max_us': wake_max_us,
      'wake_failures': wake_failures,
      'antenna': antenna,
      'antenna_changes': antenna_changes,
   }


def decode_range_results(data):
   """One ranges notification: a count byte, then (u8 uid, u16 mm) pairs, filtered as a RANGES record is. Returns the
   ranges and whether the notification stopped short of its own count, as a too-small Bluetooth packet makes it."""
   data = bytes(data)
   ranges = {}
   if not data:
      return ranges, True
   count = min(data[0], MAX_NUM_DEVICES)
   decoded = 0
   while decoded < count and 1 + (decoded + 1) * 3 <= len(data):
      at = 1 + decoded * 3
      millimetres = struct.unpack_from('<H', data, at + 1)[0]
      if millimetres < MAX_RANGING_DISTANCE_MM:
         ranges[data[at]] = millimetres
      decoded += 1
   return ranges, decoded < data[0]


def eui_from_system_id(data):
   """The device's 6-byte EUI, low byte first, from its GATT System ID."""
   data = bytes(data)
   return bytes(data[offset] for offset in SYSTEM_ID_EUI_OFFSETS)


# The hardware status says only THAT the device stopped, never what stopped it, which is why a run of watchdog
# resets used to be uninterpretable. The firmware therefore packs its own verdict into the four bits above the
# status, written to a scratch register that survives the reset and folded into this record on the next boot.
# Keep in step with reset_diagnostic_t in src/peripherals/include/system.h.
RESET_DIAGNOSTIC_SHIFT = 12
RESET_DIAGNOSTIC_MASK = 0xF
RESET_DIAGNOSTICS = {
   0:  None,
   1:  'stalled: TimeAlignedTask',
   2:  'stalled: StorageTask',
   3:  'stalled: AppTask',
   4:  'stalled: BLETask',
   5:  'stalled: RangingTask',
   6:  'stalled: several tasks (scheduler or tick stopped)',
   7:  'hard fault',
   8:  'stack overflow',
   9:  'assertion failed',
   10: 'allocation failed',
   11: 'storage fault',
   12: 'storage unwritable',
   13: '32 kHz clock stopped',
   14: 'nothing recorded a cause (breadcrumb survived, so no handler ran)',
}


def decode_reset_reason(status):
   """Decode a raw reset-status word into a list of human-readable causes."""
   causes = [name for bit, name in RESET_FLAGS if status & MAX_RESET_STATUS & bit]
   diagnostic = RESET_DIAGNOSTICS.get((status >> RESET_DIAGNOSTIC_SHIFT) & RESET_DIAGNOSTIC_MASK)
   if diagnostic:
      causes.append(diagnostic)
   return causes or ['Unknown (0x%04X)' % status]

FORMAT_V1 = 1
FORMAT_V2 = 2

# v2 stream header: magic, format version, details length, total pages, total payload bytes
V2_STREAM_MAGIC = b'TTS1'
V2_STREAM_HEADER = struct.Struct('<4sHHII')

# v2 per-page header: seq, first timestamp, last timestamp, payload length, record count, payload CRC
V2_PAGE_HEADER = struct.Struct('<IIIHHI')
V2_MAX_PAYLOAD_BYTES = 4096
NO_TIMESTAMP = 0xFFFFFFFF

# Format version 2 additionally prefixes every record with its own data length, so a payload can be walked
# without knowing the record types. Version 1 payloads are the same records with no prefixes.
FRAMED_VERSION = 2
FRAMED_LENGTH = struct.Struct('<H')


def detect_format(data):
   """Return FORMAT_V2 if the stream carries the v2 magic, otherwise FORMAT_V1.

   Safe because byte 0 of a v1 stream is always a record type in 1..6, and 'T' is 0x54.
   """
   return FORMAT_V2 if data[:4] == V2_STREAM_MAGIC else FORMAT_V1


# Record Grammar --------------------------------------------------------------------------------------------------

def _strip_framing(payload):
   """Remove the per-record length prefixes from a version 2 payload.

   Returns ``(data, boundaries)``, where ``data`` is the payload in the same layout a version 1 page uses --
   ``[type][timestamp][data]`` back to back -- and ``boundaries`` maps the offset of each record in it to the
   offset of the next.  Those boundaries are what the prefixes are for: they are the device's own account of
   where each record ends, so a record this parser does not recognise can be stepped over exactly rather than
   guessed at.  Returns ``(None, None)`` if the prefixes do not describe the payload it was given, which
   means it is damaged rather than merely unfamiliar.
   """
   data, boundaries, offset = bytearray(), {}, 0
   while offset + FRAMED_LENGTH.size <= len(payload):
      data_length = FRAMED_LENGTH.unpack_from(payload, offset)[0]
      offset += FRAMED_LENGTH.size
      if offset + 5 + data_length > len(payload):
         return None, None
      start = len(data)
      data += payload[offset:offset + 5 + data_length]
      boundaries[start] = len(data)
      offset += 5 + data_length
   return (bytes(data), boundaries) if offset == len(payload) else (None, None)


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
   elif record_type == STORAGE_TYPE_TIME_ANCHOR:
      length = 9                                                     # uint32 raw RTC timestamp
   elif record_type == STORAGE_TYPE_DIAGNOSTICS:
      length = 5 + DIAGNOSTICS_STRUCT.size                           # fixed-size counter block
   elif record_type == STORAGE_TYPE_RADIO_ABORT:
      length = 5 + RADIO_ABORT_STRUCT.size
   elif record_type == STORAGE_TYPE_SCHEDULE_CATCH:
      length = 5 + SCHEDULE_CATCH_STRUCT.size
   elif record_type == STORAGE_TYPE_ROUND_START:
      length = 5 + ROUND_START_STRUCT.size
   elif record_type == STORAGE_TYPE_SESSION_END:
      length = 5 + SESSION_END_STRUCT.size
   elif record_type == STORAGE_TYPE_RADIO_TIMING:
      length = 5 + RADIO_TIMING_STRUCT.size
   else:
      return None
   return length if (length is not None and i + length <= len(data)) else None


def _parse_records(data, experiment_start_time, log_data, uid_to_labels, resynchronize, framed=False):
   """Decode records from ``data`` into ``log_data``.

   ``resynchronize`` selects between the two framing assumptions.  v1 has no framing, so a byte that does
   not begin a plausible record is skipped and the scan tries again one byte later.  v2 payloads are
   record-aligned by construction, so a byte that does not begin a valid record means the payload is
   damaged; the page is abandoned rather than slid through, which avoids inventing records from garbage.

   ``framed`` marks a payload whose records carry their own lengths.  Those lengths are then authoritative
   for stepping, in both directions: a record that decodes is still advanced past by its declared length
   rather than by what the decoder consumed, so the two can never drift apart, and a record that does not
   decode -- including one of a type this parser has never heard of -- is stepped over exactly.

   Returns ``(decoded, rejected)`` -- records successfully decoded, and structurally valid records whose
   contents failed validation and were stepped over.
   """
   boundaries = None
   if framed:
      data, boundaries = _strip_framing(data)
      if data is None:
         return 0, 0
   i = 0
   decoded = 0
   rejected = 0
   now = int(time.time())
   while i + 5 < len(data):
      record_type = data[i]
      timestamp_raw = struct.unpack('<I', data[i + 1:i + 5])[0]
      timestamp = experiment_start_time + (timestamp_raw / 1000)
      consumed = 0

      # The 500 ms grid is a v1 resynchronisation heuristic
      on_grid = (timestamp_raw % 500) == 0 if resynchronize else True
      if timestamp <= now and on_grid and 1 <= record_type < STORAGE_NUM_TYPES:
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

         elif record_type == STORAGE_TYPE_TIME_ANCHOR and i + 9 <= len(data):
            # Payload is the device's OWN clock in ms since experiment start, no network offset applied; the
            # record's own timestamp is the same instant on the NETWORK clock. The offset is therefore exactly
            # their difference, at the RTC's 10 ms resolution. 'rtc' is republished as absolute Unix time
            local_ms = struct.unpack('<I', data[i + 5:i + 9])[0]
            if local_ms <= (now - experiment_start_time + 86400) * 1000:
               log_data[timestamp]['rtc'] = round(experiment_start_time + local_ms / 1000, 2)
               log_data[timestamp]['offset'] = timestamp_raw - local_ms
               log_data[timestamp]['lag'] = round((local_ms - timestamp_raw) / 1000, 2)
               consumed = 9

         elif record_type == STORAGE_TYPE_DIAGNOSTICS and i + 5 + DIAGNOSTICS_STRUCT.size <= len(data):
            # Near-misses, not faults. A non-zero value in any of these is the firmware reporting that it
            # came close to something without the log otherwise showing it: watchdog pets refused, charger
            # interrupts discarded as chatter, or BLE buffer allocations that returned NULL
            (declines, late, suppressed, failures, largest, high_water, capacity, master_failures,
             revision, flags, temperature, rx_ok, rx_failed, tx_late, rx_arm_late, isr_over_budget, isr_warm_max_us,
             irq_stuck, wake_max_us, wake_failures, dropped, *rest) = DIAGNOSTICS_STRUCT.unpack_from(data, i + 5)
            stacks = rest[:DIAGNOSTICS_NUM_STACKS]
            ble_resets, bad_blocks = rest[DIAGNOSTICS_NUM_STACKS], rest[DIAGNOSTICS_NUM_STACKS + 1]
            log_data[timestamp]['diag'] = {
               'watchdog_declines': declines,
               'watchdog_late': {name: late[j] for j, name in enumerate(WATCHDOG_TASK_NAMES) if late[j]},
               'charger_suppressed_edges': suppressed,
               'wsf_alloc_failures': failures,
               'wsf_largest_failed_length': largest,
               'wsf_pool_peak': list(high_water),
               'wsf_pool_size': list(capacity),
               'master_cycle_failures': master_failures,
               'firmware': f'{revision:08x}',
               'firmware_modified': bool(flags & DIAGNOSTICS_FLAG_FIRMWARE_MODIFIED),
               'diagnostic_build': bool(flags & DIAGNOSTICS_FLAG_DIAGNOSTIC_BUILD),
               'tempco_disabled': bool(flags & DIAGNOSTICS_FLAG_TEMPCO_DISABLED),
               'tempco_available': bool(flags & DIAGNOSTICS_FLAG_TEMPCO_AVAILABLE),
               'tempco_applied': bool(flags & DIAGNOSTICS_FLAG_TEMPCO_APPLIED),
               'temperature_c': None if temperature == DIAGNOSTICS_TEMPERATURE_UNKNOWN else temperature,
               'radio_rx_ok': rx_ok,
               'radio_rx_failed': rx_failed,
               'radio_tx_late': tx_late,
               'radio_rx_arm_late': rx_arm_late,
               'radio_isr_over_budget': isr_over_budget,
               'radio_isr_warm_max_us': isr_warm_max_us,
               'radio_irq_stuck': irq_stuck,
               'radio_wake_max_us': wake_max_us,
               'radio_wake_failures': wake_failures,
               'records_dropped': dropped,
               'stack_free_words': {name: (None if words == DIAGNOSTICS_STACK_UNMONITORED else words)
                                    for name, words in zip(DIAGNOSTICS_STACK_NAMES, stacks)},
               'ble_resets': ble_resets,
               'nand_bad_blocks': bad_blocks,
            }
            consumed = 5 + DIAGNOSTICS_STRUCT.size

         elif record_type == STORAGE_TYPE_RADIO_ABORT and i + 5 + RADIO_ABORT_STRUCT.size <= len(data):
            phase, slot, schedule_size, late_us, isr_elapsed_us, isr_events, since_temperature_ms, trigger, isr_entry_us, event_to_isr_us, \
               asleep_us, wake_to_isr_us = RADIO_ABORT_STRUCT.unpack_from(data, i + 5)
            log_data[timestamp]['abort'] = {
               'phase': RADIO_ABORT_PHASES.get(phase, phase),
               'slot': slot,
               'schedule_size': schedule_size,
               'late_us': late_us,
               'isr_elapsed_us': None if isr_elapsed_us == RADIO_ABORT_UNMEASURED else isr_elapsed_us,
               'isr_events': isr_events,
               'since_temperature_ms': None if since_temperature_ms == RADIO_ABORT_UNMEASURED else since_temperature_ms,
               'trigger': RADIO_ABORT_TRIGGERS.get(trigger, trigger),
               'isr_entry_us': None if isr_entry_us == RADIO_ABORT_UNMEASURED else isr_entry_us,
               'event_to_isr_us': None if event_to_isr_us == RADIO_ABORT_NO_EVENT_TIME else event_to_isr_us,
               'asleep_us': None if asleep_us == RADIO_ABORT_UNMEASURED else asleep_us,
               'wake_to_isr_us': None if wake_to_isr_us == RADIO_ABORT_UNMEASURED else wake_to_isr_us,
            }
            consumed = 5 + RADIO_ABORT_STRUCT.size

         elif record_type == STORAGE_TYPE_SCHEDULE_CATCH and i + 5 + SCHEDULE_CATCH_STRUCT.size <= len(data):
            first_copy, rounds_missed, lead_us, timer_to_task_us, wake_us, rx_errors, other_frames, first_error_us, \
               carrier_cppm, wake_correction_us, timer_latency_us = SCHEDULE_CATCH_STRUCT.unpack_from(data, i + 5)
            heard = first_copy != SCHEDULE_CATCH_NONE
            log_data[timestamp]['catch'] = {
               'first_copy': first_copy if heard else None,
               'rounds_missed': None if rounds_missed == SCHEDULE_CATCH_NONE else rounds_missed,
               'lead_us': lead_us if heard else None,
               'timer_to_task_us': None if timer_to_task_us == SCHEDULE_CATCH_UNMEASURED else timer_to_task_us,
               'wake_us': None if wake_us == SCHEDULE_CATCH_UNMEASURED else wake_us,
               'rx_errors': rx_errors,
               'other_frames': other_frames,
               'first_error_us': None if first_error_us == SCHEDULE_CATCH_UNMEASURED else first_error_us,
               'carrier_offset_ppm': carrier_cppm / 100 if heard else None,
               'wake_correction_us': wake_correction_us,
               'timer_latency_us': None if timer_latency_us == SCHEDULE_CATCH_UNMEASURED else timer_latency_us,
            }
            consumed = 5 + SCHEDULE_CATCH_STRUCT.size

         elif record_type == STORAGE_TYPE_ROUND_START and i + 5 + ROUND_START_STRUCT.size <= len(data):
            timer_to_task_us, wake_us, timer_to_transmit_us, flags, schedule_size, devices_ranged, timer_latency_us = \
               ROUND_START_STRUCT.unpack_from(data, i + 5)
            log_data[timestamp]['round'] = {
               'timer_to_task_us': timer_to_task_us,
               'wake_us': None if wake_us == SCHEDULE_CATCH_UNMEASURED else wake_us,
               'timer_to_transmit_us': timer_to_transmit_us,
               'second_copy_failed': bool(flags & ROUND_START_FLAG_SECOND_COPY_FAILED),
               'computed': bool(flags & ROUND_START_FLAG_COMPUTED),
               'abandoned': bool(flags & ROUND_START_FLAG_ABANDONED),
               'join_heard': bool(flags & ROUND_START_FLAG_JOIN_HEARD),
               'join_relayed': bool(flags & ROUND_START_FLAG_JOIN_RELAYED),
               'schedule_size': schedule_size,
               'devices_ranged': devices_ranged,
               'timer_latency_us': None if timer_latency_us == SCHEDULE_CATCH_UNMEASURED else timer_latency_us,
            }
            consumed = 5 + ROUND_START_STRUCT.size

         elif record_type == STORAGE_TYPE_SESSION_END and i + 5 + SESSION_END_STRUCT.size <= len(data):
            reason, role, schedule_size, collision_phase, collision_type, collision_source, collision_at_us, session_ms, \
               rounds_ranged, schedules_heard, join_requests_sent, join_requests_heard, listen_errors, stalls = \
               SESSION_END_STRUCT.unpack_from(data, i + 5)
            log_data[timestamp]['session_end'] = {
               'reason': SESSION_END_REASONS.get(reason, reason),
               'role': SCHEDULE_ROLES.get(role, role),
               'schedule_size': schedule_size,
               'collision': {
                  'phase': SCHEDULER_PHASES.get(collision_phase, collision_phase),
                  'packet': PACKET_TYPES.get(collision_type, collision_type),
                  'source': collision_source,
                  'at_us': collision_at_us,
               } if reason == 3 else None,
               'session_ms': session_ms,
               'rounds_ranged': rounds_ranged,
               'schedules_heard': schedules_heard,
               'join_requests_sent': join_requests_sent,
               'join_requests_heard': join_requests_heard,
               'listen_errors': listen_errors,
               'stalls': stalls,
            }
            consumed = 5 + SESSION_END_STRUCT.size

         elif record_type == STORAGE_TYPE_RADIO_TIMING and i + 5 + RADIO_TIMING_STRUCT.size <= len(data):
            arms, after_sleep, during_sleep_entry, slack_min_us, slack_under_25_us, event_to_isr_min_us, event_to_isr_max_us, \
               *bands, wake_to_isr_max_us, antenna, antenna_changes = RADIO_TIMING_STRUCT.unpack_from(data, i + 5)
            log_data[timestamp]['timing'] = {
               'arms': arms,
               'after_sleep': after_sleep,
               'during_sleep_entry': during_sleep_entry,
               'slack_min_us': None if slack_min_us == 0x7FFF else slack_min_us,
               'slack_under_25_us': slack_under_25_us,
               'event_to_isr_min_us': None if event_to_isr_min_us == 0xFFFF else event_to_isr_min_us,
               'event_to_isr_max_us': event_to_isr_max_us,
               'event_to_isr_counts': list(bands),
               'wake_to_isr_max_us': wake_to_isr_max_us,
               'antenna': antenna,
               'antenna_changes': antenna_changes,
            }
            consumed = 5 + RADIO_TIMING_STRUCT.size

         elif record_type == STORAGE_TYPE_RESET_REASON and i + 7 <= len(data):
            status = struct.unpack('<H', data[i + 5:i + 7])[0]
            if ((status >> RESET_DIAGNOSTIC_SHIFT) & RESET_DIAGNOSTIC_MASK) in RESET_DIAGNOSTICS:
               log_data[timestamp]['rst'] = decode_reset_reason(status)
               consumed = 7

      if boundaries is not None:
         # The device said where this record ends; take its word over anything inferred here
         nxt = boundaries.get(i)
         if nxt is None:
            break
         if consumed:
            decoded += 1
         else:
            rejected += 1
         i = nxt
      elif consumed:
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
   if version not in (1, FRAMED_VERSION):
      raise ValueError(f'unsupported v2 format version {version}')
   framed = (version == FRAMED_VERSION)

   offset = V2_STREAM_HEADER.size
   details = data[offset:offset + details_length]
   offset += details_length

   if experiment_start_time is None:
      if len(details) < 4:
         raise ValueError('stream header is missing experiment details')
      experiment_start_time = struct.unpack('<I', details[:4])[0]

   log_data = defaultdict(dict)
   report = {'total_pages': total_pages, 'pages_read': 0, 'holes': [], 'crc_failures': [],
             'short_pages': [], 'rejected_records': [], 'repaired': [], 'last_seq': None, 'first_verified': None, 'verified_seqs': set(),
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
      # range by scanning these same bounds, so where they are not ordered its selection cannot be trusted --
      # and a short selection is invisible otherwise, because the stream it sends is internally consistent and
      # reports no gaps. A page starting less than TIMESTAMP_TOLERANCE_MS early is not that: a range stamped at
      # its round's start can be written after a record stamped later, and keeps its own time.
      if (first_ts != NO_TIMESTAMP) and (previous_last is not None) and (previous_last - first_ts > TIMESTAMP_TOLERANCE_MS):
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

      # Only a page whose payload verified can be believed about its own sequence number: one that failed CRC
      # carries that number in the same damaged bytes as the payload.
      if report['first_verified'] is None:
         report['first_verified'] = (position - 1, seq)
      report['verified_seqs'].add(seq)

      decoded, rejected = _parse_records(payload, experiment_start_time, log_data, uid_to_labels,
                                        resynchronize=False, framed=framed)
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
                                         resynchronize=False, framed=framed)
      report['pages_read'] += 1
      report['repaired'].append((None, seq))
      report['verified_seqs'].add(seq)
      if report['last_seq'] is None or seq > report['last_seq']:
         report['last_seq'] = seq
      if rejected:
         report['rejected_records'].append((None, seq, rejected))

   report['truncated'] = report['pages_read'] < total_pages
   return _finalize(log_data), report


def extract_page_frames(data):
   """Return ``{seq: frame}`` for every CRC-valid page in a stream, frame being header plus payload.

   A retransmission response carries the same page framing as a normal download, but with no experiment
   details, since the host already holds them.  Pages that are still unreadable on the device come back
   as zero-length frames and are simply absent from the result.

   Keeping the header is what makes a repaired page substitutable.  The device sends its own timestamps,
   record count, payload length and CRC with each page, all mutually consistent and verified here, so
   splicing a frame into a stream needs no field rewritten and cannot leave a header disagreeing with the
   payload under it.
   """
   frames = {}
   if len(data) < V2_STREAM_HEADER.size or data[:4] != V2_STREAM_MAGIC:
      return frames
   _magic, version, details_length, _total_pages, _total_payload = V2_STREAM_HEADER.unpack_from(data, 0)
   if version not in (1, FRAMED_VERSION):
      return frames
   offset = V2_STREAM_HEADER.size + details_length
   while offset + V2_PAGE_HEADER.size <= len(data):
      start = offset
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
         frames[seq] = data[start:offset]
   return frames


def extract_pages(data):
   """Return ``{seq: payload}`` for every CRC-valid page in a stream."""
   return {seq: frame[V2_PAGE_HEADER.size:] for seq, frame in extract_page_frames(data).items()}


def _expected_seqs(first_verified, held, total_pages):
   """The sequence numbers a stream declaring ``total_pages`` should carry.

   Sequence numbers run contiguously within an epoch, so the whole set follows from one page whose number
   can be believed and where it sat. ``first_verified`` is that page as ``(position, seq)``; counting back
   from its POSITION rather than starting at its number is what still finds a missing first page.

   A damaged frame ahead of it can shift that position either way -- a junk header inserts a phantom page,
   a bad payload length swallows a real one -- so the estimate is clamped by the pages actually ``held``:
   the range must reach down to the lowest of them and up to the highest.
   """
   first_position, first_seq = first_verified
   base = first_seq - first_position
   base = max(min(base, min(held)), max(held) - total_pages + 1)
   return range(max(base, 0), base + total_pages)


def merge_repairs(data, repairs):
   """Splice repaired pages into a stream, returning one that holds what was recovered.

   This is what makes a repair survive.  Asking the device to resend a page and then writing a file that
   still lacks it means the recovery lives only in whatever the tool happened to display, and re-reading
   the saved log reports holes that were already fixed.  The merged stream is the artefact of record.

   The merged stream is rebuilt from the pages that can be believed, in sequence order: every page that
   arrived intact keeps its original bytes, a repair fills each page that did not, and a page the device
   reported unreadable keeps its empty frame if nothing has replaced it, so a re-read still names it.  A
   frame whose CRC failed is dropped -- its payload is unusable and its header, sequence number included,
   sits in the same untrustworthy bytes -- which is also what removes a phantom page a damaged transfer
   wrote into the stream.  With nothing new to merge the input is returned unchanged, so a clean download
   stays byte-for-byte what the tag sent.

   The stream header keeps the page total the DEVICE declared.  It is the only record of how many pages
   the log should hold, so a merge that still lacks some leaves a file that reads as incomplete and says
   which pages are missing, rather than one that has quietly shrunk to fit what arrived.

   ``repairs`` is ``{seq: frame}`` as returned by :func:`extract_page_frames`.
   """
   if not repairs or len(data) < V2_STREAM_HEADER.size or data[:4] != V2_STREAM_MAGIC:
      return data
   _magic, version, details_length, total_pages, total_payload = V2_STREAM_HEADER.unpack_from(data, 0)
   header_length = V2_STREAM_HEADER.size + details_length

   intact, unreadable, first_verified = {}, {}, None
   offset, position = header_length, 0
   while position < total_pages and offset + V2_PAGE_HEADER.size <= len(data):
      start = offset
      seq, _first_ts, _last_ts, payload_length, _record_count, payload_crc = \
         V2_PAGE_HEADER.unpack_from(data, offset)
      offset += V2_PAGE_HEADER.size
      # A payload running past the end is the transfer stopping mid-page.  One claiming more than a page
      # can hold is damaged outright, and stepping by it would desynchronise every page after.
      if payload_length and ((offset + payload_length > len(data)) or (payload_length > V2_MAX_PAYLOAD_BYTES)):
         break
      offset += payload_length
      position += 1
      if not payload_length:
         unreadable.setdefault(seq, data[start:offset])
      elif zlib.crc32(data[start + V2_PAGE_HEADER.size:offset]) == payload_crc:
         intact.setdefault(seq, data[start:offset])
         if first_verified is None:
            first_verified = (position - 1, seq)

   recovered = {seq: frame for seq, frame in repairs.items() if seq not in intact}
   if not recovered:
      return data
   frames = {**intact, **recovered}

   # Only an empty frame whose number falls inside the declared range can be the device's own marker; one
   # outside it is a header read out of damaged bytes.
   if first_verified is not None:
      expected = _expected_seqs(first_verified, frames.keys(), total_pages)
      for seq, frame in unreadable.items():
         if seq in expected and seq not in frames:
            frames[seq] = frame

   body = b''.join(frames[seq] for seq in sorted(frames))
   payload_bytes = len(body) - len(frames) * V2_PAGE_HEADER.size
   header = bytearray(data[:header_length])
   V2_STREAM_HEADER.pack_into(header, 0, V2_STREAM_MAGIC, version, details_length, max(total_pages, len(frames)), max(total_payload, payload_bytes))
   return bytes(header) + body


def missing_seqs(report):
   """Sequence numbers worth asking the device to resend.

   Every page the device declared that is not held intact: holes, CRC failures, and pages a truncated or
   damaged transfer never delivered at all.  A page whose CRC passed arrived intact, so a page that merely
   stopped decoding early has a record-level problem that a second copy of the same bytes would not fix.

   A transfer in which NO page arrived yields an empty list, because there is no anchor to count from --
   a stream does not necessarily begin at sequence zero, since a wrapped log or a time-bounded download
   starts partway through the epoch.  That case is a failed transfer rather than a partial one, and the
   caller should repeat the whole download instead of naming pages.
   """
   first = report.get('first_verified')
   if first is None or not report['total_pages']:
      return []
   # Ask for what the device declared less what is held, rather than naming each bad page by its position:
   # a phantom page written into the stream by a damaged transfer would otherwise shift every name after it,
   # sending the repair loop after pages it already holds while the real gaps go unasked for.
   expected = _expected_seqs(first, report['verified_seqs'], report['total_pages'])
   return [seq for seq in expected if seq not in report['verified_seqs']]


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
                    'short_pages': [], 'rejected_records': [], 'repaired': [], 'last_seq': None, 'first_verified': None, 'verified_seqs': set(),
                    'time_discontinuities': [], 'details': None, 'truncated': False}
