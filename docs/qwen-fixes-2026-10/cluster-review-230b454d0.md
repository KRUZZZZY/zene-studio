FALSE-POSITIVE

# cluster review-230b454d0 — contained-clip crossfade windows

**Finding.** "Contained clip crossfade fade windows do not overlap": for `earlier [0, 100)` and `later [20, 40)` the
new length clamp bounds both ramps to the shared 20 ticks, but the earlier clip's 20-tick fade-out still applies at
`[80, 100)` (its own end) and the later clip's fade-in at `[20, 40)`, so the shared span is not crossed. The finding
asks for "existing fade design decision or architecture support for aligned overlap-window gain".

**Verdict.** FALSE-POSITIVE. Both things the finding says are missing exist in this tree, and they say the reported
behaviour is the intended contract:

- The design decision: `docs/m0-design/decisions-r3-clips.md:19` — "Default crossfade mode uses only edge fades with
  zero offsets, bounded by the clips' actual overlap. For a contained pair, the ordinary mode never writes an offset
  fade on the outer clip." Same file `:21` — "The audible change is intentional and limited to newly requested default
  crossfades: they no longer duck the outer clip inside the inner clip's bounds", and "Keep offset-fade support, the
  prior save/load defaults for old files, legacy pair repair, and one-step undo." This is the 2026-10-09 owner ruling,
  committed at `0ce68cced6` ("fix(clips): honor linked windows and clamp crossfades (cluster r3-clips)") and pinned in
  the test suite at `2e4722cfb3` ("pin the 2026-10-09 contained-crossfade ruling in ArrangementModesTest").
- The architecture support: the alignment is the *explicit* container gesture — `Alt` at drop, or
  `clip.crossfade mode: "container"` — which writes the paired offset fades. `src/core/ContainedCrossfade.cpp:75-101`
  (`apply`) puts the outer fade-out over `[contained start, +entry)` and the outer fade-in over
  `[contained end - exit, contained end)`, exactly the "aligned overlap-window gain" the finding asks for.

**Code evidence (read this session).**

- `src/core/ArrangementEdits.cpp:373-397` — `crossfade()` is the default (clamp) writer. `:375-376` computes the
  overlap as `min(earlier end, later end) - later start`, which for `[0,100)` / `[20,40)` is the true shared span, 20.
  `:380` sets `out.fadeOutTicks = overlap`, `:382` forces `out.fadeOutOffsetTicks = 0`, `:387` sets
  `in.fadeInTicks = overlap`, `:389` forces `in.fadeInOffsetTicks = 0`: the lengths are clamped, the windows are the
  clips' own edges. That is the finding's arithmetic, and it is the ruling's sentence implemented.
- `src/core/ClipEdits.cpp:238-246` — `fadeOutFactor()` measures the ramp inward from the clip's last output frame when
  the offset is 0, so a 20-tick fade on a 100-tick clip applies at `[80, 100)`. The finding's "applies 80..100" is this
  line, by design.
- `src/core/ArrangementEdits.cpp:402-414` — `overlapOne()`: for a contained pair the container is always the `earlier`
  argument (`:411`, the order test `other->start < start`), so the clamp path keeps the container's ramp at the
  container's own edge. A plain overlap (`earlier` ends where the shared span ends) crosses exactly.
- `src/core/ArrangementEdits.cpp:349-369` — `crossfadeContainer()`, the explicit gesture, computes the entry/exit ramps
  from the contained clip's authored fades and calls `containedcrossfade::apply`.
- `src/core/ControlCommandsClipEdits.cpp:196-221` — `crossfadeSidesFor()`: the command's mode defaults to `"clamp"`
  (`:199-200`) and uses `crossfadeSides` (edge fades, offsets zeroed at `:144-152`); `"container"` is explicit and goes
  to `containedSides()` (`:169-193`). `:534-546` computes the command's overlap as the true intersection
  (`overlapEnd - overlapStart`), so the clamp length is right on this path too.
- `src/core/ClipEdits.cpp:202-213` — `withValidOffsetFades()` drops a lone offset (`isOffsetFadePair` fails →
  `clearOffsetFades`). This is why "move only the outer window" is not representable: an aligned duck is a *pair*, and
  the pair is the V the container gesture writes.
- `tests/src/core/ArrangementModesTest.cpp:142-158` already pins the ruling (ramp lengths 20, every offset 0),
  `:161-182` pins the explicit V (outer offsets 70 and 30, i.e. ramps at `[20,30)` and `[30,40)`), and `:183-200` pins
  the contained clip's rendered fade-in.

