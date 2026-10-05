# Import lane — batch 06

### Cluster 5075 — A ZIP comment can be mistaken for the end record

Verdict: FIXED

Evidence: `src/core/DawProjectZip.cpp:363` (pre-fix) scanned backwards for the
end-of-central-directory signature and accepted the first match, so a ZIP
comment (this module writes none, a foreign container may have one) ending in 22
bytes that look like an empty end record was taken as the real one. The reader
then saw `entryCount == 0`, ran no central-directory loop, and returned `true`
with an EMPTY archive — the file's real entries were dropped and the loss was
reported as success. Now `src/core/DawProjectZip.cpp:259`
(`findEndOfCentralDirectory()`) accepts a candidate only when (a) its own
comment length places its end exactly at the file end and (b) the central
directory it declares ends exactly where the record starts, which is what
separates a genuine record from an impostor that names no directory at all.
Note that check (a) alone is NOT sufficient: a fake record sitting at EOF that
declares a zero-length comment satisfies it trivially — the first attempt at the
fix still returned 0 entries for exactly this reason (verified), so (b) is what
carries the fix.

Commit: 8185125e9

Test: DawProjectInterchangeRoundTripTest::aCommentThatLooksLikeAnEndRecordIsNotOne

Red/green:
- red: `cd build/tests && ctest -R '^DawProjectInterchangeRoundTripTest$' --output-on-failure > ../bunny-t.log 2>&1` → EXIT=8, `readBack.size()` 0 vs expected 2
- green: same command → EXIT=0, `Totals: 33 passed, 0 failed`
- no regressions: `ctest -R '^(DawProjectInterchangeRoundTripTest|ControlSurfaceReferenceTest|RevisionTimelineTest|ReversibilityContractTest)$'` → EXIT=0, 4/4 passed
- gates: `bash tests/complexity-gate.sh --check` EXIT=0; `bash tests/file-length-gate.sh --check` EXIT=0 (the search had to move into `findEndOfCentralDirectory()`; left inline it pushed the grandfathered `dawProjectZipRead` from CCN 36 to 37 and the ratchet failed)

### Cluster 5074 — Valid DEFLATE-compressed DAWproject containers are refused

Verdict: SKIPPED-SCOPE

Evidence: the finding's premise does not hold. The interchange contract already
narrows the method to STORE, for writer and reader alike:
`docs/DAWPROJECT-INTERCHANGE.md:124` ("**A stored entry, not a deflated one.**
The container is a ZIP whose entries use the **STORE** method"), and
`src/core/DawProjectZip.cpp:8` states the rationale: the format constrains the
ENTRIES, not the compression method, so STORE is chosen as the interoperable
option. The quote the finding leans on ("does not constrain the compression
method") is the *writer's* justification for emitting STORE — it is not a claim
that the reader accepts any method. The reader refuses DEFLATE by name and with
a typed message at `src/core/DawProjectZip.cpp:495-502`, which is exactly the
contract, not a silent misdeclaration.

The finding offered two fixes; one is already done (narrow the advertised
contract — done, at `docs/DAWPROJECT-INTERCHANGE.md:124`) and the other is a new
feature, not a bug fix: supporting DEFLATE means a raw-DEFLATE decompressor this
tree does not carry. `DataFile.cpp`'s `qUncompress` is zlib's *framed* format
and cannot inflate a raw stream, so it cannot be reused (same reasoning recorded
at `src/core/DawProjectZip.cpp:11-14` and `src/core/CMakeLists.txt:968`) — i.e. a
new compression dependency plus a new decode path, far beyond a defect fix.

Commit: none

Test: none (no behaviour change; no test was weakened or added)

Red/green: none

Leftovers: none — cluster 5075 is fixed, tested and committed; cluster 5074 is
left as documented behaviour with no code change, as scoped.