FIXED

# new-p16-189: omitted retro MIDI capture edge at negative ticks

## Decision and evidence

**Verdict: REAL, fixed.** Before the change, `include/RetroMidiClipWriter.h:96` defaulted `windowEndTick` to `-1`; `src/core/RetroMidiClipWriter.cpp:141` treated that value as a real edge and floored it against the start tick. For a lone note-on at tick `-10`, with start `-10` and no explicit edge, the unmatched note therefore ended at `-1` and had length 9.

The declaration now represents omission with `std::optional<tick_t>` and defaults to `std::nullopt` (`include/RetroMidiClipWriter.h:96`). The implementation uses the start tick as the fallback floor only when the edge is omitted, while retaining the existing start clamp for explicit edges (`src/core/RetroMidiClipWriter.cpp:133-142`). The regression injects the `-10` timestamp and checks the selected edge and one-tick minimum (`tests/src/core/RetroMidiCaptureCommandsTest.cpp:310-331`). The red run failed at `written.windowEnd` (`-1`, expected `-10`); the reverse-patch proof reproduced the same failure.

## Regression matrix

| Path | Coverage and result |
| --- | --- |
| Normal omitted-edge path | `omittedWindowEndAtNegativeTickUsesLatestEvent`: a single unmatched note-on at `-10` with start `-10` produces end `-10`, position 0, and length 1. Fails without the fix and passes with it. |
| Explicit edge and later note events | Existing writer tests cover omitted edges ending at later note events; the MIDI and session-capture callers pass explicit capture ends. Both callers and the test target compiled after the signature change; the full capture test passed. Explicit edge clamping remains `max(start, edge)`. |
| Refusal, exhaustion, reconfiguration | Existing `emptyWindowWritesNothing` covers null clip, null events, and empty input. The writer consumes a completed caller-provided snapshot and keeps no ring state; ring exhaustion and capture reconfiguration do not enter this function and were not changed. |
| Audio-thread constraints | This writer remains on the GUI/control consumer path as documented in its header. `std::optional<tick_t>` is stack value handling; no audio-thread path, allocation, locking, waiting, or logging was added. |
| Build/platform | Rebuilt the configured Linux C++20 `zene` target and `RetroMidiCaptureCommandsTest`, which compile all three header consumers (`ControlCommandsMidi.cpp`, `ControlCommandsSessionCapture.cpp`, and the test). The API uses standard C++20 `std::optional`; other platform toolchains were not available in this worktree. |

## Gate blocker repair

The first feature check found two failures caused by source registration: the modified test had no `tests/upstream-modifications.txt` entry, and `tests/all-sources.txt` still listed 1,148 vendored `plugins/SfzPlayer/sfizz/` files even though its own header excludes that tree. The test registration was added in commit `c2ec522c9`. The manifest entry list was regenerated with its documented recipe; the header and gate thresholds were left unchanged. A diff check confirmed that all 1,148 removed entries were under the excluded sfizz path and that no entries were added. The manifest now reproduces and the feature gates pass.

## Command evidence

