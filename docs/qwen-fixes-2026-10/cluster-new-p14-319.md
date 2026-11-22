FIXED

# cluster new-p14-319 - reading a large SMF can exhaust memory

## Verdict: REAL
- Before the fix, `readConductorTrack()` (src/core/SmfInterchangeReader.cpp, `const QByteArray bytes = file.readAll();` in HEAD) read the
  whole file with no size check; `readAllTracks()` then built one `Candidate` per tempo/metre event.
  The sibling note reader already caps input (include/SmfNoteRead.h:45 `SmfNoteMaxBytes` = 16 MiB, src/core/SmfNoteRead.cpp:343).
- Callers: src/core/ControlCommandsInterchange.cpp:329 and :405 (interchange.smf_read / import) both go through readConductorTrack, so one fix covers both.

## Fix
- include/SmfInterchange.h: `SmfMaxReadBytes` = 16 MiB (documented) and `readBoundedFile()` declaration.
- src/core/SmfInterchange.cpp: `readBoundedFile()` opens the file, refuses when `size()` > limit, and caps the read itself at limit+1 bytes
  (size() can lie for growing files); error names the limit. Nothing beyond the cap is read.
- src/core/SmfInterchangeReader.cpp: `readConductorTrack()` calls it (moved there, not inlined, because the reader file sits exactly at the 500-code-line ratchet).
- docs/SMF-INTERCHANGE.md section 4: the limit is stated.
- Candidate storage is bounded transitively: every candidate needs at least a 6-byte tempo meta event in <= 16 MiB of input.
- No audio-thread code, no control command added, undo untouched. Files up to the limit read exactly as before.

## Regression test
tests/src/core/SmfInterchangeTest.cpp `aFileBeyondTheSizeLimitIsRefusedUnread`: a well-formed file padded by an unknown meta event.
Exactly 16 MiB reads (1 event); 16 MiB + 1 byte is refused with an error containing "16 MiB". Deterministic, no timing.

## Commands and results
- RED (test added, no fix): `cd build/tests && ctest -R '^SmfInterchangeTest$' --output-on-failure` -> EXIT=8, log test-1-red.log:
  `FAIL! : SmfInterchangeTest::aFileBeyondTheSizeLimitIsRefusedUnread() '!refused.ok' returned FALSE`
- GREEN: same command with fix -> EXIT=0 (test-6-green.log: SmfInterchangeTest, SmfInterchangeRoundTripTest, SmfNoteReadTest all Passed).
- 4b: `git diff -- include/SmfInterchange.h src/core/SmfInterchange.cpp src/core/SmfInterchangeReader.cpp > logs/fix.patch && git apply -R logs/fix.patch` (EXIT 0),
  rebuild EXIT=0, ctest RED_EXIT=8 (test-7-revert.log, same FAIL line); `git apply logs/fix.patch` (EXIT 0), rebuild EXIT=0, ctest GREEN_EXIT=0 (test-8-reapply.log).
- Builds: `cmake --build build --target zene SmfInterchangeTest SmfInterchangeRoundTripTest SmfNoteReadTest midiexport -j4` EXIT=0 (build-7.log).
- Gates: `feature_check.py <worktree>` EXIT=0, RESULT: PASS on all 8 gates (gates-2.log).

## Not verified
- No local TSan; the change is file I/O on the control/UI thread only. A file that grows during the read is capped by the limit+1 read.
- Other tests including SmfInterchange.h (e.g. ClipImportFileTest, DawProjectInterchangeRoundTripTest) were not built or run.
