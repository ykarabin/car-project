#!/usr/bin/env python3
"""Static checks of the project's own C code: clang-format and cppcheck with the MISRA addon.

    tools/lint/check.py format              check formatting (add --fix to rewrite the files)
    tools/lint/check.py cppcheck            cppcheck + MISRA for ECU, shared protocol and Gateway
    tools/lint/check.py cppcheck --update-baseline

cppcheck rules:
* General checks (warning, style, performance, portability) must report nothing.
  Justified exceptions go inline as `/* cppcheck-suppress <id> */` with a reason.
* MISRA C:2012 findings are compared with tools/lint/misra-baseline.json, a count per rule
  and file. A count above the baseline fails; a count below asks to lower the baseline.
  Rules deviated for the whole project and known false positives (docs/dev/misra.md)
  are not counted.

The Gateway analysis needs firmware/remote-gateway/build/compile_commands.json, which any
`idf.py build` or `idf.py reconfigure` writes. Without it the Gateway part is skipped locally
and fails in CI (--require-gateway).
"""
import argparse
import collections
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import sources  # noqa: E402

CLANG_FORMAT_VERSION = "23.1"
CPPCHECK_VERSION = "2.22"
BASELINE = os.path.join(HERE, "misra-baseline.json")
GATEWAY_DB = os.path.join(ROOT, "firmware/remote-gateway/build/compile_commands.json")

# Project-wide deviations, justified in docs/dev/misra.md.
DEVIATED_RULES = {
    "misra-c2012-15.5",  # early return for input checks, fault exits and name tables
    "misra-c2012-8.7",   # module API kept external: called from one file and from host tests
    "misra-c2012-17.1",  # <stdarg.h> only in the printf-style debug console
    "misra-c2012-21.6",  # bounded snprintf/vsnprintf for diagnostics and the UART text protocol
    "misra-c2012-19.2",  # tagged union in vehicle_log_message_t
}

# Rules the MISRA addon reports wrongly for this code base (docs/dev/misra.md).
FALSE_POSITIVE_RULES = {
    "misra-c2012-2.5",  # config macros used by files of another analysis run
    "misra-c2012-5.6",  # remote_protocol.h typedefs seen twice
    "misra-c2012-5.7",  # remote_protocol.h tags seen twice
}
IGNORED_RULES = DEVIATED_RULES | FALSE_POSITIVE_RULES

ECU = os.path.join(ROOT, "firmware/vehicle-ecu")
ECU_ARGS = [
    "-DSTM32G473xx",
    "-DUSE_HAL_DRIVER",
    "-I" + os.path.join(ECU, "Core/Inc"),
    "-I" + os.path.join(ROOT, "shared/remote_protocol"),
    "-I" + os.path.join(ECU, "ThirdParty/SEGGER_RTT"),
    "-I" + os.path.join(ECU, "Drivers/STM32G4xx_HAL_Driver/Inc"),
    "-I" + os.path.join(ECU, "Drivers/STM32G4xx_HAL_Driver/Inc/Legacy"),
    "-I" + os.path.join(ECU, "Middlewares/Third_Party/FreeRTOS/Source/include"),
    "-I" + os.path.join(ECU, "Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2"),
    "-I" + os.path.join(ECU, "Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F"),
    "-I" + os.path.join(ECU, "Drivers/CMSIS/Device/ST/STM32G4xx/Include"),
    "-I" + os.path.join(ECU, "Drivers/CMSIS/Include"),
    "--platform=arm32-wchar_t4",
]
VENDOR_SUPPRESSIONS = [
    "--suppress=*:*/Drivers/*",
    "--suppress=*:*/Middlewares/*",
    "--suppress=*:*/ThirdParty/*",
    "--suppress=*:*/esp-idf/*",
    "--suppress=*:*/managed_components/*",
    "--suppress=*:*/build/*",
    "--suppress=missingIncludeSystem",
    "--suppress=checkersReport",
]
LINE = re.compile(r"^(?P<file>[^:]+):(?P<line>\d+):(?P<severity>\w+):(?P<id>[^:]+):(?P<msg>.*)$")


def tool_version(command):
    try:
        out = subprocess.run(command, capture_output=True, text=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError):
        return None
    match = re.search(r"(\d+\.\d+(?:\.\d+)?)", out)
    return match.group(1) if match else None


def require(name, command, expected):
    version = tool_version(command)
    if version is None:
        sys.exit(f"{name} not found (expected {expected}.x, see docs/dev/README.md)")
    if not version.startswith(expected):
        print(f"warning: {name} {version}, CI uses {expected}.x; results may differ",
              file=sys.stderr)


def check_format(fix):
    require("clang-format", ["clang-format", "--version"], CLANG_FORMAT_VERSION)
    files = sources.sources(sorted(sources.GROUPS))
    mode = ["-i"] if fix else ["--dry-run", "--Werror"]
    result = subprocess.run(["clang-format", *mode, *files], cwd=ROOT)
    if result.returncode != 0:
        print("\nformat: run `tools/lint/check.py format --fix`", file=sys.stderr)
    else:
        print(f"format: {len(files)} files OK")
    return result.returncode


