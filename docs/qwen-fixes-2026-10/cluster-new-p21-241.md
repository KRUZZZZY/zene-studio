FALSE-POSITIVE

Cluster new-p21-241: "Metronome tick math can divide/modulo by zero" (src/core/Metronome.cpp:35-38, finding line 40).

## Verdict

No value the product can hand to `Metronome::processTick` makes `beatsPerBar` zero, makes `ticksPerBeat` zero, or
leaves `Engine::getSong()` null while a tick is processed. The three preconditions in the finding are excluded by code
on every write path. The one caller, `Song::processMetronome`, passes the song's own time signature, and that
time signature is clamped to 1..32 for numerator and denominator.

## Evidence (file:line)

1. **The only caller passes the song's time signature.** `src/core/Song.cpp:971`:
   `m_metronome.processTick(currentTick(), ticksPerBar(), m_timeSigModel.getNumerator(), bufferOffset, ...)`.
   `ticksPerBar()` is `TimePos::ticksPerBar(m_timeSigModel)` (`include/Song.h:249-251`), which is
   `DefaultTicksPerBar * num / den` (`include/TimePos.h:149`, `DefaultTicksPerBar = 192` at `include/TimePos.h:39`).
   Nothing else calls `processTick` (`rg -n processTick src include tests`: the definition and this call only).

2. **The numerator and denominator are IntModels clamped to 1..32.** `src/core/MeterModel.cpp:35-36`:
   `m_numeratorModel( 4, 1, 32, ...)`, `m_denominatorModel( 4, 1, 32, ...)`.
   Every write goes through `AutomatableModel::setValue` (`src/core/AutomatableModel.cpp:462`, which calls `fittedValue`)
   or `setValueInternal` (`:497-500`, which also calls `fittedValue`). `fittedValue` clamps to `[m_minValue, m_maxValue]`
   at `src/core/AutomatableModel.cpp:604-606`. Loading a saved project (`AutomatableModel::loadSettings`, `:419`/`:440`) and
   automation (`:347`) take the same path, so a hand-edited or automated `0` or `1000` becomes `1` or `32`.
   The `TimeSig(const MeterModel&)` constructor (`src/core/TimePos.cpp:45-48`) does not check, so the clamp is what holds.

3. **The arithmetic cannot reach zero.** With numerator `n` in 1..32 and denominator `d` in 1..32,
   `ticksPerBar = floor(192 n / d) >= 6n`, so `ticksPerBeat = ticksPerBar / n >= 6` and `beatsPerBar = n >= 1`
   (`src/core/Metronome.cpp:35`). Checked exhaustively over all 1024 signatures:
   `$TMPDIR` program mirrored from `TimePos::ticksPerBar(TimeSig)` and `Metronome.cpp:35`, run output
   `models=1024 min_ticksPerBeat=6 at 1/28`. Source: `logs/invariant-check.cpp`; build EXIT=0, run EXIT=0
   (`logs/invariant-build-1.log`, `logs/invariant-run-1.log`).

4. **The engine song is non-null whenever ticks are processed.** `src/core/Engine.cpp:72` sets `s_song = new Song`.
   `src/core/Engine.cpp:102` stops audio processing before `src/core/Engine.cpp:123` deletes the song. `processMetronome`
   is a `Song` member (`src/core/Song.cpp:962`), so the song that calls it is the engine song, and the null check
   `Engine::getSong()->isRecording()` (`src/core/Metronome.cpp:38`) is safe.

So the finding's "when" cases are not reachable:
- `beatsPerBar == 0`: needs numerator 0, which the clamp (2) makes impossible.
- `ticksPerBar < beatsPerBar`: needs `192/d < 1`, i.e. `d > 192`, which the clamp (2) makes impossible.
- Null song during a tick: the song is set for the engine's whole processing lifetime (4).

`processTick` is still a public method with unchecked int parameters (`include/Metronome.h:39`). A direct caller passing
`ticksPerBar = 0` or `beatsPerBar = 0` would trap. That is a contract question for the header, not a defect on any
product path. I did not add a guard: that would be a behaviour-free change to code no product path can reach, and the
brief asks for changes that fix a real defect.

## What I added (pins the invariant the verdict rests on)

Because the verdict depends on the clamp, I added a test that fails if the clamp is removed. It is in the existing
`tests/src/core/TimelineTest.cpp` (new slot `MetronomeTickMathTests`, plus `#include "Metronome.h"`). It is not a
regression test for a live bug, so it is not a RED/GREEN proof of a fix. Its job is to fail if the bounds change.

- Clamp: `setNumerator(0)`→1, `setNumerator(1000)`→32, `setDenominator(0)`→1, `setDenominator(1000)`→32.
- All 1024 signatures: `ticksPerBeat >= 1` and `processTick` over every tick of the bar, with the metronome inactive
  (so no click is built and the audio path is not run).

I did not add a new test file. The configured build tree refuses reconfigure (`bunny-guard` in
`/home/kruzzzzy/.local/share/zene-bunny-fix/bunny/shims/cmake`: "configuring is the parent's job"), so a new CMake
target could not be created from this lane. The test went into an existing registered target instead.

