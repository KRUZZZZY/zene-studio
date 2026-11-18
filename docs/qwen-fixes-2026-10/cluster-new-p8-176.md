# Cluster new-p8-176: invalid note probability

**Verdict: FIXED**

`src/core/Note.cpp:455-458` now checks `QString::toFloat` conversion success and finiteness. A malformed or non-finite `prob` value falls back to the documented default, `1.f`, before the existing range clamp. The regression at `tests/src/core/MidiProbabilityPersistenceTest.cpp:174-192` covers `prob="oops"` and `prob="nan"`; both rows failed before the fix and passed after it.

The focused CTest family was `MidiProbabilityPersistenceTest`: 1/1 CTest target ran. The pre-fix run reported 6 passed and 2 failed QtTest cases; the fixed runs passed.

## Command evidence

All build and test logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-176.r1/logs`.

| Purpose | Exact command | Exit |
| --- | --- | --- |
| Original-code build | `cmake --build build --target zene MidiProbabilityPersistenceTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-176.r1/logs/build-1.log 2>&1; build_exit=$?; printf 'EXIT=%s\n' "$build_exit"; exit "$build_exit"` | 0 |
| Original-code RED | `ctest -R '^MidiProbabilityPersistenceTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-176.r1/logs/test-1.log 2>&1; test_exit=$?; printf 'EXIT=%s\n' "$test_exit"; exit "$test_exit"` | 8 |
| Fixed build | `cmake --build build --target zene MidiProbabilityPersistenceTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-176.r1/logs/build-2.log 2>&1; build_exit=$?; printf 'EXIT=%s\n' "$build_exit"; exit "$build_exit"` | 0 |
| Fixed GREEN | `ctest -R '^MidiProbabilityPersistenceTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-176.r1/logs/test-2.log 2>&1; test_exit=$?; printf 'EXIT=%s\n' "$test_exit"; exit "$test_exit"` | 0 |
| Save fix and reverse it for the inverse proof | `git diff -- src/core/Note.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-176.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-176.r1/logs/fix.patch > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-176.r1/logs/revert-1.log 2>&1; proof_exit=$?; printf 'EXIT=%s\n' "$proof_exit"; exit "$proof_exit"` | 0 |
| Inverse-proof build | `cmake --build build --target zene MidiProbabilityPersistenceTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-176.r1/logs/build-3.log 2>&1; build_exit=$?; printf 'EXIT=%s\n' "$build_exit"; exit "$build_exit"` | 0 |
| Inverse-proof RED | `ctest -R '^MidiProbabilityPersistenceTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-176.r1/logs/test-3.log 2>&1; test_exit=$?; printf 'EXIT=%s\n' "$test_exit"; exit "$test_exit"` | 8 |
| Restore fix patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-176.r1/logs/fix.patch > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-176.r1/logs/restore-1.log 2>&1; proof_exit=$?; printf 'EXIT=%s\n' "$proof_exit"; exit "$proof_exit"` | 0 |
| Restored-fix build | `cmake --build build --target zene MidiProbabilityPersistenceTest -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-176.r1/logs/build-4.log 2>&1; build_exit=$?; printf 'EXIT=%s\n' "$build_exit"; exit "$build_exit"` | 0 |
| Restored-fix GREEN | `ctest -R '^MidiProbabilityPersistenceTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-176.r1/logs/test-4.log 2>&1; test_exit=$?; printf 'EXIT=%s\n' "$test_exit"; exit "$test_exit"` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-176.r1/logs/complexity-gate-1.log 2>&1; gate_exit=$?; printf 'EXIT=%s\n' "$gate_exit"; exit "$gate_exit"` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-176.r1/logs/file-length-gate-1.log 2>&1; gate_exit=$?; printf 'EXIT=%s\n' "$gate_exit"; exit "$gate_exit"` | 0 |

The `fff` grep tool was not exposed in this session and `command -v fff` returned no path. I searched the log folder with `rg -n` for `error`, `FAILED`, and `Assertion`; the RED entries are the expected regression failures.

## Files in the commit

- `src/core/Note.cpp`
- `tests/src/core/MidiProbabilityPersistenceTest.cpp`
- `tests/fork-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p8-176.md`
