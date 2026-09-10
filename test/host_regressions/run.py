#!/usr/bin/env python3
"""Build the real engine/UI with narrow hardware mocks, then run ASan/UBSan."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TEST = Path(__file__).resolve().parent
COMMON = ["chess960", "chess_board", "chess_rules", "chess_zobrist", "chess_opening_book"]
SCENE = COMMON + ["chess_ai", "chess_clock", "chess_scene", "chess_storage", "chess_storage_codec",
                  "cursor_navigation", "game_records", "lobby_scene", "profile_storage",
                  "profile_storage_codec", "puzzle_data"]
UI = ["cardgfx", "cardgfx_canvas", "cardgfx_scene", "cardgfx_widget", "cardgfx_input", "cardgfx_layout"]
compiler = shlex.split(os.environ.get("CXX", "clang++"))
flags = ["-std=c++17", "-O1", "-g", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
         "-fno-omit-frame-pointer", "-include", "cstdio", "-include", "cstring",
         "-DCARDGFX_USE_PSRAM=0", '-DFIRMWARE_VERSION="test"',
         "-I", str(TEST / "stubs"), "-I", "src", "-I", "lib/cardgfx/src"]
with tempfile.TemporaryDirectory(prefix="cardputer-regressions-") as directory:
    for name, sources in [
        ("engine", [TEST / "engine.cpp"] + [ROOT / "src" / f"{s}.cpp" for s in COMMON]),
        ("scenes", [TEST / "scenes.cpp", TEST / "mocks.cpp"] +
         [ROOT / "src" / f"{s}.cpp" for s in SCENE] +
         [ROOT / "lib/cardgfx/src" / f"{s}.cpp" for s in UI]),
    ]:
        output = Path(directory) / name
        subprocess.run(compiler + flags + list(map(str, sources)) + ["-o", str(output)], cwd=ROOT, check=True)
        subprocess.run([str(output)], cwd=ROOT, check=True)
