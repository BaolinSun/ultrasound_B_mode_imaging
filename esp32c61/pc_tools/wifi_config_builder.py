"""Pure JSON command builders adapted from ultrasound_dev_kit (read-only reference)."""
from __future__ import annotations
import sys
import json
from pathlib import Path
from typing import Any
from wifi_dtgc import calc_dtgc_registers

LNA_GAIN = {18: 0b00, 21: 0b01, 15: 0b11}
PGA_GAIN = {24: 0b00, 27: 0b01, 21: 0b10}
LPF_PROG = {20: 0b00, 25: 0b01, 10: 0b10, 15: 0b11}
HPF_KHZ = {
    100: 0b00000,
    110: 0b00001,
    120: 0b00010,
    130: 0b00011,
    140: 0b00100,
    150: 0b00101,
    160: 0b00110,
    170: 0b00111,
    20: 0b01000,
    30: 0b01001,
    40: 0b01010,
    50: 0b01011,
    60: 0b01100,
    70: 0b01101,
    80: 0b01110,
    90: 0b01111,
    260: 0b10000,
    270: 0b10001,
    290: 0b10010,
    300: 0b10011,
    310: 0b10100,
    180: 0b11000,
    190: 0b11001,
    200: 0b11010,
    210: 0b11011,
    220: 0b11100,
    230: 0b11101,
    240: 0b11110,
    250: 0b11111,
}
DHPF_REGS = {0: 0x15, 1: 0x21, 2: 0x2D, 3: 0x39}
DTGC_PROFILE_BASE = {0: 0xA1, 1: 0xA6, 2: 0xAB, 3: 0xB0}
DTGC_MEMORY_WORDS = 160
DTGC_CALC_FLOAT_KEYS = (
    "adc_clk_hz",
    "sound_speed",
    "dtgc_start_depth_cm",
    "max_gain_depth_cm",
    "ramp_down_depth_cm",
)
DTGC_CALC_INT_KEYS = (
    "lna_gain_db",
    "pga_gain_db",
    "start_gain_code",
    "stop_gain_code",
    "pos_step_code",
    "neg_step_code",
    "slope_fac",
    "mem_bank",
)
DTGC_CALC_KEYS = DTGC_CALC_FLOAT_KEYS + DTGC_CALC_INT_KEYS
VALID_MAPS = {"GLOBAL", "ADC", "VCA", "DTGC"}
BRAM_SECTIONS = {
    "fdemod_sin": 8192,
    "fdemod_cos": 8192,
    "log_table": 1024,
    "x_element": 64,
    "hamming": 64,
    "dfilter": 64,
    "t_timing": 10,
    "sin_beta": 64,
    "delay_profile": 16384,
}
TX7332_SECTIONS = {"tx7332_config_data": 128}
BRAM_TARGET_ORDER = (
    "delay_profile",
    "demod_coeffs",
    "log_table",
    "x_element",
    "hamming",
    "dfilter",
    "t_timing",
    "sin_beta",
)
def parse_int(value: Any, name: str) -> int:
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        return int(value, 0)
    raise ValueError(f"{name} must be an integer or integer string")


def parse_float(value: Any, name: str) -> float:
    if isinstance(value, (int, float)):
        return float(value)
    if isinstance(value, str):
        return float(value)
    raise ValueError(f"{name} must be a number or numeric string")


def require_range(value: int, bits: int, name: str) -> int:
    limit = (1 << bits) - 1
    if not 0 <= value <= limit:
        raise ValueError(f"{name} must fit in {bits} bits, got {value}")
    return value


def add_reg(commands: list[str], reg_map: str, addr: int, data: int) -> None:
    reg_map = reg_map.upper()
    if reg_map not in VALID_MAPS:
        raise ValueError(f"invalid AFE register map: {reg_map}")
    require_range(addr, 8, "AFE address")
    require_range(data, 16, "AFE data")
    commands.append(f"AFE WR {reg_map} 0x{addr:02X} 0x{data:04X}")


