#!/usr/bin/env python3
"""Generate the launcher's own icon (assets/breadbin-6502.png), used for desktop
entries and packages when no game pack icon is embedded.

Original, procedurally drawn art: a generic beige wedge-shaped home computer with
rows of keys. No logos or brand marks. Output is deterministic. Standard library only.

usage: python3 tools/make_icon.py [output.png]   (default: assets/breadbin-6502.png)
"""
import os
import struct
import sys
import zlib

SIZE = 256
SS = 4  # supersampling for smooth edges


def write_png(path, w, h, rgba):
    rows = b"".join(b"\0" + bytes(rgba[y * w * 4:(y + 1) * w * 4]) for y in range(h))

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(rows, 9)))
        f.write(chunk(b"IEND", b""))


def inside_round_rect(x, y, x0, y0, x1, y1, r):
    if x < x0 or x > x1 or y < y0 or y > y1:
        return False
    cx = min(max(x, x0 + r), x1 - r)
    cy = min(max(y, y0 + r), y1 - r)
    return (x - cx) ** 2 + (y - cy) ** 2 <= r * r


def shade(x, y):
    """Colour (r, g, b, a) at icon coordinates (0..1)."""
    # Wedge body: top edge slopes down towards the front (bottom of the icon).
    top = 0.30 - (x - 0.5) * 0.0
    body = inside_round_rect(x, y, 0.08, top, 0.92, 0.80, 0.06)
    # slanted top face: narrower at the back
    back_inset = 0.06 * (1 - (y - top) / 0.18) if y < top + 0.18 else 0.0
    if body and (x < 0.08 + back_inset or x > 0.92 - back_inset):
        body = False
    if not body:
        # soft shadow under the case
        if 0.10 < x < 0.90 and 0.80 < y < 0.86:
            d = (y - 0.80) / 0.06
            return (0.0, 0.0, 0.0, 0.35 * (1 - d))
        return (0.0, 0.0, 0.0, 0.0)
    # case colour: beige, darker towards the front lip
    if y > 0.70:
        return (0.62, 0.55, 0.45, 1.0)           # front lip
    base = (0.86, 0.80, 0.68)
    if y < top + 0.03:
        base = (0.93, 0.89, 0.79)                # highlight on the back edge
    # keyboard area: dark keys on a brown well
    kx0, kx1, ky0, ky1 = 0.16, 0.84, 0.40, 0.66
    if kx0 <= x <= kx1 and ky0 <= y <= ky1:
        well = (0.36, 0.29, 0.24, 1.0)
        rows = 4
        row_h = (ky1 - ky0) / rows
        r = min(int((y - ky0) / row_h), rows - 1)
        ry = (y - ky0) - r * row_h
        offset = (0.0, 0.02, 0.035, 0.05)[r]
        keys = 11 if r < 3 else 1
        if r == 3:  # space bar
            if 0.30 <= x <= 0.70 and 0.012 < ry < row_h - 0.012:
                return (0.20, 0.17, 0.15, 1.0)
            return well
        kw = (kx1 - kx0 - offset) / keys
        kx = (x - kx0 - offset)
        if kx < 0:
            return well
        k = int(kx / kw)
        fx = kx - k * kw
        if k < keys and 0.008 < fx < kw - 0.008 and 0.012 < ry < row_h - 0.012:
            top_face = ry < row_h * 0.55
            return (0.27, 0.23, 0.20, 1.0) if top_face else (0.18, 0.15, 0.13, 1.0)
        return well
    # power light
    if (x - 0.80) ** 2 + (y - 0.35) ** 2 < 0.018 ** 2:
        return (0.95, 0.20, 0.15, 1.0)
    return base + (1.0,)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "assets", "breadbin-6502.png")
    px = bytearray(SIZE * SIZE * 4)
    for y in range(SIZE):
        for x in range(SIZE):
            acc = [0.0, 0.0, 0.0, 0.0]
            for sy in range(SS):
                for sx in range(SS):
                    r, g, b, a = shade((x + (sx + 0.5) / SS) / SIZE, (y + (sy + 0.5) / SS) / SIZE)
                    acc[0] += r * a
                    acc[1] += g * a
                    acc[2] += b * a
                    acc[3] += a
            n = SS * SS
            a = acc[3] / n
            i = (y * SIZE + x) * 4
            if a > 0:
                px[i:i + 4] = bytes((round(acc[0] / acc[3] * 255), round(acc[1] / acc[3] * 255),
                                     round(acc[2] / acc[3] * 255), round(a * 255)))
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    write_png(out, SIZE, SIZE, px)
    print("wrote", out)


if __name__ == "__main__":
    main()
