"""
Post-build script: merge bootloader + partitions + firmware into a single
.bin suitable for M5Burner's "User Custom" upload.

Runs automatically after `pio run -e m5stack-cores3`.
"""
Import("env")

import os
import sys
import subprocess


def merge_bin(source, target, env):
    build_dir = env.subst("$BUILD_DIR")
    fw_name = env.GetProjectOption("custom_firmware_name", "firmware")
    fw_version = env.GetProjectOption("custom_firmware_version", "0.0.0")

    bootloader = os.path.join(build_dir, "bootloader.bin")
    partitions = os.path.join(build_dir, "partitions.bin")
    firmware = os.path.join(build_dir, "firmware.bin")

    out_dir = os.path.join(env.subst("$PROJECT_DIR"), "firmware")
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, f"{fw_name}-{fw_version}.bin")

    print(f"Merging into M5Burner binary: {out_path}")

    # PlatformIO's SCons runs under its own virtualenv where esptool is installed.
    # Use the current interpreter (not sys.executable, which may point to Homebrew).
    python = env.subst("$PYTHONEXE") or sys.executable

    subprocess.check_call(
        [
            python, "-m", "esptool",
            "--chip", "esp32s3",
            "merge-bin",
            "--flash-mode", "dio",
            "--flash-size", "16MB",
            "-o", out_path,
            "0x0000", bootloader,
            "0x8000", partitions,
            "0x10000", firmware,
        ]
    )

    size_kb = os.path.getsize(out_path) / 1024
    print(f"M5Burner binary ready: {out_path} ({size_kb:.0f} KB)")


env.AddPostAction("$BUILD_DIR/firmware.bin", merge_bin)
