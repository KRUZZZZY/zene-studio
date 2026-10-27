FIXED: a clip or note whose start plus duration passes `tick_t` is no longer imported with an overflowing end; the duration is cut back to the room the start leaves, and the cut is counted in `rounded_times`. The finding asked for a refusal instead; see "Design choice" below.

# Cluster new-p14-89: clip and note end times overflow the engine tick range

## Verdict and evidence

**REAL, fixed on `bugs/qwen-new-p14-89`.** Both paths the finding names are real on the base `7641e9493d`:

- **Reader, no end check.** `src/core/DawProjectReadTracks.cpp` (base) read a note's `time` and `duration` (base lines 134-139) and a clip's `time` and `duration` (base lines 175-180) each through `ticksFromBeats()`, which clamps each value into `int32` (`src/core/DawProjectReadShared.h:237-255`). Nothing summed them. A document with a clip at tick 2147483568 (beat 44739241) and duration 96 ticks was accepted with the end at 2147483664, past `INT32_MAX`.
- **Engine end is an `int` add.** `include/Clip.h:128-131`: `const int sp = m_startPosition; return sp + m_length;`. Past `INT32_MAX` that is signed overflow. The session applies the clip at `src/core/DawProjectSession.cpp:572-573` (`createClip(TimePos(start))`) and sets its length at `:606-608` (`changeLength`).
- **Writer already refuses the same condition.** `src/core/DawProjectWrite.cpp:161` (clip end) and `:176` (note end), via `endFitsTickRange()` at `:137-143`. A document this module writes never contains the overflowing end; only a foreign document does.

### Design choice (please confirm)

The finding's fix line says to "refuse values past the tick range". I did not refuse, for one reason. The reader's documented contract (`DawProjectReadShared.h:231-236`) is that a time past the 32-bit tick is clamped and counted, not refused, and an existing test pins that contract: `DawProjectInterchangeRoundTripTest.cpp:1842` `hugeBeatPositionsCannotEnterTheModelOutOfTickRange` imports a clip at beat 50000000 with duration 4 and a note at the same start, and asserts `dawProjectModelFromXml()` succeeds. Refusing the end would make that test fail. Changing its expectation would weaken an existing test, which this lane is not allowed to do.

So the fix extends the same clamp to the end. `durationWithinTickRange()` (`src/core/DawProjectReadTracks.cpp:97-104`) returns the duration unchanged when `start + duration <= tick_t` max, and otherwise returns `max - start` and increments `loss.roundedTimes`. It applies to every note, and to a clip only when the clip is carried (it has `<Notes>`), so an uncarried clip keeps its full duration and the skipped-clip count is unchanged (`:223-230`). If the owner wants refusal instead, `hugeBeatPositionsCannotEnterTheModelOutOfTickRange` must change its expectation in the same commit. That is an owner call.

## Changes

- `src/core/DawProjectReadTracks.cpp`: new `durationWithinTickRange()` (`:97-104`); note start/duration with the end cut (`:144-151`); clip start/duration with the end cut only for a carried clip (`:185-191`, `:223-230`).
- `tests/src/core/DawProjectInterchangeRoundTripTest.cpp`: new `readerKeepsATimelineEndpointInsideTheTickRange()` (`:2122`), placed beside the writer's end-point tests. It is a private slot, so QTest runs it.
- No header change. No new file. No new control command. No audio-thread code. `tests/fork-sources.txt` and `all-sources.txt` already list both files (`DawProjectReadTracks.cpp` in fork-sources; the test in all-sources at `:2732`), so no registration change is needed.

## Test matrix

| Path | Case | Covered by |
| --- | --- | --- |
| Normal | clip and note that fit (`79` ticks, end exactly at max) keep their duration, no loss counted | `readerKeepsATimelineEndpointInsideTheTickRange` (fit clip, fit note, `roundedTimes == 0`) |
| Overflow, clip | start 2147483568 + duration 96 cut to 79, counted | same test, clip block |
| Overflow, note | note start 2147483568 + duration 96 cut to 79, counted | same test, note block |
| Uncarried clip | clip with no `<Notes>` keeps full duration, counted as automation-skipped | same test, last block |
| Clamped start (existing) | start beyond tick range, duration 4 beats: accepted, positions clamped | existing `hugeBeatPositionsCannotEnterTheModelOutOfTickRange`, passes unchanged |
| Writer side | writer still refuses overflowing clip/note end | existing `writerRefusesAClipEndpointPastTheTickRange`, `writerRefusesANoteEndpointPastTheTickRange` |
| Audio thread / undo / commands | not applicable: import-time reader only; no command, no undo path | n/a |
| Platform | Qt 5.15/6 APIs only (`qRound64`, `qint64`); no Windows macros, no `std::filesystem` | n/a |

