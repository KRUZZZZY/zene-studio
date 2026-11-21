# FIXED — cluster new-p14-353: rejected frozen take leaves its source clips muted

**Verdict: FIXED** — a project whose frozen take has an unparsable `frozenStart` or `frozenEnd` is still rejected, and now the clips its freeze muted are unmuted when they load. The regression fails on the original code and passes with the fix. The frozen-take family is green except `ControlFreezeCommandsTranscript`, which fails identically on the original code (see the A/B below).

## Evidence

- **Root cause.** `Track::loadTrack()` calls `clearFrozenTake()` then `loadFrozenTake(element)` at [`src/core/Track.cpp:769`](../../src/core/Track.cpp). `loadFrozenTake()` rejects a take whose `frozenStart` or `frozenEnd` does not parse and sets `m_frozen = FrozenTake{}` at [`src/core/Track.cpp:1348`](../../src/core/Track.cpp), so the `frozenMuted` record is discarded before anything reads it. The clips are not in `m_clips` yet at that point: they are created later in `loadTrack()`'s child loop, so unmuting inside `loadFrozenTake()` would find nothing. The only existing unmute recovery, [`src/core/Track.cpp:890`](../../src/core/Track.cpp), runs only when `m_frozen.isFrozen()` still holds, which a rejected take never does. The clips therefore keep the `muted="1"` the freeze saved.
- **Fix.** In `loadTrack()`, after the clip loop, a take that was rejected (`frozenAudio` present, `!m_frozen.isFrozen()`) has its `frozenMuted` attribute parsed and the named clips unmuted: [`src/core/Track.cpp:893-900`](../../src/core/Track.cpp). The unmute body moved into the file-local helper `unmuteFrozenClips()` ([`src/core/Track.cpp:246`](../../src/core/Track.cpp)), which the existing recovery at line 890 now also calls, so both paths share one loop. The rejection branch in `loadFrozenTake()` gains only a comment ([`src/core/Track.cpp:1347`](../../src/core/Track.cpp)).
- **Why it is read from the attribute, not from a record kept across the rejection.** `Track.h` is untouched. Returning the record from `loadFrozenTake()` would change a private declaration in a header included across the whole tree, which would make the step 4b patch toggle rebuild everything. The attribute is still on the element when the clips load, so reading it there is equivalent to the finding's "parse before rejecting" and lets the change stay in `Track.cpp`.
- **Scope kept.** The block runs only on the full-load path. `presetMode` returns at [`src/core/Track.cpp:776`](../../src/core/Track.cpp) before it, so a preset load never touches existing clips. A take with a valid file or a missing file is unaffected: the rejection condition requires `!m_frozen.isFrozen()`, and the existing 890 block is unchanged in behaviour.
- **Regression test.** `FrozenTakeRoundTwoTest::rejectedFrozenTakeReleasesItsMutedClips` at [`tests/src/core/FrozenTakeRoundTwoTest.cpp:402`](../../tests/src/core/FrozenTakeRoundTwoTest.cpp). It freezes a track with its one clip muted by the take, saves the project, rewrites `frozenStart` to `not-a-tick`, reloads, and requires `!clip->isMuted()`. Deterministic: no timing or sleeps.
- **Registration.** `tests/upstream-modifications.txt` gains two lines (`src/core/Track.cpp`, `tests/src/core/FrozenTakeRoundTwoTest.cpp`) with reasons. `FrozenTakeRoundTwoTest.cpp` is already in `tests/fork-sources.txt`.

## Commands and exit codes

Run from the worktree root unless shown. `LOGS` = `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-353.r1/logs`.

**Environment note.** The first build (`build-1.log`, EXIT=2) failed before any compile: ccache could not create a temp file in `/run/user/1000/ccache-tmp` (Permission denied). All later builds set `CCACHE_TEMPDIR=/tmp/ccache-tmp-p14-353` to route ccache's temp files to `/tmp`. No code change was involved.

