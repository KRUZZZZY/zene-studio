# Space Bunny batch 08 — `core` lane

Six Luna findings, each checked against the current tree
(`bugs/bunny-core-2026-10-05`, head before this batch `c52a96b02`). Line numbers
are from the current tree, not the revisions the findings quoted.

Note on tooling: `mcp__fff__grep` / `multi_grep` returned 0 matches for identifiers
that plainly exist in this tree (`setOptions`, `crossfade`), so its index is stale
for this worktree. Every location below was confirmed with a bounded `rg` plus a
direct read instead.

## Summary

| Cluster | Verdict | Commit |
| --- | --- | --- |
| 1826 — Crossfade length is wrong when one clip is contained in another | FIXED | `230b454d0` |
| 3667 — NaN appearance scales escape the clamp | FIXED | `216fc8d73` |
| 5399 — Completed jobs retain all stem buffers indefinitely | SKIPPED-SCOPE | none |
| 5408 — Tempo map snapshot has a C++ data race | SKIPPED-SCOPE | none |
| 5415 — Frozen-take state races with audio playback | SKIPPED-SCOPE | none |
| 3672 — Crossfades overwrite fades when several clips overlap | SKIPPED-SCOPE | none |

### Cluster 1826 — Crossfade length is wrong when one clip is contained in another
Verdict: FIXED
Evidence: `src/core/ArrangementEdits.cpp:305` (before the fix) computed the
overlap as `earlier->endPosition() - later->startPosition()`, so the span was not
limited to the later clip's end. `Clip::setClipEdits` is a plain assignment
(`include/Clip.h:177`) with no clamp, so the wrong length reached the model
verbatim. With clips `[0, 100)` and `[20, 40)` the code set both fades to 80
ticks while the shared span is 20.
Fix: the overlap ends at whichever clip ends first, which is the span the two
clips actually share. `std::min` keeps it inside both clips, so neither fade can
outlive its clip.
Commit: 230b454d0
Test: `ArrangementModesTest::aCrossfadeIntoAContainedClipStopsAtItsEnd`
Red/green: `cmake --build build --target zene ArrangementModesTest` → EXIT=0;
`cd build/tests && ctest -R '^ArrangementModesTest$' --output-on-failure` → EXIT=8
before the fix (`Actual (earlier->clipEdits().fadeOutTicks): 80`, `Expected: 20`;
14 passed, 1 failed), EXIT=0 after (15 passed).
Also rerun after rebuilding each target, per the stale-binary trap:
`ctest -R '^(RangeEditsTest|ClipJoinRippleTest|ArrangingCatchUpTest|AudioEditingTest|ArrangerTimelineTest)$'`
→ EXIT=8 with four "Not Run" (binaries unbuilt) and one real failure,
`ArrangingCatchUpTest::queuedInstrumentHandlesAreRemovedBeforeTheirTrackCanDie`,
which is unrelated to this change: `build/plugins` contains zero `.so` files, so
`loadInstrument("tripleoscillator")` falls back to `dummyinstrument`. Rebuilt those
five targets first (EXIT=0) and reran; that one case still fails on the missing
plugin artifact and the other four pass. Not caused by, and not fixed by, this commit.
Gates: complexity-gate EXIT=0, file-length-gate EXIT=0.

### Cluster 3667 — NaN appearance scales escape the clamp
Verdict: FIXED
Evidence: `src/core/Appearance.cpp:77` and `:86` (before the fix) guarded the zoom
with `std::clamp` alone, which returns NaN unchanged — every comparison against NaN
is false, so neither the `0.5` nor the `3.0` bound applies. Verified empirically
with a standalone C++20 program rather than assumed. The value was then written to
the config and to the one-line scale file, and `options()` returned it for the
interface to size itself from. Reachable from a command: `requestedAppearance`
(`src/gui/ControlCommandsHelp.cpp:105`) assigns `next.scale = zoom.toDouble(next.scale)`,
which yields a non-finite value for a non-numeric or infinite argument.
One correction to the finding: `Options` has no `fontScale` member
(`include/Appearance.h:53-57`); `font_scale` is a hardcoded `1.0` emitted by
`toJson`, and the command accepts it only as an alias for `scale`. So the
`fontScale` half of the report does not exist; the `scale` half is real.
Fix: `boundedScale()` substitutes a fallback for any non-finite value and clamps
otherwise, on both the read and the write path. A refused zoom keeps the zoom in
force rather than storing a value the interface cannot draw with.
Commit: 216fc8d73
Test: `WorkspaceAccessTest::aNonFiniteAppearanceScaleIsRefused`
Red/green: the first build already had the fix, so RED was captured honestly by
temporarily reverting only the storing path and rebuilding:
`cmake --build build --target zene WorkspaceAccessTest` → EXIT=0;
`cd build/tests && ctest -R '^WorkspaceAccessTest$' --output-on-failure` → EXIT=8
(`Actual (appearance::options().scale): 3`, `Expected (before): 1`; 21 passed,
2 failed — the stored NaN also leaked into
`theTextSizeFieldSetsTheOneZoom`), EXIT=0 after restoring the fix.
Also green: `ctest -R '^HelpAccessTest$' --output-on-failure` → EXIT=0.
Gates: complexity-gate EXIT=0, file-length-gate EXIT=0.

