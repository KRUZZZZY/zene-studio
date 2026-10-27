FIXED

## Decision

REAL. [NoteConditions.cpp:80](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p18-69/src/core/NoteConditions.cpp:80) sends every loaded lyric attribute to `internLyric()`. Before this fix, `src/core/NoteConditions.cpp:96` used `std::find` across the whole vector for every nonempty value. The fixed indexed lookup is at [NoteConditions.cpp:123](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p18-69/src/core/NoteConditions.cpp:123).

## Fix

`internLyric()` now checks a `QHash<QString, int>` index and keeps the existing vector as the ID-ordered source for `lyricText()`. Empty text remains ID 0, duplicate text returns its original ID, and newly seen text receives the next vector ID. No ID cap or command behavior changed.

The indexed growth remains on the existing UI-thread path. The other `internLyric()` caller is the UI control handler at `src/core/ControlCommandsNotePlayback.cpp:76`; there is no audio render caller. `lyricText()` remains vector-backed. No header or realtime code changed.

## Test matrix

| Area | Coverage and result |
|---|---|
| Normal import | `NotePlaybackTest::importingDistinctLyricsUsesIndexedLookup` loads 512 distinct lyric attributes through `NoteConditions::load`, checks sequential IDs and text round-trips, and reloads each value to check ID reuse. A test-only lookup-work counter makes the bound deterministic; no timing is used. |
| Empty, duplicate, invalid ID | The same test checks empty text is ID 0, ID 0 and `INT_MAX` read as empty, duplicate imports retain their ID, and all imported IDs resolve to their original text. |
| Failure, exhaustion, reconfiguration | The table has no reconfiguration API or configured lyric limit. The test covers growth and duplicate reuse without adding a cap. Allocation failure and exhausting the existing `int` ID space are not safely injectable; no new failure behavior was introduced. |
| Audio-thread constraints | `internLyric()` is documented UI-thread-only. The index allocates only in its existing import/control callers; playback and render code were not changed. |
| Platform build | Linux/Qt configured build verified below. The implementation uses Qt `QHash` and C++20; no Windows/macOS toolchain was available for this run. |

## Evidence

The original scan failed the regression as expected. The first CTest attempt, before the test's instrument modules were built, had two additional missing-plugin failures; after building `monstro`, `nes`, and `tripleoscillator`, the clean RED run had only the regression failure.

| Check | Exact command | Exit | Result |
|---|---|---:|---|
| Build original implementation and test prerequisites | `cmake --build build --target zene NotePlaybackTest monstro nes tripleoscillator -j12` | 0 | Built `zene`, `NotePlaybackTest`, and the three modules. |
| Initial RED attempt | `ctest -R '^NotePlaybackTest$' --output-on-failure` (from `build/tests`) | 8 | 27 passed, 3 failed; the lookup assertion and two tests whose modules had not yet been built. |
| Clean RED | `ctest -R '^NotePlaybackTest$' --output-on-failure` (from `build/tests`) | 8 | 29 passed, 1 failed: the lookup-work bound. |
| GREEN build | `cmake --build build --target zene NotePlaybackTest monstro nes tripleoscillator -j12` | 0 | Built with the index fix. |
| GREEN family | `ctest -R '^NotePlaybackTest$' --output-on-failure` (from `build/tests`) | 0 | 1/1 CTest test passed; all 30 QtTest cases passed. |
| Negative-control patch reverse | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-69.r1/logs/fix.patch` | 0 | Restored the original scan while retaining the test seam. |
| Negative-control build | `cmake --build build --target zene NotePlaybackTest -j12` | 0 | Rebuilt the original scan with the regression. |
| Negative-control test | `ctest -R '^NotePlaybackTest$' --output-on-failure` (from `build/tests`) | 8 | 29 passed, 1 failed: the same lookup-work assertion. |
| Patch reapply | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-69.r1/logs/fix.patch` | 0 | Restored the index fix. |
| Reapplied build | `cmake --build build --target zene NotePlaybackTest -j12` | 0 | Rebuilt `zene` and the test. |
| Reapplied test | `ctest -R '^NotePlaybackTest$' --output-on-failure` (from `build/tests`) | 0 | 1/1 CTest test passed. |

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-69.r1/logs/` (`5` through `12` contain the RED/GREEN and negative-control build/test records; `13` and `15` record the gate repair; `17-feature-check-final-edit.log` contains the final pre-commit gate table; `18-submit-check-initial.log` contains the post-commit completion check).

`python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p18-69` first returned 1 because `tests/all-sources.txt` still contained 1,148 vendored sfizz paths excluded by its own documented recipe. I regenerated that manifest from the recipe; no gate limit or baseline changed. The same command then returned 0:

| Gate | Result |
|---|---|
| `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p18-69` | EXIT=0; all gates PASS. Complexity and file-length report no new items. |
| `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p18-69 new-p18-69` | EXIT=0; all completion checks passed. It emitted five Self-check prompts, answered below. |

## Self-check

- **Callers and ID flow:** `NoteConditions::load()` and `ControlCommandsNotePlayback.cpp:76` call `internLyric()`. Both are UI-side; IDs remain `int`, assigned from the vector size, and `lyricText(int)` still reads that same vector.
- **Compatibility:** Empty lyrics, duplicate reuse, append-only ID order, serialization through `lyricText()`, and unknown IDs retain their existing behavior. No limits were added.
- **Realtime and undo:** No render path, locking, logging, new commands, or undo behavior changed.
- **RED/GREEN proof:** The clean RED CTest run exited 8 (29 passed, 1 failed). With `fix.patch` reversed, the build exited 0 and the CTest run exited 8 on the same assertion. Reapplying the patch exited 0; the rebuild and CTest run exited 0. The CTest family has 1 registered CTest test; the executable ran 30 QtTest cases.
- **Determinism:** The test imports the fixed values `new-p18-69-import-0` through `new-p18-69-import-511`. It uses `resetInternLyricLookupWorkForTest()` / `internLyricLookupWorkForTest()` under `LMMS_TESTING` and asserts at most 1,024 indexed lookup steps. It has no clock, sleep, worker thread, or race.
- **Registration:** Both changed C++ files were already registered in `tests/fork-sources.txt`; `tests/all-sources.txt` now matches its documented generator. No new source file was added.
- **Callers and siblings:** The only `internLyric()` callers are `NoteConditions::load()` and the `note.lyrics` UI control handler. `lyricText()` still reads the ID-ordered vector used by save and the note canvas; no declaration, wider type, audio path, command, or undo path changed.
- **Compatibility and limits:** Previously saved `lyr` strings still load through `NoteConditions::load()` and retain their text/ID behavior. The vector remains the ID authority, duplicate IDs are reused, and no new limit can reject existing or future callers.
- **Conditional verdict question:** Not applicable; the verdict is FIXED for a REAL finding.
- **Gates:** Final `feature_check.py` exit 0; post-commit `submit_check.py` exit 0 (log `18-submit-check-initial.log`).

## Committed files

- `src/core/NoteConditions.cpp`
- `tests/src/core/NotePlaybackTest.cpp`
- `tests/all-sources.txt` (regenerated from its documented recipe to repair the gate's stale inventory)
- `docs/qwen-fixes-2026-10/cluster-new-p18-69.md`
