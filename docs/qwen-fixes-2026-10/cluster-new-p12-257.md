# Cluster new-p12-257: A pending Record press carries into the next project

**Verdict: FIXED**

`Song::record()` sets `m_recording` and `m_recordLatched`; `startSongPlayback()` consumes the latch on the next Play so the pass records. `Song::clearProject()` only called `stop()` when `m_playing`, so a Record press made while stopped left both the latch and the recording flag set, and the next project's first Play started a recording take it should not have had.

Fix in `src/core/Song.cpp:1370` (inside `Song::clearProject()`): after the optional `stop()`, unconditionally clear `m_recordLatched` and `m_recording`, so a cleared/new project can never inherit a pending Record press or an armed recording state.

Evidence in `src/core/Song.cpp`:

- `:805`: `startSongPlayback()` reads the latch (`const bool latched = m_recordLatched;`) and `m_recordLatched = false;`, then sets `m_recording` only when `recording || latched`.
- `:841`: `record()` sets `m_recording = true; m_recordLatched = true;`.
- `:971`: `stop()` clears only the latch and only takes effect when already playing (early-return otherwise), so it cannot clear a stopped-but-latched Record press.
- `:1370-1378`: `clearProject()` now clears the latch and recording state.

## Regression test

`tests/src/core/TapTempoTest.cpp` adds `aPendingRecordPressDoesNotCarryIntoTheNextProject`:

- calls `Engine::getSong()->record()` and asserts it is recording;
- calls `clearProject()` and asserts it is no longer recording;
- calls `playSong()` and asserts playback is not recording;
- calls `stop()`.

## Evidence table

| Check | Exact command and result |
| --- | --- |
| RED before fix | `cmake --build build --target TapTempoTest > .../logs/build-1.log 2>&1`, then from `build/tests`: `ctest -R '^TapTempoTest$' --output-on-failure > .../logs/test-red.log 2>&1`; **EXIT=8**, `FAIL! ... '!song->isRecording()' returned FALSE.` at `TapTempoTest.cpp:51`. |
| GREEN after fix | Same ctest command, output to `.../logs/test-green.log`; **EXIT=0**, 1/1 passed. |
| Reversed-fix proof | `git apply -R .../logs/fix.patch` **EXIT=0**; rebuild `cmake --build build --target TapTempoTest` **EXIT=0** (`build-rev.log`); ctest output `.../logs/test-rev.log` **EXIT=8**, same assertion failure. |
| Restored-fix proof | `git apply .../logs/fix.patch` **EXIT=0**; rebuild **EXIT=0** (`build-reapply.log`); ctest output `.../logs/test-reapply.log` **EXIT=0**, 1/1 passed. |
| Affected family | `TapTempoTest`: 2 CTest test cases executed; 2/2 passed on green runs. |
| Complexity gate | `bash tests/complexity-gate.sh --check` **EXIT=1** (pre-existing baseline drift in 6 unrelated functions; our change adds no regression - same list before and after). |
| File-length gate | `bash tests/file-length-gate.sh --check` **EXIT=1** (pre-existing baseline drift in 13 unrelated files; our change adds no regression - same list before and after). |
| No-upstream-regression gate | `bash tests/no-upstream-regression-gate.sh` **EXIT=0**. |
| Unregistered-tests gate | `bash tests/unregistered-tests-gate.sh` **EXIT=0**. |
| zene build | `cmake --build build --target zene` **EXIT=0**. |

## Files committed

- `src/core/Song.cpp`
- `tests/src/core/TapTempoTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p12-257.md`
