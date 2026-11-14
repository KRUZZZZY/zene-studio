# Rune interaction research — 2026-11-14

**Purpose:** source-grounded interaction rationale for the Rune front end and its proposed contract. This is not a UI implementation approval, queue edit, or claim of Phase 0 completion. Scope: behavior, input parity, accessibility, file/task safety, native plug-ins, audio feedback, and practical evaluation.

## 1. Decision summary

Keep the prototype's fixed Workbench: transport at a stable top location, arrangement as the main work surface, browser and inspector in stable side regions, and mixer/piano roll in a stable bottom region. Let the user show/hide regions; do not let selection or an automatic “smart” mode move controls. Use direct manipulation for clips, notes and continuous controls only when the same operation has visible preview, a single commit point, an undo result and keyboard/click-only alternatives. Treat engine truth, file durability, accessibility and focus as first-class interaction states.

This conclusion combines three evidence classes that should not be conflated:

1. **Normative/technical guidance:** W3C's web accessibility criteria and platform accessibility documentation establish testable expectations within their scope. They do not turn every CSS-pixel or browser rule into a universal desktop rule.
2. **Design canon and vendor precedent:** direct manipulation literature and DAW/platform manuals supply mechanisms and patterns. A shipping vendor example proves precedent, not measured superiority.
3. **Project hypotheses:** the demo Rune prototype and existing Zene research suggest stable layout, command parity and visible edit feedback. They lack independent user testing, real-engine behavior and complete screen-reader evidence. These claims must be resolved by Phase 0 and human testing.

The recommended work is intentionally staged: retain Phase 0's explicit feasibility gate; use this contract to guide one coherent first-session journey and Terra retests; defer broad editor parity and bespoke plug-in views until real engine, platform, latency, native-window and accessibility behavior is demonstrated.

## 2. Project evidence reviewed

### Prototype facts and limits

`side-projects/zene-rune-prototype/README.md` describes a Rust/Rune desktop experiment pinned to a Rune revision, with six surfaces and 26 headless model/view checks. It has demo tracks/clips, simulated playback position and meter values, no audio engine or plug-in host, no project file I/O, and synthetic waveform peaks. Its command enum is shared by buttons, keys, menus, palette and scripts. It tests clip/note edits, browser drop refusal, field edits, undo, shortcut suppression while typing, and layout at several sizes/scales.

The same README states that no person had used the build with real mouse/keyboard, Orca was not tested, there is no real-window frame-pacing measurement, and keyboard editing is incomplete. The demo does not prove audio, engine latency, durable save/reopen, assistive technology, or cross-platform parity. Its measured startup/frame CPU figures are UI-only on one Linux machine; they must not be presented as DAW launch or interaction performance.

### Existing design decisions that remain useful

`DESIGN-RATIONALE.md` compared a fixed Workbench against a single-lane arrangement/deck where the same side rail and detail deck changed with the selected object. It chose the fixed workbench because browser and inspector remain together, mixer keeps bus/master overview, and controls do not silently move. Its own evidence grades are appropriate: vendor manuals show shipped patterns, community comments indicate sentiment, and hypotheses require a test.

`SPEC-UI-REWRITE.md` proposes a two-process Rune UI over the existing control surface, subscriptions, a high-rate telemetry lane and bulk waveform data, with plug-in editors native to the engine process. It labels engine-without-Qt, round-trip latency, Windows/macOS Rune support and AccessKit parity as unknowns. `PHASE0-BRIEF.md` adds the owner's explicit boundaries: real rendered audio; one-frame p95 engine ack or clean optimistic reconciliation; three-platform proof; native plug-in window testing; Orca S5; engine without Qt and audio-editing engine questions; no separate preview binary; no desktop sound/window use. This research does not infer that these gates have passed.

### Program-level evidence

