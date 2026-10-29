"""Validated PC-only display settings. Never generates hardware commands."""
from copy import deepcopy
import math

TGC_POINTS = [[0, 0], [512, 0], [1024, 0], [2048, 0], [3072, 0], [3247, 0],
              [4096, 8], [4608, 13], [5120, 17], [5632, 21], [6143, 24]]
FIELDS = {'version', 'dynamic_range_db', 'brightness_db', 'contrast', 'noise_floor_db',
          'tgc_enabled', 'tgc_points', 'lut', 'depth_mm', 'angles_deg'}
GEOMETRY = ('depth_mm', 'angles_deg')


def defaults(lut):
    return {'version': 1, 'dynamic_range_db': 45., 'brightness_db': 3., 'contrast': 1.,
            'noise_floor_db': -45., 'tgc_enabled': True, 'tgc_points': deepcopy(TGC_POINTS),
            'lut': list(map(int, lut)), 'depth_mm': 120.,
            'angles_deg': [-45.+90.*i/63 for i in range(64)]}


def number(value, low, high, name):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or not low <= value <= high:
        raise ValueError(f'{name} 必须在 {low}～{high} 范围内')
    return float(value)


def validate(settings):
    if not isinstance(settings, dict) or set(settings) != FIELDS:
        raise ValueError('显示配置字段缺失或包含未知字段')
    result = deepcopy(settings)
    if type(result['version']) is not int or result['version'] != 1:
        raise ValueError('不支持的显示配置版本')
    for key, low, high in (('dynamic_range_db', 20, 70), ('brightness_db', -10, 20),
                           ('contrast', .5, 2), ('noise_floor_db', -60, -20), ('depth_mm', 10, 188)):
        result[key] = number(result[key], low, high, key)
    if type(result['tgc_enabled']) is not bool:
        raise ValueError('tgc_enabled 必须为布尔值')
    points = result['tgc_points']
    if not isinstance(points, list) or not 2 <= len(points) <= 128:
        raise ValueError('TGC 需要 2～128 个控制点')
    previous = -1
    for point in points:
        if not isinstance(point, list) or len(point) != 2:
            raise ValueError('TGC 每个控制点为 [原始采样位置, dB]')
        x = number(point[0], 0, 6143, 'TGC 位置')
        if x <= previous:
            raise ValueError('TGC 位置必须严格递增')
        previous = x
        number(point[1], -24, 24, 'TGC 增益')
    if points[0][0] != 0 or points[-1][0] != 6143:
        raise ValueError('TGC 必须覆盖原始采样点 0～6143')
    lut = result['lut']
    if not isinstance(lut, list) or len(lut) != 256 or any(type(x) is not int or not 0 <= x <= 255 for x in lut):
        raise ValueError('LUT 必须包含 256 个 0～255 的整数')
    angles = result['angles_deg']
    if not isinstance(angles, list) or len(angles) != 64:
        raise ValueError('需要 64 条线的角度')
    for i, angle in enumerate(angles):
        number(angle, -89, 89, '角度')
        if i and angle <= angles[i-1]:
            raise ValueError('64 个角度必须严格递增')
    return result


def from_profile(profile, current):
    """Explicit user import only. Metadata is never proof of PS/PL state."""
    if not isinstance(profile, dict):
        raise ValueError('配置必须为对象')
    if set(profile)-{'viewer', 'bram'}:
        raise ValueError('PC 导入接口仅接受显示设置及角度表')
    bram = profile.get('bram', {})
    if not isinstance(bram, dict) or set(bram)-{'sin_beta'}:
        raise ValueError('PC 导入不接受其他硬件表')
    result = deepcopy(current)
    viewer = profile.get('viewer', {})
    if not isinstance(viewer, dict) or set(viewer)-FIELDS:
        raise ValueError('未知显示配置字段')
    result.update(viewer)
    if 'angles_deg' not in viewer and 'sin_beta' in bram:
        values = bram['sin_beta']
        if not isinstance(values, list) or len(values) != 64:
            raise ValueError('sin_beta 必须有 64 个字')
        angles = []
        for raw in values:
            if isinstance(raw, str):
                raw = int(raw, 0)
            if type(raw) is not int or not 0 <= raw <= 0xffffffff:
                raise ValueError('sin_beta 必须为 32 位无符号传输字')
            value = raw & 0xfffff
            if raw >> 20 not in (0, 0xfff) or (raw >> 20 == 0xfff and not value & 0x80000):
                raise ValueError('sin_beta 不是合法的 20 位有符号编码')
            if value & 0x80000:
                value -= 1 << 20
            sine = value / (1 << 18)
            if not -1 < sine < 1:
                raise ValueError('sin_beta 超出有效正弦范围')
            angles.append(math.degrees(math.asin(sine)))
        result['angles_deg'] = angles
    return validate(result)
