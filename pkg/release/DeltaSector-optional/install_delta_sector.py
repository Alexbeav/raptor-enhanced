"""
install_delta_sector.py - Add the community "Delta Sector" 4th campaign to
YOUR OWN Raptor data files.

It reads FILE0001.GLB and FILE0004.GLB from your Raptor folder, makes .bak
backups, and writes patched copies that add:
  * a "DELTA SECTOR" row on the ship-computer sector-select screen, and
  * 9 new waves (MAP1G4..MAP9G4), remixed from the game's own terrain.

No game data is distributed with this script - it only transforms the files
you already own. Requires the Enhanced raptor.exe (Delta Sector needs the
new engine); the stock game will ignore the extra maps.

Usage:
    python install_delta_sector.py  "C:\\path\\to\\your\\Raptor folder"
    python install_delta_sector.py            (uses the current folder)

Requires Python 3.10+.  raptor_glb.py must sit next to this script.
"""
import shutil
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from raptor_glb import (GlbFile, GlbItem, GlbSet, build_map,
                        normalize_spawn_order, parse_map, spawn_groups)

# ---- 9 waves: each splices three terrain bands (start / middle / end) -------
RECIPES = {
    1: ("MAP1G3_MAP", "MAP1G2_MAP", "MAP1G1_MAP"), 2: ("MAP2G1_MAP", "MAP2G3_MAP", "MAP2G2_MAP"),
    3: ("MAP3G2_MAP", "MAP3G1_MAP", "MAP3G3_MAP"), 4: ("MAP4G3_MAP", "MAP4G1_MAP", "MAP4G2_MAP"),
    5: ("MAP5G1_MAP", "MAP5G3_MAP", "MAP5G2_MAP"), 6: ("MAP6G2_MAP", "MAP6G1_MAP", "MAP6G3_MAP"),
    7: ("MAP7G3_MAP", "MAP7G2_MAP", "MAP7G1_MAP"), 8: ("MAP8G1_MAP", "MAP8G2_MAP", "MAP8G3_MAP"),
    9: ("MAP9G2_MAP", "MAP9G1_MAP", "MAP9G3_MAP"),
}
MAP_COLS, MAP_ROWS = 9, 150
FLD, H_TXTOFS, H_NUMFLDS, F_Y, F_TXTOFF = 148, 80, 96, 128, 140
ROWS = {4: 39, 5: 61, 10: 83, 12: 105, 11: 127}     # respaced sector rows

# Fan-written Delta ending text (END4_TXT), same script format as END1-3_TXT.
DELTA_END_TXT = (
    "TEXT_POS 30 13 \r\nTEXT_COLOR 84\r\n"
    "    The Delta Sector falls silent\r\n"
    + "".join("TEXT_DOWN 5\r\n" + line + "\r\n" for line in (
        "behind you.  Three campaigns' worth",
        "of defenses, thrown together into",
        "one last desperate gauntlet - and",
        "you flew through all of it.  The",
        "bounty clears before you even dock.",
        "Somewhere out beyond the charts,",
        "someone is already hiring for the",
        "next war.  You'll be ready.",
    ))
).encode("ascii")


def base_tile(level):
    c = {}
    for row in level["tiles"]:
        for cell in row:
            c[(cell["flats"], cell["fgame"])] = c.get((cell["flats"], cell["fgame"]), 0) + 1
    return max(c, key=c.get)


def plain_rows(level):
    b = base_tile(level)
    return {r for r, row in enumerate(level["tiles"]) if sum((c["flats"], c["fgame"]) == b for c in row) >= 7}


def nearest_plain(level, target, lo, hi):
    p = plain_rows(level)
    for d in range(MAP_ROWS):
        for r in (target - d, target + d):
            if lo <= r <= hi and r in p:
                return r
    return target


def band_sprites(level, src_lo, src_hi, dst_lo, mirror):
    out = []
    for g in spawn_groups(level["sprites"]):
        if src_lo <= g[0]["y"] < src_hi:
            for s in g:
                out.append({**s, "y": min(139, s["y"] - src_lo + dst_lo),
                            "x": (MAP_COLS - 1 - s["x"]) if mirror else s["x"]})
    return out


