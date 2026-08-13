#!/usr/bin/env python3
"""Create the 2026.1 SDT platform, application, and FSBL from the new XSA."""

import os
import re
import shutil
import stat
import sys
import time
import json

import vitis


SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(SCRIPT_DIR))
WORKSPACE = os.path.join(ROOT, "workspace")
XSA = os.path.join(ROOT, "build", "output", "ultrasound_B_mode_imaging.xsa")
APP_SOURCE = os.path.join(ROOT, "build", "src", "ps")
OUTPUT = os.path.join(ROOT, "build", "output")
PLATFORM_NAME = "ubmi_p"
DOMAIN_NAME = "standalone_ps7_cortexa9_0"
APP_NAME = "B_mode_focus_imaging"
FSBL_NAME = "zynq_fsbl"
STAGE = os.environ.get("ULTRASOUND_VITIS_STAGE", "all").lower()


def newest_file(root, filename):
    matches = []
    for directory, _, files in os.walk(root):
        for item in files:
            if item.lower() == filename.lower():
                path = os.path.join(directory, item)
                matches.append((os.path.getmtime(path), path))
    if not matches:
        raise RuntimeError(f"{filename} was not produced below {root}")
    return max(matches)[1]


def remove_readonly(function, path, _excinfo):
    os.chmod(path, stat.S_IWRITE)
    function(path)


def remove_workspace(path):
    if not os.path.isdir(path):
        return
    last_error = None
    for _ in range(30):
        try:
            shutil.rmtree(path, onexc=remove_readonly)
            return
        except PermissionError as error:
            last_error = error
            time.sleep(1)
    raise RuntimeError(f"Vitis workspace remained locked: {path}") from last_error


def set_cmake_list(text, variable, values):
    body = "\n".join(f'"{value}"' for value in values)
    pattern = rf"set\({re.escape(variable)}\s*.*?\n\)"
    replacement = f"set({variable}\n{body}\n)"
    updated, count = re.subn(pattern, replacement, text, count=1, flags=re.DOTALL)
    if count != 1:
        raise RuntimeError(f"could not update {variable} in UserConfig.cmake")
    return updated


