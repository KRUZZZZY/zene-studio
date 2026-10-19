IMPLEMENTED

Cluster Q050-148 - Clarify pitch-preserving warp documentation (docs-only, S, L05).

The engine has had the pitch-preserving stretch (WSOLA, feature-list row 30) and the optional
Rubber Band R3 mode (owner decision 12) in place, but the warp docs still carried the blanket
claim that warping changes pitch and that pitch-preserving stretch is not built, and several
documents described `warp.stretch` as a two-mode command. Every stale site was corrected and
each claim was re-verified against the implementation and the registered tests.

## What was corrected

- `docs/KNOWN-LIMITATIONS.md` (warp entry): "Warping changes pitch ... Pitch-preserving stretch
  is not built" replaced by the three-mode picture with per-mode limits, the optional
  Rubber Band dependency, the WSOLA fallback and the honest refusals; the detailed
  pitch-stretch paragraph gained the third mode and its fallback cases.
- `docs/WARP.md` §0/§3/§6: "True pitch-preserving stretch ... is not built" and the §6
  "No pitch-preserving stretch" bullet corrected (dated notes; the lane's historical claims
  kept as history); stale line references fixed (`warpRatio()` -> SamplePlayHandle.cpp:551,
  `totalFrames()` -> :672).
- `docs/PITCH-STRETCH.md`: the enum row and the `warp.stretch` schema now list
  `resample` / `preserve_pitch` / `rubberband`; the refusal list gained the no-library case;
  new "Run it yourself (Q050-148)" block with the reviewer-runnable command.
- `docs/CAPABILITIES-0.3.0.md`: the "Warp and stretch" paragraph names the third mode with a
  dated note (Appendix A is a generated capture and was not hand-edited).
- `docs/FEATURE-LIST-0.3.0.md`: row 22's limit corrected; row 30 moved "**to build**" ->
  "**in the tree**" with proofs and limits.
- `docs/STATUS.md` and `docs/RELEASE-NOTES-v0.3.0-alpha.md` (dated correction in the file's
  own style): the "no pitch-preserving stretch" claims corrected.
- `tests/fork-sources.txt`: the two new docs files registered (cluster-doc convention).

## Rework round 1: the change-sensitive docs test

The round-1 review correctly found that no test would catch the documentation reverting.
Round 2 adds `tests/warp-docs-agree.py`, registered as ctest `WarpDocsAgree`
(`tests/CMakeLists.txt`, beside `DocFiguresGate`; offline — reads the docs and the transcript
script, no binary, no socket). It checks, over the seven docs this unit changed:

1. 40 required corrected statements (exact phrases added by the docs commit) are still there;
2. the per-algorithm caveats are still there (WSOLA formants / transients / ~345 Hz floor /
   linear SRC / one-rate-per-period; Rubber Band optional dependency, no-library refusal,
   WSOLA fallback);
3. the runnable reviewer example is still runnable: `tests/control-pitch-stretch-transcript.py`
   named in the docs, exists, and still drives `warp.stretch` / `preserve_pitch` / `warp.list`;
4. the old blanket claims are rejected as current statements ("Pitch-preserving stretch is
   not built", "no pitch-preserving stretch", "there is no pitch-preserving time-stretch",
   "True pitch-preserving stretch is a DSP project", row 30's "**to build**", the
   "or **`preserve_pitch`**" two-mode schema, STATUS's "Resampling stretch, so it changes
   pitch") — an occurrence is legal only where the file marks it historical (a
   `Corrected` / `no longer` / `was:` / `SUPERSEDED` note on the line or within the
   following six lines).

Registration: `tests/fork-sources.txt` (sorted entry + named pathspec in the recipe) and
`tests/CMakeLists.txt` (`add_test(NAME WarpDocsAgree ...)`). Measured before registering:
every function CCN <= 10, file under 500 code lines, so gates 4/7 measure it at their
existing thresholds (verified: both ratchet lists unchanged by this round).

## Acceptance evidence (commands and exits exactly as run)

