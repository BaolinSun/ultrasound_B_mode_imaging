#!/usr/bin/env python3
"""Zynq commands over ESP32 HTTP. No automatic command replay or HV enable."""
from __future__ import annotations
import argparse
from datetime import datetime
import ipaddress
import json
import math
from pathlib import Path
import re
import secrets
import sys
import time
from urllib.error import HTTPError
from urllib.request import Request, urlopen

import wifi_config_builder as builder
from log_paths import LOG_HELP, resolve_log_path

TARGETS = ("afe5832", "tx7332", *builder.BRAM_TARGET_ORDER)


def number(text, maximum=0xFFFFFFFF):
    if not re.fullmatch(r"(?:0[xX][0-9a-fA-F]+|[0-9]+)", text):
        raise ValueError("expected unsigned decimal or 0x hexadecimal integer")
    value = int(text, 16 if text.lower().startswith("0x") else 10)
    if not 0 <= value <= maximum:
        raise ValueError(f"value out of range 0..{maximum}")
    return value


def validate_command(line):
    """Canonical grammar mirrored by shared PS/ESP C validation."""
    if not isinstance(line, str) or not 1 <= len(line) <= 159 or not line.isascii():
        raise ValueError("command must contain 1..159 ASCII characters")
    if not re.fullmatch(r"[!-~]+(?: [!-~]+)*", line) or "*" in line:
        raise ValueError("command must use single spaces and contain no control characters or '*'")
    t = line.split(" ")
    if t == ["STATUS"]:
        return line
    if len(t) >= 2:
        root, op = t[:2]
        if root == "CFG":
            if op == "BEGIN" and len(t) == 3:
                number(t[2]); return line
            if op in ("END", "ABORT") and len(t) == 2:
                return line
        if root == "BEAM" and op in ("START", "STOP") and len(t) == 2:
            return line
        if root == "HV" and op in ("ENABLE", "DISABLE") and len(t) == 2:
            return line
        if root == "GAIN" and op == "SET" and len(t) == 3:
            number(t[2], 8); return line
        if root in ("AFE", "TX7332") and len(t) == 2 and op in ("SLEEP", "WAKE", "STATUS"):
            return
        if root == "AFE" and len(t) in (4, 5) and t[2] in builder.VALID_MAPS:
            number(t[3], 255)
            if op == "RD" and len(t) == 4:
                return line
            if op == "WR" and len(t) == 5:
                number(t[4], 65535); return line
        if root == "TX7332" and op == "RD" and len(t) == 3:
            number(t[2], 1023); return line
        if root in ("BRAM", "TX7332") and op == "WR" and len(t) >= 6:
            depths = builder.BRAM_SECTIONS if root == "BRAM" else builder.TX7332_SECTIONS
            if t[2] not in depths:
                raise ValueError("unknown section")
            offset, count = number(t[3]), number(t[4], 8)
            if not count or len(t) != 5 + count or offset + count > depths[t[2]]:
                raise ValueError("write range/count mismatch")
            for i, word in enumerate(t[5:]):
                value = number(word)
                if root == "BRAM" and t[2] == "t_timing" and ((offset + i == 0 and value != 6144) or (offset + i == 9 and value > 4)):
                    raise ValueError("FRAME_LAYOUT: raw samples must be 6144 and decimation must be 0..4")
            return line
    raise ValueError("unsupported command or argument count")