| Step | Exact command | Exit | Log / result |
|---|---|---:|---|
| RED build (test only, original code) | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p14-353 cmake --build build --target zene FrozenTakeRoundTwoTest -j4 > LOGS/build-2.log 2>&1; echo EXIT=$?` | 0 | build-2.log |
| **RED** test | `cd build/tests && ctest -R '^FrozenTakeRoundTwoTest$' --output-on-failure > LOGS/test-1.log 2>&1; echo EXIT=$?` | **8** | `FAIL! : FrozenTakeRoundTwoTest::rejectedFrozenTakeReleasesItsMutedClips() '!loadedClips.front()->isMuted()' returned FALSE`; Totals 9 passed, 1 failed. 0 of 1 family tests passed. |
| GREEN build (with fix) | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p14-353 cmake --build build --target zene FrozenTakeRoundTwoTest -j4 > LOGS/build-3.log 2>&1; echo EXIT=$?` | 0 | build-3.log |
| **GREEN** test | `cd build/tests && ctest -R '^FrozenTakeRoundTwoTest$' --output-on-failure > LOGS/test-2.log 2>&1; echo EXIT=$?` | **0** | `100% tests passed, 0 tests failed out of 1` |
| 4b: save product diff | `git diff -- src/core/Track.cpp > LOGS/fix.patch; echo EXIT=$?` | 0 | fix.patch |
| 4b: reverse fix | `git apply -R LOGS/fix.patch; echo EXIT=$?` | 0 | |
| 4b: rebuild without fix | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p14-353 cmake --build build --target zene FrozenTakeRoundTwoTest -j4 > LOGS/build-4.log 2>&1; echo EXIT=$?` | 0 | build-4.log |
| 4b: **must FAIL** without fix | `cd build/tests && ctest -R '^FrozenTakeRoundTwoTest$' --output-on-failure > LOGS/test-3.log 2>&1; echo EXIT=$?` | **8** | Same assertion as RED; Totals 9 passed, 1 failed |
| 4b: re-apply fix | `git apply LOGS/fix.patch; echo EXIT=$?` | 0 | |
| 4b: rebuild with fix | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p14-353 cmake --build build --target zene FrozenTakeRoundTwoTest -j4 > LOGS/build-5.log 2>&1; echo EXIT=$?` | 0 | build-5.log |
| 4b: **must PASS** with fix | `cd build/tests && ctest -R '^FrozenTakeRoundTwoTest$' --output-on-failure > LOGS/test-4.log 2>&1; echo EXIT=$?` | **0** | 1/1 passed |
| Family build | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p14-353 cmake --build build --target zene FrozenTakeHandoffTest FrozenTakeRoundTwoTest BounceInPlaceTest ControlBounceSupportTest ControlBounceRenderTest -j4 > LOGS/build-6.log 2>&1; echo EXIT=$?` | 0 | build-6.log |
| Family run (first) | `cd build/tests && ctest -R '^(FrozenTakeHandoffTest\|FrozenTakeRoundTwoTest\|BounceInPlaceTest\|ControlBounceSupportTest\|ControlBounceRenderTest\|ControlFreezeCommandsTranscript)$' --output-on-failure > LOGS/test-5.log 2>&1; echo EXIT=$?` | 8 | **6 tests executed**: 5 passed; `ControlFreezeCommandsTranscript` Failed (see A/B) |
| A/B: original `Track.cpp` (reverse, rebuild, run transcript test, re-apply, rebuild) | `git apply -R LOGS/fix.patch` (0) → `cmake --build build --target zene -j4 > LOGS/build-7.log 2>&1` (0) → `ctest -R '^ControlFreezeCommandsTranscript$' --output-on-failure > LOGS/test-6.log 2>&1` (**8**) → `git apply LOGS/fix.patch` (0) → `cmake --build build --target zene -j4 > LOGS/build-8.log 2>&1` (0) | 8 | Same Failed result on the original code. Log shows `PASS: no audible instrument in this build: Skipped, never Passed`. |
| Family run (after restore) | `ctest -R '^(…same six…)$' --output-on-failure > LOGS/test-7.log 2>&1` (after `build-9.log` EXIT=0) | 8 | 5 of 6 pass; `ControlFreezeCommandsTranscript` fails as on original code |
| Gates (`feature_check.py`) | see note below | **0** | `RESULT: PASS`, 8 rows PASS |
| Final check on the committed source (after a comment-only edit) | `cmake --build build --target zene FrozenTakeRoundTwoTest -j4` (`build-10.log`, EXIT 0); `ctest -R '^FrozenTakeRoundTwoTest$'` (`test-8.log`, EXIT 0); gates again (`gates-3.log`, EXIT 0) | 0 | GREEN unchanged |

**Pre-existing failure, not fixed here.** `ControlFreezeCommandsTranscript` exits 77 when the build has no audible instrument (`tests/control-freeze-commands-transcript.py:488-489`: "Skipped, never Passed"). Its registration at [`tests/CMakeLists.txt:4557-4562`](../../tests/CMakeLists.txt) sets no `SKIP_RETURN_CODE 77`, so ctest records the 77 as Failed. The A/B above shows the same failure with the original `Track.cpp`. Changing it would mean editing a test registration, which this bug fix does not do.

**Gate note.** `feature_check.py` as shipped writes its logs to `<worktree>/../<name>-feature-check`, which is outside the write-jail. The first run (`LOGS/gates-1.log`) failed with `PermissionError` at that path (EXIT=1, before any gate ran). The gate was therefore run from a copy at `/tmp/fc-p14-353/feature_check_logdir.py` that differs in two lines only: `ROOT` is pinned to the shared qwen directory (so `FIX-BASE` and the `gate-baselines` cache are the shared ones) and `logs` points into `LOGS/gates-feature-check`. Gate logic, limits and baselines are unchanged. Command: `python3 -I /tmp/fc-p14-353/feature_check_logdir.py <worktree> > LOGS/gates-2.log 2>&1; echo EXIT=$?` → 0. Base: `b61e5f9658c2f55b74ee8137de37292ebe2cc8a0` (cached). Table:

```
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

