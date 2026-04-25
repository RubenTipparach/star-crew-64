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


def write_png(path: str, pixels: list[tuple[int, int, int, int]], size: int = SIZE) -> None:
    """Write a square RGBA PNG. `pixels` is a flat row-major list of length size*size."""
    assert len(pixels) == size * size

    raw = bytearray()
    for y in range(size):
        raw.append(0)  # filter: None
        for x in range(size):
            r, g, b, a = pixels[y * size + x]
            raw.extend((r & 255, g & 255, b & 255, a & 255))

    def chunk(tag: bytes, data: bytes) -> bytes:
        return (
            struct.pack(">I", len(data))
            + tag
            + data
            + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
        )

    sig = b"\x89PNG\r\n\x1a\n"
    ihdr = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)  # 8-bit RGBA
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


def make_bridge_panel() -> list[tuple[int, int, int, int]]:
    """
    Bridge control panel atlas (32x32). The panel mesh samples three regions
    of this texture to dress its different faces:
      band 0 (y  0- 7): glowing main display — green grid + scanline
      band 1 (y  8-15): button rows — red/yellow/green caps with bezels
      band 2 (y 16-23): metal console body — dark brushed steel + seams
      band 3 (y 24-31): impulse throttle slot — dark groove + amber edge
    """
    bezel    = (28, 30, 38, 255)
    metal    = (62, 66, 78, 255)
    metal_hl = (110, 118, 132, 255)
    metal_dk = (38, 42, 50, 255)
    screen   = (12, 35, 22, 255)
    grid     = (40, 180, 110, 255)
    glow     = (160, 240, 200, 255)
    btn_red    = (220, 60, 50, 255)
    btn_red_hl = (255, 130, 110, 255)
    btn_yel    = (240, 200, 60, 255)
    btn_yel_hl = (255, 235, 130, 255)
    btn_grn    = (60, 200, 110, 255)
    btn_grn_hl = (140, 240, 170, 255)
    amber      = (230, 165, 50, 255)
    amber_dk   = (130, 90, 30, 255)

    px = [metal] * (SIZE * SIZE)
    for y in range(SIZE):
        for x in range(SIZE):
            if y < 8:
                # Main display: dark green base with grid lines + horizontal scanlines.
                c = screen
                if x % 4 == 0 or (y % 4 == 0):
                    c = grid
                if y == 3 and 4 <= x <= 27:
                    c = glow  # readout highlight line
                if x < 1 or x > 30 or y == 0 or y == 7:
                    c = bezel
            elif y < 16:
                # Button row: 4 large round-ish caps across, bezel between them.
                cell = x // 8
                inset_x = x % 8
                inset_y = (y - 8)
                # bezel between cells (x % 8 == 0 or 7) and at top/bottom of band
                if inset_x == 0 or inset_x == 7 or inset_y == 0 or inset_y == 7:
                    c = bezel
                else:
                    base, hl = [
                        (btn_red, btn_red_hl),
                        (btn_yel, btn_yel_hl),
                        (btn_grn, btn_grn_hl),
                        (btn_red, btn_red_hl),
                    ][cell % 4]
                    # round-ish: corners get bezel
                    if (inset_x in (1, 6)) and (inset_y in (1, 6)):
                        c = bezel
                    elif inset_x in (2, 3) and inset_y in (2, 3):
                        c = hl
                    else:
                        c = base
            elif y < 24:
                # Console body: brushed metal w/ horizontal seam at top + bottom.
                yb = y - 16
                if yb == 0 or yb == 7:
                    c = metal_dk
                elif (x * 3 + yb) % 11 == 0:
                    c = metal_hl
                else:
                    c = metal
                # vertical screw heads
                if x in (3, 28) and yb in (3, 4):
                    c = bezel
            else:
                # Throttle slot: dark recess with amber illuminated edges.
                yb = y - 24
                slot_top = 1
                slot_bot = 6
                if yb in (slot_top, slot_bot):
                    c = amber
                elif slot_top < yb < slot_bot:
                    if x in (1, 30):
                        c = amber_dk
                    elif (x % 4) == 0:
                        c = amber_dk
                    else:
                        c = bezel
                else:
                    c = metal_dk
            px[y * SIZE + x] = c
    return px


