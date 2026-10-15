# Cluster new-p10-56-port

**Verdict: FIXED** (port of accepted `bugs/qwen-new-p10-56` commit `5d3dd9a24` onto the newest line, `050/m0-train-5`, written by hand; not merged or cherry-picked).

Finding: `reference.copy_devices` records an undo step but returns no `__transaction`, so clients cannot see that the copy is reversible or how to undo it.

## Evidence

- Before the fix, the copy records a transaction with no target context: `theReferenceCopiesDevices` fails at `tests/src/core/ArrangingSpacerReferenceTest.cpp:266` (`before.target` is `""`, expected `"ch-1"`).
- After the fix, `src/core/ControlCommandsReference.cpp:230-232` inserts `__transaction` via `transactionPayload` with `before.target` = the target id, inverse `control.undo`, `reversible` true, and a mechanism naming the recorded undo step and the captured `<fxchain>`. The result is returned at `:233`.
- The regression lives in `tests/src/core/ArrangingSpacerReferenceTest.cpp:264-272`. The test file was split out of `ArrangingCatchUpTest.cpp` on the newest line (`tests/CMakeLists.txt:800-803`), so the port targets the new home. `ArrangingCatchUpTest.cpp` is unchanged.
- The reversibility table (`src/core/ControlReversibilityTableW3Library.cpp:135`) already declares `copy_devices` as `TrueInverse`, so the runtime metadata now matches it.
- No new files; no registration changes needed (`tests/CMakeLists.txt` already lists `ArrangingSpacerReferenceTest.cpp`). No new control commands.

## Commands and exit codes

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-56-port.r1/logs`.

ccache could not write to `/run/user/1000/ccache-tmp` (sandbox permission), so builds after `build-2` set `CCACHE_TEMPDIR=/tmp/ccache-tmp-qwen`. This only changes the temp dir for ccache.

| Step | Command (run from the worktree / `build/tests` as noted) | Log | Exit |
|---|---|---|---:|
| Plugins, first try | `cmake --build build --target amplifier tripleoscillator -j2` | build-1.log | 2 (ccache permission, not a code error) |
| Test target, first try | `cmake --build build --target ArrangingSpacerReferenceTest -j2` | build-2.log | 2 (same) |
| Plugins | `CCACHE_TEMPDIR=/tmp/ccache-tmp-qwen cmake --build build --target amplifier tripleoscillator -j2` | build-3.log | 0 |
| Test target (new assertions, old code) | `CCACHE_TEMPDIR=/tmp/ccache-tmp-qwen cmake --build build --target ArrangingSpacerReferenceTest -j2` | build-4.log | 0 |
| **RED** (old production code) | `cd build/tests && ctest -R ArrangingSpacerReferenceTest --output-on-failure` | test-1.log | **8** (`theReferenceCopiesDevices` FAIL, line 266) |
| Fix applied; rebuild zene | `CCACHE_TEMPDIR=/tmp/ccache-tmp-qwen cmake --build build --target zene -j2` | build-5.log | 0 |
| Rebuild test | `CCACHE_TEMPDIR=/tmp/ccache-tmp-qwen cmake --build build --target ArrangingSpacerReferenceTest -j2` | build-6.log | 0 |
| **GREEN** (fix) | `cd build/tests && ctest -R ArrangingSpacerReferenceTest --output-on-failure` | test-2.log | **0** |
| Proof: save product diff | `git diff -- src/core/ControlCommandsReference.cpp > .../logs/fix.patch` | - | 0 |
| Proof: revert fix | `git apply -R .../logs/fix.patch` | - | 0 |
| Rebuild zene (reverted) | `cmake --build build --target zene -j2` | build-7.log | 0 |
| Rebuild test (reverted) | `cmake --build build --target ArrangingSpacerReferenceTest -j2` | build-8.log | 0 |
| **Proof RED** (reverted) | `cd build/tests && ctest -R ArrangingSpacerReferenceTest --output-on-failure` | test-3.log | **8** (same line 266 failure) |
| Proof: re-apply fix | `git apply .../logs/fix.patch` | - | 0 |
| Rebuild zene (re-applied) | `cmake --build build --target zene -j2` | build-9.log | 0 |
| Rebuild test (re-applied) | `cmake --build build --target ArrangingSpacerReferenceTest -j2` | build-10.log | 0 |
| **Proof GREEN** (re-applied) | `cd build/tests && ctest -R ArrangingSpacerReferenceTest --output-on-failure` | test-4.log | **0** |
| Family binary, full | `cd build/tests && ./ArrangingSpacerReferenceTest` | test-5.log | 0 (13 passed, 0 failed) |
| Neighbour: transcript tests (python, against rebuilt zene) | `ctest -R "ControlReversibilityTranscript\|ControlUndoStructuralTranscript\|ReferenceProjectPanelTest"` | test-6.log | 8 (see below) |
| Neighbour: build of a nonexistent target name (typo, not a code error) | `cmake --build build --target ControlReversibilityTranscript ControlUndoStructuralTranscript ReferenceProjectPanelTest -j2` | build-11.log | 2 (no such target) |
| Neighbour: panel test build | `cmake --build build --target ReferenceProjectPanelTest -j2` | build-12.log | 0 |
| Neighbour: panel test | `cd build/tests && ctest -R "^ReferenceProjectPanelTest$" --output-on-failure` | test-7.log | 8 (7 passed, 1 failed; see below) |
| Gate | `bash tests/complexity-gate.sh --check` | gate-1.log | **1** (6 REGRESSION entries, none in the changed files) |
| Gate | `bash tests/file-length-gate.sh --check` | gate-2.log | **1** (14 REGRESSION entries, none in the changed files) |
| Gate on HEAD (pristine `git archive HEAD` copy in /tmp) | `bash tests/complexity-gate.sh --check` / `bash tests/file-length-gate.sh --check` | gate-3-baseline-head.log / gate-4-baseline-head.log | 1 / 1 (same 6 and 14 REGRESSION entries) |

## Test results in the affected family

- `ArrangingSpacerReferenceTest`: ctest 1/1 entry; the binary reports 13 passed, 0 failed, 0 skipped (test-5.log). The device-copy test (`theReferenceCopiesDevices`) passes.
- `ControlUndoStructuralTranscript` and `ControlReversibilityTranscript` (python, against the rebuilt `zene`): both Passed (test-6.log).
- `ReferenceProjectPanelTest`: 7 passed, 1 failed (test-7.log). The device-copy panel test `copyDevicesPutsTheSelectedDevicesOnTheChosenChain` passes. The failure is `theButtonsOpenAndCloseTheReference`, a QFATAL timeout after 300 s on the file dialog path (Open... / answerFileDialog). That function does not call `copy_devices` (grep of `tests/src/gui/ReferenceProjectPanelTest.cpp`). I did not run that function against a HEAD build, so I have not proven it is pre-existing, only that this change does not reach that code.

## Gates: not clean, but not caused by this change

Both gates exit 1 on this branch tip. The gate on a pristine `git archive HEAD` copy gives the same entries and counts (6 complexity, 14 file-length). None of the listed entries are in `src/core/ControlCommandsReference.cpp` or `tests/src/core/ArrangingSpacerReferenceTest.cpp`, and `copyDevices` is not over the CCN limit. I did not touch baselines or limits. The gates need an owner decision on the branch-wide regressions, which are outside this one-bug port.

## Committed files

- `src/core/ControlCommandsReference.cpp`
- `tests/src/core/ArrangingSpacerReferenceTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-56-port.md`
