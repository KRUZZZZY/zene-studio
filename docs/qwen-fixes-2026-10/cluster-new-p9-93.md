# Cluster new-p9-93 — "Crossover EQ filters keep the old sample rate"

**Verdict: FALSE-POSITIVE**

The finding claims that `CrossoverEQEffect::sampleRateChanged()` is never connected
or called after construction, so the crossover filters keep the old sample rate.
That is not true on this tree. The effect's helper *is* wired to
`AudioEngine::sampleRateChanged`, from the controls object that the effect owns.

## Evidence

- `plugins/CrossoverEQ/CrossoverEQControls.cpp:49` (constructor) —
  `connect( Engine::audioEngine(), SIGNAL( sampleRateChanged() ), this, SLOT( sampleRateChanged() ) );`
- `plugins/CrossoverEQ/CrossoverEQControls.cpp:118-121` — the slot forwards to the
  effect: `m_effect->sampleRateChanged();`
- `plugins/CrossoverEQ/CrossoverEQ.cpp:79-89` — the effect sets the new rate on all
  six `StereoLinkwitzRiley` filters and raises `m_needsUpdate`.
- `plugins/CrossoverEQ/CrossoverEQ.cpp:95-109` — `processImpl` sees `m_needsUpdate`
  and recomputes the filter coefficients at the new rate.
- `include/BasicFilters.h:68-71` (`LinkwitzRiley::setSampleRate`) and
  `include/BasicFilters.h:101-115` (`setLowpass`/`setHighpass`) — the coefficients
  are derived from `m_sampleRate`, so the retune is real.
- The connect is `m_controls` in the effect (`plugins/CrossoverEQ/CrossoverEQ.h:54`),
  a direct member constructed with the effect, so the connection is always live.
- The pre-migration reference copy carries the identical connect
  (`tests/reference/CrossoverEQ/CrossoverEQControls.cpp:49`), so this was never a
  migration regression either. The finding was derived from `CrossoverEQ.cpp`
  alone and missed the controls constructor.
- The finding's suggested ctest names do not exist on this tree: `ctest -N -R CrossoverEQ`
  reports `Total Tests: 0`. Real coverage is `PluginPortsMigrationTest`.

## Runtime proof (temporary probe, not committed)

I added a temporary `crossoverEqRetunesAfterSampleRateChange` slot to
`tests/src/plugins/PluginPortsMigrationTest.cpp`, modeled exactly on the existing
MultitapEcho regression: create an effect at the original rate, change the rate with
`audioDev()->setSampleRateForTesting()` + `emit engine->sampleRateChanged()`, create a
second effect at the changed rate, and compare the two renders.

- On the **unmodified** product code the probe **PASSED** — proving the filters do
  retune, i.e. the finding is false.
- To prove the probe is sensitive (not vacuous), I then disabled the connect
  (`plugins/CrossoverEQ/CrossoverEQControls.cpp:49`) and the identical probe
  **FAILED** with `outdatedSamples == currentSamples` / "crossover filters differ
  after a sample-rate change". Re-enabling the connect made it pass again.
- Both the probe and the temporary mutation were reverted, so no test or product file
  was changed in this commit.

## Verification commands and results

Logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-93.r1/logs`.

| Step | Exact command | Result |
|---|---|---|
| Build probe target | `cmake --build build --target PluginPortsMigrationTest > LOGS/build-1.log 2>&1; echo EXIT=$?` | `EXIT=0` (`build-1.log`) |
| Probe on unmodified code | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > LOGS/test-1.log 2>&1; echo EXIT=$?` (from `build/tests`) | `EXIT=8`; probe `PASS` (`test-1.log:206`); the only failure is the pre-existing, unrelated `nonRealtimeLadspaEffectsAreInvalid` (`test-1.log:163`, SWH Decimator fixture not loaded) |
| Build with connect disabled | `cmake --build build --target PluginPortsMigrationTest > LOGS/build-2-mutation.log 2>&1; echo EXIT=$?` | `EXIT=0` (`build-2-mutation.log`) |
| Probe with connect disabled | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > LOGS/test-2-mutation.log 2>&1; echo EXIT=$?` | `EXIT=8`; `FAIL! ... crossoverEqRetunesAfterSampleRateChange() 'outdatedSamples == currentSamples' returned FALSE. (crossover filters differ after a sample-rate change)` (`test-2-mutation.log:206`) |
| Restore + rebuild | `git checkout -- tests/src/plugins/PluginPortsMigrationTest.cpp` then `cmake --build build --target PluginPortsMigrationTest crossovereq > LOGS/build-3-restored.log 2>&1; echo EXIT=$?` | `EXIT=0` (`build-3-restored.log`) |
| Family after restore | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > LOGS/test-3-restored.log 2>&1; echo EXIT=$?` | `EXIT=8`; `Totals: 44 passed, 1 failed, 2 skipped`; the 1 failure is the same pre-existing LADSPA fixture test |
| Complexity gate | `bash tests/complexity-gate.sh --check > LOGS/gate-complexity.log 2>&1; echo EXIT=$?` | `EXIT=1` — see "Pre-existing gate state" |
| File-length gate | `bash tests/file-length-gate.sh --check > LOGS/gate-filelen.log 2>&1; echo EXIT=$?` | `EXIT=1` — see "Pre-existing gate state" |

## Pre-existing gate state (not caused by this task)

Both ratchet gates already fail on the **unmodified** HEAD (`git status` clean before
and after; no product or test file differs from HEAD). They are red because the
fork baselines are stale relative to the train tip, not because of anything in this
cluster:

- `tests/complexity-baseline.tsv` — 17698 fork-scope functions measured, 68 over
  CCN 10, but the baseline holds 68 entries and is missing functions that landed
  after its last update (6 `REGRESSION:` lines, e.g.
  `dotCentreAlong@tests/src/gui/XyPadPairTest.cpp` CCN 16, added by `6ab6dcc23`).
- `tests/file-length-baseline.tsv` — 1792 fork-scope sources measured, 19 over 500
  code lines, but the baseline holds 3 file entries (14 `REGRESSION:` lines, e.g.
  `src/core/TakeLane.cpp` 520, `tests/src/core/DeviceDspTest.cpp` 752).

Re-anchoring either baseline is explicitly forbidden for a bug-fix task, and the
offending files are unrelated to CrossoverEQ, so no baseline or limit was touched.

**Files committed:** `docs/qwen-fixes-2026-10/cluster-new-p9-93.md` only.
