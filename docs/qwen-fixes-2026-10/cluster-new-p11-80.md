# Cluster new-p11-80 — "Automated LFO amount is treated as samples" (Delay)

**Verdict: REAL BUG.** Confirmed, fixed, and pinned by a deterministic
regression test that is RED on the pre-fix code and GREEN after the fix.

## The bug

`plugins/Delay/DelayEffect.cpp`, `DelayEffect::processImpl` (lines 88-144).
The delay length fed to `StereoDelay::setLength()` is

```
m_currentLength + amplitudeModulation
```

where `m_currentLength` is in **samples**. The two paths into
`amplitudeModulation` disagreed on units:

| path | pre-fix code | units |
|---|---|---|
| static (no automation on the knob) | `float amplitude = ...m_lfoAmountModel.value() * sr;` then `setLength(m_currentLength + (amplitude * tick()))` | seconds × sampleRate → samples ✓ |
| automated (`valueBuffer()` non-null) | `setLength(m_currentLength + (*amplitudePtr * tick()))` — `*amplitudePtr` is the raw model value | seconds used as samples ✗ |

`m_lfoAmountModel` is a seconds-of-modulation control with range `[0, 0.5]`.
On the automated path that whole value was read as a sample count: at 44.1 kHz
the modulation collapsed from ±8820 samples (0.2 s) to ±0.2 samples —
below the integer truncation `StereoDelay::tick()` applies to the read index
(`static_cast<int>(m_length)`), so an automated LFO amount was, to the ear and
to the samples, almost no modulation at all. The knob's own static position
worked; automating it broke it.

## The fix

Two lines in `plugins/Delay/DelayEffect.cpp`, one per path:

```diff
-	float amplitude = m_delayControls.m_lfoAmountModel.value() * sr;
+	float amplitude = m_delayControls.m_lfoAmountModel.value();
...
-		m_delay->setLength( m_currentLength + ( *amplitudePtr * ( float )m_lfo->tick() ) );
+		m_delay->setLength( m_currentLength + ( *amplitudePtr * sr * ( float )m_lfo->tick() ) );
```

The conversion moved from the static-only precomputation into the shared
expression. The static path evaluates `(*amplitudePtr * sr) * tick` =
`(value * sr) * tick` — the same float operations in the same order as before,
so its output is bit-identical. The automated path now converts each buffer
value the same way. One extra float multiply per sample on the audio thread;
no allocation, no lock, no logging.

This also keeps the migration harness green by construction:
`PluginPortsMigrationTest` applies settings via
`loadSettings → setInitValue`, which makes `valueBuffer()` return nullptr, so
the harness exercises the static (bit-identical) path on both the migrated
plugin and the frozen reference.

## The regression test

`tests/src/plugins/DelayLfoAutomationTest.cpp` (new target
`DelayLfoAutomationTest`; the Delay plugin sources are compiled into the test
binary — the WasmSandboxTest pattern — so both LFO-amount paths run in-process
without loading a module).

Oracle: wet=1 / dry=0 / feedback=0 makes the effect a pure delay line, and the
input is an exact ramp of slope 1/8192 (a power of two, float-exact). The wet
output at frame `g` is then exactly `slope · (g − D(g))`, so
`out − slope·g` is a direct readout of the integer delay length `D(g)`. Its
min-max spread over two full LFO periods is `2 · amount · sampleRate`
regardless of the (uninitialised) phase the fresh `Lfo` starts at:

* **static render** — amount is a plain model value (`valueBuffer()` is
  nullptr): spread ≈ 2.15 ramp units at 44.1 kHz (the full ±0.2 s sweep).
* **automated render** — a second fresh `DelayEffect` where every period
  publishes a constant `AutomationRamp` for the amount
  (`incrementPeriodCounter` → `publishAutomationRamp` → `processAudioBuffer`,
  the production block sequence): with the fix the spread matches the static
  one; with the bug it collapses to one ramp step (1/8192 ≈ 1.2e-4).

