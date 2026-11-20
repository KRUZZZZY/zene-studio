ALREADY-FIXED

# Cluster 5376: truncated SMF track without end-of-track is refused

Verdict: **ALREADY-FIXED**. The defect (an SMF track whose chunk ends right after its last event, with no end-of-track meta event, was read as a complete track) was fixed in commit `270796e7c` ("fix(midi): a track truncated after its last event is refused, not read as ended"). That commit is an ancestor of HEAD. The current code is the same fix; this report records it.

Why this report exists: the earlier patrol re-reported a bug that was already fixed. No report for cluster 5376 was on the branch, so the patrol had no record of the fix and filed it again. This document is that record.

## Where the fix is

- `src/core/SmfInterchangeReader.cpp:320`: `bool sawEndOfTrack = false;` is set per track.
- `src/core/SmfInterchangeReader.cpp:348`: `sawEndOfTrack = true;` when the walk reads an end-of-track event.
- `src/core/SmfInterchangeReader.cpp:359-365`: if the walk stops because the chunk ran out of bytes and `sawEndOfTrack` is still false, the track is refused. The refusal message is at `:361`: "a track ends without an end-of-track meta event; ... the chunk was truncated after its last event rather than complete".
- Regression test: `tests/src/core/SmfInterchangeRoundTripTest.cpp:660-674`, `aTrackWithoutAnEndOfTrackEventIsRefused()`. It asserts that `interchange.smf_read` refuses the file with `InvalidArgs` and an error containing "end-of-track", and that `interchange.smf_import` imports nothing (`digest(mapEvents()) == QString()`).

## Caller chain

`readConductorTrack` (`SmfInterchangeReader.cpp:637`) -> `readAllTracks` (`:611`) -> `readTracks` (`:459`, called at `:619`) -> `readTrack` (`:311`, called at `:495`, which holds the walk at `:320-365`).

`readAllTracks` keeps the local `candidates` vector (`:617`) and merges it (`mergeCandidates`, `:629`) only after `readTracks` succeeds (`:619`). So a refusal returns before the merge and imports nothing.

## Fixture

`trackWithoutEndOfTrack()` at `tests/src/core/SmfInterchangeRoundTripTest.cpp:137-143`:

- delta 0 (`appendVlq(body, 0)`),
- meta `0x51` with length 3, bytes `07 A1 20` (= 500000 µs per quarter note = 120 bpm),
- then the chunk ends, with no end-of-track event. `smfFileWithBody(body)` wraps it as a complete file.

## Commands and exit codes

All commands are from `redproof-4b.log` in `/home/kruzzzzy/.config/kads/zene/state/runs/5376.r1/logs/`, run in order, each echoed as `CMD:` and exited as `*_EXIT=` in that log. The reverse diff was the fix patch `git diff 270796e7c^ 270796e7c -- src/core/SmfInterchangeReader.cpp`, which is 1 file, 15 deletions.

| Step | Command (verbatim from the log) | Exit |
|---|---|---|
| Generate fix patch | `git diff 270796e7c^ 270796e7c -- src/core/SmfInterchangeReader.cpp > $L/fix.patch` | `DIFF_EXIT=0` |
| Check reverse apply | `git apply -R --check $L/fix.patch` | `CHECK_EXIT=0` |
| Revert fix | `git apply -R $L/fix.patch` | `REVERT_EXIT=0` |
| Red build | `cmake --build build --target zene SmfInterchangeRoundTripTest` | `BUILD_RED_EXIT=0` |
| Red test | `ctest -R '^SmfInterchangeRoundTripTest$' --output-on-failure` | `RED_EXIT=8` |
| Restore fix | `git apply $L/fix.patch` | `RESTORE_EXIT=0` |
| Tree clean after restore | `git diff --quiet` | `CLEAN_EXIT=0` |
| Green build | `cmake --build build --target zene SmfInterchangeTest SmfInterchangeRoundTripTest SmfNoteReadTest` | `BUILD_GREEN_EXIT=0` |
| Green test | `ctest -R '^SmfInterchangeRoundTripTest$' --output-on-failure` | `GREEN_EXIT=0` |
| Family | `ctest -R '^Smf' --output-on-failure` | `FAMILY_EXIT=0` |

