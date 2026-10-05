# Space Bunny fixer lane `cmds1` — batch 03

Branch `bugs/bunny-cmds1-2026-10-05`, six findings, 1 fixed / 1 already fixed / 1 real-untested /
3 false-positive or skipped.

### Cluster 5591 — Transient slicing omits the audible segment before the first transient
Verdict: REAL-UNTESTED
Evidence: `src/core/ControlCommandsAudioSlice.cpp:197-210` — `sliceFrames()` returns
`clipTransientFrames(clip)` unchanged for `at != "beats"`, while the beats branch explicitly
prepends `clip.sourceFrameAt(clip.startPosition())`. So on the transients path SlicerT's slice 0
(the head before the first cut, built at `:186-187` from `points{0.0}`) gets no note, and the clip's
opening audio is never replayed. The asymmetry is real and the fix is one `insert()`.
Test: none (the existing `AudioEditingTest::slicingToASamplerTrack` cannot exercise it headlessly)
Red/green: none. A patch plus a strengthened assertion in that case (`notes().size()` 4 -> 5, first
note `pos() == TimePos(0)`) built cleanly (`--target AudioEditingTest zene` EXIT=0), but the case
SKIPs in this environment: `SKIP: AudioEditingTest::slicingToASamplerTrack() this build's plug-in
folder has no SlicerT`, with `QWARN: "The plugin \"slicert\" wasn't found or could not be loaded!"`
— the test build scans 0 plugin files. Per rule 2 (no deterministic headless failing test, so no
product-code change) the source and the test were reverted and the tree left as found. The fix is
ready to apply wherever SlicerT is loadable.
Commit: none

### Cluster 5592 — Undoing a bounce can leave a recreated source track muted
Verdict: ALREADY-FIXED
Evidence: `src/core/ControlCommandsBounceLand.cpp:80-96` — `bounceLand` captures
`const QString sourceId = trackIdOf(source)` and both the undo and redo lambdas resolve the live
track with `resolveTrack(sourceId, &ignored)` before writing mute. The comment at `:81-83` states
exactly the reported scenario ("a later track.remove and its undo re-create the track as a NEW
object under the same id, so a pointer kept here would name a deleted track"). The same file already
carries a second commit for this same cluster; the fix is on this branch already.
Commit: none (present on the branch before this batch)

### Cluster 4711 — Malformed collection stores appear empty
Verdict: FIXED
Evidence: `src/core/ControlCommandsBrowserCollections.cpp:74-79` — `readAll()` called
`readStore(&all)` and threw away the bool, so a store file that is not a JSON object became `{}`.
`list()` (`:143`) then answered success with an empty list and `run()` (`:153`) answered
not-found for a collection that was on disk, while `save`/`remove` already refused a damaged store
through `readForEdit()`. `readAll()` now takes a `ControlResult*` and propagates the failure;
`list()` and `run()` refuse with `Refused`; `store()`'s undo snapshot switched to `readForEdit()` so a
damaged store is still never written over.
Commit: e8365bc88
Test: LibraryProjectsTest::aDamagedCollectionsFileIsRefusedNotReadAsEmpty
Red/green: `cmake --build build --target LibraryProjectsTest zene` -> EXIT=0 with the source fix
reverted, then `cd build/tests && ctest -R '^LibraryProjectsTest$' --output-on-failure` -> EXIT=8
(`FAIL! : LibraryProjectsTest::aDamagedCollectionsFileIsRefusedNotReadAsEmpty() '!listed.ok' returned
FALSE. (a damaged collections file was listed as an empty one)`). After restoring the fix: same build
-> EXIT=0, same ctest -> EXIT=0 (8 cases, 0 failures). Both gates EXIT=0.

### Cluster 102 — Preview decodes the entire file before limiting it to 30 seconds
Verdict: SKIPPED-SCOPE
Evidence: `src/core/ControlCommandsBrowserMedia.cpp:241-247` — `SampleDecoder::decode(key)` fills
the whole file and only then `decoded->data.resize(std::min(..., PreviewSeconds * rate))`. The cap is
indeed after the fact, but `SampleDecoder::decode` (`include/SampleDecoder.h:51`,
`src/core/SampleDecoder.cpp:232`) has no frame limit at all and fans out to three decoders
(`src/core/SampleDecoder.cpp:53-57`), each reading its file to the end
(`:71-72` `sf_read_float` over `sfInfo.frames`). Bounding it means a new parameter threaded through
the decoder header, its three implementations and its 8 other call sites
(`SampleBuffer.cpp:81`, `RenderQueue.cpp:118`, `ImportDetection.cpp:128`, …) — a decoder API change,
well past the ~150-line / single-defect limit.
Commit: none

### Cluster 4714 — Failed instrument loading can report success with a blank track
Verdict: REAL-UNTESTED
Evidence: `src/core/ControlCommandsBrowserMedia.cpp:300-301` — the load is
`if (Instrument* instrument = track->loadInstrument(...)) { instrument->loadFile(path); }` followed by
an unconditional `return track;`. A null instrument is discarded, so `load()` (`:321-329`) records the
track via `recordCreatedTrack()` and reports success for a track holding no instrument. The finding is
correct as read; no headless test can force `loadInstrument()` to return null for an extension that
`pluginSupportingExtension()` already resolved, and in this build the plug-in folder scans 0 files, so
no such extension exists to name. No product change without a failing test (rule 2).
Commit: none

### Cluster 4716 — Undoing a clip move can apply different overlap behavior
Verdict: FALSE-POSITIVE
Evidence: `src/core/ControlCommandsClip.cpp:210-212` does choose `clip.move` as the declared inverse
for a move that landed on nothing (`trackWide == false`), which reads like the reported hazard. But
the inverse is only ever executed when it carries `applies == "command"`
(`src/core/ControlCommandsControl.cpp:331-336`), and `transactionPayload`
(`src/core/ControlEditSupport.cpp:481-493`) never writes that key — it defaults to `"journal"`, so
undo goes through `undoThroughJournal()` and the checkpoint the handler took at
`ControlCommandsClip.cpp:193`. Journal restore does not re-read the overlap or ripple modes, so the
mode-dependence the report describes cannot occur; `tests/src/core/ArrangementModesTest.cpp:165`
already covers move-under-trim undoing as one step.
Commit: none

Leftovers: cluster 5591 has a ready, reviewed patch (one `insert()` in `sliceFrames()` plus a
strengthened `AudioEditingTest::slicingToASamplerTrack` expecting 5 notes with the first at
`TimePos(0)`) that was reverted only because SlicerT is not loadable in this test build; cluster 4714
is a real but untested blank-track path. Both need an environment with a working plug-in folder, and
`SampleDecoder::decode` (cluster 102) needs a frame-limit parameter across its header, three
decoders and eight call sites.