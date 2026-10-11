# Cluster new-p9-474: TempoMapPublisher exception safety

Verdict: **FIXED**

## Finding

`include/TempoMap.h:300-315` contained the bug. `TempoMapPublisher::edit()` made the version odd before invoking the callback, but did not restore it when the callback threw. `snapshot()` then retried forever.

Edits now have rollback-on-exception semantics: `edit()` saves the prior map, restores it on any callback exception, returns the version to even, and rethrows. The regression at `tests/src/core/TempoMapTest.cpp:461` mutates the map and throws; it checks that `snapshot()` returns within one second and sees the original empty map (`:486`, `:495-501`).

## Evidence

The original implementation failed the regression with `snapshot() did not return after the edit callback threw`. The same test failed again after removing the production patch, and passed after the patch was restored.

| Check | Command result | Exit |
| --- | --- | ---: |
| Original-code RED (`test-1-red.log`) | `ctest -R '^TempoMapTest$' --output-on-failure` | 8 |
| Original-code failure evidence | `TempoMapTest::thePublisherRecoversAfterAThrowingEdit()` timed out waiting for `snapshot()` | 1/1 failed |
| Fixed family GREEN (`test-2-green.log`) | `ctest -R '^(TempoMapTest|TempoMapPersistenceTest|ControlTempoMapCommandsTest)$' --output-on-failure` | 0 |
| Revert-proof RED (`test-3-reverted.log`) | `ctest -R '^TempoMapTest$' --output-on-failure` | 8 |
| Restored family GREEN (`test-4-restored-green.log`) | `ctest -R '^(TempoMapTest|TempoMapPersistenceTest|ControlTempoMapCommandsTest)$' --output-on-failure` | 0 |
| Affected-family count | `ControlTempoMapCommandsTest`, `TempoMapPersistenceTest`, `TempoMapTest` | 3 passed |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check` | 0 |

## Commands and recorded exits

All build and test output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-474.r1/logs`.

The first test-source build exposed an invalid capture in the new regression; it was corrected before the RED run:

```sh
cmake --build build --target TempoMapTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-474.r1/logs/build-1.log 2>&1; echo EXIT=$?
# EXIT=2
cmake --build build --target TempoMapTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-474.r1/logs/build-2.log 2>&1; echo EXIT=$?
# EXIT=0
ctest -R '^TempoMapTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-474.r1/logs/test-1-red.log 2>&1; echo EXIT=$?
# EXIT=8
```

The first broad fixed rebuild was logged as `build-3-fixed.log`, but its shell status was not captured by the tool session. The same targets were rebuilt with explicit status capture:

```sh
cmake --build build --target zene TempoMapTest TempoMapPersistenceTest ControlTempoMapCommandsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-474.r1/logs/build-4-fixed.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0
# EXIT=0
ctest -R '^(TempoMapTest|TempoMapPersistenceTest|ControlTempoMapCommandsTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-474.r1/logs/test-2-green.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0
# EXIT=0; 3 tests passed
```

The required revert proof removed only `include/TempoMap.h`, rebuilt the application and regression target, and observed the same failure. The patch was then reapplied and all affected targets rebuilt:

```sh
git diff -- include/TempoMap.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-474.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-474.r1/logs/fix.patch; result=$?; printf 'EXIT=%s\n' "$result"; exit 0
# EXIT=0
cmake --build build --target zene TempoMapTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-474.r1/logs/build-5-reverted.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0
# EXIT=0
ctest -R '^TempoMapTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-474.r1/logs/test-3-reverted.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0
# EXIT=8
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-474.r1/logs/fix.patch; result=$?; printf 'EXIT=%s\n' "$result"; exit 0
# EXIT=0
cmake --build build --target zene TempoMapTest TempoMapPersistenceTest ControlTempoMapCommandsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-474.r1/logs/build-6-restored.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0
# EXIT=0
ctest -R '^(TempoMapTest|TempoMapPersistenceTest|ControlTempoMapCommandsTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-474.r1/logs/test-4-restored-green.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0
# EXIT=0; 3 tests passed
```

Final gates:

```sh
bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-474.r1/logs/gate-1-complexity.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0
# EXIT=0
bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-474.r1/logs/gate-2-file-length.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit 0
# EXIT=0
```

## Files committed

- `include/TempoMap.h`
- `tests/src/core/TempoMapTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-474.md`
