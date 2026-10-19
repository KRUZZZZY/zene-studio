# Cluster new-p11-79-port — "Automated delay LFO rate is interpreted as seconds per cycle" (Delay)

**Verdict: FIXED.** The bug was real on this line (it was never ported here), the
DSP fix of `bugs/qwen-new-p11-79` (commit `2098f5b2f`) is re-applied by hand against
the current code, and the regression test is RED on the pre-fix code and GREEN after
the fix.

This is a PORT job, not a review: the fix accepted on the older line was rejected by
the merge train only because its test looked the Delay models up by their old display
names. On this line the same names are `LFO period` (renamed by `f55be4832`, cluster
new-p11-83) and `Delay time (s)` (renamed by `c91e6eb14`, cluster new-p11-82). The
regression therefore lives here in the tree's own Delay LFO test, using the current
names, and the product change is the DSP fix itself.

## The bug

`plugins/Delay/DelayEffect.cpp`, `DelayEffect::processImpl` (lines 84-144 pre-fix).

The Delay plugin's LFO model (`m_lfoTimeModel`, display name `LFO period`, hint ` s`,
range `[0.01, 5]` seconds) holds the LFO **period in seconds**. `Lfo::setFrequency()`
wants **Hz** (`plugins/Delay/Lfo.h:50`), so the value has to be reciprocated. The two
paths into `setFrequency()` disagreed on that:

| path | pre-fix code | units |
|---|---|---|
| static (no automation on the knob) | `float lfoTime = 1.0 / m_delayControls.m_lfoTimeModel.value();` then `m_lfo->setFrequency( *lfoTimePtr );` where `lfoTimePtr` points at that local | seconds → Hz ✓ |
| automated (`valueBuffer()` non-null) | the same `setFrequency( *lfoTimePtr )`, but `lfoTimePtr` points at the **buffer**, which carries the raw model value | seconds used as Hz ✗ |

So automating the LFO period to 2 s set the LFO to 2 Hz instead of 0.5 Hz, while the
same knob left static at 2 s modulated at 0.5 Hz. The knob's own position worked;
automating it changed the rate by the factor `period²`.

This is the sibling of cluster new-p11-80 (the LFO **amount** was read as samples
instead of seconds on the automated path); that one is already fixed on this line at
`plugins/Delay/DelayEffect.cpp:122` (`*amplitudePtr * sr * tick`).

The upstream accepted fix (`git show 2098f5b2f`) reciprocates the automated value
before `setFrequency`. This port does the same, and additionally makes the units
explicit so the two paths cannot drift apart again.

## The fix

`plugins/Delay/DelayEffect.cpp` — the period is converted where it is used, for both
paths alike:

```diff
-	float lfoTime = 1.0 / m_delayControls.m_lfoTimeModel.value();
+	// The model holds the LFO PERIOD in seconds, like the amount above: the
+	// static path and the automated path carry the same unit and the
+	// conversion to the unit the LFO wants happens where it is used.
+	float lfoPeriod = m_delayControls.m_lfoTimeModel.value();
...
-	float *lfoTimePtr = lfoTimeBuffer ? &( lfoTimeBuffer->values()[ 0 ] ) : &lfoTime;
+	float *lfoPeriodPtr = lfoTimeBuffer ? &( lfoTimeBuffer->values()[ 0 ] ) : &lfoPeriod;
...
-		m_lfo->setFrequency( *lfoTimePtr );
+		// setFrequency() wants Hz, the model holds the period in seconds.
+		const float lfoFrequency = 1.0 / *lfoPeriodPtr;
+		m_lfo->setFrequency( lfoFrequency );
```

Notes:

* The static path evaluates `1.0 / value` in the same expression, with the same double
  division and the same rounding to `float`, as before — it is bit-identical to the
  pre-fix static path (the local variable now holds the period, the conversion moved
  to the point of use, exactly the shape cluster new-p11-80 gave the amount path).
* The automated path now reciprocates each buffer value, like the static path.
* One division per sample on the audio thread; no allocation, no lock, no logging,
  no I/O, no signal. `Lfo::setFrequency()` ignores a frequency outside
  `[0, sampleRate/2]`, so even a degenerate period of 0 s (1/0 = inf) leaves the LFO
  on its last frequency instead of producing a NaN.
