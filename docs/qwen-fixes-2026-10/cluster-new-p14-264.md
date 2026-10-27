FIXED

# Cluster new-p14-264

Millisecond offsets were converted to integers with a bare `static_cast` after `ceilf()`.
For NaN, an infinity, or a finite value outside the integer range that cast is undefined
behaviour. On x86-64 the NaN and infinity cases produce `INT64_MIN`/`2^63`, so `read(dst, NaN)`
read from the wrong position. The finding's `read()` and `movePosition()` paths and the
matching `write()` family (`msToFrames()`, same cast pattern) now go through one saturating
conversion: NaN becomes zero, and out-of-range values clamp to the limits of the target
integer type. Finite, in-range values use the same float expression and the same cast as
before, so their results are unchanged.

## File and line evidence

Product (the only product file changed is the header):

- `include/RingBuffer.h:213-224` adds `saturatingFrameCount<Int>()`: `isnan` → 0, `<= min` → min,
  `>= max` → max, otherwise `static_cast<Int>`. Comparisons are done in `float` against the
  exactly representable `min` and the rounded `max` (`2^63` / `2^64`), so no out-of-range cast
  is reachable.
- `include/RingBuffer.h:225-233`: `msToFrames()` (`:227`) and `msToFrameOffset()` (`:232`) use it.
  The argument expression `ceilf( ms * (float)m_samplerate * 0.001f )` is unchanged.
- `include/RingBuffer.h:31`: `#include <limits>`.

Callers checked (all in `src/core/RingBuffer.cpp` unless noted):

- `msToFrameOffset()`: `:196` (`movePosition(float)`), `:245` (`read(float)`), `:271`
  (`read(float, length)`).
- `msToFrames()`: `:298` (`write(float)`), `:325` (`writeAdding(float)`), `:353`
  (`writeAddingMultiplied(float)`), `:381` (`writeSwappedAddingMultiplied(float)`).
- Sibling conversions that already validate: the sizing paths `RingBuffer.cpp:127` and `:165`
  (`checkedFramesFromMilliseconds`, which throws). Left unchanged. Throwing is not used in the
  offset path because that path runs on the audio thread.
- In-tree users of the float overloads: `plugins/MultitapEcho/MultitapEcho.cpp:160,175`
  (`writeSwappedAddingMultiplied` / `writeAddingMultiplied` with a float `offset` built from a
  positive `stepLength`), and `:181` (`pop`, no float conversion). `m_buffer` is a runtime
  member (`plugins/MultitapEcho/MultitapEcho.h:74`) with no save/load use in
  `MultitapEcho.cpp` (grep `m_buffer` lists only lines 62, 66, 146, 160, 175, 181).

## Regression test

`tests/src/core/RecordRingBufferTest.cpp:257-326`, slot
`RingBuffer_NonFiniteMillisecondOffsetsAreDefined`. It uses the existing ring setup of
`RingBuffer_NegativeOffsetsMoveBackward` (distinct per-index frames, so each read identifies its
source position). It checks:

- `read(dst, NaN)` equals `read(dst, 0)`                      (`:277-282`, RED assertion at `:282`)
- `read(dst, +inf)` and `read(dst, FLT_MAX)` equal `read(dst, INT64_MAX)`; `read(dst, -inf)` equals `INT64_MIN`   (`:284-297`)
- `movePosition(NaN)` leaves the position at 0; `movePosition(+inf)` equals `movePosition(INT64_MAX)`   (`:299-311`)
- `write(src, NaN, 1)` lands at the current position; `write(src, +inf, 1)` lands at `UINT64_MAX % size`   (`:313-325`)

No clocks, sleeps, threads or random values are used. The inputs are fixed literals, and the
expectations are relative to other fixed inputs, so the result does not depend on the
sample rate or the period size.

## Commands and EXIT codes

