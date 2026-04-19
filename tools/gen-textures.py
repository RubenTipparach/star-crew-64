#!/usr/bin/env python3
"""
Procedural texture generator for star-crew-64.

Produces 32x32 RGBA PNGs into assets/textures/:
  - hallway.png       : industrial corrugated floor (deck plating)
  - room.png          : crew quarters floor panel
  - airlock.png       : yellow/black hazard stripes
  - hallway_wall.png  : industrial corridor wall (vertical paneling, conduit)
  - room_wall.png     : crew quarters wall (warm composite paneling)
  - character.png     : simple humanoid color atlas (head/torso/limbs regions)

Wall textures are mapped to vertical quads (V axis = world height); the
trim strips near V=0 and V=SIZE-1 read as floor-level baseboard and
ceiling cornice respectively when applied to a TILE_SIZE x WALL_HEIGHT
quad.

No third-party deps — writes PNGs directly via zlib + struct.
N64 textures are tiny; 32x32 is the size cap we've chosen.
"""

import os
import struct
import zlib

SIZE = 32
OUT = os.path.join(os.path.dirname(__file__), "..", "assets", "textures")


def write_png(path: str, pixels: list[tuple[int, int, int, int]]) -> None:
    """Write a SIZE x SIZE RGBA PNG. pixels is a flat row-major list of RGBA."""
    assert len(pixels) == SIZE * SIZE

    raw = bytearray()
    for y in range(SIZE):
        raw.append(0)  # filter: None
        for x in range(SIZE):
            r, g, b, a = pixels[y * SIZE + x]
            raw.extend((r & 255, g & 255, b & 255, a & 255))

    def chunk(tag: bytes, data: bytes) -> bytes:
        return (
            struct.pack(">I", len(data))
            + tag
            + data
            + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
        )

    sig = b"\x89PNG\r\n\x1a\n"
    ihdr = struct.pack(">IIBBBBB", SIZE, SIZE, 8, 6, 0, 0, 0)  # 8-bit RGBA
    idat = zlib.compress(bytes(raw), 9)

    with open(path, "wb") as f:
        f.write(sig)
        f.write(chunk(b"IHDR", ihdr))
        f.write(chunk(b"IDAT", idat))
        f.write(chunk(b"IEND", b""))


def make_hallway() -> list[tuple[int, int, int, int]]:
    """Dark deck plating with a brighter center stripe and rivet dots."""
    base = (55, 60, 72, 255)
    stripe = (90, 100, 120, 255)
    rivet = (25, 28, 35, 255)
    hl = (120, 135, 160, 255)

    px = [base] * (SIZE * SIZE)
    for y in range(SIZE):
        for x in range(SIZE):
            # Corrugation: horizontal bands every 4 rows
            if y % 4 == 0:
                px[y * SIZE + x] = rivet
            elif y % 4 == 1:
                px[y * SIZE + x] = hl
            # Center travel stripe
            if 13 <= x <= 18:
                px[y * SIZE + x] = stripe if y % 4 != 0 else rivet
            # Rivets along edges
            if (x in (1, 30)) and (y % 6 == 3):
                px[y * SIZE + x] = rivet
    return px


def make_room() -> list[tuple[int, int, int, int]]:
    """Crew-quarters floor: 2 panels with seams, subtle noise."""
    base = (80, 78, 72, 255)
    seam = (45, 43, 40, 255)
    light = (110, 108, 100, 255)

    px = [base] * (SIZE * SIZE)
    for y in range(SIZE):
        for x in range(SIZE):
            # Panel seams at the midpoints
            if x == 0 or x == 15 or x == 16 or x == 31 or y == 0 or y == 15 or y == 16 or y == 31:
                px[y * SIZE + x] = seam
            # Deterministic "noise" highlights (checkerboard-ish)
            elif ((x * 7 + y * 13) % 23) < 2:
                px[y * SIZE + x] = light
    return px


