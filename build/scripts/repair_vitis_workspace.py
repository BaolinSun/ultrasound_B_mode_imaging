"""Repair/build the EXISTING IDE BSP/app through the IDE's build backends.

Works while the IDE owns its workspace lock. No component recreation, server,
source import, launch/download, or changes to debug settings.
"""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import yaml

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))
from uart_bsp_compat import configure_uart_bsp

ROOT = SCRIPT_DIR.parents[1]
WORKSPACE = Path(os.environ.get("ULTRASOUND_REPAIR_WORKSPACE", str(ROOT / "workspace"))).resolve()
if not (WORKSPACE / "ubmi_p/vitis-comp.json").is_file():
    raise RuntimeError("Existing IDE platform ubmi_p is required; refusing to create/delete workspace")
config = (WORKSPACE / "B_mode_focus_imaging/src/UserConfig.cmake").read_text(encoding="utf-8")
if "-Wl,--wrap=outbyte" not in config:
    raise RuntimeError("Application must enable --wrap=outbyte before this repair")
tools = Path(os.environ.get("AMD_DESIGN_TOOLS_2026_1", "D:/AMDDesignTools/2026.1"))
cmake = tools / "tps/win64/cmake-4.0.1/bin/cmake.exe"
empyro = tools / "Vitis/bin/empyro.bat"
os.environ["PATH"] = str(tools / "Vitis/gnu/aarch32/nt/gcc-arm-none-eabi/bin") + os.pathsep + str(tools / "Vitis/bin") + os.pathsep + os.environ["PATH"]


def run_command(*args):
    print("VITIS_REPAIR: " + " ".join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), check=True)


print(f"VITIS_REPAIR: existing workspace {WORKSPACE}; no IDE lock/server access", flush=True)
bsp = WORKSPACE / "ubmi_p/ps7_cortexa9_0/standalone_ps7_cortexa9_0/bsp"
domain_config = bsp / "bsp.yaml"
metadata = yaml.safe_load(domain_config.read_text(encoding="utf-8"))
metadata["os_config"]["standalone"]["standalone_stdout"]["value"] = "ps7_uart_0"
metadata["os_config"]["standalone"]["standalone_stdin"]["value"] = "ps7_uart_1"
domain_config.write_text(yaml.safe_dump(metadata, sort_keys=False), encoding="utf-8")
configure_uart_bsp(WORKSPACE)
bsp_build = bsp / "libsrc/build_configs/gen_bsp"
run_command(cmake, "-S", bsp, "-B", bsp_build, "-Dstandalone_stdout=ps7_uart_0", "-Dstandalone_stdin=ps7_uart_1")
run_command(cmake, "--build", bsp_build, "--parallel", "2")
run_command(cmake, "--install", bsp_build)
export = WORKSPACE / "ubmi_p/export/ubmi_p/sw/standalone_ps7_cortexa9_0"
for name in ("include", "lib"):
    shutil.copytree(bsp / name, export / name, dirs_exist_ok=True)
header = (export / "include/bspconfig.h").read_text(encoding="utf-8")
if "#define STDOUT_BASEADDRESS 0xe0000000" not in header or "#define STDIN_BASEADDRESS 0xe0001000" not in header:
    raise RuntimeError("Exported BSP stdout/stdin mismatch")
app = WORKSPACE / "B_mode_focus_imaging"
run_command(cmake, "--build", app / "build", "--target", "clean")
# Exactly the build_app backend used by the Vitis IDE, twice to check re-build.
for _ in range(2):
    run_command("cmd.exe", "/C", empyro, "build_app", "-s", app / "src", "-b", app / "build")
print(f"VITIS_WORKSPACE_REPAIR_COMPLETE: {app / 'build/B_mode_focus_imaging.elf'}", flush=True)
