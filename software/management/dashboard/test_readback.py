"""Reading a deployment back after writing it, and labels the 16-byte field could cut."""

import os, sys, unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tottag

S1 = [0x11, 0x22, 0x33, 0x44, 0x55, 0x66]
CG1 = [0x12, 0x22, 0x33, 0x44, 0x55, 0x66]


def packed(labels=(b'S1', b'CG1'), uids=(S1, CG1), use_daily=1):
   return tottag.pack_experiment_details({
      'start_time': 1788300000, 'end_time': 1788300000 + 2 * 86400, 'daily_start_time': 13 * 3600, 'daily_end_time': 3 * 3600,
      'use_daily_times': use_daily, 'num_devices': len(uids),
      'uids': list(uids) + [[0] * 6] * (tottag.MAX_NUM_DEVICES - len(uids)),
      'labels': list(labels) + [b''] * (tottag.MAX_NUM_DEVICES - len(labels))})


class Readback(unittest.TestCase):

   def test_what_was_sent_reads_back_clean(self):
      sent = packed()
      self.assertEqual(tottag.readback_differences(sent, sent[1:]), [])
      tottag.check_readback(sent, bytearray(sent[1:]))

   def test_each_difference_is_named_in_the_browser_tools_words(self):
      sent = packed()
      got = bytearray(packed(labels=(b'S1', b'Mom'), uids=(S1, [0x7A, 0x01, 0, 0, 0, 0]))[1:])
      got[-1] = 1
      self.assertEqual(tottag.readback_differences(sent, bytes(got)), [
         'Tag 2 in its list is TotTag 01:7A where TotTag 22:12 was sent.',
         'It stored the label "Mom" where "CG1" was sent.',
         'It reports the deployment as already canceled.'])

   def test_daily_times_count_only_when_daily_hours_are_used(self):
      sent, got = packed(use_daily=0), bytearray(packed(use_daily=0)[1:])
      got[8] ^= 1
      self.assertEqual(tottag.readback_differences(sent, bytes(got)), [])

   def test_a_short_read_is_a_mismatch_not_a_crash(self):
      sent = packed()
      with self.assertRaises(tottag.DetailsMismatch) as caught:
         tottag.check_readback(sent, sent[1:100])
      self.assertEqual(tottag.write_outcome(caught.exception),
                       ('mismatch', 'It read back 99 bytes of deployment details where 239 were sent.'))

   def test_a_mismatch_carries_its_differences_into_the_record(self):
      sent = packed()
      with self.assertRaises(tottag.DetailsMismatch) as caught:
         tottag.check_readback(sent, bytes(239))
      outcome, detail = tottag.write_outcome(caught.exception)
      self.assertEqual(outcome, 'mismatch')
      self.assertIn('It stored a different start time from the one sent.', detail)


class Labels(unittest.TestCase):

   def test_a_label_cut_mid_character_still_decodes(self):
      cut = ('A' + 'ñ' * 8).encode('utf-8')[:16]
      self.assertEqual(len(cut), 16)
      self.assertEqual(tottag.decode_label(cut), 'A' + 'ñ' * 7 + '�')

   def test_padding_is_dropped(self):
      self.assertEqual(tottag.decode_label(b'S1\x00\x00\x00'), 'S1')

   def test_a_label_is_refused_only_when_its_bytes_overflow_the_field(self):
      self.assertIsNone(tottag.label_problem('A' * 16))
      self.assertIsNone(tottag.label_problem('ñ' * 8))
      self.assertIn('too long', tottag.label_problem('ñ' * 9))
      self.assertIn('letters with accents count as two', tottag.label_problem('A' * 17))


if __name__ == '__main__':
   unittest.main()
