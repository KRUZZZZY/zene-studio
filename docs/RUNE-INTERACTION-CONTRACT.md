# Rune interaction contract

**Status:** interaction contract adopted for the owner-authorized Rune queue category on 2026-11-14. It does not certify implementation, approve a release or bypass feasibility/review gates. Phase 0 feasibility and owner gates in `side-projects/zene-rune-prototype/{PHASE0-BRIEF.md,SPEC-UI-REWRITE.md}` remain controlling. The live queue and Terra priority policy take precedence over any future scheduling.

**Owner-authorized queue metadata (2026-11-14):** P0 (top ordinary queue class, after the Terra gate), area L14/L13 (accessibility and interaction), type completion, difficulty 3. The canonical queue contains the Rune category and difficulty-3 tasks. Keep Phase 0, interaction-contract implementation, and later parity work as separately gated work; this document itself is a bounded design deliverable.

## 1. Scope and design thesis

Rune should expose the existing engine as a stable, keyboard- and pointer-operable musical workspace: transport, browser, arrangement, selection inspector, mixer, piano roll, and project/file operations. Preserve the prototype's fixed workbench and user-invoked region toggles; never move the same control automatically between modes. Every surface must show what is selected, what has focus, what is playing, what changed, and whether a long task is still running. Direct manipulation is valuable when it makes a meaningful object visible and lets a musician make a rapid, incremental, reversible edit; it does not make a drag the only way to act.

The reference prototype has useful behavioral evidence from 26 headless checks: one command path for buttons/keys/scripts; clip and note edits previewed and committed as one undo step; explicit snap state; pointer-anchored zoom; selection-driven inspector; field editing equivalent to drag; lane-kind drop refusal; and typing in fields does not fire single-key shortcuts. Its README also limits the evidence: simulated transport/meters, no engine, no project I/O, no real mouse/keyboard session, no screen-reader test, limited keyboard editing, no arrangement marquee, and no real-time frame-pacing data. Treat its interactions as hypotheses that Phase 0 and Terra sessions must revalidate, not product guarantees.

### HCI theory mapped to DAW interactions

These are design lenses that produce testable questions; they do not establish that Rune will be faster or easier to learn.

| Theory | DAW application and observable check |
|---|---|
| Mental models and Norman's gulfs of execution/evaluation | “Move this clip to bar 9” should have an evident action path, while the resulting track/time and engine confirmation should be interpretable. Ask users to predict before commit and explain the resulting state afterward; record wrong predictions and recovery. |
| Affordances, signifiers, and constraints | Treat a drag handle, cursor cue and ghost as cues that communicate a real supported action; constrain incompatible drops before commit and explain why. Do not call a visual icon an affordance or imply an unsupported capability. |
| Feedforward and feedback | Before release, show target lane, snap point, duration and likely overlap; after release, show committed location or rejection. Compare predicted and actual result. |
| Mapping and consistency | Fader direction, time fields, pitch navigation and labels map consistently to the outcome; equivalent controls across inspector, mixer, menu and shortcut use the same command semantics. Measure wrong-direction/wrong-surface edits, not “naturalness” by assertion. |
| Recognition over recall | Keep units, snap/grid, shortcut, target, selection count and undo label visible or discoverable in context. Test whether first-time users can identify a command and its location without recalling a hidden shortcut. |
| Error prevention and recovery | Type checks and lane constraints prevent invalid drops; previews prevent accidental commit; undo/save/recovery make recoverable errors diagnosable. Count invalid attempts, unintended changes, successful undo and unrecovered loss. |
| Spatial grouping (Gestalt proximity/common region) | Keep tempo/loop/transport controls together; keep a selected object's relevant fields and actions adjacent; visually separate browser, arrangement and inspector groups without using color alone. At compact sizes, verify groups do not become ambiguous or separated. |
| Fitts' target-acquisition principle | Tiny clip edges, note handles, mute/solo and record buttons are high-risk targets. Make frequent targets reachable and provide alternate exact controls for narrow handles. Test selection time and wrong-target rate across zoom/scaling/input devices; do not prescribe a universal pixel size or response time from Fitts' model. |
| Hick–Hyman choice principle | Large command sets can increase choice uncertainty in a choice-reaction task; group by meaningful DAW task, use clear labels, search and recents while retaining visible command homes. Do not blindly delete or hide commands based on item count: menu familiarity, hierarchy, task frequency and learned structure matter. Test findability/task success. |