All build output is in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-264.r1/logs/`. ccache could not
write to `~/.local/share/zene-bunny-fix/dsh/home/ccache-tmp-r6` (Permission denied in
`build-1-red.log`), so every build below ran with `CCACHE_DISABLE=1`. No code was changed for
this.

| Step | Command (run in `zene-qwen-new-p14-264`, tests run in `build/tests`) | EXIT | Log |
| --- | --- | --- | --- |
| RED build (test only, before fix) | `CCACHE_DISABLE=1 cmake --build build --target RecordRingBufferTest -j2` | 0 | `build-2-red.log` (first attempt `build-1-red.log`: EXIT 2, ccache) |
| RED test | `ctest -R '^RecordRingBufferTest$' --output-on-failure` | 8 | `test-1-red.log`: `RingBuffer_NonFiniteMillisecondOffsetsAreDefined` fails, `RecordRingBufferTest.cpp:282`, Actual 256, Expected 0 |
| Fix applied | edit `include/RingBuffer.h` (see above) | - | - |
| GREEN build | `CCACHE_DISABLE=1 cmake --build build --target zene RecordRingBufferTest MultitapEchoBoundsTest PluginPortsMigrationTest -j2` | 0 | `build-3-green.log` |
| GREEN family | `ctest -R '^(RecordRingBufferTest\|MultitapEchoBoundsTest\|PluginPortsMigrationTest)$' --output-on-failure` | 8 | `test-2-green.log`: RecordRingBufferTest and MultitapEchoBoundsTest pass; PluginPortsMigrationTest fails (baseline, see below) |
| Proof: save patch | `git diff -- include/RingBuffer.h > logs/fix.patch` | 0 | `fix.patch` |
| Proof: revert | `git apply -R logs/fix.patch` | 0 | - |
| Proof: build without fix | `CCACHE_DISABLE=1 cmake --build build --target zene RecordRingBufferTest MultitapEchoBoundsTest PluginPortsMigrationTest -j2` | 0 | `build-4-noFix.log` |
| Proof: RED without fix | `ctest -R '^(RecordRingBufferTest\|MultitapEchoBoundsTest\|PluginPortsMigrationTest)$' --output-on-failure` | 8 | `test-3-noFix-4b.log`: RecordRingBufferTest FAILS (`RingBuffer_NonFiniteMillisecondOffsetsAreDefined`, fuzzy compare); PluginPortsMigrationTest fails identically; MultitapEchoBoundsTest passes |
| Proof: re-apply | `git apply logs/fix.patch` | 0 | - |
| Proof: build with fix | `CCACHE_DISABLE=1 cmake --build build --target zene RecordRingBufferTest MultitapEchoBoundsTest PluginPortsMigrationTest -j2` | 0 | `build-5-fix.log` |
| Proof: GREEN with fix | `ctest -R '^(RecordRingBufferTest\|MultitapEchoBoundsTest\|PluginPortsMigrationTest)$' --output-on-failure` | 8 | `test-4-fix-4b.log`: same as the GREEN family row (only PluginPortsMigrationTest fails) |
| Totals, RecordRingBufferTest | `./RecordRingBufferTest` (in `build/tests`) | 0 | `run-5-record.log`: `Totals: 20 passed, 0 failed, 0 skipped` |
| Totals, MultitapEchoBoundsTest | `./MultitapEchoBoundsTest` (in `build/tests`) | 0 | `run-6-multitap.log`: `Totals: 4 passed, 0 failed, 0 skipped` |
| Gates (the script's own checks) | `python3 -I logs/gate_driver.py <worktree> logs/gates-2` | 0 | `gate-2-feature-check.log`: all 8 rows PASS, `RESULT: PASS` |
| Gates (script as-is) | `python3 feature_check.py <worktree>` | 1 | `gate-1-feature-check.log`: `PermissionError` creating `<worktree>-feature-check`, outside this lane's write area |

Affected family: `RecordRingBufferTest` runs 18 test functions (17 existing + 1 new); QTest's
`Totals: 20 passed` also counts `initTestCase` and `cleanupTestCase`. `MultitapEchoBoundsTest`
reports `Totals: 4 passed`. `PluginPortsMigrationTest` reports `82 passed, 1 failed, 2 skipped`
(the same with and without the fix).

## Baseline failure in PluginPortsMigrationTest (not caused by this change)

`PluginPortsMigrationTest::nonRealtimeLadspaEffectsAreInvalid` fails at
`tests/src/plugins/PluginPortsMigrationTest.cpp:1380` with "SWH Decimator test fixture was not
loaded". The fixture is the system LADSPA plugin `decimator_1202.so`, which is not in
`/usr/lib/ladspa`, `/usr/local/lib/ladspa` or `/usr/lib64/ladspa` on this machine. The same
failure, with the same three `Loc:` lines (`:1380`, `:1911`, `:5674`), appears in the run with
the fix reverted (`test-3-noFix-4b.log`), so it predates this change.

## Gate note

`feature_check.py` writes its logs to `<worktree>/../<name>-feature-check`. That directory is
outside this lane's write area, so the script stops with `PermissionError` (`gate-1-feature-check.log`).
I did not change the gate. `gate_driver.py` imports the same module, runs the same `SIMPLE` and
`RATCHET` lists and the same comparison, and writes logs to the run folder. Its output is
`gate-2-feature-check.log`, which shows all rows PASS. The file-length check passes: the test
file is 544 lines in total, but the gate counts code lines and the file is not in its over-500 list.

## Registration

- `tests/upstream-modifications.txt`: added a `cluster new-p14-264` row for the inherited
  `include/RingBuffer.h` change (the file already had a row from new-p9-389).
- `tests/src/core/RecordRingBufferTest.cpp` is product-new and already registered in
  `tests/all-sources.txt:2881` and `tests/CMakeLists.txt:629`; no new file was added.

## Behaviour changes, stated plainly

- NaN, infinite and out-of-range millisecond offsets change from undefined behaviour to the
  defined results in the verdict.
- `write(float)`, `writeAdding(float)`, `writeAddingMultiplied(float)` and
  `writeSwappedAddingMultiplied(float)` with a negative millisecond value were also undefined
  behaviour (a negative float to an unsigned count). They now write at offset 0. The header
  says write offsets "may *NOT* be negative", and no in-tree caller passes a negative value.
- Nothing else changes. No control commands, no undo, no saved data, no audio-thread allocation,
  lock, log or throw.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.** RED:
  `test-1-red.log`, EXIT 8, assertion at `RecordRingBufferTest.cpp:282`. Revert-proof: EXIT 8
  with the same test failing (`test-3-noFix-4b.log`). GREEN: `test-4-fix-4b.log` shows the
  RecordRingBufferTest and MultitapEchoBoundsTest passing; the direct run `run-5-record.log`
  has `20 passed, 0 failed` (EXIT 0).
- **The test is deterministic.** No timing, sleeps, threads or randomness. Inputs are fixed
  literals (`NaN`, `±inf`, `FLT_MAX`, `INT64_MAX/MIN`, `UINT64_MAX % size`), and the expected
  values come from the same ring read with a fixed, valid offset. The seam is the public
  `read`/`movePosition`/`write` overloads, so no test hook is needed.
- **The fix is at the cause and complete.** Both ms-to-frame helpers (`msToFrames`,
  `msToFrameOffset`) were checked, and every caller of them and of their float overloads is
  listed above (`RingBuffer.cpp:196,245,271,298,325,353,381`; `MultitapEcho.cpp:160,175`). The
  sizing constructors and `changeSize` already validate and throw, so they stay as they are.
- **No collateral damage.** For finite, in-range values the expression and cast are unchanged,
  so results are identical. The only change for negative input is the write path noted under
  Behaviour changes, which is undefined behaviour before and a contract violation. The
  `movePosition`/`read` path accepts negative offsets as before (the saturation only applies
  beyond `int64` range). `RingBuffer` is not serialized; the MultitapEcho buffer is
  runtime-only.
- **Verdict is FIXED,** so no ALREADY-FIXED or FALSE-POSITIVE evidence is needed.
- **Gates.** `gate_driver.py` (same checks, logs redirected): EXIT 0, all rows PASS.
  `feature_check.py` as-is: EXIT 1 from the log-path permission error (see Gate note).
