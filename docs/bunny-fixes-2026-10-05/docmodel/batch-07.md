# docmodel batch-07 — 2026-10-05

Lane: DataFile, DocumentIndex, provenance section, automation clips/transforms/commands, track folders.
Branch: `bugs/bunny-docmodel-2026-10-05`.

Verdicts: 3 FIXED, 1 FIXED, 1 FALSE-POSITIVE, 1 SKIPPED-SCOPE — 4 fixes committed, 1 false positive
argued from source, 1 out of scope.

### Cluster 5058 — Recovery can replace a valid project with unrelated XML

Verdict: FIXED

Evidence: `src/core/DataFile.cpp:215` (at the pre-fix revision) returned true from
`isCompleteDocument()` for any bytes that parsed as XML with a root element, and
`recoverPendingWrite()` (pre-fix `src/core/DataFile.cpp:241-271`, now `src/core/DataFile.cpp:299`)
renamed that temp over the canonical project. `<not-a-project/>` satisfies both conditions, so opening a song with a stale
`song.mmp.new` beside it replaced the project with unrelated XML. Adoption now requires a
document this program recognizes — a `zene-project` (or pre-rename `lmms-project`) root, a
recognized type, and the content element that type names
(`DataFile::isRecoverableDocument`, `include/DataFile.h` + `src/core/DataFile.cpp:342`).

Commit: ad4a3d96d

Test: `DataFileSaveIntegrityTest::unrelatedXmlNewFileIsNotAdopted`

Red/green:
- RED: `cmake --build build --target DataFileSaveIntegrityTest zene` → EXIT=0; `cd build/tests && ctest -R '^DataFileSaveIntegrityTest$' --output-on-failure` → EXIT=8 (`'report.contains( "Discarded incomplete" )' returned FALSE`, the unrelated `.new` was adopted as `song.mmp`).
- GREEN: same build → EXIT=0; `ctest -R '^(DataFileSaveIntegrityTest|DataFileFormatTest)$' --output-on-failure` → EXIT=0. Gates: complexity EXIT=0, file-length EXIT=0.

### Cluster 2229 — Interrupted-save recovery parses XML before checking its depth

Verdict: FIXED

Evidence: the normal load path guards every parse with `projectXmlDepthWithinLimit`
(`src/core/DataFile.cpp:2577`, `include/XmlDepthGuard.h`), but the recovery helper called
`lmms::setContent` on the pending temp directly, with no depth limit at all. A deeply nested
but complete `.new` beside a valid project was parsed — and adopted — before the guarded load
could refuse it. Both recovery parses now go through `parsesAsRecoverableDocument`
(`src/core/DataFile.cpp:220-228`), which checks depth before building any node.

Commit: 6cf671ac7

Test: `DataFileSaveIntegrityTest::deeplyNestedNewFileIsRefusedBeforeParsing`

Red/green:
- RED: `cmake --build build --target DataFileSaveIntegrityTest zene` → EXIT=0; `cd build/tests && ctest -R '^DataFileSaveIntegrityTest$' --output-on-failure` → EXIT=8 (`a deeply nested .new was adopted`).
- GREEN: build EXIT=0; `ctest -R '^(DataFileSaveIntegrityTest|DataFileFormatTest)$' --output-on-failure` → EXIT=0. Gates: complexity EXIT=0, file-length EXIT=0.

### Cluster 3090 — Recovery can exhaust memory decompressing a crafted `.new`

Verdict: FIXED

Evidence: `isCompleteDocument` read the whole temp and handed non-XML bytes to `qUncompress`,
whose 4-byte header declares the uncompressed size and is allocated before any byte is
decompressed. Both the file's own size and the size its header declares are now measured
against `MaxRecoveredDocumentBytes` (256 MiB, `src/core/DataFile.cpp:203`) before either is
used; an oversized temp is reported as a refusal ("Refused an oversized …") and left in place
rather than deleted, since it was never validated
(`decompressesAsRecoverableDocument`, `src/core/DataFile.cpp:242-259`, `recoverPendingWrite`
`src/core/DataFile.cpp:299`).

Commit: 3ffe83d9a

Test: `DataFileSaveIntegrityTest::newFileDeclaringAnEnormousSizeIsRefusedBeforeDecompressing`

Red/green:
- RED: `cmake --build build --target DataFileSaveIntegrityTest zene` → EXIT=0; `cd build/tests && ctest -R '^DataFileSaveIntegrityTest$' --output-on-failure` → EXIT=8 (`the declared size was not checked`; the crafted temp was adopted as `song.mmp`).
- GREEN: build EXIT=0; `ctest -R '^(DataFileSaveIntegrityTest|DataFileFormatTest)$' --output-on-failure` → EXIT=0. Gates: complexity EXIT=0, file-length EXIT=0.

