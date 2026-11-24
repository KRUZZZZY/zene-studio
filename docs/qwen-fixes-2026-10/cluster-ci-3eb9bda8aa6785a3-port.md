FIXED

# Cluster ci-3eb9bda8aa6785a3-port — `build / mingw64` on the train-51 line

Logs: `/home/kruzzzzy/.config/kads/zene/state/runs/ci-3eb9bda8aa6785a3-port.r2/logs`.

## What the hosted mingw64 job hit, and the state of each cause on this branch

The cluster's original cause, a MinGW PPA key fetched from an unreachable keyserver
(`docs/qwen-fixes-2026-10/cluster-ci-3eb9bda8aa6785a3.md`), is already fixed here.
`.github/workflows/setup-mingw-packages.sh:24-29` installs the committed
`.github/workflows/keys/tobydox-mingw-w64.gpg` with `signed-by=`, and CTest `MingwPpaKeyGate` passes.

The newer hosted mingw64 logs (read as data) show two later stops, both in **Build**,
both of the same class: a file-local test helper whose only caller is compiled out on
MinGW becomes `-Werror=unused-function`.

| CI commit | mingw64 log | first error | on this branch |
|---|---|---|---|
| `31f559ab` | `run-43764792400-job-132967358836.log` | `tests/src/core/MidiOutTimingTest.cpp:83/107` `measure`/`measureAgainstRequested` defined but not used | ALREADY-FIXED: the helpers share the `LMMS_HAVE_ALSA` guard (`MidiOutTimingTest.cpp:55,69,259`); CTest `Train51Portability` passes |
| `fa4a5f2e` | `run-43786984969-job-133036727594.log` | `tests/src/core/RemotePluginAudioPortsTest.cpp:75` `countCriticals` defined but not used | ALREADY-FIXED by `79691f5414` (not an ancestor of `fa4a5f2e`, ancestor of HEAD: `git merge-base --is-ancestor` exit 1 / 0); guard at `RemotePluginAudioPortsTest.cpp:75-84` |

Both CI logs live under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/train-ci/<sha>/`.
`fa4a5f2e` is the newest run there, so no hosted run of HEAD exists.

## REAL: the next hidden stop of the same class (fixed here)

gmake stops at the first failing TU, so each hosted run reveals only one of these errors.
An audit of every test TU under a MinGW macro configuration (`_WIN32`, `SYNC_WITH_SHM_FIFO`;
no `Q_OS_UNIX`, no ALSA) found one more. The audit's detector reproduced the
`countCriticals` finding on `79691f5414^`.

- `tests/src/plugins/PluginPortsMigrationTest.cpp:3218` (before the fix: `:3210`):
  `renderGigNotesCountingHeap` sits in an anonymous namespace with no guard.
- Its only caller, `:3540` (now `:3549`) in `gigplayerAudioPathDoesNotAllocate()`, sits
  inside `#ifdef Q_OS_UNIX`. MinGW does not define that macro, so the helper is unused there.
- The target is built on MinGW: `tests/CMakeLists.txt:4792` `add_executable(PluginPortsMigrationTest ...)`
  is not inside any platform `if()`.
- The helper has been in the TU since `ffe21f4714` and was present at `fa4a5f2e`
  (`git show fa4a5f2e:<file>`, line 3087). The `countCriticals` stop hid it.

### Fix

- `PluginPortsMigrationTest.cpp:3209-3214`: `PLUGIN_PORTS_TEST_HAVE_POSIX_ALLOCATOR_HOOK` is
  defined when `Q_OS_UNIX` is defined and the test seam `PLUGIN_PORTS_TEST_NO_POSIX_ALLOCATOR_HOOK`
  is not. The caller branch (`:3529`) now tests that macro.
- On every real host the macro equals `Q_OS_UNIX`, so test behaviour does not change.
- `:3216` / `:3262`: the helper is wrapped in `#if defined(PART_C_MIGRATED_gigplayer) && defined(PLUGIN_PORTS_TEST_HAVE_POSIX_ALLOCATOR_HOOK)`.
  That is exactly the condition of its only caller.
- Its one dependency, `HeapWatch` (271-293), is a type that the `Q_OS_UNIX` block at 295-382
  also uses. Nothing new becomes unused in either configuration.
- Other `Q_OS_UNIX` uses (`:74`, `:295-382`) are untouched.
- No product code changes. There is nothing on the audio thread, no undo change and no control command.

### Regression

`tests/CMakeLists.txt` (after the PluginPortsMigrationTest block) adds the OBJECT library
`PluginPortsMigrationNoPosixHookCompileCheck`. It follows the existing
`RemotePluginAudioPortsShmCompileCheck` pattern:

