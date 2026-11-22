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

Logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/5376.r1/logs/`. The `-4` logs are this session's evidence: each command's output and its `*_EXIT=` line are in the same file, written by a shell group. The `-3` logs have no exit lines, so none of their results is cited for an exit code.

| Step | Command / source | Result | Exit |
|---|---|---|---|
| Base file list | `git diff --name-only "$BASE..HEAD"`, `BASE` = merge-base with `FIX-BASE` (`base-4.log`) | Only `docs/qwen-fixes-2026-10/cluster-5376.md` differs (report only) | `BASE_DIFF_EXIT=0` |
| Feature gates | `gates-4.log` (`feature_check.py`) | `RESULT: PASS` | `GATES_EXIT=0` |
| fix.patch generation | `patch-4.log`: `git diff 270796e7c^ 270796e7c -- src/core/SmfInterchangeReader.cpp > fix.patch` | written | `DIFF_EXIT=0` |
| fix.patch check | `revcheck-4.log`: `git apply -R --check fix.patch` on the fixed tree | passes | `CHECK_EXIT=0` |
| Revert | `revert-4.log`: `git apply -R fix.patch` | tree shows only `M src/core/SmfInterchangeReader.cpp` | `REVERT_EXIT=0` |
| Red build | `build-red-4.log` | `SmfInterchangeReader.cpp.o` recompiled; `Built target SmfInterchangeRoundTripTest` | `BUILD_RED_EXIT=0` |
| Red test | `test-red-4.log`: `FAIL! ... aTrackWithoutAnEndOfTrackEventIsRefused() '!read.ok' returned FALSE. (a track that never ends was accepted)` | `0% tests passed, 1 tests failed out of 1` | `RED_EXIT=8` |
| Restore | `restore-4.log`: `git apply fix.patch` | restored | `RESTORE_EXIT=0` |
| Clean tree | `restore-4.log`: `git diff --quiet` | tree clean | `CLEAN_EXIT=0` |
| Green build | `build-green-4.log` | `SmfInterchangeReader.cpp.o` recompiled; `Built target SmfInterchangeRoundTripTest` | `BUILD_GREEN_EXIT=0` |
| Green test | `test-green-4.log` | `100% tests passed out of 1` | `GREEN_EXIT=0` |
| Family list | `test-family-list-4.log`: `ctest -N -R '^Smf'` | `Total Tests: 3` | `LIST_EXIT=0` |
| Family | `test-family-4.log`: `ctest -R '^Smf'` | `100% tests passed out of 3` | `FAMILY_EXIT=0` |

Earlier quiet-runner re-runs from p5 (`duet/12-worker/tmp/003-p5-green.log`, `004-p5-family.log`) also passed, with `GREEN_QUIET_EXIT=0` and `FAMILY_QUIET_EXIT=0`. They are not the evidence for the rows above.

## Test matrix

| Case | Covered by | Result |
|---|---|---|
| Normal path (track ends with end-of-track) | Round-trip family | Pass: `test-family-4.log` (3/3); `FAMILY_EXIT=0` |
| Failure: tempo event, then chunk ends with no end-of-track | `aTrackWithoutAnEndOfTrackEventIsRefused` (fixture above) | Red with fix reverted: `test-red-4.log`, `RED_EXIT=8`. Green with fix restored: `test-green-4.log`, `GREEN_EXIT=0` |
| Exhaustion: chunk ends immediately after the tempo bytes | Same test and fixture | Refused with the end-of-track reason; nothing imported |
| Reconfiguration | N/A | Pure reader, no runtime reconfiguration |
| Audio thread | N/A | Reader runs on the caller's thread; no audio-thread path |
| Platforms | Code review | Plain `bool`, `QString`, `quint64`; no platform-specific calls. Qt 5.15 and MSVC compatibility was not built here. |

## Self-check

Answers to the five ASK items from `submit_check.py`. Anything not checked this session is marked unverified.

1. **Regression test fails with the change reverted and passes with it.** Red: `test-red-4.log` shows FAIL on `aTrackWithoutAnEndOfTrackEventIsRefused` (`0% tests passed, 1 tests failed out of 1`), `RED_EXIT=8`; `build-red-4.log` shows `SmfInterchangeReader.cpp.o` recompiled, `BUILD_RED_EXIT=0`. Green: `test-green-4.log`, `GREEN_EXIT=0` (100% of 1). Family: `test-family-4.log`, `FAMILY_EXIT=0` (100% of 3). All from this session.
2. **Deterministic: no wall-clock timing, sleeps or races.** This session I read the test body (`:660-674`) and the fixture (`:137-143`). The fixture is fixed bytes. The test body has no clock, sleep or thread. It writes the file under its own temp directory and calls the real `interchange.smf_read` and `interchange.smf_import` control surface. Repeated runs for flakiness were **not** done this session.
3. **Fix at the cause and complete.** The check is in the one track walk, `readTrack` (`SmfInterchangeReader.cpp:320-365`). The caller chain above (verified this session) reaches it for both `smf_read` and `smf_import`, and the regression test asserts both. This cluster's fix does not touch the note reader. `src/core/SmfNoteRead.cpp:260-268` is a separate reader that still accepts a track ending without end-of-track (verified this session by reading the code). It is listed under Other findings and is outside this cluster.
4. **No collateral damage.** The branch diff against the `FIX-BASE` merge-base lists only `docs/qwen-fixes-2026-10/cluster-5376.md` (`base-4.log`, `BASE_DIFF_EXIT=0`). The Smf family passed 3/3 (`FAMILY_EXIT=0`). This is an import reader and there are no save/load or project data changes. Other test suites were **not** run this session, so collateral in them is unverified. Whether any file in this repository's data lacks end-of-track was **not** checked.
5. **ALREADY-FIXED: the code and the contract.** Code, verified this session: `src/core/SmfInterchangeReader.cpp:320` (`bool sawEndOfTrack = false;`), `:348` (`sawEndOfTrack = true;`), `:359-365` (the `if (!sawEndOfTrack)` refusal, with the message at `:361`). Introduced by `270796e7c`, an ancestor of HEAD (`ancestor-4.log`, `ANCESTOR_EXIT=0`, this session). Contract: an SMF track is ended by its end-of-track meta event; a chunk that stops without one is truncated and must be refused. Test: `tests/src/core/SmfInterchangeRoundTripTest.cpp:660-674`, verified this session.

## Other findings

- `src/core/SmfNoteRead.cpp:260-268` still accepts a note track that ends without an end-of-track event. This is a separate reader, out of scope for cluster 5376, and was not fixed here. It needs its own cluster and its own test.
