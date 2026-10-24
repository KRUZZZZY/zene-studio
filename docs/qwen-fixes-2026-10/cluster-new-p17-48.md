FIXED

# new-p17-48 — Parameter aliases can be cut in the middle of an emoji

## Finding and evidence

The finding is **REAL**. `QString::left(MaxAliasLength)` counted UTF-16 code units, so with 63 ASCII characters followed by an emoji it retained the emoji's high surrogate and discarded its low surrogate. `DeviceOptions::setAlias()` now counts valid UTF-16 surrogate pairs as one code point and truncates after at most 64 code points (`src/core/DeviceOptions.cpp:64-75`; limit: `include/DeviceOptions.h:86`). The direct parameter rename path delegates to this setter (`src/core/ControlCommandsDeviceParams.cpp:144-151`, `src/core/ParameterAliases.cpp:135-142`), and project loading also uses it (`src/core/DeviceOptions.cpp:104-108`), so these paths share the fix.

The regression at `tests/src/core/DeviceToolsTest.cpp:361-380` sets an alias to 63 ASCII characters plus an emoji, then saves and reloads it. Against the original code it failed with actual `...\uD83D` versus expected `...\uD83D\uDE00`; it passes with the fix. The test uses in-memory values and DOM serialization, with no clock, sleep, or race.

## Validation evidence

| Check | Exact command | Exit | Result |
|---|---|---:|---|
| Build regression on original code | `cmake --build build --target DeviceToolsTest` | 0 | Built test before production change. |
| RED on original code | From `build/tests`: `./DeviceToolsTest aliasTruncationPreservesUnicodeCodePoints` | 1 | Failed at stored alias comparison; lone `\uD83D`. |
| Build fixed code and affected targets | `cmake --build build --target zene DeviceToolsTest DeviceToolsChainTest RealtimeSnapshotTest` | 0 | `zene` and all three affected test targets built. |
| GREEN | From `build/tests`: `./DeviceToolsTest aliasTruncationPreservesUnicodeCodePoints` | 0 | Regression and save/load round trip passed. |
| Save fix-only patch and reverse it | `git diff -- src/core/DeviceOptions.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-48.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-48.r1/logs/fix.patch` | 0 | Reversed only the product-code hunk. |
| Rebuild without fix | `cmake --build build --target zene DeviceToolsTest` | 0 | Rebuilt before counterproof. |
| Counterproof RED | From `build/tests`: `./DeviceToolsTest aliasTruncationPreservesUnicodeCodePoints` | 1 | Same comparison failed again. |
| Restore fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-48.r1/logs/fix.patch` | 0 | Restored product change. |
| Final rebuild | `cmake --build build --target zene DeviceToolsTest DeviceToolsChainTest RealtimeSnapshotTest` | 0 | All affected targets rebuilt. |
| Counterproof GREEN | From `build/tests`: `./DeviceToolsTest aliasTruncationPreservesUnicodeCodePoints` | 0 | Passed after restoring fix. |
| Affected ctest family | From `build/tests`: `ctest -R '^(RealtimeSnapshotTest|DeviceToolsChainTest|DeviceToolsTest)$' --output-on-failure` | 0 | 3/3 tests passed. |
| Initial feature gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p17-48` | 1 | Exposed the pre-existing stale `tests/all-sources.txt` entries for vendored sfizz sources. |
| Final feature gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p17-48` | 0 | All rows PASS; complexity and file-length report no new items. The manifest was regenerated using its documented recipe (`tests/all-sources.txt:19-27`), which excludes vendored sfizz as the comments require (`tests/all-sources.txt:102-109`). |
| Completion check | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p17-48 new-p17-48` | 0 | Passed after the first cluster commit; rerun after the final report commit. |

Build and test output, in command order, is recorded in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-48.r1/logs/01-build-red.log` through `09-family-test.log`; gate output is in `10-feature-check.log`, `14-feature-check-final.log`, and `15-feature-check-final.log`; the first completion-check output is in `16-submit-check.log`. The first gate failure was the stale manifest, not this code change; after regeneration, `fork-sources` and `all-sources-reproduce` both pass.

## Files in the cluster commit

- `src/core/DeviceOptions.cpp`
- `tests/src/core/DeviceToolsTest.cpp`
- `tests/all-sources.txt` (rederived to match its documented recipe and scope)
- `docs/qwen-fixes-2026-10/cluster-new-p17-48.md`

## Self-check

- **Regression proof:** The same targeted test exits 1 on the original code, exits 1 with the product patch reversed and rebuilt, and exits 0 after the patch is restored and rebuilt. See the commands and results above.
- **Determinism:** The test directly constructs a fixed alias and uses in-memory save/load. It uses no time-based behavior, sleeps, races, or external state.
- **Cause and callers:** The changed code is the shared bound in `DeviceOptions::setAlias()`. The parameter rename path, legacy alias adoption through `setAlias()`, and XML project load all converge on it. Searches found no second alias truncation site.
- **Compatibility:** The alias key, trimming, persistence format, and 64-code-point bound remain intact. Previously saved aliases within the old 64-code-unit limit still fit within 64 code points; save/load round-trips the repaired boundary case. The changed function is on the control path, not the audio thread.
- **Verdict evidence:** The finding is FIXED, so the ALREADY-FIXED/FALSE-POSITIVE evidence request does not apply.