All build and test output was written under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-189.r1/logs`.

| Purpose | Exact command | Exit | Evidence |
| --- | --- | ---: | --- |
| Confirm target registration | `cmake --build build --target help > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-189.r1/logs/build-1.log 2>&1; echo EXIT=$?` | 0 | Target listed in `build-1.log`. |
| Confirm CTest registration | `ctest -N -R '^RetroMidiCaptureCommandsTest$' > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-189.r1/logs/test-1.log 2>&1; echo EXIT=$?` | 0 | One registered CTest test, #131; executable was then built. |
| Build before red run | `cmake --build build --target zene RetroMidiCaptureCommandsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-189.r1/logs/build-2.log 2>&1; echo EXIT=$?` | 0 | Original product code with regression test compiled. |
| RED regression | `ctest -R '^RetroMidiCaptureCommandsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-189.r1/logs/test-2.log 2>&1; echo EXIT=$?` | 8 | `written.windowEnd`: actual `-1`, expected `-10`. |
| GREEN build | `cmake --build build --target zene RetroMidiCaptureCommandsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-189.r1/logs/build-4.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-189.r1/logs/build-4.log; echo EXIT=$result` | 0 | Rebuilt the application and all test/header dependents. |
| GREEN capture family | `ctest -R '^RetroMidiCaptureCommandsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-189.r1/logs/test-3.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-189.r1/logs/test-3.log; echo EXIT=$result` | 0 | CTest 1/1 passed. |
| Reverse product patch for proof | `git diff -- include/RetroMidiClipWriter.h src/core/RetroMidiClipWriter.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-189.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-189.r1/logs/fix.patch; result=$?; echo EXIT=$result` | 0 | Only the two product files were reverted; regression remained. |
| Build without product fix | `cmake --build build --target zene RetroMidiCaptureCommandsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-189.r1/logs/build-5.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-189.r1/logs/build-5.log; echo EXIT=$result` | 0 | Recompiled reverted product code and regression. |
| FAIL proof | `ctest -R '^RetroMidiCaptureCommandsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-189.r1/logs/test-4.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-189.r1/logs/test-4.log; echo EXIT=$result` | 8 | Same `-1` actual versus `-10` expected assertion. |
| Restore product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-189.r1/logs/fix.patch; result=$?; echo EXIT=$result` | 0 | Restored the exact saved product patch. |
| Final build after proof | `cmake --build build --target zene RetroMidiCaptureCommandsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-189.r1/logs/build-6.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-189.r1/logs/build-6.log; echo EXIT=$result` | 0 | `zene` and capture test target rebuilt. |
| Final capture family | `ctest -R '^RetroMidiCaptureCommandsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-189.r1/logs/test-5.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-189.r1/logs/test-5.log; echo EXIT=$result` | 0 | CTest 1/1 passed. |
| QtTest case count | `ctest -V -R '^RetroMidiCaptureCommandsTest$' > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-189.r1/logs/test-6.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-189.r1/logs/test-6.log; echo EXIT=$result` | 0 | QtTest totals: 19 passed, 0 failed; CTest 1/1 passed. |
| Pre-repair manifest reproduction | `bash tests/all-sources-reproduce.sh > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-189.r1/logs/test-7.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-189.r1/logs/test-7.log; echo EXIT=$result` | 1 | Confirmed the blocker before editing the manifest. |
| Generate manifest entries from the documented recipe | `{ git ls-files '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hpp' | grep -vE '^(src/3rdparty/|tests/reference/|plugins/NeuralAmp/(rtneural|nam|tests)/|plugins/RnnoiseDenoiser/rnnoise/|plugins/SfzPlayer/sfizz/)' ; git ls-files tests/control-detect-commands.py tests/control_detect_fixtures.py ; } | LC_ALL=C sort -u > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-189.r1/logs/all-sources-generated-1.txt; result=$?; echo EXIT=$result` | 0 | Used as the entry list; the existing comment header was preserved byte-for-byte. |
| Rewrite manifest entries, preserving its header | `python3` heredoc shown below | 0 | The recipe's output replaced only the manifest's data entries. |
| Post-repair manifest reproduction | `bash tests/all-sources-reproduce.sh > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-189.r1/logs/test-8.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-189.r1/logs/test-8.log; echo EXIT=$result` | 0 | `REPRODUCES: the entry list in all-sources.txt is the recipe's own output.` |
| Focused rebuild after registry repair | `cmake --build build --target zene RetroMidiCaptureCommandsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-189.r1/logs/build-7.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-189.r1/logs/build-7.log; echo EXIT=$result` | 0 | Rebuilt both focused targets. |
| Focused capture test after registry repair | `ctest -R '^RetroMidiCaptureCommandsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-189.r1/logs/test-9.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-189.r1/logs/test-9.log; echo EXIT=$result` | 0 | CTest 1/1 passed. |
| Final feature gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-189 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-189.r1/logs/gate-7.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-189.r1/logs/gate-7.log; echo EXIT=$result` | 0 | Every gate passes; complexity and file-length report no new item. |

The manifest rewrite command was:

```sh
python3 - <<'PY'
from pathlib import Path
manifest = Path('tests/all-sources.txt')
generated = Path('/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-189.r1/logs/all-sources-generated-1.txt').read_bytes()
data = manifest.read_bytes()
lines = data.splitlines(keepends=True)
first_entry = next(i for i, line in enumerate(lines) if line.strip() and not line.lstrip().startswith(b'#'))
manifest.write_bytes(b''.join(lines[:first_entry]) + generated)
PY
```

## Files committed

- `include/RetroMidiClipWriter.h`
- `src/core/RetroMidiClipWriter.cpp`
- `tests/src/core/RetroMidiCaptureCommandsTest.cpp`
- `tests/upstream-modifications.txt` (test registration, commit `c2ec522c9`)
- `tests/all-sources.txt` (entry list regenerated from its documented recipe)
- `docs/qwen-fixes-2026-10/cluster-new-p16-189.md`

## Self-check

1. **Red without the fix, green with it:** `test-4.log` records exit 8 after `fix.patch` reversed only the product hunks; the regression reports actual `-1`, expected `-10`. After restoring that same patch, `test-5.log` records exit 0. The identical CTest command was used both times.
2. **Deterministic:** the test sets `RetroMidiEvent::tick` directly to `-10`, passes start `-10`, and omits the end argument. It uses no clock, sleeps, threads, or race-dependent state.
3. **Cause and callers:** the header declaration and cpp definition changed together. `rg` found the two production callers: `src/core/ControlCommandsMidi.cpp:454` and `src/core/ControlCommandsSessionCapture.cpp:92`; each supplies an explicit capture end and both compiled in the rebuilt `zene` target. There is no overload or sibling writer entry point.
4. **Compatibility:** the change adds no project data, serialization, loader, or renderer changes and adds no limit. Explicit end ticks retain the prior `max(start, edge)` clamp; only omitted edges stop treating `-1` as a tick. The existing project load/save paths were not changed.
5. **Verdict evidence:** this finding is REAL, so the conditional ALREADY-FIXED/FALSE-POSITIVE citation does not apply. The intended omitted-edge contract is `include/RetroMidiClipWriter.h:68-70`; the prior implementation contradicted it at `src/core/RetroMidiClipWriter.cpp:141` by treating `-1` as an edge. The red test proves the user-visible length error.

The feature gate now exits 0. The source registry repair follows the manifest's existing first-party scope and its documented recipe; it did not change gate limits, complexity baselines, or file-length baselines.
