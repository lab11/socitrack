#!/usr/bin/env python3
"""Compare two page-framed downloads of the same log, page by page.

A log is append-only, so a page that has already been committed never changes: any sequence number present
in both files must carry byte-identical payload in both.  That makes an earlier download a regression
oracle for a later one, which is the point of this tool -- it was written to check that switching the BLE
transport from indications to notifications still delivers exactly what indications delivered.

Notifications are unacknowledged, so a transport defect shows up as missing or corrupted pages rather than
as an error.  Comparing against a known-good download is how that becomes visible.

    python3 compare_downloads.py reference.ttg candidate.ttg

Exit status is 0 when every shared page matches and the candidate is internally sound, 1 otherwise.
"""

import sys
import zlib

import tottag_format


def load_pages(path):
   """Return (pages, report) where pages maps sequence number to payload bytes."""
   with open(path, 'rb') as handle:
      data = handle.read()
   if tottag_format.detect_format(data) != tottag_format.FORMAT_V2:
      raise SystemExit(f'{path}: not a page-framed (v2) download, so there are no pages to compare')
   pages, _records = tottag_format.extract_pages(data), None
   return data, pages, tottag_format.parse(data)[1]


def describe(label, path, data, pages, report):
   print(f'{label}: {path}')
   print(f'   {len(data)} bytes, {report["pages_read"]}/{report["total_pages"]} pages, '
         f'{len(pages)} with valid CRC')
   for key, text in (('holes', 'unreadable on the device'), ('crc_failures', 'failed CRC'),
                     ('short_pages', 'stopped decoding early'),
                     ('time_discontinuities', 'time discontinuities')):
      if report[key]:
         print(f'   {len(report[key])} page(s) {text}')
   if report['truncated']:
      print('   TRUNCATED: the transfer ended before every page arrived')


def main(argv):
   if len(argv) != 3:
      raise SystemExit(__doc__)
   ref_path, new_path = argv[1], argv[2]
   ref_data, ref_pages, ref_report = load_pages(ref_path)
   new_data, new_pages, new_report = load_pages(new_path)

   describe('reference', ref_path, ref_data, ref_pages, ref_report)
   describe('candidate', new_path, new_data, new_pages, new_report)

   # A later download of a still-running deployment holds MORE pages, not different ones, so compare only
   # the sequence numbers the two have in common and report the rest as growth
   shared = sorted(set(ref_pages) & set(new_pages))
   missing = sorted(set(ref_pages) - set(new_pages))
   added = sorted(set(new_pages) - set(ref_pages))
   differing = [seq for seq in shared if ref_pages[seq] != new_pages[seq]]

   print(f'\nshared sequence numbers: {len(shared)}')
   print(f'   identical payloads: {len(shared) - len(differing)}')
   print(f'   DIFFERING payloads: {len(differing)}')
   print(f'   present in the reference but MISSING from the candidate: {len(missing)}')
   print(f'   new in the candidate (the log kept growing): {len(added)}')

   for seq in differing[:5]:
      a, b = ref_pages[seq], new_pages[seq]
      first = next((i for i, (x, y) in enumerate(zip(a, b)) if x != y), min(len(a), len(b)))
      print(f'   seq {seq}: {len(a)} vs {len(b)} bytes, first difference at byte {first}')
   if missing[:10]:
      print(f'   missing sequence numbers (first 10): {missing[:10]}')

   # A page that is absent from the candidate is only a real loss if the candidate claimed to cover it
   claimed = [seq for seq in missing
              if new_report['last_seq'] is not None and seq <= new_report['last_seq']]

   failed = bool(differing) or bool(claimed) or new_report['truncated'] or \
            bool(new_report['holes']) or bool(new_report['crc_failures'])
   if differing:
      print('\nFAIL: a committed page changed between downloads, which cannot happen to an append-only log')
   if claimed:
      print(f'\nFAIL: {len(claimed)} page(s) within the candidate\'s own range never arrived: {claimed[:10]}')
   if new_report['truncated']:
      print('\nFAIL: the candidate transfer was truncated')
   if new_report['holes'] or new_report['crc_failures']:
      print(f'\nFAIL: the candidate has {len(new_report["holes"])} hole(s) and '
            f'{len(new_report["crc_failures"])} CRC failure(s) that retransmission did not repair')
   if not failed:
      print(f'\nPASS: every one of the {len(shared)} shared pages is byte-identical, '
            f'and the candidate reports no loss')
   return 1 if failed else 0


if __name__ == '__main__':
   sys.exit(main(sys.argv))
