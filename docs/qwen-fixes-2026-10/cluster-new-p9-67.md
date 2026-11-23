FIXED

# cluster new-p9-67 - BUG 9: two extractions running at the same time got the same default WAV

Branch `bugs/qwen-new-p9-67`, base `21c2f3ee2e` (`050/m0-train-49`).

## Verdict and evidence

**FIXED.** The finding is real on this branch, and it was already named as a known residual by the previous cluster in
this area: `docs/qwen-fixes-2026-10/cluster-new-p9-66.md`, self-check item 8 - "Two
`video.extract_audio {background:true}` requests sent before the first finishes both get `<video>-audio.wav`, and the
second overwrites the first's output."

The code path:

- `src/core/ControlCommandsVideo.cpp:329-330` - `extractAudio` picks the destination **when the request arrives**:
  `const QString wav = out.isEmpty() ? defaultAudioPath(video.path) : ...`.
- `src/core/ControlCommandsVideo.cpp:253-258` - `defaultAudioPath` -> `firstFreeAudioPath`; before this fix the only
  "taken" test there was `QFileInfo::exists(candidate)` (the pre-fix search is in `fix.patch`, reverted form).
- `src/core/ControlCommandsVideo.cpp:293-319` - `extractInBackground` hands that path to
  `QThreadPool::globalInstance()->start(...)` (`:307`) and the worker decodes into it later (`:310`).
  The file itself only appears at commit: `WavSink` writes through `QSaveFile`
  (`src/core/VideoAudioExtract.cpp:100`, `:128-139`, `:147-156`), so between the naming and the commit the destination does not
  exist and the existence check cannot see the first request. Both requests therefore get `<video>-audio.wav`, and the
  second `QSaveFile::commit` replaces the first WAV.

Reproduced before any product change: the new test case fails on the unmodified tree
(`test-1-red.log`, EXIT=8) with

```
FAIL!  : twoSearchesBeforeAWavExistsGetDifferentNames() Compared values are not the same
   Actual   (second): ".../clip-audio.wav"
   Expected          : ".../clip-audio-2.wav"
```

Two threads are not needed to show it: the naming happens on the control thread for both requests, before either
worker creates anything.

## Fix (at the cause: the name is claimed when it is handed out)

- `src/core/VideoAudioExtract.cpp:53-88` - a process-wide claim registry: `PathClaimMutex` + `PathsInFlight` (a
  `QSet<QString>` of absolute paths an extraction is writing right now), `claimKey()` (one spelling per path),
  `firstUnclaimedAudioName()` (the old search, now skipping claimed names too) and `releaseClaim()`.
- `src/core/VideoAudioExtract.cpp:305-311` - `firstFreeAudioPath` does the search and the claim under the one mutex,
  so two concurrent searches cannot both see a name free.
- `src/core/VideoAudioExtract.cpp:280-287` and `:289-296` - `extractVideoAudio` gives the claim back when it returns,
  in both the FFmpeg and the no-FFmpeg build. That is the end of the extraction: the WAV is on disk by then, or nothing
  was written (a failed or cancelled run leaves the destination untouched, `QSaveFile` discards its temp file), so the
  name is free again in exactly the cases where no file took it.
- `include/VideoAudioExtract.h:54-65` and `:67-71` - the contract of `extractVideoAudio` and of `firstFreeAudioPath`,
  restated: the returned name is claimed, and the claim ends when that extraction returns.
- `src/core/ControlCommandsVideo.cpp:250-252` - comment on `defaultAudioPath` records the claim. No behaviour change
  in that file; the command's schema and description are untouched (the `ControlCommandsSnapshot` canary does not move).

No new command, no new undo step, and no new call site: `defaultAudioPath` has one caller (`extractAudio:330`), and
both the synchronous path (`:341`) and the worker (`:310`) reach the same `extractVideoAudio`, which releases.

Explicit `out` is deliberately unchanged: it still replaces an existing file, as `include/VideoAudioExtract.h`
documents ("success replaces it") - the user names the target there, so it is not the default naming this finding
describes. See "Other findings".

## Regression test

`tests/src/core/VideoAudioNameTest.cpp` (the existing file for this area, already registered in `tests/CMakeLists.txt`
and `tests/fork-sources.txt`). Four new cases; no FFmpeg, no clock, no sleep, no thread, no engine:

| case | what it pins | reverting the fix |
| --- | --- | --- |
| `twoSearchesBeforeAWavExistsGetDifferentNames` | two searches before either WAV exists get `<stem>.wav` then `<stem>-2.wav` | **FAIL** (both got `<stem>.wav`) |
| `everyNameClaimedGivesNoName` | range 3: three claims are handed out one by one, a fourth gets nothing | **FAIL** (second claim got `<stem>.wav`) |
| `aFailedExtractionGivesItsNameBack` | the claim ends with its extraction: a failed one writes nothing and frees the name | pass (guards a fix that claims forever) |
| `aNameItsExtractionWroteStaysTaken` | a name a file now holds stays taken after the claim ended | pass (guards a fix that releases the file's claim into reuse) |

Test matrix that was checked: normal path (one search takes the simplest name, existing cases unchanged), failure
(a decode that fails releases its claim), exhaustion (all names in range claimed -> empty, so the caller refuses
instead of reusing one), reconfiguration (a released name that a file now holds is still skipped), audio-thread
constraints (nothing here touches the audio thread - the mutex is taken by the control thread and by the decode's
worker thread only), supported platforms (Qt `QDir`/`QFileInfo`/`QMutex`/`QSet` only, no FFmpeg requirement, the cases
run in both the FFmpeg and the no-FFmpeg configuration).

## Commands run (EXIT codes as seen)

Logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p9-67.r1/logs/`.

| step | command (cwd = worktree unless noted) | log | EXIT |
| --- | --- | --- | --- |
| RED build 1 | `cmake --build build --target VideoAudioNameTest -j4` | `build-1-red.log` | **2** (my new test file was missing two `using` declarations - test typos, no product change) |
| RED build 2 | `cmake --build build --target VideoAudioNameTest -j4` | `build-2-red.log` | 0 |
| **RED run** (no product change yet) | `cd build/tests && ctest -R VideoAudioNameTest --output-on-failure` | `test-1-red.log` | **8** (2 failed, 7 passed) |
| GREEN build | `cmake --build build --target zene VideoAudioNameTest -j4` | `build-3-green.log` | 0 |
| **GREEN run** | `cd build/tests && ctest -R VideoAudioNameTest --output-on-failure` | `test-2-green.log` | **0** |
| family build | `cmake --build build --target VideoTrackTest VideoFfmpegTest VideoTestClipTest VideoTestClipPortabilityTest VideoClipsTest ArrangerOverlaysVideoLaneTest -j4` | `build-4-family.log` | 0 |
| family run 1 | `cd build/tests && ctest -R Video --output-on-failure` | `test-3-family.log` | **8** (2 failures: `VideoClipsGuiTest` "Not Run" - its binary was not built in this tree - and `NoFfmpegVideoClipsCompileCheck`, which needs that target's generated `VideoClipsGuiTest.moc`) |
| family build 2 | `cmake --build build --target VideoClipsGuiTest -j4` | `build-5-family-gui.log` | 0 |
| family run 2 | `cd build/tests && ctest -R Video --output-on-failure` | `test-4-family.log` | **0** (11 of 11 passed) |
| 4b save patch | `git diff -- include/VideoAudioExtract.h src/core/VideoAudioExtract.cpp src/core/ControlCommandsVideo.cpp > .../logs/fix.patch` | - | 0 |
| 4b revert | `git apply -R .../logs/fix.patch` | - | 0 |
| 4b reverted build | `cmake --build build --target VideoAudioNameTest -j4` | `build-6-proof-reverted.log` | 0 |
| **4b reverted run** | `cd build/tests && ctest -R VideoAudioNameTest --output-on-failure` | `test-5-proof-reverted.log` | **8** (`twoSearchesBeforeAWavExistsGetDifferentNames` and `everyNameClaimedGivesNoName` fail: "Totals: 9 passed, 2 failed") |
| 4b restore | `git apply .../logs/fix.patch` | - | 0 |
| 4b restored build | `cmake --build build --target zene VideoAudioNameTest -j4` | `build-7-proof-restored.log` | 0 |
| **4b restored run** | `cd build/tests && ctest -R VideoAudioNameTest --output-on-failure` | `test-6-proof-restored.log` | **0** |
| gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | `gates-1-feature-check.log` | **0** |
| final build | `cmake --build build --target zene VideoAudioNameTest VideoTrackTest VideoClipsTest VideoFfmpegTest VideoTestClipTest VideoTestClipPortabilityTest ArrangerOverlaysVideoLaneTest VideoClipsGuiTest -j4` | `build-8-final.log` | 0 |
| final family run | `cd build/tests && ctest -R Video --output-on-failure` | `test-7-final-family.log` | **0** (11 of 11) |

Gate table (`gates-1-feature-check.log`):

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

Reverted-state output (`test-5-proof-reverted.log`):

```
FAIL!  : VideoAudioNameTest::twoSearchesBeforeAWavExistsGetDifferentNames() Compared values are not the same
   Actual   (second): ".../VideoAudioNameTest-.../clip-audio.wav"
FAIL!  : VideoAudioNameTest::everyNameClaimedGivesNoName() Compared values are not the same
   Actual   (second): ".../VideoAudioNameTest-.../clip-audio.wav"
Totals: 9 passed, 2 failed, 0 skipped, 0 blacklisted, 2ms
```

## Files changed

`src/core/VideoAudioExtract.cpp`, `include/VideoAudioExtract.h`, `src/core/ControlCommandsVideo.cpp` (comment only),
`tests/src/core/VideoAudioNameTest.cpp`, this report. All four product/test files are already listed in
`tests/fork-sources.txt` (`:1457`, `:1939`, `:2269`, `:3051`), so no registration file changed and the lists were not
touched.

## Self-check

1. **The regression test fails with the product change reverted and passes with it.** Yes, both directions run this
   session: reverted (`git apply -R fix.patch`) -> `test-5-proof-reverted.log`, EXIT **8**, two assertion failures;
   restored -> `test-6-proof-restored.log`, EXIT **0**. Recorded before any product change too: `test-1-red.log`,
   EXIT **8**.
2. **The test is deterministic.** No clock, sleep, thread, FFmpeg or engine: each case makes its own `QTemporaryDir`
   and drives `firstFreeAudioPath` / `extractVideoAudio` in order. The "concurrent request" is modelled by calling the
   naming function twice before any file exists - exactly the state the two background requests are in - so the case
   does not depend on scheduling. Release is driven by a decode that fails on a missing path (`no-such-video.mkv`), an
   injected value, not a timing hook.
3. **The fix is at the cause and complete; callers and siblings checked.** `firstFreeAudioPath` has one product caller
   (`defaultAudioPath`, `ControlCommandsVideo.cpp:257`), which has one caller (`extractAudio:330`). Both destinations
   in `extractAudio` (synchronous `:341`, background `:310`) go through `extractVideoAudio`, so both release. The
   other path into the same function - the `QThreadPool` worker - was checked: it always calls `extractVideoAudio`,
   including for a task cancelled before it starts, so a cancelled background job cannot strand a claim. The no-FFmpeg
   overload releases too, so a build without FFmpeg behaves the same. Sibling commands (`video.attach`,
   `video.clip_add`, `video.clip_remove`, `video.detach`, `video.get_state`, `video.render_frames`,
   `video.set_offset`, `video.window_show`) do not name or write a WAV: `video.render_frames` writes
   `frame-NNNNN.png` into a directory the caller names (`src/core/ControlCommandsVideoRender.cpp:182`) and has no
   search, so it is not affected and unchanged. `task.cancel`/`task.list`/`task.clear_finished`/`task.show` only
   report on the job; the claim is not a task and is not exposed to them.
4. **No collateral damage.** The name search itself is unchanged: same candidates, same order, same `MaxAudioNames`
   limit (100), same refusal when every name is taken, and an explicit `out` is not subject to the limit or the claim,
   as before. Projects and saved data are untouched - no format, no field, no command schema, no description string
   (the description of `video.extract_audio` is byte-identical, so `ControlCommandsSnapshot` cannot move). Old
   projects still load: nothing here is read at load time. The only behaviour that changes is the documented one: a
   second extraction started while a first is still writing no longer gets the first's path.
5. **Verdict is FIXED**, so no ALREADY-FIXED/FALSE-POSITIVE citation is needed; the evidence is above
   (`ControlCommandsVideo.cpp:293-319` naming before the worker writes, `VideoAudioExtract.cpp:100`/`:128-156` the
   late commit).

## Other findings

- Explicit `out` still replaces an existing file, including when two background jobs are given the same explicit
  `out`: that is the documented contract of the parameter ("a failed or cancelled run leaves `@a wav` unchanged, and
  success replaces it", `include/VideoAudioExtract.h:54-57`), and the user names that target. Making a duplicate
  explicit target an error would be a new refusal in a documented overwrite path, so it is out of this fix.
- The claim is in-process: two `zene` processes extracting the same video into the same folder can still both choose
  `<video>-audio.wav`. A cross-process reservation would need a lock file next to the project (litter, and a crashed
  process leaves it behind), which is a separate design decision.