* No setting key changed: the model still saves/loads under `LfoFrequency`
  (`plugins/Delay/DelayControls.cpp:54`), so old projects load exactly as before.

## The regression test

`tests/src/plugins/DelayLfoAutomationTest.cpp` (CTest entry `DelayLfoAutomationTest`,
the Delay plugin sources are compiled into that binary). The new case
`automatedLfoPeriodUsesFrequencyNotPeriod()` reuses the file's existing oracle.

Oracle: wet=1 / dry=0 / feedback=0 makes the effect a pure delay line and the input
is an exact ramp of slope 1/8192 (a power of two, float-exact). The wet output at
frame `g` is then exactly `slope · (g − D(g))`, so `out − slope·g` is a direct
readout of the integer delay length `D(g)` in samples. `D(g)` follows the LFO, so
**the residual is the LFO**: filtering it at 0.5 Hz and at 2 Hz tells the two
readings apart. The measurement window is exactly 6 s (3 whole cycles at 0.5 Hz, 12
at 2 Hz), so the two probe frequencies are exactly orthogonal and each probe sees
only its own tone, whatever phase the fresh `Lfo` starts at.

* **static render** — the period is a plain model value (`valueBuffer()` is nullptr):
  the 0.5 Hz probe reads ≈ 1.0767 ramp units (`amount·sampleRate/8192`), the 2 Hz
  probe reads ≈ 0. This is the reference the automated path must agree with.
* **automated render** — a second fresh `DelayEffect` where every period publishes a
  constant `AutomationRamp` of 2.0 s for the period model
  (`incrementPeriodCounter` → `publishAutomationRamp` → `processAudioBuffer`, the
  production block sequence): with the fix the 0.5 Hz probe reads the same ≈ 1.0767;
  with the bug it reads 4.4e-08 (nothing) and the 2 Hz probe carries the tone.

Assertions, in order: static 0.5 Hz tone > half the expected depth (harness control
— engine, settings, oracle), static 0.5 Hz tone > 4× the 2 Hz tone (the static path
really is at 0.5 Hz), automated 0.5 Hz tone > half the expected depth (the regression
gate), automated 0.5 Hz tone > 4× the 2 Hz tone (the period is not read as a
frequency), and automated/static tone ratio ∈ (0.75, 1.25) (both paths mean the same
thing).

The existing `automatedLfoAmountMatchesStatic()` case is untouched in behaviour; only
its two `renderWetRamp` call sites grew the new `automatedValue` argument.

## Evidence