Fitts' and Hick's original results came from specified experimental tasks, not a DAW, screen reader, arbitrary menu, or every input modality. Their equations offer hypotheses about target difficulty and choice uncertainty, not universal milliseconds, target pixels, or a rule that “fewer options is always better.”

### Terms that must stay distinct

- **Selection** is the set of project objects an edit targets. **Keyboard focus** is the one control receiving text/key events. **Playback position** is the engine's current transport time. A selection or focus change must not silently change playback.
- **Preview** is a temporary, non-committed interaction state. **Commit** is one engine edit/action. **Undo** reverses a committed, undoable edit; cancelling a preview sends no edit.
- **Pointer alternative** means completing the same outcome with clicks/taps and no pointer-down movement. **Keyboard equivalent** means the same outcome from keyboard-only operation. One is not a substitute for the other.
- **UI acknowledgment** (optimistic or confirmed) is not engine completion. **Saved** means the engine/filesystem reports durable success, not merely that a save command was queued.

## 2. Interaction invariants

These are observable contracts, suitable for automated checks plus a human scenario pass.

### 2.1 One object model, one command meaning

1. Each musical object has one stable identity and one command meaning across arrangement, inspector, mixer, browser drops, menus, palette and scripts. A menu, palette result, shortcut and direct manipulation action for the same operation call the same command/action path and produce the same state and undo boundary.
2. Every important operation has a visible menu or labeled-control home. A command palette may accelerate discovery, but cannot be the only route. Every palette result states its surface/menu location, current availability, shortcut if present, and a short effect description.
3. Context menus supplement, never replace, a visible route. Every context-menu action targets the object under the pointer and the same action has a discoverable command home.
4. Region visibility, density, theme, zoom, monitor arrangement, and focus changes never change project data. Controls remain in a stable region. Selection may change inspector contents but does not relocate controls.
5. Context-sensitive commands report why they are unavailable (for example, “MIDI clip cannot be dropped on an audio-only lane”) and retain the source item. No silent no-op or destructive coercion.

### 2.2 Selection, focus, and multiselection

1. Pointer selection and keyboard selection visibly distinguish the selected set from the focused widget. Focus has a persistent, high-contrast ring whenever keyboard navigation is active; it is not communicated by color alone.
2. Each selectable object exposes its name, kind, track/lane, start, duration and selection state to accessibility APIs. The selection inspector reports a single object's name/type or an aggregate count and applicable common values for a multi-selection; mixed values are identified as mixed, not replaced with a misleading single value.
3. In arrangement: click selects one clip; modifier-click adds/removes one; Shift-click extends a documented range; Ctrl/Cmd+A selects the documented scope (current editor or project, stated in the command label/help). Clicking empty canvas clears selection without editing. Selection never changes through a drag until the drag threshold is crossed.
4. A multi-edit either applies to every selected compatible object or is explicitly unavailable before commit. It preserves relative offsets for group moves; resize/trim semantics are announced before commit. Incompatible types are excluded with a visible count/reason, never silently dropped from the operation.
5. Escape cancels an active preview first, then closes the top transient surface (menu/palette/dialog), then clears selection only when no transient action is active. It never discards a committed edit.
6. After an edit, keyboard focus remains at the initiating control unless that control is removed; if removed, focus moves to a named, predictable neighbor and the change is announced.

### 2.3 Drag, drop, and gestures

1. Before pointer-down, show the affordance and target. During a drag, show a ghost/outline at the candidate location, snap/grid feedback, the target track, and rejection reason as soon as known. The source stays intact until a valid commit. Pointer-up outside a valid target, Escape, window deactivation, or lost capture cancels cleanly.
2. A drag commits once, on pointer-up, and is one undo step regardless of frame count. No partially committed intermediate states leak to the engine. If server reconciliation rejects it, restore the source, explain why, and preserve unrelated concurrent edits.
3. Every drag outcome also has a click-only path: select then invoke Move/Trim/Resize/Route via a labeled menu or dialog/stepper with explicit destination/value. Examples: browser item → “Add to selected track” plus track chooser; clip move → inspector start/track fields; reorder → Move Earlier/Later; fader/knob → click track plus numeric entry or increment/decrement buttons. This satisfies the *kind* of single-pointer alternative described by WCAG 2.2 SC 2.5.7 for web content; it is a product contract for desktop, not a claim that WCAG directly governs this native app.
4. Every drag also has a keyboard-only path. Focus the object, enter a named move/resize mode or use documented nudge/step commands, expose the active mode and increments, commit with Enter, cancel with Escape. The default shortcuts must not conflict with text entry or OS/window-manager bindings; Alt cannot be the only way to bypass snap on Linux.
5. Gestures have one meaning per surface and are documented in a discoverable help/shortcut view. Plain wheel scrolls the axis it is over; a modifier changes zoom; middle-drag pans only where middle-drag is available. Zoom is anchored to the pointer/playhead as stated. No undiscoverable chord or gesture is required for a basic task.
6. Snap state and resolution are visible in the transport/edit context and are named in assistive output. Snap bypass is previewed and one-shot only if that exact behavior is disclosed; global snap-off remains reachable without a modifier.

