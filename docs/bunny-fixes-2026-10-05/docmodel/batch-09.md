# docmodel — batch 09

Six findings, all in the provenance section and the track-folder paths. Each was
confirmed against the current tree, given a regression test that failed first, and
fixed in its own commit.

### Cluster 5264 — Provenance byte accounting undercounts the saved section

Verdict: FIXED

Evidence: `src/core/ProvenanceSection.cpp:125` — `serialisedBytes()` summed
`QString::size()` of the attribute values only. It counted no attribute names, no
`="` quoting and no `<z:change/>` markup, contradicting both its own comment at
`:122-124` ("plus the attribute names they ride on") and the header's contract for
`Change::bytes` ("serialised size, the quantity the byte bound counts"). The UTF-8
half of the report is practically moot — every field value in practice is ASCII —
but the missing names and markup are not: a measured entry counted 189 bytes
against 264 serialised, so the byte cap bounded roughly 72% of what the writer
emits. The UTF-16/UTF-8 and entity-escaping width is handled anyway, so a
non-ASCII value cannot silently undercount either.

Commit: c90676a40

Test: ProvenanceSectionTest::theCountedBytesAreTheSerialisedBytesOfTheSavedEntry

Red/green: `cmake --build build --target zene ProvenanceSectionTest` EXIT=0;
`cd build/tests && ctest -R '^ProvenanceSectionTest$'` EXIT=8 (Actual 189,
Expected 264) → EXIT=0 (9 passed); `ctest -R
'^(DocumentSectionsTest|ProjectContainerTest)$'` EXIT=0; complexity-gate.sh --check
EXIT=0; file-length-gate.sh --check EXIT=0

### Cluster 3399 — Malformed provenance entries are claimed and rewritten

Verdict: FIXED

Evidence: `src/core/ProvenanceSection.cpp:204` (the loader) read `seq` and the
seven known fields but never checked the element's shape. `writeTo()` at `:301`
emits exactly `seq`, `at`, `actor`, `instance`, `command` plus the optional
`target`/`before`/`after` — so an entry carrying an unknown attribute was adopted
and that attribute dropped on the next save, and an entry missing one of the four
fields the writer always wrote was adopted and rewritten with an empty value.
`Song::restoreNamedSection` (`src/core/Song.cpp:2295`) treats a true return as
claimed, which is what made the rewrite reachable.

Commit: 1d59bb250

Test: ProvenanceSectionTest::anEntryWithAnUnknownAttributeOrAMissingFieldIsRefused