All logs are in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-79-port.r1/logs/`.
Commands ran from the worktree root except `ctest`, which ran from `build/tests`.

| step | exact command | EXIT / result | log |
|---|---|---|---|
| baseline build | `cmake --build build --target DelayLfoAutomationTest` | 0 | `build-1.log` |
| baseline test | `ctest -R '^DelayLfoAutomationTest$' --output-on-failure` | 0; 1/1 passed | `test-baseline.log` |
| build with the new test (pre-fix) | `cmake --build build --target DelayLfoAutomationTest` | 0 | `build-2.log` |
| **RED** | `ctest -R '^DelayLfoAutomationTest$' --output-on-failure` | **8**; 3 passed, 1 failed | `test-1-red.log` |
| build with the fix | `cmake --build build --target zene DelayLfoAutomationTest` | 0 | `build-3.log` |
| **GREEN** | `ctest -R '^DelayLfoAutomationTest$' --output-on-failure` | **0**; 1/1 passed | `test-2-green.log` |
| build the rest of the family | `cmake --build build --target MonoDelayTest EnvelopeAndLfoParametersTest StereoDelay LfoGraphTest DelayControlsTest` | 0 | `build-4.log` |
| family run | `ctest -R 'Delay\|Lfo' --output-on-failure` | 0; **6/6 passed** | `test-4-family.log` |
| reverse the product patch | `git diff -- plugins/Delay/DelayEffect.cpp > .../fix.patch && git apply -R .../fix.patch` | 0 | `fix.patch`, `reverse-patch-1.log` |
| rebuild without the fix | `cmake --build build --target zene DelayLfoAutomationTest` | 0 | `build-5-nofix.log` |
| **RED again (proof the test catches the bug)** | `ctest -R '^DelayLfoAutomationTest$' --output-on-failure` | **8**; 3 passed, 1 failed | `test-5-reverse-red.log` |
| restore the product patch | `git apply .../fix.patch` | 0 | `restore-patch.log` |
| final rebuild | `cmake --build build --target zene DelayLfoAutomationTest PluginPortsMigrationTest` | 0 | `build-6-final.log`, `build-10-final.log` |
| **GREEN again** | `ctest -R '^DelayLfoAutomationTest$' --output-on-failure` | **0**; 1/1 passed | `test-6-final-green.log` |
| family run, final | `ctest -R 'Delay\|Lfo' --output-on-failure` | 0; **6/6 passed** | `test-9-family-final.log` |
| requested ctest name | `ctest -N -R '^DelayEffect$'` | 0; `Total Tests: 0` (no such CTest entry on this line) | `discover-1.log` |
| migration harness | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | 8 — **pre-existing** failure, see below | `test-7-migration.log` |
| same harness with the fix reverted | `git apply -R .../fix.patch` + `cmake --build build --target zene PluginPortsMigrationTest` + `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | 8 — identical failure, so it is not caused by this change | `build-9-nofix-migration.log`, `test-8-migration-nofix.log` |
| wider plugin family | `ctest -R 'Delay\|Lfo\|Effect\|Plugin\|Fx\|Stereo' --output-on-failure` | 8; 24 entries `Not Run` (their binaries are not built in this tree), 1 pre-existing failure | `test-11-plugin-family.log` |
| gate table | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p11-79-port` | **0**; 8/8 rows PASS | `gate-1-feature-check.log` |
| complexity ratchet | `bash tests/complexity-gate.sh --check` | 1 — 6 pre-existing items, **none in the files of this change** | `gate-2-complexity.log` |
| file-length ratchet | `bash tests/file-length-gate.sh --check` | 1 — 14 pre-existing items, **none in the files of this change** | `gate-3-file-length.log` |

RED failure line (`test-1-red.log`, repeated identically in `test-5-reverse-red.log`):

```
FAIL!  : lmms::DelayLfoAutomationTest::automatedLfoPeriodUsesFrequencyNotPeriod()
'automatedPeriodTone > 0.5 * expectedTone' returned FALSE.
(an automated LFO period of 2 s does not modulate at 0.5 Hz: tone=4.44576e-08,
 expected about 1.07666 (was the period read as a frequency?))
```

GREEN output (`test-2-green-detail.log`):

```
QINFO  : ...automatedLfoPeriodUsesFrequencyNotPeriod() delay LFO period tone at 0.5 Hz:
         static=1.07666 automated=1.07666 (expected about 1.07666)