## Commands and exit codes

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-89.r1/logs/`.

| # | Command (run from the worktree unless noted) | Log | EXIT |
| --- | --- | --- | --- |
| 1 | `cmake --build build --target DawProjectInterchangeRoundTripTest -j4` | `build-1-red.log` | 2 (ccache could not write its temp dir: `Permission denied`; no compile happened) |
| 2 | `CCACHE_DISABLE=1 cmake --build build --target DawProjectInterchangeRoundTripTest -j4` | `build-2-red.log` | 0 |
| 3 | `ctest -R DawProjectInterchangeRoundTripTest` after the first RED test (expected refusal of a clip end) | `test-1-red.log` | 8: `60 passed, 1 failed`; `accepted a timeline end past the tick range: time="44739241" duration="2"` (first test version, before the fix) |
| 4 | Same binary with a **refusal** fix (first attempt) | `build-3-green.log`, `test-2-green.log` | build 0; test 8: `60 passed, 1 failed`; `hugeBeatPositionsCannotEnterTheModelOutOfTickRange` now refused → this is why the design changed to a clamp |
| 5 | Clamp fix, second test version | `build-4-green.log`, `test-3-green.log` | build 0; test 0 |
| 6 | Proof pass 1 on the clamp fix, before the uncarried-clip change: revert, rebuild, test; restore, rebuild, test | `build-5-red-proof.log`, `test-4-red-proof.log`, `build-6-green-proof.log`, `test-5-green-proof.log` | build 0, **test 8** (`readerKeepsATimelineEndpointInsideTheTickRange` fails); build 0, **test 0** |
| 7 | Final code (uncarried-clip change, final test): `CCACHE_DISABLE=1 cmake --build build --target DawProjectInterchangeRoundTripTest -j4`, then `cd build/tests && ctest -R DawProjectInterchangeRoundTripTest -V` | `build-8-green.log`, `test-7-green.log` | build 0; test 0 |
| 8 | **RED/GREEN proof (4b), final code.** `git diff -- src/core/DawProjectReadTracks.cpp > logs/fix.patch`; `git apply -R logs/fix.patch`; rebuild; ctest. Then `git apply logs/fix.patch`; rebuild; ctest | `build-9-red-proof.log`, `test-8-red-proof.log`, `build-10-green-proof.log`, `test-9-green-proof.log` | diff 0, revert 0, build 0, **RED test 8**; reapply 0, build 0, **GREEN test 0** |
| 9 | `CCACHE_DISABLE=1 cmake --build build --target zene DawProjectDeflateImportTest -j4` (final tree) | `build-11-final.log` | 0 |
| 10 | `cd build/tests && ctest -R DawProject -V` (final tree) | `test-10-family-final.log` | 0 |
| 11 | Gates, final tree: `python3 runs/new-p14-89.r1/gate-wrap.py <worktree> logs/gate-logs` (see note) | `gates-3.log` | 0 |
| — | Stock `feature_check.py` run (before the wrapper) | `gates-1.log` | 1: `PermissionError` creating `<worktree>-feature-check` outside the jail; no check ran |
| — | Wrapper run before the uncarried-clip change | `gates-2.log` | 0 (superseded by `gates-3.log`) |

Note on 11: `feature_check.py` itself writes its per-gate logs to `<worktree>/../<name>-feature-check`, which is outside the write-jail, so it stopped with `PermissionError` before running any check (`gates-1.log`, exit 1). I did not edit the shared script. I ran a wrapper that imports `feature_check.py` unchanged and calls its own `SIMPLE`/`RATCHET` lists and classification, with logs in the run folder (`gate-wrap.py`). Its table is the one below. The baseline cache for base `7641e9493d` was already present, so the complexity and file-length comparison used the existing cache. Someone with write access to the parent folder should re-run the stock `feature_check.py` to confirm.

### Evidence table

| Item | Result |
| --- | --- |
| RED command and EXIT | `test-8-red-proof.log`, fix reverted, `ctest … -V`: **EXIT 8**, `60 passed, 1 failed`; failed `readerKeepsATimelineEndpointInsideTheTickRange`: `Actual (qRound(clip.duration * DawProjectTicksPerQuarterNote)): 96`, `Expected (79): 79` (`DawProjectInterchangeRoundTripTest.cpp:2144`) |
| GREEN command and EXIT | `test-9-green-proof.log`, fix restored, `ctest … -V`: **EXIT 0**, `61 passed, 0 failed` |
| Affected family (ctest `-R DawProject`) | **2 of 2 tests** passed (`#354 DawProjectInterchangeRoundTripTest`, `#355 DawProjectDeflateImportTest`), `test-10-family-final.log` EXIT 0 |
| Gate exit (`feature_check` table) | `gates-3.log` EXIT 0, `RESULT: PASS` (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures: PASS; complexity and file-length: "no new item in your files") |
| Files committed | `src/core/DawProjectReadTracks.cpp`, `tests/src/core/DawProjectInterchangeRoundTripTest.cpp`, `docs/qwen-fixes-2026-10/cluster-new-p14-89.md` |

