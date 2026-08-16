"""Builds the engine's mod-system data files (all original content):

  pkg/release/rapmenu.glb        - MODMENU_SWD, the in-game MODS window
  pkg/release/mods/NightOps.glb  - first mod: night-camo Raptor everywhere
                                   (a manifest-only mod; no game data inside)

SWD layout per swdapi.h (SWIN 120 bytes + SFIELD32 148 bytes each + text);
validated byte-exactly by the raptor-window-maker test suite.
"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "pkg" / "release" / "DeltaSector-optional"))
from raptor_glb import GlbFile, GlbItem  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent

FLD_TEXT, FLD_BUTTON, FLD_CLOSE = 1, 2, 5
FILL, INVISABLE = 0, 4
MODS_ROWS = 8  # keep in sync with src/modapi.h MOD_MAX


def swin(name, *, x, y, lx, ly, color, numflds, txtofs, firstfld=1):
    return struct.pack(
        "<8i16s16s14i",
        0, 0, 1, 1, 0, 0,          # version, swdsize, arrowflag, display, opt3, opt4
        0, 0,                      # id, type
        name.encode(), b"",        # name, item_name
        -1, FILL, 1,               # item, picflag(FILL), lock
        120, txtofs, firstfld,     # fldofs, txtofs, firstfld
        0, color, numflds,         # opt, color, numflds
        x, y, lx, ly, 1,           # x, y, lx, ly, shadow
    )


def sfield(*, opt, fid, x, y, lx, ly, txtoff, name="", font="FONT1_FNT",
           basecolor=82, maxchars=0, color=0, lite=15, selectable=0,
           hotkey=0, picflag=0):
    return struct.pack(
        "<8i16s16si16s16i",
        opt, fid, hotkey, 0, 0, 0, 0, 0,
        name.encode(), b"",
        -1, font.encode(),
        -1, basecolor, maxchars, picflag,
        color, lite, 0, 0, 0, selectable,
        x, y, lx, ly, txtoff, 0,
    )


FLD_BUMPIN = 7


def build_modmenu():
    """The MODS popup, styled after the shipped OPTS_SWD conventions:
    FONT1 title, FONT2 body text, an inset bumpin panel framing the rows,
    window color 1. Row labels are rewritten at runtime via
    SWD_SetFieldText, so the text area reserves a fixed-size slot per row.
    Field INDEXES must stay in sync with winids.h: 0 close, 1 title,
    2..2+MODS_ROWS-1 rows, then footer."""
    ROW_CHARS = 36
    ROW_H = 12
    fields = []
    labels = []

    def add(label_text, reserve=None, **kw):
        labels.append((label_text, reserve or len(label_text) + 1))
        fields.append(dict(kw))

    panel_ly = MODS_ROWS * ROW_H + 8
    ly = 27 + panel_ly + 18

    # fields draw in index order, so the inset panel comes before the rows;
    # winids.h MODM_* indexes mirror this layout exactly
    add("", 2, opt=FLD_CLOSE, fid=0, x=4, y=4, lx=12, ly=12, color=8, lite=15)
    add("MOD FILES", opt=FLD_TEXT, fid=1, x=58, y=7, lx=84, ly=14,
        maxchars=10, font="FONT1_FNT", basecolor=82)
    add("", 2, opt=FLD_BUMPIN, fid=2, x=5, y=27, lx=190,
        ly=panel_ly, color=1, lite=15)
    for i in range(MODS_ROWS):
        add("", ROW_CHARS + 1, opt=FLD_BUTTON, fid=3 + i, x=10, y=31 + i * ROW_H,
            lx=180, ly=ROW_H, maxchars=ROW_CHARS, picflag=INVISABLE,
            selectable=1, font="FONT2_FNT", basecolor=82)
    add("CHANGES APPLY AFTER RESTART", opt=FLD_TEXT, fid=3 + MODS_ROWS,
        x=10, y=31 + panel_ly + 4, lx=180, ly=8, maxchars=28,
        font="FONT2_FNT", basecolor=48)

    n = len(fields)
    txtofs = 120 + n * 148
    # lay out reserved text slots and compute per-record txtoff
    text = bytearray()
    for i, (label_text, reserve) in enumerate(labels):
        fields[i]["txtoff"] = (txtofs + len(text)) - (120 + i * 148)
        slot = bytearray(reserve)
        slot[:len(label_text)] = label_text.encode()
        text += slot

    out = swin("MODMENU", x=60, y=22, lx=200, ly=ly,
               color=1, numflds=n, txtofs=txtofs, firstfld=3)
    for f in fields:
        out += sfield(**f)
    return bytes(out + text)


def main():
    menu = GlbFile()
    menu.items.append(GlbItem(0, "MODMENU_SWD", build_modmenu()))
    (ROOT / "pkg/release/rapmenu.glb").write_bytes(menu.build())
    print(f"rapmenu.glb: MODMENU_SWD {len(menu.items[0].data)} bytes")

    night = GlbFile()
    night.items.append(GlbItem(0, "MODINFO_TXT", (
        "Night Ops\n"
        "Fly the night-camo Raptor on every mission.\n"
        "ALIAS LPLAYER_PIC DPLAYER_PIC\n"
    ).encode()))
    mods = ROOT / "pkg/release/mods"
    mods.mkdir(exist_ok=True)
    (mods / "NightOps.glb").write_bytes(night.build())
    print(f"mods/NightOps.glb: {len(night.items[0].data)}-byte manifest, no game data")


if __name__ == "__main__":
    main()