### 2.4 Keyboard, shortcut conflicts, screen readers

1. All project actions are reachable by keyboard, in a logical traversal order. The action registry documents shortcut, scope, focused-widget exception, and conflicting command status. Text editors, numeric fields, search, plug-in native editors and menu navigation receive normal text/navigation keys; single-letter project shortcuts are suppressed while a text-input control owns focus.
2. Provide conventional platform equivalents for Save/Open, Undo/Redo, Delete, Space transport, and menu access, with platform modifier translation. Show active shortcuts in menus/palette and provide a searchable shortcut reference. Rebinding is deferred until the registry can detect collisions and offer a conflict resolution; no duplicate binding silently shadows another.
3. Tab/Shift+Tab traverse controls; arrow keys operate the focused slider/stepper or a documented musical grid. Focus never disappears when a panel is hidden or closed; restore it to the invoking control or a named neighbor. No keyboard trap; Escape exits transient modes.
4. Accessible name, role, current value/value text, selected/checked/disabled state, and keyboard focus are exposed for custom controls. Use audio-specific text values (“−6.0 dB”, “pan 20% left”, “bar 12 beat 3”) rather than raw normalized values. Announce selection count, undo/redo labels, file/job state, and errors without announcing every meter sample or playhead tick.
5. Native plug-in editor focus is a separate focus scope. Opening it does not redirect global transport shortcuts into a text field or steal them unpredictably. Provide host-level transport/stop access and a documented way to return focus to the project. When the plug-in window closes, restore host focus.
6. Screen-reader users can operate transport, track/clip list, selection inspector, mixer controls and file dialogs without relying on spatial position, color, waveform shape or meter animation. The waveform/roll may have a visual overview, but also expose named objects, time/value fields and navigation/edit commands. Meters are rate-limited or user-queryable; their constant updates must not flood speech.

### 2.5 Precision controls and value editing

1. Every rotary control/fader exposes its exact current value and units nearby or through focus/screen-reader output. A knob is never the only value-entry surface. Provide a text/stepper route, plus keyboard increments; faders allow click-to-position and keyboard increments. Fine control is explicit (for example Shift-drag if supported) and not the only path.
2. Continuous controls show a live value while changing. Dragging uses a temporary preview where possible and commits one undo entry on release; discrete toggles commit on activation. Double-click reset exists only when labeled in help/menu and names its reset value. Reset is undoable.
3. Numeric entry validates range and unit, preserves precision supported by the engine, and rejects invalid input with the entered value retained and a clear reason. Do not silently clamp a user's typed value; if normalization is required, show the accepted value before commit.
4. Audio meters and playheads represent live telemetry, never edits. Meter peak hold/clip state is distinguishable from current level and can be queried as text; color is reinforced by labels/shape. Meter update cadence is capped to display refresh and should not trigger full view rebuilds. Waveform peak data is not confused with audio samples or an editable envelope.

### 2.6 Undo, redo, destructive actions, and persistence