def make_ship() -> list[tuple[int, int, int, int]]:
    """
    Ship hull atlas (32x32). Two bands:
      band 0 (y  0-15): top-side hull — pale grey panels + cockpit accent
      band 1 (y 16-31): underside / engines — darker plating + amber thruster
    The procedural ship model UVs sample whichever band matches the face.
    """
    hull       = (180, 188, 200, 255)
    hull_hl    = (220, 226, 235, 255)
    hull_dk    = (110, 118, 132, 255)
    seam       = (60, 65, 75, 255)
    cockpit    = (40, 110, 200, 255)
    cockpit_hl = (130, 200, 255, 255)
    under      = (70, 75, 88, 255)
    under_dk   = (40, 44, 52, 255)
    engine     = (255, 170, 60, 255)
    engine_hl  = (255, 230, 160, 255)

    px = [hull] * (SIZE * SIZE)
    for y in range(SIZE):
        for x in range(SIZE):
            if y < 16:
                c = hull
                # plate seams
                if x in (0, 15, 16, 31) or y in (0, 15):
                    c = seam
                elif (x + y) % 9 == 0:
                    c = hull_hl
                elif (x * 5 + y * 3) % 13 == 0:
                    c = hull_dk
                # cockpit dome — a small bright patch in the upper-middle
                if 12 <= x <= 19 and 4 <= y <= 9:
                    c = cockpit
                    if 14 <= x <= 17 and 5 <= y <= 7:
                        c = cockpit_hl
            else:
                c = under
                yb = y - 16
                if x in (0, 15, 16, 31) or yb in (0, 15):
                    c = under_dk
                elif (x * 7 + yb * 11) % 17 == 0:
                    c = under_dk
                # twin engine glow patches at the back
                if (3 <= x <= 8 or 23 <= x <= 28) and 10 <= yb <= 13:
                    c = engine
                    if (4 <= x <= 7 or 24 <= x <= 27) and 11 <= yb <= 12:
                        c = engine_hl
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


STAR_SIZE = 8  # small billboards


def _make_star_sprite(core, halo) -> list[tuple[int, int, int, int]]:
    """
    8×8 star billboard: a single bright core at (3,3) with a soft 4-neighbour
    halo, everything else transparent-looking (black with alpha=0). Combining
    multiple scattered star quads in 3D gives the background its twinkle.
    """
    bg = (0, 0, 0, 0)  # zero alpha so the dark corners don't punch a hole
    px = [bg] * (STAR_SIZE * STAR_SIZE)
    cx, cy = 3, 3

    px[cy * STAR_SIZE + cx] = core
    # Plus-shaped halo; corners stay transparent so the sprite reads as round.
    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        x, y = cx + dx, cy + dy
        if 0 <= x < STAR_SIZE and 0 <= y < STAR_SIZE:
            px[y * STAR_SIZE + x] = halo
    return px


def make_star_white():
    return _make_star_sprite(core=(255, 255, 250, 255), halo=(160, 160, 180, 255))


def make_star_blue():
    return _make_star_sprite(core=(180, 210, 255, 255), halo=(70, 110, 200, 255))


def make_star_yellow():
    return _make_star_sprite(core=(255, 230, 140, 255), halo=(190, 150,  60, 255))


def make_star_red():
    return _make_star_sprite(core=(255, 140, 120, 255), halo=(170,  60,  50, 255))


def main() -> None:
    os.makedirs(OUT, exist_ok=True)
    # 32×32 level/character textures.
    big_outputs = {
        "hallway.png": make_hallway(),
        "room.png": make_room(),
        "airlock.png": make_airlock(),
        "hallway_wall.png": make_hallway_wall(),
        "room_wall.png": make_room_wall(),
        "character.png": make_character(),
        "bridge_panel.png": make_bridge_panel(),
        "ship.png": make_ship(),
    }
    for name, pixels in big_outputs.items():
        path = os.path.join(OUT, name)
        write_png(path, pixels, SIZE)
        print(f"  wrote {path}")

    # 8×8 billboard star sprites.
    small_outputs = {
        "star_white.png":  make_star_white(),
        "star_blue.png":   make_star_blue(),
        "star_yellow.png": make_star_yellow(),
        "star_red.png":    make_star_red(),
    }
    for name, pixels in small_outputs.items():
        path = os.path.join(OUT, name)
        write_png(path, pixels, STAR_SIZE)
        print(f"  wrote {path}")

    # Clean up old single-sheet star textures that have been replaced by the
    # individual 8×8 billboard sprites above.
    for stale_name in ("stars.png", "starfield.png"):
        stale = os.path.join(OUT, stale_name)
        if os.path.exists(stale):
            os.remove(stale)
            print(f"  removed {stale}")


if __name__ == "__main__":
    main()
