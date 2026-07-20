#!/usr/bin/env python3
# Open Trader
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

"""Build runner: configures and/or builds cmake project, captures output to build.log."""

import argparse
import subprocess
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
BUILD_DIR = PROJECT_ROOT / "cmake-build-debug-clang"
BUILD_LOG = BUILD_DIR / "build.log"

CMAKE_CONFIGURE_ARGS = [
    "cmake",
    "-S", str(PROJECT_ROOT),
    "-B", str(BUILD_DIR),
    "-G", "Ninja",
    "-DCMAKE_BUILD_TYPE=Debug",
    "-DCMAKE_C_COMPILER=/usr/bin/clang-23",
    "-DCMAKE_CXX_COMPILER=/usr/bin/clang++-23",
]


def run(cmd, log_path):
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with open(log_path, "w") as log:
        result = subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT)
    return result.returncode


def configure():
    print(f"Configuring: {' '.join(CMAKE_CONFIGURE_ARGS)}")
    return run(CMAKE_CONFIGURE_ARGS, BUILD_LOG)


def build(target, jobs):
    cmd = ["cmake", "--build", str(BUILD_DIR), "--target", target, f"-j{jobs}"]
    print(f"Building: {' '.join(cmd)}")
    return run(cmd, BUILD_LOG)


def main():
    parser = argparse.ArgumentParser(description="Build agent for extools project")
    parser.add_argument("action", choices=["configure", "build", "auto"], help="configure: generate build files; build: compile; auto: configure if needed then build")
    parser.add_argument("--target", default="trader", help="build target (default: trader)")
    parser.add_argument("--jobs", type=int, default=7, help="parallel jobs (default: 7)")
    args = parser.parse_args()

    if args.action == "configure":
        rc = configure()
    elif args.action == "build":
        if not (BUILD_DIR / "build.ninja").exists():
            print(f"Error: no build.ninja in {BUILD_DIR}, run with 'configure' or 'auto' first", file=sys.stderr)
            sys.exit(1)
        rc = build(args.target, args.jobs)
    elif args.action == "auto":
        if not (BUILD_DIR / "build.ninja").exists():
            print("Build files missing, configuring first...")
            rc = configure()
            if rc != 0:
                print(f"rc={rc}")
                print(f"log={BUILD_LOG}")
                sys.exit(rc)
        rc = build(args.target, args.jobs)

    print(f"rc={rc}")
    print(f"log={BUILD_LOG}")
    sys.exit(rc)


if __name__ == "__main__":
    main()