| # | Criterion | Command (exact) | Saw | Exit |
|---|-----------|-----------------|-----|------|
| 1 | docs compared with the implemented warp.stretch modes, blanket claim corrected | `cmake --build build --target SampleClipStretchTest SampleClipRubberBandTest -- -j4` | built clean | 0 |
| 1 | (proof) | `ctest -R "SampleClipStretchTest\|SampleClipRubberBandTest" --output-on-failure` (from `build/tests`) | `2/2 Test #436: SampleClipStretchTest ... Passed`, `2/2 Test #438: SampleClipRubberBandTest ... Passed`; the evidence lines match the numbers the corrected docs cite (resample 2x: 0.5000/0.3000 at 880/1320 Hz; preserve: 0.4992/0.2990 at 440/660 Hz; rubberband: 0.5000/0.3000 at 440/660 Hz; linear clip: 0 differing frames of 88200) | 0 |
| 2 | caveats per algorithm + runnable command example | `QT_QPA_PLATFORM=offscreen python3 tests/control-pitch-stretch-transcript.py build/zene` | `PASS: pitch-stretch control-surface transcript (every check held)` — 11 PASS / 0 FAIL (round 2 rerun: 20 PASS / 0 FAIL counting the repeated report pass): default `resample` read, typed refusal while the clip renders linearly (the documented prerequisite), 2x warp authored, mode switched to `preserve_pitch`, read back via `warp.list`, A16 `true_inverse` record checked, `control.undo` restores `resample` | 0 |
| 1+2 | (rework) the docs regression test passes on the current docs | `ctest -R WarpDocsAgree --output-on-failure` (from `build/tests`) | `1/1 Test #584: WarpDocsAgree ... Passed (0.09 sec)` — 40 required statements, 7 blanket-claim sweeps and the runnable example all held | 0 |
| 1+2 | (rework) the test is change-sensitive: FAILS with the docs hunks reversed | `git checkout de5a85a96 -- docs/WARP.md docs/PITCH-STRETCH.md docs/KNOWN-LIMITATIONS.md docs/CAPABILITIES-0.3.0.md docs/FEATURE-LIST-0.3.0.md docs/STATUS.md docs/RELEASE-NOTES-v0.3.0-alpha.md` then the same ctest | `1/1 Test #584: WarpDocsAgree ...***Failed (0.09 sec)` — 39 findings, every corrected statement gone and the blanket claims unmarked | 8 |
| 1+2 | (rework) passes again once the docs are restored | `git checkout HEAD -- docs/...` (same seven) then `ctest -R "WarpDocsAgree\|SampleClipStretchTest\|SampleClipRubberBandTest"` | `3/3 Passed` | 0 |
| 2 | (rework) the forbidden-claim half fires on its own | the current docs plus one appended blanket-claim line, in a scratch copy under /tmp (script `--root` flag) | `FAIL docs/KNOWN-LIMITATIONS.md:2834: the old blanket claim is back as a current statement` | 1 |

Logs (numbered files): `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-148.r1/logs/`
(build-1, test-2, test-3, test-4, gate-5..gate-15, build-16, build-17, test-18,
test-19-reversed, test-20, test-21, doctest-reversed, doctest-restored, gate-22..gate-29).

## Gate exits (round 2)

| Gate | Exit |
|---|---:|
| `bash tests/complexity-gate.sh --check` | 1 (red on train 5 by design; over-target list byte-identical to the pre-rework run and to `../zene-cachyos` — the new test's functions are all CCN <= 10, nothing newly listed) |
| `bash tests/file-length-gate.sh --check` | 1 (red on train 5 by design; over-limit list unchanged by this unit's files — the new test is under 500 code lines) |
| `bash tests/fork-sources-gate.sh` | 0 (1795 fork-NEW entries, 0 stale) |
| `bash tests/all-sources-reproduce.sh` | 0 |
| `bash tests/no-upstream-regression-gate.sh` | 0 |
| `tests/scripted/check-namespace` | 0 |
| `bash tests/no-tautology-gate.sh` | 0 |
| `python3 tests/doc-figures-gate.py` | 0 (828 ids / 76 groups, unchanged — docs-only unit) |

No command, project-format, audio-thread or portability change was made.
