# LANE-STATE — `030/render-presets` in `zene-030/wrender`

> **Location (2026-09-15).** Moved from the repository root to `docs/reports/LANE-STATE-RENDER-PRESETS.md` by
> REPO-4 ("move lane reports and transcripts out of the repository root"), the second pass —
> the 2026-09-13 pass moved `DOCS-NAMING.md`, `CMDN-REPORT.md` and `CMDN-TRANSCRIPT.md` and
> left the rule in `docs/reports/README.md`. It is a lane report, not the render-preset documentation.
> The lane's own text is unchanged: it is a record of what the lane measured, and editing a
> record is how a record stops being evidence. A citation that names it by bare name still
> resolves to this file; the rule is recorded in `docs/reports/README.md` and
> `docs/CONVENTIONS.md`.

Feature rows **70** (render/export presets) and **71** (selection-to-audio), board task **#656**.
Base: `release/0.3.0` @ `f611c888b`; branch `030/render-presets`; **nothing is pushed and nothing is
merged** — the lane commits on its own branch only.

## Commits on this branch

| SHA | what |
|---|---|
| `68e4eaab8` | the lane's feature commit: engine + four command ids + A16 rows + the render range + proof + docs |

## What row 70 is, and what it is not

A **render/export preset** is a name plus the three `OutputSettings` fields a render can actually be
told: **sample rate, bit depth, stereo mode**. It is stored as ONE JSON document per preset in the user
preset tree (`<userPresets>/renderpresets/<name>.zrp`), **outside the project** — the chain-preset
store's rule, so a preset is usable in another project and `project.open` cannot lose it.

`grep -rniI "export preset\|RenderPreset\|batch export"` over the tree returned **0** hits before this
commit; the store did not exist in any form.

| id | group | class (A16) |
|---|---|---|
| `export.preset_add` | export | `true_inverse` — recorded action checkpoint (removes the document / writes back the revision it replaced) |
| `export.preset_list` | export | `not_mutating` |
| `export.preset_apply` | export | `true_inverse` — recorded action checkpoint (restores the selection it replaced, **by value**, so a preset removed in between still comes back) |
| `export.preset_remove` | export | `true_inverse` — recorded action checkpoint (writes the captured bytes back) |

**None of the four is typed-irreversible**, and that is a property of where the state lives rather than
an optimistic claim: a preset is a document outside the project and the applied selection is a
process-wide scalar, so both can be captured before the write. The one case where the recorded inverse
is unavailable — the bounded undo stack evicting a step (docs/UNDO-BOUNDS.md, where `control.undo`
already refuses typed) — has a **named manual fallback per verb**, written in
`ControlReversibilityTableExportPresets.cpp`'s header: re-issue the verb (`export.preset_remove "<name>"`
for an add; `export.preset_apply "<previous>"` for an apply; `export.preset_add` with the values the
transaction carries for a remove).

An **apply is not a render**: it moves the selection, and `render.render` passes it to the render CHILD
as its own command line (`controlExportPresetRenderArgs`). No second renderer; with no preset applied
the argument list is exactly the one `render.render` always built (`-s 44100`).

## What row 71 is

