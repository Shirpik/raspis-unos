#!/usr/bin/env python3
"""Temporarily relax each new room rule to identify an infeasible combination."""

from __future__ import annotations

import copy
import json
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "data" / "timetable_data.json"
EXE = ROOT / "build" / "Release" / "timetable_solver.exe"


def relax(payload: dict, rule: str) -> None:
    teachers = {int(t["id"]): t for t in payload["teachers"]}
    if rule == "kalchevskaya":
        teachers[49]["allowed_campuses"] = [1]
        teachers[49]["campus_priority"] = [1]
        for lesson in payload["lessons"]:
            if int(lesson.get("teacher", -1)) == 49 and lesson.get("is_lab"):
                lesson.update({"fixed_room": -1, "preferred_room": -1,
                               "allow_room_substitution": False, "allowed_campuses": [1]})
    elif rule == "kalchevskaya_lesnaya_no_fixed":
        teachers[49]["allowed_campuses"] = [0]
        teachers[49]["campus_priority"] = [0]
        for lesson in payload["lessons"]:
            if int(lesson.get("teacher", -1)) == 49 and lesson.get("is_lab"):
                lesson.update({"fixed_room": -1, "preferred_room": -1,
                               "allow_room_substitution": False, "allowed_campuses": [0]})
    elif rule == "limonova":
        for lesson in payload["lessons"]:
            if int(lesson.get("teacher", -1)) == 55 and lesson.get("is_lab"):
                lesson.update({"fixed_room": -1, "preferred_room": -1,
                               "allow_room_substitution": False, "allowed_campuses": [0, 1]})
    elif rule == "samtsov":
        for lesson in payload["lessons"]:
            if (int(lesson.get("teacher", -1)) == 59 and not lesson.get("is_lab")
                    and "МДК" in str(lesson.get("name", "")).upper()):
                lesson.update({"fixed_room": -1, "preferred_room": -1,
                               "allow_room_substitution": False, "allowed_campuses": [0]})
    elif rule == "semenova_both":
        teachers[10]["allowed_campuses"] = [0, 1]
        teachers[10]["campus_priority"] = [1, 0]
        for lesson in payload["lessons"]:
            if int(lesson.get("teacher", -1)) == 10 and int(lesson.get("fixed_room", -1)) < 0:
                lesson["allowed_campuses"] = [0, 1]
    elif rule == "sinelnikova_both":
        teachers[30]["allowed_campuses"] = [0, 1]
        teachers[30]["campus_priority"] = [1, 0]
        for lesson in payload["lessons"]:
            if int(lesson.get("teacher", -1)) == 30 and int(lesson.get("fixed_room", -1)) < 0:
                lesson["allowed_campuses"] = [0, 1]
    elif rule in ("kalchevskaya_365_only", "kalchevskaya_381_only"):
        keep_id = 365 if rule.endswith("365_only") else 381
        teachers[49]["allowed_campuses"] = [0, 1]
        teachers[49]["campus_priority"] = [0, 1]
        for lesson in payload["lessons"]:
            if int(lesson.get("teacher", -1)) != 49 or not lesson.get("is_lab"):
                continue
            if int(lesson["id"]) == keep_id:
                continue
            lesson.update({"fixed_room": -1, "preferred_room": -1,
                           "allow_room_substitution": False, "allowed_campuses": [1]})


def main() -> None:
    original = DATA.read_bytes()
    base = json.loads(original.decode("utf-8"))
    results = {}
    try:
        for rule in (
            "max_student_5",
            "student_windows_soft",
            "kalchevskaya_lesnaya_no_fixed",
            "kalchevskaya_365_only",
            "kalchevskaya_381_only",
            "semenova_both",
            "sinelnikova_both",
        ):
            payload = copy.deepcopy(base)
            relax(payload, rule)
            config = payload["settings"]["solver_config"]
            config["use_quality_objective"] = False
            config["quality_improvement_seconds"] = 0
            config["week_time_limit_seconds"] = 20
            if rule == "max_student_5":
                config["max_student_pairs_per_day"] = 5
            if rule == "student_windows_soft":
                config["hard_no_student_windows"] = False
            DATA.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
            output = ROOT / "output" / "diagnostics" / f"relax-{rule}"
            output.mkdir(parents=True, exist_ok=True)
            process = subprocess.run(
                [str(EXE), "--generate", "--output", str(output)],
                cwd=ROOT,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                encoding="utf-8",
                errors="replace",
                timeout=30,
            )
            tail = process.stdout[-1000:]
            results[rule] = {"exit_code": process.returncode, "tail": tail}
            print(f"{rule}: exit={process.returncode}")
            if process.returncode == 0:
                print("FEASIBLE")
            else:
                print(tail)
    finally:
        DATA.write_bytes(original)
    (ROOT / "output" / "diagnostics" / "revision-feasibility.json").write_text(
        json.dumps(results, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
