# Cluster new-p10-266 — project note loading

**Verdict: FIXED**

`Note::loadSettings()` accepted note pitches and timing outside the editor's supported ranges, and
legacy `tone`/`oct` arithmetic could overflow `int`. The loader now parses these fields as checked
64-bit values, saturates legacy pitch arithmetic safely, clamps keys to `0..127`, and clamps note
position and length so the note stays within `MaxSongLength`.

## File evidence

- `src/core/Note.cpp:42-63`: checked integer parsing and overflow-safe legacy pitch calculation.
- `src/core/Note.cpp:435-445`: key and timing normalization in the load path.
- `tests/src/core/SlideNotesTest.cpp:163-215`: four independent load-path regressions for key 128,
  negative position, negative length, and legacy octave 2147483647.
- `tests/upstream-modifications.txt:696,1054`: inherited production and test files registered with
  reasons.

## Verification evidence

The affected CTest family is `SlideNotesTest`: **1 CTest test executed**. The four new QtTest cases
each fail on the original loader and pass with the fix.

| Stage | Exact command | Exit | Result |
|---|---|---:|---|
| Initial RED build | `cmake --build build --target zene SlideNotesTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-266.r1/logs/build-3-red.log 2>&1; echo EXIT=$?` | 0 | Both app and test target rebuilt. |
| Initial RED test | `QT_QPA_PLATFORM=offscreen ctest -R '^SlideNotesTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-266.r1/logs/test-2-red.log 2>&1; echo EXIT=$?` | 8 | Four independent regression cases failed before the fix. |
| Remove production patch | `git diff -- src/core/Note.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-266.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-266.r1/logs/fix.patch; echo EXIT=$?` | 0 | Production change reversed for the catchability check. |
| Rebuild without fix | `cmake --build build --target zene SlideNotesTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-266.r1/logs/build-5-unfixed-proof.log 2>&1; echo EXIT=$?` | 0 | Rebuilt before rerunning the test. |
| Catchability RED | `QT_QPA_PLATFORM=offscreen ctest -R '^SlideNotesTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-266.r1/logs/test-4-unfixed-proof.log 2>&1; echo EXIT=$?` | 8 | The same four cases failed without the production patch. |
| Reapply production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-266.r1/logs/fix.patch; echo EXIT=$?` | 0 | Restored the fix. |
| Rebuild with fix | `cmake --build build --target zene SlideNotesTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-266.r1/logs/build-6-restored-proof.log 2>&1; echo EXIT=$?` | 0 | Rebuilt both app and test target. |
| GREEN test | `QT_QPA_PLATFORM=offscreen ctest -R '^SlideNotesTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-266.r1/logs/test-5-restored-proof.log 2>&1; echo EXIT=$?` | 0 | 1/1 CTest test passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-266.r1/logs/gate-1-complexity.log 2>&1; echo EXIT=$?` | 0 | Passed. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-266.r1/logs/gate-2-file-length.log 2>&1; echo EXIT=$?` | 0 | Passed. |

The `fff` grep tool was unavailable in this environment, so the required log matches were checked
with `rg` against the individual log files.

## Files committed

- `src/core/Note.cpp`
- `tests/src/core/SlideNotesTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-266.md`
