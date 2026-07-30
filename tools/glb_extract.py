#!/usr/bin/env python3
"""Extract assets from Raptor: Call of the Shadows .GLB archives.

Lists archive contents, decrypts items, and decodes both image formats
(flat pics and sprite-segment pics) to PNG. Written for the PSP port to
compose the XMB icon/background from the game's own art, but generally
useful for modding.

Usage:
    python glb_extract.py FILE0001.GLB out/            # list items
    python glb_extract.py FILE0001.GLB out/ 19 34      # dump items 19, 34

Format notes (see also src/glbapi.cpp and the DOS Game Modding Wiki):
- Archive: 28-byte KEYFILE header {u32 opt, u32 offset, u32 filesize,
  char name[16]}, encrypted with key "32768GLB" (additive feedback
  cipher, seed index 25 % keylen). Header's `offset` = item count; then
  one encrypted KEYFILE per item (opt=1 means item data is encrypted).
- Pic item: 20-byte header {i32 type, i32 opt1, i32 opt2, i32 width,
  i32 height}. type 1 = flat 8-bpp pixels, width*height bytes.
  type 0 = sprite segments: repeated {i32 x, i32 y, i32 offset,
  i32 length} + `length` pixel bytes, terminated by offset == -1.
- Palette items (*_DAT, 768 bytes) are VGA 6-bit; scale by 255/63.
  Item 0 of FILE0001.GLB (PALETTE_DAT) is the main game palette.

Requires Pillow (pip install pillow).
"""
import struct, sys, os
from PIL import Image

KEY = b"32768GLB"
SEED = 0x19

def decrypt(data: bytes) -> bytes:
    out = bytearray(len(data))
    kidx = SEED % len(KEY)
    prev = KEY[kidx]
    for i, b in enumerate(data):
        out[i] = (b - KEY[kidx] - prev) % 256
        prev = b
        kidx = (kidx + 1) % len(KEY)
    return bytes(out)

def load_glb(path):
    with open(path, "rb") as f:
        raw = f.read()
    head = decrypt(raw[:28])
    _, num_items, _ = struct.unpack_from("<III", head, 0)
    items = []
    for n in range(num_items):
        off = 28 * (n + 1)
        opt, offset, size = struct.unpack_from("<III", decrypt(raw[off:off+28]), 0)
        name = decrypt(raw[off:off+28])[12:28].split(b"\0")[0].decode("ascii", "replace")
        items.append((name, opt, offset, size))
    return raw, items

def get_item(raw, items, idx):
    name, opt, offset, size = items[idx]
    data = raw[offset:offset+size]
    if opt == 1:
        data = decrypt(data)
    return name, data

def decode_flat(data, palette):
    typ, o1, o2, w, h = struct.unpack_from("<iiiii", data, 0)
    if w <= 0 or h <= 0 or w > 1024 or h > 1024 or 20 + w * h > len(data):
        return None
    img = Image.frombytes("P", (w, h), data[20:20 + w * h])
    img.putpalette(palette)
    return img.convert("RGB")

def decode_sprite(data, palette):
    typ, o1, o2, w, h = struct.unpack_from("<iiiii", data, 0)
    if w <= 0 or h <= 0 or w > 1024 or h > 1024:
        return None
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    px = img.load()
    p = 20
    while p + 16 <= len(data):
        x, y, off, ln = struct.unpack_from("<iiii", data, p)
        if off == -1:
            break
        p += 16
        for k in range(ln):
            c = data[p + k]
            xx = x + k
            if 0 <= xx < w and 0 <= y < h:
                px[xx, y] = (palette[c*3], palette[c*3+1], palette[c*3+2], 255)
        p += ln
    return img

def save_pic(data, name, outdir, palette):
    if len(data) < 20:
        return None
    typ = struct.unpack_from("<i", data, 0)[0]
    img = decode_flat(data, palette) if typ == 1 else decode_sprite(data, palette)
    if img is None:
        return None
    out = os.path.join(outdir, f"{name}.png")
    img.save(out)
    return (out, img.size, typ)

def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return
    glb = sys.argv[1]
    outdir = sys.argv[2]
    wanted = sys.argv[3:]          # item indices to dump; empty = list only
    os.makedirs(outdir, exist_ok=True)
    raw, items = load_glb(glb)
    print(f"{len(items)} items")
    # main palette = item 0 (PALETTE_DAT), VGA 6-bit
    _, pal_data = get_item(raw, items, 0)
    palette = [min(255, v * 255 // 63) for v in pal_data[:768]]
    if not wanted:
        for i, (name, opt, offset, size) in enumerate(items):
            print(f"{i:4d} 0x{i:03x} {name:18s} opt={opt} size={size}")
        return
    for a in wanted:
        i = int(a, 0)
        name, data = get_item(raw, items, i)
        r = save_pic(data, f"{i:03d}_{name}", outdir, palette)
        print(i, name, r if r else f"(not decodable as a pic, {len(data)} bytes)")

if __name__ == "__main__":
    main()
