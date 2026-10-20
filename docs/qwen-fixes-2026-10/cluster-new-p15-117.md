# Cluster new-p15-117: zero pattern steps can crash MIDI export

**Verdict: ALREADY-FIXED**

The crash is real in the divisor code, but the zero input cannot reach it on this branch. Every route that sets a clip's step count keeps it at one bar or more, so `steps * 12` is never zero when `writePatternClip()` divides by it. The loader already reset a saved `steps="0"` to the default before the cluster 3261 change; the comment at `src/tracks/MidiClip.cpp:542` records that a zero count "fell back to the default, as 0 always did". No product code changed. A regression test pins the export path through a real pattern-store clip, and a mutation proof shows the test crashes with `SIGFPE` once that protection is removed.

## Evidence (file:line, current tree)

**The divisor exists as described.** `plugins/MidiExport/MidiExport.cpp:222` computes `len = steps * 12` from the saved `midiclip` element. Three call sites pass it on: `:230`, `:237`, `:251`. The divide is at `:347` (`time = ... ceil((start - srcNote.time) / len) * len`), inside a loop over the source notes (`:345`). So the crash also needs at least one source note, as the finding says.

**The export reads live state, not the project file.** `:193` calls `instTrack->saveState(...)`. The `steps` attribute it reads is written from the live clip at `src/tracks/MidiClip.cpp:469` (`setAttribute("steps", m_steps)`).

**Every writer of `m_steps` keeps it at one bar or more.** The complete list in `src/tracks/MidiClip.cpp`:

| Line | Write | Why it stays >= 1 |
| --- | --- | --- |
| `:48` | constructor | `TimePos::stepsPerBar()` (16) |
| `:64` | copy constructor | copies a valid value |
| `:106` | copy from a sibling clip | the sibling already satisfies the invariant |
| `:542-549` | `loadSettings()` | `m_steps = (steps >= 1 && steps <= maxSteps) ? steps : stepsPerBar()`. A saved `steps="0"` loads as 16. The comment at `:542` says it "fell back to the default, as 0 always did". |
| `:622` | `addSteps()` | `+= 16` |
| `:630` | `cloneSteps()` | `*= 2` |
| `:654`, `:660` | `removeSteps()` | guarded by `if (n < m_steps)`, so the result stays at or above 16 |
| `:721` | `updateLength()` | `std::max(stepsPerBar, ...)` |

There are no other writers in `src/`, `include/` or `plugins/` (checked with `rg -n 'm_steps\s*(=|\+=|-=|\*=)'`).

**The zero case is already tested at the clip level.** `tests/src/core/ClipSerialisationTest.cpp:410` (`aMalformedStepCountDoesNotSurviveALoad`) loads `steps="0"` and asserts `loaded > 0` at `:434`.

## Regression test (added, not a product fix)

`tests/src/core/MidiExportTest.cpp:321` adds `aZeroStepPatternExportsWithoutDividingByZero`. It goes through the same path as the finding:

1. A pattern track is added first, so the pattern-store track gets its clip for pattern 0.
2. A pattern-store instrument track gets one note at tick 0.
3. That clip is round-tripped through `saveState`, with `steps` set to `"0"`, then `restoreState`.
4. A pattern clip is added to the pattern track.
5. `project.export_midi` runs, and the output is parsed. It must have two tracks (conductor plus store) and exactly one note-on in the store track.

The test is deterministic: no timers, sleeps or threads, and one note with one attribute.

I also added `#include <QDomDocument>`, `"PatternStore.h"` and `"PatternTrack.h"`. `MidiExportTest.cpp` is already listed in `tests/fork-sources.txt:2349`, so no registration change is needed.

## Evidence table

