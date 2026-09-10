# Host regressions for Cardputer ADV

Run `python3 test/host_regressions/run.py` from the repository. Requires Python 3
and Clang with AddressSanitizer and UndefinedBehaviorSanitizer. Set `CXX` to use
another compatible compiler. The runner uses a temporary build directory and
removes it afterward.

The tests compile the real engine, CardGFX input/routing/rendering, chess and lobby
scenes, clocks, storage codecs, and profile logic. Only the hardware boundaries
are mocked: M5 keyboard state, time/randomness, display output, NVS I/O, puzzle
progress persistence, and ESP-NOW transport. This does not verify physical
keyboard scanning, flash durability, radio delivery, or display output on a device.

Coverage:

- Sort all 218 legal moves in a promoted-piece position without corrupting memory,
  losing a move, or duplicating one.
- Score checkmate, stalemate, the 50-move rule, and insufficient material at the
  search horizon; bound checking extensions and handle clock rollover.
- Standard starting-position perft depth 4: 197,281 nodes.
- Repetition with uncapturable, capturable, and pinned en passant targets.
- Per-side mating material, including cooperative enemy blocking pieces and
  multiple bishops of one square color.
- Held-key repeat, equal-count key swaps, Fn modifiers, and side-button state.
- Completely offscreen and partially clipped lines, including extreme arguments.
- Puzzle animation/input ordering, opponent autoplay ownership, retry feedback,
  actual-position move notation, and requiring the player's final move.
- Both meanings of an ambiguous Chess960 king destination, cancellation, and
  stale board focus behind the choice dialog.
- A real v1 save upgraded by Resume, with complete moved-piece records and undo.
- Local timeout results and peer acceptance/rejection of timeout results.
- A generated 260-ply game that stays live without captures where possible,
  retains the latest 250 plies, and adjudicates subsequent repetition.

Also run `pio test -e native`. Its storage suite checks save truncation, CRC and
field validation, v1-v6 compatibility, v7 recent-history round trips, and migration
of a legacy overflow prefix. Firmware compilation is `pio run -e cardputer-adv`.
