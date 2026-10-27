FIXED

# Cluster new-p17-166 - Malformed project numbers silently become zero

## Verdict

The finding is REAL. `ClipSlot::restoreState` used unchecked `QString::toInt()` for `launchmode`, `legato`, `loopstart`, `looplength`, `transpose`, `detune`, `ram`, and `warp` (`src/core/SessionClip.cpp:216-230`). A malformed or overflowing value becomes zero, which is also the documented default for these fields. The loaded field value is therefore unchanged, but the malformed project data was silently discarded on the next save and the user received no diagnostic.

The claim that there was no load-diagnostic sink was incorrect: `Song::collectError()` records load errors (`include/Song.h:167-176`), and `Song::loadProject()` presents `errorSummary()` or returns those errors to a headless caller (`src/core/Song.cpp:2296-2310`).

## Fix

`clipIntegerAttribute()` now uses the strict integer reader and reports each present, unparseable clip integer through `Song::collectError()` (`src/core/SessionClip.cpp:42-56`). It preserves the previous fallback values, leaves absent attributes unreported, and retains valid integer values. All eight unchecked integer reads use this helper.

The existing registered `SessionMalformedAttributeTest` now covers valid nonzero clip integers, malformed text, and an overflowing detune. It verifies the prior defaults remain intact and that all eight malformed attributes produce distinct load errors (`tests/src/core/SessionMalformedAttributeTest.cpp:210-269`).

## Verification

Commands below were run in this worktree. Build logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-166.r1/logs/`.

| Command | EXIT | Evidence |
| --- | ---: | --- |
| `cmake --build build --target zene SessionMalformedAttributeTest` (regression added; product unchanged) | 0 | `qa-red-build-1.log` |
| `ctest -R SessionMalformedAttributeTest --output-on-failure` (product unchanged) | 8 | `qa-red-test-1.log`: expected 8 errors, got 0 |
| `cmake --build build --target zene SessionMalformedAttributeTest SessionModelTest` (fixed) | 0 | `qa-final-build-2.log` |
| `ctest -R 'Session(Model|MalformedAttribute)Test' --output-on-failure` (fixed) | 0 | `qa-final-test-2.log`: 2/2 tests pass |
| Reverse only `src/core/SessionClip.cpp` using `git apply -R qa-product-fix.patch`; rebuild the same targets | 0 | `qa-reversed-build-2.log` |
| `ctest -R 'Session(Model|MalformedAttribute)Test' --output-on-failure` (product change reversed) | 8 | `qa-reversed-test-2.log`: `SessionModelTest` passes; regression fails at line 262 (0 errors, expected 8) |
| Restore the product patch and rebuild the same targets | 0 | `qa-restored-build-2.log` |
| `ctest -R 'Session(Model|MalformedAttribute)Test' --output-on-failure` (restored) | 0 | `qa-restored-test-2.log`: 2/2 tests pass |
| `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p17-166` | 0 | `qa-feature-check-final-2.log`: all checks pass; no new complexity or file-length item |

## Self-check

- Regression is deterministic: XML fixtures only; no timing, sleeps, or races. It is RED without the product change and GREEN with it.
- Valid nonzero inputs still load unchanged. Malformed inputs keep their prior defaults and now appear in the project load error report.
- The change is confined to session project deserialization; it adds no realtime work, command, or test suppression.
- The test source was already registered in `tests/CMakeLists.txt` and `tests/fork-sources.txt`.
- Focused tests: `SessionMalformedAttributeTest` and `SessionModelTest`; both pass after the product patch is restored.
