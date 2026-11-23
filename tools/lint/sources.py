#!/usr/bin/env python3
"""Lists the project's own C sources: the code that clang-format and cppcheck check.

Generated or vendor code is left out: CubeMX files (main.c, HAL MSP/IT, system, syscalls,
app_freertos.c), HAL, CMSIS, FreeRTOS, SEGGER RTT, ESP-IDF, LVGL and the display unit
prototype (its generated fonts and images).

usage: tools/lint/sources.py [--group ecu|gateway|shared|tests]   (paths relative to repo root)
"""
import argparse
import glob
import os

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

GROUPS = {
    "ecu": ["firmware/vehicle-ecu/Core/Src/*.c", "firmware/vehicle-ecu/Core/Inc/*.h"],
    "gateway": [
        "firmware/remote-gateway/main/*.[ch]",
        "firmware/remote-gateway/components/*/*.[ch]",
        "firmware/remote-gateway/components/*/include/*.h",
    ],
    "shared": ["shared/remote_protocol/*.[ch]"],
    "tests": [
        "tests/*.[ch]",
        "tests/fakes/*.[ch]",
        "firmware/remote-gateway/tests/*.[ch]",
    ],
}

# CubeMX-generated or vendor-configuration files inside Core/.
EXCLUDED = {
    "firmware/vehicle-ecu/Core/Src/main.c",
    "firmware/vehicle-ecu/Core/Src/app_freertos.c",
    "firmware/vehicle-ecu/Core/Src/stm32g4xx_hal_msp.c",
    "firmware/vehicle-ecu/Core/Src/stm32g4xx_hal_timebase_tim.c",
    "firmware/vehicle-ecu/Core/Src/stm32g4xx_it.c",
    "firmware/vehicle-ecu/Core/Src/syscalls.c",
    "firmware/vehicle-ecu/Core/Src/sysmem.c",
    "firmware/vehicle-ecu/Core/Src/system_stm32g4xx.c",
    "firmware/vehicle-ecu/Core/Inc/main.h",
    "firmware/vehicle-ecu/Core/Inc/FreeRTOSConfig.h",
    "firmware/vehicle-ecu/Core/Inc/SEGGER_RTT_Conf.h",
    "firmware/vehicle-ecu/Core/Inc/stm32g4xx_hal_conf.h",
    "firmware/vehicle-ecu/Core/Inc/stm32g4xx_it.h",
}


def sources(groups):
    found = set()
    for group in groups:
        for pattern in GROUPS[group]:
            for path in glob.glob(os.path.join(ROOT, pattern)):
                rel = os.path.relpath(path, ROOT)
                if rel not in EXCLUDED:
                    found.add(rel)
    return sorted(found)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--group", action="append", choices=sorted(GROUPS))
    args = parser.parse_args()
    for path in sources(args.group or sorted(GROUPS)):
        print(path)
