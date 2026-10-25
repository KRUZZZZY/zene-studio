FIXED

Finding: REAL. In [ProjectKey.cpp](../../src/core/ProjectKey.cpp#L92), `loadSettings()` previously called `toInt()` and both `toDouble()` conversions without checking success, then returned true. A malformed value therefore became zero while the element remained nonempty. The loader now checks all three conversions before assigning any fields. Since it clears first, an unreadable value returns false with an empty key. `SongSections.cpp:188` uses this loader for project XML; `fromXml()` also delegates to it at `ProjectKey.cpp:150`.

The existing missing-attribute behavior is preserved: absent pitch class defaults to `-1`, and absent confidence or margin defaults to `0`. The new source list note records the gate-required regeneration of `tests/all-sources.txt`: its published recipe excluded 1,148 stale vendored sfizz paths and had no missing entries.

## Test matrix

| Path | Coverage / result |
| --- | --- |
| Normal saved key | `theProjectKeyRoundTripsThroughItsXml` covers save, load, and field equality. |
| Optional numeric attributes omitted | `omittedProjectKeyNumbersKeepTheirDefaults` checks `-1`, `0`, and `0`. |
| Invalid pitch class | `unreadableProjectKeyNumbersAreRejectedAndCleared` calls `loadSettings()` with `pitch-class="bad"`; expects false and an empty key. |
| Invalid confidence | Same regression with `confidence="bad"`; expects false and an empty key. |
| Invalid margin | Same regression with `margin="bad"`; expects false and an empty key. |
| Exhaustion / reconfiguration | Not applicable: this value parser has no capacity or runtime reconfiguration state. The parse-failure cases above exercise its failure paths. |
| Audio-thread constraints | `loadSettings()` is reached from project section loading and XML action restore; no audio render/process caller is present. This change adds no audio-thread work. |
| Platform build | Built the registered Qt `ImportDetectionTest` target in the configured Linux build. No separate platform build was available in this worktree. |

## Commands and evidence

All builds ran from the worktree root. All ctest commands ran from `build/tests`. Build and test output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4345.r1/logs`.

| Run | Command | Exit | Evidence |
| --- | --- | ---: | --- |
| Initial RED build | `cmake --build build --target zene ImportDetectionTest` | 0 | `build-1.log` |
| Initial RED test | `ctest -R '^ImportDetectionTest$' --output-on-failure` | 8 | `test-1.log`: 14 passed, 1 failed; assertion showed malformed pitch class was accepted. |
| GREEN build | `cmake --build build --target zene ImportDetectionTest` | 0 | `build-2.log` |
| GREEN test | `ctest -R '^ImportDetectionTest$' --output-on-failure` | 0 | `test-2.log`: 100% tests passed, 1/1 registered test. |
| Revert product fix | `git diff -- src/core/ProjectKey.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4345.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4345.r1/logs/fix.patch` | 0 | Product-only patch reversed. |
| 4b RED rebuild | `cmake --build build --target zene ImportDetectionTest` | 0 | `build-3.log` |
| 4b RED test | `ctest -R '^ImportDetectionTest$' --output-on-failure` | 8 | `test-3.log`: the same malformed pitch-class assertion failed. |
| Restore product fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/4345.r1/logs/fix.patch` | 0 | Product fix restored. |
| 4b GREEN rebuild | `cmake --build build --target zene ImportDetectionTest` | 0 | `build-4.log` |
| 4b GREEN test | `ctest -R '^ImportDetectionTest$' --output-on-failure` | 0 | `test-4.log`: 100% tests passed, 1/1 registered test. |

The affected ctest family contains one registered test, `ImportDetectionTest`. The red runs prove the regression fails without the product change; the green runs prove it passes with the change.

### Feature gate table

The first `feature_check.py` run exited 1 because the pre-existing `tests/all-sources.txt` did not reproduce from its own recipe. The manifest was regenerated from that recipe after confirming 1,148 stale excluded-vendor entries and zero missing entries. The final gate run is:

| Gate | Result |
| --- | --- |
| fork-sources | PASS (exit 0) |
| all-sources-reproduce | PASS (exit 0) |
| no-upstream-regression | PASS (exit 0) |
| check-namespace | PASS (exit 0) |
| no-tautology | PASS (exit 0) |
| doc-figures | PASS (exit 0) |
| complexity | PASS (no new item in changed files) |
| file-length | PASS (no new item in changed files) |
| `feature_check.py` | EXIT=0 (`gate-4.log`; the initial run was EXIT=1 in `gate-1.log`) |

Exact gate command: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-4345`.

## Self-check

- **Regression red/green:** with the product-only patch reversed, `ctest -R '^ImportDetectionTest$' --output-on-failure` exited 8 in `test-3.log`; after restoring the patch, the same command exited 0 in `test-4.log`. Both builds completed with exit 0.
- **Determinism:** the regression parses three fixed XML strings synchronously. It uses no wall-clock values, sleeps, races, or asynchronous work.
- **Cause and completeness:** `loadSettings()` checks pitch class, confidence, and margin before assignment. `SongSections.cpp:188` and `ProjectKey::fromXml()` at `ProjectKey.cpp:150` are the two callers; both use the checked loader. Missing attributes keep the prior defaults.
- **Collateral behavior:** normal saved XML round-trips; absent optional values remain `-1`, `0`, and `0`. No numeric bounds were added, so existing numeric values accepted by Qt conversions remain accepted. No command, undo, or realtime path changed.
- **Conditional verdict evidence:** the verdict is FIXED, so the ALREADY-FIXED / FALSE-POSITIVE condition does not apply. The original unchecked conversions were at `ProjectKey.cpp:104-107`; the current checks are at `ProjectKey.cpp:103-122`.
- **Platform:** built and tested in the configured Linux / Qt build. No separate Qt 5.15, MSVC, or macOS build was available here.

## Files in this commit

- `src/core/ProjectKey.cpp`
- `tests/src/core/ImportDetectionTest.cpp`
- `tests/all-sources.txt` (recipe-derived source manifest repair required for the registration gates)
- `docs/qwen-fixes-2026-10/cluster-4345.md`
