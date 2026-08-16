"""make_mod.py - pack a folder of PNGs (and an optional manifest) into a
Raptor Enhanced mod .GLB.

Usage:
    python tools/make_mod.py <mod-folder> [-o Out.glb] [--game <raptor dir>]

The folder may contain:
    modinfo.txt      first line: display name, second: description, then
                     optional "ALIAS <base-item> <other-base-item>" lines
    NAME.png         becomes item NAME, overriding the base item of the
                     same name (or adding a new item if the name is new)
    NAME.2.png       the SECOND base item with that name (multi-frame art
                     like LPLAYER_PIC), .3.png the third, and so on

PNGs are quantized to the game palette (read from FILE0000.GLB in --game,
default: the current directory); pixels with alpha < 128 are transparent.
Each image is encoded in the same format (block or sprite) as the base item
it overrides, so both UI art and in-game sprites work. Requires Pillow.
"""
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "pkg" / "release" / "DeltaSector-optional"))
from raptor_glb import (  # noqa: E402
    GlbFile, GlbItem, GlbSet, load_palette, quantize_rgba,
    encode_pic, GTYPE_PIC,
)


def item_sort_key(path):
    parts = path.stem.split(".")
    name = parts[0].upper()
    occurrence = int(parts[1]) if len(parts) > 1 and parts[1].isdigit() else 1
    return (name, occurrence)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("folder", type=Path)
    ap.add_argument("-o", "--out", type=Path, default=None)
    ap.add_argument("--game", type=Path, default=Path("."),
                    help="folder containing FILE0000.GLB (for the palette)")
    args = ap.parse_args()

    try:
        from PIL import Image
    except ImportError:
        sys.exit("Pillow is required: pip install pillow")

    pngs = sorted(args.folder.glob("*.png"), key=item_sort_key)
    manifest = args.folder / "modinfo.txt"

    if not pngs and not manifest.exists():
        sys.exit(f"{args.folder}: nothing to pack (no *.png, no modinfo.txt)")

    glbs = None
    palette = None

    if pngs:
        try:
            glbs = GlbSet(args.game)
            palette = load_palette(glbs)
        except (FileNotFoundError, KeyError):
            sys.exit(f"--game {args.game}: FILE0000.GLB with PALETTE_DAT needed to quantize images")

    mod = GlbFile()

    if manifest.exists():
        mod.items.append(GlbItem(0, "MODINFO_TXT", manifest.read_bytes()))

    for png in pngs:
        name, occurrence = item_sort_key(png)
        if len(name) > 15:
            sys.exit(f"{png.name}: item name {name} is over 15 characters")
        img = Image.open(png).convert("RGBA")
        pixels, mask = quantize_rgba(img.width, img.height, img.tobytes(), palette)
        import struct
        gtype = GTYPE_PIC
        # reuse the base item's encoding so sprites stay sprites
        seen = 0
        for num in sorted(glbs.files):
            for item in glbs.files[num].items:
                if item.name == name and item.data:
                    seen += 1
                    if seen == occurrence:
                        gtype = struct.unpack_from("<i", item.data)[0]
        data = encode_pic(img.width, img.height, pixels, mask, gtype)
        mod.items.append(GlbItem(0, name, data))
        print(f"  {name} ({img.width}x{img.height}, {'sprite' if gtype == 0 else 'block'}, occurrence {occurrence})")

    out = args.out or args.folder.with_suffix(".glb")
    out.write_bytes(mod.build())
    print(f"{out}: {len(mod.items)} item(s)")


if __name__ == "__main__":
    main()
