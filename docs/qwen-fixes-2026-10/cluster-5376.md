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

`readConductorTrack` (`SmfInterchangeReader.cpp:637`) calls `readAllTracks` at `:649`. `readAllTracks` (defined `:611`) calls `readTracks` at `:619`. `readTracks` (defined `:459`) calls `readTrack` at `:495`. `readTrack` (defined `:311`) holds the walk at `:320-365`.

`readAllTracks` keeps the local `candidates` vector (`:617`) and merges it (`mergeCandidates`, `:629`) only after `readTracks` succeeds (`:619`). So a refusal returns before the merge and imports nothing.

## Fixture

`trackWithoutEndOfTrack()` at `tests/src/core/SmfInterchangeRoundTripTest.cpp:137-143`:

- delta 0 (`appendVlq(body, 0)`),
- meta `0x51` with length 3, bytes `07 A1 20` (= 500000 µs per quarter note = 120 bpm),
- then the chunk ends, with no end-of-track event. `smfFileWithBody(body)` wraps it as a complete file.

## Evidence from this session

Logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/5376.r1/logs/` unless a path is given. The `-3` logs record test output but not exit codes. Exit codes below are only those seen this session.

| Step | Command / source | Result | Exit |
|---|---|---|---|
| Base measurement | `git diff --stat fa4a5f2e78 HEAD` (run before this commit) | 1 file changed, 76 insertions: this report only | `DIFF_BASE_EXIT=0` |
| Feature gates | `gates-3.log` | 8 gates PASS, `RESULT: PASS` | not logged (RESULT: PASS only) |
| fix.patch generation | `git diff 270796e7c^ 270796e7c -- src/core/SmfInterchangeReader.cpp`, regenerated and compared with saved `fix.patch` | byte-identical (`PATCH_SAME`) | `REGEN_EXIT=0` |
| fix.patch check | `git apply -R --check fix.patch` on the fixed tree | 1 file, 15 insertions, 0 deletions | `REVCHECK_EXIT=0` |
| Revert and red build | `build-red-3.log` | `Built target SmfInterchangeRoundTripTest` | not logged |
| Red test | `test-red-3.log`: `FAIL! ... aTrackWithoutAnEndOfTrackEventIsRefused() '!read.ok' returned FALSE. (a track that never ends was accepted)`, `Loc: SmfInterchangeRoundTripTest.cpp(666)` | `Totals: 17 passed, 1 failed`; `0% tests passed, 1 tests failed out of 1` | not logged |
| Restore | `git apply fix.patch` | not logged this session | not logged |
| Green build | `build-green-3.log` | `Linking CXX executable`, `Built target SmfInterchangeRoundTripTest` | not logged |
| Green test | `test-green-3.log` | `100% tests passed out of 1` | not logged |
| Green test, re-run | `ctest -R '^SmfInterchangeRoundTripTest$'` from `build/tests`, via quiet runner (`duet/12-worker/tmp/003-p5-green.log`) | `100% tests passed out of 1` | `GREEN_QUIET_EXIT=0` |
| Family list | `test-family-list-3.log` | 3 tests: #409 SmfInterchangeTest, #410 SmfInterchangeRoundTripTest, #705 SmfNoteReadTest | not logged |
| Family | `test-family-3.log`; re-run `ctest -R '^Smf'` via quiet runner (`duet/12-worker/tmp/004-p5-family.log`) | `100% tests passed out of 3` | `FAMILY_QUIET_EXIT=0` |

The red run's exit code is not in `test-red-3.log`. `RED_EXIT=8` appears only in `redproof-4b.log` (earlier session, 19:00). That value is not cited as this session's red result.

## Test matrix

| Case | Covered by | Result |
|---|---|---|
| Normal path (track ends with end-of-track) | Round-trip family | Pass: `test-family-3.log` (3/3); `FAMILY_QUIET_EXIT=0` |
| Failure: tempo event, then chunk ends with no end-of-track | `aTrackWithoutAnEndOfTrackEventIsRefused` (fixture above) | Red with fix reverted: `test-red-3.log` (17 passed, 1 failed). Green with fix restored: `test-green-3.log`; `GREEN_QUIET_EXIT=0` |
| Exhaustion: chunk ends immediately after the tempo bytes | Same test and fixture | Refused with the end-of-track reason; nothing imported |
| Reconfiguration | N/A | Pure reader, no runtime reconfiguration |
| Audio thread | N/A | Reader runs on the caller's thread; no audio-thread path |
| Platforms | Code review | Plain `bool`, `QString`, `quint64`; no platform-specific calls. Qt 5.15 and MSVC compatibility was not built here. |

## Self-check

Answers to the five ASK items from `submit-1.log`. Anything not checked this session is marked unverified.

1. **Regression test fails with the change reverted and passes with it.** Red: `test-red-3.log` shows FAIL on `aTrackWithoutAnEndOfTrackEventIsRefused` (17 passed / 1 failed, `0% tests passed`). The red run's exit code is not logged this session, so the red exit is **unverified this session**. Green: `GREEN_QUIET_EXIT=0` (1/1 passed, this session). Family: `FAMILY_QUIET_EXIT=0` (3/3 passed, this session).
2. **Deterministic: no wall-clock timing, sleeps or races.** This session I read the test body (`:660-674`) and the fixture (`:137-143`). The fixture is fixed bytes. The test body has no clock, sleep or thread. It writes the file under its own temp directory and calls the real `interchange.smf_read` and `interchange.smf_import` control surface. Repeated runs for flakiness were **not** done this session.
3. **Fix at the cause and complete.** The check is in the one track walk, `readTrack` (`SmfInterchangeReader.cpp:320-365`). The caller chain above (verified this session) reaches it for both `smf_read` and `smf_import`, and the regression test asserts both. This cluster's fix does not touch the note reader. `src/core/SmfNoteRead.cpp:260-268` is a separate reader that still accepts a track ending without end-of-track (verified this session by reading the code). It is listed under Other findings and is outside this cluster.
4. **No collateral damage.** The branch diff against `fa4a5f2e78` is this report only (`DIFF_BASE_EXIT=0`, 1 file, 76 insertions, measured before this commit). The Smf family passed 3/3. This is an import reader and there are no save/load or project data changes. Other test suites were **not** run this session, so collateral in them is unverified. Whether any file in this repository's data lacks end-of-track was **not** checked.
5. **ALREADY-FIXED: the code and the contract.** Code, verified this session: `src/core/SmfInterchangeReader.cpp:320` (`bool sawEndOfTrack = false;`), `:348` (`sawEndOfTrack = true;`), `:359-365` (the `if (!sawEndOfTrack)` refusal, with the message at `:361`). Introduced by `270796e7c`, an ancestor of HEAD (`ANCESTOR_EXIT=0`). Contract: an SMF track is ended by its end-of-track meta event; a chunk that stops without one is truncated and must be refused. Test: `tests/src/core/SmfInterchangeRoundTripTest.cpp:660-674`, verified this session.

## Other findings

- `src/core/SmfNoteRead.cpp:260-268` still accepts a note track that ends without an end-of-track event. This is a separate reader, out of scope for cluster 5376, and was not fixed here. It needs its own cluster and its own test.
