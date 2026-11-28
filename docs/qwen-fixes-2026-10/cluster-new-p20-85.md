FIXED

# Cluster new-p20-85: a MonoDelay length below one sample read a full-ring-old sample

## Decision: REAL (fixed at the cause)

`plugins/Flanger/MonoDelay.cpp` `tick()` computed its read tap as
`int readIndex = m_writeIndex - static_cast<int>(m_length);` (line 62 before this change). For a
positive `m_length` below one sample `static_cast<int>` yields `0`, so `readIndex` became
`m_writeIndex` - the slot the same call overwrites two statements later. On entry that slot still holds the
sample written `m_maxLength` ticks earlier, so a `setLength(0.5f)` delay returned a sample from a full
ring ago instead of half a sample.

Reproduced on this branch before touching the product file: with a 4-frame ring holding one cycle of
history (inputs 1, 2, 3, 4 at `setLength(1.0f)`) a 0.5-sample delay of `20.0` returned **1.0** (the
sample from four ticks = one full ring earlier) where the half-sample tap is `0.5*4 + 0.5*20 = 12.0`
(`test-red-3.log`, `Actual (sample): 1`, `Expected (12.0f): 12`).

Why this is reachable in the product and not only in a test:
`plugins/Flanger/FlangerEffect.cpp:65-66` constructs `MonoDelay( 1, outputSampleRate )` and
`:114-115` calls `setLength( length + amplitude * (lfo + 1.0) )` on every frame, i.e. fractional sample
lengths are the normal input; `setLength` accepts any `0 <= length <= m_maxLength`
(`plugins/Flanger/MonoDelay.h:46-52`, guard at `:48`), so a sub-sample length is legal, and the flanger's own delay and
LFO-amount models can drive it there.

## The fix

`plugins/Flanger/MonoDelay.cpp:57-87` - the read now has a defined sub-sample contract:

- `m_length < 1.0` (`:64-75`): linear interpolation between the two samples that bracket the request,
  the incoming sample (delay 0, `*sample`) and the newest stored sample (delay 1,
  `m_buffer[m_writeIndex - 1]`). Weight `(1 - m_length)` on the stored sample and `m_length` on the
  incoming one, so `setLength(0.5f)` is exactly half of each and `setLength(0.9f)` is 90 % of the input.
- `m_length >= 1.0` (`:76-83`): the previous whole-sample read, unchanged. `static_cast<int>` truncation
  for longer fractional delays is kept deliberately: the frozen pre-migration reference is rendered
  sample-exactly by `PluginPortsMigrationTest::migratedPluginsPreserveBehaviour`
  (`tests/src/plugins/PluginPortsMigrationTest.cpp:2620-2690` compares `sha256`), and the flanger fixture
  runs at `0.005 s` delay and `0.001 s` LFO depth, i.e. lengths of 220.5 samples and up
  (`tests/src/plugins/PluginPortsHarness.h:168-173`). Changing those lengths would change an unrelated
  render and break a gate this cluster must not weaken.
- Zero length still returns before any indexing (`:59`), so the invariant the earlier zero-capacity fix
  relies on (zero capacity implies zero length) is untouched.

Both branches are pure float arithmetic on existing members: no allocation, free, lock, wait, log, I/O
or Qt call was added on the audio thread. No header changed. No command, undo or save format changed.

## Registration

`tests/upstream-modifications.txt:766` - one new ledger line for `plugins/Flanger/MonoDelay.cpp`
(inherited upstream file), describing the sub-sample contract and naming the regression slot.

## Regression test

New slot `PluginPortsMigrationTest::subSampleDelayInterpolatesBetweenAdjacentSamples` in the existing
MonoDelay test file `tests/src/plugins/PluginPortsMigrationTest.cpp` (declaration `:1102`, body `:5409-5491`;
the file already includes the real `MonoDelay.cpp` at `:397`). Matrix:

| Path | Case | Expected |
| --- | --- | --- |
| failure (the finding) | 4-frame ring holding 1,2,3,4; `setLength(0.5)`; input 20 | `12.0` (`0.5*4 + 0.5*20`), pre-fix `1.0` |
| failure | empty ring, `setLength(0.5)`, input 4 | `2.0`, pre-fix `0.0` |
| exhaustion / wrap | five ticks at `setLength(0.5)` over a 4-frame ring (write index wraps twice) | `2, 5, 7, 9, 11` exactly |
| smallest legal ring | `MonoDelay{1, 1}`, `setLength(0.25)` | `2.0` then `10.0` |
| normal path | `setLength(1.0)` history then `setLength(2.7)`, input 100 | `3.0` (whole-sample read unchanged) |
| normal path | zero length, integer delays, sample-rate reconfiguration | existing slots `zeroLengthDelayPassesInputThrough`, `monoDelayUpdatesItsDelayAcrossSampleRateChanges`, `zeroCapacityDelayPassesInputThrough` still pass |
| audio thread | the slot calls `tick()` only | no allocation, lock, log or timing |
| platform | plain C++20 float arithmetic, no new API or header | builds with the rest of the TU |