Totals: 4 passed, 0 failed, 0 skipped, 0 blacklisted, 1114ms
```

1.07666 = `amount(0.2) · sampleRate(44100) / 8192` — the oracle's prediction for a
0.5 Hz modulation of the delay, identical on the static and the automated path.

The pre-existing `PluginPortsMigrationTest` failure is
`nonRealtimeLadspaEffectsAreInvalid()` ("SWH Decimator test fixture was not
loaded") — a LADSPA fixture that is not built in this environment. It fails with and
without this change (both runs above), and it is unrelated to the Delay plugin. The
Delay part of that harness (`migratedPluginsPreserveBehaviour`, which compares the
migrated plugin against the frozen reference in `tests/reference/Delay/`) passes,
because the harness drives the **static** path (`loadSettings → setInitValue` leaves
`valueBuffer()` null), which this fix leaves bit-identical.

## Files committed

* `plugins/Delay/DelayEffect.cpp` — the unit fix (the period is converted to a
  frequency where it is used, for both paths).
* `tests/src/plugins/DelayLfoAutomationTest.cpp` — the regression case
  `automatedLfoPeriodUsesFrequencyNotPeriod()` and the tone-probe helper; already
  registered in `tests/all-sources.txt` (fork-NEW test source, no new registration
  needed).
* `tests/upstream-modifications.txt` — the reason appended to the existing
  `plugins/Delay/DelayEffect.cpp` entry.
* `docs/qwen-fixes-2026-10/cluster-new-p11-79-port.md` — this report.

## Self-check

1. **The regression test FAILS with the product change reverted and PASSES with it.**
   RED: `ctest -R '^DelayLfoAutomationTest$' --output-on-failure` → EXIT 8
   (`test-1-red.log`, and again `test-5-reverse-red.log` after `git apply -R` of
   `fix.patch` + rebuild). GREEN: the same command → EXIT 0 (`test-2-green.log`,
   `test-6-final-green.log`).
2. **The test is deterministic.** No wall-clock, no sleeps, no threads, no races. It
   injects every value it needs: the LFO period is written into the controls element
   and loaded back (`LfoFrequency` = 2.0), the automation is a constant
   `AutomationRamp` published for the current period counter, the sample rate and
   the block size come from the engine, and the delay length is read out of the
   rendered samples. The analysis window is an exact number of samples (6 s), so the
   two probe frequencies are exactly orthogonal and the assertion thresholds are
   4× apart from the measured values (1.0767 vs 4.4e-08).
3. **The fix is at the cause and complete.** The cause is the unit mismatch between
   the model (period in seconds) and `Lfo::setFrequency()` (Hz); the conversion now
   happens once, at the point of use, for both paths. Callers and siblings checked:
   * `DelayEffect::processImpl` is the only reader of `m_lfoTimeModel` in the
     product (`rg m_lfoTimeModel` → `plugins/Delay/DelayEffect.cpp` only);
     `plugins/Delay/DelayControlsDialog.cpp:61` binds the same model to the
     `TempoSyncKnob`, which shows the period in seconds and is unaffected;
     `plugins/Delay/DelayControls.cpp:54` keeps the saved key `LfoFrequency`, so
     saved data is unchanged.
   * The sibling LFO parameter in the same function (the **amount**, seconds → samples)
     was fixed by cluster new-p11-80 and now has the same shape as the period:
     the local and the buffer carry the model unit and the conversion is at the
     point of use. The delay length (`lengthPtr`) is unchanged.
   * `Lfo` itself (`plugins/Delay/Lfo.h`) is untouched: it has always taken Hz, and
     the Flanger's own LFO (`plugins/Flanger/FlangerEffect.cpp:99`) has always
     reciprocated its model value itself and reads no value buffer, so it is not
     affected.
   * The frozen reference in `tests/reference/Delay/` is a verbatim upstream copy
     (blob ids in `tests/reference/ORIGIN.tsv`) and was deliberately not touched.
4. **No collateral damage.** The static path is bit-identical (same expression, same
   double division, same rounding); no model, setting key, dialog or project file
   changed, so old projects load and render exactly as before; no limit or check was
   added, so no other caller can trip over it. The affected family
   (`ctest -R 'Delay|Lfo'`, 6 CTest entries) is green, and the wider plugin family
   shows only the pre-existing `PluginPortsMigrationTest` failure that also fails
   without this change.
5. **"If your verdict is ALREADY-FIXED or FALSE-POSITIVE" — not applicable.** The
   verdict is FIXED (a real bug): the pre-fix code on this line handed the raw model
   value (seconds) to `Lfo::setFrequency()` at `plugins/Delay/DelayEffect.cpp:120`,
   while the static path reciprocated it at `plugins/Delay/DelayEffect.cpp:93`. Both
   lines are quoted in the RED log evidence above.
6. **Realtime, undo, commands.** The only audio-thread change is one double division
   per sample inside `DelayEffect::processImpl` — no allocation, no free, no mutex,
   no wait, no I/O, no logging, no signal, no new command, no new control, no undo
   step (the fix is a pure computation, not a user action). The `ControlCommands`
   snapshot is untouched.
7. **Gates and registration.** `feature_check.py` → EXIT 0 with all eight rows PASS
   (`gate-1-feature-check.log`); the complexity and file-length ratchets report only
   pre-existing items, none of them in the files of this change
   (`gate-2-complexity.log`, `gate-3-file-length.log`). The changed product file is
   declared with its reason in `tests/upstream-modifications.txt`; the changed test
   file is already registered in `tests/all-sources.txt`.
