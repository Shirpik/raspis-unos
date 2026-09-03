#!/usr/bin/env python3
"""Assemble 02 Sep and 03–05 Sep into one auditable scenario chain."""

from __future__ import annotations

import argparse
import copy
import json
from collections import defaultdict
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BASELINE = ROOT / "output" / "scenarios" / "baseline_data.json"


def read(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def write(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def merge_schedules(first: dict, rest: dict) -> dict:
    groups: dict[int, dict] = {}
    for source in (first, rest):
        for raw_group in source.get("groups", []):
            group_id = int(raw_group["group_index"])
            target = groups.setdefault(group_id, {
                "group_index": group_id,
                "group_name": raw_group.get("group_name", f"#{group_id}"),
                "days": [],
            })
            target["days"].extend(copy.deepcopy(raw_group.get("days", [])))
    for group in groups.values():
        unique = {day.get("date_iso", day.get("date")): day for day in group["days"]}
        group["days"] = [unique[key] for key in sorted(unique)]
    return {"groups": [groups[key] for key in sorted(groups)]}


def combined_targets(*allocations: dict) -> list[dict]:
    totals: dict[int, dict] = defaultdict(lambda: {"minimum_pairs": 0, "desired_pairs": 0})
    names: dict[int, str] = {}
    for allocation in allocations:
        for row in allocation.get("teacher_targets", []):
            teacher_id = int(row["teacher"])
            names[teacher_id] = row.get("name", f"#{teacher_id}")
            minimum = int(row.get("minimum_pairs", 0))
            desired = int(row.get("desired_pairs", minimum))
            totals[teacher_id]["minimum_pairs"] += minimum
            totals[teacher_id]["desired_pairs"] += desired
    return [
        {"teacher": teacher_id, "name": names[teacher_id], **totals[teacher_id]}
        for teacher_id in sorted(totals) if totals[teacher_id]["minimum_pairs"] > 0
    ]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--scenario-dir", type=Path, required=True)
    parser.add_argument("--sep2-schedule", type=Path, required=True)
    parser.add_argument("--sep3-5-schedule", type=Path, required=True)
    args = parser.parse_args()

    scenario = args.scenario_dir
    merged = merge_schedules(read(args.sep2_schedule), read(args.sep3_5_schedule))
    data = read(BASELINE)
    data.setdefault("settings", {})["start_date"] = "2026-09-02"
    data["settings"]["end_date"] = "2026-09-05"
    data["settings"]["teacher_period_targets"] = combined_targets(
        read(scenario / "allocation_sep2.json"),
        read(scenario / "allocation_sep3_5.json"),
    )
    data.setdefault("meta", {})["generation_scenario"] = {
        "date_from": "2026-09-02", "date_to": "2026-09-05",
        "chain_includes_sep2": True,
    }
    write(scenario / "schedule_sep2_5_combined.json", merged)
    write(scenario / "input_sep2_5_audit.json", data)
    print(scenario / "schedule_sep2_5_combined.json")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
