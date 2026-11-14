# Rune UI rework: implementation plan

Status: owner-approved top feature category (2026-11-14), difficulty 3; implementation remains gated by the approved Phase 0 feasibility report. This does not
authorize a release, a default-UI switch, or retirement of the Qt UI. The independent interaction
contract is in [RUNE-INTERACTION-CONTRACT.md](RUNE-INTERACTION-CONTRACT.md); it is the source of
truth for gesture, focus, keyboard, and state-feedback details.

## Product outcome

Deliver Rune as the product's front end to the existing Zene engine. The UI and engine are separate
processes and communicate through the existing control surface plus additive, versioned support where
measurements show it is needed. Keep the engine usable without a Qt window and reusable by other
front ends and agents. Retain the current Qt UI throughout the parity period.

The first complete musician journey is: open or create a project; add an instrument or audio; hear
real rendered sound; edit clips and notes; set tempo and loop; balance tracks and apply an effect;
undo and redo; save and reopen; render an output. Recording is an additional journey requiring real
device input, monitoring, and preservation of the take. Every supported target is in scope from the
start: Linux, Windows (MSVC and MinGW where the product supports them), and macOS.

## Preconditions and boundaries

- Phase 0 is already approved. Product integration starts only after its written report records a
  go decision for real-engine use, latency, all-platform builds/runs, native plug-in windows, and
  accessibility, with unresolved limitations named. A failed Phase 0 gate returns to owner review;
  these implementation tasks do not waive it.
- The current Rune prototype is a useful interaction and visual reference, but its demo playback,
  demo project, UI-local undo, and headless checks are not product evidence. Replace those behaviors
  with the real engine and real project state as each surface is integrated.
- Reuse the existing engine and project format. No wholesale engine rewrite, Qt Rune-look skin,
  separate preview binary, or silent removal of current Qt workflows. Engine/control-surface changes
  are additive, justified by observed integration needs, separately covered, and go through the
  normal product train and gates.
- Preserve existing save/load, undo journal, audio-thread, real-time, plug-in, compatibility,
  validation, Terra, and release gates. Required-fix, data-safety, and validation work keeps its
  established priority. These tasks must not be used to bypass Terra dispatch, independent review,
  issue budgets, integration recovery limits, or promotion policy.
- Feature lanes may implement and review only when the live runtime policy makes them eligible.
  All tasks remain subject to current queue resource, model, usage, and independent-review limits.
- A separate dependency-ordered task inventory is in `rune-rework-tasks.json`. Its `difficulty: 3`
  is an integer queue classification on every unit; the root coordinator owns insertion and runtime
  queue wiring. The top-level category is `rune-ui`.

## Cross-cutting evidence and review

Each work unit has three distinct proof layers:

1. **Deterministic proof:** focused regression coverage for the behavior, including state transitions,
   boundary/error cases, save/reopen or undo where relevant, and stable headless UI tests. Run the
   affected tests, full applicable test suite, and unchanged required gates at their normal merge
   points. A headless UI check does not establish real audio or human usability.
2. **Engine/platform proof:** execute the same meaningful scenario against the real engine and
   project state. Record command/reply and subscription evidence, state before/after, render or
   engine meter/peak evidence for audio, and platform/build details. Use a real device only in an
   explicitly arranged, safe session; Dummy/offline operation cannot be described as audible output.
3. **Terra human-use proof:** after each user-facing unit, Terra exercises the named scenario on a
   runnable build, observes the result, and records state evidence plus audio evidence when the
   scenario claims sound. Keep human-use review in Terra's existing centralized review and repair
   flow, with independent acceptance and budgets. Do not duplicate a parallel review queue. Screenshots
   can show visible state, but never prove audio, correct engine state, latency, or accessibility alone.

Every unit must include failure and recovery behavior, preserve accessibility semantics, and keep
audio-thread work bounded and free of allocation/locking where the existing engine contract requires
it. No task marks a user-facing feature done from mocks alone. Platform evidence distinguishes local
measurements from hosted CI and from native-device manual checks.

## Dependency-ordered work units

The JSON inventory holds the implementation-ready acceptance text and dependency identifiers. This
section defines the intended sequencing and exit gates; implementation details remain with the lane.

### Stage A — feasibility, connection, and trustworthy state

1. **Phase 0 feasibility gate.** Complete the approved real-engine, latency, platform, native editor,
   and accessibility spikes and publish the measured go/no-go. No product integration starts on a
   no-go or unresolved blocker.
2. **Process lifecycle and engine connection.** Launch and supervise the existing engine without a Qt main window and Rune UI,
   connect through the control socket on all supported platforms, report connection/startup failures,
   and close without losing the project. Prove the existing control contract is enough for the initial
   UI and record additive gaps rather than broadening it speculatively.
3. **Live project state and change delivery.** Populate the Rune model from an actual project and keep
   transport, tracks, clips, notes, mixer, and selection synchronized with engine changes. Any
   subscription or high-rate/bulk data addition is additive, measured, versioned, and tested for
   reconnect, stale data, and bounded resource use.
