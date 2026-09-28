# `040/survey-fixes` — what the 2026-09-28 survey found, fixed and left open

Branch `040/survey-fixes`, worktree `zene-survey/`, based on `0.4.0/train-w1` @ `042abe489`.
Independent of the two unmerged lanes (`040/feat-597`, `040/feat-capture-limits`): it touches
none of their files. **Not merged, not pushed.**

The survey itself (read-only, then owner-approved "go ahead with all changes") is summarised in
the workspace's `plans/INDUSTRY-VERDICT-INTEGRATION.md` §G/§H. This file is the lane's record:
each change, its test, and what the test proved — including before-the-fix runs.

## 1. Engine correctness (G1–G4)

| # | Defect | Fix | Test |
| --- | --- | --- | --- |
| G1 | `ModulationLayerPublisher::snapshot()` copied `ModulationRuntime` — which holds `QPointer<AutomatableModel>` — inside a seqlock on the audio thread. A `QPointer` copy increments a shared weak-reference count (a write the seqlock cannot retry away, into a block the control thread may be freeing), and destroying the copy can free on the audio thread. | The audio thread now copies `ModulationAudioView` — plain bytes (raw pointer, slot token, floats; `static_assert` trivially copyable). Liveness is a per-slot atomic token that `QObject::destroyed` clears — the same moment a `QPointer` nulls — and every republish re-issues every slot, so an older view can never write a re-bound target. `src/core/ModulationLayerPublisher.cpp`. | `ModulationAudioViewTest` |
| G2 | `Song::processModulation` (audio thread) read `layer().shouldPersist()` — a `std::vector::empty()` on the control-only authored layer. | `ModulationLayerPublisher::hasLayer()`, an atomic published in `edit()`. The empty-layer early return (the byte-identity guarantee) is kept. | `ModulationAudioViewTest::hasLayerFollowsTheAuthoredLayer` |
| G3 | `Mixer::refreshGroups` reset every channel's VCA gain to 1.0, then re-applied group gains — a concurrent audio period could read unity for a grouped channel. | Each channel's gain is decided first and stored once; the later group still wins; channels past `mix_ch_t`'s range are in no group (the loop counter no longer wraps). | `VcaRefreshAtomicityTest` |
| G4 | `MidiClip::addNote` (default `quant_pos`) and `Clip::copyStateTo` dereferenced `gui::getGUI()` unguarded — headless crashes, the first reachable from `MidiImport`'s `addNote(n)`. | Null guards in `Song.cpp`'s idiom. With a GUI, behaviour is unchanged. | `HeadlessGuiGuardTest` |

**Not fixed, filed:** `AudioJack.cpp:141,149` and `MidiJack.cpp:61` show a `QMessageBox` on
`getGUI()->mainWindow()` when JACK drops the client — a headless crash on a JACK instance. There is
no JACK server on this box to exercise the path, so no change was made (an unverifiable change on
a device path is the thing this programme refuses).

## 2. The control surface

- **Result-schema conformance** (`include/ControlResultCheck.h`). Every command published a
  `resultSchema`; nothing checked a reply against it. `control::finishResult` — the dispatch's last
  step, in place of the bare `__transaction` removal — now validates the reply with the same schema
  subset as the arguments when `ZENE_CONTROL_CHECK_RESULTS=1`, and turns a violation into a typed
  `refused` naming it. Off in a shipped instance (a reply never changes because of it); ON for
  every instance the socket-driver harness starts (`tests/control_socket_harness.py`).
  `ControlResultSchemaTest` holds the checker itself and sweeps the registry for `id == group.verb`.
- **What turning it on found — a defect class, not a nit.** The first run with checks on failed 46
  of 250 tests. Recording every reply the 79 socket drivers receive (2,512 replies) and validating
  them offline against the published schemas found **303 violations in 53 of the 176 commands the
  suite drives (30%)**: 291 keys a handler returns but its schema omits while declaring
  `additionalProperties: false` (a strict MCP client would reject those replies), and 12 `null`s
  where the schema names a non-null type (nullable-by-design readings such as "no measurement yet",
  which the schema subset cannot yet express). The other 176 commands are not driven by any test,
  so they are unchecked. **Fixed in this lane:** the nine `vca.*` mutators, which reply with the
  whole group state but each declared a subset — they now declare `vcacontrol::groupStateSchema()`.
  **Grandfathered:** the other 46 commands, listed with their measured violations in
  `tests/result-schema-known-violations.txt`, which the harness passes as the check's value: listed
  commands are skipped, every other command (and every command added from now on) is held to its
  schema. The list only shrinks. It does not police its own staleness — a fixed command stays
  listed until someone deletes its line.