Assertions (in order): static spread > 0.5 (harness control — engine, router
activation, settings, oracle), automated spread > 0.5 (the regression gate),
and automated/static ratio ∈ (0.75, 1.25) (same sweep depth — fails on a
partial or over-scaled conversion).

## Evidence

All logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-80.r1/logs/`.

| step | command (cwd = worktree unless noted) | EXIT | log |
|---|---|---|---|
| build test (pre-fix) | `cmake --build build --target DelayLfoAutomationTest` | 0 | build-6.log |
| **RED** | `ctest -R '^DelayLfoAutomationTest$' --output-on-failure` (cwd `build/tests`) | 8 | test-3-red.log |
| fix applied | `git diff plugins/Delay/DelayEffect.cpp` (2 lines, shown above) | — | — |
| build fix | `cmake --build build --target zene delay DelayLfoAutomationTest` | 0 | build-7.log |
| **GREEN** | `ctest -R '^DelayLfoAutomationTest$' --output-on-failure` (cwd `build/tests`) | 0 | test-4-green.log |
| family sanity | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` (cwd `build/tests`) | 0 | test-6-migration.log |
| reverse patch | `git diff -- plugins/Delay/DelayEffect.cpp > fix.patch && git apply -R fix.patch` | 0 | fix.patch |
| rebuild reverted | `cmake --build build --target zene delay DelayLfoAutomationTest` | 0 | build-9-reversed.log |
| **RED again (proof of catch)** | `ctest -R '^DelayLfoAutomationTest$' --output-on-failure` (cwd `build/tests`) | 8 | test-7-red-reversed.log |
| re-apply | `git apply fix.patch` | 0 | — |
| rebuild + **GREEN again** | `cmake --build ...` then `ctest -R '^DelayLfoAutomationTest$'` (cwd `build/tests`) | 0 | build-10-reapplied.log, test-8-green-reapplied.log |
| final pair | `ctest -R '^(DelayLfoAutomationTest\|PluginPortsMigrationTest)$' --output-on-failure` (cwd `build/tests`) | 0 | test-9-final-both.log |
| complexity gate | `bash tests/complexity-gate.sh --check` | 0 | gate-complexity-check.log |
| file-length gate | `bash tests/file-length-gate.sh --check` | 0 | gate-filelength-check.log |

RED failure line (test-3-red.log, repeated identically in
test-7-red-reversed.log):

```
FAIL! : ...automatedLfoAmountMatchesStatic() 'automatedSpread > MinDeepSpread' returned FALSE.
(automated LFO amount no longer modulates the delay like the static one:
 automated spread=0.00012207, static spread=2.15332 (seconds were read as samples?))
```

0.00012207 = 1/8192 (one ramp step — the integer read index barely moves);
2.15332 = 2 · 0.2 · 44100 / 8192 (the full ±0.2 s sweep). Both numbers are
exactly the oracle's predictions.

Whole-tree gate scope (`--check --scope all`) fails on pre-existing ratchet
debt in files this change does not touch (e.g. `ClapHostTest.cpp` 505→614,
`main.cpp`, `ClipView.cpp`); no regression is attributed to any file in this
change — `DelayEffect::processImpl`'s CCN 11 is grandfathered and report-only
(gate-complexity-check-all.log, gate-filelength-check-all.log).

## Files committed with this fix

* `plugins/Delay/DelayEffect.cpp` — the 2-line unit fix (ledger entry extended
  in `tests/upstream-modifications.txt`).
* `tests/src/plugins/DelayLfoAutomationTest.cpp` — new regression test
  (registered in `tests/all-sources.txt`).
* `tests/CMakeLists.txt` — new `DelayLfoAutomationTest` target.
* `tests/all-sources.txt` — registration of the new source.
* `tests/upstream-modifications.txt` — reason appended to the existing
  DelayEffect.cpp entry.
* `docs/qwen-fixes-2026-10/cluster-new-p11-80.md` — this report.