Red/green: `cmake --build build --target zene ProvenanceSectionTest` EXIT=0;
`ctest -R '^ProvenanceSectionTest$'` EXIT=8 ("an entry with an unrecognized
attribute must be refused"; "an entry with no command must be refused") → EXIT=0
(12 passed); `ctest -R '^(DocumentSectionsTest|ProjectContainerTest)$'` EXIT=0;
complexity-gate.sh --check EXIT=0; file-length-gate.sh --check EXIT=0

### Cluster 5263 — The provenance loader adopts out-of-order or incomplete entries

Verdict: FIXED

Evidence: `src/core/ProvenanceSection.cpp:204` — the loop checked each child's name
and that its seq parsed and was non-zero, but never that entries ASCEND. The
writer assigns `m_nextSeq++` and appends (`src/core/ProvenanceSection.cpp:159`), so
a file carrying 3, 1 is a shape this writer never produced; adopting it made the
next append produce 3, 1, 4, which this same loader then refuses. The "or
incomplete" half of the title is cluster 3399's schema half and was fixed there, as
its own commit.

Commit: ac93fcf4a

Test: ProvenanceSectionTest::anOutOfOrderSectionIsRefusedRatherThanAdopted

Red/green: `cmake --build build --target zene ProvenanceSectionTest` EXIT=0;
`ctest -R '^ProvenanceSectionTest$'` EXIT=8 ("'!Section::instance().load(section)'
returned FALSE") → EXIT=0 (11 passed); `ctest -R
'^(DocumentSectionsTest|ProjectContainerTest)$'` EXIT=0; complexity-gate.sh --check
EXIT=0; file-length-gate.sh --check EXIT=0

### Cluster 5267 — The maximum provenance sequence wraps to zero

Verdict: FIXED

Evidence: `src/core/ProvenanceSection.cpp:229` — `m_nextSeq = highest + 1` overflowed
at `UINT64_MAX`, so the next append wrote `seq="0"`, which the loader itself rejects
at `:208` (a seq of zero is refused). A project holding the last sequence in the
space was claimed on load and then made unwritable.

Commit: b1d0ce402

Test: ProvenanceSectionTest::anExhaustedSequenceIsRefusedRatherThanWrappedToZero

Red/green: `cmake --build build --target zene ProvenanceSectionTest` EXIT=0;
`ctest -R '^ProvenanceSectionTest$'` EXIT=8 → EXIT=0 (10 passed); `ctest -R
'^(DocumentSectionsTest|ProjectContainerTest)$'` EXIT=0; complexity-gate.sh --check
EXIT=0; file-length-gate.sh --check EXIT=0

### Cluster 4550 — Malformed saved folder bindings can become channel-zero bindings

Verdict: FIXED

Evidence: `src/tracks/TrackFolder.cpp:72-85` — `parseBindings()` read both halves of
every `prevch` pair with an unchecked `toInt()`, which yields 0 for "bad", and 0 is
the master channel, a value the mixer treats as real. `loadTrackSpecificSettings`
(`:405`) feeds it the file's attribute directly, so `prevch="bad:4,1:bad"` loaded as
two bindings of track 0.

Commit: fec8ce6c1

Test: TrackFolderTest::aMalformedSavedBindingIsDiscardedNotTakenAsChannelZero

Red/green: `cmake --build build --target zene TrackFolderTest` EXIT=0; `ctest -R
'^TrackFolderTest$'` EXIT=8 ("'folder->savedChildChannels().empty()' returned
FALSE") → EXIT=0 (17 passed); `ctest -R
'^(TrackFolderMenuTest|UnclaimedTrackTypeTest|SessionPerformTest|AudioAnalysisTest)$'`
EXIT=0; complexity-gate.sh --check EXIT=0; file-length-gate.sh --check EXIT=0

### Cluster 5421 — Deleting a routing folder leaves its mixer channel behind

Verdict: FIXED

Evidence: `src/tracks/TrackFolder.cpp:105-113` — the destructor unlinked the
children but never released `m_mixerChannel`, unlike `releaseRouting()` at `:338`
which deletes it. Two things had to be handled beyond the missing call: the release
must run AFTER the unlink (so no child points at the channel when
`Mixer::deleteChannel()` renumbers the ones above it), and it must be skipped when
there is no channel to release — `Engine::destroy()` (`src/core/Engine.cpp:96-127`)
clears the song before deleting the mixer, and a `mixch` loaded from a file is an
index this folder never allocated. Without the range guard the first fix SIGSEGV'd
in `Mixer::deleteChannel` on the existing `aMalformedSavedBinding` fixture, which
loads `mixch="1"`; that was found by the regression suite, not by inspection.

Commit: 197a8955c

Test: TrackFolderTest::deletingARoutingFolderGivesItsMixerChannelBack

Red/green: `cmake --build build --target zene TrackFolderTest` EXIT=0; `ctest -R
'^TrackFolderTest$'` EXIT=8 → EXIT=0 (18 passed); `ctest -R
'^(TrackFolderMenuTest|UnclaimedTrackTypeTest|SessionPerformTest|AudioAnalysisTest)$'`
EXIT=0; complexity-gate.sh --check EXIT=0; file-length-gate.sh --check EXIT=0

Leftovers: none. Six FIXED, no SKIPPED-SCOPE, no findings left untested. No command
schema, tolerance, gate or test was weakened; no new test file was needed, so
`tests/CMakeLists.txt` and `tests/fork-sources.txt` are unchanged.