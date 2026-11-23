#!/usr/bin/env python3
"""Check the ECU firmware image before it is flashed.

- No loadable bytes in the steering calibration region 0x0807F000..0x0807FFFF:
  flashing must never overwrite the stored calibration.
- The image fits the 508 KB application Flash region.

Usage: check_ecu_image.py path/to/vehicle-ecu.elf
Uses READELF from the environment (default arm-none-eabi-readelf).
"""
import os
import re
import subprocess
import sys

FLASH_START = 0x08000000
APP_FLASH_END = 0x0807F000          # 508 KB application region
CAL_START, CAL_END = 0x0807F000, 0x08080000


def load_segments(elf: str):
    readelf = os.environ.get("READELF", "arm-none-eabi-readelf")
    out = subprocess.run([readelf, "-lW", elf], check=True,
                         capture_output=True, text=True).stdout
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 6 and parts[0] == "LOAD":
            phys, file_size = int(parts[3], 16), int(parts[4], 16)
            if file_size > 0:
                yield phys, phys + file_size


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    errors = []
    flash_end = FLASH_START
    for start, end in load_segments(sys.argv[1]):
        if start < CAL_END and end > CAL_START:
            errors.append(f"segment 0x{start:08X}-0x{end:08X} overlaps calibration region")
        if FLASH_START <= start < CAL_END:
            flash_end = max(flash_end, end)
    used = flash_end - FLASH_START
    total = APP_FLASH_END - FLASH_START
    print(f"Flash image: {used} B of {total} B ({100.0 * used / total:.1f} %)")
    if flash_end > APP_FLASH_END:
        errors.append("image exceeds the 508 KB application region")
    for error in errors:
        print(f"ERROR: {error}")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
