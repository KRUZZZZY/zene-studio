# Space Bunny fixer lane `core` — batch-01

Worktree: `zene-bunny-core`, branch `bugs/bunny-core-2026-10-05`. Six findings reviewed against the
current tree. No product code was changed; all six hypotheses resolved from source evidence.

Verdicts: FIXED 0, FALSE-POSITIVE 2, SKIPPED-SCOPE 3, REAL-UNTESTED 1.

### Cluster 5526 — Changing ramp mode during playback races with ramp evaluation

Verdict: REAL-UNTESTED
Evidence: `include/AutomationClip.h:200-204` — `sampleAccurate()` reads `m_rampSetting` and
`m_progressionType` unlocked, while writers hold `m_clipMutex` (`src/core/AutomationClip.cpp:699-703`
`setRampSetting`, `:126-138` `setProgressionType`). The audio-thread reader is
`src/core/Song.cpp:1255` via `buildAutomationRamps`, invoked per block at `src/core/Song.cpp:418`.
Sibling cross-thread flags on the same class are already `std::atomic`
(`include/AutomationClip.h:286,231,338`), so atomics are the intended convention here.
The report's shape is correct, but a data race cannot be made deterministically RED headlessly
(it needs ThreadSanitizer plus a racy playback/edit harness). Per procedure, no product change
without a failing regression test.
Commit: none
Test: none
Red/green: none

### Cluster 72 — Clip edits race with playback

Verdict: SKIPPED-SCOPE
Evidence: `include/Clip.h:177` — `setClipEdits` is a plain assignment. Audio-thread readers are
`src/tracks/InstrumentTrackNotes.cpp:67` in `decorateNoteHandle` (the AUDIO THREAD contract is
documented at `include/ChordFollow.h:37`), reached from `triggerNote` at `:212` via
`src/tracks/InstrumentTrack.cpp:937`, plus `:224,230` in `passEnd`/`loopedPosition`. Writers are
control commands (`src/core/ControlCommandsClipEdits.cpp:202,268,374-375`). (Correction: the original wording here said
that rule 6 forbids *any* snapshot publication or synchronization on the audio path — that was too
broad. Rule 6 forbids allocation, locking and unbounded growth on audio-thread callbacks; bounded
atomics and a preallocated snapshot publication can comply.) Any minimal fix in this lane would still
be an API/design change beyond a minimal patch; the minimal proven fix stays out of this lane's scope.
Commit: none
Test: none
Red/green: none

### Cluster 5555 — Clip source mapping does not clamp to the clip's source range

Verdict: FALSE-POSITIVE
Evidence: the frame-domain mapping that can address nonexistent source data is
`SampleClip::sourceFrameAt`, which already clamps at `src/core/SampleClip.cpp:515`
(`std::clamp(..., m_window.sourceIn, m_window.sourceOut)`) and `SampleClip::timelinePosAt` at
`:528`; the behaviour is pinned by `tests/src/tracks/SampleClipWarpTest.cpp:282-283`. The base
mapping at `include/Clip.h:368-378` has no window or source range to clamp against (it is a
tick-domain identity for MIDI/pattern/automation clips, `include/Clip.h:364-367`), so clamping it
would change existing callers (`src/core/TakeLane.cpp:332`, `src/core/ControlCompSupport.cpp:155`).
Commit: none
Test: none
Red/green: none

### Cluster 5553 — Lane index zero cannot distinguish "no lane" from the first lane

Verdict: SKIPPED-SCOPE
Evidence: 0 is both the default and a valid index — `include/Clip.h:194-195,354`, and
`src/core/TakeLane.cpp:104-118` `addLane` starts numbering at 0; consumers index directly with it
(`src/tracks/InstrumentTrack.cpp:802,806`, `src/core/TakeLane.cpp:344`). The behaviour is documented
by design at `include/Clip.h:179-193` ("0 by default", absent attribute → 0, byte identity). A fix
would change the on-disk lane attribute vocabulary and the comp control schemas — a design change.
Commit: none
Test: none
Red/green: none

### Cluster 5554 — Link group zero cannot distinguish "unlinked" from a valid group

Verdict: FALSE-POSITIVE
Evidence: `src/core/ClipLinks.cpp:103-110` — `allocateGroupId()` explicitly never returns 0
(`return id > 0 ? id : 1;`, with the comment "Never 0: 0 is 'unlinked'"), so group IDs start at 1 and
the sentinel is unambiguous. All consumers gate on `> 0` / `<= 0`
(`src/tracks/MidiClip.cpp:203`, `src/core/ClipLinks.cpp:84,217`,
`src/core/ControlCommandsClipLinkState.cpp:101,158`). The report's premise that IDs start at 0 is
simply wrong for this codebase.
Commit: none
Test: none
Red/green: none

### Cluster 5054 — DPI scaling can double-scale widget geometry

Verdict: SKIPPED-SCOPE
Evidence: `include/DpiHelper.h:41-51` scales by `screen->devicePixelRatio()` and states the intent
at `:36-40`; the round-trip is self-consistent — scaled in at `src/gui/tracks/TrackView.cpp:110` and
converted back to model design units by `unscaledPixels` at `:537`. Qt6 auto-scales widget geometry
(`src/core/main.cpp:599-601`), so the observation is plausible, but changing it restyles every widget
across 10+ call sites (`src/gui/tracks/TrackView.cpp:110,196,208,266,331,335,425,536-537`,
`src/gui/widgets/ComboBox.cpp:57`, `src/gui/editors/TrackContainerView.cpp:537`) and cannot be
verified headlessly — a UI design decision, not a defect fix.
Commit: none
Test: none
Red/green: none

Leftovers: cluster 5526 remains an open race in reality but is unproven deterministically from a
headless test; if the parent wants it closed, the TSan suite or an atomic-publication patch (matching
the existing `std::atomic` siblings at `include/AutomationClip.h:286,231,338`) is the route, and it
should land under `src/core/automation` rather than this report lane.