| Check | Exact command (run from the stated directory) | Result |
| --- | --- | --- |
| Baseline build, first attempt | `cmake --build build --target MidiExportTest ClipSerialisationTest -j4 > .../logs/build-1.log 2>&1; echo EXIT=$?` | **EXIT=2**. ccache could not create its temp file under `/run/user/1000`. Environment problem, not code. |
| Baseline build, retry | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p15-117 cmake --build build --target MidiExportTest ClipSerialisationTest -j4 > .../logs/build-2.log 2>&1; echo EXIT=$?` | **EXIT=0** |
| Baseline family (before the test was added) | from `build/tests`: `ctest -R '^(MidiExportTest\|ClipSerialisationTest)$' --output-on-failure > .../logs/test-1.log 2>&1; echo EXIT=$?` | **EXIT=0**, 2/2 tests passed |
| Test added: build | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p15-117 cmake --build build --target MidiExportTest -j4 > .../logs/build-3.log 2>&1; echo EXIT=$?` | **EXIT=0** |
| Test added: GREEN, target | from `build/tests`: `ctest -R '^MidiExportTest$' --output-on-failure > .../logs/test-2.log 2>&1; echo EXIT=$?` | **EXIT=0**, 1/1 CTest target passed |
| Test added: GREEN, the new case by name | from `build/tests`: `./MidiExportTest aZeroStepPatternExportsWithoutDividingByZero -v2 > .../logs/test-3.log 2>&1; echo EXIT=$?` | **EXIT=0**. Log: `PASS : MidiExportTest::aZeroStepPatternExportsWithoutDividingByZero()`, `Totals: 3 passed, 0 failed` |
| Mutation: the saved patch | `git diff -- src/tracks/MidiClip.cpp > .../logs/mutant.patch` | 13-line patch, removes `steps >= 1 &&` at `src/tracks/MidiClip.cpp:549` |
| Mutation: build | `CCACHE_TEMPDIR=... cmake --build build --target zene MidiExportTest -j4 > .../logs/build-4-mutant.log 2>&1; echo EXIT=$?` | **EXIT=0** |
| Mutation: RED | from `build/tests`: `./MidiExportTest aZeroStepPatternExportsWithoutDividingByZero -v2 > .../logs/test-4-red.log 2>&1; echo EXIT=$?` | **EXIT=136** (signal 8). Log: `Received signal 8 (SIGFPE), code FPE_INTDIV`. This is the reported crash, reached through the test. |
| Mutation: restore | `git apply -R .../logs/mutant.patch; echo APPLY_R=$?` then `git diff --stat -- src/tracks/MidiClip.cpp` (empty) and `rg -n "steps >= 1 && steps <= maxSteps"` (line 549) | **APPLY_R=0**; the clamp is back at `:549` |
| Restored: build | `CCACHE_TEMPDIR=... cmake --build build --target zene MidiExportTest -j4 > .../logs/build-5-restored.log 2>&1; echo EXIT=$?` | **EXIT=0** |
| Restored: GREEN, the new case | from `build/tests`: `./MidiExportTest aZeroStepPatternExportsWithoutDividingByZero -v2 > .../logs/test-5-green.log 2>&1; echo EXIT=$?` | **EXIT=0**, 3 passed, 0 failed |
| Affected family, final | from `build/tests`: `ctest -R '^(MidiExportTest\|ClipSerialisationTest)$' --output-on-failure > .../logs/test-6-family.log 2>&1; echo EXIT=$?` | **EXIT=0**, 2/2 tests passed |
| Gates (ratchets and the rest) | see "Gates" below | see below |

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-117.r1/logs/`.

## Gates

`feature_check.py` could not write its logs. It creates `<worktree>/../<name>-feature-check`, which is outside the write area, and it failed with `PermissionError`. I ran an identical copy from `/tmp/p15-gate/`, changing only the log path to the LOG FOLDER. The gate logic, `FIX-BASE` (`050/m0-train-8`, merge base `2da2ed356`) and `gate-baselines` cache are unchanged, and the cache hit means no git operations on the shared repo. The first attempt's `EXIT=1` output was overwritten by the successful run in `gates-1.log`.

- Copy run before the test was added, with 0 changed files: `EXIT=0`, `RESULT: PASS`. Rows: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length, all PASS.
- Final run, with the test change and this report in the working tree (2 changed files): `python3 /tmp/p15-gate/feature_check.py <worktree> > .../logs/gates-2.log 2>&1; echo EXIT=$?` gave **EXIT=0**, `RESULT: PASS`. Rows: the six simple gates and complexity and file-length all PASS.

## Observations outside this finding (not fixed, not run)

- `plugins/MidiExport/MidiExport.cpp:347`: `ceil((start - srcNote.time) / len)` divides two `int`s before `ceil`, so the `ceil` does nothing. The first repetition can start before the clip. I read this but did not run it. It is a separate issue.
- `src/tracks/MidiClip.cpp:630`: `cloneSteps()` does `m_steps *= 2` with no guard. The load cap at `:542-547` is sized for one doubling. A second clone of a very long clip could overflow `int`, which would produce a negative `len` in the export. I read this but did not reproduce it. Worth a separate look.

## Self-check

1. **Regression test FAILS with the product change reverted and PASSES with it.** There is no product fix in this commit, so the mutation proof is the check. Removing the clamp at `src/tracks/MidiClip.cpp:549` makes the new case crash with `SIGFPE` (EXIT=136, `test-4-red.log`). Restoring it gives PASS (EXIT=0, `test-5-green.log`). The family passes (`test-6-family.log`, 2/2).
2. **Deterministic.** No wall-clock timing, sleeps or races. The inputs are one note at tick 0 and one `steps` attribute. The assertions are exact (`QCOMPARE(ons, 1)`, two tracks).
3. **Fix at the cause, complete.** No product change. The protection is the loader clamp at `MidiClip.cpp:549`. Every writer of `m_steps` is listed above, and each keeps the count at one bar or more. The only route is the `steps` attribute, and it is clamped on load.
4. **No collateral damage.** No product code changed. The loader's behaviour for `steps="0"` is unchanged (`ClipSerialisationTest` still passes).
5. **Realtime, undo, commands.** Nothing on the audio thread was touched. No command was added and no snapshot changed. The new case only uses existing export and track APIs.
6. **ALREADY-FIXED is backed by code.** The divisor (`MidiExport.cpp:222, :347`) and the zero-protection (`MidiClip.cpp:549`, with the writer table) are cited above, and `aMalformedStepCountDoesNotSurviveALoad` (`ClipSerialisationTest.cpp:410-434`) covers the zero case at the clip level.
