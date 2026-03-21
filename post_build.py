"""
Post-build script for `pio run -e m5stack-cores3`. Produces release artifacts:

  firmware/cardputer-chess-<ver>-m5-burner.bin  — merged (M5Burner, flash at 0x0)
  firmware/cardputer-chess-<ver>-app.bin        — app only (launcher/manual, flash at 0x10000)
  bin/m5stack-cores3/firmware.bin               — raw firmware for GitHub release
"""
Import("env")

import os
import subprocess
import shutil


def merge_bin(source, target, env):
    build_dir = env.subst("$BUILD_DIR")
    fw_name = env.GetProjectOption("custom_firmware_name", "firmware")
    fw_version = env.GetProjectOption("custom_firmware_version", "0.0.0")

    bootloader = os.path.join(build_dir, "bootloader.bin")
    partitions = os.path.join(build_dir, "partitions.bin")
    firmware = os.path.join(build_dir, "firmware.bin")

    out_dir = os.path.join(env.subst("$PROJECT_DIR"), "firmware")
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, f"{fw_name}-{fw_version}-m5-burner.bin")

    print(f"Merging into M5Burner binary: {out_path}")

    # Find esptool from PlatformIO's tool package
    esptool_pkg = env.PioPlatform().get_package_dir("tool-esptoolpy")
    esptool_py = os.path.join(esptool_pkg, "esptool.py")

    subprocess.check_call([
        env.subst("$PYTHONEXE"), esptool_py,
        "--chip", "esp32s3",
        "merge_bin",
        "--flash_mode", "dio",
        "--flash_size", "16MB",
        "-o", out_path,
        "0x0000", bootloader,
        "0x8000", partitions,
        "0x10000", firmware,
    ])

    size_kb = os.path.getsize(out_path) / 1024
    print(f"M5Burner binary ready: {out_path} ({size_kb:.0f} KB)")

    # Copy app-only binary for launcher/manual flashing (offset 0x10000)
    app_path = os.path.join(out_dir, f"{fw_name}-{fw_version}-app.bin")
    shutil.copy2(firmware, app_path)
    app_kb = os.path.getsize(app_path) / 1024
    print(f"App-only binary ready: {app_path} ({app_kb:.0f} KB)")

    # Copy raw firmware to bin/ for release artifact
    bin_dir = os.path.join(env.subst("$PROJECT_DIR"), "bin", "m5stack-cores3")
    os.makedirs(bin_dir, exist_ok=True)
    bin_path = os.path.join(bin_dir, "firmware.bin")
    shutil.copy2(firmware, bin_path)
    print(f"Raw firmware staged: {bin_path} ({app_kb:.0f} KB)")


env.AddPostAction("$BUILD_DIR/firmware.bin", merge_bin)
