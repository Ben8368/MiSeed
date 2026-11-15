"""Stream build diagnostics and reject GNU make clock-skew success exits."""

# SPDX-License-Identifier: AGPL-3.0-only

import argparse
import os
import re
import subprocess
import sys


CLOCK_DIAGNOSTIC = re.compile(
    rb"Clock skew detected|has modification time .* in the future", re.I)


def run(command, stream):
    env = dict(os.environ, LC_ALL="C")
    skew = False
    with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          env=env) as process:
        for line in process.stdout:
            stream.write(line)
            stream.flush()
            skew |= bool(CLOCK_DIAGNOSTIC.search(line))
        code = process.wait()
    if skew:
        stream.write(b"ERROR: build clock skew; synchronize host/VM clocks and inspect "
                     b"future-dated inputs before rebuilding. Timestamps were not changed.\n")
        stream.flush()
    return code if code else int(skew)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not command:
        parser.error("a build command is required")
    try:
        return run(command, sys.stdout.buffer)
    except OSError as exc:
        parser.exit(1, f"build command failed: {exc}\n")


if __name__ == "__main__":
    sys.exit(main())
