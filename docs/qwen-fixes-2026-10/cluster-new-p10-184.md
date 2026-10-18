# Cluster new-p10-184: LB302 note enqueue spin

Verdict: **BLOCKED**

The LB302 enqueue fix is implemented and the regression passes with it. The required complexity and file-length gates still exit 1 on violations outside the files changed here, so the task cannot receive a clean gate sign-off. The baseline files and gate limits were left unchanged.

## File and line evidence

- `plugins/Lb302/Lb302.cpp:536` publishes the note pointer with a release store to that slot's readiness sequence. The producer no longer waits for earlier producers to commit.
- `plugins/Lb302/Lb302.cpp:572-577` scans only the contiguous ready prefix before processing notes, preserving queue order without spinning on an unpublished slot.
- `plugins/Lb302/Lb302.h:274` declares the per-slot readiness sequence array.
- `tests/src/plugins/PluginPortsMigrationTest.cpp:2162` adds a regression that represents producer 0 paused after claiming sequence 0, then verifies producer 1 returns before that gap is released.

## Verification evidence

| Check | Exact command | Exit | Result |
|---|---|---:|---|
| RED, fix reversed | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | 8 | `test-5.log`: the regression failed at `returnedBeforeGapRelease`; the unrelated LADSPA fixture check passed. |
| GREEN, fix restored | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | 0 | `test-6.log`: 1/1 CTest entries passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 1 | `gate-5.log`: six over-target functions in other files; none in the changed files. |
| File-length gate | `bash tests/file-length-gate.sh --check` | 1 | `gate-6.log`: 19 over-500-line sources in other files; none in the changed files. |

The initial RED runs (`test-1.log` and `test-2.log`) also showed the expected LB302 assertion, but the existing `nonRealtimeLadspaEffectsAreInvalid` check failed because its `decimator_1202` fixture target had not been built. The first post-fix CTest run (`test-3.log`) passed the LB302 regression but still exited 8 on that fixture check. Building the existing target separately made the fixture check pass in `test-4.log`; no test assertion or gate was changed.

## Build and proof commands

Each command below was run with output captured in the numbered log named in parentheses.

- `cmake --build build --target PluginPortsMigrationTest` — exit 0 (`build-1.log`).
- `cmake --build build --target zene PluginPortsMigrationTest` — exit 0 (`build-2.log`).
- `cmake --build build --target zene PluginPortsMigrationTest` — exit 143 after its queued wrapper was terminated (`build-3.log`).
- `/usr/bin/cmake --build build --parallel 2 --target zene PluginPortsMigrationTest` — exit 0 (`build-4.log`, `build-6.log`, `build-7.log`).
- `/usr/bin/cmake --build build --parallel 2 --target decimator_1202` — exit 0 (`build-5.log`).
- `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` — exit 8 (`test-1.log`, `test-2.log`, `test-3.log`, `test-5.log`) and exit 0 (`test-4.log`, `test-6.log`).
- `git diff -- plugins/Lb302/Lb302.cpp plugins/Lb302/Lb302.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-184.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-184.r1/logs/fix.patch` — exit 0.
- `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-184.r1/logs/fix.patch` — exit 0.
- `bash tests/complexity-gate.sh --check` — exit 1 (`gate-5.log`).
- `bash tests/file-length-gate.sh --check` — exit 1 (`gate-6.log`).

## Files in this commit

- `plugins/Lb302/Lb302.cpp`
- `plugins/Lb302/Lb302.h`
- `tests/src/plugins/PluginPortsMigrationTest.cpp`
- `tests/upstream-modifications.txt`
- `tests/fork-sources.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-184.md`
