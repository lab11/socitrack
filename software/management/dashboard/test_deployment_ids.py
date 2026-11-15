"""The ID rules the dashboard applies before scheduling a deployment, which the browser tool shares."""

import os, sys, unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tottag

# IDs as device_uid() returns them: least-significant byte first, so uid[0] is the last part of the address
A = [0x02, 0x00, 0x42, 0xE5, 0x98, 0xC0]
B = [0x05, 0x01, 0x42, 0xE5, 0x98, 0xC0]
B_TWIN = [0x05, 0x02, 0x42, 0xE5, 0x98, 0xC0]
ZERO = [0x00, 0x03, 0x42, 0xE5, 0x98, 0xC0]


class ShortUidRules(unittest.TestCase):

   def test_distinct_nonzero_last_bytes_are_fine(self):
      self.assertIsNone(tottag.short_uid_problem(['a', 'b'], [A, B]))

   def test_shared_last_byte_is_refused_even_when_the_byte_before_differs(self):
      problem = tottag.short_uid_problem(['a', 'b', 'twin'], [A, B, B_TWIN])
      self.assertIsNotNone(problem)
      self.assertIn('b and twin both end in 05', problem)

   def test_an_id_ending_in_00_is_refused(self):
      problem = tottag.short_uid_problem(['a', 'zero'], [A, ZERO])
      self.assertIsNotNone(problem)
      self.assertIn('zero ends in 00', problem)

   def test_matches_device_uid_byte_order(self):
      self.assertEqual(tottag.device_uid('c0:98:e5:42:01:05')[0], 0x05)


if __name__ == '__main__':
   unittest.main()