1. Every reversible project mutation is undoable through the engine journal, whether invoked by pointer, keyboard, menu, palette, script or inspector. Equivalent edits share a labeled undo description; a continuous gesture is one step. A canceled gesture is zero steps. Undo/redo enabled state and next action name are visible, and the menu/button label predicts the next effect (“Undo Move Clip”, “Redo Set Volume”).
2. Undo/redo never applies while a text field is editing text unless the field's documented edit semantics say so; host project undo and native plug-in undo are explicitly distinguished. A failed/no-op command must not create a phantom history entry. Reopening/undo boundaries follow the engine's documented journal contract, not a UI snapshot stack.
3. Destructive operations state target and consequence before commit. Prefer undo/recovery for project edits; require a second confirmation only when the operation is irreversible or changes external files/recorded source material, with an explicit cancel. Never use a generic “Are you sure?” alone. Deleting a source take, replacing media, overwrite, destructive sample processing and clearing recovery data are high-risk; show exact object/path and offer a safe copy/backup route where available.
4. Save/open/import/export show the target path/name, format, overwrite state and progress. A successful save clears the dirty indicator only after durable completion; save failure leaves the document dirty and identifies the failed path/recovery option. Overwrite confirmation names the existing file. Cancel before a destructive write leaves source and destination unchanged.
5. Long operations are asynchronous and non-modal unless a decision is required. Show a stable task name, phase, progress if measurable (never fabricated), cancellation state, and completion/failure. Allow safe cancellation. Completion/error is announced once and remains inspectable in a task/status area. Do not freeze transport or editing merely because project indexing, plug-in scan, render or waveform transfer is pending.
6. A disconnected engine or stale response is visible. Commands have correlation IDs/ordering and idempotency or deduplication policy; stale replies cannot overwrite a newer user edit. During reconnect, show whether a displayed value is pending, confirmed or reverted. A failed save/record/render does not masquerade as success. Recovery must identify last confirmed saved state and unsaved edits.

### 2.7 Responsive layout, scaling, colors and visual state

1. The workbench remains a stable, single-window composition, with user-toggled regions. At compact sizes, a region may be user-hidden or scrollable, but controls do not auto-migrate to another mode. No action is clipped or made unreachable at 1366×768 at scale 1 or 2; also validate 1920×1080 and a narrow/short window. These are product test fixtures, not WCAG pixel requirements.
2. Respect OS text/display scaling and high-contrast settings; test 100%, 150%, 200% and fractional display scaling on supported platforms. Text wraps or controls grow; labels, values and focus rings must not overlap or clip. Density changes may reduce spacing, never remove command reachability or state controls.
3. Pair color with text, shape, line style or position for selected, armed, muted, soloed, clipping, invalid, pending and error states. Track color is decorative/organizational unless a textual name carries the semantics. Themes are data; the default theme passes contrast checks, and user themes warn on failure without falsely claiming conformance from a single palette calculation.
4. Persistent status is visible for engine/device state, record arm/monitor, snap, dirty/saved, pending operation, and current transport. Do not use transient hover alone for essential meaning.

## 3. Surface-specific behavior

| Surface | Contract |
|---|---|
| Transport | Play/Stop, seek, tempo, loop and snap have explicit labels/state. Space works when focus is not editing text and is available as a visible action. Stopped seek and displayed playhead agree with engine state. Start/stop never commits project edits. |
| Browser and file import | Search/filter empty states offer reset; Enter/double-click adds without drag. Preview/audition is clearly distinct from insertion; if preview is unavailable, say so. Drop ghost identifies target/time/type; refusal retains source. File path, format and missing-media state are readable. |
| Arrangement | Clip, track, lane and time are exposed as navigable objects. Move/trim/resize and multi-move preview and commit once. Empty-space seek/selection is not mistaken for clip editing. Overlap/crossfade behavior is shown before commit. |
| Inspector | Follows the current project selection without stealing keyboard focus on every selection event. Single and multi-selection states are explicit; field and direct-edit paths converge on the same undoable engine command. Mixed values are marked. |
| Mixer | Strip label, routing destination, fader value, pan, mute, solo, arm/monitor and peak are individually named and traversable. Meter motion never acts as the only feedback. Fader changes can be entered precisely, incremented with keys and undone. |
| Piano roll | Notes are individual named/time/pitch/velocity objects. Add/move/resize/delete/velocity and multi-note edits work via pointer, click-only menu/fields, and keyboard-only path. Selection and focus differ. Pitch/time values are spoken/textual; note audition has a non-audio/no-audition state when unavailable. |
| Plug-in windows | Native editor open/focus/close/z-order behavior is verified per platform; expose host fallback generic controls when no accessible native editor path exists. Avoid claiming host accessibility when the plug-in itself is not accessible. |
| Waveforms/meters | Waveform data is fetched as bounded bulk/peak data and labeled as overview; expose time/value navigation and audio state textually. Fast telemetry uses a separate lane from project edits, is sampled at display cadence, and cannot flood accessibility events or dirty the project. |

## 4. Acceptance method and measures

Every criterion below is pass/fail for its applicable platform and input method. A “not tested” result is not a pass.

### Automated contract checks

