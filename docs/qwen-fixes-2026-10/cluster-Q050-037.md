IMPLEMENTED

Q050-037 - define the automation value before its first point. Feature record:
`docs/050-features/Q050-037.md`. Focused logs are in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-037.r2/logs`.

## Acceptance

| Criterion | Command (from `build/tests`, `QT_QPA_PLATFORM=offscreen`) | Result |
|---|---|---|
| Times before the first node read the authored parameter's initial value, or an explicit clip-start value when present, rather than zero | `ctest -R "^(AutomationPreFirstValueTest\|SampleAccurateAutomationTest)$" --output-on-failure` (`build-4.log` EXIT=0; `test-2.log`) | EXIT=0. `beforeTheFirstNodeReadsTheParameterInitialValue`, `aPointAtTheClipStartIsTheExplicitStartValue`, `theAudioRampHoldsTheSamePreFirstValue` and `SampleAccurateAutomationTest::aBlockBeforeTheFirstPointReadsThePreFirstValue` pass; the last one now expects the parameter's inverse-scaled initial value instead of `0`. |
| Seek or start before the first node, stop, and return to start; playback reports the same value each time | same command (`test-2.log`) | EXIT=0. `playbackReportsTheSameValueAtEveryReturnToStart`: real blocks through `Song::processNextBuffer()` at tick 0 (from a stale 0), 150 (inside the curve), then 0, 0 and 20 all report `0.72`. |
| Save and reload a project with a later first node: pre-first contract stable, interpolation at/after the first node unchanged | same command (`test-2.log`) | EXIT=0. `saveAndReloadKeepsThePreFirstContract`: project-owned master volume (initial 100), first node tick 50; after `saveProjectFile`/`loadProject` the pre-first read is again 100 and the values at 50, 125 and 300 equal the pre-save reads. |
| Per-target initial values on the ramps `Song` actually publishes (review r1) | same command (`test-2.log`) | EXIT=0. `eachTargetHoldsItsOwnInitialValueThroughSongPublish`: two targets (0.72 / 0.35) read their own value on every frame of a block before the first node through `Song::buildAutomationRamps`, and both read the shared curve at the first node. |
| Neighbour regression sweep | `ctest -R "^(AutomationModesTest\|AutomationLanesTest\|AutomationTrackTest\|AutomationPrepassProofTest\|AutomationRenderAllocationTest\|ControlAutomationModesTest\|AutomationWriteToEndTest\|AutomationThinTest\|AudioAutomationToolsTest\|AutomationPreFirstValueTest\|SampleAccurateAutomationTest)$" --output-on-failure` (`build-5.log` EXIT=0; `test-3.log`) | EXIT=0, 11/11 passed. |
| Realtime sweep with the changed render callees declared | `python3 tests/rt-safety-sweep.py --check` (`rt-sweep-1.log`) | EXIT=0; 73 declared paths, 0 fresh hits, 3 allowlisted keys unchanged (no new allowlist entry). |
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py $PWD` (`feature-check-1.log`) | EXIT=0; fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity and file-length all PASS. |

**RED evidence (product behaviour reverted, test APIs kept so the tests still
compile).** Temporary reverse hunks: `preFirstValueLocked()` returned `0`, and
`TrackContainer::collectAutomatedValues()` always wrote the one `valueAt()` value.
`cmake --build build --target zene AutomationPreFirstValueTest SampleAccurateAutomationTest -- -j4`
(`build-reversed-1.log`) EXIT=0; then
`ctest -R "^(AutomationPreFirstValueTest|SampleAccurateAutomationTest)$" --output-on-failure`
(`test-reversed-1.log`) **EXIT=8**, 0/2 targets passed:
`beforeTheFirstNodeReadsTheParameterInitialValue` (fuzzy compare),
`theAudioRampHoldsTheSamePreFirstValue` ("frame 0 of the ramp reads 0, not ... 0.72"),
`eachTargetHoldsItsOwnInitialValueThroughSongPublish`,
`playbackReportsTheSameValueAtEveryReturnToStart`,
`saveAndReloadKeepsThePreFirstContract`, and
`SampleAccurateAutomationTest::aBlockBeforeTheFirstPointReadsThePreFirstValue`
("frame 0 ... reads 0, not ... 50"). Restored with `git checkout --` and rebuilt:
`build-restored-1.log` EXIT=0; the same ctest (`test-restored-1.log`) EXIT=0.

