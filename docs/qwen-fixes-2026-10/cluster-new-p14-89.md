FIXED: a clip or note whose endpoint passes `tick_t` is refused before it reaches the engine. Valid boundary clips are built at the origin, assigned their final length, then moved to their start; auto-resize stays off while notes are added.

# Cluster new-p14-89: clip and note end times overflow the engine tick range

## Verdict and evidence

**REAL, fixed on `bugs/qwen-new-p14-89`.** The overflow is reachable through two paths, and both are fixed.

*Reader.* Individual out-of-domain positions retain the existing clamp-and-count behavior in `src/core/DawProjectReadShared.h:237-255`. `durationWithinTickRange()` (`src/core/DawProjectReadTracks.cpp:97-104`) checks carried clip and note endpoints in a wider type and refuses one past `tick_t`.

*Apply (round 2, review rework).* The engine adds in `tick_t` in every place an imported clip passes through:
- `include/Clip.h:128-131`: `endPosition()` is `sp + m_length`, an `int` add.
- `src/tracks/InstrumentTrack.cpp:1091-1095`: `createClip()` builds the clip and calls `movePosition()`, which calls `Song::updateLength()` (`src/core/Clip.cpp:280-289`). `Track::length()` reads `endPosition()` (`src/core/Track.cpp:1250-1264`).
- `src/tracks/MidiClip.cpp:116-125`: `MidiClip::init()` runs `updateLength()` at construction, so a new clip gets a one-bar length.
- `src/tracks/MidiClip.cpp:200-205`: `addNote()` runs `updateLength()`, which rounds the latest note end up with `TimePos::nextFullBar()` (`include/TimePos.h:93`, adds `ticksPerBar - 1` in `tick_t`) and sets the length from it.

So a clip starting in the last bar overflowed at creation, and a note ending in the last bar overflowed in auto-resize, before the imported length was ever set.

## Design choices

1. **Refuse overflowing endpoints.** `applyDawProjectModel()` checks every carried clip and note endpoint before clearing the live session (`timelineEndpointsFit()`), while individual out-of-domain positions retain the reader's clamp-and-count behavior.
2. **Build at the origin, move last (round 2).** `importClip()` creates the clip at 0, so no default-length sum is taken at a late start. `placeImportedClip()` sets the final length, then moves the clip to its start. The start and length are held to the range, so the final sum fits.
3. **Auto-resize off while notes are added (round 2).** `addImportedNotes()` turns auto-resize off, so `updateLength()` never rounds a note end near the top of the range in `tick_t`. `autoLengthForNotes()` computes the equivalent length in `qint64` and it is held to the available clip range.
4. **Same final state for in-range documents.** With auto-resize off, the engine's own updates are replaced by their results: a document length wins, otherwise the note-driven length (the same rounding the engine does, in `qint64`). The engine's start-offset reset, which runs when notes are added, is kept (`:288`). That reset is pre-existing behaviour: it erases a `playStart` on a clip that has notes. This change keeps it and does not fix it; it is an observation for the owner.
5. **Zero-duration clip with notes (round 2).** It takes the note-driven length, held to the room after its start. Before this change the same clip overflowed at creation.

## Changes

- `src/core/DawProjectSession.cpp`: helpers `LastTick`, `NoteSpan`, `noteSpan()`, `autoLengthForNotes()`, `addImportedNotes()`, `placeImportedClip()`, `importClip()` (`:226-320`); `applyDawProjectModel`'s clip loop calls `importClip()` (`:672`); includes `<algorithm>` and `<limits>`.
- `tests/src/core/DawProjectInterchangeRoundTripTest.cpp`: `readerRefusesATimelineEndpointPastTheTickRange()` (`:2122`, round 1, model level) and `appliedBoundaryEndpointsStayInTheTickRange()` (`:2181`, round 2, applies through `applyDawProjectModel` into the engine). The apply test counts, through `Song::lengthChanged`, every clip whose start or length leaves the tick range during the import, so a transient overflow is caught even when the final values are right. It then checks the final clip and note positions for three fixtures: a clip 79 ticks short of the end, a note ending on the last tick, and a zero-duration clip 79 ticks short of the end.
- No header, no new file, no control command, no audio-thread code. Both files are fork-new and already registered (`tests/fork-sources.txt`, `tests/all-sources.txt:2732`).