def make_wave(glbs, wave):
    bot_n, mid_n, top_n = RECIPES[wave]
    top, mid, bot = (parse_map(glbs.by_name(n).data) for n in (top_n, mid_n, bot_n))
    cut1 = nearest_plain(top, 50, 35, 70)
    mid_start = nearest_plain(mid, 28, 15, 55)
    mp, bp = plain_rows(mid), plain_rows(bot)
    cut2 = next((r for d in range(16) for r in (100 - d, 100 + d)
                 if 85 <= r <= 118 and r in bp and (r - cut1 + mid_start - 1) in mp),
                nearest_plain(bot, 100, 85, 115))
    tiles = []
    for r in range(MAP_ROWS):
        if r < cut1:
            tiles.append([dict(c) for c in top["tiles"][r]])
        elif r < cut2:
            tiles.append([dict(c) for c in mid["tiles"][min(MAP_ROWS - 1, r - cut1 + mid_start)]])
        else:
            tiles.append([dict(c) for c in bot["tiles"][r]])
    sprites = (band_sprites(bot, cut2, MAP_ROWS, cut2, False)
               + band_sprites(mid, mid_start, mid_start + (cut2 - cut1), cut1, True)
               + band_sprites(top, 0, cut1, 0, False))
    return build_map({"rows": MAP_ROWS, "cols": MAP_COLS, "tiles": tiles,
                      "sprites": normalize_spawn_order(sprites)})


def patch_shipcomp(glb):
    """Add a visible DELTA SECTOR row to the sector-select window (FILE0001)."""
    gi = glb.index_of("SHIPCOMP_SWD")
    d = bytearray(glb.items[gi].data)
    fldofs = struct.unpack_from("<i", d, 76)[0]
    txtofs = struct.unpack_from("<i", d, H_TXTOFS)[0]
    n = struct.unpack_from("<i", d, H_NUMFLDS)[0]
    if n != 12:
        print("  (ship-computer already patched or unexpected - skipping button)")
        return
    fields = [bytearray(d[fldofs + i * FLD: fldofs + (i + 1) * FLD]) for i in range(n)]
    text = bytes(d[txtofs:])
    g4 = bytearray(fields[10]); g4[32:48] = b"GAME4".ljust(16, b"\0")
    fields.append(g4)
    for idx, y in ROWS.items():
        struct.pack_into("<i", fields[idx], F_Y, y)
    for f in fields[:12]:
        struct.pack_into("<i", f, F_TXTOFF, struct.unpack_from("<i", f, F_TXTOFF)[0] + 148)
    new_txtofs = fldofs + 13 * FLD
    new_text = bytearray(text); pos = len(new_text); new_text += b"DELTA SECTOR\0"
    struct.pack_into("<i", g4, F_TXTOFF, (new_txtofs + pos) - (fldofs + 12 * FLD))
    out = bytearray(d[:fldofs])
    struct.pack_into("<i", out, H_NUMFLDS, 13)
    struct.pack_into("<i", out, H_TXTOFS, new_txtofs)
    for f in fields:
        out += f
    out += new_text
    glb.items[gi].data = bytes(out)


def main():
    folder = Path(sys.argv[1]) if len(sys.argv) > 1 else Path.cwd()
    print(f"Raptor folder: {folder}")
    try:
        glbs = GlbSet(folder)
    except FileNotFoundError:
        sys.exit("No FILE000n.GLB found here. Pass your Raptor folder as an argument.")
    if glbs.item_id("MAP1G3_MAP") < 0 or 4 not in glbs.files:
        sys.exit("Delta Sector needs the full game (FILE0001-FILE0004). Not found.")

    def path_of(num):
        for name in (f"FILE{num:04}.GLB", f"file{num:04}.glb"):
            if (folder / name).exists():
                return folder / name
        return folder / f"FILE{num:04}.GLB"

    for num in (1, 4):
        p = path_of(num)
        bak = p.with_suffix(p.suffix + ".bak")
        if not bak.exists():
            shutil.copy(p, bak)
            print(f"  backup: {bak.name}")

    # FILE0004: append the 9 Delta maps
    glb4 = glbs.files[4]
    for wave in range(1, 10):
        name = f"MAP{wave}G4_MAP"
        data = make_wave(glbs, wave)
        idx = glb4.index_of(name)
        if idx >= 0:
            glb4.items[idx].data = data
        else:
            glb4.items.append(GlbItem(0, name, data))
    end_idx = glb4.index_of("END4_TXT")
    if end_idx >= 0:
        glb4.items[end_idx].data = DELTA_END_TXT
    else:
        glb4.items.append(GlbItem(1, "END4_TXT", DELTA_END_TXT))
    path_of(4).write_bytes(glb4.build())
    print("  FILE0004: 9 Delta waves + ending text added")

    # FILE0001: add the visible menu row
    patch_shipcomp(glbs.files[1])
    path_of(1).write_bytes(glbs.files[1].build())
    print("  FILE0001: DELTA SECTOR menu row added")

    print("\nDone. Launch raptor.exe, go to the hangar -> Ship Computer, and pick\n"
          "DELTA SECTOR (or press D). To undo, restore the .bak files.")


if __name__ == "__main__":
    main()
