FALSE-POSITIVE

# Cluster new-p23-191: "OSS conversion handles NaN but not infinity"

The finding claims that `toPcmSample` replaces only NaN with zero, so "an infinite sample passes
through clip/multiply/cast with undefined behavior". The second half is false on this branch: the
sample is not passed to the cast unchanged. `AudioEngine::clip` saturates `+inf` to `1.0f` and
`-inf` to `-1.0f`, and `OUTPUT_SAMPLE_MULTIPLIER` is `32767.0f`, so the value that reaches the
`std::int16_t` cast is always inside the representable range. Only NaN survives `clip` unchanged
(both of its comparisons are false for NaN), which is exactly the value the existing `std::isnan`
guard removes. Nothing is undefined for infinities, and treating them as zero instead of full
scale would be a behaviour change for a value the documented clamp already defines.

The undefined behaviour the finding describes was real *before* `e7854be176` ("fix: clamp OSS PCM
output", cluster new-p10-492), when the OSS path was
`static_cast<int16_t>(buf[i] * OUTPUT_SAMPLE_MULTIPLIER)` with no clamp. That commit is an ancestor
of this branch (`git merge-base --is-ancestor e7854be176 HEAD` → exit 0), so the reported defect is
already gone from the code the finding names.

## File evidence

- [OssSampleConversion.h:27-31](../../src/core/audio/OssSampleConversion.h:27) — the whole
  converter: `if (std::isnan(sample)) { sample = 0.0f; }` (`:29`) then
  `static_cast<std::int16_t>(AudioEngine::clip(sample) * OUTPUT_SAMPLE_MULTIPLIER)` (`:30`). The
  sample is clipped, not passed through, so the finding's "passes through clip" premise is wrong.
- [AudioEngine.h:427-439](../../include/AudioEngine.h:427) — `clip()`: `if (s > 1.0f) return 1.0f;
  else if (s < -1.0f) return -1.0f; return s;`. IEEE comparison `+inf > 1.0f` is true and
  `-inf < -1.0f` is true, so both infinities are replaced by the clamp limits before the multiply.
  NaN compares false both ways and is returned unchanged — the one non-finite value `clip` cannot
  repair, and the one the guard handles.
- [AudioEngine.h:81](../../include/AudioEngine.h:81) — `OUTPUT_SAMPLE_MULTIPLIER = 32767.0f`. With
  the clamp in front, `±1.0f * 32767.0f = ±32767.0f`, and `-32768 <= -32767 <= 32767 <= 32767`, so
  the float→`int16_t` conversion is in range for every non-NaN input, finite or not.
- [AudioOss.cpp:238](../../src/core/audio/AudioOss.cpp:238) — the only OSS caller,
  `pcmBuf[i] = audio::oss::toPcmSample(buf[i]);`. There is no second OSS conversion path with a raw
  cast (`git show e7854be176 -- src/core/audio/AudioOss.cpp` shows that raw cast was the pre-fix
  line).
- Sibling float→16-bit/32-bit conversions share the contract, clip first, and are therefore also
  in range for infinities: `AudioDevice.cpp:202`, `AudioDevice.cpp:217`, `AudioSndio.cpp:151`,
  `AudioFileWave.cpp:76`, `AudioFileWave.cpp:282`, `ExportNoiseShaper.cpp:74`.

## Arithmetic proof (UBSan, not memory)

A standalone probe repeating the exact two expressions from the product code, built with
`-fsanitize=undefined,float-cast-overflow`:

`g++ -std=c++20 -O0 -fsanitize=undefined,float-cast-overflow nonfinite_oss_probe.cpp -o nonfinite_oss_probe`
→ `BUILD_EXIT=0`; `./nonfinite_oss_probe` → `RUN_EXIT=0`, output in `LOGS/probe-2-nonfinite.log`:

```
--- current code (isnan guard + clip) ---
toPcmSample(inf): clip=1 -> pcm=32767
toPcmSample(-inf): clip=-1 -> pcm=-32767
toPcmSample(nan): clip=nan -> pcm=0
toPcmSample(1.25): clip=1 -> pcm=32767
toPcmSample(-1.25): clip=-1 -> pcm=-32767
toPcmSample(1): clip=1 -> pcm=32767
toPcmSample(0.5): clip=0.5 -> pcm=16383
--- pre-e7854be17 code (no clamp) ---
nonfinite_oss_probe.cpp:24:42: runtime error: inf is outside the range of representable values of type 'short int'
preClamp(inf)=0
preClamp(-inf)=0
preClamp(1.25)=-24578
```

The first block is the code as it stands and UBSan reports nothing in it. The second block is the
same probe calling the pre-clamp expression, and there UBSan reports the out-of-range float→short
cast — the undefined behaviour the finding attributes to the current code. A second build with
`-fno-sanitize-recover=all` (`LOGS/probe-1-nonfinite.log`, `RUN_EXIT=1`) aborts on the pre-clamp
line only, which is the same result from the other direction.

## Regression test added

`tests/src/core/DeviceDspTestParts1.h` + slot declaration in `tests/src/core/DeviceDspTest.cpp`:

```
void DeviceDspTest::ossConversionInfiniteSamplesStayInRange() {
	const auto infinity = std::numeric_limits<float>::infinity();
	QCOMPARE(audio::oss::toPcmSample(infinity), std::int16_t{32767});
	QCOMPARE(audio::oss::toPcmSample(-infinity), std::int16_t{-32767});
	QCOMPARE(audio::oss::toPcmSample(std::numeric_limits<float>::quiet_NaN()), std::int16_t{0});
	QCOMPARE(audio::oss::toPcmSample(-std::numeric_limits<float>::quiet_NaN()), std::int16_t{0});
}
```

It pins the property the finding says is missing: an infinite sample reaches the 16-bit cast as an
in-range value. It is deterministic (fixed IEEE values, no clock, thread, sleep or random input) and
it is not vacuous: with the clamp removed it reports `toPcmSample(infinity)` = 0 (the wrap artifact
the UB produces) and fails.

## Test matrix

| Path | Case | Evidence |
|---|---|---|
| normal | finite in-range and over-range samples | `ossConversionClipsOverrangeSamples`, `ossConversionFullScaleStaysInRange` (pre-existing) plus the new test: PASS |
| failure / non-finite | `+inf`, `-inf`, `NaN`, `-NaN` | new `ossConversionInfiniteSamplesStayInRange`: PASS with the clamp, FAIL without it |
| exhaustion | none — `toPcmSample` has no allocation, buffer, lock or ownership | by inspection of `OssSampleConversion.h:27-31` |
| reconfiguration | none — no state, sample-rate or channel-count dependence | same |
| audio-thread constraints | called from `AudioOss::run` (`AudioOss.cpp:238`); unchanged: pure arithmetic on the stack, no allocation/free/lock/wait/log; the test itself touches no engine state | code unchanged in this cluster |
| platform build | `std::isnan`/`clip`/`std::int16_t` only, no Qt, no POSIX; header compiles in the `zene` and `DeviceDspTest` targets on this Linux host | `build-1`, `build-4` exit 0 |

## Commands and exit codes

Logs: `/home/kruzzzzy/.config/kads/zene/state/runs/new-p23-191.r1/logs/` (short name `LOGS/`).
`CCACHE_TEMPDIR=LOGS` is set for every build because the default ccache temp dir is not writable
from this session.

| Step | Exact command | Exit / result |
|---|---|---|
| UBSan probe, build + run | `g++ -std=c++20 -O0 -fsanitize=undefined,float-cast-overflow nonfinite_oss_probe.cpp -o nonfinite_oss_probe 2>&1; echo BUILD_EXIT=$?` then `./nonfinite_oss_probe > LOGS/probe-2-nonfinite.log 2>&1; echo RUN_EXIT=$?` | 0 / 0; output above |
| UBSan probe, abort-on-first | same source with `-fno-sanitize-recover=all` → `RUN_EXIT=1`, aborts on the pre-clamp line | `LOGS/probe-1-nonfinite.log` |
| GREEN, family build | `CCACHE_TEMPDIR=LOGS cmake --build build --target DeviceDspTest -j4 > LOGS/build-1-green.log 2>&1; echo EXIT=$?` | 0 |
| GREEN, family test | `cd build/tests && ctest -R DeviceDspTest --output-on-failure > LOGS/test-1-green.log 2>&1; echo EXIT=$?` | 0 — `1/1 Test #392: DeviceDspTest ... Passed` |
| GREEN, new test alone | `cd build/tests && ./DeviceDspTest ossConversionInfiniteSamplesStayInRange > LOGS/test-2-green-single.log 2>&1; echo EXIT=$?` | 0 — `PASS : DeviceDspTest::ossConversionInfiniteSamplesStayInRange()`, `Totals: 3 passed` |
| RED, clamp removed | header line set back to the pre-`e7854be176` form `static_cast<std::int16_t>(sample * OUTPUT_SAMPLE_MULTIPLIER)`; `CCACHE_TEMPDIR=LOGS cmake --build build --target DeviceDspTest -j4 > LOGS/build-2-red-clamp-removed.log 2>&1; echo BUILD_EXIT=$?` and `./DeviceDspTest ossConversionInfiniteSamplesStayInRange > LOGS/test-3-red-clamp-removed.log 2>&1; echo TEST_EXIT=$?` | build 0; test **1** — `FAIL! : DeviceDspTest::ossConversionInfiniteSamplesStayInRange() ... Actual (audio::oss::toPcmSample(infinity)): 0  Expected: 32767  Loc: [../../tests/src/core/DeviceDspTestParts1.h(79)]` |
| Restore product header | `cp LOGS/OssSampleConversion.h.orig src/core/audio/OssSampleConversion.h && sha256sum -c LOGS/orig.sha256` | 0 — `src/core/audio/OssSampleConversion.h: OK` |
| GREEN after restore | `CCACHE_TEMPDIR=LOGS cmake --build build --target DeviceDspTest -j4 > LOGS/build-3-restored.log 2>&1; echo BUILD_EXIT=$?` and `./DeviceDspTest ossConversionInfiniteSamplesStayInRange > LOGS/test-4-green-restored.log 2>&1; echo TEST_EXIT=$?` | 0 / 0 — `PASS : DeviceDspTest::ossConversionInfiniteSamplesStayInRange()` |
| Product target + family after restore | `CCACHE_TEMPDIR=LOGS cmake --build build --target zene -j4 > LOGS/build-4-zene.log 2>&1; echo BUILD_EXIT=$?` and `cd build/tests && ctest -R DeviceDspTest --output-on-failure > LOGS/test-5-green-ctest.log 2>&1; echo CTEST_EXIT=$?` | 0 / 0 |
| No product change | `git diff -- src/ include/ > LOGS/fix.patch; echo DIFF_EXIT=$?; wc -c LOGS/fix.patch` | 0; `0 LOGS/fix.patch` — there is no product hunk to reverse, so step 4b's `git apply -R` has nothing to apply; the RED/GREEN pair above is measured by removing and restoring the pre-existing clamp commit's expression instead |

## Gates

| Gate | Exact command | Exit |
|---|---|---|
| The stock gate script | `FEATURE_CHECK_LOG_DIR=LOGS/feature-check python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > LOGS/gate-1-feature-check.log 2>&1; echo GATE_EXIT=$?` | **1** — not a code result: no baseline cache exists for this train (`gate-baselines/9913d36d53…json` is absent) and the script's own cache fill is refused: `bunny-guard: 'git -C .../gate-baselines/tree checkout -q --detach 9913d36d5...' is not allowed in a Bunny lane` (`returncode 97`); the cache directory is also read-only for this session |
| Complexity, worktree | `PATH=$TMPDIR/venv/bin:$PATH bash tests/complexity-gate.sh --check > LOGS/gate-2-complexity.log 2>&1; echo COMPLEXITY_EXIT=$?` | 0 — `PASS (check mode: no regressions; baseline not written)`; the new test function is CCN 1 and is not listed among the 60 over-target functions |
| File length, worktree | `bash tests/file-length-gate.sh --check > LOGS/gate-3-file-length.log 2>&1; echo FILELEN_EXIT=$?` | 0 — `PASS (check mode: no regressions…)`; the 4 files over 500 code lines are all pre-existing and none is mine |
| Baseline for the two ratchets | base commit extracted read-only with `git archive 9913d36d53 \| tar -x -C $TMPDIR/base9913` (a `git checkout` is refused in this lane), then both gates run there: `PATH=$TMPDIR/venv/bin:$PATH bash tests/complexity-gate.sh --check > LOGS/base-9913-complexity.log 2>&1` and `bash tests/file-length-gate.sh --check > LOGS/base-9913-file-length.log 2>&1` | 0 / 0 — both clean, so the base REGRESSION sets are empty |
| Full gate table | a copy of `feature_check.py` in `$TMPDIR` whose `base_listings()` reads those two base logs instead of the (uncreatable) cache; the six gate commands, the `key()` transform and the `/changed files/` filter are unchanged. `PATH=$TMPDIR/venv/bin:$PATH FEATURE_CHECK_LOG_DIR=LOGS/feature-check BASELINE_LOGS=LOGS BASE_CACHE=$TMPDIR/gate-baselines python3 $TMPDIR/fc-new-p23-191.py <worktree> > LOGS/gate-4-feature-check.log 2>&1; echo GATE_EXIT=$?` | **0** — `RESULT: PASS`; fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures all `exit 0`; complexity and file-length `no new item in your files` |
| Completion check | `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p23-191 > LOGS/submit-1.log 2>&1; echo SUBMIT_EXIT=$?` | 1 — every row PASS except `FAIL gates (feature_check.py)`, which is the stock-script refusal documented two rows above; all five ASK items are answered in the Self-check section |

The substituted baseline is the *strictest* possible one (empty base set: every REGRESSION line in a
changed file counts as new), and my run emits none, so the two ratchet rows pass under the real
comparison as well as under this one. `lizard` is not installed on this host (`complexity-gate.sh`
exits 2 with "lizard is not installed"); it was installed into a scratch venv in `$TMPDIR`
(`venv-lizard.log`) and only that venv's `bin` was put on `PATH` for the gate runs. No system
package was changed and nothing outside `$TMPDIR` was installed.

## Files committed

- `tests/src/core/DeviceDspTest.cpp` — slot declaration and doc comment for the new test (already a
  fork source, `tests/fork-sources.txt:2814`).
- `tests/src/core/DeviceDspTestParts1.h` — the test body (already a fork source,
  `tests/fork-sources.txt:784`).
- `docs/qwen-fixes-2026-10/cluster-new-p23-191.md` — this report.

No product file changed; `src/core/audio/OssSampleConversion.h` was edited twice for the RED run and
restored byte for byte (`sha256sum -c LOGS/orig.sha256` → OK, `f10054cc1119…`). Registration needed
no edit: the fork-sources gate passes with both files already listed.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.** There is no
  product change in this cluster — the verdict is FALSE-POSITIVE. The equivalent measurement, which
  the finding's mechanism requires, is the clamp: with `OssSampleConversion.h:30` set back to
  `static_cast<std::int16_t>(sample * OUTPUT_SAMPLE_MULTIPLIER)` the new test FAILS, exit 1, actual
  `0` against expected `32767` (`LOGS/test-3-red-clamp-removed.log`); with the header restored it
  PASSES, exit 0 (`LOGS/test-4-green-restored.log`). UBSan independently shows the reverted form is
  the one with the undefined float→short conversion (`LOGS/probe-2-nonfinite.log`).
- **The test is deterministic.** No wall-clock, sleep, thread, engine or random input: fixed values
  `std::numeric_limits<float>::infinity()`, its negation, and `quiet_NaN()`; the seam is the pure
  function `audio::oss::toPcmSample`.
- **The fix is at the cause and complete: every caller and sibling checked.** There is no fix to
  make: the only non-finite value that survives `AudioEngine::clip` is NaN and it is already mapped
  to zero at the cause (`OssSampleConversion.h:29`); `OssSampleConversion.h` has exactly one caller,
  `AudioOss.cpp:238`, and every sibling converter (`AudioDevice.cpp:202`, `:217`,
  `AudioSndio.cpp:151`, `AudioFileWave.cpp:76`, `:282`, `ExportNoiseShaper.cpp:74`) clips before
  scaling, so infinities are in range there too. Changing them would be behaviour change outside
  the finding.
- **No collateral damage.** No product, data or save-format code changed, so old projects load and
  render exactly as before; no new limit or check was added that any caller could trip over. The
  only committed change is a test function plus its declaration.
- **Verdict evidence (ALREADY-FIXED/FALSE-POSITIVE item).** `AudioEngine.h:427-439` (`clip`
  saturates both infinities) + `AudioEngine.h:81` (`32767.0f`) + `OssSampleConversion.h:27-31`, with
  the UBSan probe as the arithmetic proof, and `git merge-base --is-ancestor e7854be176 HEAD` → 0
  for the history. Reviewed against the intended contract: `toPcmSample` clamps to ±full scale and
  silences NaN, which is what the pre-existing tests `ossConversionClipsOverrangeSamples` and
  `ossConversionFullScaleStaysInRange` pin.
- **Realtime/undo/commands.** Nothing added runs on the audio thread: the added lines exist only in
  a Qt test. No command, no undo step, no `ControlCommandsSnapshot` change.
- **The one FAIL in `submit_check.py` is the gate script's environment, not this patch.** The row is
  `FAIL gates (feature_check.py)`. The stock script aborts before printing a table because it needs
  a baseline cache entry for base `9913d36d53` that does not exist, and it cannot create one:
  `bunny-guard: 'git -C /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/gate-baselines/tree checkout
  -q --detach 9913d36d5397025d1b7092097f50f6d50c652dc4' is not allowed in a Bunny lane ...`
  (`returncode 97`, `LOGS/gate-1-feature-check.log`), and a direct write there fails with
  `OSError: [Errno 30] Read-only file system` (attempted once, refused, nothing written). Every gate
  was therefore run individually in the worktree and the baseline was measured in an extracted copy
  of the same base commit; the resulting table is `RESULT: PASS`, exit 0
  (`LOGS/gate-5-feature-check-final.log`), with the comparison unchanged and stricter (empty base
  set). No gate limit, baseline file or test was edited.

## Other findings

- Sibling converters have no NaN guard at all: `AudioDevice.cpp:202`, `AudioDevice.cpp:217`,
  `AudioSndio.cpp:151`, `AudioFileWave.cpp:282` and `ExportNoiseShaper.cpp:74` cast
  `clip(x) * OUTPUT_SAMPLE_MULTIPLIER` straight to an integer, and `clip` returns NaN unchanged, so
  a NaN sample is the undefined float→int conversion there. That is a real gap, but it is a
  different file and a different finding; it is not fixed here (scope rule).
- Environment: the shared `qwen/gate-baselines` cache is not writable from a lane session and the
  bunny guard refuses the script's own `git checkout` into its cached tree, so `feature_check.py`
  cannot fill a baseline for a train it has not seen. `lizard` is not installed on this host, so
  `complexity-gate.sh` exits 2 until it is installed (done here in a scratch venv).