Deterministic: fixed integer rings and fixed sample values, exact float comparisons; no clock, sleep,
thread or random input, so no seam or injected timestamp is needed.

## Commands and exit codes

Worktree `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p20-85`,
log folder `/home/kruzzzzy/.config/kads/zene/state/runs/new-p20-85.r1/logs` (`$LOGS` below), test runs
started from `build/tests`.

| Step | Command | EXIT | Log |
| --- | --- | --- | --- |
| RED build (test only, product untouched) | `CCACHE_DISABLE=1 cmake --build build --target PluginPortsMigrationTest -j4` | 0 | `build-1.log` |
| RED run | `./PluginPortsMigrationTest subSampleDelayInterpolatesBetweenAdjacentSamples` | **1** | `test-red-1.log` |
| GREEN build | `CCACHE_DISABLE=1 cmake --build build --target flanger PluginPortsMigrationTest -j4` | 0 | `build-green-1.log` |
| GREEN run | `./PluginPortsMigrationTest subSampleDelayInterpolatesBetweenAdjacentSamples` | **0** | `test-green-1.log` |
| Build the sibling ctest entry | `CCACHE_DISABLE=1 cmake --build build --target MonoDelayTest -j4` | 0 | `build-mono-1.log` |
| family (with fix) | `ctest -R "^(PluginPortsMigrationTest\|MonoDelayTest)$" --output-on-failure` | 8 | `test-family-1.log` (96 passed, 4 failed, 2 skipped) |
| 4b save product hunk | `git diff -- plugins/Flanger/MonoDelay.cpp > $LOGS/fix.patch` | 0 | `fix.patch` (37 lines) |
| 4b reverse | `git apply -R $LOGS/fix.patch` | 0 | - |
| 4b RED build | `CCACHE_DISABLE=1 cmake --build build --target flanger PluginPortsMigrationTest -j4` | 0 | `build-red-2.log` |
| 4b RED run | `./PluginPortsMigrationTest subSampleDelayInterpolatesBetweenAdjacentSamples` | **1** | `test-red-2.log` |
| 4b family without the fix | `ctest -R "^(PluginPortsMigrationTest\|MonoDelayTest)$" --output-on-failure` | 8 | `test-family-2.log` (95 passed, 5 failed) |
| 4b restore | `git apply $LOGS/fix.patch` | 0 | - |
| 4b GREEN build | `CCACHE_DISABLE=1 cmake --build build --target flanger PluginPortsMigrationTest -j4` | 0 | `build-green-2.log` |
| 4b GREEN run | `./PluginPortsMigrationTest subSampleDelayInterpolatesBetweenAdjacentSamples` | **0** | `test-green-2.log` |
| 4b family with the fix | `ctest -R "^(PluginPortsMigrationTest\|MonoDelayTest)$" --output-on-failure` | 8 | `test-family-3.log` (96 passed, 4 failed) |
| reorder RED (full-ring case first, same assertions) | `git apply -R $LOGS/fix.patch`; `CCACHE_DISABLE=1 cmake --build build --target PluginPortsMigrationTest -j4`; `./PluginPortsMigrationTest subSampleDelayInterpolatesBetweenAdjacentSamples` | 0; 0; **1** | `build-red-3.log`, `test-red-3.log` (`Actual 1`, `Expected 12.0f`) |
| reorder GREEN | `git apply $LOGS/fix.patch`; `CCACHE_DISABLE=1 cmake --build build --target flanger PluginPortsMigrationTest -j4`; `./PluginPortsMigrationTest subSampleDelayInterpolatesBetweenAdjacentSamples` | 0; 0; **0** | `build-green-3.log`, `test-green-3.log` |
| final family | `ctest -R "^(PluginPortsMigrationTest\|MonoDelayTest)$" --output-on-failure` | 8 | `test-family-4.log` (96 passed, 4 failed, 2 skipped) |
| gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | **0** | `gate-feature-check-2.log` |

`ctest` exits 8 because the `PluginPortsMigrationTest` entry contains four failures that are not this
cluster's (see below); the single test entries in the table above are the ones this change owns.
`test-red-1.log` shows the empty-ring case failing (`Actual 0`, `Expected 2.0`); after that first
cycle I moved the full-ring case first so the RED log shows the finding's own symptom (`Actual 1`,
`Expected 12.0`, `test-red-3.log`). The assertions are identical in both orders.

Gate table (`gate-feature-check-2.log`, unpiped exit code 0):

```
feature-check kads-new-p20-85 (base 3436a808b, 3 changed files; logs in .../build/feature-check)
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
EXIT=0
```

## What the family run shows about collateral damage

- With the fix: `PluginPortsMigrationTest` 96 passed, 4 failed, 2 skipped; `MonoDelayTest` passed
  (`test-family-4.log`).
