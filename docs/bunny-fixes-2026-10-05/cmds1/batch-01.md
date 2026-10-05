# cmds1 batch-01 — control commands A-M

Six findings, `src/core/ControlCommandsAlternatives.cpp`, `ControlCommandsArrangeTools.cpp`,
`ControlCommandsArrangement.cpp`. Two fixed, two false positives, two skipped as scope.

### Cluster 3764 — C++17 build fails on a defaulted equality operator
Verdict: FALSE-POSITIVE
Evidence: `src/core/ControlCommandsAlternatives.cpp:66` holds
`bool operator==(const Alternatives&) const = default;`, a C++20 feature — but the project is
C++20: `src/CMakeLists.txt:25` sets `SET(CMAKE_CXX_STANDARD 20)` under the comment
`# Enable C++20`, and the whole tree builds clean at that standard. This is also binding rule 9's
named case ("the project requires C++20 (C++17 complaints are false)"). Nothing to change.
Commit: none
Test: none
Red/green: none

### Cluster 5584 — Overwriting an alternative cannot be undone
Verdict: FALSE-POSITIVE
Evidence: the behaviour is the command's advertised contract, not an accident. The file header
says "Saving and removing an entry are one undo step each (the list; a written file stays on
disk)" (`src/core/ControlCommandsAlternatives.cpp:30-31`) and the registered description repeats
it — "Saving under an existing name replaces that alternative's file. One undo step takes it off
the list (the file stays)" (`:253-254`). `saveAlternative()` writes the file at `:153` and
records only the list (`:158`, `record()` at `:132-137`), which is exactly what it promises. A
client is told the overwritten file is not restored. Making it undoable would mean snapshotting
file contents into the undo step or refusing overwrites — a design change to a documented
command, which is not a defect repair.
Commit: none
Test: none
Red/green: none

### Cluster 5589 — Remove-silence duration is wrong when source and engine rates differ
Verdict: FIXED
Evidence: `minFrames` was `minTicks * Engine::framesPerTick()` — the ENGINE scalar
(`src/core/Engine.cpp:133-137`, no argument, so the engine's own rate) — while `quietRuns()`
compares `f - start >= minFrames` (`:156`) over frames of `clip.sample().data()`
(`ControlCommandsArrangeTools.cpp:141-161`), the clip's OWN sample. `SampleClip` already exposes the correct
scalar, `clipFramesPerTick()`, which uses `m_sample.sampleRate()` and folds in the clip's tempo
mode (`src/core/SampleClip.cpp:337-350`) — the same value `SampleClip::timelinePosAt()` uses to
turn those very frames back into ticks. So a 96 kHz clip in a 48 kHz engine was compared against
half the frames asked for. Line 184 now uses `clip->clipFramesPerTick()`.
Commit: 919334ca2
Test: ArrangeToolsTest::aQuietStretchIsMeasuredInTheClipsOwnRate
Red/green: red `cmake --build build --target ArrangeToolsTest` EXIT=0 and
`cd build/tests && ctest -R '^ArrangeToolsTest$' --output-on-failure` EXIT=8 (`FAIL!` —
`aQuietStretchIsMeasuredInTheClipsOwnRate() Compared values are not the same`, 12 passed / 2
failed); green `cmake --build build --target zene ArrangeToolsTest` EXIT=0 and the same ctest
EXIT=0, with `NoteToolsTest` EXIT=0. Both gates EXIT=0.

### Cluster 3773 — `clip.list` reports an unknown track as a successful empty result
Verdict: FIXED
Evidence: `listClips()` read the filter at `src/core/ControlCommandsArrangeTools.cpp:71` and only
compared it with `trackIdOf(track)` inside the loop (`:77`), so an id matching no track fell
through to `success` with an empty `clips` array — the report is correct. The house precedent
already existed one file over: `automationGetState()` tracks a `matched` flag and refuses
`NotFound` when a non-empty filter matched nothing
(`src/core/ControlCommandsAutomation.cpp:117-132`). `listClips()` now does the same
(`:78`, `:82`, `:94-97`), with the same message. No schema or result-shape change: only a
previously-successful typo now returns the typed `NotFound` every other command returns.
Commit: 823a8228b
Test: ArrangeToolsTest::aClipListRefusesATrackThatIsNotThere
Red/green: red `cmake --build build --target ArrangeToolsTest` EXIT=0 and
`cd build/tests && ctest -R '^ArrangeToolsTest$' --output-on-failure` EXIT=8 (`FAIL!` —
`aClipListRefusesATrackThatIsNotThere() Compared values are not the same`, 12 passed / 2
failed); green `cmake --build build --target zene ArrangeToolsTest` EXIT=0 and the same ctest
EXIT=0 (14/14). Both gates EXIT=0.

### Cluster 3776 — `clip.remove_silence` omits its reversibility record
Verdict: SKIPPED-SCOPE
Evidence: the undo itself is intact and complete; only a descriptive record is absent.
`removeSilence()` takes one track checkpoint (`src/core/ControlCommandsArrangeTools.cpp:191`) and
the contract table classes it `TrueInverse` over a "Track checkpoint"
(`src/core/ControlReversibilityTableDevices.cpp:170`). Adding a `__transaction` result key would
not make the undo more capable: these records are never replayed. `transactionPayload()` writes
only `before`/`inverse`/`reversible`/`mechanism` and no `applies` key
(`src/core/ControlEditSupport.cpp:481-493`), so `undoByCommand()` reads `applies` as `"journal"`
and returns `std::nullopt` (`src/core/ControlCommandsControl.cpp:334-335`) — `control.undo`
unwinds the journal, which is the real inverse here. Same refutation as the clips lane's
batch-02 cluster and the notes lane's batch-06 cluster. Making the record replayable would need
rule-8 territory (new result keys and new inverse arguments), so the code was left alone.
Commit: none
Test: none
Red/green: none

### Cluster 980 — `track.remove` reports an inverse that lacks the data needed to replay it
Verdict: SKIPPED-SCOPE
Evidence: same mechanism as cluster 3776, and here the record is already honest about it.
`track.remove` is `TrueInverse` and its row states the captured-document inverse
(`src/core/ControlReversibilityTableStructure.cpp:99-105`), and the reply's `mechanism` string
names it precisely — the track's own XML is captured before the delete and the recorded undo step
recreates it with `Track::create(element, song)`, clips included, because a checkpoint taken
after the delete could not (`src/core/ControlCommandsArrangement.cpp:275-284`). The `before`
value is the summary snapshot by design, since the XML lives in the journal's bounded 64 KiB
document — putting it in the reply would duplicate a payload the contract already declares is
counted against the stack's byte budget (`include/ControlReversibility.h:484-501`) and would
change the result schema (rule 8). The report's premise — that no replayable restore exists — is
false: the restore is the journal step, exactly as `mechanism` says.
Commit: none
Test: none
Red/green: none

Leftovers: nothing half-done. Cluster 3776 and 980 are both unfixed by scope and both stand as
advisory follow-ups: the `__transaction` payload family is descriptive only, so any finding that
reads one as a replayable inverse needs the `undoByCommand` `applies` check checked first.
`alternatives`' `operator==` (cluster 3764) is unused in the current tree; it was left in place
under rule 7 rather than removed as drive-by cleanup.