4. **Transport and audible playback.** Play, stop, seek, tempo, loop, and position work against the
   engine. Prove sound by offline render comparison/peaks or a documented real-device session; meter
   and playhead updates remain responsive under load.

### Stage B — composing and mixing

5. **Browser and arrangement first composition flow.** Browse/search installed content, add an
   instrument or audio to a track, then select, move, resize, duplicate, split, mute, and delete
   supported audio/MIDI clips. Make content/load failures clear; edits are engine-backed, undoable,
   and reflected after external state updates. Demonstrate inserted material in engine state and
   hear/render it.
6. **Piano-roll editing.** Open a MIDI clip and add, move, resize, remove notes and edit velocity;
   note edits survive undo/redo and project round trip and produce the expected rendered MIDI
   instrument output.
7. **Mixer, routing basics, and effects.** Show real track/master meters, volume, pan, mute/solo,
   and insert/bypass an effect using the current engine. Verify routing and rendered output, including
   bypass/undo behavior.

### Stage C — durable musician workflow

8. **Project lifecycle and history.** Create/open/save/reopen real projects through the engine's
   established format and undo journal. Preserve compatibility, dirty-state prompts, errors, and
   recovery behavior; no UI-local snapshot undo may claim to replace engine history.
9. **Render and export.** Configure and run a real engine render, show progress/cancel/failure
   behavior, and verify the resulting file's format, duration, and non-silent content where expected.
10. **Recording and take safety.** Arm a supported real input, monitor and record, stop, inspect the
    resulting take, save/reopen it, and recover safely from interruption. Prove capture and take data
    with engine state and audio/file evidence; Dummy input is not a recording pass.

### Stage D — host integration and adoption readiness

11. **Native plug-in editors and built-in parameter editing.** Open/focus/close supported native
    editor windows beside Rune on each target platform and provide a usable Rune parameter view for
    built-ins without a bespoke view. Preserve host state and automation behavior; inventory
    limitations rather than claiming all plug-ins equivalent from one example.
12. **Accessibility, staged parity, and adoption gate.** Keyboard navigation and command discovery,
    focus/selection cues, screen-reader roles/names/states, scaling, and contrast work for the
    first-session journey on all platforms. Build a traceable inventory from the current control
    surface, advertised feature set, current Qt workflows, and supported platforms; classify each
    workflow as equivalent, intentionally deferred with owner acceptance, or blocked. Run Terra
    human sessions on the complete journey and platform checks; resolve genuine findings through
    normal priority/review flow. Keep Qt available until the owner accepts parity and separately
    decides default adoption. Qt retirement is a later explicit decision that must address its tests
    and build line; this task cannot remove it. Validate accessibility with platform tools; a
    screenshot is not accessibility evidence.

## Parity and release gates

Parity is staged, observable, and reversible:

- **Stage A exit:** the UI reconnects to live engine state, transport works, and real audio evidence
  exists on each supported platform path. The Phase 0 report remains attached as baseline.
- **Stage B exit:** the composing journey reaches an audible/rendered result and its edits are
  undoable, state-backed, and covered by deterministic regression tests.
- **Stage C exit:** a project survives save/reopen, render/export works, and recording/take support is
  either proven for each supported capture path or explicitly excluded by an owner-approved
  limitation. Loss or silent content substitution is a blocker.
- **Stage D exit:** the parity inventory has no unreviewed claims, Terra's human-use findings are
  independently resolved or explicitly classified under the existing policy, and platform,
  accessibility, native editor, performance, packaging, and release evidence is complete. Only then
  request the owner's decision on making Rune default while retaining Qt. Qt removal has a separate
  owner gate.

### Resource and disk limits

Respect the live runtime's resource and usage limits, current Terra-first dispatch, concurrency
controls, and work-account feature-downtime policy. Do not create more product worktrees/build trees
than the established lane procedure permits. Before any full build, check available disk and leave the
workspace reserve required by the current project rules; use targeted builds during implementation.
Record peak build footprint, startup-to-usable-frame, steady memory, interaction latency, and
high-rate data bandwidth on a representative project. Establish acceptable budgets from Phase 0 and
the baseline before claiming parity; no unmeasured number in this plan is a threshold. Avoid
unbounded event queues, repeated full-project snapshots, and frame-rate JSON polling.

## Interaction contract

Implementers should read `plans/RUNE-INTERACTION-CONTRACT.md` before touching Rune interaction
surfaces. That companion document owns detailed gesture and command semantics. This plan deliberately
keeps task-level user outcomes and evidence criteria stable while leaving component structure,
algorithms, internal types, and test framework choices to the implementation lane.

## Executable Terra review profiles

`plans/rune-review-scenarios.json` contains the per-unit human journeys and small interaction checks. Use the existing Terra runner with `--uses-file <absolute path to that file> --task-id Q050-505` (substitute the task ID), the actual built Rune launcher via `--binary`, and its pinned `--train` identity. These task reviews use separate run folders and do not sign off or replace the promoted-build button/session sweeps. The final adoption review must account for every profile case; a selected subset is only incremental evidence. Required hardware/audio/platform proof remains separate from screenshots. Do not run a demo-data profile and describe it as engine integration.