## Commands and exit codes

Mutation check (proves the test catches the hazard): widened `src/core/MeterModel.cpp:35` to `m_numeratorModel( 4, 0, 32, ...)`.
The patch is `logs/mutation.patch`. Restored with `git apply -R logs/mutation.patch` (EXIT=0, `logs/restore-1.log`).

| Step | Command (run from the worktree / `build/tests`) | Log | EXIT |
|---|---|---|---|
| Build, new cases | `CCACHE_DISABLE=1 cmake --build build --target TimelineTest -j4` | `logs/build-4.log` | 0 |
| Pass on current code | `./TimelineTest` (in `build/tests`) | `logs/test-1-direct.log` | 0 (7 passed) |
| Pass on current code, ctest | `ctest -R '^TimelineTest$' --output-on-failure` | `logs/test-1.log` | 0 (1/1 passed) |
| Mutation build | `CCACHE_DISABLE=1 cmake --build build --target TimelineTest -j4` | `logs/build-5.log` | 0 |
| Mutation run (expect FAIL) | `./TimelineTest` | `logs/test-2-mutated.log` | 1 |
| Restore | `git apply -R logs/mutation.patch` | `logs/restore-1.log` | 0 |
| Restored build | `CCACHE_DISABLE=1 cmake --build build --target TimelineTest -j4` | `logs/build-6.log` | 0 |
| Restored run (expect PASS) | `ctest -R '^TimelineTest$' --output-on-failure` | `logs/test-3-green.log` | 0 (1/1 passed) |
| Family build | `CCACHE_DISABLE=1 cmake --build build --target RevisionTimelineTest ArrangerTimelineTest -j4` | `logs/build-7.log` | 0 |
| Family run | `ctest -R 'Timeline' --output-on-failure` (in `build/tests`) | `logs/test-5-family.log` | 0 (3/3 passed) |
| Gates | `python3 <gate copy> <worktree>` (see note) | `logs/feature-check-3.log` | 0 (RESULT: PASS, 8/8 rows) |

The mutation failure, from `logs/test-2-mutated.log`:
`FAIL! : TimelineTest::MetronomeTickMathTests() Compared values are not the same`,
`Actual (meter.getNumerator()): 0`, `Expected (1): 1`, at `tests/src/core/TimelineTest.cpp(126)`.

Family note: `RevisionTimelineTest` and `ArrangerTimelineTest` were "Not Run" (binary missing) in the first family
pass (`logs/test-4-family.log`). I built them (`logs/build-7.log`) and reran the family: 3/3 passed (`logs/test-5-family.log`).

Gate note: `feature_check.py` writes its per-gate logs to `<worktree>/../<name>-feature-check`, which is outside this
lane's write-jail, and it failed there with `PermissionError` (`logs/feature-check-1.log`). I ran a copy with only the
log directory and the `ROOT` path changed, so the gate logic is untouched. The copy is at `$TMPDIR/p21/fc_logdir.py`.
The shared `gate-baselines` cache was read (the base listing for `cf3618e61` was already there).

Build note: `ccache` could not write to its temp dir in this sandbox (`logs/build-3.log`), so every build used
`CCACHE_DISABLE=1`. That changes the cache, not the output.

## Files changed

- `tests/src/core/TimelineTest.cpp`: new slot `MetronomeTickMathTests` and `#include "Metronome.h"`.
- `docs/qwen-fixes-2026-10/cluster-new-p21-241.md`: this report.

No product file changed. `src/core/MeterModel.cpp` was mutated for the check and restored. `git diff` is empty for it.
No new file, so `tests/fork-sources.txt`, `tests/all-sources.txt` and `tests/CMakeLists.txt` are unchanged.

## Self-check

- **Regression test FAILS with the product change reverted and PASSES with it.** There is no product change, so the
  RED/GREEN pair is the mutation check above. With the clamp widened (`MeterModel.cpp:35`), the test fails with EXIT=1
  (`logs/test-2-mutated.log`). With the code restored, it passes with EXIT=0 (`logs/test-3-green.log`).
- **Deterministic.** The test has no wall-clock, sleep, or race. Inputs are the enumerated signatures 1..32 × 1..32
  and the ticks `0..ticksPerBar-1`. The metronome is inactive, so `processTick` returns after the modulo check and never
  reaches the click or the audio engine.
- **Fix at the cause and complete.** Not applicable: no fix is made. The verdict covers the only caller
  (`Song.cpp:971`) and the only write paths into the time signature (`AutomatableModel.cpp:462`, `:497-500`, `:604-606`).
- **No collateral damage.** No product code changed. The test is in an existing registered target, so no
  registration changed.
- **Verdict evidence.** `src/core/MeterModel.cpp:35-36`, `src/core/AutomatableModel.cpp:604-606`,
  `src/core/Song.cpp:971`, `src/core/Engine.cpp:72,102,123`, `logs/invariant-run-1.log`.