## Test matrix

| Path | Case | Covered by |
| --- | --- | --- |
| Model, boundary clip | overflowing endpoint refused; exact fit kept | `readerRefusesATimelineEndpointPastTheTickRange` |
| Model, boundary note | overflowing note endpoint refused; exact fit kept | same test |
| Model, exact fit | end at `tick_t` max unchanged, nothing counted | same test |
| Model, uncarried clip | clip without `<Notes>` is counted as skipped | same test |
| Apply, invalid endpoints | invalid direct models refused before session mutation | `appliedBoundaryEndpointsStayInTheTickRange` |
| Apply, exact-fit clip | creation and move stay in range | same test, fixture 1 |
| Apply, exact-fit note | note endpoint stays in range | same test, fixture 2 |
| Apply, zero-duration boundary clip | length held to room | same test, fixture 3 |
| In-range round trips | 60 existing tests unchanged | whole binary |
| Clamped start (existing) | `hugeBeatPositions...` still imports | existing test |
| Writer | still refuses overflowing ends | existing writer end-point tests |
| Audio thread / undo / commands | not applicable: import runs on the calling thread; no command or undo path | n/a |
| Platform | `std::clamp`, `qRound64`, `qint64`; no Windows macros | n/a |

A caveat on the RED evidence: `QTest` stops at the first failing function's first failing assertion, so the reversed-product run fails on fixture 1. Fixtures 2 and 3 pass on the rework (GREEN) but were not separately shown RED on the old code.

