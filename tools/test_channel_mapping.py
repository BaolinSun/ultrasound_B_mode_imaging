#!/usr/bin/env python3
"""Self-check the TX mapping and the preserved GUI/boot profile split."""

from __future__ import annotations

import json
import hashlib
import math
import re
from pathlib import Path

from generate_channel_map import ChannelMapRow, load_and_validate_map


PROJECT_ROOT = Path(__file__).resolve().parents[1]
LEGACY_GUI_PROFILE_SHA256 = "186b9ea59b61600ec59728cef722c10c6d11c385f25bfb980a9d3fda81eebe4f"
WORDS_PER_APERTURE = 128
GRP1_ADDR = (40, 41, 42, 43, 44, 45, 46, 47)
GRP1_HREG = (16, 12, 8, 4, 15, 11, 7, 3)
GRP1_LREG = (14, 10, 6, 2, 13, 9, 5, 1)
GRP2_ADDR = (32, 33, 34, 35, 36, 37, 38, 39)
GRP2_HREG = (32, 28, 24, 20, 31, 27, 23, 19)
GRP2_LREG = (30, 26, 22, 18, 29, 25, 21, 17)


def physical_delays(offset_us: float) -> list[list[int]]:
    pitch = 0.3e-3
    element_count = 64
    sound_speed = 1540.0
    focus_distance = 80e-3
    element_x = [
        (index - (element_count - 1) / 2) * pitch
        for index in range(element_count)
    ]
    result = []
    for transmit in range(64):
        angle = math.radians(-45.0 + 90.0 * transmit / 63.0)
        focus_x = focus_distance * math.sin(angle)
        focus_z = focus_distance * math.cos(angle)
        row = []
        for coordinate in element_x:
            distance = math.sqrt((coordinate - focus_x) ** 2 + focus_z**2)
            delay = offset_us * 1e-6 - (distance - focus_distance) / sound_speed
            row.append(math.floor(delay / 10e-9))
        result.append(row)
    return result


def map_to_hardware(values: list[int], channel_map: list[ChannelMapRow]) -> list[int]:
    hardware = [0] * 64
    for row in channel_map:
        hardware[row.tx_global_channel - 1] = values[row.physical_element - 1]
    return hardware


def pack_pair(high_delay: int, low_delay: int) -> int:
    return ((high_delay & 0xFFFF) << 16) | (low_delay & 0xFFFF)


def pack_profile(offset_us: float, channel_map: list[ChannelMapRow]) -> list[int]:
    packed = []
    for physical in physical_delays(offset_us):
        hardware = map_to_hardware(physical, channel_map)
        chip1 = hardware[:32]
        chip2 = hardware[32:]
        for aperture in range(2):
            transaction = []
            for register_index in range(16):
                if register_index < 8:
                    index = register_index
                    words = [
                        GRP1_ADDR[index],
                        pack_pair(chip1[GRP1_HREG[index] - 1], chip1[GRP1_LREG[index] - 1]),
                        GRP2_ADDR[index],
                        pack_pair(chip1[GRP2_HREG[index] - 1], chip1[GRP2_LREG[index] - 1]),
                        GRP1_ADDR[index],
                        pack_pair(chip2[GRP1_HREG[index] - 1], chip2[GRP1_LREG[index] - 1]),
                        GRP2_ADDR[index],
                        pack_pair(chip2[GRP2_HREG[index] - 1], chip2[GRP2_LREG[index] - 1]),
                    ]
                elif register_index == 8:
                    if aperture == 0:
                        words = [26, 0, 26, 0, 26, 0xFFFFFFFF, 26, 0xFFFFFFFF]
                    else:
                        words = [26, 0xFFFFFFFF, 26, 0xFFFFFFFF, 26, 0, 26, 0]
                else:
                    words = [0] * 8
                if register_index == 15:
                    words[-1] = 9
                transaction.extend(words)
            assert len(transaction) == WORDS_PER_APERTURE
            packed.extend(transaction)
    assert len(packed) == 16384
    return packed


def read_c_profile() -> list[int]:
    path = PROJECT_ROOT / "build" / "src" / "ps" / "bram" / "delay_profile.c"
    return [int(token, 16) for token in re.findall(r"0x([0-9A-Fa-f]{8})", path.read_text())]


def read_json_profile() -> list[int]:
    path = PROJECT_ROOT / "tools" / "configs" / "delay_profile_default.json"
    payload = json.loads(path.read_text(encoding="utf-8"))
    return [int(word, 16) for word in payload["bram"]["delay_profile"]]


def main() -> None:
    channel_map = load_and_validate_map()
    boot_profile = read_c_profile()
    gui_profile = read_json_profile()
    assert boot_profile == pack_profile(6.0, channel_map), "6 us boot C table is stale"
    gui_path = PROJECT_ROOT / "tools" / "configs" / "delay_profile_default.json"
    gui_digest = hashlib.sha256(gui_path.read_bytes()).hexdigest()
    assert gui_digest == LEGACY_GUI_PROFILE_SHA256, "legacy PC/GUI JSON changed"
    assert len(gui_profile) == 16384, "legacy PC/GUI JSON must contain 16384 words"
    assert boot_profile != gui_profile, "legacy GUI and 6 us boot profiles unexpectedly match"
    print("PASS: TX mapping, packing, 6 us boot table, and preserved legacy GUI table")


if __name__ == "__main__":
    main()