def build_vca_commands(afe: dict[str, Any], commands: list[str]) -> None:
    vca = afe.get("vca")
    if not vca:
        return

    lna_db = parse_int(vca.get("lna_gain_db", 18), "lna_gain_db")
    pga_db = parse_int(vca.get("pga_gain_db", 24), "pga_gain_db")
    lpf_mhz = parse_int(vca.get("lpf_mhz", 20), "lpf_mhz")
    hpf_khz = parse_int(vca.get("lna_hpf_khz", 100), "lna_hpf_khz")
    power_mode = str(vca.get("power_mode", "low_noise")).lower()

    if lna_db not in LNA_GAIN:
        raise ValueError("lna_gain_db must be one of 15, 18, 21")
    if pga_db not in PGA_GAIN:
        raise ValueError("pga_gain_db must be one of 21, 24, 27")
    if lpf_mhz not in LPF_PROG:
        raise ValueError("lpf_mhz must be one of 10, 15, 20, 25")
    if hpf_khz not in HPF_KHZ:
        raise ValueError("lna_hpf_khz is not supported by AFE5832 register C7")
    if power_mode not in {"low_noise", "low_power"}:
        raise ValueError("power_mode must be low_noise or low_power")
    if power_mode == "low_power" and lpf_mhz == 25:
        raise ValueError("25 MHz LPF is not supported in low-power mode")

    lpf1 = LPF_PROG[lpf_mhz]
    lpf2 = 0 if power_mode == "low_power" else lpf1
    c7 = (
        (lpf2 << 14)
        | (HPF_KHZ[hpf_khz] << 9)
        | (lpf1 << 5)
        | (PGA_GAIN[pga_db] << 2)
        | LNA_GAIN[lna_db]
    )
    add_reg(commands, "VCA", 0xC7, c7)
    add_reg(commands, "VCA", 0xC8, 1 if power_mode == "low_power" else 0)


def build_dhpf_commands(afe: dict[str, Any], commands: list[str]) -> None:
    dhpf = afe.get("dhpf")
    if not dhpf:
        return
    for item in dhpf.get("groups", []):
        group = parse_int(item.get("group"), "dhpf.group")
        if group not in DHPF_REGS:
            raise ValueError("dhpf group must be 0, 1, 2, or 3")
        enable = 1 if item.get("enable", True) else 0
        corner = require_range(parse_int(item.get("corner_code", 3), "corner_code"), 4, "corner_code")
        round_enable = 1 if item.get("round_enable", False) else 0
        data = (round_enable << 5) | (corner << 1) | enable
        add_reg(commands, "ADC", DHPF_REGS[group], data)


def build_dtgc_commands(afe: dict[str, Any], commands: list[str]) -> None:
    dtgc = afe.get("dtgc")
    if not dtgc:
        return
    if has_dtgc_calc_params(dtgc):
        build_dtgc_internal_nonuniform_commands(dtgc, commands)
        return

    for profile in dtgc.get("profiles", []):
        idx = parse_int(profile.get("profile"), "dtgc.profile")
        if idx not in DTGC_PROFILE_BASE:
            raise ValueError("dtgc profile must be 0, 1, 2, or 3")
        base = DTGC_PROFILE_BASE[idx]
        start_gain = require_range(parse_int(profile.get("start_gain", 0), "start_gain"), 8, "start_gain")
        stop_gain = require_range(parse_int(profile.get("stop_gain", 0x90), "stop_gain"), 8, "stop_gain")
        pos_step = require_range(parse_int(profile.get("pos_step", 0), "pos_step"), 5, "pos_step")
        pos_step_feq = require_range(parse_int(profile.get("pos_step_feq", 0), "pos_step_feq"), 3, "pos_step_feq")
        neg_step = require_range(parse_int(profile.get("neg_step", 0x1F), "neg_step"), 5, "neg_step")
        neg_step_feq = require_range(parse_int(profile.get("neg_step_feq", 0x07), "neg_step_feq"), 3, "neg_step_feq")
        add_reg(commands, "DTGC", base + 0, (start_gain << 8) | stop_gain)
        add_reg(commands, "DTGC", base + 1, (pos_step << 11) | (pos_step_feq << 8) | (neg_step << 3) | neg_step_feq)

        # Optional extended profile fields write the following DTGC registers.
        if any(key in profile for key in ("start_index", "stop_index", "start_gain_time", "hold_gain_time")):
            start_index = require_range(parse_int(profile.get("start_index", 0), "start_index"), 8, "start_index")
            stop_index = require_range(parse_int(profile.get("stop_index", 0x90), "stop_index"), 8, "stop_index")
            start_gain_time = require_range(
                parse_int(profile.get("start_gain_time", 0), "start_gain_time"), 16, "start_gain_time"
            )
            hold_gain_time = require_range(
                parse_int(profile.get("hold_gain_time", 0), "hold_gain_time"), 16, "hold_gain_time"
            )
            add_reg(commands, "DTGC", base + 2, (start_index << 8) | stop_index)
            add_reg(commands, "DTGC", base + 3, start_gain_time)
            add_reg(commands, "DTGC", base + 4, hold_gain_time)

    for reg in dtgc.get("raw_registers", []):
        add_reg(
            commands,
            "DTGC",
            parse_int(reg.get("addr"), "dtgc.raw_registers.addr"),
            parse_int(reg.get("data"), "dtgc.raw_registers.data"),
        )


def has_dtgc_calc_params(dtgc: Any) -> bool:
    return isinstance(dtgc, dict) and all(key in dtgc for key in DTGC_CALC_KEYS)