def make_config(args):
    paths = {target: getattr(args, target + "_config") for target in TARGETS}
    if args.config:
        if args.target == "all":
            raise ValueError("--target all requires explicit per-target --*-config paths")
        if paths[args.target]:
            raise ValueError("use either --config or the target-specific config path")
        paths[args.target] = args.config
    selected = (*builder.BRAM_TARGET_ORDER, "tx7332", "afe5832") if args.target == "all" else (args.target,)
    commands = []
    for target in selected:
        path = paths[target]
        if path is None:
            raise ValueError(f"explicit --{target.replace('_', '-')}-config or --config required")
        data = json.loads(path.read_text(encoding="utf-8-sig"))
        if not isinstance(data, dict):
            raise ValueError("configuration JSON must be an object")
        if target == "afe5832":
            if not isinstance(data.get("afe5832"), dict) or not data["afe5832"]:
                raise ValueError("nonempty afe5832 object required")
            generated = builder.build_afe_commands(data)
        elif target == "tx7332":
            generated = builder.build_tx7332_commands(data, args.chunk_words)
        else:
            sections = ("fdemod_sin", "fdemod_cos") if target == "demod_coeffs" else (target,)
            bram = data.get("bram")
            if not isinstance(bram, dict) or set(bram) != set(sections):
                raise ValueError(f"{target} JSON must contain precisely these bram sections: {sections}")
            generated = builder.build_bram_commands(data, args.chunk_words)
        if not generated:
            raise ValueError(f"configuration {target} produces no commands")
        commands.extend(generated)
    return [f"CFG BEGIN {secrets.randbits(32)}", *commands, "CFG END"]


class ControlFailure(RuntimeError):
    def __init__(self, message, unknown=False):
        super().__init__(message)
        self.unknown = unknown


