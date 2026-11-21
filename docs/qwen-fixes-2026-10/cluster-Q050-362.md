IMPLEMENTED

# Cluster Q050-362 — Correct sample-accuracy limitation docs

Docs unit, L06, member R-performance-73. `docs/KNOWN-LIMITATIONS.md` still carried the
blanket "**Automation is not sample-accurate.**" while its own feature-row-9 section described
the implemented curve-clip ramps. The overview bullet now states which clips are
sample-accurate (Linear/CubicHermite by default; Discrete keeps its step), that CubicHermite is
approximated per tick, the **32-knot** per-block capacity with surplus knots refused and
counted, and the typed **pattern-clip refusal**, then points at the detailed section. The dated
`docs/AUTOMATION-MODES.md` and `docs/STATUS.md` accounts are marked superseded, the stale
`docs/FEATURE-LIST-0.3.0.md` row is corrected, the manual states the behavior, and a docs gate
(`AutomationDocsAgree`) keeps the old blanket claim from returning. Docs and test only: no
command, schema, format, GUI or audio-path change.

Acceptance, with the exact commands run in this worktree and the exits seen (logs in
`Q050-362.r1/logs/`):

| # | Criterion | Evidence (test, what it proves) | Command | Exit |
|---|-----------|----------------------------------|---------|------|
| 1 | `KNOWN-LIMITATIONS.md` replaces the blanket statement that automation is not sample-accurate with a pointer to the implemented curve-clip behavior | `AutomationDocsAgree` requires the new overview statement, the Discrete exception, the CubicHermite approximation, the detailed feature-row-9 pointer and the `docs/SAMPLE-ACCURATE-AUTOMATION.md` design-record pointer; the same text is held by the engine's `SampleAccurateAutomationTest::theRampIsTheDefaultForACurveClip` (Linear resolves to the ramp, Discrete does not) and `SampleAccurateAutomationTest` render comparison | `ctest -R "SampleAccurateAutomationTest\|AutomationDocsAgree" --output-on-failure` from `build/tests` (`ctest-1.log`, `ctest-2.log`; reverted `ctest-reverted-1.log`) | 0 |
| 2 | The text states the current 32-knot capacity and that unsupported pattern clips are refused | `AutomationDocsAgree` requires `**32 knots**` and the typed `Refused` pattern sentence in `KNOWN-LIMITATIONS.md`, the feature list and the manual, and `MaxKnots` (32) + the typed refusal in `docs/SAMPLE-ACCURATE-AUTOMATION.md`; `SampleAccurateAutomationTest::theRampRefusesRatherThanGrows` holds the capacity behavior (32 accepted, surplus counted) and `automation.ramp_set`'s `isInPattern()` branch implements the typed refusal | `ctest -R "SampleAccurateAutomationTest\|AutomationDocsAgree" --output-on-failure` (`ctest-2.log`) | 0 |
| 3 | A documentation search finds no remaining contradictory blanket claim | `AutomationDocsAgree` sweeps eight old blanket claims across `KNOWN-LIMITATIONS.md`, `SAMPLE-ACCURATE-AUTOMATION.md`, `AUTOMATION-MODES.md`, `STATUS.md`, `FEATURE-LIST-0.3.0.md`, the manual, `CAPABILITIES-0.3.0.md` and the 0.3.0 release notes; an occurrence is accepted only inside a dated `Corrected`/`Superseded` unit. Dated reports, measurement snapshots and feature records are excluded by design (the brief allows dated reports to keep their own tree's statements) | `python3 tests/automation-docs-agree.py` (`automation-docs-agree-1.log`); `ctest -R AutomationDocsAgree` (`ctest-1.log`) | 0 |

Test efficacy: the five docs files were reverted to the base with
`git apply -R docs-change.patch` (the gate, its CMake registration and the feature record left
in place), and `ctest -R "AutomationDocsAgree$"` failed with **22 checks failed** — the old
blanket line back in `KNOWN-LIMITATIONS.md`/`STATUS.md`, `AUTOMATION-MODES.md`'s current
"sample-accurate playback is not", and row 9 back at `to build` — ctest exit **8**
(`ctest-reverted-1.log`). With the docs restored, the same test passes, exit **0**
(`ctest-2.log`). The built-in `AutomationDocsAgreeSelfTest` independently refuses six controls
(old blanket line back, 32-knot sentence dropped, pattern refusal dropped, row 9 back at
`to build`, manual paragraph gone, superseded marker gone), exit **0**
(`automation-docs-agree-selftest-1.log`).

Gate run: `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` — all eight
rows PASS, exit **0** (`feature-check-1.log`). `python3 tests/doc-figures-gate.py` — exit **0**
(`doc-figures-1.log`).

## Self-check

- **Each new test FAILS with your product change reverted, then PASSES restored.** The product
  change here is prose; the new test is `tests/automation-docs-agree.py`. With the five docs
  hunks reversed and the test/CMake hunks kept, `ctest -R "AutomationDocsAgree$"` exited **8**
  (`ctest-reverted-1.log`, 22 checks failed); with them restored it exited **0**
  (`ctest-2.log`). `AutomationDocsAgreeSelfTest` also proves the gate's own controls, exit **0**.
- **Every acceptance criterion is proven by a test I ran.** Criterion 1 and 3:
  `AutomationDocsAgree`/`AutomationDocsAgreeSelfTest`, exits 0 (logs above), with the reverse
  run at exit 8. Criterion 2: the same gate for the wording plus the engine's
  `SampleAccurateAutomationTest` (ctest exit 0) for the capacity rule; the pattern refusal is
  the implementation's `isInPattern()` branch in
  `src/core/ControlCommandsAutomationRamp.cpp:130-138`, and the by-progression-type default
  (`Discrete` resolves off, the curve types on) is `include/AutomationClip.h:301-308`.
- **One undo step per edit, and every new or changed command has an honest reversibility class.**
  No command, schema, registry or journal code was touched. The existing classes stand:
  `automation.ramp_set` is `true_inverse` (live automation-clip checkpoint) and
  `automation.ramp_get` is `not_mutating`
  (`src/core/ControlReversibilityTableAutomationRamp.cpp:92-106`); no edit in this unit is a
  command call, so no undo step was added.
- **Nothing added runs on the audio thread.** The diff touches Markdown/HTML, the test CMake
  comments, one new Python docs gate and the feature/report files. No audio-path source was
  changed; nothing allocates, frees, locks, waits, does I/O or emits a Qt signal on the render
  thread.
- **Old projects still load and render exactly as before; new data round-trips through save and
  load.** No serialization, loader or audio code was changed. The documented behavior is the
  existing one: only an explicit `sample_accurate` setting is saved, absence resolves by
  progression type, and `AutomationClip::loadSettings()` resets on absence; the docs now say so.
- **Qt 5.15 / MSVC / macOS < 10.15 portability.** The only code added is a Python 3 script using
  the standard library; there is no Qt/C++ API, no `windows.h` identifier, no
  `std::filesystem`, and no POSIX-only call. The docs changes are plain UTF-8 text/HTML.
- **GUI and agent, the same command.** This unit adds no user-facing action. It documents the
  existing `automation.ramp_set` / `automation.ramp_get` agent surface (socket + MCP) and states
  the known UI absence; no GUI control was added or changed, so no GUI test was appropriate.
- **New or changed command metadata.** None: no `ControlCommands*`,
  `ControlReversibilityTable*` or `ControlRegistry*` file changed, so the committed command
  snapshot and A16 histogram are unchanged; `doc-figures-gate.py` still exits 0.