## Files committed

- `src/core/Track.cpp`
- `tests/src/core/FrozenTakeRoundTwoTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p14-353.md`

## Evidence table

| Item | Result |
|---|---|
| RED command / exit | `ctest -R '^FrozenTakeRoundTwoTest$'` / **8** (test-1.log) |
| GREEN command / exit | `ctest -R '^FrozenTakeRoundTwoTest$'` / **0** (test-2.log) |
| 4b without fix / with fix | 8 (test-3.log) / 0 (test-4.log) |
| Affected family tests executed | **6** (5 pass; `ControlFreezeCommandsTranscript` fails pre-existing, EXIT 8 on original code too) |
| Gate exit | **0** (`RESULT: PASS`) |
| Build exits | all 0 after the first ccache-permission failure (build-1.log, EXIT=2, not a code error) |

## Self-check

Verdict is FIXED. Nothing is claimed as ALREADY-FIXED or FALSE-POSITIVE.

1. **Regression fails with the product change reverted and passes with it.** RED: `test-1.log`, EXIT 8, `'!loadedClips.front()->isMuted()' returned FALSE`. GREEN: `test-2.log`, EXIT 0. Patch toggle (step 4b): reverse `fix.patch` (EXIT 0), rebuild (`build-4.log`, EXIT 0), run `test-3.log` EXIT 8; re-apply (EXIT 0), rebuild (`build-5.log`, EXIT 0), run `test-4.log` EXIT 0.
2. **Deterministic.** The test runs synchronously on the control thread. It has no sleeps, timers, threads or wall-clock reads. Its inputs are fixed: a 440 Hz tone from `taketest::writeToneWav`, one clip whose start and length are read back from the model, a project saved with `saveProjectFile`, one attribute rewritten in the XML, then `loadProject`. No injection seam is needed.
3. **Fix at the cause, siblings checked.**
   - The only caller of `loadFrozenTake()` is `loadTrack()` (`src/core/Track.cpp:769`; the grep over `src/` shows no other call).
   - The existing recovery at `src/core/Track.cpp:888-891` now calls the same helper, `unmuteFrozenClips()` (`src/core/Track.cpp:246`), so the two unmute paths cannot diverge.
   - `unfreeze()` (`src/core/Track.cpp:~1282-1300`), `freezeTo()` (`~1235-1279`) and `clearFrozenTake()` are unchanged.
   - The `presetMode` return (`src/core/Track.cpp:776`) comes before the new block, so a preset load never touches existing clips.
   - `include/Track.h` is unchanged, so no other translation unit needed a rebuild.
   - `WriteRefusalGateTest`, which lists `clearFrozenTake,loadFrozenTake` as the writers of the frozen record, passes: `build-11.log` EXIT 0, `test-9.log` EXIT 0, 1 of 1.
4. **No collateral damage.**
   - The new block runs only when `frozenAudio` names a file and no take was installed, which is exactly the tick-rejection case. Projects with no frozen take are unaffected.
   - A take that was installed, including one whose file is missing or undecodable, takes the existing path, which is unchanged.
   - No limit or check was added.
   - Known edge, same identity rule as the existing code: `frozenMuted` matches clips by `(start, length)`. A clip the user muted at exactly the position and length of a rejected take's record would also be unmuted on load. Unfreeze and the existing recovery already match on the same key, and changing that key would be a separate behaviour change.
5. **Gate row in `submit_check` (FAIL) is an environment limit, not a gate result.** `submit_check.py` runs the shared `feature_check.py` on the worktree. That script writes its logs to `<worktree>/../<name>-feature-check`, which is outside this write-jail, so it fails on `mkdir` (`gates-1.log`, `PermissionError`, EXIT 1). The same gate code, with only the log directory relocated, passes: `gates-2.log` and `gates-3.log`, `RESULT: PASS`, EXIT 0. The gate itself is not edited. Whoever runs the loop outside the jail should see this row pass; if it does not, `gates-2.log` is the evidence to check.
