Verdict: FIXED
new-p21-187: FIXED
new-p21-188: FIXED

## new-p21-187 — resize capacity

**Decision: REAL.** In `plugins/SfzPlayer/sfizz/src/sfizz/HistoricalBuffer.h:35-43`,
`resize()` changed `buffer` but left the `size` member stale. `push()` uses that member
for both its write and wrap at lines 50-57. Growing from 2 to 4 returned 2.5 instead of
3.0 after three pushes; shrinking from 4 to 2 returned 15 instead of 25 after three
pushes (`logs/029-ctest-final-red-resize.log`).

The fix assigns `this->size = size` after the buffer resize succeeds. The deterministic
regression in `tests/src/plugins/HistoricalBufferResizeTest.cpp` checks ordinary average,
normal circular overwrite, growth, shrink, and the changed-capacity wrap.

RED/GREEN proof (CTest ran from `build/tests`):

- RED: `ctest -R '^HistoricalBufferResizeTest$' --output-on-failure` — EXIT 8, with both
  growth and shrink assertions failing (`logs/029-ctest-final-red-resize.log`).
- GREEN: the identical command — EXIT 0 (`logs/033-ctest-final-green-resize.log`).

## new-p21-188 — empty capacity

**Decision: REAL, with a corrected cause.** The reported divide-by-zero is not what the
current scalar mean does: `sfz::meanScalar` returns zero for size 0
(`plugins/SfzPlayer/sfizz/src/sfizz/simd/HelpersScalar.h:147-158`). The empty-buffer path
fails earlier. `Buffer::resize(0)` clears its storage (`plugins/SfzPlayer/sfizz/src/sfizz/Buffer.h:200-218`),
then `HistoricalBuffer::resize()` unconditionally called `sfz::fill()` on that empty
buffer (`HistoricalBuffer.h:37-40`). The fill forms its range as `output` to
`output + size` (`plugins/SfzPlayer/sfizz/src/sfizz/SIMDHelpers.h:139-146`); with a null
data pointer and size zero, the test process segfaulted during construction/resize
(`logs/030-ctest-final-red-empty.log`).

The fix skips filling an empty buffer and has `getAverage()` return `ValueType{0}` when
empty (`HistoricalBuffer.h:39-40, 65-73`). The regression in
`tests/src/plugins/HistoricalBufferEmptyTest.cpp` checks zero-sized construction, resize
to zero, and ignored pushes.

RED/GREEN proof (CTest ran from `build/tests`):

- RED: `ctest -R '^HistoricalBufferEmptyTest$' --output-on-failure` — EXIT 8, SIGSEGV
  (`logs/030-ctest-final-red-empty.log`).
- GREEN: the identical command — EXIT 0 (`logs/034-ctest-final-green-empty.log`).

## Test matrix and runtime scope

The tests cover the ordinary ring path, full-capacity overwrite, growth and shrink
reconfiguration, zero-capacity construction and resize, and pushes into an empty buffer.
`Buffer::resize()` exposes no allocation-failure injection seam; its existing exception
behavior is unchanged. The header had no in-tree callers before these tests. No render or
audio-thread path was changed: `push()` still only writes into the buffer, `getAverage()`
adds no allocation, lock, wait, or log, and `resize()` retains its existing allocation
behavior. The C++20 test target uses the already-built vendored `sfzplayer_sfizz` library
and QtTest; no new platform dependency was added.

## Evidence table

