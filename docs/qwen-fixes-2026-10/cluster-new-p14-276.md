ALREADY-FIXED

# Cluster new-p14-276: infinite decode limit reaches an out-of-range integer conversion

Finding (Luna patrol, BUG 12): `decodeLimited` rejects nonpositive limits but accepts `+infinity`; `boundedSF` then converts `ceil(maxSeconds * sampleRate)` to `int64_t` unchecked.

The defect was real before commit `0b1e4132f1` ("cluster new-p14-276: a decode limit that is not a frame count is refused"). That commit is an ancestor of this branch's HEAD (`git merge-base --is-ancestor 0b1e4132f1 HEAD`, EXIT=0), so the fix is already on the line. Nothing in this lane changes product code.

## Evidence (current tree, HEAD `5c1c6715d9`)

| Point | Location | What it does |
| --- | --- | --- |
| Limit guard | `src/core/SampleDecoder.cpp:409` | `decodeLimited` refuses a non-finite window before any decoder runs: `if (!(maxSeconds > 0.0) \|\| !std::isfinite(maxSeconds)) { return std::nullopt; }`. `+infinity` never reaches the decoders. |
| Single checked conversion | `src/core/SampleDecoder.cpp:258-265` | `boundedFrameCount` refuses a non-finite limit or sample rate, then checks `ceil(maxSeconds * sampleRate)` for finiteness and against `MaxBoundedFrames` (2^32, `:253`) before `static_cast<std::int64_t>`. |
| Callers of the conversion | `:277` (boundedSF), `:329` (boundedOggVorbis), `:418` (DrumSynth path) | Each handles the `nullopt` case. |
| Regression tests | `tests/src/core/SampleDecoderLimitTest.cpp:108` `infiniteLimitIsRefused`; `:91` `limitBeyondTheSupportedMaximumIsRefused` (1e12 s) | Both assert the window is refused and not converted out of range. |

Reachability: the only production caller is `src/core/BrowserFeatures.cpp:123`, which passes `BrowserFeatures::FeatureSeconds` (`include/BrowserIndex.h:49`, constant 20.0). Infinity is not reachable from the app today. The guard covers any future or external caller.

The finding's `~397` line is stale; the code it describes is now at `SampleDecoder.cpp:402-426`.

## RED / GREEN proof

The fix commit's patch no longer reverse-applies (`git apply -R` EXIT=1, "patch does not apply" at `SampleDecoder.cpp:317`), because a later commit `bd324436ed` (cluster new-p14-271, Ogg guard) edited the same file. The revert was therefore done by hand: the finiteness and range checks in `boundedFrameCount` and the `isfinite` check in `decodeLimited` were removed. HEAD's file was saved to `logs/SampleDecoder.cpp.head` first, and restored from it afterwards; `git status` and `git diff HEAD -- src/core/SampleDecoder.cpp` were clean after the restore.

With the fix reverted, `ctest -R '^SampleDecoderLimitTest$'` failed:

```
FAIL!  : SampleDecoderLimitTest::limitBeyondTheSupportedMaximumIsRefused() '!bounded.has_value()' returned FALSE. ()
FAIL!  : SampleDecoderLimitTest::infiniteLimitIsRefused() the infinite limit was converted out of range: cannot create std::vector larger than max_size()
```

With HEAD restored, it passes.

