# Control lane — batch 05 (2026-10-05)

Six findings, five fixed. The sixth (repeated hashing of the same resolved file)
is real in the code but has no observable headless failure, so it is recorded
REAL-UNTESTED and left alone.

### Cluster 4063 — Malformed rack macro and zone IDs get the wrong error kind

Verdict: FIXED

Evidence: `src/core/ControlRackSupport.cpp:157-178` — `resolveMacroIndex` and
`resolveZoneIndex` each collapsed a parse failure and an absent index into the
same `NotFound`, so `macro-x` (not a `macro-<n>` id at all) was answered
not_found. `resolveRackChannel` in the same file (~line 54) already splits the
two, returning `InvalidArgs` for a malformed `ch-<n>`, so the fix brought the
macro and zone resolvers in line with that precedent rather than inventing a new
shape. Both resolvers now report `InvalidArgs` for a malformed id and keep
`NotFound` for a well-formed id that is simply absent; the regression test pins
both halves so the fix cannot over-correct in the other direction.

Commit: df634d4fb

Test: RackMacrosTest::malformedMacroIdIsInvalidArgs

Red/green: `cd build/tests && ctest -R '^RackMacrosTest$' --output-on-failure`
EXIT=8 (9 passed, 1 failed; `malformedMacro.errorKind` 1 = NotFound, expected
3 = InvalidArgs) → EXIT=0 after the fix. Related coverage rebuilt and run:
`ctest -R '^(RackTest|RackZonesTest|RackZoneRoutingTest|RackMacrosTest)$'`
EXIT=0 (4/4 passed). Gates: complexity EXIT=0, file-length EXIT=0.

### Cluster 4974 — Empty asset paths are counted as embedded media

Verdict: FIXED

Evidence: `src/core/ControlProjectAssets.cpp:255` (pre-fix) — `collectReferences`
incremented `embeddedCount` for ANY recognised element carrying an empty path
attribute, including `sf2player` and the session `clip`, which `kAssetElements`
(lines 80-85) gives no `embedAttribute` at all. The increment is now gated on
`spec.embedAttribute[0] != '\0' && element.hasAttribute(...)` — the same test
`fillReference` already used to decide `ref.embedded` — so an empty-path
element counts as inline media only when its element type actually has an
inline form and that media is present. Such elements remain unlisted and are not
reported as missing, matching the pre-existing "names no file" comment.

Commit: 2fa5957a7

Test: ControlProjectArchiveTest::onlyInlineMediaCountsAsEmbedded

Red/green: `cd build/tests && ctest -R '^ControlProjectArchiveTest$' --output-on-failure`
EXIT=8 (13 passed, 1 failed; `embedded_count` 3, expected 1) → EXIT=0 (14
passed) after the fix. Gates: complexity EXIT=0, file-length EXIT=0.

### Cluster 4975 — Asset digests label present but unhashed files as missing

Verdict: FIXED

Evidence: `src/core/ControlProjectAssets.cpp:289-291` (pre-fix) — `digestOf`
wrote the `"missing"` marker for every reference that was neither hashed nor
embedded, so files present on disk but not hashed (`project.missing_assets`
hashes nothing; `project.hash_assets` skips files over the cap) digested
identically to files that are genuinely absent. A distinct `"unhashed"` marker
is now written when `ref.exists`, preserving the command's result schema (only
the digest VALUE changes) and matching the digest format documented in
`include/ControlProjectAssets.h`.

Commit: a1d852425

Test: ControlProjectArchiveTest::presentButUnhashedIsNotDigestIndistinguishableFromMissing

Red/green: `cd build/tests && ctest -R '^ControlProjectArchiveTest$' --output-on-failure`
EXIT=8 (14 passed, 1 failed; the intact scan and the media-removed scan produced
the same digest) → EXIT=0 (15 passed) after the fix. Gates: complexity EXIT=0,
file-length EXIT=0.

### Cluster 3054 — Project reads and `.mmpz` inflation have no size bound

Verdict: FIXED

Evidence: `src/core/ControlProjectAssets.cpp` `readProjectDocument` did
`file.readAll()` with no cap and called `qUncompress` unchecked, so the
10,000-reference limit (`include/ControlProjectAssets.h:149`) was consulted only
after the whole document — or the whole inflated document — was already
allocated. The size on disk is now measured before the file is opened, and a
`.mmpz` is refused from the 4-byte declared expanded size in its header, which
`qUncompress` uses to size its output allocation before inflating anything. Both
bounds use the engine's own 256 MiB `MaxRecoveredDocumentBytes`
(`src/core/DataFile.cpp:202`); the largest real project under `data/projects` is
about 208 kB. The two refusal paths were split into `storedSizeWithinCap`,
`readBoundedCompressed` and `parseStoredDocument` so `readProjectDocument` stays
within the complexity gate's CCN target — the first inline attempt measured CCN
14 and the gate correctly rejected it.

Commit: 49f0ee50e

Test: ControlProjectArchiveTest::anOversizedProjectIsRefusedBeforeItIsRead

