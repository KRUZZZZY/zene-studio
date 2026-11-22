FIXED

# Cluster new-p12-59: control-room state accepted finite gains that overflow audio samples

Logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-59.r3/logs/` (every command below cites its file there and its EXIT code).

## Verdict: REAL, fixed

`setState()` replaced non-finite dB values but let finite ones through. A trim of `10000.f`
reached `publish()` (`src/core/ControlRoom.cpp:59`), where `linear(state.trimsDb[...])` did
`std::pow(10.f, 10000.f / 20.f)` (`src/core/ControlRoom.cpp:46`) - the exact infinity an
IEEE float cannot hold - and `apply()` then multiplied output samples by an infinite gain
(`src/core/ControlRoom.cpp:103-104`). `monitor.control_room` documents and clamps
-24..12 dB for trims and -60..0 dB for `dim_db`, but the clamp lived only in the command
handler (`src/core/ControlCommandsMixerMeters.cpp:189-190`), not in the state path itself.

Fix (commit `ee721d2c04`): `setState()` now clamps every trim to `[-24.f, 12.f]` and `dimDb`
to `[-60.f, 0.f]` before publishing (`src/core/ControlRoom.cpp:74-77`) - the same ranges
`monitor.control_room` and the preferences loader (`storedTrim`, `src/core/ControlRoom.cpp:50-55`;
`loadPreferences`, `src/core/ControlRoom.cpp:85-91`) already enforce. In-range values pass
through unchanged, so nothing behaves differently except the overflow case.

## Where the regression lives

`tests/src/core/ControlRoomTest.cpp::theControlRoomClampsExtremeFiniteGains` (new file, this
branch). The regression was first added inside `MixerToolsTest.cpp`, but that file sat at
exactly 500 code lines - the file-length gate's limit - and the added block tipped it to 531,
so the test was moved into its own target (`tests/CMakeLists.txt`, registered in
`tests/fork-sources.txt` and `tests/all-sources.txt`). `MixerToolsTest.cpp` is byte-identical
to its pre-fix state again.

## Exact commands and EXIT codes

Baseline with the fix (commit `ee721d2c04`) already in the tree:
- `cmake --build build --target MixerToolsTest > logs/build-1.log 2>&1` - EXIT=0
- `cd build/tests && ctest -R '^MixerToolsTest$' --output-on-failure > logs/test-1.log 2>&1` - EXIT=0 (1/1 passed)

RED proof, original placement (fix reverted via `git apply -R logs/fix.patch`, where
`fix.patch` = `git diff ee721d2c04^ ee721d2c04 -- src/core/ControlRoom.cpp`):
- `cmake --build build --target MixerToolsTest > logs/build-2.log 2>&1` - EXIT=0
- `cd build/tests && ctest -R '^MixerToolsTest$' --output-on-failure > logs/test-2.log 2>&1` - EXIT=8
  (`FAIL! : MixerToolsTest::theControlRoomClampsExtremeFiniteGains() 'settled.trimsDb[0] <= 12.f' returned FALSE.`)
- `git apply logs/fix.patch` then rebuild - EXIT=0
- `cd build/tests && ctest -R '^MixerToolsTest$' --output-on-failure > logs/test-3.log 2>&1` - EXIT=0

After the round-2 move into `ControlRoomTest.cpp` (current, authoritative):
- `cmake --build build --target MixerToolsTest > logs/build-6.log 2>&1` - EXIT=0 (rebuilt after manifest/CMake edits)
- `cmake --build build --target ControlRoomTest > logs/build-7.log 2>&1` - EXIT=0
- `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^(ControlRoomTest|MixerToolsTest)$' --output-on-failure > logs/test-4.log 2>&1` - EXIT=0 (2/2 passed)
- RED: `git apply -R logs/fix.patch && cmake --build build --target ControlRoomTest > logs/build-8.log 2>&1` - EXIT=0;
  `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^ControlRoomTest$' --output-on-failure > logs/test-5.log 2>&1` - EXIT=8
  (`'settled.trimsDb[0] <= 12.f' returned FALSE`)
- GREEN: `git apply logs/fix.patch && cmake --build build --target ControlRoomTest > logs/build-9.log 2>&1` - EXIT=0;
  `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^ControlRoomTest$' --output-on-failure > logs/test-6.log 2>&1` - EXIT=0

Gates:
- `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-59 > logs/gate-1.log 2>&1` - EXIT=1
  (`FAIL file-length - NEW: new file over 500 lines: tests/src/core/MixerToolsTest.cpp (531)`; every other row PASS)
- after the move: same command `> logs/gate-3.log 2>&1` - EXIT=0 (all 8 rows PASS)
- `(intermediate) ... > logs/gate-2.log 2>&1` - EXIT=1 (fork-sources / all-sources-reproduce flagged the not-yet-tracked new test file; fixed by `git add`ing it, then `bash tests/all-sources-reproduce.sh` prints `REPRODUCES`, and gate-3 is EXIT=0)

