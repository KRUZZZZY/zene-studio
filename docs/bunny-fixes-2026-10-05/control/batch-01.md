# Space Bunny fix lane `control` — batch-01 (2026-10-05)

6 findings, cluster 3765 / 4725 / 5581 / 997 / 5582 / 3780.
2 FIXED, 1 FALSE-POSITIVE, 3 SKIPPED-SCOPE.

### Cluster 3765 — An empty browser query path silently searches the roots

Verdict: FIXED

Evidence: `src/core/ControlBrowserSupport.cpp:119-120` (old) — `readQueryPath()`
read the trimmed value and `if (raw.isEmpty()) { return true; }`, i.e. a PRESENT but
empty `path` returned success and left `query->path` unset, so the walk fell back to
the browser roots. That contradicts this file's own stated contract in its header
comment: "an empty `path` ... get their own message naming the value that was
refused". Only the ABSENT-key case may mean "walk the roots". The reader now
refuses a present-but-empty (or whitespace-only) path with `invalid_args` and an
absent key still walks the roots.

Commit: 93e1aa2b3

Test: ControlBrowserCommandsTest::refusalsAreTypedAndChangeNothing

Red/green:
- Red: `cd build/tests && ctest -R '^ControlBrowserCommandsTest$' --output-on-failure > build/bunny-t.log 2>&1; echo EXIT=$?` -> EXIT=8
  (`refusalsAreTypedAndChangeNothing() Compared values are not the same`)
- Green: same command after the fix -> EXIT=0 (9 passed)
- Build: `cmake --build build --target zene ControlBrowserCommandsTest > build/bunny-b.log 2>&1; echo EXIT=$?` -> EXIT=0

### Cluster 4725 — Browser file paths are trimmed and can address a different file

Verdict: FIXED

Evidence: `src/core/ControlBrowserSupport.cpp:279` (old) — `readBrowserFileArgument()`
did `.toString().trimmed()` before resolving, so with both `/tmp/kick.wav` and
`/tmp/kick.wav ` on disk a request for the padded name resolved to the unpadded file
and the tag edit landed on a file the caller never named; `readQueryPath()` trimmed
directory paths the same way. A leading or trailing space is a legal filename
character, so the value is now used exactly as given, and only a value that is empty
or entirely whitespace is still refused (`readBrowserFileArgument` line 298,
`readQueryPath` line 124). The regression writes a real second fixture named
`kick_48k.wav ` beside `kick_48k.wav` and reads the tag store back for both keys.

Commit: b452e91a8

Test: ControlBrowserCommandsTest::paddedPathsAddressTheFileTheyName

Red/green:
- Red: `cd build/tests && ctest -R '^ControlBrowserCommandsTest$' --output-on-failure > build/bunny-t.log 2>&1; echo EXIT=$?` -> EXIT=8
  (`paddedPathsAddressTheFileTheyName() ... 'tagsOnDisk(m_store, padded).contains("spaced")' returned FALSE`)
- Green: `cd build/tests && ctest -R '^(ControlBrowserCommandsTest|BrowserCatalogTest|LibraryBrowserTest|BrowserTagMenuTest)$' --output-on-failure > build/bunny-t.log 2>&1; echo EXIT=$?` -> EXIT=0 (4/4 passed)
- Build: `cmake --build build --target zene ControlBrowserCommandsTest > build/bunny-b.log 2>&1; echo EXIT=$?` -> EXIT=0
  (the three sibling browser tests first reported EXIT=8 as "Unable to find
  executable" — a stale build tree, not a failure; after building those three
  targets they pass.)

### Cluster 5581 — Bounce ranges cast unchecked doubles to `tick_t`

Verdict: FALSE-POSITIVE

