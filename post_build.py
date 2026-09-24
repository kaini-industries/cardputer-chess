"""Build validated, reproducible release artifacts for Cardputer Advance.

The normal compatibility outputs remain in ``firmware/`` and
``bin/cardputer-adv/``.  A clean, version-scoped set of assets suitable for a
GitHub release is additionally written to ``release/<name>-<version>/``.
"""

Import("env")

import csv
import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import zipfile


FLASH_SIZE_BYTES = 8 * 1024 * 1024
FLASH_MODE = "dio"
PARTITION_TABLE_OFFSET = 0x8000
BOOT_APP0_OFFSET = 0xE000
APP_OFFSET = 0x10000
EXPECTED_NETWORK_PROTOCOL = 7
ZIP_TIMESTAMP = (1980, 1, 1, 0, 0, 0)


def _fail(message):
    raise RuntimeError("Release packaging failed: " + message)


def _require_file(path, label):
    if not os.path.isfile(path):
        _fail("required {} is missing: {}".format(label, path))
    if os.path.getsize(path) <= 0:
        _fail("required {} is empty: {}".format(label, path))


def _safe_component(value, label):
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", value or ""):
        _fail("{} contains unsafe filename characters: {!r}".format(label, value))
    return value


def _sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _files_identical(first, second):
    return os.path.getsize(first) == os.path.getsize(second) and _sha256(first) == _sha256(second)


def _copy_file(source, destination):
    os.makedirs(os.path.dirname(destination), exist_ok=True)
    temporary = destination + ".tmp"
    shutil.copyfile(source, temporary)
    os.replace(temporary, destination)


def _write_text(path, content):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    temporary = path + ".tmp"
    with open(temporary, "w", encoding="utf-8", newline="\n") as stream:
        stream.write(content)
    os.replace(temporary, path)


def _parse_size(value):
    text = str(value).strip().upper()
    match = re.fullmatch(r"(0X[0-9A-F]+|[0-9]+)\s*([KM]?B?)?", text)
    if not match:
        _fail("cannot parse size value {!r}".format(value))
    number = int(match.group(1), 0)
    suffix = match.group(2) or ""
    if suffix in ("K", "KB"):
        number *= 1024
    elif suffix in ("M", "MB"):
        number *= 1024 * 1024
    return number


def _load_partitions(path):
    partitions = []
    with open(path, "r", encoding="utf-8", newline="") as stream:
        for row_number, row in enumerate(csv.reader(stream), 1):
            if not row or not row[0].strip() or row[0].lstrip().startswith("#"):
                continue
            if len(row) < 5:
                _fail("partition row {} has fewer than five columns".format(row_number))
            name = row[0].strip()
            offset_text = row[3].strip()
            size_text = row[4].strip()
            if not offset_text:
                _fail("partition {!r} must have an explicit offset".format(name))
            partitions.append({
                "name": name,
                "type": row[1].strip(),
                "subtype": row[2].strip(),
                "offset": _parse_size(offset_text),
                "size": _parse_size(size_text),
            })
    if not partitions:
        _fail("partition table contains no partitions: {}".format(path))
    return partitions


def _partition_by_name(partitions, name):
    for partition in partitions:
        if partition["name"] == name:
            return partition
    _fail("partition table is missing required {!r} partition".format(name))


def _validate_partition_layout(partitions, flash_size):
    ordered = sorted(partitions, key=lambda item: item["offset"])
    previous_end = 0
    previous_name = None
    for partition in ordered:
        start = partition["offset"]
        end = start + partition["size"]
        if partition["size"] <= 0 or end > flash_size:
            _fail("partition {!r} is empty or exceeds the flash".format(partition["name"]))
        if previous_name is not None and start < previous_end:
            _fail("partitions {!r} and {!r} overlap".format(previous_name, partition["name"]))
        previous_end = end
        previous_name = partition["name"]

    ota_data = _partition_by_name(partitions, "otadata")
    if ota_data["offset"] != BOOT_APP0_OFFSET:
        _fail("otadata must start at 0x{:X}, found 0x{:X}".format(
            BOOT_APP0_OFFSET, ota_data["offset"]
        ))
    app = _partition_by_name(partitions, "app0")
    if app["offset"] != APP_OFFSET:
        _fail("app0 must start at 0x{:X}, found 0x{:X}".format(APP_OFFSET, app["offset"]))
    return ota_data, app