All logs are in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-187.r1/logs`.
Build commands ran from the worktree root; CTest commands ran from `build/tests`.

| Phase | Exact command | EXIT | Log |
|---|---|---:|---|
| Save/reverse product-only patch | `git diff -- plugins/SfzPlayer/sfizz/src/sfizz/HistoricalBuffer.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-187.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-187.r1/logs/fix.patch` | 0 | `013-product-revert.log` (repeated at `027-product-revert.log`) |
| Build with product patch reverted | `cmake --build build --target zene HistoricalBufferResizeTest HistoricalBufferEmptyTest` | 0 | `028-build-final-tests-red-proof.log` |
| RED, resize | `ctest -R '^HistoricalBufferResizeTest$' --output-on-failure` | 8 | `029-ctest-final-red-resize.log` |
| RED, empty capacity | `ctest -R '^HistoricalBufferEmptyTest$' --output-on-failure` | 8 | `030-ctest-final-red-empty.log` |
| Reapply product-only patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-187.r1/logs/fix.patch` | 0 | `031-product-reapply.log` |
| Build with product patch restored | `cmake --build build --target zene HistoricalBufferResizeTest HistoricalBufferEmptyTest` | 0 | `032-build-final-tests-green-proof.log` |
| GREEN, resize | `ctest -R '^HistoricalBufferResizeTest$' --output-on-failure` | 0 | `033-ctest-final-green-resize.log` |
| GREEN, empty capacity | `ctest -R '^HistoricalBufferEmptyTest$' --output-on-failure` | 0 | `034-ctest-final-green-empty.log` |
| Affected family | `ctest -R '^HistoricalBuffer(Resize|Empty)Test$' --output-on-failure` | 0; 2/2 passed | `035-ctest-final-green-family.log` |
| Existing SFZ family build | `cmake --build build --target SfzPlayerTest SfzPlayerRealtimeTest` | 0 | `022-build-sfz-tests.log` |
| Existing SFZ family | `ctest -R '^SfzPlayer(Test|RealtimeTest)$' --output-on-failure` | 0; 2/2 passed | `023-ctest-sfz-tests.log` |
| Initial feature gate before the whole-tree list was regenerated | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-187` | 1 | `024-feature-check.log` |
| Fork-source registration gate after rederiving the list | `bash tests/fork-sources-gate.sh` | 0 | `041-fork-sources-gate.log` |
| Whole-tree list recipe | `bash tests/all-sources-reproduce.sh` | 0 | `042-all-sources-reproduce.log` |
| Final feature gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-187` | 0 | `043-feature-check-final.log` |
| Completion check on the committed tree | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-187 new-p21-187` | 0 | `046-submit-check-after-commit.log` |

Both new test sources and this report are registered in `tests/fork-sources.txt`. The CMake
and `tests/all-sources.txt` registrations are recorded in `tests/upstream-modifications.txt`;
the vendored header's entry there now records these local fixes. The whole-tree list was
rederived from its existing recipe to include both test sources; its recipe and exclusions
did not change.

## Files committed

Commit `a42f71003` contains:

- `plugins/SfzPlayer/sfizz/src/sfizz/HistoricalBuffer.h`
- `tests/src/plugins/HistoricalBufferResizeTest.cpp`
- `tests/src/plugins/HistoricalBufferEmptyTest.cpp`
- `tests/CMakeLists.txt`
- `tests/fork-sources.txt`
- `tests/all-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p21-187.md`

## Self-check

- **RED/GREEN:** with the product-only patch reversed, resize CTest EXIT 8
  (`029-ctest-final-red-resize.log`) and empty CTest EXIT 8
  (`030-ctest-final-red-empty.log`); after restoring the patch they exit 0
  (`033-ctest-final-green-resize.log`, `034-ctest-final-green-empty.log`). The combined
  affected family exits 0 with 2/2 CTest targets passing (`035-ctest-final-green-family.log`).
- **Determinism:** resize tests inject fixed values and compare the resulting average;
  the empty test constructs/resizes to zero and checks ignored pushes. There are no clocks,
  sleeps, worker threads, or race-dependent assertions.
- **Cause and callers:** constructor calls `resize()`, and `push()` uses the same stored size
  for writes and modulo. The test directly covers both growth and shrink plus zero capacity.
  A search found no other in-tree `HistoricalBuffer` callers. The empty path is also checked
  against the scalar mean's size-zero guard at `simd/HelpersScalar.h:147-158`.
- **Collateral and realtime:** project files, saved data, commands, and undo are untouched.
  No new operation on an audio thread allocates, frees, locks, waits, performs I/O, or logs;
  `resize()` retains its existing allocation behavior.
- **Verdict evidence:** both clusters are FIXED. The empty finding's claimed division by zero
  is corrected above with the observed zero-range fill crash and its file/line evidence.
- **Gates:** the final `feature_check.py` is EXIT 0 (`043-feature-check-final.log`); the
  scope and manifest checks are EXIT 0 (`041-fork-sources-gate.log`,
  `042-all-sources-reproduce.log`).
- **Commit and completion check:** commit `a42f71003` contains the eight files listed above.
  `submit_check.py` exited 0 on that committed, clean tree (`046-submit-check-after-commit.log`);
  its commit, clean-tree, report, test-registration, and feature-gate checks all passed.
