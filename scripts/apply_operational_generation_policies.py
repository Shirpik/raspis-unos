#!/usr/bin/env python3
"""Apply the confirmed room/campus policies to an existing solver snapshot."""

from __future__ import annotations

import argparse
from pathlib import Path

from prepare_generation_scenario import apply_operational_policies, read_json, write_json


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    value = read_json(args.input)
    changed = apply_operational_policies(value)
    destination = args.output or args.input
    write_json(destination, value)
    print(f"{destination} (changed={str(changed).lower()})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
