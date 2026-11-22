FIXED

# Cluster new-p14-362 - resampler errors reported as successful writes

Finding: `src/core/VideoAudioExtract.cpp` (historical line 130). `swr_convert()` returns a negative AVERROR on failure, but
`if (got <= 0) { return true; }` treated it as success. Once earlier frames had been written, extraction went on and
committed a truncated WAV.

## Evidence (file:line, this branch)

Before the fix (`HEAD` = `cf3618e61`):
- `src/core/VideoAudioExtract.cpp` `WavSink::write`: `if (got <= 0) { return true; }` (negative result = success), and the
  same `room <= 0` test for `swr_get_out_samples`, which also returns a negative value on error (`swresample.h:312`,
  `swresample.h:487`).
- Only one swresample conversion site exists in the tree: `rg -n "swr_convert|swr_get_out_samples" src include plugins`
  returns `src/core/VideoAudioExtract.cpp:138` and `:143` and nothing else.
- Callers of `WavSink::write`: the decode loop and the final flush in `decodeAll` (`VideoAudioExtract.cpp`, `decodeAll`).
  Both stop when `write` returns false, and `decodeAll` then reports `whole = false`.

After the fix:
- `src/core/VideoAudioExtract.cpp:64` new flag `convertFailed`.
- `:125-131` `failConversion(int code)`: records the error text through `av_strerror`, sets `convertFailed`, returns false.
- `:137` both failure flags stop further writes. The flush after a failed decode is also refused.
- `:139-140` negative `swr_get_out_samples` is a failure. Zero means nothing to write and is still success.
- `:144-145` negative `swr_convert` is a failure. Zero means a successful conversion with no frames and is still success.
- `:205` `extract()` reports `the video's audio could not be converted (...)` rather than "cannot write", so the WAV is
  never committed (`commit()` is not reached).

Behaviour change: only negative FFmpeg results change. Those used to yield a silently truncated WAV, so no legitimate
caller depended on them. No limit or check was added that other callers could hit.

## Regression test

`tests/src/core/VideoTrackTest.cpp`, new slot `aResamplerErrorFailsTheExtractionAndPublishesNoWav` (around line 759).
It arms an interposed `swr_convert` (lines 172-180 and 228-238, same `ZENE_HAVE_VIDEO_ALLOC_FAILURE_HOOKS` pattern as the
existing allocation hooks) so the second conversion returns `AVERROR_INVALIDDATA`. The first conversion runs for real and
writes audio, so the failure comes after audio was already converted. The test asserts that exactly one failure was
injected, that extraction fails with a non-empty error, and that no WAV exists at the destination.