- Without the fix (the same binary, product hunk reversed): 95 passed, 5 failed - the same four plus
  this cluster's slot (`test-family-2.log`). So the four were already failing on this branch and are
  unrelated; they also match the four reported as pre-existing on this line in
  `docs/qwen-fixes-2026-10/cluster-new-p17-91.md:27`:
  `malletsVoiceLimitIsEnforcedPerInstrument`, `malletsVoiceLimitSurvivesDeviceChange`,
  `slice8PluginsAreLiveAndExact` (malletsstk renders silence) and `nonRealtimeLadspaEffectsAreInvalid`
  (`SWH Decimator test fixture was not loaded`).
- The flanger render is byte-identical to the frozen pre-migration reference in the final run:
  `migratedPluginsPreserveBehaviour() flanger: 32768 samples | ... sha256 migrated=b1bf7025...c327
  reference=b1bf7025...c327 | max|delta|=0` (`test-family-4.log`). No other plugin's render changed.
- No save, project or model code was touched, so old projects load exactly as before.

## Files committed

- `plugins/Flanger/MonoDelay.cpp` (the fix, `:57-87`)
- `tests/src/plugins/PluginPortsMigrationTest.cpp` (declaration `:1102`, slot `:5409-5491`)
- `tests/upstream-modifications.txt` (ledger line for the inherited file)
- `docs/qwen-fixes-2026-10/cluster-new-p20-85.md` (this report)

## Self-check

1. **The regression test FAILS with the product change reverted and PASSES with it.** Reverted with
   `git apply -R $LOGS/fix.patch` (EXIT 0), rebuilt (`build-red-3.log`, EXIT 0), ran:
   EXIT **1**, `Actual (sample): 1`, `Expected (12.0f): 12` (`test-red-3.log`). Restored with
   `git apply $LOGS/fix.patch` (EXIT 0), rebuilt (`build-green-3.log`, EXIT 0), ran: EXIT **0**,
   `PASS subSampleDelayInterpolatesBetweenAdjacentSamples` (`test-green-3.log`). The same cycle was run
   earlier on the original ordering (`test-red-1/2.log` EXIT 1, `test-green-1/2.log` EXIT 0).
2. **Deterministic.** The slot constructs `MonoDelay{1, 4}`, `{1, 1}`, `{1, 8}` with fixed integers,
   feeds fixed sample values (1, 2, 4, 6, 8, 10, 12, 16, 20, 100) and compares exact floats. No clock,
   sleep, thread, timer or random source, so there is nothing to inject or synchronise.
3. **At the cause and complete.** The cause is the truncated read offset in `MonoDelay::tick`; the fix is
   there. Callers checked: `FlangerEffect` is the only caller (`plugins/Flanger/FlangerEffect.cpp:65-66`
   construct, `:101-102` `setFeedback`, `:114-115` `setLength`, `:118-123` `tick`, `:139-140`
   `setSampleRate`, inside `changeSampleRate` `:136-141`); it passes non-negative fractional lengths and needs no change. Siblings checked:
   `setLength` (`plugins/Flanger/MonoDelay.h:46-52`) still bounds `[0, m_maxLength]`; `setSampleRate`
   (`plugins/Flanger/MonoDelay.cpp:92-115`) still scales `m_length`, and the new branch reads that scaled
   value, so a rate change moves the tap with it; the zero-capacity path keeps `m_length == 0`, which
   returns at `:59` before any ring access. The same truncation exists in the Delay plugin's
   `StereoDelay` (see Other findings) - a different plugin and a different finding, left unchanged here.
4. **No collateral damage.** No save/load, model or command code changed; the only rendered output that
   can differ is a flanger (or direct `MonoDelay`) delay below one sample, which previously read the wrong
   sample. The frozen reference parity run is unchanged (`max|delta|=0`, `test-family-4.log`), and no
   limit or check was added that could reject another caller: `setLength`'s bounds are untouched.
5. **ALREADY-FIXED / FALSE-POSITIVE evidence:** not applicable - the verdict is FIXED, and the RED runs
   above show the defect on the unmodified product code.

No new control command, undo step or save-format change was introduced; the ControlCommandsSnapshot
canary is untouched (no file under `tools/mcp-zene-control/` changed).

## Other findings

- `plugins/Delay/StereoDelay.cpp:81` (`tick()`) has the identical truncation:
  `int readIndex = m_writeIndex - static_cast<int>(m_length);`. A sub-sample length there also reads the
  write slot (and, unlike MonoDelay, `StereoDelay::tick` has no zero-length early return, so a zero length
  reads the stale slot too). It is a different plugin and not part of this finding, so it was not changed.
  Its renders are pinned by the same frozen-reference comparison
  (`tests/src/plugins/PluginPortsMigrationTest.cpp:2620-2690`).
- Four `PluginPortsMigrationTest` slots fail on this branch before and after this change (Mallets voice
  limit x2, `slice8PluginsAreLiveAndExact`, `nonRealtimeLadspaEffectsAreInvalid`). They are environmental
  (missing SWH `decimator_1202.so` fixture, malletsstk soundfont) and untouched here.
