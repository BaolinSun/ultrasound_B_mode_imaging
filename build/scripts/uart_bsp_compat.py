"""Idempotent, project-local 2026.1 standalone stdout workaround.

Character redirection uses the application --wrap link flag, not a patched
UART driver. A regenerated BSP may therefore keep its strong outbyte safely.
"""
from pathlib import Path


def configure_uart_bsp(workspace, platform_name="ubmi_p", domain_name="standalone_ps7_cortexa9_0"):
    libsrc = Path(workspace) / platform_name / "ps7_cortexa9_0" / domain_name / "bsp/libsrc"
    cmake_file = libsrc / "standalone/src/xilstandalone.cmake"
    text = cmake_file.read_text(encoding="utf-8")
    marker = "# Ultrasound local fix: honour standalone_stdout independently of stdin."
    if marker in text:
        return False
    anchor = 'else()\n    set(FREERTOS_UARTCONFIG_INCLUDE "#include \\"FreeRTOSUARTConfig.h\\"")'
    if text.count(anchor) != 1:
        raise RuntimeError("2026.1 standalone console template changed; review UART BSP workaround")
    correction = marker + '''
if (standalone_stdout IN_LIST UARTPS_NUM_DRIVER_INSTANCES)
    set(index 0)
    LIST_INDEX(${index} ${standalone_stdout} "${UARTPS_NUM_DRIVER_INSTANCES}")
    list(GET TOTAL_UARTPS_PROP_LIST ${index} reg)
    set(STDOUT_BASEADDRESS ${${reg}})
endif()
'''
    cmake_file.write_text(text.replace(anchor, correction + anchor, 1), encoding="utf-8", newline="\n")
    return True
