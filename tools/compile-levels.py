#!/usr/bin/env python3
"""
Compile a level-editor JSON export into the .lvl binary consumed by the game.

Usage:
    python3 tools/compile-levels.py <input.json> <output.lvl>

Format (big-endian, matches src/level_format.h):
    Header (16 bytes):
        u32 magic      'STLV'
        u16 version    1
        u16 flags      0
        u16 grid_w
        u16 grid_h
        u16 num_entities
        u16 reserved
    Tile grid: grid_w * grid_h bytes, row-major (y=0 row first).
               Padded with NULs to 4-byte alignment.
    Entities:  num_entities * 20 bytes
        i16  x             (cell coord)
        i16  z             (cell coord)
        char group[16]     (NUL-padded)
"""

import json
import struct
import sys
from pathlib import Path

MAGIC          = b"STLV"
VERSION        = 1
TILE_EMPTY     = 0
TILE_HALLWAY   = 1
TILE_ROOM      = 2
TILE_AIRLOCK   = 3
GROUP_LEN      = 16


def compile_level(src_path: Path, dst_path: Path) -> None:
    data = json.loads(src_path.read_text(encoding="utf-8"))

    grid = data.get("grid") or {}
    grid_w = int(grid.get("w", 0))
    grid_h = int(grid.get("h", 0))
    if grid_w <= 0 or grid_h <= 0:
        raise ValueError(f"{src_path}: grid.w/grid.h must be positive, got {grid_w}x{grid_h}")

    tiles = bytearray(grid_w * grid_h)  # initialised to TILE_EMPTY

    # Rooms first, so hallway cells that happen to overlap a room (shouldn't
    # happen from the editor, but be defensive) don't win.
    for r in data.get("rooms", []):
        rx, ry, rw, rh = int(r["x"]), int(r["y"]), int(r["w"]), int(r["h"])
        for y in range(ry, ry + rh):
            for x in range(rx, rx + rw):
                if 0 <= x < grid_w and 0 <= y < grid_h:
                    tiles[y * grid_w + x] = TILE_ROOM

    for cell in data.get("hallways", []):
        xs, ys = cell.split(",")
        x, y = int(xs), int(ys)
        if 0 <= x < grid_w and 0 <= y < grid_h:
            if tiles[y * grid_w + x] == TILE_EMPTY:
                tiles[y * grid_w + x] = TILE_HALLWAY

    # Entities — group "airlock" is treated as an airlock tile marker as well,
    # so the existing TileType.AIRLOCK still has a source. Any cell that has
    # an airlock entity on it flips the tile to AIRLOCK *if it was ROOM*; this
    # lets level designers tag specific room cells as airlocks without a new
    # tool.
    entities = data.get("entities", [])
    for e in entities:
        if e.get("group") == "airlock":
            ex, ey = int(e["x"]), int(e["y"])
            if 0 <= ex < grid_w and 0 <= ey < grid_h:
                idx = ey * grid_w + ex
                if tiles[idx] == TILE_ROOM:
                    tiles[idx] = TILE_AIRLOCK

    # Header
    header = struct.pack(
        ">4sHHHHHH",
        MAGIC, VERSION, 0, grid_w, grid_h, len(entities), 0,
    )

    # Tile block, padded to 4-byte alignment
    pad = (-len(tiles)) % 4
    tile_block = bytes(tiles) + b"\x00" * pad

    # Entity records
    ent_block = bytearray()
    for e in entities:
        group = str(e.get("group", "")).encode("ascii", errors="replace")[:GROUP_LEN]
        group = group.ljust(GROUP_LEN, b"\x00")
        ent_block += struct.pack(">hh", int(e["x"]), int(e["y"])) + group

    dst_path.parent.mkdir(parents=True, exist_ok=True)
    dst_path.write_bytes(header + tile_block + bytes(ent_block))

    print(f"    [LEVEL]  {dst_path}  "
          f"({grid_w}x{grid_h}, {len(entities)} entities, "
          f"{dst_path.stat().st_size} bytes)")


def main(argv):
    if len(argv) != 3:
        print("usage: compile-levels.py <input.json> <output.lvl>", file=sys.stderr)
        return 2
    compile_level(Path(argv[1]), Path(argv[2]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
