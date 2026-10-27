Verdict: ALREADY-FIXED

new-p12-148: Non-finite surround settings from project XML can reach audio (`src/core/MixerSurround.cpp`, finding line 226 is stale)

Evidence (branch `bugs/qwen-new-p12-148`, HEAD `639e1d23e`):

- The parse helper that the finding describes is `attribute()` at [MixerSurround.cpp:284](src/core/MixerSurround.cpp:284)-[290](src/core/MixerSurround.cpp:290). It now rejects non-finite values before clamping: `return ok && std::isfinite(value) ? std::clamp(value, low, high) : fallback;` (line 290). A parsed `"nan"` or `"inf"`, or an overflowed literal such as `"1e999"` (where `toFloat` sets `ok == false`), falls back to the default instead of being clamped.
- The finding's line 226 is now `discreteMatrix()`'s `std::min(channelCount(from), channelCount(to))` ([MixerSurround.cpp:226](src/core/MixerSurround.cpp:226)). It reads no parsed float, so it is not the NaN source.
- The fix is already in history: commit `3a8724211` ("Fix cluster new-p10-257-port-port-port-port: reject non-finite mixer XML floats"). `git merge-base --is-ancestor 3a8724211 HEAD` returned ancestor=yes.
- Every XML reader of these settings goes through `attribute()`:
  - Downmix: `loadMixer()` reads `centre`, `surround` and `lfe` at [MixerSurround.cpp:445](src/core/MixerSurround.cpp:445)-[451](src/core/MixerSurround.cpp:451) and onward.
  - Pan: `ChannelSurround::load()` reads `x`, `y`, `lfe` and `spread` at [MixerSurround.cpp:513](src/core/MixerSurround.cpp:513) and [523](src/core/MixerSurround.cpp:523)-[526](src/core/MixerSurround.cpp:526).
- Sibling path, the JSON control command `mixer.downmix_options`: `readDb()` ([ControlCommandsMixerSurround.cpp:235](src/core/ControlCommandsMixerSurround.cpp:235)) uses `inRange()` ([ControlCommandsMixerSurround.cpp:168](src/core/ControlCommandsMixerSurround.cpp:168)-[170](src/core/ControlCommandsMixerSurround.cpp:170)), which is `value >= low && value <= high`. That is false for NaN, so NaN is rejected there too. JSON cannot carry a NaN literal in any case.
- Regression: `ProjectStateTest::nonFiniteDownmixSettingsAreIgnored()` ([tests/src/core/ProjectStateTest.cpp:279](tests/src/core/ProjectStateTest.cpp:279)-[297](tests/src/core/ProjectStateTest.cpp:297)) loads `<surrounddownmix centre="nan" surround="1e999">`, then checks that the options fall back to the defaults and that the 5.1-to-stereo matrix entries are finite. It is the finding's exact reproduction.

RED/GREEN proof (the code already has the fix, so RED was produced by a temporary mutation, then reverted):

| Step | Command (run from the worktree / `build/tests`) | Result |
| --- | --- | --- |
| Build attempt 1 | `cmake --build build --target ProjectStateTest -j4 > .../build-1.log 2>&1; echo EXIT=$?` | EXIT=2. ccache could not create a temp file under `dsh/home/ccache-tmp-r6` (permission denied). No product change; an environment issue. |
| Build attempt 2 | `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp cmake --build build --target ProjectStateTest -j4 > .../build-2.log 2>&1; echo EXIT=$?` | EXIT=0 |
| Build family | `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp cmake --build build --target SurroundTest SurroundPannerTest ExportWavWriteErrorTest -j4 > .../build-3.log 2>&1; echo EXIT=$?` | EXIT=0 |
| GREEN on current tree | `ctest -R '^(ProjectStateTest\|SurroundPannerTest\|ExportWavWriteErrorTest\|SurroundTest)$' --output-on-failure > .../test-1.log 2>&1; echo EXIT=$?` | EXIT=0. `100% tests passed, 0 tests failed out of 4` (ProjectStateTest, SurroundPannerTest, ExportWavWriteErrorTest, SurroundTest) |
| Mutation (RED) | Temporarily changed `MixerSurround.cpp:290` to `return ok ? std::clamp(value, low, high) : fallback;` (removed only `std::isfinite`) | `git status` showed the change; reverted below |
| Build with mutation | `CCACHE_TEMPDIR=... cmake --build build --target ProjectStateTest -j4 > .../build-4-red-mutant.log 2>&1; echo EXIT=$?` | EXIT=0 |
| RED run | `ctest -R '^ProjectStateTest$' --output-on-failure > .../test-2-red.log 2>&1; echo EXIT=$?` | EXIT=8. `FAIL! : ProjectStateTest::nonFiniteDownmixSettingsAreIgnored()`, `Actual (surround::downmixOptions().centre): nan`, `Expected ...: 0.7071068`, `Loc: [ProjectStateTest.cpp(292)]`. Other test in that binary passed. |
| Revert | Restored `MixerSurround.cpp:290` to `return ok && std::isfinite(value) ? std::clamp(value, low, high) : fallback;` | `git status --short` was empty before the rebuild |
| Build after revert | `CCACHE_TEMPDIR=... cmake --build build --target ProjectStateTest SurroundTest SurroundPannerTest ExportWavWriteErrorTest -j4 > .../build-5-green.log 2>&1; echo EXIT=$?` | EXIT=0 |
| GREEN after revert | same ctest family command, `> .../test-3-green.log` | EXIT=0, 4/4 passed |

