Verdict: FALSE-POSITIVE

new-p9-5 (BUG 14, "Invalid warp data can dereference a missing song"): the trigger the finding describes cannot occur. A `Clip` cannot exist while `Engine::getSong()` is null, so `Clip::loadWarp` always runs with a song installed. The two unguarded `collectError` calls are real, but no reachable state reaches them. No product change is made, and no regression test is committed, because no state exists that a test could reproduce.

## Evidence

1. The calls the finding names are unguarded: `src/core/Clip.cpp:785` and `src/core/Clip.cpp:807` call `Engine::getSong()->collectError(...)` inside `Clip::loadWarp` (`src/core/Clip.cpp:767`). `Clip::restoreState` calls `loadWarp` at `src/core/Clip.cpp:204`.
2. A clip cannot be built without the audio engine. The `Clip` constructor builds `m_mutedModel` (`src/core/Clip.cpp:68`; declared as `BoolModel` at `include/Clip.h:378`). The `AutomatableModel` constructor reads `Engine::audioEngine()->framesPerPeriod()` at `src/core/AutomatableModel.cpp:150`.
3. The audio engine and the song are created together, and only there. `src/core/Engine.cpp:59` is `Engine::init`; `:71` assigns `s_audioEngine = new AudioEngine(renderOnly)` and `:72` assigns `s_song = new Song`. A search of `src/` finds no other assignment to either pointer. Teardown removes the audio engine (`src/core/Engine.cpp:113`) before the song (`:123`), after `s_song->clearProject()` (`:106`). Headless mode (`Engine::init(true)`) creates the song as well.
   Consequence: any live clip implies a non-null audio engine, which implies a non-null song. The finding's state ("a malformed warp map is restored on a clip before a song is installed") cannot be reached.
4. Runtime check of point 2: a `Clip` built with no engine crashes inside its constructor, before `loadWarp` runs. `$LOGS/test-1-red.log` lines 44-46 show `AutomatableModel::AutomatableModel` called from `lmms::Clip::Clip`, and `$LOGS/test-2-green.log` shows the same chain.

## Why there is no regression test

- The first regression test (a no-song class in `ClipWarpPersistenceTest.cpp`, run before `Engine::init`) was built and run RED. It crashed in the `Clip` constructor (point 4), not in `loadWarp`, so that run says nothing about the reported path. This is recorded as an invalid RED, not as the bug's failure.
- I then applied a guard at both sites (`collectWarpLoadError`, a null-checked wrapper around `Engine::getSong()->collectError`). The same test still crashed in the constructor, so the guard could not be exercised. Both edits were reverted. The discarded diff is kept at `$LOGS/abandoned-fix-and-test.patch`.
- No public path can reach `loadWarp` with a null song: `Engine::init` always creates the song, and nothing clears it while a clip is alive.

## Siblings (checked, not changed)

- `src/core/SampleClip.cpp:648` (`setWarpMarkers`) calls `Engine::getSong()->collectError` unguarded. Its only caller is `src/core/ControlCommandsWarp.cpp:232`, a control command that runs with the engine up. It is not on a restore path.
- `src/core/Track.cpp:1418` calls `Engine::getSong()->collectError` unguarded. The same function already requires a song at `src/core/Track.cpp:1421`, so a guard there would not change behaviour.

## Commands and exit codes

`$LOGS` = `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-5.r1/logs`. Commands were run from the worktree (or from `build/tests` where noted).

