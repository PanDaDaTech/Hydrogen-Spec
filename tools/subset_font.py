# -*- coding: utf-8 -*-
"""Rebuild the embedded MiSans subset and verify UI character coverage.

One face is embedded: MiSans Medium (family "MiSans", usWeightClass 500).
No bold anywhere: synthetic emboldening smears stems into grey fuzz once
text is composited through per-pixel alpha, and a real bold face was
explicitly rejected ("直接用 Medium 就行了").  Hierarchy comes from size
and color only - keep every CreateFontW / DrawTextGpLine request at
FW_NORMAL.

The full font (~8 MB) is not committed. Place it at:

    build/font-backup/MiSans-Medium.ttf

Usage:
    python tools/subset_font.py --full path/to/MiSans-Medium.ttf
    python tools/subset_font.py --check

Collected characters:
  1. every character in hspec.cpp string/char literals (all UI labels);
  2. printable ASCII 0x20-0x7E;
  3. characters from tools/hardware_words.txt (Chinese vendor names and
     hardware vocabulary), unless --minimal is given.

At runtime, any dynamic line containing a character outside this set is
drawn with the system CJK font instead (per-line fallback), so nothing is
ever rendered as tofu.
"""
import sys
from pathlib import Path

# CI 跑在 Windows runner 上，stdout 默认是 cp1252：一旦要打印缺失的中文字符，
# 脚本自己先抛 UnicodeEncodeError，真正的「哪个字缺了」反而看不到。
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "hspec.cpp"
HARDWARE = ROOT / "tools" / "hardware_words.txt"

TTF_OUT = ROOT / "winres" / "MiSans-Medium.ttf"
TTF_FULL = ROOT / "build" / "font-backup" / "MiSans-Medium.ttf"

ESCAPES = {"n": "\n", "t": "\t", "r": "\r", "\\": "\\", '"': '"', "'": "'",
           "0": "\0", "a": "\a", "b": "\b", "f": "\f", "v": "\v"}


def _escaped(text, i, out):
    n = len(text)
    if i + 1 >= n:
        return i + 1
    c = text[i + 1]
    if c == "u" and i + 6 <= n:
        try:
            out.add(chr(int(text[i + 2:i + 6], 16)))
            return i + 6
        except ValueError:
            pass
    if c == "x":
        j = i + 2
        while j < n and text[j] in "0123456789abcdefABCDEF" and j - i < 5:
            j += 1
        try:
            out.add(chr(int(text[i + 2:j], 16)))
            return i + 2
        except ValueError:
            pass
    out.add(ESCAPES.get(c, c))
    return i + 2


def source_chars(text):
    out = set()
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            while i < n and text[i] != "\n":
                i += 1
        elif c == "/" and i + 1 < n and text[i + 1] == "*":
            i += 2
            while i + 1 < n and not (text[i] == "*" and text[i + 1] == "/"):
                i += 1
            i += 2
        elif c == '"':
            i += 1
            while i < n and text[i] != '"':
                if text[i] == "\\":
                    i = _escaped(text, i, out)
                else:
                    out.add(text[i])
                    i += 1
            i += 1
        elif c == "'":
            i += 1
            while i < n and text[i] != "'":
                if text[i] == "\\":
                    i = _escaped(text, i, out)
                else:
                    out.add(text[i])
                    i += 1
            i += 1
        else:
            i += 1
    return out


def hardware_chars():
    out = set()
    if HARDWARE.exists():
        out |= {c for c in HARDWARE.read_text(encoding="utf-8", errors="replace")
                if ord(c) >= 0x80}
    return out


def collect(include_hardware):
    chars = {chr(c) for c in range(0x20, 0x7F)}
    chars |= source_chars(SRC.read_text(encoding="utf-8", errors="replace"))
    if include_hardware:
        chars |= hardware_chars()
    chars -= set("\r\n\t\0")
    return chars


def _family_name(font):
    for r in font["name"].names:
        if r.nameID == 1 and r.platformID == 3:
            return r.toUnicode()
    return None


def _fix_metadata(font):
    """GDI / GDI+ match by family + weight; fix MiSans' non-standard metadata.

    nameID 16/17 (typographic family/subfamily) would override nameID 1/2
    on some stacks - drop them so the rewrite below always wins.
    """
    os2 = font["OS/2"]
    os2.usWeightClass = 500
    # fsSelection bits: 0x01 ITALIC, 0x20 BOLD, 0x40 REGULAR
    os2.fsSelection = (os2.fsSelection & ~0x21) | 0x40

    nametbl = font["name"]
    nametbl.names = [r for r in nametbl.names
                     if r.platformID == 3 and r.nameID not in (16, 17)]
    for r in nametbl.names:
        if r.nameID == 1:
            r.string = "MiSans"
        elif r.nameID == 2:
            r.string = "Medium"


def build(full, out):
    from fontTools.ttLib import TTFont
    from fontTools import subset

    chars = collect("--minimal" not in sys.argv)
    print("UI chars: %d" % len(chars))

    font = TTFont(str(full))
    options = subset.Options()
    options.name_IDs = ["*"]
    options.notdef_outline = True
    options.recalc_bounds = True
    options.drop_tables += ["FFTM"]
    ss = subset.Subsetter(options)
    ss.populate(unicodes=[ord(c) for c in chars])
    ss.subset(font)

    _fix_metadata(font)

    font.save(str(out))
    print("[OK] wrote %s: %d chars, %d bytes (family MiSans, weight 500)"
          % (out.relative_to(ROOT), len(chars), out.stat().st_size))


def check(out):
    from fontTools.ttLib import TTFont

    chars = collect("--minimal" not in sys.argv)
    font = TTFont(str(out))
    cmap = font.getBestCmap()
    missing = sorted(c for c in chars if ord(c) >= 0x80 and ord(c) not in cmap)
    if missing:
        print("[ERR] %d chars missing from the subset:" % len(missing))
        for c in missing:
            print("   U+%04X %s" % (ord(c), c))
        return False
    os2 = font["OS/2"]
    if os2.usWeightClass != 500 or (os2.fsSelection & 0x61) != 0x40:
        print("[ERR] OS/2 metadata is wrong (usWeightClass=%d fsSelection=0x%04X)"
              % (os2.usWeightClass, os2.fsSelection))
        return False
    fam = _family_name(font)
    if fam != "MiSans":
        print("[ERR] family name is %r (expected MiSans)" % fam)
        return False
    print("[OK] subset covers all UI chars (cmap %d, %d bytes)"
          % (len(cmap), TTF_OUT.stat().st_size))
    return True


def main():
    if "--check" in sys.argv:
        return 0 if check(TTF_OUT) else 1

    full = TTF_FULL
    if "--full" in sys.argv:
        full = Path(sys.argv[sys.argv.index("--full") + 1])
    if not full.exists():
        print("[ERR] full font not found: %s (use --full)" % full)
        return 1
    build(full, TTF_OUT)
    return 0


if __name__ == "__main__":
    sys.exit(main())
