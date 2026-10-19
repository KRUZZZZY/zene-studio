# Cluster new-p12-157-port: zero-length MonoDelay

Verdict: **FIXED**

`MonoDelay::tick()` now returns immediately when its configured length is zero, so the input sample
passes through unchanged instead of emitting the sample left in the ring slot it is about to
overwrite. This is the accepted fix from `bugs/qwen-new-p12-157` (commit `696999cde`), re-applied by
hand against the newest line `ded355440` (050/m0-train-5) - no merge, cherry-pick or rebase. The
previous attempt could not be merged because the newer line had already grown its own
`MonoDelay` rows in `tests/src/plugins/PluginPortsMigrationTest.cpp`; this port therefore adds only
the new test slot and its body, reusing the implementation include the newer line already has.

## File:line evidence

- `plugins/Flanger/MonoDelay.cpp:59` - the zero-length pass-through (`if (m_length == 0.0f) { return; }`).
  Before the fix, `tick()` advanced `m_writeIndex` and read `m_buffer[m_writeIndex]` at
  `plugins/Flanger/MonoDelay.cpp:59-64`, i.e. the slot it writes on the same call, which at length
  zero still holds data from the previous ring cycle (`m_maxLength` samples old).
- `tests/src/plugins/PluginPortsMigrationTest.cpp:713` - the new test slot declaration.
- `tests/src/plugins/PluginPortsMigrationTest.cpp:1915` - the new test body: a 4-slot ring is ticked
  once around, then a zero-delay tick with input `17` must return `17`, not the stale slot value.
- `tests/src/plugins/PluginPortsMigrationTest.cpp:141` / `:143` - the MonoDelay `.h` / `.cpp`
  includes the newer line already carries; the regression compiles the production implementation.
- `tests/upstream-modifications.txt:687` and `:2298` - the inherited-file rows for the production and
  the test change.
- `tests/reference/Flanger/MonoDelay.cpp` is untouched (frozen upstream reference), so `MonoDelayTest`
  still exercises the reference implementation and passes.

The audio path adds no allocation, no lock, no logging and no Qt call; `tick()` is a per-sample
realtime function and the guard is a single float compare on an already-hot cache line.

## Evidence table