`render.render` gained optional **`start_ticks` / `end_ticks`** (both required together) and the render
CLI gained **`--range-start` / `--range-end`**, applied through the engine's OWN bounded render —
`Song::setRenderBetweenMarkers(true)` plus `Timeline::setLoopPoints()` (the path the GUI's "render
between loop markers" checkbox drives). The span is rendered EXACTLY: no tail bar, no loop repetition.
Half a range, an empty range and a negative range are typed `invalid_args` refusals raised **before** the
session is serialised or a destination opened. The CLI also gained `--bit-depth 16|24|32` so a preset's
depth can reach the render.

The range is an **argument** of `render.render`, not a second verb, so its A16 row is `render.render`'s
own row (`not_mutating`) — and that row now says so explicitly, including the honesty that the loop
points it rides are set in the **child** process. A render already performed is not undone by undoing
the apply that shaped its options: the fallback named in the row and in the docs is to render again.

## Four-part scope contract (CHARTER 3.1)

| part | state |
|---|---|
| 1. engine work in the tree | **in** — `include/ControlExportPresetSupport.h` + `src/core/ControlExportPresetSupport.cpp`, and the render path's range in `src/core/main.cpp` |
| 2. control-surface command group | **in** — the four ids above, registered in `ControlRegistryRegistrations.cpp`, rows in `ControlReversibilityTableExportPresets.cpp` |
| 3. a registered proof | **in** — ctest `ControlRenderPresets` (`tests/control-render-presets.py`), added to `tests/CMakeLists.txt` |
| 4. UI-absence lines | **in** — `docs/KNOWN-LIMITATIONS.md` and `docs/RELEASE-NOTES-v0.3.0-alpha.md` |

## Gates run on this tree (measured, unpiped exit codes)

| gate | exit | line |
|---|---|---|
| `tests/fork-sources-gate.sh` | **0** | `PASS: every tracked source in scope is registered (438 fork-NEW, 1060 inherited, 34 tooling).` |
| `tests/file-length-gate.sh --check` | **0** | `PASS (check mode: no regressions; baseline not written)` |
| `tests/duplication-gate.sh` | **0** | `PASS: duplicated lines 0.31% (budget 5%)` |
| `tests/no-upstream-regression-gate.sh` | **0** | `PASS: every change to upstream-inherited code since 01148947e is declared` |

Two gate failures were found and FIXED while landing, rather than re-anchored:
`new file over 500 lines: tests/control-render-presets.py (538)` (the test was compacted to **496**
lines) and two real compile errors in `ControlExportPresetSupport.cpp` (`isEmpty()` called on a
`const char*` — the check now tests the enum's own `Count`).

## What the build and the suite say (measured, 2026-09-15)

| command | exit | what it returned |
|---|---|---|
| `cmake --build build --target zene -j2` | **0** | `[100%] Built target zene` (incremental; the first two runs failed on two real compile errors, both fixed — see above) |
| `ctest -R ControlRenderPresets --output-on-failure` (from `build/tests`) | **0** | `100% tests passed, 0 tests failed out of 1` |
| `python3 tests/control-render-presets.py build/zene` | **0** | all **58** checks ok, `PASS: render/export presets and the ranged render (every check held)` |
| `zene render data/projects/shorties/DirtyLove.mmpz -o whole.wav` | **0** | 44100 Hz / 16 bit, 4066048 frames |
| `zene render <same> -o one-bar.wav --range-start 0 --range-end 768 --bit-depth 24` | **0** | 44100 Hz / **24 bit**, **239104** frames — the range and the depth flag both honoured, measured in the file |
| `zene render <same> -o bad.wav --range-start 768` (half a range) | **1** | refused before anything was written |

The proof's own numbers, from its output: one bar of ticks renders **302400** frames (the frame count
`Engine::updateFramesPerTick`'s expression implies) and two bars **604800**; the same span at the
preset's 96 kHz renders **658286** frames and a **96000 Hz / 24 bit** header; a rendered span is
**byte-identical** to the same span of a whole-project render; `control.undo` after an apply puts the
defaults back and the re-render is byte-identical to the pre-apply render. Each render child took
~0.5 s against its declared 180 s budget.

Two real defects were found and fixed by this lane's own proof rather than by hand: the illegal preset
name (a `/` in it — the store's name rule refused it, and the refusal is now a named check) and the
`DefaultTicksPerBar = 192` vs "a 4/4 bar is 768 ticks" confusion in the test's expectation (the
observed 4× discrepancy is recorded in the test's own comment).

## Not verified

* The full ctest suite and `tests/run-all-gates.sh` were NOT run on this tree (the owner's directive
  for this pass: land the feature, fix red CI in a dedicated pass at the end).
* Nothing is pushed and no CI run exists for `68e4eaab8`. `gh` is not authenticated on this box.
* The Chinese-wall case the row's fallback names — an EVICTED undo step — is documented, not exercised:
  the proof drives the recorded inverse through `control.undo` while it is still on the stack.
