import pathlib
import sys
import unittest
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]/'pc_tools'))
from throughput_statistics import summarize


def events(times, duration=10):
    return [{'event': 'frame', 'passed': True, 'bytes': 1000, 'completed_t_s': t} for t in times]+[
        {'event': 'summary', 'passed': True, 'elapsed_s': duration,
         'valid_frames': len(times), 'valid_bytes': len(times)*1000}]


class StatisticsTests(unittest.TestCase):
    def test_fixed_windows_include_header_and_interframe_wait(self):
        s = summarize(events([.1, 4.9, 5., 9.9]))
        self.assertAlmostEqual(s['overall_mbit_s'], .0032)
        self.assertEqual([x['valid_frames'] for x in s['windows']], [2, 2])
        self.assertEqual(s['window_cv_percent'], 0)

    def test_zero_window_is_not_hidden(self):
        s = summarize(events([.1, 14.9], 15))
        self.assertEqual(s['zero_windows'], 1)
        self.assertEqual(s['window_min_mbit_s'], 0)
        self.assertGreater(s['window_std_mbit_s'], 0)

    def test_partial_window_only_excluded_from_extremes(self):
        s = summarize(events([.1, 11], 12.5))
        self.assertEqual(s['full_windows'], 2)
        self.assertEqual(s['valid_frames'], 2)
        self.assertEqual(sum(x['valid_frames'] for x in s['windows']), 1)

    def test_empty_run_has_no_fake_cv(self):
        s = summarize(events([], 10), expected_length=99328)
        self.assertEqual(s['overall_mbit_s'], 0)
        self.assertIsNone(s['window_cv_percent'])
        self.assertFalse(s['expected_length_matches'])

    def test_missing_or_reversed_time_rejected(self):
        for data in (events([2, 1]), events([-1]), events([11])):
            with self.assertRaises(ValueError): summarize(data)
        data = events([1]); del data[0]['completed_t_s']
        with self.assertRaises(ValueError): summarize(data)

    def test_mixed_lengths_and_missing_events_not_accepted(self):
        self.assertFalse(summarize(events([1]), expected_length=99328)['expected_length_matches'])
        data = events([1]); data[-1]['valid_bytes'] = 2000
        with self.assertRaises(ValueError): summarize(data)


if __name__ == '__main__': unittest.main()
