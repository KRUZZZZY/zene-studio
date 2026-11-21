FIXED

# Cluster new-p16-100 — toFloat accepts non-finite project values

## Verdict

REAL. `LocaleHelper::toFloat` let NaN and ±infinity through to the caller unchanged, so a project
file with `nan`, `inf` or `1e999` in a float attribute could put a non-finite value into an
automatable model.

## Evidence (current file:line)

- `include/LocaleHelper.h:58-67` (before the fix) had the guard
  `!std::isinf(d) && fabs(d) > FLT_MAX`. The `!std::isinf(d)` term means infinity never enters the
  branch, and `std::fabs(NaN) > max` is false, so NaN also skipped the branch. NaN and ±inf were
  returned unchanged as `float`.
- Fix line: `include/LocaleHelper.h:61` is now
  `if (!std::isfinite(d) || std::fabs(d) > std::numeric_limits<float>::max())`, which sends NaN,
  ±inf and float overflow to the `ok = false; return 0.0f;` branch (lines 62-65).
- `src/core/Mixer.cpp:155-163` (`hasNonFiniteFloatAttribute`) used `toFloat` and tested the
  result with `std::isfinite`. Once `toFloat` returns 0.0f for non-finite input, that detector can
  never fire, so the Mixer guard would stop protecting its defaults. The fix changes this helper to
  read `LocaleHelper::toDouble` and test `std::isfinite` on the double. This is why
  `src/core/Mixer.cpp` must change: it used `toFloat`'s non-finite return as its detector.
- `tests/src/plugins/DispersionNonFiniteTest.cpp` (header comment at lines 46-47 and the test
  bodies): the old test wrote `nan` into an XML fixture and relied on the float parse to inject it.
  That path no longer produces NaN, so the test now injects NaN and inf directly into the model.
  The guard it checks (the Nyquist clamp) is unchanged.

## Contract

- A failed float conversion returns `0.0f` with `ok = false`, the same as the existing too-large
  branch. Callers that already handle overflow now also handle NaN and infinity through that path.
- `toDouble` is deliberately unchanged. A double can represent inf and NaN, and its `ok` reports
  only whether the locale parse succeeded. Changing it would alter the Mixer detector and every
  double caller.

## Regression test

`tests/src/core/LocaleHelperTest.cpp`, `LocaleHelperTest::nonFiniteTextIsRejected()` (line 99):
parses `nan`, `inf`, `-inf`, `1e999` and `-1e999` with `toFloat`, and checks that each returns
`0.0f` with `ok == false`. It also checks that finite values, including `FLT_MAX`, still parse
with `ok == true`. The test is registered in `tests/CMakeLists.txt` and listed in
`tests/fork-sources.txt`, `tests/all-sources.txt` and `tests/upstream-modifications.txt`.

## Commands and exit codes

Exit codes are the quiet runner's values as recorded in the packet. The on-disk logs in
`logs/` hold the ctest summaries but not the runner's exit line, so the codes are not re-derived
from the logs here; the summaries are cited where they agree.

| Step | Command (build tree `build/`) | EXIT | Log summary |
|---|---|---|---|
| build-1 / test-1 | baseline build, ctest on the relevant family | 0 | test-1: 5/5 passed |
| build-2b / test-2b | new test added, before fix | 8 | test-2b: LocaleHelperTest failed |
| build-3 / test-3 | fix applied | 0 | test-3: 6/6 passed |
| build-3b / test-4 | Mixer mutation (guard reverted to toFloat) | 1 | test-4: MixerStripTest failed |
| build-3c / test-4b | Mixer restored | 0 | test-4b: MixerStripTest passed |
| build-3e / test-3e | green re-run | 0 | test-3e: 1/1 passed |
| build-3f / test-3f | green re-run | 0 | test-3f: 1/1 passed |
| fix.patch | `git diff` of the fix | 0 | patch saved |
| git apply (forward) | apply fix.patch | 0 | |
| git apply (reverse) | reverse fix.patch | 0 | |
| build-4 / test-5 | fix reversed (RED) | 8 | test-5: LocaleHelperTest failed |
| build-5 / test-6 | fix restored (GREEN) | 0 | test-6: 3/3 passed |
| feature-check-1 | feature check | 0 | all PASS |

## Scope and collateral

- Deterministic: the test uses a direct string parse and direct model injection. It has no timing,
  sleeps or threads.
- No audio-thread, undo or control changes.
- No collateral: finite values, including `FLT_MAX`, still parse. The Mixer non-finite defaults are
  preserved (proven by MixerStripTest). The Dispersion guard is preserved.
- `src/core/DataFile.cpp:1240` (the load-time upgrade of `vol`) now writes `0` instead of `nan` for
  a malformed `vol`. This is the intended rejection.

## Other findings

- The pinned zynaddsubfx submodule copy still accepts non-finite values. It was not changed; it is
  outside this cluster.

## Self-check

1. RED/GREEN exits: RED is 8 (test-2b before the fix, test-5 with the fix reversed). GREEN is 0
   (test-3, test-6 restored, test-3e/test-3f). The Mixer mutation is RED at 1 (test-4), restored
   GREEN at 0 (test-4b).
2. Determinism: direct parse and direct model injection. No timing, sleeps or threads.
3. Cause and completeness (PARTIAL, one item open): the cause is the `toFloat` guard. Checked and
   consistent: the sibling that used `toFloat` as a detector (`src/core/Mixer.cpp:155-163`, now
   reads `toDouble`), the Dispersion test injection, and the load-time caller `DataFile.cpp:1240`.
   Still open: `toFloat` has 207 call sites across 87 files. The only change for them is the
   non-finite input contract, which now follows the existing `0.0f`/`ok=false` overflow path. A
   per-call-site audit of all 207 sites was NOT done in this run. `toDouble` is unchanged on
   purpose (see Contract).
4. No collateral and no limit rejection: finite values including `FLT_MAX` still parse (test
   `nonFiniteTextIsRejected`). The Mixer defaults are preserved (MixerStripTest passes). The
   `DataFile.cpp:1240` change is the intended rejection of a malformed `vol`: it writes `0`, not
   `nan`. No new limit was added, so no caller is rejected for exceeding one.
5. Not applicable: the verdict is REAL, not ALREADY-FIXED or FALSE-POSITIVE. The REAL evidence is
   `include/LocaleHelper.h:58-67` (old condition; fix at line 61), `src/core/Mixer.cpp:155-163`,
   and the Dispersion test change, as cited above.