- It compiles the real `PluginPortsMigrationTest.cpp` with the test target's include
  directories and definitions, plus `PLUGIN_PORTS_TEST_NO_POSIX_ALLOCATOR_HOOK`.
- So the Linux build (USE_WERROR=ON) takes the branch mingw64 takes.
- It builds with the tests and has no CTest entry. Its proof is the build exit code.

## Commands and EXIT codes (from the logs)

- `cmake --build build --target zene RemotePluginAudioPortsTest MidiOutTimingTest -j4 > build-1.log`: EXIT=0
- `cmake --build build --target RemotePluginAudioPortsShmCompileCheck -j4 > build-2.log`: EXIT=0
- From build/tests, `ctest -R 'RemotePluginAudioPorts|MidiOutTiming|Train51Portability|MingwPpaKeyGate' --output-on-failure > test-1.log`:
  EXIT=0, `100% tests passed out of 4`.
- Proof that the existing SHM check catches the countCriticals defect:
  - `git apply -R guard.patch` (the 79691f5414 guard), then build `RemotePluginAudioPortsShmCompileCheck` (`build-3-red.log`): RED_EXIT=2,
    `RemotePluginAudioPortsTest.cpp:77:6: error: ... countCriticals ... defined but not used [-Werror=unused-function]`.
  - `git apply guard.patch`, then rebuild (`build-4-green.log`): GREEN_EXIT=0.
- New check, with the seam present and the helper unguarded (`build-5-red.log`): EXIT=2.
  With the guard (`build-6-green.log`): EXIT=0.
- Step 4b, where `fix.patch` holds the two guard lines of `PluginPortsMigrationTest.cpp`:
  - `git apply -R fix.patch` (EXIT=0), then
    `cmake --build build --target PluginPortsMigrationNoPosixHookCompileCheck -j4 > build-8-4b-red.log`: **RED_EXIT=2**,
    `PluginPortsMigrationTest.cpp:3217:6: error: ... renderGigNotesCountingHeap(lmms::Instrument&, int, int) defined but not used [-Werror=unused-function]`.
  - `git apply fix.patch` (EXIT=0), then the same build (`build-9-4b-green.log`): **GREEN_EXIT=0**.
- `cmake --build build --target zene PluginPortsMigrationTest -j4 > build-7.log`: EXIT=0.
- `ctest -R '^GigPlayer' --output-on-failure > test-2.log`: EXIT=0, 1/1 passed.
- `./PluginPortsMigrationTest gigplayerAudioPathDoesNotAllocate > test-3.log`: EXIT=0,
  `PASS : PluginPortsMigrationTest::gigplayerAudioPathDoesNotAllocate()`. It ran and was not skipped.
- `python3 .../feature_check.py <worktree> > gates-2.log`: EXIT=0, all 8 gates PASS.

## Limits

- This box has no x86_64-w64-mingw32 toolchain, so no real MinGW build ran.
  The Linux compile check reproduces the MinGW preprocessor branch.
- The audit did not scan 14 TUs that are absent from the Linux `compile_commands.json`
  (wasm tests, `ControlWasmRenderTest`, `PhaseDPerfBench`, `TwoTrackAlsaCaptureProbe`,
  `MsFriendLookupProbe`, `tests/reference/*`, `tests/data/gigproof/make_gig.cpp`).
- The next hosted mingw64 run is the confirmation.

## Self-check

- Verdict evidence: the CI log lines and `file:line` references above. The red/green
  build exits come from logs written this session.
- The regression fails without the fix (RED_EXIT=2) and passes with it (GREEN_EXIT=0).
  It is deterministic: a compile, with no timing.
- Registration:
  - `tests/upstream-modifications.txt` has entries for `tests/CMakeLists.txt` and `PluginPortsMigrationTest.cpp`.
  - This report is listed in `tests/fork-sources.txt`.
- No gate, limit or assertion was changed.

Delegation: Haiku found the mingw64 job logs and their first errors, then built and ran the
four existing CTests. Sonnet (1) audited all test TUs for MinGW-only unused helpers and found
`renderGigNotesCountingHeap`. Sonnet (2) implemented the designed guard, seam, compile check
and registration, and ran the red/green builds and the GigPlayer tests. I reviewed the diff
and ran step 4b, the gates and the commit myself.

## Other findings

- `tests/src/core/AudioDeviceDiagnosticsTest.cpp:100,105`: `valueOf` and `available` are used
  only under `LMMS_HAVE_ALSA` / `LMMS_HAVE_JACK`. They are fine while mingw64 keeps weakjack
  (`CMakeLists.txt:694-700`); they would break a build with JACK off. Not changed here.