Red result (from `redproof-4b.log`): `FAIL! : SmfInterchangeRoundTripTest::aTrackWithoutAnEndOfTrackEventIsRefused() '!read.ok' returned FALSE. (a track that never ends was accepted)`; `Totals: 17 passed, 1 failed`; `0% tests passed, 1 tests failed out of 1`.

Green result: `100% tests passed out of 1`. Family result: `100% tests passed out of 3`.

The feature-gate result (`gates-2.log`: `RESULT: PASS`, `EXIT=0`) is from an earlier run and was not re-run in this cycle.

## Test matrix

| Case | Covered by | Result |
|---|---|---|
| Normal path (track ends with end-of-track) | Round-trip family | Pass: `redproof-4b.log` (green, family 3/3) |
| Failure: tempo event, then chunk ends with no end-of-track | `aTrackWithoutAnEndOfTrackEventIsRefused` (fixture above) | Red with fix reverted (`RED_EXIT=8`), green with fix restored (`GREEN_EXIT=0`) |
| Exhaustion: chunk ends immediately after the tempo bytes | Same test and fixture | Refused with the end-of-track reason; nothing imported |
| Reconfiguration | N/A | Pure reader, no runtime reconfiguration |
| Audio thread | N/A | Reader runs on the caller's thread; no audio-thread path |
| Platforms | Code review | Plain `bool`, `QString`, `quint64`; no platform-specific calls. Qt 5.15 and MSVC compatibility was not built here. |

## Self-check

Answers to the five ASK items from `submit-1.log`.

1. **Regression test fails with the change reverted and passes with it.** Red: `redproof-4b.log`, `RED_EXIT=8`, FAIL on `aTrackWithoutAnEndOfTrackEventIsRefused`, totals 17 passed / 1 failed. Green: `GREEN_EXIT=0`, 1/1 passed. Family: `FAMILY_EXIT=0`, 3/3 passed.
2. **Deterministic: no wall-clock timing, sleeps or races.** The test writes fixed bytes to a file under its own temp directory (`writeFile`, `path(m_directory, ...)`), runs the two control calls synchronously, and checks the result. There is no clock, sleep, or thread in the test body. The byte fixture is at `tests/src/core/SmfInterchangeRoundTripTest.cpp:137-143` (see Fixture).
3. **Fix at the cause and complete.** The check is in the one track walk (`readTrack`, `SmfInterchangeReader.cpp:320-365`), which the caller chain above reaches for both `smf_read` and `smf_import`; the regression test asserts both. This cluster's fix does not touch the note reader. `src/core/SmfNoteRead.cpp:260-268` is a separate reader for note tracks and still accepts a track that ends without end-of-track. It is listed under Other findings below and is outside this cluster.
4. **No collateral damage.** This is an import reader. The change touches only `src/core/SmfInterchangeReader.cpp` (see the diff stat above), and there are no save/load format or project data changes, so old projects load as before. The SMF format requires an end-of-track event on every track, so a well-formed file loads unchanged; the green and family runs cover the round-trip files. A file written without end-of-track by an older tool is now refused by the interchange reader. That is the intended behaviour of the fix. Whether any file of that kind exists in this repository's data was **not** checked in this session.
5. **ALREADY-FIXED: the code and the contract.** Code: `src/core/SmfInterchangeReader.cpp:320`, `:348`, `:359-365` (refusal at `:361`), introduced by `270796e7c` (ancestor of HEAD). Contract: an SMF track is ended by its end-of-track meta event; a chunk that stops without one is truncated and must be refused. Test: `tests/src/core/SmfInterchangeRoundTripTest.cpp:660-674`.

## Other findings

- `src/core/SmfNoteRead.cpp:260-268` still accepts a note track that ends without an end-of-track event. This is a separate reader, out of scope for cluster 5376, and was not fixed here. It needs its own cluster and its own test.
