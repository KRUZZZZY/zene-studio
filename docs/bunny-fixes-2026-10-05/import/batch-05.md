# Bunny import batch-05 — DAWproject import/export

Six deduplicated findings (v2), each verified against the tree before anything was touched.
Five were real and are fixed with a regression test each; one was already fixed on this branch.

### Cluster 5066 — The writer emits invalid model values instead of refusing them
Verdict: FIXED
Evidence: `src/core/DawProjectWrite.cpp:257` (`modelRefusal()`, aggregating `tempoRefusal()`,
`meterRefusal()`, `identityRefusal()` and the per-entity time refusals, all small helpers in the
file's anonymous namespace). Before the fix, `dawProjectXmlFromModel()` checked only the tempo
bounds and the metre denominator, so a clip at beat `-1`, a note with a NaN duration, or a tempo
point at a non-finite time were serialized and reported as a successful write — the header
(`include/DawProjectInterchange.h:447-451`) promises a refusal for "a negative time". A NaN
slipped past even the tempo range check because it compares false against both bounds. Every
serialized position (clip time/duration/playStart, note time/duration, tempo/meter point times)
and every tempo is now refused for finiteness and range before a byte is written. The checks are
helpers so `dawProjectXmlFromModel()` stays inside its CCN-42 baseline.
Commit: a23a8f258
Test: DawProjectInterchangeRoundTripTest::timesOutsideTheEngineGridAreRefusedByTheWriter
Red/green: `cmake --build build --target DawProjectInterchangeRoundTripTest zene` → EXIT=0;
`cd build/tests && ctest -R '^DawProjectInterchangeRoundTripTest$' --output-on-failure` → EXIT=8
("a clip at beat -1 was written") → same command EXIT=0 (27 passed).
Gates: complexity-gate EXIT=0, file-length-gate EXIT=0.

### Cluster 5067 — A default model can be written but cannot be read back
Verdict: FIXED
Evidence: the reader refuses `<Application>` without a name at `src/core/DawProjectRead.cpp:212`
(Project.xsd makes it required), but the writer emitted the attribute unconditionally, so
`dawProjectXmlFromModel(DawProjectModel{})` succeeded with `name=""` and produced a file this
module cannot read. The writer now refuses it for the same reason it refuses an out-of-range
tempo, via `identityRefusal()` inside `modelRefusal()`. No existing fixture needed changing:
`dawProjectModelFromSong()` sets "Zene Studio" (`src/core/DawProjectSession.cpp:236`).
Commit: 83c8fc32f
Test: DawProjectInterchangeRoundTripTest::anEmptyApplicationNameIsRefusedByTheWriter
Red/green: build EXIT=0; `ctest -R '^DawProjectInterchangeRoundTripTest$' --output-on-failure`
→ EXIT=8 → EXIT=0 (28 passed).
Gates: complexity-gate EXIT=0, file-length-gate EXIT=0.

### Cluster 5071 — The reported SHA-256 is not the hash of the output file
Verdict: FIXED
Evidence: `include/DawProjectInterchange.h:374-375` documents `DawProjectWriteReport::bytes` and
`sha256` as describing the written `.dawproject` FILE, but the report hashed only the in-memory
`projectXml` entry string. The write path now reads the file back with `QFile` and hashes those
bytes (`src/core/DawProjectWrite.cpp:620-627`), so `bytes` and `sha256` describe the same object.
Commit: 2c6e177d0
Test: DawProjectInterchangeRoundTripTest::theWriteReportHashesTheFileItWrote
Red/green: build EXIT=0; `ctest -R '^DawProjectInterchangeRoundTripTest$' --output-on-failure`
→ EXIT=8 (report digest did not match the file) → EXIT=0 (29 passed).
Gates: complexity-gate EXIT=0, file-length-gate EXIT=0.

### Cluster 4133 — Write reports always claim there were no losses
Verdict: FIXED
Evidence: the successful-write report block assigned a default-constructed `DawProjectLossReport`
unconditionally, so a file written from a model whose tracks carried `lostContentType` still
reported a clean loss report. `lossReportForModel()` (`src/core/DawProjectWrite.cpp:309`) now
counts only what the model explicitly MARKS: `lostContentType` → `unmappedTrackTypes`, and
`splitEvents` → `splitMapEvents`. `panNotWritten` and mixer sharing are deliberately NOT inferred —
a model read from a file cannot distinguish "this track type has no panning model" from "the source
document had no `<Pan>`" — and `dawproject.export` computes its own loss from
`dawProjectModelFromSong` (`src/core/ControlCommandsDawProject.cpp:354-368`), so no command result
schema moves.
Commit: 6d54e95f6
Test: DawProjectInterchangeRoundTripTest::theWriteReportCountsWhatTheModelMarksLost
Red/green: build EXIT=0 (with `lossReportForModel` stubbed to return a default report);
`ctest -R '^DawProjectInterchangeRoundTripTest$' --output-on-failure` → EXIT=8
(`unmappedTrackTypes` 0 vs 2) → with the fix, EXIT=0 (31 passed).
Gates: complexity-gate EXIT=0, file-length-gate EXIT=0.

### Cluster 4137 — Failed ZIP export can delete an existing project
Verdict: ALREADY-FIXED
Evidence: `writeWhole()` (`src/core/DawProjectZip.cpp:198-224`) writes through a `QSaveFile`: on a
short write it calls `cancelWriting()` (line 217) and returns false without touching the
destination, and on success it `commit()`s (line 220), which atomically renames the temporary over
the target. The destination is therefore never truncated before the write completes, so a failed
export cannot destroy the previous file. `QSaveFile` has been there since commit 57980beef. No
product change needed; no test added because the failure path needs a real disk-full condition that
cannot be produced headlessly and deterministically.
Commit: none
Test: none
Red/green: none

### Cluster 5073 — The ZIP writer truncates classic ZIP size fields without checking limits
Verdict: FIXED
Evidence: `dawProjectZipWrite()` cast the entry name length and the entry count into `quint16` and
each body size, the central-directory size and every offset into `quint32` with no range check, so
an entry with a 70000-byte name returned success while the central directory described different
bytes than the local records held. The writer now refuses an oversized name, body, entry count, or
assembled archive BEFORE writing anything, via `entryRefusal()`, `containerRefusal()` and
`assembledRefusal()` (`src/core/DawProjectZip.cpp:154-200`) — extracted as helpers so
`dawProjectZipWrite` stays inside its CCN-12 baseline, which a gate run confirmed. No ZIP64 record
is emitted, so refusing is the only correct answer.
Commit: 1d26449f6
Test: DawProjectInterchangeRoundTripTest::archivesPastClassicZipLimitsAreRefused
Red/green: build EXIT=0; `ctest -R '^DawProjectInterchangeRoundTripTest$' --output-on-failure`
→ EXIT=8 ("a 70000-byte entry name was narrowed into a 16-bit field and written") → EXIT=0
(32 passed). Also re-ran the other tests covering the ZIP writer:
`ctest -R '^(ProjectContainerTest|RevisionTimelineTest)$' --output-on-failure` → EXIT=0 (2/2).
Gates: complexity-gate EXIT=0, file-length-gate EXIT=0.

Leftovers: none open. Two judgement calls worth the parent's eye: (1) the 5071 read-back means a
file that cannot be re-read after a successful write turns into a failed `writeDawProject()` with a
clear error instead of a success; (2) the 4133 loss report counts only model-marked losses, so a
file-derived model reports no pan or mixer losses — that is honest rather than complete.