def make_airlock() -> list[tuple[int, int, int, int]]:
    """Hazard stripes: diagonal yellow/black bands, warning border."""
    yellow = (230, 195, 40, 255)
    black = (25, 22, 18, 255)
    border = (200, 40, 40, 255)

    px = [black] * (SIZE * SIZE)
    for y in range(SIZE):
        for x in range(SIZE):
            # Diagonal bands, 4 px wide
            if ((x + y) // 4) % 2 == 0:
                px[y * SIZE + x] = yellow
            else:
                px[y * SIZE + x] = black
            # Red warning border
            if x < 2 or x > 29 or y < 2 or y > 29:
                px[y * SIZE + x] = border
    return px


def make_hallway_wall() -> list[tuple[int, int, int, int]]:
    """
    Industrial corridor wall: dark steel paneling with vertical recessed
    channels every 8 px, a horizontal conduit strip near the top, and a
    baseboard at the bottom. Reads correctly when V=0 is the floor.
    """
    base       = (62, 70, 84, 255)     # cool steel
    channel    = (38, 44, 54, 255)     # recessed panel seam
    highlight  = (110, 122, 140, 255)  # specular edge of the channel
    baseboard  = (28, 32, 40, 255)
    cornice    = (90, 100, 118, 255)
    conduit    = (180, 140, 60, 255)   # amber pipe accent
    rivet      = (24, 26, 32, 255)

    px = [base] * (SIZE * SIZE)
    for y in range(SIZE):
        for x in range(SIZE):
            # Vertical paneling: a 2 px channel every 8 px.
            xm = x % 8
            if xm == 0:
                c = channel
            elif xm == 1:
                c = highlight
            else:
                c = base
            # Horizontal cornice band near the top (y close to SIZE-1 = ceiling).
            if y >= SIZE - 3:
                c = cornice if y == SIZE - 2 else channel
            # Conduit pipe just below the cornice.
            elif SIZE - 7 <= y <= SIZE - 5:
                c = conduit if y == SIZE - 6 else (140, 110, 50, 255)
            # Baseboard at the floor (y = 0..2).
            elif y <= 2:
                c = baseboard
            # Occasional rivets along mid-height.
            elif y in (12, 20) and x % 8 == 4:
                c = rivet
            px[y * SIZE + x] = c
    return px


def make_room_wall() -> list[tuple[int, int, int, int]]:
    """
    Crew quarters wall: warm composite paneling with horizontal seams
    every 6 px, a wood-like base trim, and a soft picture rail near the
    top. Same V=0 = floor convention as hallway_wall.
    """
    base      = (140, 118, 92, 255)    # warm tan composite
    seam      = (90, 72, 55, 255)
    highlight = (180, 158, 130, 255)
    baseboard = (60, 48, 36, 255)
    rail      = (100, 80, 60, 255)
    accent    = (200, 175, 140, 255)

    px = [base] * (SIZE * SIZE)
    for y in range(SIZE):
        for x in range(SIZE):
            c = base
            # Horizontal panel seams every 6 px (1 px dark + 1 px highlight).
            ym = y % 6
            if ym == 0:
                c = seam
            elif ym == 1:
                c = highlight
            # Baseboard at the floor.
            if y <= 2:
                c = baseboard
            elif y == 3:
                c = seam
            # Picture-rail trim near the ceiling.
            elif y == SIZE - 4:
                c = rail
            elif y >= SIZE - 3:
                c = accent if y == SIZE - 2 else seam
            # Subtle vertical wood-grain ticks.
            elif (x * 11 + y * 5) % 29 == 0:
                c = highlight
            px[y * SIZE + x] = c
    return px


def make_character() -> list[tuple[int, int, int, int]]:
    """
    Character UV atlas (32x32). Laid out as 4 horizontal bands, each 32x8:
      row 0 (y  0- 7): head/skin
      row 1 (y  8-15): torso/shirt
      row 2 (y 16-23): arms/sleeves
      row 3 (y 24-31): legs/pants + boots strip
    Model UVs can be assigned to sample whichever band matches the body part.
    """
    skin = (215, 175, 140, 255)
    skin_dk = (160, 120, 90, 255)
    shirt = (60, 95, 160, 255)
    shirt_hl = (95, 135, 200, 255)
    pants = (50, 55, 65, 255)
    pants_hl = (80, 85, 95, 255)
    boots = (30, 30, 35, 255)

    px = [(0, 0, 0, 255)] * (SIZE * SIZE)
    for y in range(SIZE):
        for x in range(SIZE):
            if y < 8:
                c = skin_dk if (x + y) % 6 == 0 else skin
            elif y < 16:
                c = shirt_hl if (x * 3 + y) % 7 == 0 else shirt
            elif y < 24:
                c = shirt_hl if x % 8 < 2 else shirt  # sleeve with cuff accent
            else:
                if y >= 29:
                    c = boots
                else:
                    c = pants_hl if x % 5 == 0 else pants
            px[y * SIZE + x] = c
    return px


def main() -> None:
    os.makedirs(OUT, exist_ok=True)
    outputs = {
        "hallway.png": make_hallway(),
        "room.png": make_room(),
        "airlock.png": make_airlock(),
        "hallway_wall.png": make_hallway_wall(),
        "room_wall.png": make_room_wall(),
        "character.png": make_character(),
    }
    for name, pixels in outputs.items():
        path = os.path.join(OUT, name)
        write_png(path, pixels)
        print(f"  wrote {path}")


if __name__ == "__main__":
    main()