`research/ui/UI-DIRECTION-RECONCILED.md` already rejects hybridizing multiple DAW interaction languages; it values a single object model, controls with stable spatial memory, discoverable command homes and behavioral rather than screenshot acceptance. `FUTURE-UI-PLAN.md` and the evidence packets appropriately distinguish direct vendor documentation from user reports and unsourced claims. `plans/LAUNCH-PRIORITIES-2026-11-14.md` ranks a dependable first music-making journey after baseline/required Terra/Phase 0; notes current real-audio and undo evidence gaps; and warns that the current button sweep included failures, unsure and skipped cases, none of which can be silently treated as passes.

## 3. Source findings and implications

### 3.1 Direct manipulation: retain its loop, not pointer exclusivity

Shneiderman's 1983 account describes visible objects/actions and rapid, incremental, reversible operations through actions on the object. That supports a clip/note ghost, live value preview and immediate feedback. It does not prove that drag is always more natural, learnable, precise or accessible. For Rune, define direct manipulation as a **feedback/commit loop**: show target and predicted effect, let the user cancel before commit, commit atomically, then expose the result and undo description.

Project implementation inferences: one release event is one undoable edit; cancel/reject must leave no partial project edit; multi-selection preserves relative timing; pointer-anchored zoom must keep its anchor; and an optimistic display must be marked pending until engine confirmation. These are measurable contract rules proposed here, not findings from Shneiderman's paper.

### 3.2 Dragging alternatives and keyboard access are separate

WCAG 2.2 SC 2.5.7 says, for web content, functionality operated by dragging must have an equivalent single-pointer method without dragging unless an exception applies. WAI explicitly notes that keyboard equivalence does not satisfy this separate pointer criterion. Its examples include selecting an object then using menu actions or click buttons. WCAG keyboard criteria are likewise separate. Therefore every Rune drag has (a) a click-only path and (b) a keyboard-only path; having one does not excuse the other.

Native desktop conformance obligations depend on the platform/product regime; this research makes no blanket claim that WCAG's CSS pixels or web conformance calculation apply directly to every Rune widget. The product contract adopts the underlying interaction objective because pointer alternatives also help trackball, eye-gaze, switch and low-dexterity input. Validate touch/pen and assistive technologies in the actual toolkit.

### 3.3 Focus, keyboard and screen readers

W3C's Focus Visible criterion requires an available mode with visible keyboard focus; Focus Appearance (AAA) gives one web reference for focus size/contrast. WAI-ARIA APG's slider pattern specifies arrow/Home/End value operations and requires a name/value/range; it warns that touch screen-reader behavior for custom sliders varies and must be tested. Microsoft Windows guidance similarly treats focus visibility and predictable focus traversal as needed so users understand the effect of keystrokes. Apple macOS guidance names keyboard-only workflows and high-precision input as expected parts of desktop productivity.

Implications: a selected clip and focused control cannot share one ambiguous highlight; text entry suppresses global single-key shortcuts; focus traversal is logical; screen-reader output speaks semantic values and state changes but not every meter sample; plugin window focus has an explicit return route. Accessibility API metadata is necessary but insufficient: acceptance requires operating actual Orca, NVDA and VoiceOver on their supported hosts, as Phase 0 already recognizes.

### 3.4 Precision parameter controls

The WAI slider pattern supports keyboard operations and human-readable value text, while the APG warning cautions against assuming custom slider gestures are interoperable with touch assistive technology. Apple's macOS guidance explicitly recognizes high-precision input modes. The Rune prototype uses a fader with Shift-drag for fine control and arrow keys when focused, and exposes inspector fields; however, this is demo behavior and numeric values have not been evaluated by musicians.

Implications: no knob/fader is the sole route to a value. Keep a nearby exact value and editable numeric/stepper control; support keyboard increments, click-to-position where suitable, and a disclosed fine mode. Use units meaningful to musicians (dB, pan, BPM, bars/beats/ticks, semitones) and preserve engine precision. A double-click reset is not self-evident: label/help it and expose the reset value. Do not assume one constant step feels correct across ranges (linear slider vs logarithmic frequency/gain is an engineering/design question to validate per parameter).