## Completeness - every path into the published gain

- `controlroom::setState` callers: `ControlCommandsMixerMeters.cpp:193` (the `monitor.control_room`
  handler - already clamped both ranges at `:189-190`; still consistent, now doubly enforced at
  the cause) and the tests. No other production caller exists.
- `controlroom::loadPreferences` (`src/core/ControlRoom.cpp:85-91`) reads the saved preferences:
  trims via `storedTrim` (`:50-55`, clamps `[-24, 12]`, non-finite/unparseable -> 0), speaker
  clamped to `[0, 2]`. The new `setState` clamp covers the dim path for direct state injection,
  which is the reported bug.
- Sibling `publish` (`:57-62`) is the only place `g_gain` is computed; with trims in
  `[-24, 12]` and dim in `[-60, 0]`, `linear()` yields at most `10^(12/20) * 10^0 ≈ 3.98`
  and at least `10^((-24-60)/20) ≈ 1e-4`(dim adds at most 0 dB). No infinity is reachable.
- `apply` (`:95-106`) and the audio thread are untouched: no allocation, no locks, no logging;
  it only multiplies by the precomputed `g_gain` (relaxed atomic), as before.
- `setExporting` (`:93`) and the export short-circuit (`:99`) are untouched.

## No collateral damage

- Saved data / projects: the monitor section is **not** saved in projects; only the `controlroom`
  preferences (`trims`, `speaker`) are, via `setState`'s `ConfigManager` writes (`:79-82`) - and
  those preferences are written *after* the clamp, so edited preferences can only ever contain
  in-range values now. Loading an old preferences file behaves exactly as before (loader
  already clamped). Projects open bit-identically; nothing in the save format changed.
- Legitimate in-range callers: the clamp is the identity on `[-24, 12]` / `[-60, 0]`, so
  `monitor.control_room` with valid arguments behaves byte-for-byte as before.
- No new control commands; the ControlCommandsSnapshot canary is untouched.

## Self-check

? The regression test FAILS with your product change reverted and PASSES with it (exit codes).
  Yes - exact commands in "Exact commands and EXIT codes": `logs/test-5.log` EXIT=8 with the
  fix reverted (`'settled.trimsDb[0] <= 12.f' returned FALSE`), `logs/test-6.log` EXIT=0 with
  it re-applied. The pre-move variant is in `logs/test-2.log` (EXIT=8) and `logs/test-3.log`
  (EXIT=0).

? The test is deterministic: no wall-clock timing, sleeps or races (name the seam or injected values it uses).
  It uses no clock, no sleep, no thread. `Engine::init(true)` boots the engine in-process;
  `controlroom::setState` runs synchronously on the calling thread and only then does the test
  read `controlroom::state()` and a local `std::array<SampleFrame, 4>` through
  `controlroom::apply`. The "seam" is direct injection of a hand-built `controlroom::State`
  (`bad.trimsDb[0] = 10000.f`, `bad.dimDb = 10000.f`, then `bad.dimDb = -10000.f`) - fixed
  constants, no randomness, no timing.

? The fix is at the cause and complete: every caller and sibling of what you changed was checked and stays consistent.
  Yes - see "Completeness" above: the one production caller (`monitor.control_room`),
  the preferences loader sibling (`storedTrim`), and the single gain computation site
  (`publish`) were all checked; all enforce the same ranges, so the paths agree.

? No collateral damage: old projects and saved data still load as before, and any limit or check you added does not hit other callers that legitimately exceed it.
  Yes - see "No collateral damage" above: monitor state is not in the project format; the
  clamp is the identity on the documented ranges; every caller that legitimately exceeds the
  ranges did not exist (the only out-of-range sources were the bug itself and untrusted
  config, both of which are exactly what the clamp is for).

? If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended contract that shows it.
  Not applicable - verdict is FIXED. The pre-fix code at `src/core/ControlRoom.cpp:68-83`
  (before commit `ee721d2c04`) clamped only `speaker` and non-finite dB values; finite
  `10000.f` reached `linear()` and produced an infinite gain, which is the reported bug.

## Files committed

Commit `ee721d2c04` (fix + first regression placement):
- `src/core/ControlRoom.cpp` - the fix
- `tests/src/core/MixerToolsTest.cpp` - regression (moved out again in the follow-up)

This commit (round-2 QA follow-up):
- `tests/src/core/ControlRoomTest.cpp` - new target holding the regression
- `tests/src/core/MixerToolsTest.cpp` - back to its base content (test moved)
- `tests/CMakeLists.txt` - registers `ControlRoomTest`
- `tests/fork-sources.txt`, `tests/all-sources.txt` - new file registered in both scope manifests
- `docs/qwen-fixes-2026-10/cluster-new-p12-59.md` - this report
