# Cluster new-p12-141 — FIXED

## Finding

Repeated refusals could replace an unserved intermediate MIDI recovery cut. The queue retained the earliest and latest stamps, so refusals at stamps 3, 4, and 5 left stamp 5 after CUT3 cleared.

`MidiChannelOverflow` now retains each distinct pending stamp in fixed per-channel atomic slots. The consumer selects the earliest stamp and refreshes its cached head after clearing a cut. Storage is bounded to 257 positions per channel, requires no allocation or mutex, and includes the production queue's 256 positions plus the next cut boundary.

Evidence: `include/MidiHandoffQueue.h:290` selects the earliest due cut; `:350`, `:432`, and `:507` define and store bounded per-channel cut slots; `:358` and `:368` record stamps; `:396` and `:409` select the effective stamp; `:446` finds the earliest slot; and `:467` refreshes the cached head after clearing. The regression in `tests/src/core/MidiHandoffQueueTest.cpp:340` records refusals at stamps 3, 4, and 5 (`:354`, `:368`, `:371`), checks stamp 4 after CUT3 (`:374`), and checks CUT4 before E4 (`:377`).

## Verification

The affected CTest family contains two binaries: `MidiHandoffQueueTest` and `CarlaMidiHandoffTest`. In the RED run, `MidiHandoffQueueTest` failed at the stamp-4 assertion (actual stamp 5; 14 internal Qt checks passed and one failed), while `CarlaMidiHandoffTest` passed. The same failure was reproduced after reversing the product patch. With the product change restored, both binaries passed.

| Check | Command log | Exit | Result |
|---|---|---:|---|
| Initial RED on original header | `test-1.log` | 8 | `MidiHandoffQueueTest` failed; `CarlaMidiHandoffTest` passed |
| RED with final patch reversed | `test-5.log` | 8 | `MidiHandoffQueueTest` failed (actual 5, expected 4); `CarlaMidiHandoffTest` passed |
| GREEN with final patch restored | `test-6.log` | 0 | 2/2 CTest binaries passed |
| Affected-family test count | `test-6.log` | 0 | 2 CTest binaries |
| Complexity ratchet | `gate-4.log` | 1 | Unrelated over-target entries remain |
| File-length ratchet | `gate-5.log` | 1 | Unrelated overlength entries remain |

The exact RED, GREEN, build, reversal, and gate commands follow.

The product-only reversal and restoration commands both exited 0:

```sh
git diff -- include/MidiHandoffQueue.h > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-141.r2/logs/fix.patch' && git apply -R '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-141.r2/logs/fix.patch'
git apply '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-141.r2/logs/fix.patch'
```

The initial RED run used the original header before any product edit. The exact initial RED, final reversal proof, and final green commands were (CTest ran from `build/tests`):

```sh
ctest -R '^(MidiHandoffQueueTest|CarlaMidiHandoffTest)$' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-141.r2/logs/test-1.log' 2>&1; rc=$?; printf 'EXIT=%d\n' "$rc" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-141.r2/logs/test-1.log'; echo EXIT=$rc; exit $rc
cmake --build build --target zene MidiHandoffQueueTest CarlaMidiHandoffTest RemotePluginAudioPortsTest RemotePluginClientE2ETest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-141.r2/logs/build-5.log' 2>&1; rc=$?; printf 'EXIT=%d\n' "$rc" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-141.r2/logs/build-5.log'; echo EXIT=$rc; exit $rc
ctest -R '^(MidiHandoffQueueTest|CarlaMidiHandoffTest)$' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-141.r2/logs/test-5.log' 2>&1; rc=$?; printf 'EXIT=%d\n' "$rc" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-141.r2/logs/test-5.log'; echo EXIT=$rc; exit $rc
cmake --build build --target zene MidiHandoffQueueTest CarlaMidiHandoffTest RemotePluginAudioPortsTest RemotePluginClientE2ETest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-141.r2/logs/build-6.log' 2>&1; rc=$?; printf 'EXIT=%d\n' "$rc" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-141.r2/logs/build-6.log'; echo EXIT=$rc; exit $rc
ctest -R '^(MidiHandoffQueueTest|CarlaMidiHandoffTest)$' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-141.r2/logs/test-6.log' 2>&1; rc=$?; printf 'EXIT=%d\n' "$rc" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-141.r2/logs/test-6.log'; echo EXIT=$rc; exit $rc
```

The final focused tests therefore have an exact RED command with exit 8 and an exact GREEN command with exit 0. The final build rebuilt `zene` and every test target directly including the changed header.

Both required ratchet gates were rerun in check mode. They still report unrelated worktree entries; neither lists `MidiHandoffQueue.h` or `MidiHandoffQueueTest.cpp`:

```sh
bash tests/complexity-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-141.r2/logs/gate-4.log' 2>&1; rc=$?; printf 'EXIT=%d\n' "$rc" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-141.r2/logs/gate-4.log'; echo EXIT=$rc; exit $rc
bash tests/file-length-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-141.r2/logs/gate-5.log' 2>&1; rc=$?; printf 'EXIT=%d\n' "$rc" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-141.r2/logs/gate-5.log'; echo EXIT=$rc; exit $rc
```

Log searches used `rg` because the requested `fff` tool was unavailable in this session.

## Files in the commit

- `include/MidiHandoffQueue.h`
- `tests/src/core/MidiHandoffQueueTest.cpp`
- `tests/fork-sources.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p12-141.md`