def _validate_images(images, flash_size, ota_data, app_partition):
    ordered = sorted(images, key=lambda item: item["offset"])
    for index, image in enumerate(ordered):
        size = os.path.getsize(image["path"])
        start = image["offset"]
        end = start + size
        if end > flash_size:
            _fail("{} image exceeds the {}-byte flash".format(image["role"], flash_size))
        if index + 1 < len(ordered) and end > ordered[index + 1]["offset"]:
            _fail("{} image overlaps {} image".format(
                image["role"], ordered[index + 1]["role"]
            ))

    boot_app0 = next(item for item in images if item["role"] == "boot_app0")
    boot_app0_size = os.path.getsize(boot_app0["path"])
    if boot_app0_size > ota_data["size"]:
        _fail("boot_app0 is larger than the otadata partition")

    application = next(item for item in images if item["role"] == "application")
    app_size = os.path.getsize(application["path"])
    if app_size > app_partition["size"]:
        _fail("application is {} bytes but app0 is only {} bytes".format(
            app_size, app_partition["size"]
        ))


def _validate_merged_image(merged_path, images, flash_size):
    merged_size = os.path.getsize(merged_path)
    if merged_size > flash_size:
        _fail("merged factory image exceeds the {}-byte flash".format(flash_size))

    with open(merged_path, "rb") as merged:
        for image in images:
            with open(image["path"], "rb") as source:
                expected = source.read()
            merged.seek(image["offset"])
            actual = merged.read(len(expected))
            if image["role"] == "bootloader":
                # esptool deliberately patches flash mode/size in bytes 2-3 of
                # the first image while merging.  Everything else must match.
                if len(actual) != len(expected) or actual[:2] != expected[:2] or actual[4:] != expected[4:]:
                    _fail("bootloader is not present at offset 0x0 in the merged image")
            elif actual != expected:
                _fail("{} is not present at offset 0x{:X} in the merged image".format(
                    image["role"], image["offset"]
                ))


def _zip_entry(archive, archive_name, data):
    info = zipfile.ZipInfo(archive_name, ZIP_TIMESTAMP)
    info.compress_type = zipfile.ZIP_DEFLATED
    info.create_system = 3
    info.external_attr = 0o100644 << 16
    archive.writestr(info, data, compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)


def _write_deterministic_zip(destination, entries):
    temporary = destination + ".tmp"
    with zipfile.ZipFile(temporary, "w") as archive:
        for archive_name, source in sorted(entries, key=lambda item: item[0]):
            if isinstance(source, bytes):
                data = source
            else:
                with open(source, "rb") as stream:
                    data = stream.read()
            _zip_entry(archive, archive_name, data)
    os.replace(temporary, destination)


def _git_commit(project_dir):
    try:
        return subprocess.check_output(
            ["git", "rev-parse", "HEAD"],
            cwd=project_dir,
            stderr=subprocess.DEVNULL,
            text=True,
        ).strip()
    except (OSError, subprocess.CalledProcessError):
        return None


def _package_version(package_dir):
    metadata_path = os.path.join(package_dir, "package.json") if package_dir else ""
    try:
        with open(metadata_path, "r", encoding="utf-8") as stream:
            metadata = json.load(stream)
        return metadata.get("version")
    except (OSError, ValueError):
        return None


def _platformio_version():
    try:
        import platformio
        return getattr(platformio, "__version__", None)
    except ImportError:
        return None


def _as_list(value):
    if value is None:
        return []
    if isinstance(value, (list, tuple)):
        return [str(item) for item in value]
    return [line.strip() for line in str(value).splitlines() if line.strip()]