### Cluster 4971 — Invalid preset can pass the track-type check

Verdict: FIXED

Evidence: `src/core/ControlCommandsTrackPresets.cpp:80` read the preset's track type with
`toInt()` and never checked the parse result, so a missing or nonnumeric `type` answered 0 —
which is `Track::Type::Instrument`'s own value (`include/Track.h:83-86`). A malformed preset
therefore passed the same-kind-of-track check on an instrument track, added an undo step and was
loaded. The parse result is now checked, and an unresolved content element is refused, before
anything is undone (`src/core/ControlCommandsTrackPresets.cpp:78-92`). The command's params,
result schema and registry row are untouched.

Commit: 243fd5fa5

Test: `LibraryProjectsTest::aPresetWithoutATrackTypeIsRefused`

Red/green:
- RED: `cmake --build build --target LibraryProjectsTest zene` → EXIT=0; `cd build/tests && ctest -R '^LibraryProjectsTest$' --output-on-failure` → EXIT=8 (`Compared values are not the same` — the typeless preset was loaded).
- GREEN: build EXIT=0; `ctest -R '^(LibraryProjectsTest|MixerToolsTest)$' --output-on-failure` → EXIT=0. Gates: complexity EXIT=0, file-length EXIT=0.

### Cluster 2228 — Iterative upgrade changes the caller's element handle

Verdict: FALSE-POSITIVE

Evidence: the helper does mutate the caller's handle
(`src/core/DataFile.cpp:1564`: `el = pending.takeLast();`, with the caller advancing at
`src/core/DataFile.cpp:1619-1624`) — that part of the report is accurate. But the claimed
consequence does not follow: the helper appends **every** child element of each element it
visits to its walk stack (`src/core/DataFile.cpp:1586-1591`), so its first call — which starts
at the document's root element (`src/core/DataFile.cpp:1620`, `firstChildElement()`) — already
visits every descendant, i.e. every later root child. The caller's `el = el.nextSiblingElement()`
afterwards only re-walks siblings of the last visited descendant; that is redundant (the
per-element `syncmode` migration is idempotent) and it terminates, but it cannot skip an
element. Verified empirically: a temporary case (since reverted) loaded a `1.2.0-rc2` project
with four root children each carrying `syncmode` and a `*_numerator` attribute and asserted all
four derived `*_syncmode` attributes — `head`, `trackcontainer`, `playlist` and `masterTracklist`
all migrated. `./DataFileFormatTest temporaryProbeEveryRootChildIsUpgraded` → EXIT=0, 3 passed,
0 failed. No product change made.

Commit: none

Test: none (the probe was a temporary check, reverted; `DataFileFormatTest` → EXIT=0 after revert)

Red/green: none — the defect as described does not reproduce.

### Cluster 1213 — Disabling MIDI output forgets its configured ports

Verdict: SKIPPED-SCOPE

Evidence: `stopOutput` (`src/core/ControlCommandsTrackMidiOut.cpp:57-65`) unsubscribes every
writable port and then disables output, so re-enabling with no `port` yields no destinations,
against the command's own contract text ("'enabled' false stops it and leaves every port",
`src/core/ControlCommandsTrackMidiOut.cpp:100`). The loop, however, is not the cause and removing
it would change nothing: `MidiPort::setWritable(false)` fires `m_writableModel`'s `dataChanged`
into `updateMidiPortMode` (`src/core/midi/MidiPort.cpp:96-98`, DirectConnection), which itself
unsubscribes every subscribed writable port whenever output is not enabled
(`src/core/midi/MidiPort.cpp:410-420`) and forgets each one from the reconnect memory
(`src/core/midi/MidiPort.cpp:379`). Honouring the contract therefore means changing core MIDI
routing semantics in `MidiPort` — what "disabled but configured" means for the GUI's port menu,
for `saveSettings` (which only writes `outports` while output is enabled,
`src/core/midi/MidiPort.cpp:250-263`) and for the reconnect memory — which is a design change
well beyond this lane's smallest correct change. It is also not deterministically testable
headlessly: no test fixture provides writable MIDI ports to an instrument track's `MidiPort`
(the only use in tests is `tests/src/core/MidiReconnectTest.cpp:416`), so the port retention
cannot be observed without one.

Commit: none

Test: none

Red/green: none.

Leftovers: none — all four fixes are committed with green regression tests and both gates passing
at each commit; `git status --short` is empty. Cluster 1213 is left unfixed by design decision,
not half-done: it needs a `MidiPort`-level answer to "does a disabled port keep its configured
destinations?", plus a writable-port test fixture, and that should go to the parent rather than be
guessed at here.
