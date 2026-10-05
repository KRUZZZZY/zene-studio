# midi batch-04 — SMF conductor track reader refusals

Six deduplicated findings. All six were real and are fixed, one commit each. Every
fix is a new refusal in `src/core/SmfInterchangeReader.cpp` on the
`interchange.smf_read` / `interchange.smf_import` surface; no control command was
added, removed or renamed, and no params/result schema changed. A refused import
leaves the tempo map empty (`digest(mapEvents()) == ""`).

## Summary

| Cluster | Verdict | Commit |
| --- | --- | --- |
| 5376 track truncated without End-of-Track accepted | FIXED | `270796e7c` |
| 5375 truncated MThd header extension accepted | FIXED | `2034207a1` |
| 5379 format 2 / unknown format merged as one timeline | FIXED | `ff24fb74b` (+ `4e4283f36`) |
| 5378 declared trackCount never compared | FIXED | `5e404cc47` |
| 5377 trailing partial chunk header ignored | FIXED | `01ef61b6c` |
| 5381 converted tick narrows out of tick_t range | FIXED | `200d25eb4` |

Plus one gate follow-up, `4e4283f36`: the format rejection from 5379 pushed
`parseHeader()` to CCN 12, so the new checks were moved into `validateFormat()`
and `skipHeaderExtension()` with their behaviour and messages unchanged. The rules
and outcomes are identical before and after that split.

### Cluster 5376 — A track truncated immediately after an event is accepted without End of Track

**Verdict:** FIXED

**Evidence:** `src/core/SmfInterchangeReader.cpp` `readTrack()` returned success when
its event walk ran out of bytes and nothing recorded whether the End-of-Track meta
event had arrived. A chunk truncated immediately after a complete tempo event
therefore imported as a valid conductor track, the file's silence past the event
being read as "the track ended here". The walk now tracks the event and refuses a
chunk that ends without it.

**Commit:** `270796e7c`

**Test:** `SmfInterchangeRoundTripTest::aTrackWithoutAnEndOfTrackEventIsRefused`

**Red/green:**
- red `cmake --build build --target zene SmfInterchangeRoundTripTest` → `EXIT=0`; `cd build/tests && ctest -R '^SmfInterchangeRoundTripTest$' --output-on-failure` → `EXIT=8`, `FAIL! ... '!read.ok' returned FALSE. (a track that never ends was accepted)`.
- green `cmake --build build --target zene SmfInterchangeRoundTripTest SmfInterchangeTest` → `EXIT=0`; `ctest -R '^SmfInterchangeRoundTripTest$'` → `EXIT=0`; `ctest -R '^SmfInterchangeTest$'` → `EXIT=0`; `bash tests/complexity-gate.sh --check` → `EXIT=0`; `bash tests/file-length-gate.sh --check` → `EXIT=0`.

### Cluster 5375 — Truncated extended MIDI headers can be accepted

**Verdict:** FIXED

**Evidence:** `src/core/SmfInterchangeReader.cpp` `parseHeader()` called
`cursor.skip(headerLength - 6)` and discarded the result. The chunk length is the
file's own statement of where the tracks begin, so extension bytes the file names
but does not carry left the cursor short of that point, yet parsing continued and
the file was accepted. The skip's failure is now a refusal.

**Commit:** `2034207a1`

**Test:** `SmfInterchangeRoundTripTest::aTruncatedHeaderExtensionIsRefused`

**Red/green:**
- red `cmake --build build --target zene SmfInterchangeRoundTripTest` → `EXIT=0`; `cd build/tests && ctest -R '^SmfInterchangeRoundTripTest$' --output-on-failure` → `EXIT=8`, `FAIL! ... '!read.ok' returned FALSE. (a header extension the file does not carry was accepted)`.
- green `cmake --build build --target zene SmfInterchangeRoundTripTest SmfInterchangeTest` → `EXIT=0`; `ctest -R '^SmfInterchangeRoundTripTest$'` → `EXIT=0`; `ctest -R '^SmfInterchangeTest$'` → `EXIT=0`; `bash tests/complexity-gate.sh --check` → `EXIT=0`; `bash tests/file-length-gate.sh --check` → `EXIT=0`.

### Cluster 5379 — Format 2 MIDI tracks are merged as if they play together

**Verdict:** FIXED

**Evidence:** `src/core/SmfInterchangeReader.cpp` `parseHeader()` recorded the
format field without checking it, and `mergeCandidates()` folded every track into
one tempo map. A format 2 file's tracks are independent sequences with their own
tick origins and tempos, so the first track's tick-zero tempo silently became the
tempo of all of them; an unknown format said nothing about how the tracks relate
and was accepted the same way. This import path is documented as accepting format
0 and 1 only. Both cases are now refused.

**Commit:** `ff24fb74b`, with the CCN-10 gate follow-up `4e4283f36`

**Test:** `SmfInterchangeRoundTripTest::aFormatTheReaderCannotMergeIsRefused`

**Red/green:**
- red `cmake --build build --target zene SmfInterchangeRoundTripTest` → `EXIT=0`; `cd build/tests && ctest -R '^SmfInterchangeRoundTripTest$' --output-on-failure` → `EXIT=8`, `FAIL! ... '!read.ok' returned FALSE. (SMF format 2 was accepted)`.
- green `cmake --build build --target zene SmfInterchangeRoundTripTest SmfInterchangeTest` → `EXIT=0`; `ctest -R '^SmfInterchangeRoundTripTest$'` → `EXIT=0`; `ctest -R '^SmfInterchangeTest$'` → `EXIT=0`; `bash tests/complexity-gate.sh --check` → `EXIT=0`; `bash tests/file-length-gate.sh --check` → `EXIT=0`.