- Replay the same edit using button, key, menu, palette, inspector, direct pointer and command script; compare engine state, command ID and undo description.
- For each drag operation, verify ghost/target feedback, invalid-drop non-mutation, cancellation non-mutation, one commit on release, exactly one undo and one redo; verify click-only pointer path and keyboard path reach same final state.
- Verify text entry does not trigger global shortcuts; every action with a shortcut has a conflict-free binding and visible menu home; every focusable control has accessible role/name/value/state and stable focus restoration.
- Exercise multi-selection with same-type and mixed-type objects; verify membership, relative timing, inspector mixed values and all-or-none behavior.
- Simulate delayed/reordered/rejected engine acknowledgments and disconnect/reconnect; verify stale updates do not replace newer edits and pending/saved/error state remains honest.
- Simulate slow save/import/render/scan and cancellation/failure; verify UI remains responsive, progress semantics are truthful, no success state is shown early, and data survives or is recovered.
- Render UI at the sizes/scales in §2.7, test both themes, contrast tokens, high contrast, color-independent state, clipping and focus ring visibility. These are desktop product checks; WCAG's CSS-pixel thresholds do not automatically translate to physical desktop pixels.

### Human-use pass

Use the same frozen build for mouse, keyboard-only, screen-reader and scaling scenarios. A participant should complete each task without coaching for “discoverability”; log completion, wrong-target edits, recoveries, command/undo result, and any point where state is unclear. Record unknown/unattempted separately from pass/fail. The five-person UI-shell test and three questions in `research/ui/UI-DIRECTION-RECONCILED.md` are inherited hypotheses, not a replacement for the Phase 0 accessibility/platform gate.

**Proposed measurable bars:** zero silent data-loss or wrong-target outcomes; 100% completion of each core task by at least one tested method per modality; all tested keyboard paths have visible focus and no trap; all screen-reader actions expose meaningful labels/state; drag and click-only equivalent yield identical final state; no UI freeze over 100 ms for telemetry/edit feedback under the agreed Phase 0 load (the 16.7 ms p95 engine-ack goal remains the S2 target, with optimistic reconciliation as documented fallback); zero clipped/overlapping controls at required scale fixtures. These proposed product thresholds require owner acceptance and must not be represented as WCAG criteria.

## 5. Terra scenario mapping

The exact run logs are the evidence of record. This mapping makes likely Rune interactions observable in future Terra-style human/button sweeps; it does not create or reprioritize Terra issues. Per `AGENTS.md` §13, existing required Terra findings remain priority one and block ordinary queue work until both sweeps of the promoted base are complete.

| Human/button scenario | Invariants observed | Required result |
|---|---|---|
| First song: add instrument/audio, hear it, edit note/clip, tempo/loop, mix/effect, save/reopen/render | 2.1, 2.2, 2.3, 2.5, 2.6; transport, browser, piano roll, inspector, mixer | Each action's target/state is clear; audio is real where claimed; errors are recoverable; undo/redo and saved state match engine truth. |
| Train30 musician-session repro: notes, transport, loop, tempo, import/move/copy/delete audio, add effect, save/reopen; redo-after-undo issue | Undo/redo, selection, transport, file flow, command parity | Redo is available only when valid and restores exact undone action; record the earlier “redo disabled after undo” observation as a known repro requiring retest, not resolved by this spec. |
| Terra buttons sweep: every visible button/menu/toolbar action | 2.1, 2.4, 2.6; command home and feedback | Every control performs its labeled action, disabled state explains why, and destructive/editing outcomes are undoable or warned. Invalid/duplicate/hidden controls are logged with a reproducible path. |
| Terra human session: build a short beat/song; move/resize multi-clips and notes; mix with precision; correct a mistake; reopen | Multi-selection, drag and alternatives, keyboard, precision, history, persistence | Mouse, keyboard and screen reader each finish applicable subtasks; preview/cancel leaves no edits; commit/undo/redo state agrees; file survives reopen. |
| Device and plug-in: open native editor, edit, close, resume transport, undo host edit | Native-window focus and separate undo domains | Correct owner/z-order, keyboard routing and focus restoration; host vs plug-in undo is clear; no claim that inaccessible third-party UI is host-accessible. |
| Recording/recovery: arm, monitor, capture take, cancel/failure/relaunch | Destructive boundaries, async status, recovery | Arm/monitor are unmistakable; source take is not discarded silently; recording state and durability are reported; crash/recovery path identifies confirmed and unsaved data. |
| Screen-reader, scaling and color sweep: transport, track list, mixer, selection, error, meter, waveform | 2.4 and 2.7 | Spoken names and useful values/states; focus does not jump; meter updates do not flood speech; state remains understandable in monochrome/high contrast and at enlarged text. |