class Client:
    def __init__(self, device, client_id=None, timeout=300, opener=urlopen):
        self.base = "http://" + str(ipaddress.IPv4Address(device))
        self.client = client_id or f"{secrets.randbelow(0xFFFFFFFF) + 1:08x}"
        if not re.fullmatch(r"[0-9a-fA-F]{8}", self.client) or int(self.client, 16) == 0:
            raise ValueError("client-id must be eight hexadecimal characters and nonzero")
        self.request = secrets.randbelow(0xFFFFFF00) + 1
        self.timeout, self.opener = timeout, opener
        self.history = []
        self.config_active = False

    def api(self, path, payload=None):
        headers = {}
        body = None
        if payload is not None:
            headers["Content-Type"] = "application/json"
            body = json.dumps(payload).encode("ascii")
        try:
            with self.opener(Request(self.base + path, data=body, headers=headers), timeout=8) as response:
                return json.load(response)
        except HTTPError as error:
            # Do not replay commands when their execution outcome is unknown.
            unknown = payload is None or error.code >= 500
            raise ControlFailure(f"HTTP {error.code}; " + ("outcome unknown, not replayed" if unknown else "control request rejected"), unknown=unknown) from None
        except (OSError, ValueError):
            raise ControlFailure("HTTP connection/response failure; outcome unknown, not replayed", unknown=True) from None

    def batch(self, commands, allow_hv=False):
        self.request += 1
        if self.request > 0xFFFFFFFF:
            raise ControlFailure("request counter exhausted")
        request_id = f"{self.request:08x}"
        entry = {"client_id": self.client, "request_id": request_id, "commands": commands}
        self.history.append(entry)
        accepted = self.api("/api/zynq/commands", {**entry, "allow_hv": allow_hv})
        if not isinstance(accepted, dict):
            raise ControlFailure("invalid submit response; outcome unknown", unknown=True)
        job_id = accepted.get("job_id", "")
        if not re.fullmatch(r"[0-9a-fA-F]{8}", job_id):
            raise ControlFailure("invalid job ID; outcome unknown", unknown=True)
        entry["job_id"] = job_id
        until = time.monotonic() + self.timeout
        while True:
            result = self.api("/api/zynq/jobs/" + job_id)
            if not isinstance(result, dict) or not isinstance(result.get("busy"), bool):
                raise ControlFailure("invalid job response; outcome unknown", unknown=True)
            if not result.get("busy"):
                items = result.get("results")
                if (not isinstance(items, list) or len(items) != len(commands) or
                        any(not isinstance(item, dict) or item.get("command") != commands[i] or
                            not isinstance(item.get("response"), str) for i, item in enumerate(items))):
                    raise ControlFailure("job command/result mismatch; outcome unknown", unknown=True)
                entry["result"] = result
                for item in result.get("results", []):
                    if item.get("response", "").startswith("@ACK "):
                        if item["command"].startswith("CFG BEGIN "):
                            self.config_active = True
                        elif item["command"] in ("CFG END", "CFG ABORT"):
                            self.config_active = False
                if result.get("state") != "succeeded":
                    raise ControlFailure(result.get("error") or result.get("state") or "command failed",
                                         unknown=result.get("state") == "unknown")
                return result
            if time.monotonic() >= until:
                raise ControlFailure("job wait timeout; outcome unknown, not replayed", unknown=True)
            time.sleep(0.1)


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--device-ip", required=True, type=ipaddress.IPv4Address)
    mode = p.add_mutually_exclusive_group(required=True)
    mode.add_argument("--command", "-c", action="append", help="Single command; repeat for an ordered list")
    mode.add_argument("--file", type=Path, help="ASCII command lines; blank lines and # comments ignored")
    mode.add_argument("--status", action="store_true")
    mode.add_argument("--target", choices=(*TARGETS, "all"))
    p.add_argument("--config", type=Path)
    for target in TARGETS:
        p.add_argument("--" + target.replace("_", "-") + "-config", type=Path)
    p.add_argument("--chunk-words", type=int, choices=range(1, 9), default=8)
    p.add_argument("--allow-hv", action="store_true", help="Explicitly permit HV ENABLE; never enabled implicitly")
    p.add_argument("--dry-run", action="store_true", help="Validate/print all commands; no HTTP")
    p.add_argument("--client-id", help="Optional eight-hex-digit ID for an explicitly managed multi-call transaction")
    p.add_argument("--timeout", type=float, default=300, help="Per-batch wait deadline, seconds")
    p.add_argument("--log", type=Path, help=LOG_HELP)
    args = p.parse_args(argv)
    try:
        if not math.isfinite(args.timeout) or not 1 <= args.timeout <= 3600:
            raise ValueError("timeout must be 1..3600 seconds")
        commands = make_config(args) if args.target else ["STATUS"] if args.status else (
            [line.strip() for line in args.file.read_text(encoding="ascii").splitlines()
             if line.strip() and not line.lstrip().startswith("#")] if args.file else args.command)
        if not commands:
            raise ValueError("no commands")
        for line in commands:
            validate_command(line)
        if "HV ENABLE" in commands and not args.allow_hv:
            raise ValueError("HV ENABLE requires --allow-hv (including dry-run)")
    except (ValueError, OSError, TypeError, KeyError) as error:
        p.error(str(error))
    if args.dry_run:
        print("\n".join(commands))
        return 0
    log = resolve_log_path(args.log or Path(datetime.now().strftime("zynq-control-%Y%m%d-%H%M%S-") + secrets.token_hex(4) + ".json"))
    log.parent.mkdir(parents=True, exist_ok=True)
    # Reserve the report before touching hardware. Existing reports are never overwritten.
    with log.open("x", encoding="utf-8") as output:
        client = None
        report = {"passed": False, "device_ip": str(args.device_ip), "commands": len(commands)}
        try:
            client = Client(args.device_ip, args.client_id, args.timeout)
            for offset in range(0, len(commands), 8):
                result = client.batch(commands[offset:offset + 8], args.allow_hv)
                for item in result["results"]:
                    print(item["response"])
            report["passed"] = True
        except (ControlFailure, ValueError, KeyboardInterrupt, EOFError) as error:
            unknown = isinstance(error, (KeyboardInterrupt, EOFError)) or getattr(error, "unknown", False)
            report.update(error="Interrupted" if isinstance(error, KeyboardInterrupt) else str(error), outcome="unknown" if unknown else "failed")
            if client and client.config_active and not unknown:
                try:
                    client.batch(["CFG ABORT"])
                except ControlFailure:
                    report["cleanup"] = "abort not confirmed; PS transaction watchdog remains active"
            print("Control failed; see report. Unknown outcomes are never replayed.", file=sys.stderr)
        finally:
            report["batches"] = client.history if client else []
            json.dump(report, output, ensure_ascii=False, indent=2)
        print(f"Report: {log.resolve()}")
        return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
