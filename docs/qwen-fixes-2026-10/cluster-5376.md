ALREADY-FIXED

# Cluster 5376: truncated SMF track without end-of-track is refused

Verdict: **ALREADY-FIXED**. The defect (an SMF track whose chunk ends right after its last event, with no end-of-track meta event, was read as a complete track) was fixed in commit `270796e7c` ("fix(midi): a track truncated after its last event is refused, not read as ended"). That commit is an ancestor of HEAD. The current code is the same fix; this report records it.

Why this report exists: the earlier patrol re-reported a bug that was already fixed. No report for cluster 5376 was on the branch, so the patrol had no record of the fix and filed it again. This document is that record.

## Where the fix is

- `src/core/SmfInterchangeReader.cpp:320`: `bool sawEndOfTrack = false;` is set per track.
- `src/core/SmfInterchangeReader.cpp:348`: `sawEndOfTrack = true;` when the walk reads an end-of-track event.
- `src/core/SmfInterchangeReader.cpp:359-365`: if the walk stops because the chunk ran out of bytes and `sawEndOfTrack` is still false, the track is refused. The refusal message is at `:361`: "a track ends without an end-of-track meta event; ... the chunk was truncated after its last event rather than complete".
- Regression test: `tests/src/core/SmfInterchangeRoundTripTest.cpp:660-674`, `aTrackWithoutAnEndOfTrackEventIsRefused()`. It asserts that `interchange.smf_read` refuses the file with `InvalidArgs` and an error containing "end-of-track", and that `interchange.smf_import` imports nothing (`digest(mapEvents()) == QString()`).

## Commands and exit codes (from the logs)

Logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/5376.r1/logs/`.

| Step | Command | Log | Exit |
|---|---|---|---|
| Red (regression test, product change reverted) | QTest run of `SmfInterchangeRoundTripTest` | `test-2-red.log` | `EXIT=8` (line 37). Line 19: `FAIL! : ...aTrackWithoutAnEndOfTrackEventIsRefused() '!read.ok' returned FALSE. (a track that never ends was accepted)`. Line 26: `Totals: 17 passed, 1 failed` |
| Green (with fix) | ctest, `SmfInterchangeRoundTripTest` | `test-3-green.log` | `EXIT=0` (line 8; "100% tests passed, 0 failed out of 1") |
| Family | ctest, `SmfInterchangeTest`, `SmfInterchangeRoundTripTest`, `SmfNoteReadTest` | `test-4-family.log` | `EXIT=0` (line 12; "100% tests passed, 0 failed out of 3") |
| Feature gates | `feature_check.py` | `gates-2.log` | `RESULT: PASS` (line 10), `EXIT=0` (line 11) |

The red run's build and revert steps are in `build-2-red.log`. The revert procedure itself was run in the earlier round; this document does not re-derive it.

## Test matrix

| Case | Covered by | Result |
|---|---|---|
| Normal path (track ends with end-of-track) | Round-trip family | Pass: `test-3-green.log`, `test-4-family.log` (3/3) |
| Failure: tempo event, then chunk ends with no end-of-track | `aTrackWithoutAnEndOfTrackEventIsRefused` | Refused with the end-of-track reason; red before fix, green after |
| Exhaustion: chunk ends immediately after the tempo bytes | Same test (the fixture `trackWithoutEndOfTrack()` is taken to hold a tempo event and no end-of-track; not re-read here) | Refused; nothing imported |
| Reconfiguration | N/A | Pure reader, no runtime reconfiguration |
| Audio thread | N/A | Reader runs on the caller's thread; no audio-thread path |
| Platforms | Code review | Plain `bool`, `QString`, `quint64`; no platform-specific calls. Qt 5.15 and MSVC compatibility was not built here. |

## Self-check

Answers to the five ASK items from `submit-1.log`.

1. **Regression test fails with the change reverted and passes with it.** Red: `test-2-red.log` line 19 FAIL on `aTrackWithoutAnEndOfTrackEventIsRefused`, line 26 totals 17 passed / 1 failed, `EXIT=8`. Green: `test-3-green.log`, `EXIT=0`, 1/1 passed. Family: `test-4-family.log`, `EXIT=0`, 3/3 passed.
2. **Deterministic: no wall-clock timing, sleeps or races.** The test writes fixed bytes to a file under its own temp directory (`writeFile`, `path(m_directory, ...)`), runs the two control calls synchronously, and checks the result. There is no clock, sleep, or thread in the test body. The byte fixture `trackWithoutEndOfTrack()` is defined elsewhere in the file and was not re-read for this report.
3. **Fix at the cause and complete.** The check is in the one track walk (`SmfInterchangeReader.cpp:320-365`), so both `smf_read` and `smf_import` see the refusal. A sibling reader for note tracks, `src/core/SmfNoteRead.cpp:260-268`, still accepts a track that ends without end-of-track. That is a separate reader and is outside this cluster; it is recorded under Other findings and not changed here. So the claim "every sibling is consistent" is **not** true for the note reader.
4. **No collateral damage.** The SMF format requires an end-of-track event on every track, so a well-formed file loads unchanged; the green and family runs cover the round-trip files. A file written without end-of-track by an older tool is now refused by the interchange reader. That is the intended behaviour of the fix. Whether any saved project data in this repository has such a track was **not** checked in this session.
5. **ALREADY-FIXED: the code and the contract.** Code: `src/core/SmfInterchangeReader.cpp:320`, `:348`, `:359-365` (refusal at `:361`), introduced by `270796e7c` (ancestor of HEAD). Contract: an SMF track is ended by its end-of-track meta event; a chunk that stops without one is truncated and must be refused. Test: `tests/src/core/SmfInterchangeRoundTripTest.cpp:660-674`.

## Other findings

- `src/core/SmfNoteRead.cpp:260-268` still accepts a note track that ends without an end-of-track event. This is out of scope for cluster 5376 and was not fixed here. It needs its own cluster and its own test.
