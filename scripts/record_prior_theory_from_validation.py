#!/usr/bin/env python3
"""Record already-completed theory required by a later schedule slice.

The input report is produced by the app's own validator.  Only missing theory
for flagged LPZ rows is added to the pre-period ledger; no lesson, teacher, or
placement is changed.
"""

from __future__ import annotations

import argparse
import json
import shutil
from collections import defaultdict
from datetime import datetime
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def write_json(path: Path, value: object) -> None:
    temp = path.with_suffix(path.suffix + ".tmp")
    temp.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    temp.replace(path)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--data", type=Path, default=ROOT / "data" / "timetable_data.json")
    parser.add_argument("--validation", type=Path, required=True)
    args = parser.parse_args()

    data = read_json(args.data)
    report = read_json(args.validation)
    lessons = {int(lesson["id"]): lesson for lesson in data.get("lessons", [])}
    missing: dict[tuple[int, int], int] = defaultdict(int)
    for issue in report.get("issues", []):
        if issue.get("code") != "theory_before_lab_violation":
            continue
        context = issue.get("context", {})
        lesson = lessons.get(int(context.get("lesson", -1)))
        if not lesson:
            raise SystemExit(f"Unknown lesson in validation report: {context.get('lesson')}")
        key = (int(lesson["group"]), int(lesson["subject_id"]))
        required = int(context.get("required", 0))
        theory_before = int(context.get("theory_before", 0))
        missing[key] = max(missing[key], required - theory_before)

    if not missing:
        print("No prior theory ledger entries are required")
        return 0

    settings = data.setdefault("settings", {})
    ledger = settings.setdefault("prior_theory_pairs", [])
    by_key: dict[tuple[int, int], list[dict]] = defaultdict(list)
    for item in ledger:
        by_key[(int(item.get("group", -1)), int(item.get("subject", -1)))].append(item)

    changed = []
    for key, needed in sorted(missing.items()):
        entries = by_key.get(key, [])
        if entries:
            entry = entries[0]
            entry["pairs"] = int(entry.get("pairs", 0)) + needed
        else:
            entry = {"group": key[0], "subject": key[1], "pairs": needed}
            ledger.append(entry)
        changed.append({"group": key[0], "subject": key[1], "added_pairs": needed})

    history = args.data.parent / "history"
    history.mkdir(parents=True, exist_ok=True)
    backup = history / f"before_prior_theory_ledger_{datetime.now():%Y%m%d-%H%M%S}.json"
    shutil.copy2(args.data, backup)
    write_json(args.data, data)
    print(json.dumps({"updated": changed, "backup": str(backup)}, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