def idf_suppressions():
    """ESP-IDF lives in /opt/esp/idf in CI and in ~/.espressif locally."""
    paths = {os.environ.get("IDF_PATH", ""), "/opt/esp"}
    return [f"--suppress=*:{os.path.realpath(p)}/*" for p in sorted(paths) if p]


def run_cppcheck(extra, files, build_dir):
    command = [
        "cppcheck",
        "--addon=misra",
        "--enable=warning,style,performance,portability",
        "--inline-suppr",
        "--std=c11",
        "--error-exitcode=0",
        f"-j{os.cpu_count() or 2}",
        f"--cppcheck-build-dir={build_dir}",
        "--template={file}:{line}:{severity}:{id}:{message}",
        *VENDOR_SUPPRESSIONS,
        *idf_suppressions(),
        *extra,
        *files,
    ]
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    findings = []
    for raw in result.stderr.splitlines():
        match = LINE.match(raw)
        if match is None:
            if raw.strip():
                findings.append(("", 0, "error", "cppcheckOutput", raw.strip()))
            continue
        path = os.path.relpath(os.path.abspath(os.path.join(ROOT, match["file"])), ROOT)
        findings.append((path, int(match["line"]), match["severity"], match["id"], match["msg"]))
    return findings


def check_cppcheck(update, require_gateway):
    require("cppcheck", ["cppcheck", "--version"], CPPCHECK_VERSION)
    findings = []
    gateway_checked = False
    with tempfile.TemporaryDirectory() as work:
        os.makedirs(os.path.join(work, "ecu"))
        own = [f for f in sources.sources(["ecu", "shared"]) if f.endswith(".c")]
        findings += run_cppcheck(ECU_ARGS, own, os.path.join(work, "ecu"))

        if os.path.exists(GATEWAY_DB):
            os.makedirs(os.path.join(work, "gateway"))
            own = [f for f in sources.sources(["gateway"]) if f.endswith(".c")]
            filters = ["--file-filter=*/" + f for f in own]
            findings += run_cppcheck(["--project=" + GATEWAY_DB, *filters], [],
                                     os.path.join(work, "gateway"))
            gateway_checked = True
        elif require_gateway:
            sys.exit(f"missing {os.path.relpath(GATEWAY_DB, ROOT)}: run idf.py reconfigure")
        else:
            print("cppcheck: Gateway skipped (no compile_commands.json; run idf.py build)")

    general = [f for f in findings if not f[3].startswith("misra-")]
    misra = collections.Counter(
        (f[3], f[0]) for f in findings if f[3].startswith("misra-") and f[3] not in IGNORED_RULES)

    status = 0
    for path, line, severity, rule, msg in general:
        print(f"{path}:{line}: {severity}: {msg} [{rule}]")
        status = 1

    current = collections.defaultdict(dict)
    for (rule, path), count in sorted(misra.items()):
        current[rule][path] = count
    if update and not gateway_checked:
        sys.exit("--update-baseline needs the Gateway analysis: run idf.py build first")
    if update:
        with open(BASELINE, "w", encoding="utf-8") as out:
            json.dump(current, out, indent=2, sort_keys=True)
            out.write("\n")
        print(f"cppcheck: baseline written, {sum(misra.values())} MISRA findings")
        return status

    with open(BASELINE, encoding="utf-8") as src:
        baseline = json.load(src)
    if not gateway_checked:
        baseline = {rule: {p: c for p, c in files.items() if not p.startswith("firmware/remote-gateway/")}
                    for rule, files in baseline.items()}
    lower = []
    for rule, path in sorted(set(misra) | {(r, p) for r in baseline for p in baseline[r]}):
        now = misra.get((rule, path), 0)
        allowed = baseline.get(rule, {}).get(path, 0)
        if now > allowed:
            status = 1
            print(f"{path}: {rule} {now} > baseline {allowed}")
            for f in findings:
                if f[3] == rule and f[0] == path:
                    print(f"  {f[0]}:{f[1]}: {f[4]}")
        elif now < allowed:
            lower.append(f"{path}: {rule} {now} < baseline {allowed}")
    if lower:
        status = 1
        print("\n".join(lower))
        print("cppcheck: fewer MISRA findings than the baseline, good; "
              "run `tools/lint/check.py cppcheck --update-baseline` and commit it")
    if status == 0:
        print(f"cppcheck: no general findings, MISRA {sum(misra.values())} = baseline")
    return status


def main():
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command", required=True)
    fmt = sub.add_parser("format")
    fmt.add_argument("--fix", action="store_true")
    cpp = sub.add_parser("cppcheck")
    cpp.add_argument("--update-baseline", action="store_true")
    cpp.add_argument("--require-gateway", action="store_true")
    args = parser.parse_args()
    if shutil.which("clang-format" if args.command == "format" else "cppcheck") is None:
        sys.exit(f"{args.command}: tool not found in PATH")
    if args.command == "format":
        return check_format(args.fix)
    return check_cppcheck(args.update_baseline, args.require_gateway)


if __name__ == "__main__":
    sys.exit(main())
