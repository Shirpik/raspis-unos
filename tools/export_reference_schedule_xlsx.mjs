import fs from "node:fs/promises";
import path from "node:path";
import { SpreadsheetFile, Workbook } from "@oai/artifact-tool";

const repoRoot = "C:/Users/SHIRP/Desktop/work/raspis-unost-master";
const schedulePath = path.join(repoRoot, "output/latest/schedule_all.json");
const dataPath = path.join(repoRoot, "data/timetable_data.json");
const outputDir = path.join(repoRoot, "outputs/schedule_reference_export_2026_09_02_05");
const outputPath = path.join(outputDir, "Расписание_02.09-05.09.2026_по_образцу.xlsx");

const schedule = JSON.parse(await fs.readFile(schedulePath, "utf8"));
const data = JSON.parse(await fs.readFile(dataPath, "utf8"));

const campuses = new Map((data.campuses ?? []).map((campus) => [campus.id, campus.name]));
const rooms = new Map((data.rooms ?? []).map((room) => [room.id, room]));
const teachers = new Map((data.teachers ?? []).map((teacher) => [teacher.id, teacher]));

const courseSheets = new Map([
  [1, []],
  [2, []],
  [3, []],
  [4, []],
]);

function courseOf(groupName) {
  const runs = String(groupName).match(/\d{3,4}/g) ?? [];
  const digit = runs[0]?.[0];
  const course = Number(digit);
  return course >= 1 && course <= 4 ? course : 1;
}

for (const group of schedule.groups ?? []) {
  courseSheets.get(courseOf(group.group_name)).push(group);
}

function shortTeacher(name) {
  const parts = String(name ?? "").trim().split(/\s+/).filter(Boolean);
  if (parts.length < 2) return parts[0] ?? "";
  const [surname, first, patronymic] = parts;
  const initials = [first, patronymic].filter(Boolean).map((part) => `${part[0]}.`).join("");
  return `${surname} ${initials}`;
}

function subjectName(lesson) {
  return String(lesson.name ?? lesson.subject ?? lesson.discipline ?? "").trim();
}

function roomLabel(lesson) {
  const room = rooms.get(lesson.room_id);
  const name = String(lesson.room_name ?? room?.name ?? "").trim();
  if (!name) return "";
  const campusName = campuses.get(room?.campus);
  const suffix = campusName?.toLowerCase().startsWith("лес") ? "_Л"
    : campusName?.toLowerCase().startsWith("крив") ? "_К"
      : campusName ? `_${campusName}` : "";
  return `${name}${suffix}`;
}

function lessonText(lesson) {
  const subject = subjectName(lesson);
  const subgroup = lesson.subgroup === 0 ? "1 п/г: "
    : lesson.subgroup === 1 ? "2 п/г: "
      : "";
  const teacherData = teachers.get(lesson.teacher_id);
  const teacher = shortTeacher(lesson.teacher_name ?? lesson.teacher ?? teacherData?.name ?? "");
  const room = roomLabel(lesson);
  const tail = [teacher, room].filter(Boolean).join(" · ");
  return [subgroup + subject, tail].filter(Boolean).join("\n");
}

function slotCell(slot) {
  const lessons = slot.lessons ?? [];
  if (!lessons.length) return "";
  return lessons.map(lessonText).join("\n\n");
}

function timeText(raw) {
  const match = String(raw ?? "").match(/\(([^)]+)\)/);
  const value = match ? match[1] : String(raw ?? "");
  return value.replace(/:/g, ".").replace(/\s*-\s*/g, "-");
}

function dateShort(day) {
  const parts = String(day.date ?? "").split(".");
  return parts.length >= 2 ? `${parts[0]}.${parts[1]}` : String(day.date ?? "");
}

function weekdayFull(day) {
  const key = String(day.weekday ?? "").toUpperCase();
  return ({
    "ПН": "ПОНЕДЕЛЬНИК",
    "ВТ": "ВТОРНИК",
    "СР": "СРЕДА",
    "ЧТ": "ЧЕТВЕРГ",
    "ПТ": "ПЯТНИЦА",
    "СБ": "СУББОТА",
    "ВС": "ВОСКРЕСЕНЬЕ",
  })[key] ?? key;
}

function rangeAddress(startRow, startCol, rowCount, colCount) {
  return { startRow, startCol, rowCount, colCount };
}

const workbook = Workbook.create();

const palette = {
  blue: "#BDD7EE",
  blueDark: "#9BC2E6",
  white: "#FFFFFF",
  gray: "#F8FAFC",
  black: "#000000",
};