### Cluster 5378 — The declared MIDI track count is not checked

**Verdict:** FIXED

**Evidence:** `src/core/SmfInterchangeReader.cpp` `parseHeader()` recorded the
header's `trackCount` and `readTracks()` counted the `MTrk` chunks it actually
found, but the two numbers were never compared. A file declaring two tracks and
carrying one succeeded with a conductor map built from half the file and nothing
in the report said a track was missing. The mismatch is now refused like any other
malformed structure.

**Commit:** `5e404cc47`

**Test:** `SmfInterchangeRoundTripTest::aFileWithFewerTracksThanItDeclaresIsRefused`

**Red/green:**
- red `cmake --build build --target zene SmfInterchangeRoundTripTest` → `EXIT=0`; `cd build/tests && ctest -R '^SmfInterchangeRoundTripTest$' --output-on-failure` → `EXIT=8`, `FAIL! ... '!read.ok' returned FALSE. (a file carrying fewer tracks than it declares was accepted)`.
- green `cmake --build build --target zene SmfInterchangeRoundTripTest SmfInterchangeTest` → `EXIT=0`; `ctest -R '^SmfInterchangeRoundTripTest$'` → `EXIT=0`; `ctest -R '^SmfInterchangeTest$'` → `EXIT=0`; `bash tests/complexity-gate.sh --check` → `EXIT=0`; `bash tests/file-length-gate.sh --check` → `EXIT=0`.

### Cluster 5377 — Trailing partial chunk headers are silently ignored

**Verdict:** FIXED

**Evidence:** `src/core/SmfInterchangeReader.cpp` `readTracks()` broke out of the
chunk loop when fewer than four bytes remained, so a file whose last chunk header
was cut short — a stray byte, or the first three bytes of an `MTrk` header —
parsed successfully and the malformed tail was dropped without a word. End of file
is the only clean end, so a tail of one to three bytes is now reported as the
truncated chunk header it is.

**Commit:** `01ef61b6c`

**Test:** `SmfInterchangeRoundTripTest::aTruncatedTrailingChunkHeaderIsRefused`

**Red/green:**
- red `cmake --build build --target zene SmfInterchangeRoundTripTest` → `EXIT=0`; `cd build/tests && ctest -R '^SmfInterchangeRoundTripTest$' --output-on-failure` → `EXIT=8`, `FAIL! ... '!read.ok' returned FALSE. (a 1-byte partial chunk header was accepted)`.
- green `cmake --build build --target zene SmfInterchangeRoundTripTest SmfInterchangeTest` → `EXIT=0`; `ctest -R '^SmfInterchangeRoundTripTest$'` → `EXIT=0`; `ctest -R '^SmfInterchangeTest$'` → `EXIT=0`; `bash tests/complexity-gate.sh --check` → `EXIT=0`; `bash tests/file-length-gate.sh --check` → `EXIT=0`.

### Cluster 5381 — Converted SMF ticks can wrap into valid LMMS positions

**Verdict:** FIXED

**Evidence:** `src/core/SmfInterchangeReader.cpp` `lmmsTickFor()` computed the
LMMS tick in `qint64` and cast it straight to `tick_t`, a signed 32-bit count
(`include/LmmsTypes.h:35`), with no range check. A file whose division let the
scaled quotient pass 2^31 wrapped it to a small nonnegative tick that later
validation accepted, placing the event near the start of the map instead of where
the file put it. The quotient is now checked against `tick_t`'s bounds and an
unrepresentable tick refuses the import; `mergeCandidates()` propagates that
refusal.

The finding's worked example (SMF tick 268,435,456, division 1) is not reachable
through the file path: it needs a five-byte VLQ and `Cursor::vlq()` reads at most
four, so the reader refuses that delta as malformed before the conversion runs. The
regression test uses 89,478,486 instead — still inside the four-byte VLQ range, but
48 × 89,478,486 = 2^32 + 32, so it exercises the same wrapping cast and narrows to
exactly tick 32.

**Commit:** `200d25eb4`

**Test:** `SmfInterchangeRoundTripTest::aTickTooWideForTheMapIsRefusedInsteadOfWrapping`

**Red/green:**
- red `cmake --build build --target zene SmfInterchangeRoundTripTest` → `EXIT=0`; `cd build/tests && ctest -R '^SmfInterchangeRoundTripTest$' --output-on-failure` → `EXIT=8`, `FAIL! ... '!read.ok' returned FALSE. (a tick too wide for the map was accepted, wrapped to another tick)`.
- green `cmake --build build --target zene SmfInterchangeRoundTripTest SmfInterchangeTest` → `EXIT=0`; `ctest -R '^SmfInterchangeRoundTripTest$'` → `EXIT=0`; `ctest -R '^SmfInterchangeTest$'` → `EXIT=0`; `bash tests/complexity-gate.sh --check` → `EXIT=0`; `bash tests/file-length-gate.sh --check` → `EXIT=0`.

Leftovers: `4e4283f36` is a gate-driven refactor of 5379's `parseHeader` and carries
no behaviour change of its own. `docs/SMF-INTERCHANGE.md` is not updated to list the
six new refusals — that documentation pass is the parent's, and this batch kept its
commits to product code, tests and this report only.