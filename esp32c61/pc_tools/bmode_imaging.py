"""Read-only, fixed B-mode processing adapted from ultrasound_dev_kit.

Each payload is an immutable, complete PL-published frame. Never compose
lines from separate transport frames. Requires the UBF1 frame-store hardware.
"""
from collections import deque
from io import BytesIO
from pathlib import Path
import threading
import time
from copy import deepcopy

import numpy as np
from PIL import Image
import bmode_settings as settings_model

LINES, SAMPLES, HEADER_WORDS = 64, 6144, 8
LINE_WORDS = SAMPLES + HEADER_WORDS
FRAME_BYTES = LINES * LINE_WORDS * 2
LINE_TTL = 5.0
DISPLAY_DEPTH_MM = 120.0
MAX_FPS = 20
LUT_PATH = Path(__file__).with_name('bmode_assets') / 'graymap_lut.txt'


def parse_frame(payload):
    if len(payload) < LINES * HEADER_WORDS * 2 or len(payload) % 2:
        raise ValueError('invalid complete-frame size')
    words = np.frombuffer(payload, dtype='<u2')
    if words[1] != SAMPLES:
        raise ValueError('raw sample count must be 6144')
    down = int(words[3] & 15)
    if down > 4:
        raise ValueError('unsupported decimation')
    samples = SAMPLES >> down
    stride = HEADER_WORDS + samples
    if words.size != LINES * stride:
        raise ValueError('frame size does not match line descriptors')
    matrix = words.reshape(LINES, stride)
    if (not np.all(matrix[:, 0] == 0x1c00) or
        not np.all(matrix[:, 1] == SAMPLES) or
        not np.array_equal(matrix[:, 2] & 127, np.arange(LINES)) or
        not np.all((matrix[:, 3] & 15) == down)):
        raise ValueError('missing, duplicate, out-of-order or mixed-configuration line')
    return down, matrix[:, HEADER_WORDS:].copy()


def parse_lines(payload):
    """Compatibility helper; only a complete, validated frame is accepted."""
    try:
        _, data = parse_frame(payload)
        return dict(enumerate(data))
    except ValueError:
        return {}


def tgc_gain(points=None):
    points = np.array(settings_model.TGC_POINTS if points is None else points, dtype=np.float32)
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


