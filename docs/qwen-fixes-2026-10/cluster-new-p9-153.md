# Cluster new-p9-153

Verdict: **FIXED**

`plugins/GigPlayer/GigPlayer.cpp:297` now rejects MIDI values below 0, while keeping the upper bound at 128. The old `<= 0` guard discarded valid MIDI note 0 before GIG plugin data or a GIG note could be created.

The regression is `PluginPortsMigrationTest::gigPlayerAcceptsMidiNoteZero` in `tests/src/plugins/PluginPortsMigrationTest.cpp:219`. It loads the built GigPlayer module, sends key 0 through `Instrument::playNote()`, and requires the note handle to receive plugin data.

## Evidence

The affected CTest family is `PluginPortsMigrationTest`. Each invocation executed **1/1 CTest target**. The original-code run produced 9 QtTest result lines: 8 passed and the new note-0 assertion failed. The remaining family cases passed. The green runs passed the whole family.

| Check | Exact command | Exit |
| --- | --- | ---: |
| RED on original code | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-153.r1/logs/test-1.log 2>&1; echo EXIT=$?` (from `build/tests`) | 8 |
| GREEN with fix | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-153.r1/logs/test-2.log 2>&1; echo EXIT=$?` (from `build/tests`) | 0 |
| Reverse product patch | `git diff -- plugins/GigPlayer/GigPlayer.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-153.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-153.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| RED after reverse patch | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-153.r1/logs/test-3.log 2>&1; echo EXIT=$?` (from `build/tests`) | 8 |
| Restore product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-153.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| GREEN after restore | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-153.r1/logs/test-4.log 2>&1; echo EXIT=$?` (from `build/tests`) | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-153.r1/logs/gate-1.log 2>&1; echo EXIT=$?` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-153.r1/logs/gate-2.log 2>&1; echo EXIT=$?` | 0 |

The RED assertion was `PluginPortsMigrationTest::gigPlayerAcceptsMidiNoteZero()`: `accepted` returned false with “GigPlayer did not register MIDI note 0”. After restoring the product patch, the same CTest command passed 1/1. The required gates passed and did not change their baselines.

Builds used before the test runs, all with exit 0:

| Log | Exact command |
| --- | --- |
| `build-1.log` | `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-153.r1/logs/build-1.log 2>&1; echo EXIT=$?` | 0 |
| `build-2.log` | `cmake --build build --target PluginPortsMigrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-153.r1/logs/build-2.log 2>&1; echo EXIT=$?` | 0 |
| `build-3.log` | `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-153.r1/logs/build-3.log 2>&1; echo EXIT=$?` | 0 |
| `build-4.log` | `cmake --build build --target PluginPortsMigrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-153.r1/logs/build-4.log 2>&1; echo EXIT=$?` | 0 |
| `build-5.log` | `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-153.r1/logs/build-5.log 2>&1; echo EXIT=$?` | 0 |
| `build-6.log` | `cmake --build build --target PluginPortsMigrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-153.r1/logs/build-6.log 2>&1; echo EXIT=$?` | 0 |
| `build-7.log` | `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-153.r1/logs/build-7.log 2>&1; echo EXIT=$?` | 0 |
| `build-8.log` | `cmake --build build --target PluginPortsMigrationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-153.r1/logs/build-8.log 2>&1; echo EXIT=$?` | 0 |

All build, test, and gate logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-153.r1/logs`. The `fff grep` tool was not exposed in this session, so I searched the logs with `rg` for `error`, `FAILED`, and `Assertion`.

## Files committed

- `plugins/GigPlayer/GigPlayer.cpp`
- `tests/src/plugins/PluginPortsMigrationTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-153.md`
