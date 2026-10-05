# docmodel batch-08 — 2026-10-05

Lane: DataFile, DocumentIndex, provenance section, automation clips/transforms/commands, track folders.
Branch: `bugs/bunny-docmodel-2026-10-05`.

Verdicts: 1 FIXED, 1 FALSE-POSITIVE, 4 SKIPPED-SCOPE — 1 fix committed, 1 false positive argued
from source, 4 out of scope. Separately, and before this batch's findings, the four visibility-set
inverse verdicts batch-06 recorded were overturned and fixed (`c04d2cdcf`).

### Cluster 5026 — UTF-8 character offsets are used as byte offsets

Verdict: FIXED

Evidence: `src/core/DocumentIndex.cpp:401` (pre-fix) and `:409` took
`QXmlStreamReader::characterOffset()` — a CHARACTER count — and used it to slice the `QByteArray`
through `data.lastIndexOf('<', ...)` and `removeRanges()`'s `data.mid(...)`. The shift is
cumulative, so any multibyte character before a selected section made every recorded offset land
earlier than the bytes it named, and the cut fell inside the section: the reducer returned
malformed XML rather than a reduced document. This build's Qt (6.4.2) has no `byteOffset()` — it
arrives in 6.5 — so the offset is now converted by `bytePositionOf()`
(`src/core/DocumentIndex.cpp:381`), which counts UTF-8 lead bytes. `include/DocumentIndex.h`'s
property-1 documentation is updated in the same commit, since header and source change together.

Commit: 26f30530c

Test: `DocumentIndexTest::aMultibyteCharacterBeforeASectionDoesNotShiftTheCut`

Red/green:
- RED: `cmake --build build --target DocumentIndexTest zene` → EXIT=0; `cd build/tests && ctest -R '^DocumentIndexTest$' --output-on-failure` → EXIT=8 (`Totals: 10 passed, 1 failed`). The recorded range reproduced `"<dropme a=\"1\">\n      <inner b=\"2\"/>\n    </d"` instead of the whole section — six bytes short, exactly the fixture's multibyte shift.
- GREEN: same build → EXIT=0; `ctest -R '^DocumentIndexTest$' --output-on-failure` → EXIT=0 (`11 passed`). Covering suites `DocumentSectionsTest` and `ProjectContainerEntriesTest` rebuilt and `zene` relinked → `ctest -R '^(DocumentIndexTest|DocumentSectionsTest|ProjectContainerEntriesTest)$'` → EXIT=0, 3/3 passed. Gates: complexity EXIT=0, file-length EXIT=0.

Note: the same defect SHAPE survives in the `.mmpz` deriver's own walk —
`src/core/ProjectContainerEntries.cpp:246` still calls `xml.characterOffset()` and indexes a
`QByteArray` with it. It is not fixed here because the conversion helper is in `DocumentIndex.cpp`'s
anonymous namespace, so reaching it means either exporting it or duplicating the rule — a
canonicalization/layout decision, not a contained fix. Recorded in Leftovers.

### Cluster 5028 — Successful section scans append to prior results

Verdict: FALSE-POSITIVE

Evidence: `scanSectionRanges()` (`src/core/DocumentIndex.cpp:447-448`, pre-fix `:409-410`) appends
to `out.ranges`/`out.names` without clearing, so the claim is factually right about the function.
But it is not reachable: the function has exactly two callers in the tree, and both pass a
FRESHLY declared local — `src/core/DocumentIndex.cpp:499` (`SectionRanges found;` in
`reduceDocumentSections`) and `src/core/ProjectContainerEntries.cpp:488` (`SectionRanges found;` in
the deriver). No caller ever hands a reused `SectionRanges` to it, so no scan can return stale
entries alongside new ones. `scanSectionRanges` is exported (`include/DocumentIndex.h:278`), so a
future caller could misuse it; that is a latent API-ergonomics note, not a defect in this tree.

Commit: none

Test: none

Red/green: none.

### Cluster 5027 — Section digests do not faithfully represent XML character data

Verdict: SKIPPED-SCOPE