### Cluster 5399 — Completed jobs retain all stem buffers indefinitely
Verdict: SKIPPED-SCOPE
Evidence: the growth is real. `m_jobs` is only ever inserted into
(`src/core/StemJobManager.cpp:142`) and a bounded `rg` over the file and header
finds no `removeJob`, `prune`, `retention`, `maxJobs` or `take(` anywhere, so a
finished job's `stems` stay in the hash for the manager's lifetime, and
`stems(jobId)` (`src/core/StemJobManager.cpp:238`) hands out a copy of the whole
`StemSet`.
Out of scope: the fix the finding proposes is a design decision, not a
behaviour-preserving repair. Whether a result is consumed by the GUI, released by
the caller, or evicted under a bound is a new public API and a new retention
policy, with a real question of what a still-open preview window should see.
That exceeds "change only what the defect needs".
Safe to note for whoever takes it: this is NOT rule 6 territory — the manager runs
its own worker `QThread` (`include/StemSeparation/StemJobManager.h:160`), not an
audio callback, so a bounded-retention fix is possible if someone owns the API
decision. `StemJobManagerTest.cpp` already exists and is the natural test home.
No test run; no product code touched.

### Cluster 5408 — Tempo map snapshot has a C++ data race
Verdict: SKIPPED-SCOPE
Evidence: the described code is present — `snapshot()` at
`include/TempoMap.h:315-327` copies the non-atomic `m_map` under an atomic version
counter (`:332`), and the copy is indeed a benign data race by the letter of the
standard. The header states this trade in as many words at `:274-283` ("Honest
limits, because this is the one place the technique needs saying ... the same trade
every seqlock makes, and it is sound here because there is exactly ONE writer").
The finding also repeats an already-fixed detail: the odd-version retry
(`:326`) is called out in-place as a repaired bug, so that part is stale.
Out of scope by rule 6: the fix the finding asks for — immutable snapshots behind a
reader-safe handoff — is precisely what the current design trades away, and it
costs the audio thread either an allocation or a second buffer to publish into,
both banned on a process/play callback. The reader is also only reached when the
map is active (`active()`, `:286-288`), which is off for every pre-existing project.
No test run; no product code touched.

### Cluster 5415 — Frozen-take state races with audio playback
Verdict: SKIPPED-SCOPE
Evidence: the members exist as described — `m_frozenBuffer` is a plain
`std::shared_ptr<const SampleBuffer>` (`include/Track.h:488`) written by
`freezeTo` (`src/core/Track.cpp:1105`), `clearFrozenTake` (`:1129`) and
`loadFrozenTake` (`:1155`), and read on the audio thread by `playFrozenTake`
(`:1183`, `:1191`, `:1194`, `:1205`) from `InstrumentTrack.cpp:823` and
`SampleTrack.cpp:110`. No transport guard exists in the freeze command
(`src/core/ControlCommandsFreeze.cpp:203-208` checks only `freezeTo`'s own error
string), so the interleaving is reachable. The header does document the intended
invariant at `include/Track.h:483-486` — the buffer "is loaded on the control
thread and never touched by the audio thread while the take is frozen", with
`m_frozenTakePlaying` as the one member the audio thread writes.
Out of scope by rule 6: a thread-safe handoff for a `shared_ptr` on this path means
either atomic refcounting or a deferred release, and `playFrozenTake` already
allocates (`new Sample`, `:1205`). Restructuring the handoff belongs with the
frozen-take design, not a bug-fix batch. Note the inconsistency is narrower than
reported: the audio thread does not write `m_frozen`/`m_frozenBuffer`, it only
reads, so this is a read/write race on the control thread's fields rather than a
partially-updated take state within the audio thread.
No test run; no product code touched.

### Cluster 3672 — Crossfades overwrite fades when several clips overlap
Verdict: SKIPPED-SCOPE
Evidence: the overwrite is real. `applyOverlap` loops over every overlap
(`src/core/ArrangementEdits.cpp:378-383`) and each call to `overlapOne` reaches
`crossfade`, which assigns `out.fadeOutTicks` / `in.fadeInTicks` outright
(`:313-322`), so with a clip moved across two earlier clips only the last
overlap's length survives on the moved clip.
Out of scope: the fix is a design decision about what a clip overlapped by
several clips should fade across — one combined span, or per-pair fades that
cannot both be expressed on one clip. That changes observable crossfade behaviour
for a case with no stated correct answer, which is a design change rather than a
behaviour-preserving fix, and it is adjacent to the cluster 1826 work above (same
function, same overlap arithmetic). Worth pairing with an explicit product decision
when someone owns the crossfade semantics. No test run; no product code touched.

Leftovers: four findings are classified with evidence but unfixed — 5399 needs an
API/retention decision (safe to fix off the audio thread), 5415 and 5408 need a
rule-6-compliant handoff, and 3672 needs a product decision on multi-overlap fade
semantics. `ArrangingCatchUpTest::queuedInstrumentHandlesAreRemovedBeforeTheirTrackCanDie`
fails on this worktree because `build/plugins` has no `.so` files; that is a stale
build artifact, not a code defect, and it predates this batch. `mcp__fff__`'s index
is stale for this worktree and matched nothing that exists; bounded `rg` plus direct
reads were used instead.
## Rework correction (2026-10-08)

Source: `f88de9f79` (REWORK in `review-core.md`, item 4).

- Cluster 1826 is narrowed from FIXED to: the stored **length** of a contained-clip crossfade is now the
  shared span (`230b454d0`), and the later clip's rendered fade-in is checked (`e318ae28c`, test
  `ArrangementModesTest::aCrossfadeIntoAContainedClipRendersAFadeInThatReachesFullGain`).
- Not fixed, verdict BLOCKED: the **earlier** clip's fade-out is anchored to its tail (`ClipEdits` fades
  are head/tail-anchored), so for `[0,100)` with a contained `[20,40)` it still covers `[80,100)`, not the
  shared span `[20,40)`. A correct containment crossfade needs an owner decision: split the containing clip
  at the contained clip's end (which changes the moved clip's identity when it is the container), or add an
  offset fade to `ClipEdits`. Neither was done in the rework.