## Commands and exit codes

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-362.r1/logs`. Build commands used
`CCACHE_DISABLE=1` because ccache could not create its temp file inside the sandbox (`build-1.log`, EXIT=2, ccache
"Permission denied"). Test binaries were built from the worktree build tree (`build/`).

| Step | Command (run from the worktree, or `build/tests` for ctest) | EXIT | Log |
|---|---|---|---|
| build (ccache, failed) | `cmake --build build --target VideoTrackTest -j4` | 2 | build-1.log |
| build (compile error in my hook signature) | `CCACHE_DISABLE=1 cmake --build build --target VideoTrackTest -j4` | 2 | build-2.log |
| build (test only, before the product fix) | `CCACHE_DISABLE=1 cmake --build build --target VideoTrackTest -j4` | 0 | build-3.log |
| **RED** (product unfixed, test present) | `ctest -R '^VideoTrackTest$' --output-on-failure` (in `build/tests`) | **8** | test-1-red.log |
| GREEN (product fixed) | `CCACHE_DISABLE=1 cmake --build build --target zene VideoTrackTest -j4` then ctest | 0 / **0** | build-4-green.log, test-2-green.log |
| direct QTest run of the same binary | `./VideoTrackTest -o /dev/stdout,txt` (in `build/tests`) | 0 (grep) | direct-green.log |
| 4b: save product diff | `git diff -- src/core/VideoAudioExtract.cpp > fix.patch` | 0 | fix.patch |
| 4b: revert product fix | `git apply -R fix.patch` | 0 | |
| 4b: rebuild unfixed | `CCACHE_DISABLE=1 cmake --build build --target zene VideoTrackTest -j4` | 0 | build-5-unfixed.log |
| **4b RED** (unfixed) | ctest `^VideoTrackTest$` | **8** | test-3-unfixed-red.log |
| 4b: re-apply fix | `git apply fix.patch` | 0 | |
| 4b: rebuild fixed | `CCACHE_DISABLE=1 cmake --build build --target zene VideoTrackTest -j4` | 0 | build-6-refixed.log |
| **4b GREEN** (fixed again) | ctest `^VideoTrackTest$` | **0** | test-4-fixed-green.log |
| gates: feature_check.py | `python3 .../qwen/feature_check.py <worktree>` | 1 | gates-1.log |
| gate 1 fork-sources | `bash tests/fork-sources-gate.sh` | 0 | gate-1-fork-sources.log |
| gate 2 all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 | gate-2-all-sources.log |
| gate 3 no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 | gate-3-no-upstream.log |
| gate 4 check-namespace | `tests/scripted/check-namespace` | 0 | gate-4-check-namespace.log |
| gate 5 no-tautology | `bash tests/no-tautology-gate.sh` | 0 | gate-5-no-tautology.log |
| gate 6 doc-figures | `python3 tests/doc-figures-gate.py` | 0 | gate-6-doc-figures.log |
| ratchet (raw) complexity | `bash tests/complexity-gate.sh --check` | 1 | gate-7-complexity.log |
| ratchet (raw) file-length | `bash tests/file-length-gate.sh --check` | 1 | gate-8-file-length.log |
| completion check | `python3 .../qwen/submit_check.py <worktree> new-p14-362` | 1 | submit-1.log (see Self-check) |

Family count: the ctest run for `^VideoTrackTest$` executed one test (`100% tests passed out of 1`, test-2-green.log). Inside
it, QTest reports `Totals: 25 passed, 0 failed, 1 skipped`. The skipped case is `withoutFfmpegEveryCommandIsUnavailable`,
which only runs in builds without FFmpeg and does not apply here. The new case passed (`PASS : ...aResamplerError...`).

Gate notes:
- `feature_check.py` could not run its own log step. It writes logs to `<worktree>/../<name>-feature-check`, i.e.
  `projects/zene-studio-program/zene-qwen-new-p14-362-feature-check`, and that path is outside this session's write
  allowance, so it died at `logs.mkdir` with `PermissionError` (`gates-1.log`). I did not change the script. I ran the same
  commands from its `SIMPLE` and `RATCHET` lists directly, with logs in the LOG FOLDER. All six simple gates exit 0.
- The two ratchets exit 1 in raw form, as the task said they would on the train. `complexity` lists 7 items, none in a
  file this branch changed (none names `VideoAudioExtract.cpp` or `VideoTrackTest.cpp`; the one touched in the list is
  `WasmSandbox.cpp`, which is not in my diff). `file-length` lists one item in a changed file:
  `new file over 500 lines: tests/src/core/VideoTrackTest.cpp (694)`. The key (numbers stripped) is already in the cached
  base listing (`gate-baselines/06c5dcf48260d3e8fe518759ee4104e8305fae6c-file-length.log` line 38), and
  `VideoTrackTest.cpp` was already 771 lines at HEAD. Under the gate's own rule (fail only for items that are new and in a
  changed file) that is not a new item. This is my reading of the gate, not its printed PASS table. The base listing it
  would compute for `cf3618e61` (`merge-base`) was not regenerated here.

## Self-check

Answers to the `submit_check.py` ASK items, with evidence.

- **RED then GREEN with the change reverted and restored.** Yes. RED: `test-1-red.log` (product unfixed) and
  `test-3-unfixed-red.log` (after `git apply -R fix.patch` and rebuild) both show
  `FAIL! : ...aResamplerErrorFailsTheExtractionAndPublishesNoWav() '!extracted' returned FALSE. (a resampler error was
  reported as a successful extraction)` and ctest EXIT=8. GREEN: `test-2-green.log` and `test-4-fixed-green.log`, ctest
  EXIT=0.
- **Deterministic, no timing.** The test has no sleeps, timers or threads. The seam is an interposed `swr_convert` driven
  by an atomic call counter (`swrConvertCallsBeforeFailure`, armed by `ScopedSwrConvertFailure(1)`: first call real,
  second call fails). The test also checks `swrConvertFailuresInjected` moved by exactly 1, so a seam that never fires
  fails instead of passing.
- **Fix at the cause, siblings checked.** Both `swr_get_out_samples` and `swr_convert` now treat negatives as failures,
  because both document a negative value on error. The flush call (`write(nullptr, 0)`) goes through the same guard. The
  grep `rg -n "swr_convert|swr_get_out_samples" src include plugins` finds only the two sites in
  `src/core/VideoAudioExtract.cpp` (lines 138 and 143), so there is no other conversion path to keep consistent. The only
  callers of `write` are the loop and the flush in `decodeAll`, both covered.
- **No collateral damage.** Zero returns keep their old meaning (no frames, success). Only negative FFmpeg results change,
  and those produced a silently truncated WAV before. Existing extraction tests in `VideoTrackTest` (partial write,
  cancelled destination preserved, background extraction, undo, shutdown hook) pass in `test-2-green.log`. No limits were
  added. No saved project data or control command is involved, and no new command was added, so the
  `ControlCommandsSnapshot` canary is unaffected.
- **Verdict FIXED** (not ALREADY-FIXED or FALSE-POSITIVE): the defect reproduces on the unfixed code (RED above) and the
  file:line evidence is listed above.

Realtime: the extraction runs on a decode worker, not the audio thread. The new path only builds an error string on
failure, and the test does not touch the audio thread.

Open items for the reviewer:
- The gate wrapper itself did not run (log-directory permission, see Gate notes). The table above is from running its
  commands directly.
- The error text comes from `av_strerror`, so it is FFmpeg's wording (for example "Invalid data found when processing
  input"), not localised.
