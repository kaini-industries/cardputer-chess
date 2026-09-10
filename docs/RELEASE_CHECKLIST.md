# Release Checklist

Use this checklist for every Cardputer Advance release. A release is approved
only when all evidence refers to the same immutable source commit and the exact
artifacts proposed for publication.

## 1. Prepare the Release Candidate

- [ ] Start from an up-to-date, clean `main` branch and record the candidate
      commit SHA.
- [ ] Choose `X.Y.Z` and confirm exact agreement between the intended `vX.Y.Z`
      tag, `custom_firmware_version` in `platformio.ini`, the embedded firmware
      version, README release status, staged directory, artifact filenames, and
      `release-manifest.json`.
- [ ] Confirm the intended ESP-NOW protocol version in code, the manifest, and
      release notes. Current source uses protocol v6.
- [ ] Run the native ASan/UBSan suite and a clean `cardputer-adv` production
      build. Save the CI run URL and complete logs.
- [ ] Confirm the app fits the `0x300000` app partition and every merged segment
      remains within the 8 MB flash layout.
- [ ] Validate the complete image contains the expected ESP32-S3 segments at
      `0x0000`, `0x8000`, `0xE000`, and `0x10000`.
- [ ] Confirm `firmware.bin` is byte-identical to the versioned app image, and
      verify every entry in `SHA256SUMS` against the staged files.

Create an RC such as `vX.Y.Z-rc.1`. Do not create the final tag until the exact
RC artifacts pass the hardware gates below.

## 2. Hardware Release Gates

Test on at least two physical M5Stack Cardputer Advance units. Record the
device revisions, installation path, candidate commit, artifact hashes, tester,
date, and pass/fail evidence.

- [ ] Device A: erase flash, install the complete image at `0x0000`, then verify
      cold boot, display, keyboard, side buttons, sound, and persistence after a
      power cycle.
- [ ] Device B: install the app-only image at `0x10000` over the previous stable
      release and confirm compatible profiles, results, saved games, puzzle
      progress, and settings survive.
- [ ] Install once through M5Burner and confirm the downloaded image hash matches
      the approved factory image.
- [ ] Smoke-test Local, AI, puzzle, timed, save/resume, review, and game-over
      flows, including conventional first-move clock behavior.
- [ ] With both devices on protocol v6, verify host discovery, displayed opponent
      name/MAC suffix, timed and untimed pairing, move acknowledgements, clock
      synchronization, draw offer/response, time gift, terminal result, and
      disconnect/reconnect behavior.
- [ ] Attempt a multiplayer connection to the previous incompatible protocol
      and confirm it is rejected clearly rather than starting a divergent game.

Any firmware change after these tests invalidates the hardware approval and
requires a new RC and a fresh pass of the affected gates.

## 3. Draft and Publish

- [ ] Create the final `vX.Y.Z` tag at the approved commit without moving or
      rebuilding from a different SHA.
- [ ] Create a draft GitHub release and attach only the exact versioned factory
      image, app image, `firmware.bin`, flash bundle, debug bundle, manifest, and
      `SHA256SUMS` produced by the approved build.
- [ ] Download every draft asset, rerun checksum and image-layout validation,
      and compare the results with the RC evidence.
- [ ] Add release notes covering notable changes, compatibility and protocol
      requirements, installation offsets, data-retention warnings, known issues,
      deferred work, test results, and the approved commit SHA.
- [ ] Have a second person review the draft, evidence, filenames, version, and
      install instructions; then publish without replacing any asset.
- [ ] Update the M5Burner catalog with the same approved factory-image hash and
      metadata. Perform a fresh catalog installation before announcing it.

## 4. Post-release and Rollback

- [ ] Verify the public downloads, checksums, release page, and M5Burner entry.
- [ ] Preserve the previous stable release and its assets. Multiplayer peers
      must upgrade or roll back together when protocol versions differ.
- [ ] Never move a published tag or silently replace an asset. If a serious
      defect appears, add a prominent release warning, withdraw the affected
      M5Burner entry, and publish a new patch release from a reviewed commit.

The release record should retain the tag and commit, CI run, tool versions,
manifest, checksums, hardware matrix, tester approvals, release URL, M5Burner
catalog version, and any accepted exceptions.