Gates:

| Gate run | Command | Result |
| --- | --- | --- |
| Gate as written | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > .../gate-1-feature-check.log 2>&1; echo EXIT=$?` | EXIT=1, `PermissionError` at `logs.mkdir()`. The script writes its logs to `<worktree>/../zene-qwen-new-p12-148-feature-check`, outside the write-jail. No gate row ran. |
| Gate, log location redirected | Copy of the script at `.../logs/gate-copy/feature_check_logdir.py`, created with `sed` so that only `ROOT` (kept pointing at the real `qwen/` directory) and the log directory changed (`.../logs/feature-check`). Run as `python3 -I .../feature_check_logdir.py <worktree> > .../gate-2-feature-check.log 2>&1; echo EXIT=$?` | EXIT=0. `RESULT: PASS`. 8 of 8 rows pass: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity ("no new item in your files"), file-length ("no new item in your files"). |

Files committed: `docs/qwen-fixes-2026-10/cluster-new-p12-148.md` only. There is no product change, because the fix is already on the branch (`3a8724211`).

Notes:
- Not an issue for this cluster: the JSON control path (`mixer.downmix_options`) and `inRange()` already reject NaN (see above).
- Environment: the ccache temp directory was not writable from the build, so builds used `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp` for the command only. The gate's log folder had to be redirected (see above). Neither changed a tracked file.

## Self-check

- **Regression fails with the fix reverted and passes with it.** Yes. With the `std::isfinite` guard removed at `src/core/MixerSurround.cpp:290`, `ctest -R '^ProjectStateTest$'` exited 8 (`test-2-red.log`: `FAIL! ... nonFiniteDownmixSettingsAreIgnored()`, `Actual ...centre: nan`). With the guard restored, the same family passed 4/4 (`test-3-green.log`, EXIT=0). The product change being tested is the guard that already sits on the branch in `3a8724211`.
- **Deterministic.** Yes. The test builds an in-memory `QDomDocument` with fixed attribute strings (`centre="nan"`, `surround="1e999"`) and calls `surround::loadMixer()` directly. It uses no clock, sleep or thread.
- **Fix at the cause, every caller and sibling checked.** Yes. `attribute()` (`MixerSurround.cpp:284-290`) is the single parser for the surround float attributes: `loadMixer()` reads `centre`, `surround`, `lfe` (`:445-451`), and `ChannelSurround::load()` reads `x`, `y`, `lfe`, `spread` (`:513-526`). The JSON control path, `mixer.downmix_options`, uses `readDb()`/`inRange()` (`ControlCommandsMixerSurround.cpp:168-170, 235-245`), which rejects NaN because `NaN >= low` is false.
- **No collateral damage.** Yes, no change in this commit. Valid values take the same path as before (clamp to the same range); only non-finite inputs fall back. SurroundTest, SurroundPannerTest, ExportWavWriteErrorTest and ProjectStateTest all pass (`test-1.log`, `test-3-green.log`).
- **Verdict ALREADY-FIXED, with the code that shows it.** `src/core/MixerSurround.cpp:290` (guard), `3a8724211` is an ancestor of HEAD (`git merge-base --is-ancestor 3a8724211 HEAD`, ancestor=yes), `tests/src/core/ProjectStateTest.cpp:279-297` (regression).

Gate row `gates (feature_check.py)` in `submit_check.py` shows FAIL. The cause is environmental: the unmodified `feature_check.py` tries to create `<worktree>/../zene-qwen-new-p12-148-feature-check`, which is outside the write-jail, and fails with `PermissionError` (`gate-1-feature-check.log`). The same gate, with only the log directory redirected into the run folder, exits 0 with 8 of 8 rows PASS on the committed tree (`gate-3-final-tree.log`). This needs the owner to allow the sibling log directory or to run the gate with a writable log location.
