"""Read-only, fixed B-mode processing adapted from ultrasound_dev_kit.

The payloads are overlapping DDR snapshots, NOT consecutive stream fragments.
Transport CRC integrity cannot certify acquisition coherence.
"""
from collections import deque
from io import BytesIO
from pathlib import Path
import threading
import time

import numpy as np
from PIL import Image

LINES, SAMPLES, HEADER_WORDS = 64, 6144, 8
LINE_WORDS = SAMPLES + HEADER_WORDS
FRAME_BYTES = LINES * LINE_WORDS * 2
LINE_TTL = 5.0
DISPLAY_DEPTH_MM = 120.0
MAX_FPS = 10
LUT_PATH = Path(__file__).with_name('bmode_assets') / 'graymap_lut.txt'


def parse_lines(payload):
    """Return complete, neighbour-confirmed lines; last duplicate wins.

    Never carry partial bytes into the next overlapping DDR snapshot. Candidate
    headers embedded in data do not invalidate a correctly spaced real pair.
    The final line needs no following header if its predecessor confirms it.
    """
    if len(payload) % 2 or len(payload) > 1048576:
        return {}
    words = np.frombuffer(payload, dtype='<u2')
    if words.size < LINE_WORDS:
        return {}
    candidates = np.flatnonzero((words[:-2] == 0x1c00) &
                               (words[1:-1] == 0x1800) & (words[2:] < LINES))
    positions = set(map(int, candidates))
    result = {}
    for base in candidates:
        base = int(base)
        if base + LINE_WORDS > words.size:
            continue
        if base-LINE_WORDS not in positions and base+LINE_WORDS not in positions:
            continue
        result[int(words[base+2])] = words[base+HEADER_WORDS:base+LINE_WORDS].copy()
    return result


class LineCache:
    def __init__(self):
        self.data = np.zeros((LINES, SAMPLES), dtype=np.uint16)
        self.updated = np.full(LINES, -np.inf)

    def update(self, lines, arrived):
        for line, values in lines.items():
            self.data[line] = values
            self.updated[line] = arrived

    def coverage(self, now):
        return int(np.count_nonzero(now-self.updated <= LINE_TTL))


def tgc_gain():
    points = np.array([[0, 0], [512, 0], [1024, 0], [2048, 0], [3072, 0],
                       [3247, 0], [4096, 8], [4608, 13], [5120, 17],
                       [5632, 21], [SAMPLES-1, 24]], dtype=np.float32)
    curve = np.interp(np.arange(SAMPLES, dtype=np.float32), points[:, 0], points[:, 1]).astype(np.float32)
    return np.power(10., curve/20.).astype(np.float32)


def load_lut(path=LUT_PATH):
    # Ship and validate the actual reference LUT; do not silently use identity.
    text = Path(path).read_text(encoding='utf-8')
    text = '\n'.join(line.split('#', 1)[0] for line in text.splitlines())
    values = text.split('[', 1)[1].split(']', 1)[0].replace(',', ' ').split()
    if len(values) != 256:
        raise ValueError('graymap LUT must contain 256 entries')
    lut = np.array([int(value) for value in values], dtype=np.int32)
    if np.any((lut < 0) | (lut > 255)):
        raise ValueError('graymap LUT values must be 0..255')
    return lut.astype(np.uint8)


def precompute_scan_converter():
    # Same float32 geometry and bilinear weights as the reference pipeline.
    c, fc, fs = 1540., 4e6, 25e6
    step = (c/fc)/2.
    range_step = c/(2.*fs)
    angles = np.deg2rad(np.linspace(-45., 45., LINES)).astype(np.float32)
    radius = DISPLAY_DEPTH_MM/1000.
    xmax = radius*np.sin(np.deg2rad(45.))
    x = np.arange(-xmax, xmax+step*.5, step, dtype=np.float32)
    z = np.arange(0., radius+step*.5, step, dtype=np.float32)
    xx, zz = np.meshgrid(x, z)
    theta, r = np.arctan2(xx, zz), np.hypot(xx, zz)
    tp, rp = (theta-angles[0])/(angles[1]-angles[0]), r/range_step
    t0, r0 = np.floor(tp).astype(np.int32), np.floor(rp).astype(np.int32)
    t1, r1 = t0+1, r0+1
    valid = ((t0 >= 0) & (t1 < LINES) & (r0 >= 0) & (r1 < SAMPLES) &
             (theta >= angles[0]) & (theta <= angles[-1]) & (r <= radius))
    wt, wr = np.clip(tp-t0, 0., 1.).astype(np.float32), np.clip(rp-r0, 0., 1.).astype(np.float32)
    return {'shape': xx.shape, 'valid_flat': valid.ravel(),
            'theta0': t0.ravel(), 'theta1': t1.ravel(), 'range0': r0.ravel(), 'range1': r1.ravel(),
            'w00': ((1.-wt)*(1.-wr)).ravel(), 'w01': ((1.-wt)*wr).ravel(),
            'w10': (wt*(1.-wr)).ravel(), 'w11': (wt*wr).ravel()}