| Evidence | Command | Result |
| --- | --- | --- |
| Covering family builds at HEAD (baseline, no product change) | `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp/ccache-tmp cmake --build build --target ClipWarpPersistenceTest LinkedClipGroupEditsTest ClipImportFileTest SampleClipStretchTest -j4 > $LOGS/build-5-baseline.log 2>&1; echo EXIT=$?` | EXIT=0 |
| Covering family runs at HEAD (baseline) | `ctest -R '^(ClipWarpPersistenceTest\|LinkedClipGroupEditsTest\|ClipImportFileTest\|SampleClipStretchTest)$' --output-on-failure > $LOGS/test-3-baseline.log 2>&1; echo EXIT=$?` | EXIT=0; "100% tests passed out of 4" (4 tests executed) |
| Invalid RED: no-song test, before any product change | `ctest -R '^ClipWarpPersistenceTest$' --output-on-failure > $LOGS/test-1-red.log 2>&1; echo EXIT=$?` | EXIT=8, SEGFAULT in `Clip::Clip` (not `loadWarp`) |
| Invalid GREEN attempt: same test with the guard applied | same command, `$LOGS/test-2-green.log` | EXIT=8, same constructor crash |
| First build attempt, ccache temp dir not writable | `cmake --build build --target ClipWarpPersistenceTest -j4 > $LOGS/build-1-red.log 2>&1` | EXIT=2 (`ccache: failed to create temporary file ... Permission denied`, `build-1-red.log:218`); fixed for later builds with the `CCACHE_TEMPDIR` override above |
| Separate test target, first try: build of a new target not in the configured tree | `cmake --build build --target ClipWarpHeadlessRestoreTest -j4 > $LOGS/build-1-red.log 2>&1` (log later overwritten), then `cmake -S . -B build > $LOGS/cmake-1.log 2>&1; echo EXIT=$?` | build: EXIT=2 "No rule to make target"; configure: EXIT=97 (`bunny-guard: configuring is the parent's job`). Registration reverted; no new target committed |
| Test compile, first attempt (abstract `Clip` subclass) | `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp/ccache-tmp cmake --build build --target ClipWarpPersistenceTest -j4 > $LOGS/build-2-red.log 2>&1` | EXIT=2 (`build-2-red.log:20`, abstract: `SerializingObject` pure virtuals); fixed by adding overrides |
| RED build, unfixed `Clip.cpp` | same command, `> $LOGS/build-3-red.log 2>&1` | EXIT=0 |
| GREEN build attempt, guard applied | same command, `> $LOGS/build-4-green.log 2>&1` | EXIT=0 |
| Feature gate script, as written | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > $LOGS/gates-1.log 2>&1; echo EXIT=$?` | EXIT=1: `PermissionError` creating `<projects>/zene-qwen-new-p9-5-feature-check`, outside the write-jail. The script did not run its gates |
| Feature gates, same SIMPLE and RATCHET lists and cwd, logs redirected to `$LOGS/gates-replica` | `python3 -I $LOGS/gates_replica.py <worktree> $LOGS/gates-replica > $LOGS/gates-2-replica.txt 2>&1; echo EXIT=$?` | EXIT=0, RESULT PASS. fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures: exit 0. complexity and file-length: ratchet exit 1 (red on train by design), "no new item in your files" (0 changed paths against base 37eaf0755) |

The ratchet exit codes are 1 on the train, as documented in `feature_check.py`. They pass because no item they list is new in a changed file.

## Self-check

- **Gate item FAIL in `submit_check.py` (`gates (feature_check.py)`).** The script exits 1 before it runs any gate: it writes its logs to `<worktree>/../zene-qwen-new-p9-5-feature-check`, which is under `~/Documents/AI_KOS_PROJECT/projects/zene-studio-program/` and is not writable from this lane (`$LOGS/gates-1.log`, `PermissionError`). I did not change the gate or move its output. The same six simple gates and two ratchets were run unchanged through `$LOGS/gates_replica.py` with logs in the LOG FOLDER: RESULT PASS (`$LOGS/gates-2-replica.txt`, EXIT=0). Re-run `feature_check.py` where that folder can be written, to confirm.
- **Regression FAILS with the change reverted and PASSES with it:** not applicable. No product change is committed, so there is nothing to revert. The only regression runs (`test-1-red.log` EXIT=8, `test-2-green.log` EXIT=8) crashed in `Clip::Clip`, not in `loadWarp`, and were not committed.
- **Deterministic:** the discarded test used no timing, sleeps or races: both runs crashed at the same constructor path. It is not committed, so there is no test to rely on.
- **Fix at the cause and complete:** no fix. Callers and siblings checked: `src/core/Clip.cpp:785,807` (the finding's sites; the only caller is `Clip::restoreState`, `Clip.cpp:204`), `src/core/SampleClip.cpp:648` (only caller `src/core/ControlCommandsWarp.cpp:232`), `src/core/Track.cpp:1418` (the same function already calls `Engine::getSong()` at `Track.cpp:1421`).
- **No collateral damage:** no product file changed. The covering family at HEAD passes 4/4 (`$LOGS/test-3-baseline.log`, EXIT=0).
- **FALSE-POSITIVE evidence (file:line):** `src/core/Clip.cpp:68`, `src/core/AutomatableModel.cpp:150`, `src/core/Engine.cpp:71-72`, `src/core/Engine.cpp:113,123`, and the runtime chain in `$LOGS/test-1-red.log:44-46`.
