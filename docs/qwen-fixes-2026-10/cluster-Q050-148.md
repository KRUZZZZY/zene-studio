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

## Acceptance evidence (commands and exits exactly as run)

| # | Criterion | Command (exact) | Saw | Exit |
|---|-----------|-----------------|-----|------|
| 1 | docs compared with the implemented warp.stretch modes, blanket claim corrected | `cmake --build build --target SampleClipStretchTest SampleClipRubberBandTest -- -j4` | built clean | 0 |
| 1 | (proof) | `ctest -R "SampleClipStretchTest\|SampleClipRubberBandTest" --output-on-failure` (from `build/tests`) | `2/2 Test #436: SampleClipStretchTest ... Passed`, `2/2 Test #438: SampleClipRubberBandTest ... Passed`; the evidence lines match the numbers the corrected docs cite (resample 2x: 0.5000/0.3000 at 880/1320 Hz; preserve: 0.4992/0.2990 at 440/660 Hz; rubberband: 0.5000/0.3000 at 440/660 Hz; linear clip: 0 differing frames of 88200) | 0 |
| 2 | caveats per algorithm + runnable command example | `QT_QPA_PLATFORM=offscreen python3 tests/control-pitch-stretch-transcript.py build/zene` | `PASS: pitch-stretch control-surface transcript (every check held)` — 11 PASS / 0 FAIL: default `resample` read, typed refusal while the clip renders linearly (the documented prerequisite), 2x warp authored, mode switched to `preserve_pitch`, read back via `warp.list`, A16 `true_inverse` record checked, `control.undo` restores `resample` | 0 |
| 2 | (evidence lines) | `./SampleClipStretchTest -v1 ; ./SampleClipRubberBandTest -v1` (evidence lines extracted) | all cited numbers re-confirmed, including the rubberband fallback test slot and the `reset()`-allocates control | 0 |

Logs (numbered files): `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-148.r1/logs/`
(build-1, test-2, test-3, test-4, gate-5..gate-12).

## Gate exits

| Gate | Exit |
|---|---:|
| `bash tests/complexity-gate.sh --check` | 1 (red on train 5 by design; over-target list byte-identical to `../zene-cachyos` — nothing of this unit newly listed) |
| `bash tests/file-length-gate.sh --check` | 1 (red on train 5 by design; differs from `../zene-cachyos` only by two other lanes' files — nothing of this unit listed) |
| `bash tests/fork-sources-gate.sh` | 0 |
| `bash tests/all-sources-reproduce.sh` | 0 |
| `bash tests/no-upstream-regression-gate.sh` | 0 |
| `tests/scripted/check-namespace` | 0 |
| `bash tests/no-tautology-gate.sh` | 0 |
| `python3 tests/doc-figures-gate.py` | 0 (828 ids / 76 groups, unchanged — docs-only unit) |

No command, project-format, audio-thread or portability change was made.