def scan_convert(polar, scan):
    out = np.full(scan['valid_flat'].shape, np.nan, dtype=np.float32)
    v = scan['valid_flat']
    t0, t1, r0, r1 = (scan[key][v] for key in ('theta0', 'theta1', 'range0', 'range1'))
    out[v] = (polar[t0, r0]*scan['w00'][v] + polar[t0, r1]*scan['w01'][v] +
              polar[t1, r0]*scan['w10'][v] + polar[t1, r1]*scan['w11'][v])
    return out.reshape(scan['shape'])


class Renderer:
    def __init__(self):
        self.scan, self.lut, self.gain = precompute_scan_converter(), load_lut(), tgc_gain()
        self.reference = None

    def reset(self):
        self.reference = None

    def gray(self, samples):
        polar = samples.astype(np.float32)/float(2**14)
        polar += np.log10(np.clip(self.gain, 10**(-24/20), 10**(24/20)))[np.newaxis, :]
        cart = scan_convert(polar, self.scan)
        finite = np.isfinite(cart)
        current = float(np.percentile(cart[finite], 99.9)) if finite.any() else 0.
        self.reference = current if self.reference is None else .85*self.reference + .15*current
        db = 20.*(cart-self.reference)
        db = (db+3.)*1.
        db = np.clip(np.where(db < -45., -45., db), -45., 0.)
        db[~finite] = -45.
        gray = np.clip(np.round((db+45.)/45.*255.), 0, 255).astype(np.uint8)
        return self.lut[gray]

    def png(self, samples):
        out = BytesIO()
        Image.fromarray(self.gray(samples)).save(out, format='PNG', compress_level=1)
        return out.getvalue()


class ImagingWorker:
    """Bounded latest-frame queue. No rendering/file I/O on a receiver thread."""
    def __init__(self, renderer=None):
        self.renderer = renderer or Renderer()
        self.cv = threading.Condition()
        self.pending = deque()
        self.generation = 0
        self.accepting = False
        self.closing = False
        self.session = None
        self.sequence = 0
        self.image = None
        self.image_at = None
        self.image_frame = None
        self.lines = self.format_errors = self.queue_drops = self.images = 0
        self.error = ''
        self.thread = threading.Thread(target=self._run, name='bmode-render', daemon=True)
        self.thread.start()

    def reset(self):
        with self.cv:
            self.generation += 1
            self.accepting = True
            self.session = None
            self.pending.clear()
            self.image = self.image_at = self.image_frame = None
            self.lines = self.format_errors = self.queue_drops = self.images = 0
            self.error = ''
            self.cv.notify_all()

    def finish(self):
        with self.cv:
            self.accepting = False
            self.generation += 1
            self.pending.clear()
            self.lines = 0
            self.cv.notify_all()

    def submit(self, session, frame, payload):
        with self.cv:
            if not self.accepting:
                return
            if self.session != session:
                self.generation += 1
                self.session = session
                self.pending.clear()
                self.image = self.image_at = self.image_frame = None
                self.lines = 0
            if len(self.pending) == 2:
                self.pending.popleft()
                self.queue_drops += 1
            self.pending.append((self.generation, frame, payload, time.monotonic()))
            self.cv.notify()

    def _run(self):
        cache, local_generation, dirty, rendered = LineCache(), -1, False, 0.
        last_valid_frame = None
        try:
            while True:
                with self.cv:
                    if not self.pending and not self.closing:
                        self.cv.wait(.05)
                    if self.closing:
                        return
                    generation = self.generation
                    item = self.pending.popleft() if self.pending else None
                if generation != local_generation:
                    cache = LineCache()
                    self.renderer.reset()
                    local_generation, dirty, rendered = generation, False, 0.
                    last_valid_frame = None
                if item and item[0] == generation:
                    _, frame, payload, arrived = item
                    lines = parse_lines(payload)
                    cache.update(lines, arrived)
                    dirty |= bool(lines)
                    if lines:
                        last_valid_frame = frame
                    with self.cv:
                        if generation == self.generation and not lines:
                            self.format_errors += 1
                now = time.monotonic()
                coverage = cache.coverage(now)
                with self.cv:
                    if generation == self.generation:
                        self.lines = coverage
                if coverage != LINES or not dirty or now-rendered < 1./MAX_FPS:
                    continue
                png = self.renderer.png(cache.data)
                with self.cv:
                    if generation == self.generation and self.accepting and cache.coverage(time.monotonic()) == LINES:
                        self.image, self.image_at = png, time.monotonic()
                        self.image_frame = last_valid_frame
                        self.sequence += 1
                        self.images += 1
                dirty, rendered = False, time.monotonic()
        except Exception as exc:
            with self.cv:
                self.error = '图像处理失败: '+str(exc)

    def snapshot(self):
        with self.cv:
            return {'lines': self.lines, 'total_lines': LINES, 'format_errors': self.format_errors,
                    'queue_drops': self.queue_drops, 'queue_size': len(self.pending), 'images': self.images,
                    'image_available': self.image is not None, 'image_sequence': self.sequence,
                    'image_age_s': None if self.image_at is None else max(0., time.monotonic()-self.image_at),
                    'session': self.session, 'image_frame': self.image_frame, 'processing_error': self.error}

    def latest(self, after):
        with self.cv:
            return (self.sequence, self.image) if self.image is not None and self.sequence > after else (self.sequence, None)

    def close(self):
        with self.cv:
            self.closing = True
            self.cv.notify_all()
        self.thread.join(2)