def precompute_scan_converter(down=0, settings=None):
    # Same float32 geometry and bilinear weights as the reference pipeline.
    c, fc, fs = 1540., 4e6, 25e6/(1 << down)
    sample_count = SAMPLES >> down
    step = (c/fc)/2.
    range_step = c/(2.*fs)
    angle_degrees = np.linspace(-45., 45., LINES) if settings is None else np.asarray(settings['angles_deg'], dtype=np.float64)
    angles = np.deg2rad(angle_degrees).astype(np.float32)
    radius = (DISPLAY_DEPTH_MM if settings is None else settings['depth_mm'])/1000.
    xmin = radius*min(0., np.sin(np.deg2rad(angle_degrees[0])))
    xmax = radius*max(0., np.sin(np.deg2rad(angle_degrees[-1])))
    x = np.arange(xmin, xmax+step*.5, step, dtype=np.float32)
    z = np.arange(0., radius+step*.5, step, dtype=np.float32)
    xx, zz = np.meshgrid(x, z)
    theta, r = np.arctan2(xx, zz), np.hypot(xx, zz)
    # Preserve the exact old uniform-angle arithmetic for default images.
    if np.allclose(np.diff(angles), angles[1]-angles[0], atol=2e-7, rtol=0):
        tp = (theta-angles[0])/(angles[1]-angles[0])
    else:
        tp = np.interp(theta, angles, np.arange(LINES), left=-1, right=LINES).astype(np.float32)
    rp = r/range_step
    t0, r0 = np.floor(tp).astype(np.int32), np.floor(rp).astype(np.int32)
    t1, r1 = t0+1, r0+1
    valid = ((t0 >= 0) & (t1 < LINES) & (r0 >= 0) & (r1 < sample_count) &
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
        self.settings = settings_model.defaults(load_lut())
        self.scan, self.lut, self.gain = precompute_scan_converter(), load_lut(), tgc_gain()
        self.reference = None
        self.down = 0

    def configure(self, down):
        if down != self.down:
            self.down = down
            self.scan = precompute_scan_converter(down, self.settings)
            self.gain = tgc_gain(self.settings['tgc_points'])[::1 << down]
            self.reset()

    def apply_settings(self, settings):
        settings = settings_model.validate(settings)
        geometry = any(settings[k] != self.settings[k] for k in settings_model.GEOMETRY)
        reset_reference = geometry or any(settings[k] != self.settings[k] for k in ('tgc_enabled', 'tgc_points'))
        self.settings = deepcopy(settings)
        if geometry:
            self.scan = precompute_scan_converter(self.down, settings)
        self.lut = np.array(settings['lut'], dtype=np.uint8)
        self.gain = tgc_gain(settings['tgc_points'])[::1 << self.down]
        if reset_reference:
            self.reset()

    def reset(self):
        self.reference = None

    def gray(self, samples):
        polar = samples.astype(np.float32)/float(2**14)
        if self.settings['tgc_enabled']:
            polar += np.log10(np.clip(self.gain, 10**(-24/20), 10**(24/20)))[np.newaxis, :]
        cart = scan_convert(polar, self.scan)
        finite = np.isfinite(cart)
        current = float(np.percentile(cart[finite], 99.9)) if finite.any() else 0.
        self.reference = current if self.reference is None else .85*self.reference + .15*current
        db = 20.*(cart-self.reference)
        s = self.settings
        db = (db+s['brightness_db'])*s['contrast']
        dr = s['dynamic_range_db']
        db = np.clip(np.where(db < s['noise_floor_db'], s['noise_floor_db'], db), -dr, 0.)
        db[~finite] = -dr
        gray = np.clip(np.round((db+dr)/dr*255.), 0, 255).astype(np.uint8)
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
        self.decimation = 0
        self.error = ''
        self.settings = settings_model.defaults(load_lut())
        self.settings_revision = self.applied_revision = 1
        self.image_settings_revision = 0
        self.image_display_settings = None
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

    def get_settings(self):
        with self.cv:
            return {'settings': deepcopy(self.settings), 'revision': self.settings_revision,
                    'applied_revision': self.applied_revision,
                    'image_revision': self.image_settings_revision}

    def set_settings(self, settings, expected_revision):
        settings = settings_model.validate(settings)
        with self.cv:
            if type(expected_revision) is not int or expected_revision != self.settings_revision:
                raise RuntimeError('显示配置已被其他页面修改，请重新读取')
            if self.accepting and settings['angles_deg'] != self.settings['angles_deg']:
                raise RuntimeError('修改角度映射前请停止接收')
            self.settings = deepcopy(settings)
            self.settings_revision += 1
            # Invalidate an in-flight old rendering, but retain immutable input.
            # Retain the last published PNG and its own geometry until the new
            # revision is rendered. Slider edits must not flash an empty image.
            self.cv.notify_all()
        return self.get_settings()

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
        local_generation, rendered, dirty = -1, 0., False
        data, arrived, last_valid_frame = None, -float('inf'), None
        try:
            while True:
                with self.cv:
                    if not self.pending and not self.closing:
                        self.cv.wait(.05)
                    if self.closing:
                        return
                    generation = self.generation
                    settings_revision = self.settings_revision
                    settings = deepcopy(self.settings) if settings_revision != self.applied_revision else None
                    item = self.pending.popleft() if self.pending else None
                if settings is not None:
                    if hasattr(self.renderer, 'apply_settings'):
                        self.renderer.apply_settings(settings)
                    with self.cv:
                        self.applied_revision = settings_revision
                    dirty = data is not None
                if generation != local_generation:
                    self.renderer.reset()
                    local_generation, dirty, rendered = generation, False, 0.
                    data, arrived, last_valid_frame = None, -float('inf'), None
                if item and item[0] == generation:
                    _, frame, payload, received_at = item
                    try:
                        down, complete = parse_frame(payload)
                    except ValueError:
                        with self.cv:
                            if generation == self.generation:
                                self.format_errors += 1
                    else:
                        if hasattr(self.renderer, 'configure'):
                            self.renderer.configure(down)
                        data, arrived, last_valid_frame = complete, received_at, frame
                        dirty = True
                        with self.cv:
                            if generation == self.generation:
                                if self.decimation != down:
                                    self.image = self.image_at = self.image_frame = None
                                    self.lines = 0
                                self.decimation = down
                now = time.monotonic()
                coverage = LINES if data is not None and now-arrived <= LINE_TTL else 0
                with self.cv:
                    if generation == self.generation:
                        self.lines = coverage
                if not coverage or not dirty or now-rendered < 1./MAX_FPS:
                    continue
                png = self.renderer.png(data)
                with self.cv:
                    if (generation == self.generation and settings_revision == self.settings_revision and
                            self.accepting and time.monotonic()-arrived <= LINE_TTL):
                        self.image, self.image_at = png, time.monotonic()
                        self.image_frame = last_valid_frame
                        self.sequence += 1
                        self.images += 1
                        self.image_settings_revision = settings_revision
                        self.image_display_settings = {k: deepcopy(self.settings[k]) for k in
                            ('depth_mm', 'angles_deg', 'dynamic_range_db', 'brightness_db', 'contrast', 'noise_floor_db', 'tgc_enabled')}
                # Limit start-to-start: PNG/render time is part of the budget,
                # not an extra 50 ms sleep after every completed picture.
                dirty, rendered = False, now
        except Exception as exc:
            with self.cv:
                self.error = '图像处理失败: '+str(exc)

    def snapshot(self):
        with self.cv:
            return {'settings_revision': self.settings_revision, 'applied_settings_revision': self.applied_revision,
                    'image_settings_revision': self.image_settings_revision,
                    'image_display_settings': deepcopy(self.image_display_settings),
                    'display_settings': {k: deepcopy(self.settings[k]) for k in ('depth_mm', 'angles_deg', 'dynamic_range_db', 'brightness_db', 'contrast', 'noise_floor_db', 'tgc_enabled')},
                    'decimation': self.decimation, 'sample_rate_mhz': 25/(1 << self.decimation),
                    'samples_per_line': SAMPLES >> self.decimation, 'frame_coherent': True, 'lines': self.lines, 'total_lines': LINES, 'format_errors': self.format_errors,
                    'queue_drops': self.queue_drops, 'queue_size': len(self.pending), 'images': self.images,
                    'image_available': self.image is not None, 'image_sequence': self.sequence,
                    'image_age_s': None if self.image_at is None else max(0., time.monotonic()-self.image_at),
                    'session': self.session, 'image_frame': self.image_frame, 'processing_error': self.error}

    def latest(self, after):
        with self.cv:
            return (self.sequence, self.image) if self.image is not None and self.sequence > after else (self.sequence, None)

    def latest_with_metadata(self, after):
        with self.cv:
            png = self.image if self.image is not None and self.sequence > after else None
            return self.sequence, png, {'revision': self.image_settings_revision,
                                        'settings': deepcopy(self.image_display_settings)}

    def close(self):
        with self.cv:
            self.closing = True
            self.cv.notify_all()
        self.thread.join(2)