def _network_protocol_version(project_dir):
    header_path = os.path.join(project_dir, "src", "chess_net_protocol.h")
    _require_file(header_path, "network protocol header")
    with open(header_path, "r", encoding="utf-8") as stream:
        contents = stream.read()
    match = re.search(r"NET_PROTOCOL_VERSION\s*=\s*([0-9]+)", contents)
    if not match:
        _fail("could not find NET_PROTOCOL_VERSION in {}".format(header_path))
    version = int(match.group(1))
    if version != EXPECTED_NETWORK_PROTOCOL:
        _fail("release requires network protocol v{}, found v{}".format(
            EXPECTED_NETWORK_PROTOCOL, version
        ))
    return version


def _flashing_instructions(name, version, app_filename):
    return """# Cardputer Chess {version} — Cardputer Advance flashing

All addresses below are hexadecimal. These images target an ESP32-S3 with
8 MB flash and the project's `partitions_8MB.csv` layout.

## Complete component flash

Erase the device, then write all four images:

```sh
esptool.py --chip esp32s3 erase_flash
esptool.py --chip esp32s3 --baud 921600 write_flash --flash_mode dio --flash_size 8MB \\
  0x0000 bootloader.bin \\
  0x8000 partitions.bin \\
  0xE000 boot_app0.bin \\
  0x10000 {app_filename}
```

The separately published `{name}-{version}-m5-burner.bin` is the equivalent
single-file factory image and must be written at `0x0000`.

The separately published `{name}-{version}-app.bin` is application-only. It
must be written at `0x10000` and should only be used on an already provisioned
device with the matching partition layout.
""".format(name=name, version=version, app_filename=app_filename)


def _artifact_record(path):
    return {
        "file": os.path.basename(path),
        "sizeBytes": os.path.getsize(path),
        "sha256": _sha256(path),
    }


