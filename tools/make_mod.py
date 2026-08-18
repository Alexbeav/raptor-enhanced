"""make_mod.py - pack a folder of PNGs (and an optional manifest) into a
Raptor Reclawed mod .GLB.

Usage:
    python tools/make_mod.py <mod-folder> [-o Out.glb] [--game <raptor dir>]

The folder may contain:
    modinfo.txt      first line: display name, second: description, then
                     optional "ALIAS <base-item> <other-base-item>" lines
    NAME.png         becomes item NAME, overriding the base item of the
                     same name (or adding a new item if the name is new)
    NAME.2.png       the SECOND base item with that name (multi-frame art
                     like LPLAYER_PIC), .3.png the third, and so on
    NAME.txt         a raw text item (e.g. PLAYRGUN_TXT gun config)
    GUN_FX+4.wav     a digitized sound: converted to the game's 8-bit
                     sample format; the "+4" targets the digital sub-item
                     four slots after the GUN_FX label (the engine's
                     SND_DIGITAL offset - use +4 for every *_FX sound)

PNGs are quantized to the game palette (read from FILE0000.GLB in --game,
default: the current directory); pixels with alpha < 128 are transparent.
Each image is encoded in the same format (block or sprite) as the base item
it overrides, so both UI art and in-game sprites work.

Only *.png packing needs Pillow; text-only mods (aliases, PLAYRGUN_TXT)
and sound mods build with a stock Python 3.10+ install.
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


def wav_to_dsp(path):
    """WAV -> the engine's dsp_t sample: int16 format(3), int16 freq,
    int32 length, unsigned 8-bit mono PCM. (No audioop: removed in 3.13.)"""
    import struct
    import wave

    with wave.open(str(path), "rb") as w:
        rate = w.getframerate()
        width = w.getsampwidth()
        channels = w.getnchannels()
        frames = w.readframes(w.getnframes())

    if rate > 32767:
        raise SystemExit(f"{path.name}: sample rate {rate} exceeds the format's int16 field")
    if width not in (1, 2):
        raise SystemExit(f"{path.name}: only 8/16-bit PCM WAVs supported")

    if width == 2:
        ints = struct.unpack(f"<{len(frames) // 2}h", frames)
    else:
        ints = [b - 128 << 8 for b in frames]  # centre, widen for the mix

    if channels > 1:
        ints = [sum(ints[i:i + channels]) // channels for i in range(0, len(ints), channels)]

    pcm = bytes(((s >> 8) + 128) & 0xFF for s in ints)
    return struct.pack("<hhi", 3, rate, len(pcm)) + pcm


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("folder", type=Path)
    ap.add_argument("-o", "--out", type=Path, default=None)
    ap.add_argument("--game", type=Path, default=Path("."),
                    help="folder containing FILE0000.GLB (for the palette)")
    args = ap.parse_args()

    pngs = sorted(args.folder.glob("*.png"), key=item_sort_key)
    wavs = sorted(args.folder.glob("*.wav"), key=item_sort_key)
    txts = sorted(p for p in args.folder.glob("*.txt") if p.name.lower() != "modinfo.txt")
    manifest = args.folder / "modinfo.txt"

    if not pngs and not wavs and not txts and not manifest.exists():
        sys.exit(f"{args.folder}: nothing to pack (no *.png/*.wav/*.txt, no modinfo.txt)")

    glbs = None
    palette = None

    if pngs:
        try:
            from PIL import Image
        except ImportError:
            sys.exit("Pillow is required to pack images: pip install pillow")
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
        base_w = base_h = None
        # reuse the base item's encoding so sprites stay sprites
        seen = 0
        for num in sorted(glbs.files):
            for item in glbs.files[num].items:
                if item.name == name and item.data:
                    seen += 1
                    if seen == occurrence:
                        gtype, _, _, base_w, base_h = struct.unpack_from("<5i", item.data)
        anchor, header = (0, 0), None
        oversize = ""
        if (gtype == 0 and base_w is not None
                and (img.width > base_w or img.height > base_h)):
            # sprite art larger than stock: center it on the base footprint
            # so the game's positioning and collision stay unchanged
            anchor = ((base_w - img.width) // 2, (base_h - img.height) // 2)
            header = (base_w, base_h)
            oversize = f", centered on {base_w}x{base_h}"
        data = encode_pic(img.width, img.height, pixels, mask, gtype,
                          anchor=anchor, header_size=header)
        mod.items.append(GlbItem(0, name, data))
        print(f"  {name} ({img.width}x{img.height}, {'sprite' if gtype == 0 else 'block'}, occurrence {occurrence}{oversize})")

    for txt in txts:
        name = txt.stem.split(".")[0].upper()
        if len(name) > 15:
            sys.exit(f"{txt.name}: item name {name} is over 15 characters")
        mod.items.append(GlbItem(0, name, txt.read_bytes()))
        print(f"  {name} (text, {txt.stat().st_size} bytes)")

    for wav in wavs:
        name = wav.stem.split(".")[0].upper()
        if len(name) > 15:
            sys.exit(f"{wav.name}: item name {name} is over 15 characters")
        data = wav_to_dsp(wav)
        mod.items.append(GlbItem(0, name, data))
        print(f"  {name} (sound, {len(data) - 8} samples)")

    out = args.out or args.folder.with_suffix(".glb")
    out.write_bytes(mod.build())
    print(f"{out}: {len(mod.items)} item(s)")


if __name__ == "__main__":
    main()