def configure_application_sources(component_source):
    sources = []
    include_dirs = {"."}
    excluded_sources = {
        # pus_ctrl_system has no AXI DMA and IRQ_F2P is tied low.  Preserve
        # the legacy source for reference, but do not compile dead hardware
        # paths against non-existent SDT macros.
        "data_transfer/axidma.c",
    }
    for directory, _, files in os.walk(component_source):
        relative_directory = os.path.relpath(directory, component_source).replace("\\", "/")
        if relative_directory != ".":
            include_dirs.add(relative_directory)
        for filename in files:
            if filename.lower().endswith((".c", ".s", ".S")):
                relative = os.path.relpath(
                    os.path.join(directory, filename), component_source
                ).replace("\\", "/")
                if relative in excluded_sources:
                    print(f"VITIS_MIGRATION: excluding unsupported source {relative}")
                    continue
                sources.append(relative)

    config_path = os.path.join(component_source, "UserConfig.cmake")
    with open(config_path, "r", encoding="utf-8") as handle:
        config = handle.read()
    config = set_cmake_list(config, "USER_COMPILE_SOURCES", sorted(sources))
    config = set_cmake_list(config, "USER_INCLUDE_DIRECTORIES", sorted(include_dirs))
    with open(config_path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(config)


def configure_debug_launch():
    """Correct the Vitis-generated Zynq PS-init Tcl selection.

    The legacy custom AFE driver contains a driver-generation Tcl file.  Vitis
    2026.1 copies it to _ide/psinit and can mistakenly select it instead of the
    Platform-generated ps7_init.tcl for a bare-metal Zynq launch.
    """
    launch_path = os.path.join(WORKSPACE, APP_NAME, "_ide", "launch.json")
    ps7_init_path = os.path.join(
        WORKSPACE, APP_NAME, "_ide", "psinit", "ps7_init.tcl"
    )
    if not os.path.isfile(launch_path):
        raise RuntimeError(f"Vitis did not generate a launch configuration: {launch_path}")
    if not os.path.isfile(ps7_init_path):
        raise RuntimeError(f"Vitis did not generate ps7_init.tcl: {ps7_init_path}")

    with open(launch_path, "r", encoding="utf-8") as handle:
        launch = json.load(handle)
    fixed = 0
    ps7_init_ref = f"${{workspaceFolder}}\\{APP_NAME}\\_ide\\psinit\\ps7_init.tcl"
    for configuration in launch.get("configurations", []):
        ps7_init = (
            configuration.get("targetSetup", {})
            .get("zynqInitialization", {})
            .get("usingPs7Init")
        )
        if ps7_init is not None:
            ps7_init["ps7InitTclFile"] = ps7_init_ref
            fixed += 1
    if fixed == 0:
        raise RuntimeError("Vitis launch configuration has no usingPs7Init entry")
    with open(launch_path, "w", encoding="utf-8", newline="\n") as handle:
        json.dump(launch, handle, indent=2)
        handle.write("\n")


if STAGE not in ("platform", "all"):
    raise RuntimeError("ULTRASOUND_VITIS_STAGE must be 'platform' or 'all'")
if not os.path.isfile(XSA):
    raise FileNotFoundError(XSA)

if os.path.isdir(WORKSPACE):
    remove_workspace(WORKSPACE)
os.makedirs(WORKSPACE, exist_ok=True)
os.makedirs(OUTPUT, exist_ok=True)

client = vitis.create_client()
try:
    client.set_workspace(WORKSPACE)
    print(f"VITIS_MIGRATION: creating platform from {XSA}")
    platform = client.create_platform_component(
        name=PLATFORM_NAME,
        hw_design=XSA,
    )
    platform.add_domain(
        name=DOMAIN_NAME,
        cpu="ps7_cortexa9_0",
        os="standalone",
    )
    platform.report()
    platform_result = platform.build()
    print(f"VITIS_MIGRATION: platform build result={platform_result!r}")
    if platform_result not in (None, 0):
        raise RuntimeError(f"platform build failed with result {platform_result!r}")

    if STAGE == "platform":
        print(f"VITIS_MIGRATION_COMPLETE: platform at {WORKSPACE}")
        sys.exit(0)

    platform_xpfm = client.find_platform_in_repos(PLATFORM_NAME)
    if not platform_xpfm:
        raise RuntimeError(f"built platform {PLATFORM_NAME} was not found")

    print("VITIS_MIGRATION: creating application")
    application = client.create_app_component(
        name=APP_NAME,
        platform=platform_xpfm,
        domain=DOMAIN_NAME,
        template="empty_application",
    )
    application.import_files(from_loc=APP_SOURCE, dest_dir_in_cmp="src")
    configure_application_sources(os.path.join(WORKSPACE, APP_NAME, "src"))
    linker = application.get_ld_script()
    linker.regenerate()
    linker.set_stack_size("0x2000")
    linker.set_heap_size("0x2000")
    app_result = application.build()
    if app_result not in (None, 0):
        raise RuntimeError(f"application build failed with result {app_result!r}")
    configure_debug_launch()

    app_elf = newest_file(WORKSPACE, f"{APP_NAME}.elf")
    fsbl_elf = newest_file(os.path.join(WORKSPACE, PLATFORM_NAME), "fsbl.elf")
    shutil.copy2(app_elf, os.path.join(OUTPUT, f"{APP_NAME}.elf"))
    shutil.copy2(fsbl_elf, os.path.join(OUTPUT, f"{FSBL_NAME}.elf"))
    print(f"VITIS_MIGRATION_COMPLETE: {app_elf}")
    print(f"VITIS_MIGRATION_COMPLETE: {fsbl_elf}")
finally:
    vitis.dispose()