Evidence: the factual claim holds — `writeCanonicalChildren()`
(`src/core/DocumentIndex.cpp:137`) trims each text/CDATA node and emits it on its own line, so
`<x> note </x>` and `<x>note</x>` digest alike, and adjacent text/CDATA nodes (`<x>a<![CDATA[b]]></x>`)
are emitted as two lines where `<x>ab</x>` is one. But the trim is not an oversight: it is the
documented invariant at `src/core/DocumentIndex.cpp:119-125` — character data participates TRIMMED
precisely so a parser-built DOM (which carries a run's indentation inside the text node) and a
setter-built one agree about the same content, which is what the digest is bought for. The merge
half is a real canonicalization gap, but closing it changes the bytes the digest is taken over,
i.e. a format migration that would invalidate every index already on disk. Out of scope for this
lane.

Commit: none

Test: none

Red/green: none.

### Cluster 3159 — Section digests omit inherited namespace bindings

Verdict: SKIPPED-SCOPE

Evidence: `writeCanonical()` (`src/core/DocumentIndex.cpp:145-164`) emits `element.nodeName()` —
the PREFIXED name — plus only the element's own attributes, so a binding inherited from an
ancestor is not covered and two documents binding `z` differently can share a digest for the same
`<z:extension/>`. The claim is correct, but the fix is the finding's own stated one ("include
in-scope namespace bindings or canonicalize expanded names") and that means threading namespace
context through the canonical writer — a redesign of the canonical form, which changes every
existing digest. Sections are project elements and are unprefixed in practice, so the practical
impact is nil today. Out of scope.

Commit: none

Test: none

Red/green: none.

### Cluster 3161 — Writing an index can change existing namespace meanings

Verdict: SKIPPED-SCOPE

Evidence: `writeDocumentIndex()` (`src/core/DocumentIndex.cpp:279`) does unconditionally
`root.setAttribute("xmlns:z", documentIndexNamespaceUri())`, overwriting a pre-existing vendor `z`
binding, so a hand-edited document binding `z` and carrying preserved `<z:custom/>` content would
have that content's namespace changed by saving an index. Reachable in principle. But the finding's
own fix — preserve the existing binding and pick another prefix — contradicts
`documentIndexNodeName()` / `documentIndexSectionNodeName()` (`include/DocumentIndex.h:44-45`),
which return the hard-coded `z:index` / `z:section` names that `parseDocumentIndex()`
(`src/core/DocumentIndex.cpp:309-339`) and every reader match on. A dynamic index prefix is a
format, writer and reader change, outside this lane.

Commit: none

Test: none

Red/green: none.

### Cluster 5059 — Test fault injection is active in production saves

Verdict: SKIPPED-SCOPE

Evidence: the claim is factually true — `src/core/DataFile.cpp:754` reads
`qgetenv("ZENE_TEST_SAVE_FAULT")` unconditionally, and the three injected stops make ordinary
saves fail: `after-write` (`:763`) returns false with the `.new` complete, `after-backup` (`:794`)
returns false with the `.bak` copied, and `after-rename` (`:821`) returns false AFTER the atomic
rename has already landed, so the file changed while the save reported failure. But this is a
deliberate, documented test seam — `src/core/DataFile.cpp:750-752` names it as such and
`DataFileSaveIntegrityTest` pins the on-disk states its steps produce. Making it test-only requires
a test-only compile definition, i.e. a build-system change, not a source fix in this lane.

Commit: none

Test: none

Red/green: none.

Leftovers: one item is HALF-DONE by scope, not by neglect — the `.mmpz` deriver's
`walkToContentStart()` at `src/core/ProjectContainerEntries.cpp:246` still uses
`xml.characterOffset()` as a `QByteArray` index, the identical defect fixed in
`src/core/DocumentIndex.cpp` by `26f30530c`. It needs either `bytePositionOf()` exported out of
`DocumentIndex.cpp`'s anonymous namespace or the rule restated in the deriver, which is a small
design call about where the conversion helper lives; the deriver's own tests pass, and no
multibyte fixture exercises it today. Everything else is complete: `26f30530c` is committed with a
red/green regression and both gates green, and `git status --short` is empty apart from this report
being committed on its own.
