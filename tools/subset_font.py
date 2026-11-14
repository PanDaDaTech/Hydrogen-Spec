# -*- coding: utf-8 -*-
"""Rebuild the embedded MiSans subsets and verify UI character coverage.

Two faces are embedded, both with family name "MiSans" so GDI/GDI+ can pick
them by weight alone:

  * Medium  (usWeightClass 500) - body text, FW_NORMAL requests
  * Bold    (usWeightClass 700) - titles, FW_BOLD requests

The bold face exists so titles use the *real* MiSans Bold outlines instead
of GDI's synthetic emboldening (which smears stems into grey fuzz once the
text is composited through per-pixel alpha).

The full fonts (~8 MB each) are not committed. Pass a path with --full
(single-face mode), or place them at:

    build/font-backup/MiSans-Medium.ttf
    build/font-backup/MiSans-Bold.ttf

Usage:
    python tools/subset_font.py                  # rebuild both faces
    python tools/subset_font.py --bold            # rebuild bold face only
    python tools/subset_font.py --medium          # rebuild medium face only
    python tools/subset_font.py --check           # verify both faces

Collected characters (identical for both faces):
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

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "hspec.cpp"
HARDWARE = ROOT / "tools" / "hardware_words.txt"

# face name -> (output, default full source, usWeightClass, is bold)
FACES = {
    "medium": (ROOT / "winres" / "MiSans-Medium.ttf",
               ROOT / "build" / "font-backup" / "MiSans-Medium.ttf",
               500, False),
    "bold":   (ROOT / "winres" / "MiSans-Bold.ttf",
               ROOT / "build" / "font-backup" / "MiSans-Bold.ttf",
               700, True),
}

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


def _fix_metadata(font, weight, bold):
    """GDI / GDI+ match by family + weight; fix MiSans' non-standard metadata.

    Both faces share family name "MiSans" and differ only by usWeightClass,
    which is exactly how CreateFontW(..., L"MiSans", FW_BOLD) picks the real
    bold face instead of synthesizing one.
    """
    os2 = font["OS/2"]
    os2.usWeightClass = weight
    # fsSelection bits: 0x01 ITALIC, 0x20 BOLD, 0x40 REGULAR
    os2.fsSelection = (os2.fsSelection & ~0x21) | (0x20 if bold else 0x40)

    nametbl = font["name"]
    nametbl.names = [r for r in nametbl.names if r.platformID == 3]
    for r in nametbl.names:
        if r.nameID == 1:
            r.string = "MiSans"
        elif r.nameID == 2:
            r.string = "Bold" if bold else "Medium"


def build(full, out, weight, bold):
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

    _fix_metadata(font, weight, bold)

    font.save(str(out))
    print("[OK] wrote %s: %d chars, %d bytes"
          % (out.relative_to(ROOT), len(chars), out.stat().st_size))


def check(out, weight, bold):
    from fontTools.ttLib import TTFont

    chars = collect("--minimal" not in sys.argv)
    font = TTFont(str(out))
    cmap = font.getBestCmap()
    missing = sorted(c for c in chars if ord(c) >= 0x80 and ord(c) not in cmap)
    if missing:
        print("[ERR] %s: %d chars missing from the subset:" % (out.name, len(missing)))
        for c in missing:
            print("   U+%04X %s" % (ord(c), c))
        return False
    os2 = font["OS/2"]
    want_sel = 0x20 if bold else 0x40
    if os2.usWeightClass != weight or (os2.fsSelection & 0x61) != want_sel:
        print("[ERR] %s: OS/2 metadata is wrong (usWeightClass=%d fsSelection=0x%04X)"
              % (out.name, os2.usWeightClass, os2.fsSelection))
        return False
    fam = _family_name(font)
    if fam != "MiSans":
        print("[ERR] %s: family name is %r (expected MiSans)" % (out.name, fam))
        return False
    print("[OK] %s covers all UI chars (cmap %d, %d bytes)"
          % (out.name, len(cmap), out.stat().st_size))
    return True


def main():
    only = None
    for name in FACES:
        if "--" + name in sys.argv:
            only = name
    targets = [only] if only else list(FACES)

    if "--check" in sys.argv:
        ok = True
        for name in targets:
            out, _, weight, bold = FACES[name]
            ok = check(out, weight, bold) and ok
        return 0 if ok else 1

    full_override = None
    if "--full" in sys.argv:
        full_override = Path(sys.argv[sys.argv.index("--full") + 1])

    for name in targets:
        out, full, weight, bold = FACES[name]
        if full_override is not None and only == name:
            full = full_override
        if not full.exists():
            print("[ERR] full font not found: %s (use --full)" % full)
            return 1
        build(full, out, weight, bold)
    return 0


if __name__ == "__main__":
    sys.exit(main())
