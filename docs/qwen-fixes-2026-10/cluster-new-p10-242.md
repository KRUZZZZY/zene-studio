# Cluster new-p10-242: MIDI song position wraps

## Verdict: FIXED

`MidiClock::songPositionOf()` in `src/core/MidiClock.cpp:123-130` now saturates at the largest representable 14-bit MIDI Song Position Pointer value (`0x3FFF`). Before the fix, masking caused 16,384 beats to become zero. The boundary regression is in `tests/src/core/MidiClockTest.cpp:459-468`.

The `MidiClockTest` ctest family contains one registered test executable. RED runs executed 14 Qt cases (13 passed, the new boundary assertion failed); GREEN runs passed the registered test.

## Evidence

| Check | Exact command | Exit |
|---|---|---:|
| RED, original implementation (`test-1.log`) | `ctest -R '^MidiClockTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-242.r1/logs/test-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-242.r1/logs/test-1.log; printf 'CTEST_EXIT=%s\n' "$rc"` | 8 |
| GREEN, fixed implementation (`test-2.log`) | `ctest -R '^MidiClockTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-242.r1/logs/test-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-242.r1/logs/test-2.log; printf 'CTEST_EXIT=%s\n' "$rc"` | 0 |
| Reverse product patch (`fix.patch`) | `git diff -- src/core/MidiClock.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-242.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-242.r1/logs/fix.patch; rc=$?; printf 'PROOF_REVERSE_EXIT=%s\n' "$rc"` | 0 |
| RED after reverse patch (`test-3.log`) | `ctest -R '^MidiClockTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-242.r1/logs/test-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-242.r1/logs/test-3.log; printf 'CTEST_EXIT=%s\n' "$rc"` | 8 |
| Reapply product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-242.r1/logs/fix.patch; rc=$?; printf 'PROOF_REAPPLY_EXIT=%s\n' "$rc"` | 0 |
| GREEN after reapply (`test-4.log`) | `ctest -R '^MidiClockTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-242.r1/logs/test-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-242.r1/logs/test-4.log; printf 'CTEST_EXIT=%s\n' "$rc"` | 0 |

The failed assertion in both RED runs was `MidiClock::songPositionOf(lastRepresentableTick + TicksPerMidiBeat)`: actual `0`, expected `16383`.

Builds of `zene` and `MidiClockTest` succeeded before each corresponding ctest run. Exact captured build commands and exits:

- `cmake --build build --target zene MidiClockTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-242.r1/logs/build-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-242.r1/logs/build-2.log; printf 'BUILD_EXIT=%s\n' "$rc"` — exit 0 (RED baseline build).
- `cmake --build build --target zene MidiClockTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-242.r1/logs/build-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-242.r1/logs/build-3.log; printf 'BUILD_EXIT=%s\n' "$rc"` — exit 0 (GREEN build).
- `cmake --build build --target zene MidiClockTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-242.r1/logs/build-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-242.r1/logs/build-4.log; printf 'BUILD_EXIT=%s\n' "$rc"` — exit 0 (reverse-patch RED build).
- `cmake --build build --target zene MidiClockTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-242.r1/logs/build-5.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-242.r1/logs/build-5.log; printf 'BUILD_EXIT=%s\n' "$rc"` — exit 0 (reapplied-patch GREEN build).

Both required gates passed:

- `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-242.r1/logs/gate-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-242.r1/logs/gate-1.log; printf 'COMPLEXITY_EXIT=%s\n' "$rc"` — exit 0.
- `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-242.r1/logs/gate-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-242.r1/logs/gate-2.log; printf 'FILE-LENGTH_EXIT=%s\n' "$rc"` — exit 0.

The changed product and test files are already listed in `tests/fork-sources.txt`; neither appears in `tests/upstream-modifications.txt`. No manifest edit was needed.

Log searches used `rg` because no fff grep tool was exposed in this session.

## Files committed

- `src/core/MidiClock.cpp`
- `tests/src/core/MidiClockTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-242.md`