### 3.5 Undo and action safety

Apple's platform guidance says undo labels should help people predict what will happen and uses native menu/keyboard conventions. Ableton documents standard shortcuts and concrete clip editing; these are shipping precedent. The project's own undo research (`EVIDENCE-patterns-and-firstrun.md`) distinguishes evidence for undo as a common expectation from the unsourced causal claim that undo necessarily causes more experimentation. So the contract treats undo as history transparency and recovery, not a proven substitute for discoverability or confirmation.

Implications: report action-specific undo/redo labels; do not add history entries for canceled or failed operations; continuous drags coalesce into one edit; host undo and plugin-specific undo domains are not conflated. Use undo/recovery for reversible project changes. Ask confirmation for irreversible/external effects (overwrite, destructive source processing, source take deletion), and name exact target/consequence. This minimizes both surprise and confirmation fatigue.

### 3.6 Asynchronous work and failure recovery

Apple's progress-indicator guidance differentiates determinate and indeterminate work, asks for accurate progress and stable placement, and recommends cancellation only when safe with consequences made clear. The Rune Phase 0 architecture already assumes engine acknowledgments/subscriptions and a separate telemetry lane; current control and demo code do not yet prove queueing, cancellation, stale-response handling or durable file state.

Implications: distinguish UI acceptance from engine completion and durable save; show pending/confirmed/rejected states; never make progress percentages up; keep a task location stable; provide cancellation only with defined safe semantics; and preserve unsaved work on errors. Exercise reordered/stale acknowledgments, reconnects, slow operations and failure injection before a release claim.

### 3.7 Color, scaling, waveform and meter semantics

WCAG's use-of-color, contrast, focus and text-resize principles are strong design checks, while Microsoft platform documentation separately emphasizes scaled text and actual high-contrast rendering. A high contrast ratio alone does not distinguish red/green to all users. The prototype's token contrast check is useful but cannot establish screen-reader or full high-contrast compatibility.

Implications: pair color with shape/label/position; use meters as telemetry and never as a state-only carrier; include textual peak/current values and keep updates bounded. A waveform is an overview derived from peak data, not a playable signal or proof of audio. Give it an accessible equivalent through named clips, time/value fields and navigation commands. Test both themes and OS scaling at layout edges.

### 3.8 DAW precedent: useful examples, not preference evidence

Ableton's official Live manual documents clip move/trim/fade behavior and conventional Undo/Redo shortcuts. The existing project corpus also cites Logic's selection inspector and REAPER's action registry as vendor-documented patterns. These show familiar DAW implementations and give concrete examples to test. None of the manuals measures learnability, error rate, speed, screen-reader success or user preference. Retain the one-pattern/one-boundary rule from `UI-DIRECTION-RECONCILED.md`; do not import another DAW's full object model or shortcut language as a bundle.

### 3.9 HCI theory mapped to the DAW, with limits

The theories below provide a practical diagnostic vocabulary. They are not outcome evidence for Rune. A testable mechanism is stronger than a slogan: identify which action/state cue is unclear, observe the musician's interpretation, and measure errors or recovery on a real task.