**History (so the REWORK is answered, not ignored).** `230b454d08` (2026-10-06) is this cluster's original
"crossfade outlasted the clip" fix. The earlier attempt on this cluster, `e318ae28c4` (2026-10-08, "a contained
crossfade's rendered fade-in was never checked"), added the rendered-fade-in check that is still in the tree
(`ArrangementModesTest.cpp:183-200`). The window-alignment question was then ruled on by the owner on 2026-10-09
(`decisions-r3-clips.md` section 2) and re-anchored at `2e4722cfb3`. Nothing in the current code contradicts the ruling.

**What I added.** One contract test, no product change:
`tests/src/core/ArrangementModesTest.cpp:202-239`
`theDefaultCrossfadeDoesNotDuckTheOuterClipUnderTheContainedClip`. It is the rendered-envelope evidence the finding
says is missing ("rather than only length assertion"): with `earlier [0,100)` / `later [20,40)` it asserts that the
default crossfade leaves the outer clip at gain 1.0 over every frame of `[20, 40)`, that the clamped ramp is silent at
frame 99 and full at 79 (its own edge, `[80, 100)`), and that the explicit container gesture is the one that ducks —
1.0 at frame 20, below 0.75 at 25, silent at 30 and back above 0.9 at 39. Ticks stand in for frames; the ramps are
ratios and `clipFadeGainAt` is allocation-free arithmetic (`src/core/ClipEdits.cpp:250-265`), so the test is
deterministic: no playback, no clock, no sleeps.

**Test matrix (procedure step 3).**

| case | expectation | covered by |
|---|---|---|
| normal: default crossfade on a contained pair | edge-only ramps, outer unity across the shared span, ramp at `[80,100)` | new test, frames 20-39 and 79/99 |
| explicit: container gesture on the same pair | outer ducks to 0 at the contained clip's midpoint and recovers | new test, frames 20/25/30/39 |
| failure: container gesture without authored inner fades | typed refusal, no edits written (pre-existing) | `ContainedCrossfade.cpp:56-70` refusal; `ClipOffsetFadePairTest::aContainedClipUnderTwoTicksReportsNoCrossfade` |
| exhaustion / capacity | none introduced: no pool, queue or persistent object; `ClipEdits` is a fixed struct | no product change |
| reconfiguration | none: no setting, command or schema changed | no product change |
| audio thread | nothing new on the render path; `clipFadeGainAt` is arithmetic on members | no product change |
| platforms | QtTest plus the file's existing helpers only, no platform API | builds with the tree's normal flags |

**Commands and exit codes** (logs in
`/home/kruzzzzy/.config/kads/zene/state/runs/review-230b454d0.r1/logs`; builds from the worktree, ctest from
`build/tests`):

| step | command | log | exit |
|---|---|---|---|
| baseline build | `cmake --build build --target zene ArrangementModesTest -j4` | `build-1.log` | 0 |
| baseline test | `ctest -R ArrangementModesTest --output-on-failure` | `test-1.log` | 0 |
| build with the new test | same build command | `build-2.log` | 0 |
| run with the new test | `ctest -R ArrangementModesTest --output-on-failure` | `test-2.log` | 0 |
| teeth: temporary product mutation (aligned-window writer) saved | `git diff -- src/core/ArrangementEdits.cpp > logs/mutation.patch` | `mutation.patch` | 0 |
| build with the mutation | same build command | `build-3.log` | 0 |
| run with the mutation | `ctest -R ArrangementModesTest --output-on-failure` | `test-3.log` | **8** (17 passed, 2 failed) |
| revert the mutation | `git apply -R logs/mutation.patch` | — | 0 |
| build after the revert | same build command | `build-4.log` | 0 |
| run after the revert | `ctest -R ArrangementModesTest --output-on-failure` | `test-4.log` | 0 |
| fade/arrangement family build | `cmake --build build --target zene ArrangementModesTest ClipOffsetFadeTest ClipOffsetFadePairTest ClipEditsTest ClipFadesRenderTest CrossfadeEditorTest -j4` | `build-5.log` | 0 |
| fade/arrangement family run | `ctest -R "^(ArrangementModesTest\|ClipOffsetFadeTest\|ClipOffsetFadePairTest\|ClipEditsTest\|ClipFadesRenderTest\|CrossfadeEditorTest)$" --output-on-failure` | `test-6.log` | 0 (6/6) |
| gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | `gates-1.log` | 0 (`RESULT: PASS`, 8/8 gates PASS) |

The mutation in `logs/mutation.patch` is the finding's requested behaviour expressed in product code: it sets the
outer clip's `fadeOutOffsetTicks` so the ramp spans `[20, 40)` and adds the paired fade-in from 40, which is valid
under `withValidOffsetFades`. With it, `test-3.log` records
`FAIL! : ArrangementModesTest::theDefaultCrossfadeDoesNotDuckTheOuterClipUnderTheContainedClip() ... Actual
(outerGain(frame)): 0.9969173 ... Expected (1.0f) ... Loc: [../../tests/src/core/ArrangementModesTest.cpp(221)]` and
`FAIL! : ArrangementModesTest::aCrossfadeIntoAContainedClipStopsAtItsEnd() ... Actual
(earlier->clipEdits().fadeInTicks): 20  Expected (0)`; after the revert, `test-4.log` is green. The new test therefore
has teeth against the change the finding proposes.

`test-5.log` also exited 8, but for an unrelated reason: the anchored regex used there was unanchored, so
`ClipEditsTest` also selected `LinkedClipEditsTest`, whose target was not built (`Unable to find executable`). The
anchored rerun is `test-6.log`, exit 0, 6/6 tests.

The one-line summary of the gate table from `gates-1.log`:
`fork-sources PASS, all-sources-reproduce PASS, no-upstream-regression PASS, check-namespace PASS, no-tautology PASS,
doc-figures PASS, complexity PASS (no new item in your files), file-length PASS (no new item in your files),
RESULT: PASS`.

## Self-check

1. **Test fails with the product change reverted, passes with it.** There is no product change: the verdict is
   FALSE-POSITIVE, so the added test is a contract pin rather than a regression fix. Its teeth are shown the other way
   round: injecting the finding's requested aligned-window writer as a temporary product mutation
   (`logs/mutation.patch`) makes `ArrangementModesTest` exit 8 with the new test failing at
   `ArrangementModesTest.cpp:221` (`0.9969173` against `1.0f`, `test-3.log`), and `git apply -R` restores exit 0
   (`test-4.log`). Both builds were rerun around both runs (`build-3.log`, `build-4.log`, both exit 0).
2. **Deterministic.** The test drives `arrangement::applyOverlap` and then evaluates `clipFadeGainAt` at literal
   frames; ticks stand in for frames because the ramps are ratios. No engine playback, no audio device, no clock, no
   sleeps, no threads: the values are injected frames and the assertions are fixed numbers. QtTest reports 17 passed
   in `test-4.log` with the new case among them.
3. **Cause and completeness, every caller and sibling.** There is no changed behaviour to complete, but both entry
   points into the default crossfade were checked and are consistent with the ruling: the drop path
   (`src/core/ArrangementEdits.cpp:402-414` → `crossfade()` `:373-397`) and the command path
   (`src/core/ControlCommandsClipEdits.cpp:196-221`, default mode `"clamp"`, overlap at `:534-546`). The explicit
   gesture is consistent on both too: `crossfadeContainer()` (`ArrangementEdits.cpp:349-369`) and `containedSides()`
   (`ControlCommandsClipEdits.cpp:169-193`) both call `containedcrossfade::apply` (`ContainedCrossfade.cpp:75-101`).
   The renderer's offset-pair rule (`ClipEdits.cpp:202-213`) was checked so the report does not claim a representation
   that the render would drop.
4. **No collateral damage.** No product file, save format, loaded field, limit or check changed: the only repository
   change besides this report is one test case. Old projects still load and render through the same code paths
   (`withValidOffsetFades` and `fadeOffsetGap` untouched), and the fade/arrangement family passes (`test-6.log`, 6/6,
   exit 0) — `ArrangementModesTest`, `ClipOffsetFadeTest`, `ClipOffsetFadePairTest`, `ClipEditsTest`,
   `ClipFadesRenderTest`, `CrossfadeEditorTest`. Undo is untouched (no command or gesture changed), and no control
   command was added, so the `ControlCommandsSnapshot` canary is unmoved.
5. **Evidence for FALSE-POSITIVE.** Intended contract: `docs/m0-design/decisions-r3-clips.md:19` and `:21` (edge fades
   with zero offsets by default, no duck of the outer clip inside the inner clip's bounds, offset-fade support kept).
   Code: `src/core/ArrangementEdits.cpp:373-397` (the zeroed offsets at `:382` and `:389`), `src/core/ClipEdits.cpp:238-246`
   (the edge-anchored ramp), `src/core/ControlCommandsClipEdits.cpp:199-200` (mode defaults to `"clamp"`), and the
   explicit gesture at `src/core/ContainedCrossfade.cpp:75-101`. Tests that already pin it:
   `tests/src/core/ArrangementModesTest.cpp:142-158` and `:161-182`.

## Other findings

- `clip.crossfade` with the *container* passed as the `in` clip (out = contained) uses the same clamp math: the
  container's fade-in lands over its first 20 ticks, before the contained clip starts, and the contained clip fades out
  over its whole length. That is the same ruling (default = edge fades, no offsets), not a second defect, and I did not
  touch it. It is only reachable through the command's explicit out/in order; the drop path always passes the container
  as `earlier` (`src/core/ArrangementEdits.cpp:411`).
- `withValidOffsetFades` (`src/core/ClipEdits.cpp:202-213`) silently drops a lone `fade_out_offset` rather than
  refusing it, so a saved clip that drifts into a lone offset renders as plain edge fades. This is the documented
  repair path (`docs/m0-owner-decisions-2026-10-08.md`, round 3 R2), not a new problem; noted only because the finding's
  "move just the outer window" would be dropped by it.