## 6. Open decisions and uncertainty

- Phase 0 must establish whether the engine can run without Qt, S2 latency/reconciliation, multi-platform Rune support, native plug-in windows, and Orca operation. None is answered by the demo prototype.
- NVDA and VoiceOver behavior needs platform tests; role/label presence alone is not proof. Windows/macOS screen-reader parity remains a product obligation, not implied by a Linux pass.
- Exact destructive-operation list, per-command undo/reversibility, crash durability, engine subscriptions, bulk waveform protocol and telemetry cadence must be verified against the real command registry and engine. The contract states outcome requirements, not protocol implementation.
- OS global shortcuts, Alt/window-manager behavior, per-platform modifier mapping, touch/pen behavior, display scaling and native plug-in window ownership can differ. Test each supported OS rather than assuming Rune's cross-platform build proves runtime equivalence.
- The prototype's 16.7 ms p95 engine-ack, layout dimensions, contrast values and human-session measures are proposed test bars. Only S2's current threshold is already present in the Phase 0 brief; the rest need owner acceptance before being release gates.

## Sources and prior art

- WAI/W3C, [WCAG 2.2 Understanding SC 2.5.7: Dragging Movements](https://www.w3.org/WAI/WCAG22/Understanding/dragging-movements): dragging requires a single-pointer non-dragging alternative on web content, separately from keyboard access. Used here as a strong product design criterion; not asserted to be a universal native-desktop law.
- WAI/W3C, [WCAG 2.2 Understanding SC 2.4.7: Focus Visible](https://www.w3.org/WAI/WCAG21/Understanding/focus-visible.html) and [SC 2.4.13: Focus Appearance](https://www.w3.org/WAI/WCAG22/Understanding/focus-appearance.html): focus must be visible; 2.4.13 is AAA for web and is used as a contrast/area reference, not transplanted as a desktop conformance claim.
- WAI/W3C, [ARIA Authoring Practices Guide: Slider Pattern](https://www.w3.org/WAI/ARIA/apg/patterns/slider/): names, values, arrow/Home/End keyboard pattern; APG warns touch screen-reader support varies, so test actual AT.
- Ben Shneiderman, [“Direct Manipulation: A Step Beyond Programming Languages” (1983)](https://www.cs.umd.edu/~ben/papers/Shneiderman1983Direct.pdf): visible objects, rapid incremental reversible actions. This is the conceptual basis, not measured proof that a specific DAW interaction is faster or more learnable.
- Apple, [Undo and redo HIG](https://developer.apple.com/design/human-interface-guidelines/undo-and-redo): describe the action to be undone and use standard platform menu/shortcut behavior.
- Apple, [Progress indicators HIG](https://developer.apple.com/design/human-interface-guidelines/progress-indicators): stable location, accurate determinate progress, explain stalled work, allow safe cancellation.
- Apple, [Designing for macOS](https://developer.apple.com/design/human-interface-guidelines/designing-for-macos): keyboard, high-precision input, high-resolution displays, resizable/movable windows and personalization.
- Microsoft, [Keyboard accessibility for Windows apps](https://learn.microsoft.com/en-us/windows/apps/design/accessibility/keyboard-accessibility), [Accessible text requirements](https://learn.microsoft.com/en-us/windows/apps/design/accessibility/accessible-text-requirements), and [accessibility checklist](https://learn.microsoft.com/en-us/windows/apps/design/accessibility/accessibility-checklist): focus traversal, readable/scaled text and accessibility validation.
- Ableton, [Live 12 keyboard shortcuts](https://www.ableton.com/en/manual/live-keyboard-shortcuts/) and [Arrangement View manual](https://www.ableton.com/en/manual/arrangement-view/): shipped DAW precedent for standard undo/redo and visible clip manipulation. Vendor behavior proves precedent, not superiority.
- Project prior art: `side-projects/zene-rune-prototype/README.md` (scope and 26 demo/headless checks), `DESIGN-RATIONALE.md` (Workbench vs Single lane tradeoff), `SPEC-UI-REWRITE.md` (proposed phases and unknowns), `PHASE0-BRIEF.md` (owner's Phase 0 acceptance), `research/ui/UI-DIRECTION-RECONCILED.md` (stable spatial memory, command homes, direct behavior testing), and `research/ui/FUTURE-UI-PLAN.md` (existing evidence grades and uncertainty).