## Commands and exit codes

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-89.r2/logs/`.

| # | What | Log | EXIT |
| --- | --- | --- | --- |
| 1-6 | Test-target builds and runs during the work: compile errors in the new test (`clipVector`, `.first()` on `std::vector`, `size_t` against `int`) and then a stale binary from the failed builds. The `test-1..6` results used the stale binary and are not evidence. | `build-1..6-*.log`, `test-1..6-*.log` | build 2 each; tests not evidence |
| 7 | Build with the test fixed, product reverted (committed code) | `build-7-red.log` | 0 |
| 8 | `ctest -R DawProjectInterchangeRoundTripTest -V`, product reverted | `test-7-red.log` | **8**: `appliedBoundaryEndpointsStayInTheTickRange` fails, `1 engine length states left the tick range` (fixture 1) |
| 9 | Build, product restored | `build-8-green.log` | 0 |
| 10 | `ctest -R DawProjectInterchangeRoundTripTest -V`, product restored | `test-8-green.log` | **0**: 62 passed, 0 failed |
| 11 | **4b proof on the final patch.** `git diff -- src/core/DawProjectSession.cpp > fix2-final.patch`; `git apply -R`; build; ctest | `build-11-red-proof.log`, `test-11-red-proof.log` | diff 0, revert 0, build 0, **test 8** (1 failed, 61 passed) |
| 12 | `git apply fix2-final.patch`; build; ctest | `build-12-green-proof.log`, `test-12-green-proof.log` | apply 0, build 0, **test 0** (62 passed) |
| 13 | Final tree: `cmake --build build --target zene DawProjectInterchangeRoundTripTest DawProjectDeflateImportTest -j4` | `build-10-final.log` | 0 |
| 14 | `ctest -R DawProject -V` (family) | `test-10-family-final.log` | **0**: `DawProjectInterchangeRoundTripTest` 62/62, `DawProjectDeflateImportTest` 11/11 |
| 15 | `git diff --check` | (inline) | 0 |

Every build used `CCACHE_DISABLE=1`: ccache could not write its temp directory (`Permission denied`).

### Gates

Gates were run **directly**, with exit codes in `logs/gates/`:

| Gate | EXIT | Note |
| --- | --- | --- |
| fork-sources | 0 | |
| all-sources-reproduce | 0 | |
| no-upstream-regression | 0 | |
| check-namespace | 0 | |
| no-tautology | 0 | |
| doc-figures | 0 | |
| complexity | 1 | `REGRESSION: new function over target: parseNotes@src/core/DawProjectReadTracks.cpp (CCN 13)`. Pre-existing, not from this change: lizard gives `parseNotes` CCN 13 at the new base `5c1c6715` and at HEAD, and no edit to its body. The cached train-18 listing (`gate-baselines/7641e9493d….json`) already lists it, so the stock gate treats it as not new. Its absence from `tests/complexity-baseline.tsv` is a train-level problem. |
| file-length | 1 | 22 items, none in a file this change touched (`logs/gates/file-length.log`). |

**Stock `feature_check.py` could not complete.** `HEAD` now includes the orchestrator's merge `915e4fb23d` of `050/m0-train-19`, so the merge-base moved to `5c1c6715`. Its base listing is not cached, and computing it runs `git checkout` in `gate-baselines/tree`, which the Bunny-lane guard refuses (`bunny-guard: … is not allowed in a Bunny lane`). The wrapper `gate-wrap.py` crashed there (`logs/gates-1.log` of round 2 is not a pass). Someone outside the lane should run the stock gate on the merged tree to confirm the complexity and file-length rows.

For this change the relevant measurements are:
- `applyDawProjectModel`: CCN 32 at the base (lizard), 33 in the baseline TSV, 24 now. The ratchet only fails on a rise.
- The new helpers (`noteSpan`, `autoLengthForNotes`, `addImportedNotes`, `placeImportedClip`, `importClip`) do not appear in the complexity log, so each is at or under CCN 10.

## Self-check

- **Each new test FAILS with the product change reverted and PASSES with it.** Yes: the reversed run fails on the boundary clip (`test-11-red-proof.log`, EXIT 8). The restored run passes, 62 of 62 (`test-12-green-proof.log`, EXIT 0). The caveat above applies to fixtures 2 and 3.
- **Acceptance criteria.** The brief lists no per-criterion table. The review's blocking items are answered: (1) the boundary clip is created at the origin, its length set, then moved (`importClip`, `placeImportedClip`); (2) the boundary note's auto-resize is off during insertion and the engine's rounding is done in `qint64` (`addImportedNotes`, `autoLengthForNotes`). The regression exercises both through `applyDawProjectModel`.
- **Deterministic.** Fixed literals, no clock, no sleep, no thread. The observer is `Song::lengthChanged`, emitted synchronously on the calling thread, and it reads positions and lengths in `qint64`.
- **Verdict item.** Not applicable: the verdict is FIXED.
- **Undo and commands.** No control command is added or changed. The import is not an undo step.
- **Audio thread.** Nothing added runs on the audio thread. `applyDawProjectModel` runs on the calling thread (a control handler or a project load). None of `processImpl`, `playImpl`, `renderPeriod`, `NotePlayHandle` or `AudioEngine::` appears in the diff.
- **Old projects still load and render exactly as before.** Documents whose ends fit produce the same final clip position, length, offset and notes as before (Design choice 4). The 60 other tests in the binary pass unchanged. The writer is untouched.
- **Callers and siblings.** `createClip` is called from the import in one place (`importClip`). The reader's end cut from round 1 is unchanged. `noteSpan()` feeds both the note creation and the auto length, so the two cannot disagree.
- **Qt portability.** `std::clamp`, `qRound64`, `qint64` are available on Qt 5.15 and 6, with no Windows macros and no `std::filesystem`.
- **Still open for the owner.** The pre-existing erasure of `playStart` on clips that have notes (Design choice 4), which this change keeps.
