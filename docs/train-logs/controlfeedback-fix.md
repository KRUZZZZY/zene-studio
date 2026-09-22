# ControlFeedbackCommands on the merged tip: diagnosis, fix, proofs (2026-09-22)

STATE AT THE ELEVEN-LANE TIP (`9d71f47be`, run recorded in `ctest-11lane.md`):
build EXIT=0, ctest 223/224 (CTEST=8) — the single failure
`ControlFeedbackCommands`, exactly two checks: both "saved project equals the
pre-change bytes (sha256)" comparisons (pre-enable and post-disable stages).

## Diagnosis (measured, not assumed)

1. Lane-vs-train fixture ET diff (`zene-709/build/tests/709-feedback-fixture.mmpz`
   vs `zene-040/build/tests/…`, old canonical form): EXACTLY three deltas —
   root `creatorversion` (`0.3.0-alpha.52+4ef3065` → `0.3.0-alpha.63+9d71f47`),
   root `xmlns:ns0="urn:zene:core:1"` binding, and the whole
   `<ns0:provenance seq="9" v="1">` subtree (9 `<ns0:change>` children with
   wall-clock `at="2026-09-22T…Z"` stamps). Line counts 278 vs 289 = the
   provenance block; every other line identical.
2. Ancestry: `git merge-base --is-ancestor 809750c52 4ef3065fa` → S6's merge is
   POST-baseline, so the journal did not exist when the constant was captured.
3. Canonical bisection against the constant: drop creatorversion+provenance →
   sha `40265a89e25cd1f4…`; the old constant `e73f9b31…` embeds the OLD
   creatorversion by construction, so no exclusion set can ever reproduce it
   on a re-configured build.

## Fix

- `canonical_project_sha()` drops `creatorversion` (build identity) and every
  `…}provenance` element (S6 journal; wall-clock, covered by S6's own goldens)
  alongside the pre-existing `writer` drop; docstring records all three.
  File exactly 500 lines = its file-length anchor.
- `BASELINE_SAVE_SHA256` ROTATED → `40265a89e25cd1f46f70c3f57e36ef2a9c23b1b0e500b6fa9911fa9dd2367381`
  (train build at `9d71f47be`). `BASELINE_PDC` / `BASELINE_MIXER` untouched —
  the re-capture compares EQUAL to both embedded pre-change constants.
- Full act + honesty record: `docs/709-logs/709-baseline-rotate.md`.

## Proofs (unpiped)

    python3 -m py_compile tests/control-feedback-commands.py        # EXIT=0
    wc -l tests/control-feedback-commands.py                       # 500
    QT_QPA_PLATFORM=offscreen python3 tests/control-feedback-commands.py \
        build/zene --capture                                       # EXIT=0
    python3 (equality): pdc==BASELINE_PDC True, mixer==BASELINE_MIXER True
    QT_QPA_PLATFORM=offscreen python3 tests/control-feedback-commands.py \
        build/zene > /tmp/cfb-full.log 2>&1; echo $?               # EXIT=0
        -> "PASS: 25 checks, every one a measured number"
        -> "after disable: the saved project equals the pre-change bytes
            (sha256)  ok"
    cd build/tests && ctest -R ControlFeedbackCommands             # EXIT=0
        -> 1/1 Test #186 ControlFeedbackCommands Passed (2.13 s)

`unverified:` no pre-`4ef3065fa` build exists here to re-run the original
constant against (one-build-dir / reclaimed builds); that original cross-build
proof stands on the lane's recorded passing runs (`709-ctest-feedback*.md`).