Evidence: `src/core/ControlBounceSupport.cpp:59-65` already clamps BEFORE the cast:
`clampedTicks()` is `static_cast<tick_t>(std::clamp(args.value(key).toDouble(), -1.0,
MaxSongLength + 1.0))`, and the comment above it names this exact hazard ("casting a
double outside tick_t's range is undefined, and a clamped value is refused below just
as the original would have been"). So the finding's `when` case — `end: 1e300` hitting
undefined behaviour "before the timeline bound check runs" — cannot happen: the clamp
runs first, and `parseRange` (line 82) then refuses `end > MaxSongLength`. The
fractional half is also handled upstream by the `isDouble()` shape check at line 73-77
plus the same clamp. The report appears to have been written against a revision before
this guard was added; the current tree already carries the fix and its rationale.

Commit: none

Test: none

Red/green: none

### Cluster 997 — Undoing a browser tag edit fails if its file was deleted

Verdict: SKIPPED-SCOPE

Evidence: the mechanism is real — `browser.tag.add` records its inverse as the PAIRED
COMMAND `browser.tag.remove` with `applies: "command"`
(`src/core/ControlCommandsBrowserTags.cpp:128`), and that inverse re-enters
`tagRemove()`, which runs the shared `readBrowserTagArguments()`
(`ControlCommandsBrowserTags.cpp:137`) → `readBrowserFileArgument()`
(`ControlBrowserSupport.cpp:315-321`), where `if (!QFileInfo(canonical).isFile())`
answers `not_found` once the audio file is gone. But the fix the finding asks for
("let undo restore the recorded tag-store state without requiring the audio file to
exist") cannot be expressed as a change to that reader: the file check is the very
thing that makes `browser.tag.remove` refuse a path that is not a file, and dropping
it for the undo path means teaching the command pair to behave differently depending
on who dispatched it. That is a change to the inverse mechanism (a snapshot restore
that bypasses the paired command), i.e. a design decision for the surface owner, not a
behaviour-preserving local fix.

Commit: none

Test: none

Red/green: none

### Cluster 5582 — Editing an existing global automation clip can lose the edit on save

Verdict: SKIPPED-SCOPE

Evidence: the premise checks out — the hidden global automation track is genuinely
outside what gets saved (`src/core/TrackContainer.cpp:268-270`, `addTrack()` refuses
`Track::Type::HiddenAutomation`, so it is not in `Song::tracks()`), while
`control::automationTracks()` deliberately appends it
(`src/core/ControlAutomationSupport.cpp:231-233`) and `existingAutomationClip()`
therefore can hand back a clip on it. But the fix is an engine-level state migration
(materialise the clip onto a saved Automation track, and rewrite the model's binding),
touching `AutomationClip::globalAutomationClip()`'s own ownership contract — the same
lane-ownership question cluster 5587 already records as a design decision — and it
changes which clip the edit commands address, i.e. the shape of `automation.*`'s
success path. Left alone.

Commit: none

Test: none

Red/green: none

### Cluster 3780 — Chord detection accepts an impossible pitch-class minimum

Verdict: SKIPPED-SCOPE

Evidence: the bound is real — `readDetectOptions()` reads `min_pitch_classes` with
`readOptionalTicks(..., 1, 13, ...)` (`src/core/ControlChordSupport.cpp:310`), and
there are only 12 pitch classes. But lowering it to 12 is a schema change: the same
bound is published as `integerProperty(1, 13)` in three places
(`src/core/ControlCommandsChord.cpp:240`, `:247`, `src/core/ControlCommandsChordEdit.cpp:402`),
which is what `ControlCommandsSnapshot` measures — the canary is the parent's job
(DELEGATION-RULES rule 8).

Commit: none

Test: none

Red/green: none

Leftovers: cluster 997 (tag-undo after file deletion) and cluster 5582 (edit to a clip
on the unsaved hidden global automation track) are both real and both left unfixed —
they need a surface-owner decision on the inverse mechanism and on lane ownership
respectively. Cluster 3780 needs the parent's ControlCommandsSnapshot bump.