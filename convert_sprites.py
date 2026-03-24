"""
PlatformIO pre-build script: convert PNG sprite sheets to RGB565 C header.

Reads pixel_chess_16x16_icons/set_regular/ PNGs (no Pillow — pure stdlib).
Outputs src/chess_sprites.h with PROGMEM uint16_t arrays.
Skips regeneration if header is newer than all source PNGs.
"""

Import("env")

import os
import struct
import zlib

SCRIPT_DIR = os.path.dirname(os.path.abspath("platformio.ini"))
SPRITE_DIR = os.path.join(SCRIPT_DIR, "pixel_chess_16x16_icons", "set_regular")
OUTPUT_FILE = os.path.join(SCRIPT_DIR, "src", "chess_sprites.h")

PIECE_W = 15
PIECE_H = 15
NUM_PIECES = 6  # pawn, knight, bishop, rook, queen, king

# Transparent color key in RGB565 — use magenta (0xF81F) which is unlikely in sprites
TRANSPARENT_RGB565 = 0xF81F


def read_png_rgba(path):
    """Parse a PNG file into (width, height, [r,g,b,a,...]) using only stdlib."""
    with open(path, "rb") as f:
        sig = f.read(8)
        if sig != b"\x89PNG\r\n\x1a\n":
            raise ValueError(f"Not a PNG: {path}")

        chunks = {}
        idat_data = b""
        while True:
            hdr = f.read(8)
            if len(hdr) < 8:
                break
            length, ctype = struct.unpack(">I4s", hdr)
            data = f.read(length)
            f.read(4)  # CRC
            ctype = ctype.decode("ascii")
            if ctype == "IHDR":
                chunks["IHDR"] = data
            elif ctype == "IDAT":
                idat_data += data
            elif ctype == "PLTE":
                chunks["PLTE"] = data
            elif ctype == "tRNS":
                chunks["tRNS"] = data
            elif ctype == "IEND":
                break

    ihdr = chunks["IHDR"]
    w, h, bit_depth, color_type = struct.unpack(">IIBB", ihdr[:10])

    raw = zlib.decompress(idat_data)

    pixels = []

    if color_type == 6 and bit_depth == 8:
        # RGBA — 4 bytes per pixel + 1 filter byte per row
        stride = w * 4 + 1
        prev_row = bytearray(w * 4)
        for y in range(h):
            row_start = y * stride
            filt = raw[row_start]
            row_data = bytearray(raw[row_start + 1 : row_start + stride])
            _unfilter(filt, row_data, prev_row, 4)
            prev_row = row_data
            for x in range(w):
                off = x * 4
                pixels.extend(row_data[off : off + 4])

    elif color_type == 2 and bit_depth == 8:
        # RGB — 3 bytes per pixel
        stride = w * 3 + 1
        prev_row = bytearray(w * 3)
        trns = chunks.get("tRNS")
        for y in range(h):
            row_start = y * stride
            filt = raw[row_start]
            row_data = bytearray(raw[row_start + 1 : row_start + stride])
            _unfilter(filt, row_data, prev_row, 3)
            prev_row = row_data
            for x in range(w):
                off = x * 3
                r, g, b = row_data[off], row_data[off + 1], row_data[off + 2]
                a = 255
                if trns and len(trns) >= 6:
                    tr = struct.unpack(">HHH", trns[:6])
                    if r == tr[0] and g == tr[1] and b == tr[2]:
                        a = 0
                pixels.extend([r, g, b, a])

    elif color_type == 3 and bit_depth == 8:
        # Indexed — palette based
        plte = chunks["PLTE"]
        trns = chunks.get("tRNS", b"")
        palette = []
        for i in range(len(plte) // 3):
            r, g, b = plte[i * 3], plte[i * 3 + 1], plte[i * 3 + 2]
            a = trns[i] if i < len(trns) else 255
            palette.append((r, g, b, a))

        stride = w + 1
        prev_row = bytearray(w)
        for y in range(h):
            row_start = y * stride
            filt = raw[row_start]
            row_data = bytearray(raw[row_start + 1 : row_start + stride])
            _unfilter(filt, row_data, prev_row, 1)
            prev_row = row_data
            for x in range(w):
                idx = row_data[x]
                pixels.extend(palette[idx] if idx < len(palette) else (0, 0, 0, 0))

    else:
        raise ValueError(f"Unsupported PNG format: depth={bit_depth} type={color_type}")

    return w, h, pixels


def _unfilter(filt, row, prev, bpp):
    """Apply PNG row unfiltering in-place."""
    if filt == 0:
        pass
    elif filt == 1:  # Sub
        for i in range(bpp, len(row)):
            row[i] = (row[i] + row[i - bpp]) & 0xFF
    elif filt == 2:  # Up
        for i in range(len(row)):
            row[i] = (row[i] + prev[i]) & 0xFF
    elif filt == 3:  # Average
        for i in range(len(row)):
            a = row[i - bpp] if i >= bpp else 0
            row[i] = (row[i] + (a + prev[i]) // 2) & 0xFF
    elif filt == 4:  # Paeth
        for i in range(len(row)):
            a = row[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            row[i] = (row[i] + _paeth(a, b, c)) & 0xFF


def _paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    elif pb <= pc:
        return b
    return c


def rgba_to_rgb565(r, g, b, a):
    """Convert RGBA pixel to RGB565, returning transparent key if alpha < 128."""
    if a < 128:
        return TRANSPARENT_RGB565
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def extract_piece_sprites(png_path):
    """Extract 6 piece sprites (each PIECE_W x PIECE_H) from a horizontal sheet."""
    w, h, pixels = read_png_rgba(png_path)
    sprites = []
    for i in range(NUM_PIECES):
        sprite = []
        for y in range(PIECE_H):
            for x in range(PIECE_W):
                px = i * PIECE_W + x
                if px < w and y < h:
                    off = (y * w + px) * 4
                    r, g, b, a = pixels[off], pixels[off + 1], pixels[off + 2], pixels[off + 3]
                    sprite.append(rgba_to_rgb565(r, g, b, a))
                else:
                    sprite.append(TRANSPARENT_RGB565)
        sprites.append(sprite)
    return sprites


def extract_single_sprite(png_path, sw, sh):
    """Extract a single sprite of size sw x sh from a PNG."""
    w, h, pixels = read_png_rgba(png_path)
    sprite = []
    for y in range(sh):
        for x in range(sw):
            if x < w and y < h:
                off = (y * w + x) * 4
                r, g, b, a = pixels[off], pixels[off + 1], pixels[off + 2], pixels[off + 3]
                sprite.append(rgba_to_rgb565(r, g, b, a))
            else:
                sprite.append(TRANSPARENT_RGB565)
    return sprite


def format_array(name, data, width):
    """Format a uint16_t array as a C PROGMEM constant."""
    lines = [f"static const uint16_t {name}[{len(data)}] PROGMEM = {{"]
    for row in range(0, len(data), width):
        vals = ", ".join(f"0x{v:04X}" for v in data[row : row + width])
        lines.append(f"    {vals},")
    lines.append("};")
    return "\n".join(lines)


def needs_rebuild(output, sources):
    """Check if output needs rebuilding based on file modification times."""
    if not os.path.exists(output):
        return True
    out_mtime = os.path.getmtime(output)
    for src in sources:
        if os.path.exists(src) and os.path.getmtime(src) > out_mtime:
            return True
    return False


def generate_header():
    white_png = os.path.join(SPRITE_DIR, "pieces_white_1.png")
    black_png = os.path.join(SPRITE_DIR, "pieces_black_1.png")
    circle_png = os.path.join(SPRITE_DIR, "circle.png")

    script_path = os.path.join(SCRIPT_DIR, "convert_sprites.py")
    sources = [white_png, black_png, circle_png, script_path]

    if not needs_rebuild(OUTPUT_FILE, sources):
        print("chess_sprites.h is up to date, skipping conversion")
        return

    print("Converting chess sprites to RGB565...")

    # Order of pieces in the PNG sprite sheets (left to right)
    piece_names = ["ROOK", "KNIGHT", "BISHOP", "KING", "QUEEN", "PAWN"]
    # Order matching PieceType enum (Pawn=1..King=6) for lookup tables
    enum_order = ["PAWN", "KNIGHT", "BISHOP", "ROOK", "QUEEN", "KING"]

    white_sprites = extract_piece_sprites(white_png)
    black_sprites = extract_piece_sprites(black_png)
    circle_sprite = extract_single_sprite(circle_png, PIECE_W, PIECE_H)

    out = []
    out.append("// Auto-generated by convert_sprites.py — DO NOT EDIT")
    out.append("#pragma once")
    out.append("#include <stdint.h>")
    out.append("#ifdef ESP32")
    out.append("#include <pgmspace.h>")
    out.append("#else")
    out.append("#define PROGMEM")
    out.append("#endif")
    out.append("")
    out.append(f"#define CHESS_SPRITE_W {PIECE_W}")
    out.append(f"#define CHESS_SPRITE_H {PIECE_H}")
    out.append(f"#define CHESS_SPRITE_TRANSPARENT 0x{TRANSPARENT_RGB565:04X}")
    out.append("")

    # Individual piece arrays
    for i, name in enumerate(piece_names):
        out.append(format_array(f"SPRITE_WHITE_{name}", white_sprites[i], PIECE_W))
        out.append("")
        out.append(format_array(f"SPRITE_BLACK_{name}", black_sprites[i], PIECE_W))
        out.append("")

    # Lookup tables
    out.append("// Indexed by PieceType enum value (Pawn=1..King=6)")
    out.append("static const uint16_t* const SPRITES_WHITE[] PROGMEM = {")
    out.append("    nullptr,  // None=0")
    for name in enum_order:
        out.append(f"    SPRITE_WHITE_{name},")
    out.append("};")
    out.append("")
    out.append("static const uint16_t* const SPRITES_BLACK[] PROGMEM = {")
    out.append("    nullptr,  // None=0")
    for name in enum_order:
        out.append(f"    SPRITE_BLACK_{name},")
    out.append("};")
    out.append("")

    # Circle sprite
    out.append(format_array("SPRITE_MOVE_DOT", circle_sprite, PIECE_W))
    out.append("")

    with open(OUTPUT_FILE, "w") as f:
        f.write("\n".join(out) + "\n")

    print(f"Generated {OUTPUT_FILE}")


generate_header()