for (const [course, groups] of courseSheets.entries()) {
  const sheet = workbook.worksheets.add(`${course} курс`);
  sheet.showGridLines = false;

  const totalCols = 3 + Math.max(groups.length, 1);
  const allDays = groups[0]?.days ?? [];
  const rows = [];

  for (const day of allDays) {
    rows.push([dateShort(day), "", "", ...groups.map((group) => group.group_name)]);
    const maxSlots = 7;
    for (let slotNo = 1; slotNo <= maxSlots; slotNo += 1) {
      const firstSlot = day.slots?.find((slot) => Number(slot.slot) === slotNo);
      const values = ["", timeText(firstSlot?.time), slotNo];
      for (const group of groups) {
        const groupDay = (group.days ?? []).find((candidate) => candidate.date_iso === day.date_iso);
        const slot = groupDay?.slots?.find((candidate) => Number(candidate.slot) === slotNo);
        values.push(slotCell(slot ?? {}));
      }
      rows.push(values);
    }
  }

  if (!rows.length) rows.push(["", "", "", ""]);
  sheet.getRangeByIndexes(0, 0, rows.length, totalCols).values = rows;

  sheet.getRangeByIndexes(0, 0, rows.length, totalCols).format = {
    font: { name: "Arial", size: 9, color: "#111827" },
    wrapText: true,
    verticalAlignment: "center",
    horizontalAlignment: "center",
    fill: palette.white,
    borders: { preset: "all", style: "thin", color: palette.black },
  };

  sheet.getRangeByIndexes(0, 0, rows.length, 3).format = {
    fill: palette.blue,
    font: { name: "Arial", size: 9, bold: true, color: "#111827" },
    wrapText: true,
    verticalAlignment: "center",
    horizontalAlignment: "center",
    borders: { preset: "all", style: "thin", color: palette.black },
  };

  for (let row = 0; row < rows.length; row += 8) {
    sheet.getRangeByIndexes(row, 0, 1, totalCols).format = {
      fill: palette.blue,
      font: { name: "Arial", size: 9, bold: true, color: "#111827" },
      wrapText: true,
      verticalAlignment: "center",
      horizontalAlignment: "center",
      borders: { preset: "all", style: "medium", color: palette.black },
    };
    sheet.getRangeByIndexes(row, 0, 1, 1).format = {
      fill: palette.blueDark,
      font: { name: "Arial", size: 10, bold: true, color: "#111827" },
      wrapText: true,
      verticalAlignment: "center",
      horizontalAlignment: "center",
      borders: { preset: "all", style: "medium", color: palette.black },
    };
    const weekday = weekdayFull(allDays[row / 8] ?? {});
    if (row + 1 < rows.length) {
      sheet.getRangeByIndexes(row + 1, 0, Math.min(7, rows.length - row - 1), 1).merge();
      sheet.getRangeByIndexes(row + 1, 0, 1, 1).values = [[weekday]];
      sheet.getRangeByIndexes(row + 1, 0, Math.min(7, rows.length - row - 1), 1).format = {
        fill: palette.blue,
        font: { name: "Arial", size: 9, bold: true, color: "#111827" },
        wrapText: true,
        verticalAlignment: "center",
        horizontalAlignment: "center",
        textOrientation: 90,
        borders: { preset: "all", style: "medium", color: palette.black },
      };
    }
  }

  sheet.getRangeByIndexes(0, 0, rows.length, 1).format.columnWidth = 11;
  sheet.getRangeByIndexes(0, 1, rows.length, 1).format.columnWidth = 14;
  sheet.getRangeByIndexes(0, 2, rows.length, 1).format.columnWidth = 5;
  if (groups.length > 0) sheet.getRangeByIndexes(0, 3, rows.length, groups.length).format.columnWidth = 22;

  for (let row = 0; row < rows.length; row += 1) {
    const height = row % 8 === 0 ? 24 : 70;
    sheet.getRangeByIndexes(row, 0, 1, totalCols).format.rowHeight = height;
  }

  sheet.freezePanes.freezeColumns(3);
}

const inspect = await workbook.inspect({
  kind: "sheet,region",
  maxChars: 4000,
  tableMaxRows: 4,
  tableMaxCols: 8,
});
console.log(inspect.ndjson);

await fs.mkdir(outputDir, { recursive: true });
const output = await SpreadsheetFile.exportXlsx(workbook);
await output.save(outputPath);
console.log(outputPath);
