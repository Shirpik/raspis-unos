"""Extract plain text from a legacy binary Word .doc using its CLX piece table."""

from __future__ import annotations

import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / ".codex-temp" / "pydeps"))
import olefile  # type: ignore


def u16(data, offset):
    return struct.unpack_from("<H", data, offset)[0]


def u32(data, offset):
    return struct.unpack_from("<I", data, offset)[0]


def extract(path: Path) -> str:
    ole = olefile.OleFileIO(str(path))
    word = ole.openstream("WordDocument").read()
    flags = u16(word, 0x0A)
    table_name = "1Table" if flags & 0x0200 else "0Table"
    table = ole.openstream(table_name).read()

    offset = 32
    csw = u16(word, offset)
    offset += 2 + csw * 2
    cslw = u16(word, offset)
    offset += 2 + cslw * 4
    cb_rg_fc_lcb = u16(word, offset)
    offset += 2
    if cb_rg_fc_lcb <= 33:
        raise ValueError("В FIB отсутствует fcClx")
    fc_clx = u32(word, offset + 33 * 8)
    lcb_clx = u32(word, offset + 33 * 8 + 4)
    clx = table[fc_clx:fc_clx + lcb_clx]

    pos = 0
    while pos < len(clx) and clx[pos] == 0x01:
        size = u16(clx, pos + 1)
        pos += 3 + size
    if pos >= len(clx) or clx[pos] != 0x02:
        raise ValueError("В CLX отсутствует Pcdt")
    plc_size = u32(clx, pos + 1)
    plc = clx[pos + 5:pos + 5 + plc_size]
    count = (plc_size - 4) // 12
    cps = [u32(plc, i * 4) for i in range(count + 1)]
    pcd_offset = (count + 1) * 4
    pieces = []
    for i in range(count):
        chars = cps[i + 1] - cps[i]
        fc_raw = u32(plc, pcd_offset + i * 8 + 2)
        compressed = bool(fc_raw & 0x40000000)
        fc = fc_raw & 0x3FFFFFFF
        if compressed:
            fc //= 2
            raw = word[fc:fc + chars]
            decoded = raw.decode("cp1251", errors="replace")
        else:
            raw = word[fc:fc + chars * 2]
            decoded = raw.decode("utf-16le", errors="replace")
        pieces.append(decoded)
    text = "".join(pieces)
    return text.replace("\r", "\n").replace("\x07", "\t").replace("\x0b", "\n")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("Использование: extract_legacy_doc.py input.doc output.txt")
    output = Path(sys.argv[2])
    output.write_text(extract(Path(sys.argv[1])), encoding="utf-8")
    print(output)