def build_dtgc_internal_nonuniform_commands(dtgc: dict[str, Any], commands: list[str]) -> None:
    kwargs: dict[str, Any] = {}
    for key in DTGC_CALC_FLOAT_KEYS:
        kwargs[key] = parse_float(dtgc[key], f"dtgc.{key}")
    for key in DTGC_CALC_INT_KEYS:
        kwargs[key] = parse_int(dtgc[key], f"dtgc.{key}")

    cfg = calc_dtgc_registers(**kwargs)
    registers = cfg["registers"]
    memory_words = cfg["memory_words"]
    if len(memory_words) > DTGC_MEMORY_WORDS:
        raise ValueError(f"dtgc.memory_words must contain at most {DTGC_MEMORY_WORDS} words")

    # Internal Non-Uniform Mode: program memory bank first, then profile timing.
    add_reg(commands, "DTGC", 0xB5, parse_int(registers["B5"], "dtgc.registers.B5"))
    for idx in range(DTGC_MEMORY_WORDS):
        word = memory_words[idx] if idx < len(memory_words) else 0
        add_reg(commands, "DTGC", 0x01 + idx, parse_int(word, f"dtgc.memory_words[{idx}]"))
    for reg_name in ("A1", "A2", "A3", "A4", "A5", "B6", "B7"):
        add_reg(commands, "DTGC", int(reg_name, 16), parse_int(registers[reg_name], f"dtgc.registers.{reg_name}"))


def build_raw_commands(afe: dict[str, Any], commands: list[str]) -> None:
    for reg in afe.get("raw_registers", []):
        add_reg(
            commands,
            str(reg.get("map", "")).upper(),
            parse_int(reg.get("addr"), "raw_registers.addr"),
            parse_int(reg.get("data"), "raw_registers.data"),
        )


def build_afe_commands(config: dict[str, Any]) -> list[str]:
    afe = config.get("afe5832", {})
    commands: list[str] = []
    if "input_impedance_ohm" in afe:
        print(
            "note: afe5832.input_impedance_ohm is recorded only; "
            "AFE5832LP has no direct input-impedance register in this interface.",
            file=sys.stderr,
        )
    build_vca_commands(afe, commands)
    build_dhpf_commands(afe, commands)
    build_dtgc_commands(afe, commands)
    build_raw_commands(afe, commands)
    return commands


def parse_bram_word(value: Any, name: str) -> int:
    word = parse_int(value, name)
    require_range(word, 32, name)
    return word


def build_bram_commands(config: dict[str, Any], chunk_words: int) -> list[str]:
    if not 1 <= chunk_words <= 8:
        raise ValueError("--chunk-words must be in the range 1..8")

    bram = config.get("bram", {})
    commands: list[str] = []
    for section, expected_len in BRAM_SECTIONS.items():
        if section not in bram:
            continue
        values = bram[section]
        if not isinstance(values, list):
            raise ValueError(f"bram.{section} must be a list")
        if len(values) != expected_len:
            raise ValueError(f"bram.{section} must contain {expected_len} words, got {len(values)}")
        words = [parse_bram_word(value, f"bram.{section}[{idx}]") for idx, value in enumerate(values)]
        for offset in range(0, len(words), chunk_words):
            chunk = words[offset : offset + chunk_words]
            payload = " ".join(f"0x{word:08X}" for word in chunk)
            commands.append(f"BRAM WR {section} {offset} {len(chunk)} {payload}")

    unknown = sorted(set(bram) - set(BRAM_SECTIONS))
    if unknown:
        raise ValueError(f"unknown BRAM section(s): {', '.join(unknown)}")
    return commands


def build_tx7332_commands(config: dict[str, Any], chunk_words: int) -> list[str]:
    if not 1 <= chunk_words <= 8:
        raise ValueError("--chunk-words must be in the range 1..8")

    tx7332 = config.get("tx7332", {})
    commands: list[str] = []
    for section, expected_len in TX7332_SECTIONS.items():
        if section not in tx7332:
            continue
        values = tx7332[section]
        if not isinstance(values, list):
            raise ValueError(f"tx7332.{section} must be a list")
        if len(values) != expected_len:
            raise ValueError(f"tx7332.{section} must contain {expected_len} words, got {len(values)}")
        words = [parse_bram_word(value, f"tx7332.{section}[{idx}]") for idx, value in enumerate(values)]
        for offset in range(0, len(words), chunk_words):
            chunk = words[offset : offset + chunk_words]
            payload = " ".join(f"0x{word:08X}" for word in chunk)
            commands.append(f"TX7332 WR {section} {offset} {len(chunk)} {payload}")

    unknown = sorted(set(tx7332) - set(TX7332_SECTIONS))
    if unknown:
        raise ValueError(f"unknown TX7332 section(s): {', '.join(unknown)}")
    return commands


def append_bram_config_commands(commands: list[str], config_path: Path, chunk_words: int) -> None:
    config = json.loads(config_path.read_text(encoding="utf-8"))
    commands.extend(build_bram_commands(config, chunk_words))