## Commands and EXIT codes

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-276.r1/logs/`.

| # | Command (run from the worktree unless noted) | EXIT | Log |
| --- | --- | --- | --- |
| 1 | `cmake --build build --target zene SampleDecoderLimitTest -j4` | 2 | `build-1-head-green.log` (ccache could not create its temp file under the shared `dsh` path; not a code error) |
| 2 | `CCACHE_TEMPDIR=$TMPDIR cmake --build build --target zene SampleDecoderLimitTest -j4` | 0 | `build-2-head-green.log` |
| 3 | `CCACHE_TEMPDIR=$TMPDIR cmake --build build --target SampleDecoderOggStateTest SampleDecoderOggChannelTest -j4` | 0 | `build-3-family.log` |
| 4 | `cd build/tests && ctest -R '^SampleDecoderLimitTest$' --output-on-failure` (GREEN baseline) | 0 | `test-1-head-green.log`: 1/1 passed |
| 5 | `cd build/tests && ctest -R '^SampleDecoder'` (family, GREEN baseline) | 0 | `test-2-family-green.log`: 3/3 passed |
| 6 | `git apply -R <fix patch>` | 1 | patch does not apply (later commit `bd324436ed`); revert done by hand |
| 7 | `cmake --build build --target zene SampleDecoderLimitTest -j4` (fix reverted) | 0 | `build-4-red.log` |
| 8 | `cd build/tests && ctest -R '^SampleDecoderLimitTest$' --output-on-failure` (RED) | 8 | `test-3-red.log`: 0/1 passed, both assertions above |
| 9 | restore HEAD file, `cmake --build build --target zene SampleDecoderLimitTest -j4` | 0 | `build-5-green-restored.log` |
| 10 | `cd build/tests && ctest -R '^SampleDecoderLimitTest$' --output-on-failure` (GREEN) | 0 | `test-4-green-restored.log`: 1/1 passed |
| 11 | `ctest -R '^SampleDecoder'` run from `build/` by mistake | 97 | `test-5-family-green.log`: the guard refused it ("run ctest from <lane>/build/tests"); no tests ran. Not evidence. |
| 12 | `cd build/tests && ctest -R '^SampleDecoder'` (family, GREEN) | 0 | `test-6-family-green.log`: 3/3 passed |
| 13 | `python3 .../qwen/feature_check.py <worktree>` | 1 | `gates-1.log`: traceback, `PermissionError` creating `<worktree>-feature-check` beside the worktree; the gate did not run |
| 14 | `fork-sources-gate.sh`, `all-sources-reproduce.sh`, `no-upstream-regression-gate.sh`, `tests/scripted/check-namespace`, `no-tautology-gate.sh`, `doc-figures-gate.py` (same commands and cwd as `feature_check.py`, logs redirected) | 0, 0, 0, 0, 0, 0 | `gate-*.log` |
| 15 | `bash tests/complexity-gate.sh --check` (raw) | 1 | `gate-complexity.log`: 6 REGRESSION lines, none in a file this branch changed (see below) |
| 16 | `bash tests/file-length-gate.sh --check` (raw) | 1 | `gate-file-length.log`: 22 REGRESSION lines, none in a file this branch changed |

Gate notes. `feature_check.py` could not run because it writes its logs to `<worktree>-feature-check`, a sibling directory outside the writable area. I ran its six simple gates myself with the same commands and cwd; all exited 0. For the two ratchet gates, the wrapper counts a REGRESSION line only when its file is in `git diff --name-only <base>..HEAD`. Here `base = merge-base(HEAD, 050/m0-train-19) = HEAD`, so that set is empty and no line is attributable to this lane. The raw exit codes are 1 because the train is red on those gates by design; `SampleDecoder.cpp` appears in none of their lines.

## Files committed

`docs/qwen-fixes-2026-10/cluster-new-p14-276.md` only. No product or test file changed; the regression test and the fix already exist on this branch.

## Residual note (not part of this finding)

The 2^32-frame cap (`SampleDecoder.cpp:253`) still allows a large buffer for a long real file with a large finite window. Production passes only the 20 s constant, so this is not the reported defect and is not changed here.

## Self-check

Additional command run for this section: `CCACHE_TEMPDIR=$TMPDIR cmake --build build --target BrowserFeaturesWindowTest -j4` (EXIT=0, `logs/build-6-browser-window.log`), then `cd build/tests && ctest -R '^BrowserFeaturesWindowTest$' --output-on-failure` (EXIT=0, 1/1 passed, `logs/test-7-browser-window.log`). This is the production-window test, which exercises `BrowserFeatures.cpp:123`.

- **Regression FAILS with the fix reverted and PASSES with it.** RED: `test-3-red.log`, `ctest` EXIT=8, 0/1 passed, both assertions quoted above. GREEN: `test-4-green-restored.log`, EXIT=0, 1/1 passed. Family GREEN: `test-6-family-green.log`, EXIT=0, 3/3 passed.
- **Deterministic.** The test writes a fixed tone file (`writeTone(file, Wav, Rate)`) and passes fixed limit values: `0.5`, `1.0e12`, and `std::numeric_limits<double>::infinity()`. It has no sleeps, no wall-clock timing and no threads. The `decodeLimited` path is synchronous.
- **Fix at the cause, and complete.** There is no new product change in this lane; the check is the one at `SampleDecoder.cpp:409` and `:258-265`. Every caller of the window-to-frame conversion uses `boundedFrameCount`: `:277`, `:329`, `:418`. Every caller of `decodeLimited` is listed by `rg`: `src/core/BrowserFeatures.cpp:123` (constant `FeatureSeconds`, 20.0), `tests/src/core/BrowserFeaturesWindowTest.cpp:103,122,123,153,177`, and `tests/src/core/SampleDecoderLimitTest.cpp:82,98,115`. The sibling `src/core/ImportDetectionDsp.cpp:327-330` has its own NaN guard and does not call `decodeLimited`; it is untouched.
- **No collateral damage.** The 2^32-frame cap (`SampleDecoder.cpp:253`) is far above any legitimate window: 20 s at 192 kHz is 3.84 M frames. The guard refuses only non-finite limits and windows past the cap, which no in-tree caller passes. `decode()` (the full decode, used by project loading) is not changed by this fix, so old projects and saved data are unaffected. `BrowserFeaturesWindowTest` passes (see above).
- **Verdict evidence.** ALREADY-FIXED is supported by `SampleDecoder.cpp:409` (non-finite window refused before any decoder), `SampleDecoder.cpp:258-265` (checked conversion), and commit `0b1e4132f1`, an ancestor of HEAD (`git merge-base --is-ancestor`, EXIT=0).

**Gates: not passed in this lane.** `feature_check.py` exited 1 (`gates-1.log`): it creates `<worktree>-feature-check` beside the worktree, which is outside this lane's writable area, so the gate could not run. The six simple gates were run by hand with the same commands and cwd and all exited 0 (`gate-*.log`). The two ratchet gates exit 1 raw on the train by design, and no REGRESSION line is attributable to this lane because its changed set against the base is empty. The gate runner needs a writable log location before a clean `feature_check.py` PASS can be recorded here.