def build_release_artifacts(source, target, env):
    del source, target

    project_dir = os.path.realpath(env.subst("$PROJECT_DIR"))
    build_dir = os.path.realpath(env.subst("$BUILD_DIR"))
    environment_name = env.subst("$PIOENV")
    firmware_name = _safe_component(
        env.GetProjectOption("custom_firmware_name", "firmware"),
        "custom_firmware_name",
    )
    firmware_version = _safe_component(
        env.GetProjectOption("custom_firmware_version", "0.0.0"),
        "custom_firmware_version",
    )
    tag_candidate = "v" + firmware_version
    requested_tag = os.environ.get("RELEASE_TAG")
    if requested_tag and requested_tag != tag_candidate:
        _fail("RELEASE_TAG {!r} does not match version {!r}".format(
            requested_tag, firmware_version
        ))

    platform_spec = str(env.GetProjectOption("platform", ""))
    board = str(env.GetProjectOption("board", ""))
    framework = _as_list(env.GetProjectOption("framework", []))
    libraries = _as_list(env.GetProjectOption("lib_deps", []))
    configured_flash_size = _parse_size(
        env.GetProjectOption("board_build.flash_size", "8MB")
    )
    if configured_flash_size != FLASH_SIZE_BYTES:
        _fail("Cardputer Advance release requires 8 MB flash, configured value is {} bytes".format(
            configured_flash_size
        ))

    partition_filename = str(env.GetProjectOption(
        "board_build.partitions", "partitions_8MB.csv"
    ))
    partition_csv = os.path.realpath(os.path.join(project_dir, partition_filename))
    if os.path.commonpath([project_dir, partition_csv]) != project_dir:
        _fail("partition table resolves outside the project directory")

    platform_object = env.PioPlatform()
    framework_package = platform_object.get_package_dir("framework-arduinoespressif32")
    esptool_package = platform_object.get_package_dir("tool-esptoolpy")
    boot_app0 = os.path.join(framework_package or "", "tools", "partitions", "boot_app0.bin")
    esptool_py = os.path.join(esptool_package or "", "esptool.py")

    bootloader = os.path.join(build_dir, "bootloader.bin")
    partition_binary = os.path.join(build_dir, "partitions.bin")
    application = os.path.join(build_dir, "firmware.bin")
    elf = os.path.join(build_dir, "firmware.elf")
    map_file = os.path.join(build_dir, "firmware.map")
    required = [
        (bootloader, "bootloader"),
        (partition_binary, "partition binary"),
        (boot_app0, "framework boot_app0"),
        (application, "application"),
        (elf, "debug ELF"),
        (map_file, "linker map"),
        (partition_csv, "partition CSV"),
        (esptool_py, "esptool"),
    ]
    for path, label in required:
        _require_file(path, label)

    partitions = _load_partitions(partition_csv)
    ota_data, app_partition = _validate_partition_layout(partitions, configured_flash_size)
    protocol_version = _network_protocol_version(project_dir)

    images = [
        {"role": "bootloader", "file": "bootloader.bin", "path": bootloader, "offset": 0x0000},
        {"role": "partitions", "file": "partitions.bin", "path": partition_binary, "offset": PARTITION_TABLE_OFFSET},
        {"role": "boot_app0", "file": "boot_app0.bin", "path": boot_app0, "offset": BOOT_APP0_OFFSET},
        {"role": "application", "file": "{}-{}-app.bin".format(firmware_name, firmware_version), "path": application, "offset": APP_OFFSET},
    ]
    _validate_images(images, configured_flash_size, ota_data, app_partition)

    compatibility_firmware_dir = os.path.join(project_dir, "firmware")
    compatibility_bin_dir = os.path.join(project_dir, "bin", "cardputer-adv")
    os.makedirs(compatibility_firmware_dir, exist_ok=True)
    os.makedirs(compatibility_bin_dir, exist_ok=True)
    merged_name = "{}-{}-m5-burner.bin".format(firmware_name, firmware_version)
    app_name = "{}-{}-app.bin".format(firmware_name, firmware_version)
    merged_path = os.path.join(compatibility_firmware_dir, merged_name)
    app_path = os.path.join(compatibility_firmware_dir, app_name)
    compatibility_alias = os.path.join(compatibility_bin_dir, "firmware.bin")

    print("Merging complete Cardputer Advance image: {}".format(merged_path))
    temporary_merged = merged_path + ".tmp"
    subprocess.check_call([
        env.subst("$PYTHONEXE"),
        esptool_py,
        "--chip", "esp32s3",
        "merge_bin",
        "--flash_mode", FLASH_MODE,
        "--flash_size", "8MB",
        "-o", temporary_merged,
        "0x0000", bootloader,
        "0x8000", partition_binary,
        "0xE000", boot_app0,
        "0x10000", application,
    ])
    os.replace(temporary_merged, merged_path)
    _validate_merged_image(merged_path, images, configured_flash_size)

    _copy_file(application, app_path)
    _copy_file(application, compatibility_alias)
    if not _files_identical(application, app_path) or not _files_identical(application, compatibility_alias):
        _fail("application compatibility aliases are not byte-identical")

    staging_dir = os.path.realpath(os.path.join(
        project_dir, "release", "{}-{}".format(firmware_name, firmware_version)
    ))
    release_root = os.path.realpath(os.path.join(project_dir, "release"))
    if os.path.commonpath([release_root, staging_dir]) != release_root or staging_dir == release_root:
        _fail("unsafe release staging directory")
    os.makedirs(staging_dir, exist_ok=True)

    staged_merged = os.path.join(staging_dir, merged_name)
    staged_app = os.path.join(staging_dir, app_name)
    staged_alias = os.path.join(staging_dir, "firmware.bin")
    _copy_file(merged_path, staged_merged)
    _copy_file(application, staged_app)
    _copy_file(application, staged_alias)
    if not _files_identical(staged_app, staged_alias):
        _fail("staged firmware.bin is not byte-identical to the versioned app image")

    instructions = _flashing_instructions(firmware_name, firmware_version, app_name)
    flash_bundle_name = "{}-{}-cardputer-advance-flash-bundle.zip".format(
        firmware_name, firmware_version
    )
    flash_bundle = os.path.join(staging_dir, flash_bundle_name)
    _write_deterministic_zip(flash_bundle, [
        ("FLASHING.md", instructions.encode("utf-8")),
        (app_name, application),
        ("boot_app0.bin", boot_app0),
        ("bootloader.bin", bootloader),
        (os.path.basename(partition_csv), partition_csv),
        ("partitions.bin", partition_binary),
    ])

    debug_bundle_name = "{}-{}-debug.zip".format(firmware_name, firmware_version)
    debug_bundle = os.path.join(staging_dir, debug_bundle_name)
    _write_deterministic_zip(debug_bundle, [
        ("{}-{}.elf".format(firmware_name, firmware_version), elf),
        ("{}-{}.map".format(firmware_name, firmware_version), map_file),
    ])

    manifest_images = []
    for image in images:
        manifest_images.append({
            "role": image["role"],
            "file": image["file"],
            "offset": image["offset"],
            "offsetHex": "0x{:X}".format(image["offset"]),
            "sizeBytes": os.path.getsize(image["path"]),
            "sha256": _sha256(image["path"]),
        })

    publishable_artifacts = [
        staged_merged,
        staged_app,
        staged_alias,
        flash_bundle,
        debug_bundle,
    ]
    manifest = {
        "schemaVersion": 1,
        "firmware": {
            "name": firmware_name,
            "version": firmware_version,
            "tagCandidate": tag_candidate,
            "gitCommit": _git_commit(project_dir),
            "networkProtocolVersion": protocol_version,
        },
        "target": {
            "environment": environment_name,
            "device": "M5Stack Cardputer Advance",
            "board": board,
            "chip": "esp32s3",
        },
        "build": {
            "platform": platform_spec,
            "framework": framework,
            "platformio": _platformio_version(),
            "python": platform.python_version(),
            "esptool": {
                "package": "tool-esptoolpy",
                "version": _package_version(esptool_package),
            },
            "arduinoFramework": {
                "package": "framework-arduinoespressif32",
                "version": _package_version(framework_package),
            },
            "libraries": libraries,
        },
        "flash": {
            "sizeBytes": configured_flash_size,
            "mode": FLASH_MODE,
            "partitionTable": {
                "file": os.path.basename(partition_csv),
                "offset": PARTITION_TABLE_OFFSET,
                "offsetHex": "0x{:X}".format(PARTITION_TABLE_OFFSET),
                "sha256": _sha256(partition_csv),
            },
            "appPartition": {
                "name": app_partition["name"],
                "offset": app_partition["offset"],
                "offsetHex": "0x{:X}".format(app_partition["offset"]),
                "sizeBytes": app_partition["size"],
            },
            "images": manifest_images,
        },
        "releaseArtifacts": [_artifact_record(path) for path in publishable_artifacts],
    }
    manifest_path = os.path.join(staging_dir, "release-manifest.json")
    _write_text(manifest_path, json.dumps(manifest, indent=2, sort_keys=True) + "\n")

    checksum_paths = publishable_artifacts + [manifest_path]
    checksum_lines = [
        "{}  {}".format(_sha256(path), os.path.basename(path))
        for path in sorted(checksum_paths, key=os.path.basename)
    ]
    checksums_path = os.path.join(staging_dir, "SHA256SUMS")
    _write_text(checksums_path, "\n".join(checksum_lines) + "\n")

    print("Release staging ready: {}".format(staging_dir))
    for path in publishable_artifacts + [manifest_path, checksums_path]:
        print("  {} ({} bytes)".format(os.path.basename(path), os.path.getsize(path)))


env.AddPostAction("$BUILD_DIR/firmware.bin", build_release_artifacts)
