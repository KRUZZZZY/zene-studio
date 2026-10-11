# Cluster new-p9-190

**Verdict: FIXED**

`InstrumentTrack::sendNoteOff` at `src/tracks/InstrumentTrackNotes.cpp:266` decremented the per-key running count even when it was already zero. The guard now accepts a note-off only when the count is positive, and sends the instrument note-off only when that decrement reaches zero.

Regression: `tests/src/core/MidiInputTransformTest.cpp:323` sends an unmatched note-off followed by two same-key note-ons. At line 345 it checks that releasing one of those notes leaves one per-voice modulation voice active. The original implementation failed with actual `0`, expected `1`; the guarded implementation passes. The test and product source are already registered in `tests/fork-sources.txt` at lines 1959 and 1793 respectively.

## Evidence

All builds used the configured `build` tree and target `MidiInputTransformTest`. All ctest runs were from `build/tests`; each run executed the one registered test in this affected family.

| Check | Build result | Test result | Tests |
|---|---:|---:|---:|
| Original behavior (RED) | 0 | 8 | 0/1 passed; regression failed as expected |
| Fix applied (GREEN) | 0 | 0 | 1/1 passed |
| Product hunk removed (RED) | 0 | 8 | 0/1 passed; same regression failed |
| Product hunk restored (GREEN) | 0 | 0 | 1/1 passed |
| Complexity gate | — | 0 | Passed |
| File-length gate | — | 0 | Passed |

Build command template, run from the worktree root (the captured `build_rc` values were all `0`):

```sh
cmake --build build --target MidiInputTransformTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-190.r1/logs/build-1.log 2>&1; build_rc=$?; echo EXIT=$build_rc
cmake --build build --target MidiInputTransformTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-190.r1/logs/build-2.log 2>&1; build_rc=$?; echo EXIT=$build_rc
cmake --build build --target MidiInputTransformTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-190.r1/logs/build-3.log 2>&1; build_rc=$?; echo EXIT=$build_rc
cmake --build build --target MidiInputTransformTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-190.r1/logs/build-4.log 2>&1; build_rc=$?; echo EXIT=$build_rc
```

The exact ctest command, run from `build/tests` (captured `test_rc` values: `8`, `0`, `8`, `0`):

```sh
ctest -R '^MidiInputTransformTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-190.r1/logs/test-1.log 2>&1; test_rc=$?; echo EXIT=$test_rc
ctest -R '^MidiInputTransformTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-190.r1/logs/test-2.log 2>&1; test_rc=$?; echo EXIT=$test_rc
ctest -R '^MidiInputTransformTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-190.r1/logs/test-3.log 2>&1; test_rc=$?; echo EXIT=$test_rc
ctest -R '^MidiInputTransformTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-190.r1/logs/test-4.log 2>&1; test_rc=$?; echo EXIT=$test_rc
```

The patch was saved and reversed, then reapplied; both commands captured exit `0`:

```sh
git diff -- src/tracks/InstrumentTrackNotes.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-190.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-190.r1/logs/fix.patch; reverse_rc=$?; echo EXIT=$reverse_rc
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-190.r1/logs/fix.patch; apply_rc=$?; echo EXIT=$apply_rc
```

Both required gates, run from the worktree root, captured exit `0`:

```sh
bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-190.r1/logs/gate-complexity-1.log 2>&1; gate_rc=$?; echo EXIT=$gate_rc
bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-190.r1/logs/gate-filelength-1.log 2>&1; gate_rc=$?; echo EXIT=$gate_rc
```

Log searches used `rg` because no `fff grep` tool was available in this session.

## Files committed

- `src/tracks/InstrumentTrackNotes.cpp`
- `tests/src/core/MidiInputTransformTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-190.md`