| Check | Command (and directory) | Result |
|---|---|---|
| RED on unmodified product code | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` (from `build/tests`, after `cmake --build build --target PluginPortsMigrationTest`) | EXIT=8; `zeroLengthDelayPassesInputThrough()` failed, Actual `1`, Expected `17.0f`; 1 ctest executed, 1 failed |
| GREEN after the fix | `ctest -R '^(PluginPortsMigrationTest\|MonoDelayTest)$' --output-on-failure` (from `build/tests`) | EXIT=8 overall; `zeroLengthDelayPassesInputThrough()` **PASS**; `MonoDelayTest` **Passed**; the only failure is the pre-existing `nonRealtimeLadspaEffectsAreInvalid()` (see below) |
| Reverse-patch proof (RED) | `git diff -- plugins/Flanger/MonoDelay.cpp > logs/fix.patch && git apply -R logs/fix.patch`, then `cmake --build build --target zene PluginPortsMigrationTest`, then the same ctest | EXIT=0 (apply), EXIT=0 (build), EXIT=8 (ctest); the case failed again with Actual `1`, Expected `17.0f` |
| Restore proof (GREEN) | `git apply logs/fix.patch`, rebuild `zene PluginPortsMigrationTest`, same ctest | EXIT=0 (apply), EXIT=0 (build), the case **PASS** |
| Affected-family test count | `ctest -R '^(PluginPortsMigrationTest\|MonoDelayTest)$' --output-on-failure` | 2 ctest tests executed (0 is a failure): `MonoDelayTest` #10, `PluginPortsMigrationTest` #477. `ctest -N -R 'MonoDelay\|Flanger'` shows only these two are registered in this configuration; the `FlangerEffect` reference test is not registered here. |
| Complexity gate | `bash tests/complexity-gate.sh --check` | EXIT=1 |
| Complexity gate, HEAD without this diff | `git apply -R logs/alldiff.patch` + `bash tests/complexity-gate.sh --check` | EXIT=1, byte-identical REGRESSION list (6 entries, none of them my files) |
| File-length gate | `bash tests/file-length-gate.sh --check` | EXIT=1 |
| File-length gate, HEAD without this diff | `git apply -R logs/alldiff.patch` + `bash tests/file-length-gate.sh --check` | EXIT=1, byte-identical REGRESSION list (14 entries, none of them my files) |
| Fork-sources registration | `bash tests/fork-sources-gate.sh` | EXIT=0, `PASS: every tracked source in scope is registered (1790 fork-NEW, 1212 inherited, 48 tooling)` |

### Honest notes on the two gates and one test failure

- Both ratchet gates **fail on this branch tip independently of this fix**. I measured that by
  reverting my whole diff, re-running each gate, and diffing the REGRESSION lists: they are
  byte-identical with and without my change, and name only files I did not touch
  (`src/core/SmfInterchangeReader.cpp`, `src/core/TakeLane.cpp`, `tests/src/core/*`,
  `tests/src/gui/ArrangerTimelineTest.cpp`, `XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`,
  `ControlCommandsMixerTools.cpp`, `ControlCommandsMixerStrip.cpp`, `ProjectRevisions.cpp`,
  `ControlCommandsShortcuts.cpp`). They are inherited from the newest line
  (`ded355440` / the 050-m0-train-5 merges). Per the rules I did **not** re-anchor or weaken any
  baseline and did **not** refactor those unrelated files, which would be outside this bug fix.
  `plugins/Flanger/MonoDelay.cpp` is in neither baseline and is not reported, i.e. `MonoDelay::tick`
  is within the CCN target after the change.
- `PluginPortsMigrationTest::nonRealtimeLadspaEffectsAreInvalid()` fails with
  `SWH Decimator test fixture was not loaded`. `/usr/lib/ladspa` on this machine contains only
  `amp.so delay.so filter.so noise.so sine.so`, so the fixture the case needs is not installed. The
  same failure is present in the very first RED log (`test-1.log`), i.e. before the product change,
  and it is independent of `MonoDelay`. Every other case in the binary, including the new one,
  passes.

## Commands and captured exit codes

All build and test output is redirected to new files in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-157-port.r1/logs`.

| Command | Log | Exit |
|---|---|---:|
| `cmake --build build --target PluginPortsMigrationTest` (test added, product still unfixed) | `build-1.log` | 0 |
| `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` (from `build/tests`) - RED | `test-1.log` | 8 |
| `cmake --build build --target zene PluginPortsMigrationTest` (with the fix) | `build-2.log` | 0 |
| `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` (from `build/tests`) | `test-2.log` | 8 |
| `git diff -- plugins/Flanger/MonoDelay.cpp > .../logs/fix.patch` | - | 0 |
| `git apply -R .../logs/fix.patch` | - | 0 |
| `cmake --build build --target zene PluginPortsMigrationTest` (fix reversed) | `build-3.log` | 0 |
| `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` (from `build/tests`) - reverse-patch RED | `test-3.log` | 8 |
| `git apply .../logs/fix.patch` | - | 0 |
| `cmake --build build --target zene PluginPortsMigrationTest` (fix restored) | `build-4.log` | 0 |
| `ctest -R '^(PluginPortsMigrationTest\|MonoDelayTest)$' --output-on-failure` (from `build/tests`) - family run | `test-4.log` | 8 (1 of 2 registered; `MonoDelayTest` not yet built) |
| `cmake --build build --target MonoDelayTest` | `build-5.log` | 0 |
| `ctest -R '^(PluginPortsMigrationTest\|MonoDelayTest)$' --output-on-failure` (from `build/tests`) - final family run | `test-6.log` | 8 |
| `bash tests/complexity-gate.sh --check` | `complexity-gate-1.log` | 1 |
| `bash tests/complexity-gate.sh --check` with the whole diff reverted | `complexity-gate-baseline.log` | 1 |
| `bash tests/file-length-gate.sh --check` | `file-length-gate-1.log` | 1 |
| `bash tests/file-length-gate.sh --check` with the whole diff reverted | `file-length-gate-baseline.log` | 1 |
| `bash tests/fork-sources-gate.sh` | `fork-sources-gate-2.log` | 0 |

## Files committed

- `plugins/Flanger/MonoDelay.cpp` (inherited, product fix)
- `tests/src/plugins/PluginPortsMigrationTest.cpp` (inherited, new regression slot + body)
- `tests/upstream-modifications.txt` (registration rows for both inherited files)
- `docs/qwen-fixes-2026-10/cluster-new-p12-157-port.md` (this report)