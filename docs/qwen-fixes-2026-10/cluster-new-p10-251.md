# Cluster new-p10-251: duplicate VCA group IDs

## Verdict: BLOCKED

The loader bug is fixed and the regression passes with the fix. The required ratchet gates still fail on unrelated files in this branch, so the gate prerequisite is blocked. Their baselines were not changed.

## Evidence

In [Mixer.cpp](../../src/core/Mixer.cpp#L2759), the loader first tries the serialized VCA ID. If it is already taken, it now creates the group using the lowest free ID and warns with both IDs. The remaining group fields and children are loaded through the normal path. [VcaGroupTest.cpp](../../tests/src/core/VcaGroupTest.cpp#L851) creates two groups, changes the second serialized ID to collide with the first, and checks that the second group's ID, fader, mute state, mixer member and edit-track member survive.

Before the fix, `VcaGroupTest::duplicateGroupIdsPreserveLaterGroup()` failed at `tests/src/core/VcaGroupTest.cpp:874` because only one group was loaded. The reverse-patch proof reproduced that failure after rebuilding `zene` and `VcaGroupTest`. Restoring the production patch made the same ctest pass.

An earlier initial build invocation (`build-1.log`) yielded without a captured exit status. I reran the test-target build as `build-2.log`; the table records that captured exit code and the RED result uses that build. No status is inferred for `build-1.log`.

The affected ctest family selected one test (`VcaGroupTest`, 1/1); the final run passed. Log files are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-251.r1/logs`.

| Phase | Exact command | Exit |
|---|---|---:|
| Initial test build | `cmake --build build --target VcaGroupTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-251.r1/logs/build-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0` | 0 |
| RED, original loader (from `build/tests`) | `ctest -R '^VcaGroupTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-251.r1/logs/test-red-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0` | 8 |
| Reverse-patch proof | `git diff -- src/core/Mixer.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-251.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-251.r1/logs/fix.patch` | 0 |
| Rebuild for reverse-patch proof | `cmake --build build --target zene VcaGroupTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-251.r1/logs/build-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0` | 0 |
| RED, reverse-patched loader (from `build/tests`) | `ctest -R '^VcaGroupTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-251.r1/logs/test-red-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0` | 8 |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-251.r1/logs/fix.patch` | 0 |
| Rebuild with fix | `cmake --build build --target zene VcaGroupTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-251.r1/logs/build-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0` | 0 |
| GREEN (from `build/tests`) | `ctest -R '^VcaGroupTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-251.r1/logs/test-green-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-251.r1/logs/gate-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0` | 1 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-251.r1/logs/gate-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0` | 1 |

The complexity gate reports six over-limit functions in other files, including `dotCentreAlong` in `tests/src/gui/XyPadPairTest.cpp` (CCN 16) and `parseNotes` in `src/core/DawProjectReadTracks.cpp` (CCN 13). The file-length gate reports fourteen other files over 500 lines, beginning with `src/core/SmfInterchangeReader.cpp` (504) and `src/core/TakeLane.cpp` (520). Neither report names a file changed for this fix. No gate limit or baseline was altered.

The session did not expose an `fff grep` tool and `fff` was not installed on PATH. I searched the logs with separate `rg -n` terms instead.

## Files included in the commit

- `src/core/Mixer.cpp`
- `tests/src/core/VcaGroupTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-251.md`