## Self-check

- **Each new test fails with the product change reverted, then passes restored.**
  Yes, evidence above: reversed build EXIT=0 -> ctest EXIT=8 with the five new
  pre-first cases plus the changed `SampleAccurateAutomationTest` slot failing;
  restored build EXIT=0 -> ctest EXIT=0.
- **Every acceptance criterion is proven by a test I ran.** Yes: three acceptance
  rows plus the multi-target review row in the table above, all from
  `test-2.log` EXIT=0 and the neighbour run `test-3.log` EXIT=0.
- **One undo step per edit, honest reversibility class.** No command was added or
  changed (the unit explicitly adds none), so there is no new command metadata,
  undo step or reversibility class. Editing automation still goes through the
  existing commands and their existing journal behaviour; the fix only changes
  what the existing read returns.
- **Nothing added on the audio thread allocates, frees, locks, waits, does I/O or
  emits a signal.** Changed audio-path functions: `Song::buildAutomationRamps`
  (builds one fixed-capacity `AutomationRamp`, copies it for a differing target,
  rewrites at most `MaxKnots` values with `AutomationRamp::setValueBefore`; no
  heap, no new lock beyond the existing single `writeBlockRamp` clip lock) and
  `TrackContainer::collectAutomatedValues` (writes the pre-existing bounded
  scratch; the clip lock is still the single `valueAt()` lock it always took,
  now returning the pre-first flag from inside it). Neither emits a signal or
  waits. Both are declared in `tests/rt-safety-scope.txt`; the sweep is EXIT=0
  with no new allowlist line. The runtime allocation probe in
  `SampleAccurateAutomationTest` also passes. Local TSan does not run on this
  box; the race argument is by design only.
- **Old projects still load and render exactly as before; new data round-trips.**
  No serialized field was added: nodes and target ids are written/read exactly as
  before (`AutomationClip::saveSettings`/`loadSettings` untouched), so files and
  undo checkpoints are byte-compatible. The only render difference is the
  intended pre-first correction; all values at and after the first node are
  unchanged (proven by the save/reload slot and the neighbour tests). The
  save/reload slot round-trips a later-first-node project through
  `saveProjectFile`/`loadProject`.
- **Portability (Qt 5.15 / MSVC / macOS < 10.15).** No new Qt API at all; the
  change uses `std::ceil`, `std::min` and plain floats, no `std::filesystem`, no
  windows.h macro names (`small`/`near`/`far`/`interface`/`min`/`max` are not used
  as identifiers), and no POSIX-only call. The header change is a defaulted
  pointer argument and a bool out-parameter, valid in C++20 on every hosted
  target.
- **GUI and agent surface.** This unit adds no user-facing action and no command:
  it fixes the value the existing automation read returns during playback. The
  existing automation authoring GUI (`automation.add_point` through the registry,
  the automation editor and the clip context menus) and the existing agent
  `automation.*` commands are unchanged, so there is no new command id,
  reversibility class, undo step or GUI control to add or test. The pre-submit
  check's WARN lines for "GUI path" and "agent command" are therefore
  not-applicable rather than missing work (`submit-check-1.log`: RESULT PASS,
  both WARNs, every FAIL item PASS).
- **Pre-submit completion check.** `python3
  ~/.local/share/zene-bunny-fix/qwen/submit_check.py $PWD Q050-037`
  (`submit-check-1.log`) EXIT=0, RESULT: PASS - submit; the only warnings are the
  not-applicable GUI/agent lines above.
