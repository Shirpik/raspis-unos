"""Create readable diagnostic crops from school teacher schedule photos."""

from pathlib import Path

from PIL import Image, ImageEnhance


ROOT = Path(r"C:\Users\SHIRP\AppData\Local\Temp")
OUT = Path(r"C:\Users\SHIRP\Desktop\work\raspis-unost-master\build\school_schedule_crops")
OUT.mkdir(parents=True, exist_ok=True)

PAGES = [
    (
        "page1",
        ROOT / "codex-clipboard-76276b0f-36eb-48ba-9dc3-90b0dcc28975.png",
        (55, 140, 171, 866),
        [171, 345, 519, 694, 873, 1053, 1238],
    ),
    (
        "page2",
        ROOT / "codex-clipboard-9fe92ddd-c486-4508-829c-2eeac22f2cb8.png",
        (126, 104, 232, 850),
        [232, 402, 573, 743, 914, 1084, 1255],
    ),
    (
        "page3",
        ROOT / "codex-clipboard-03362d6c-1019-4152-bc5b-e35ebdc702f8.png",
        (84, 130, 186, 268),
        [186, 342, 497, 653, 811, 972, 1135],
    ),
]

DAYS = ("mon", "tue", "wed", "thu", "fri", "sat")

for page_name, source, name_box, edges in PAGES:
    image = Image.open(source).convert("RGB")
    y0, y1 = name_box[1], name_box[3]
    names = image.crop(name_box)
    for index, day in enumerate(DAYS):
        schedule = image.crop((edges[index], y0, edges[index + 1], y1))
        combined = Image.new("RGB", (names.width + schedule.width, names.height), "white")
        combined.paste(names, (0, 0))
        combined.paste(schedule, (names.width, 0))
        combined = ImageEnhance.Contrast(combined).enhance(1.35)
        combined = combined.resize((combined.width * 3, combined.height * 3), Image.Resampling.LANCZOS)
        combined.save(OUT / f"{page_name}_{day}.png")

print(OUT)