- **`window.*`** (`include/ControlWindowCommands.h`, `src/gui/ControlCommandsWindow.cpp`) — the
  first slice of the UI plan's item 0a ("GUI actions become registered commands"). The View menu's
  Ctrl+1..7 items and the window toolbar's seven buttons now dispatch `window.toggle {editor}`
  through the registry, which calls the same `MainWindow` toggle slots. `window.get_state` reads
  which editors are showing. Both `not_mutating` (interface state). The agent-surface grandfather
  list shrinks by those 14 actions (42 → 28).
- **Re-scoped from the survey's proposal, and why.** The survey proposed folding the A16
  reversibility rows into each command's declaration. On reading the 38 table files that is the
  wrong move: the rows are organised by *inverse mechanism*, carry prose limits the release notes
  quote, and `ReversibilityContractTest` already fails on drift in both directions. They stay.

## 3. Tests and CI

- `ThemeContrastTest` — the shipped themes' declared text/background pairs against WCAG AA 4.5:1,
  as a ratchet (two `classic` pairs are below it today and listed with their ratio; a new failure
  fails, and so does a listed pair that starts passing).
- `quality-gates.yml` — gate 2 (coverage) now also runs **nightly**, in check mode only.

## 4. Left for the owner (blocked or a decision)

- **Merging the two lanes into the live line** — the merge was refused by the session's permission
  classifier; nothing was retried.
- **Gate 7 counting code lines instead of physical lines** — implemented and measured (731 fork
  sources; 5 over 500 code lines; `--check` PASS), but the one-time `--reanchor` the unit change
  needs was refused by the classifier, and a unit change without its re-anchor loosens the ratchet
  for the grandfathered files. So the gate was restored; the patch is
  `scratch/survey-2026-09-28-gate7-code-lines.patch` in the workspace.
- **Coverage baseline re-scope** and **golden-audio re-anchor vs tolerance** — both rewrite a
  ratchet's record; left as the owner's (see §5 for the golden-audio evidence gathered).

## 5. Evidence

All runs from `zene-survey/build/tests`, `QT_QPA_PLATFORM=offscreen` and nothing else, unpiped exit
codes, on a clean full build (`cmake --build build -- -j2`, EXIT=0).

**Fail-before / pass-after.** The product files were reverted to `HEAD` with the new tests kept,
the tests rebuilt and run, then the fixes restored (verified by `grep`) and rebuilt:

| Test | Against `HEAD`'s product code | With the fix |
| --- | --- | --- |
| `VcaRefreshAtomicityTest` | **FAIL** — `refreshes=200000 reads=224885 wrong_readings=19` | PASS — `reads=19511414 wrong_readings=0` |
| `HeadlessGuiGuardTest` | **SegFault** — `signal 11 (SIGSEGV) … address 0x38` | PASS, 4/4 |
| `ModulationAudioViewTest` | the pre-fix snapshot type measures `is_trivially_copyable = 0` (compiled against `HEAD`'s `include/ModulationLayer.h`); the test's first case asserts it is 1 | PASS, 7/7 |

**The whole bar.** With result checks off (`ZENE_CONTROL_CHECK_RESULTS=0`), **249/250**, the one
failure `ControlCommandsSnapshot` before the snapshot was regenerated (two new ids). **Notably
`ControlGoldenAudio` PASSES on this clean full build** — relevant to `BUGS_FOUND.md` §8: the live
line's failure reproduces on a tree whose modules were rebuilt piecemeal, and does not reproduce
on a from-scratch build of the same source plus this lane's (audio-neutral for the fixture) changes.

**The snapshot** was regenerated from a live instance (trap 8), with the project's
`snapshot_commands.py --socket`: `352 id(s) across 57 group(s)`. **A16:** the probe
(`tools/dawproject-proof.sh` part 2) measures `rows=350 … not_mutating=135` on the untouched live
line and `rows=352 … not_mutating=137` here; the release notes' reference block moved by exactly
those two rows.

**`window.*` through the socket** (trap 7): a hidden pattern editor is shown (`visible: true`, and
`window.get_state` agrees), an unknown editor is refused `invalid_args`, and
`control.surface_report` on the live main window counts **14** actions declaring `window.toggle`.
The probe also caught a semantic the command inherits: on the offscreen platform no sub-window is
ever *active*, so upstream's "hide it if it is the focused one" branch never fires there.