`DawProjectReadTracks.cpp` is 519 lines. It was already over the 500-line file gate on the base, and the gate reports no new item, so the limit was not touched.

## Self-check

- **Each new test FAILS with the product change reverted and PASSES with it.** Yes. Reverting `src/core/DawProjectReadTracks.cpp` (`git apply -R logs/fix.patch`) and running the new test: EXIT 8, the test fails on `Actual 96 / Expected 79` (`test-8-red-proof.log`). Restoring the fix (`git apply logs/fix.patch`): EXIT 0, 61 of 61 pass (`test-9-green-proof.log`).
- **Acceptance criteria.** The brief lists no per-criterion acceptance table. The six Definition-of-done items are each answered in this report (verdict and evidence; RED/GREEN proof; the sibling and caller check below; no collateral damage below; no realtime, undo or command change; gates and registration above).
- **Undo and commands.** No control command is added or changed. The change is reader-side and runs only when a document is imported; it is not an edit, so it has no undo step and no reversibility class.
- **Audio thread.** Nothing added runs on the audio thread. Both changed functions are in the XML reader (`parseNotes`, `parseClips`) and run during import. The diff touches none of `processImpl`, `playImpl`, `renderPeriod`, `NotePlayHandle` or `AudioEngine::`.
- **Old projects still load and render exactly as before.** For a document whose end fits in tick range, `durationWithinTickRange()` returns the duration unchanged, so the model is identical to before. Only a document whose end passes the range changes (its duration is cut and counted in `rounded_times`). The 60 existing tests in that binary pass unchanged, including `hugeBeatPositionsCannotEnterTheModelOutOfTickRange` (`:1842`) and the writer's end-point tests. The writer is untouched, so saved projects and their output are unchanged. The round trip of in-range data is covered by the existing round-trip tests, which pass.
- **Sibling and caller check.** Every caller of `ticksFromBeats()` in the reader (note time, note duration, clip time, clip duration, clip playStart, `parseNotes`/`parseClips`) was checked. The end cut is applied only where start and duration both enter the end (note, carried clip). `playStart` is not an end and is unchanged. The writer's matching checks (`DawProjectWrite.cpp:161,176`) were checked against the reader's new behaviour; both test `start + duration <= tick_t` max; the writer refuses that case, and the reader now cuts it to fit.
- **Qt portability.** `qRound64` (in the new test) and `qint64`/`std::numeric_limits` are Qt 5.15 and 6 compatible, with no Windows macros and no `std::filesystem`.
- **Deterministic.** The new test builds its documents in memory and parses them with `dawProjectModelFromXml()`. It has no clock, no sleep, no thread and no file I/O beyond the existing test fixture. The values (`44739241` beats, `2` and `1.6458333333` beat durations) are fixed literals, and the expected ticks are computed from them (`2147483568` = 44739241 × 48; `79` = `INT32_MAX` − that).
- **Verdict item.** Not applicable: the verdict is FIXED, not ALREADY-FIXED or FALSE-POSITIVE.
- **Pre-submit check, gates row.** `submit_check.py` reports `FAIL gates (feature_check.py)`. That row runs the stock `feature_check.py`, which stops with `PermissionError` creating `<worktree>-feature-check` outside the write-jail (`logs/gates-1.log`, exit 1). No gate ran in that run. The same gate set, run through the unmodified script's own lists by `gate-wrap.py`, passes: `logs/gates-3.log`, exit 0. Re-running the stock script needs write access to the worktree's parent folder.
- **Still open for the owner.** Whether to refuse (the finding's wording) or clamp (this fix). Refusal requires changing `hugeBeatPositionsCannotEnterTheModelOutOfTickRange`, which this lane did not do.