Red/green: `cd build/tests && ctest -R '^ControlProjectArchiveTest$' --output-on-failure`
EXIT=8 (15 passed, 1 failed; the 300 MiB sparse `.mmp` was read whole and failed
with a parse error that did not name the cap) → EXIT=0 (16 passed) after the
fix. `cmake --build build --target zene ControlProjectArchiveTest` EXIT=0.
Gates: complexity EXIT=0, file-length EXIT=0.

### Cluster 4065 — Note selection follows list positions after notes move

Verdict: FIXED

Evidence: `src/core/ControlNoteShared.cpp:55` — `scopeNotes` resolves the
selection store's POSITIONS against the clip's current note order, and
`control::selectNotes` (`src/core/ControlEditSupport.cpp:264-275`) is where
those positions live. `note.move` re-sorts via `clip->rearrangeAllNotes()`
(`src/core/ControlCommandsNotes.cpp:271`) without remapping them, unlike
`note.remove`, which remaps for its own shift
(`src/core/ControlCommandsNotes.cpp:199-216`) and is what commit 668e80222 fixed.
So selecting a note, moving it past another, then running a selection-scoped
edit targeted whichever note took its place. `note.move` now snapshots the
selected notes' ids before the move and re-points the store at their new
positions afterwards; ids survive a re-sort (`Note::id()`,
SPEC-stable-ids.md slice 2). The note objects' own selection flags travel with
the notes and needed no change, and the command's params and result schema are
untouched. Storing ids in the selection instead of positions would be the
broader design change the finding suggests; the remap mirrors the precedent
already on this branch and keeps the scope small.

Commit: 71560a1c5

Test: ControlEditCommandsTest::aNoteMoveDoesNotShiftTheSelectionOntoAnotherNote

Red/green: `cd build/tests && ctest -R '^ControlEditCommandsTest$' --output-on-failure`
EXIT=8 (10 passed, 1 failed; after the move `selected_notes` reported "note-21"
while the note that was selected was "note-19") → EXIT=0 (11 passed) after the
fix. Other tests covering the changed file:
`ctest -R '^(ControlEditCommandsTest|ControlProjectArchiveTest|SessionPlaybackRenderTest)$'`
EXIT=0 (3/3 passed). Gates: complexity EXIT=0, file-length EXIT=0.

### Cluster 2163 — Asset hashing can repeatedly read gigabytes on the UI thread

Verdict: REAL-UNTESTED

Evidence: `src/core/ControlProjectAssets.cpp:366-388` — `hashOneReference` opens
and hashes one file per reference, and `controlHashProjectAssets` (lines 457-466)
calls it for every present reference. Nothing in `collectReferences`
(lines 298-334) or `fillReference` (lines 132-155) de-duplicates by resolved
path, so N references that resolve to the same file do read it N times, each up
to the 1 GiB `MaxProjectAssetHashBytes` (`include/ControlProjectAssets.h:150`)
under the 10,000-reference cap — the reported multiplication is real. It is not
fixable here under the batch's procedure: the duplicate reads are pure
redundant work, and hashing the same file twice yields byte-identical
`sha256`, `hashed` and `bytes` values, so the command's entire observable result
is unchanged by a cache. There is no headless assertion that can distinguish
"hashed once and reused" from "hashed N times" without instrumenting read counts
or wall-clock timing, neither of which is available through the command surface.
The finding's second half — bounding total work per command — would change what
`project.hash_assets` guarantees for large projects and belongs to the lane
owner. Worth doing as a perf change with its own instrumentation; no product
code changed.

Commit: none

Test: none

Red/green: none

Leftovers: cluster 2163 is confirmed real but unfixed — a per-file hash cache plus a total-work bound for `project.hash_assets` still needs doing, and needs a way to observe read counts before it can be regression-tested.
## Correction (rework 2026-10-08, sha 15d82085a)

The verdict for cluster 3054 above is **REWORKED, not FIXED as first written**. Its premise was wrong: the
4-byte `.mmpz` header is a size hint, not a bound. Qt's `qUncompress` grows its output past the header, so a
header that understates the expanded size still inflated past the 256 MiB cap. The declared-size refusal in
`49f0ee50e` is kept only as a fast path.

- Corrected fix: commit `563ce2657` (`fix(rework 49f0ee50e)`). The `.mmpz` body is inflated with zlib and the cap
  is applied to the bytes produced (`inflateBodyWithinCap`, `src/core/ControlProjectAssets.cpp`); both bodies are
  read through `readBoundedBody`, which holds at most cap+1 bytes even if the file grows after the stat.
- Test: `ControlProjectArchiveTest::anOversizedProjectIsRefusedBeforeItIsRead` now includes an understated-header
  case. Red (pre-fix reader): EXIT=8. Green: EXIT=0.
- Limit: zlib is linked only when `find_package(ZLIB)` succeeds (`ZENE_HAVE_ZLIB`). Without it the reader falls back
  to `qUncompress`, where only the declared-size refusal bounds memory. Windows and macOS CI were not run here.
- Still open, out of this lane: the engine's own recovery in `src/core/DataFile.cpp` (`decompressesAsRecoverableDocument`,
  around lines 240-258) has the same header-as-bound premise. It checks the size only after `qUncompress` returns.
- Performance-cache cluster 2163 from this batch remains unfixed, as the reviewer noted.