| Theory / source | Practical DAW mapping | What it does not prove |
|---|---|---|
| **Mental models and execution/evaluation gulfs** — Norman's [Cognitive Engineering chapter](https://research.cs.vt.edu/ns/cs5724papers/4.mental.mental.norman.cogengr.pdf) and NN/G's [Two UX Gulfs](https://www.nngroup.com/articles/two-ux-gulfs-evaluation-execution/) | Execution: can a musician map “place this sample at bar 9” to a visible browser/drop or menu/field route? Evaluation: can they tell whether it was added, where it landed, and whether the engine accepted it? Assess predicted state before action and ability to explain state after. | It is an analysis model, not a quantitative success formula. A user can see a status cue and still misinterpret it; measure task performance and explanation. |
| **Affordances, signifiers, constraints** — Norman's [Affordance, Conventions and Design](https://jnd.org/affordance-conventions-and-design-part-2/) and [Signifiers, not affordances](https://jnd.org/signifiers-not-affordances/) | A visible edge handle signals trim; a drag ghost signals destination; snap and lane-type rules constrain possible results. Design cue and actual supported action must agree; rejected drop names the compatibility rule. | Norman stresses these are distinct concepts: screen graphics are cues/conventions, not magical affordances. A cue's presence does not prove that people understand it; observe use. |
| **Feedforward/feedback and mapping** — the direct-manipulation cycle (Shneiderman, [1983 paper](https://www.cs.umd.edu/~ben/papers/Shneiderman1983Direct.pdf)) and familiar control conventions | Feedforward previews likely destination/value before committing. Feedback confirms accepted location, changed dB/pan, saved state or rejection afterward. Keep horizontal/vertical direction, fader movement and numeric units consistent with the resulting musical parameter. Compare expected vs actual outcomes. | Direct-manipulation theory recommends visible, incremental, reversible action; it does not show that every gesture is efficient or “natural” in this specific DAW. |
| **Recognition rather than recall** — Nielsen's [heuristic 6](https://www.nngroup.com/articles/ten-usability-heuristics/) and Budiu's [recognition/recall explanation](https://www.nngroup.com/articles/recognition-and-recall/) | Keep time units, grid/snap, command location, shortcut, selected-object count and undo description visible/on demand. Expose menus and palette search so the musician can recognize an action rather than remember a chord. Test how users find “split,” “route,” or “insert effect” without coaching. | A discoverable command list can still be too large or poorly labeled; recognition is not equivalent to fast discovery or learning. |
| **Prevention and recovery** — Nielsen's heuristic 5 (same [heuristics source](https://www.nngroup.com/articles/ten-usability-heuristics/)) and direct manipulation's reversibility | Refuse incompatible browser drops before commit, preview destructive edits, warn before overwriting/deleting external source, and make project edits undoable with meaningful history. Measure wrong-target and invalid-action rate, undo recovery success and data loss. | Error prevention does not mean blocking all risky creative choices; false positives and confirmation fatigue also cost users. Undo does not prove that destructive action is safe. |
| **Spatial grouping** — Gestalt proximity; NN/G's [proximity principle](https://www.nngroup.com/articles/gestalt-proximity/) and Nielsen's [closeness of actions and objects](https://www.nngroup.com/articles/closeness-of-actions-and-objects-in-gui/) | Place tempo/loop/snap with transport; mixer value with its fader; clip operations with selected clip/inspector; group related controls and separate unrelated ones through spacing and region boundaries. Validate at compact layouts and scaling, where spacing changes may obscure group membership. | Proximity is a perception principle, not proof that a fixed four-region layout is optimal; crowded groups can mislead and user tasks may cross panels. |
| **Fitts' target acquisition** — Fitts' original [1954 motor-control study](https://www2.psychology.uiowa.edu/faculty/mordkoff/InfoProc/pdfs/Fitts%201954.pdf); NN/G's [UX explanation](https://www.nngroup.com/articles/fitts-law/) | Small targets farther from the pointer generally impose higher acquisition difficulty in the model. For the DAW, inspect thin clip trim/fade handles, tiny mute/solo/arm controls, and distant toolbar actions. Use forgiving hit regions where they do not collide, put frequent actions near their objects, and provide inspector/numeric/keyboard routes. Measure hit time and wrong-target rate across zoom, scaling, mouse/trackpad/pen. | Fitts studied constrained repetitive movement tasks, not every DAW gesture. Constants depend on device/task; precision, visual search, target overlap, motor accessibility and screen-reader operation are outside the simple equation. Do not derive a universal millisecond or pixel threshold. |
| **Hick–Hyman choice reaction** — Hick's original [1952 choice-reaction study](https://www2.psychology.uiowa.edu/faculty/mordkoff/InfoProc/pdfs/Hick%201952.pdf); NN/G [menu-list discussion](https://www.nngroup.com/videos/hicks-law-long-menus/) | More/less uncertain alternatives can affect choice time in a controlled choice task. Organize the command registry into meaningful domains, use clear labels, search, recents, and progressive views while keeping all actions reachable. Test first-time findability of common commands and expert access without making advanced options vanish. | Do not equate number of menu rows to choice uncertainty in every context, or infer “fewer commands = faster DAW.” Familiarity, hierarchy, semantic grouping, frequency, expertise and recognition all change the task. No universal choice-count cutoff follows. |

For Fitts and Hick in particular, the correct Rune use is **comparative local evaluation**, not importing constants: compare candidate target/layout or command organization with the same participants and tasks, report errors as well as time, and avoid inventing a “good” pixel or millisecond cutoff. Neither principle overrides keyboard-only, screen-reader, touch, scaling or click-only equivalents.

## 4. Research-derived design rules

1. Stable spatial memory is a reasoned hypothesis supported by project feedback/research, not a controlled Rune usability result. Validate it in first-session tasks; do not create automatic context-driven relocation.
2. A command registry gives all interaction modes a consistent action meaning. It does not establish a productivity benefit from a palette or shortcuts. Preserve visible menu/control paths and test discovery.
3. Reversibility should be observable: preview, commit, labeled undo/redo, dirty/save/recovery state. Do not call an action “safe” merely because some undo command exists.
4. The UI must represent distinct states: focus, selection, playback, record arm/monitor, pending engine write, dirty/saved, task progress, and error. Distinct information deserves distinct visual/accessibility output.
5. Visual-only artifacts (waveforms, meters, colors, selection outlines, motion) require textual and non-gesture equivalents for the relevant task.
6. Every essential action is available by pointer without a drag and by keyboard; menus/palette are an accelerator/discovery layer, not a replacement for direct surface operation.
7. Real-time telemetry and model mutation have separate rates and semantics. Keep playhead/meters display-rate and non-dirtying; treat editing as acknowledged commands with history and recovery.
8. No screenshot, static contrast audit, headless interaction test or build matrix substitutes for a musician using the application, actual screen-reader testing, or verified audio output.

## 5. Evaluation plan

### Phase 0 questions already authorized in project brief

The interaction contract consumes the Phase 0 answers; it does not re-open them: S1 real engine/audio across six surfaces; S2 p95 drag-to-engine ack under 16.7 ms or clean optimistic reconciliation, meters/playhead at display rate; S3 builds/opens/headless tests Linux, Windows, macOS; S4 native plugin windows open/focus/close/z-order; S5 Orca transport/track list/mixer operation and a NVDA/VoiceOver plan. Also assess engine-without-Qt and the audio editing/data protocol. The brief's stated test environment limits remain binding.

### Contract evaluation after feasibility

- Test mouse, keyboard-only, screen-reader and enlarged/high-contrast modes against the same short composition journey.
- Observe selection, focus, multiselection, click-only alternatives, invalid drag/drop, cancellation, undo/redo, exact parameter edit, save/reopen, and engine disconnect/failure recovery.
- Add explicit plugin-window focus and telemetry/waveform information tasks.
- Record pass/fail/unsure/skipped with a reason. A harness mistake is corrected and retested; an unsure/skipped accessibility or interaction step remains unresolved until valid observation.
- Count task success, wrong targets, unintended commits, recovery events and state-confusion moments. No fabricated baseline or claimed improvement before a matched comparison.

Proposed interaction targets are stated in the companion [Rune interaction contract](../../plans/RUNE-INTERACTION-CONTRACT.md): no silent data-loss/wrong-target results; identical results across pointer, key and action routes; truthful pending/saved/error states; and zero focus traps/clipped controls in the agreed fixtures. These are product thresholds proposed for owner review, not WCAG minima.

## 6. Evidence ledger: known vs inferred vs unknown

| Claim | Grade | Basis / caveat |
|---|---|---|
| WCAG 2.5.7 has a single-pointer, no-drag web-content requirement distinct from keyboard access | Normative, web | W3C criterion and Understanding page. Product borrows the design objective; desktop legal scope is not asserted. |
| Focus visibility, semantic slider values and keyboard behavior are accessibility requirements/patterns | Normative/pattern guidance | W3C; apply via Rune/AccessKit/native APIs and validate real AT. APG is author guidance, not a conformance standard. |
| Undo labels, accurate progress placement and platform shortcuts help predictability | Platform guidance | Apple HIG; authoritative for Apple design conventions, not a universal empirical law. |
| Direct manipulation involves visible objects and rapid/reversible incremental operations | Foundational HCI theory | Shneiderman 1983; conceptual model, not a DAW-specific performance trial. |
| Fixed workbench improves Rune spatial memory | Project hypothesis | `DESIGN-RATIONALE.md` compares a demo concept; no participant comparison. |
| Rune prototype has 26 passing model/interaction checks | Project measured (README report) | Applies only to demo/headless build. No live engine or human-operated test. |
| Prototype has real meters/audio, file persistence or plug-in accessibility | False / explicitly absent | README says simulated meters/playback, no file I/O/plugin host/audio; Orca untested. |
| A palette or keyboard shortcut will improve workflow speed/discovery | Unknown for this product | Existing project research grades shortcuts and learnability claims; test instead of assume. |
| 16.7 ms engine-ack and display-rate telemetry are sufficient for all users/projects | Proposed/partially specified | 16.7 ms p95 is Phase 0 S2 target, with optimistic reconciliation fallback. Larger projects, device latency and real frame pacing remain unknown. |
| Undo causes more creative exploration or replaces destructive confirmation | Unknown/unsupported | Project evidence packet explicitly distinguishes undo expectation from causal experimentation claim. |
| High contrast means the state is understandable | False as a complete claim | Contrast does not convey semantic meaning or solve color perception/AT. Pair color with redundant cues and test. |

## 7. Sources

### Primary standards and platform sources

- W3C WAI, [WCAG 2.2 Understanding SC 2.5.7: Dragging Movements](https://www.w3.org/WAI/WCAG22/Understanding/dragging-movements). Direct source for the no-drag pointer alternative and distinction from keyboard accessibility.
- W3C WAI, [Understanding SC 2.4.7: Focus Visible](https://www.w3.org/WAI/WCAG21/Understanding/focus-visible.html), and [Understanding SC 2.4.13: Focus Appearance](https://www.w3.org/WAI/WCAG22/Understanding/focus-appearance.html). 2.4.13 is AAA in WCAG; its CSS-area/contrast test is not presented here as a desktop unit rule.
- W3C WAI, [ARIA APG Slider Pattern](https://www.w3.org/WAI/ARIA/apg/patterns/slider/). Role/name/value and keyboard recommendations plus touch AT warning.
- W3C WAI, [WCAG 2.2 Understanding collection](https://www.w3.org/WAI/WCAG22/understanding/) for keyboard access, use of color, contrast, resize text and non-text contrast.
- Don Norman, [Affordance, Conventions and Design (Part 2)](https://jnd.org/affordance-conventions-and-design-part-2/) and [Signifiers, not affordances](https://jnd.org/signifiers-not-affordances/), the author's own explanations of conceptual models, perceived affordances, signifiers, constraints, conventions and feedback.
- Don Norman, [Cognitive Engineering](https://research.cs.vt.edu/ns/cs5724papers/4.mental.mental.norman.cogengr.pdf), original chapter excerpt defining the action stages and gulfs; hosted by Virginia Tech.
- Kathryn Whitenton, NN/G, [The Two UX Gulfs: Evaluation and Execution](https://www.nngroup.com/articles/two-ux-gulfs-evaluation-execution/), which cites the original Hutchins, Hollan and Norman 1986 source and explains execution/evaluation and mental models.
- Paul M. Fitts, [The Information Capacity of the Human Motor System in Controlling the Amplitude of Movement (1954)](https://www2.psychology.uiowa.edu/faculty/mordkoff/InfoProc/pdfs/Fitts%201954.pdf), original experimental paper; experimental movement conditions are not interchangeable with every UI target.
- W. E. Hick, [On the Rate of Gain of Information (1952)](https://www2.psychology.uiowa.edu/faculty/mordkoff/InfoProc/pdfs/Hick%201952.pdf), original choice-reaction study. Hick explicitly cautions that the data do not justify strong claims about subjective probabilities or real-life choice beyond tentative application.
- Jakob Nielsen, NN/G, [10 Usability Heuristics](https://www.nngroup.com/articles/ten-usability-heuristics/); Raluca Budiu, NN/G, [Memory Recognition and Recall in User Interfaces](https://www.nngroup.com/articles/recognition-and-recall/). Heuristics/interpretive guidance, not controlled Rune outcome studies.
- Aurora Harley, NN/G, [Proximity Principle in Visual Design](https://www.nngroup.com/articles/gestalt-proximity/), and Jakob Nielsen, NN/G, [Closeness of Actions and Objects in GUI Design](https://www.nngroup.com/articles/closeness-of-actions-and-objects-in-gui/), for grouping and adjacency.
- Raluca Budiu, NN/G, [Fitts's Law and Its Applications in UX](https://www.nngroup.com/articles/fitts-law/); practical interpretation of target size and distance, not universal target dimensions.
- Apple, [Undo and redo](https://developer.apple.com/design/human-interface-guidelines/undo-and-redo), [Progress indicators](https://developer.apple.com/design/human-interface-guidelines/progress-indicators), [Designing for macOS](https://developer.apple.com/design/human-interface-guidelines/designing-for-macos).
- Microsoft Learn, [Keyboard accessibility](https://learn.microsoft.com/en-us/windows/apps/design/accessibility/keyboard-accessibility), [Accessible text requirements](https://learn.microsoft.com/en-us/windows/apps/design/accessibility/accessible-text-requirements), and [Accessibility checklist](https://learn.microsoft.com/en-us/windows/apps/design/accessibility/accessibility-checklist).
- Ben Shneiderman, [“Direct Manipulation: A Step Beyond Programming Languages” (1983)](https://www.cs.umd.edu/~ben/papers/Shneiderman1983Direct.pdf), University of Maryland author-hosted copy.

### DAW and project sources

- Ableton, [Live 12 Keyboard Shortcuts](https://www.ableton.com/en/manual/live-keyboard-shortcuts/) and [Arrangement View](https://www.ableton.com/en/manual/arrangement-view/), vendor manuals for shipped undo/redo and clip-editing examples.
- Project: `side-projects/zene-rune-prototype/README.md`, `DESIGN-RATIONALE.md`, `PHASE0-BRIEF.md`, `SPEC-UI-REWRITE.md`.
- Project: `research/ui/UI-DIRECTION-RECONCILED.md`, `research/ui/FUTURE-UI-PLAN.md`, `research/ui/EVIDENCE-patterns-and-firstrun.md`, `research/daw-ux-synthesis.md`.
- Project: `plans/LAUNCH-PRIORITIES-2026-11-14.md`, `plans/TERRA-PRIORITY-POLICY.md`, workspace `AGENTS.md` §§13–14.

Remote sources in the theory-to-DAW extension were opened and reviewed on 2026-11-14: 12 distinct pages/documents (two Don Norman JND.org essays and his Cognitive Engineering chapter excerpt; NN/G gulfs article; original Fitts and Hick papers; Shneiderman's original paper; NN/G heuristics, recognition/recall, Fitts, proximity, Nielsen's action/object proximity, and Hick menu-list guidance). One separate SAGE PDF endpoint for Hick timed out; the original Hick paper itself was successfully accessed at the University of Iowa-hosted PDF linked above. Standards/platform references in the existing research were also opened earlier; direct links support claims as scoped in the text. DAW manuals establish vendor precedent only.
