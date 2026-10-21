#!/usr/bin/env python3
"""Full-frame throughput in fixed windows, including all waits and empty windows."""
import argparse
import collections
import json
import math
from pathlib import Path
import statistics


def summarize(events, window=5.0, expected_length=None):
    if not math.isfinite(window) or window <= 0:
        raise ValueError('window must be positive and finite')
    summaries = [x for x in events if x.get('event') == 'summary']
    if len(summaries) != 1:
        raise ValueError('exactly one completed receiver run is required')
    summary = summaries[0]
    duration = summary['elapsed_s']
    if not math.isfinite(duration) or duration <= 0:
        raise ValueError('invalid receiver duration')
    frames = [x for x in events if x.get('event') == 'frame' and x.get('passed')]
    lengths = collections.Counter(x['bytes'] for x in frames)
    if sum(x['bytes'] for x in frames) != summary['valid_bytes'] or len(frames) != summary['valid_frames']:
        raise ValueError('frame events and summary disagree; cannot reconstruct time windows')
    bins = [0] * math.floor(duration/window)
    frame_bins = [0] * len(bins)
    previous = -1.0
    for frame in frames:
        when = frame.get('completed_t_s')
        if when is None or not math.isfinite(when) or not 0 <= when <= duration or when < previous:
            raise ValueError('missing/invalid completion timestamp; legacy per-frame rates are not window rates')
        if not isinstance(frame['bytes'], int) or frame['bytes'] <= 0:
            raise ValueError('invalid frame length')
        previous = when
        index = math.floor(when/window)
        if index < len(bins):
            bins[index] += frame['bytes']
            frame_bins[index] += 1
    rates = [n*8/window/1e6 for n in bins]
    mean = statistics.mean(rates) if rates else None
    std = statistics.pstdev(rates) if rates else None
    def percentile(percent):
        if not rates:
            return None
        values = sorted(rates)
        pos = (len(values)-1)*percent/100
        low = math.floor(pos)
        high = math.ceil(pos)
        return values[low]+(values[high]-values[low])*(pos-low)
    return {
        'receiver_passed': summary['passed'], 'duration_s': duration,
        'valid_frames': len(frames), 'valid_bytes': summary['valid_bytes'],
        'frame_lengths': dict(lengths),
        'expected_length_matches': expected_length is None or (bool(frames) and set(lengths) == {expected_length}),
        'overall_mbit_s': summary['valid_bytes']*8/duration/1e6,
        'overall_fps': len(frames)/duration,
        'window_s': window, 'full_windows': len(bins),
        'window_min_mbit_s': min(rates) if rates else None,
        'window_max_mbit_s': max(rates) if rates else None,
        'window_mean_mbit_s': mean, 'window_std_mbit_s': std,
        'window_cv_percent': 100*std/mean if mean else None,
        'window_p5_mbit_s': percentile(5), 'window_p95_mbit_s': percentile(95),
        'window_peak_to_peak_mbit_s': max(rates)-min(rates) if rates else None,
        'zero_windows': sum(n == 0 for n in bins),
        'windows': [{'start_s': i*window, 'end_s': (i+1)*window,
                     'valid_frames': n, 'mbit_s': rates[i], 'fps': n/window}
                    for i, n in enumerate(frame_bins)],
        'note': 'Overall includes start/stop. Min/max/std/CV use full fixed windows, including zero windows; final short window is excluded only from window statistics. PC CRC-valid bytes, not socket read-call rates.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--window', type=float, default=5)
    parser.add_argument('--expected-length', type=int)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    try:
        result = summarize([json.loads(x) for x in args.log.read_text(encoding='utf-8').splitlines() if x.strip()],
                           args.window, args.expected_length)
    except (ValueError, KeyError) as error:
        parser.error(str(error))
    if args.output:
        with args.output.open('x', encoding='utf-8') as output:
            json.dump(result, output, indent=2)
    print(json.dumps({k: v for k, v in result.items() if k != 'windows'}, indent=2))
    return 0 if result['receiver_passed'] and result['expected_length_matches'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
