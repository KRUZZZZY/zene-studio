# Cluster new-p10-158-port: FreeBoy decoded shape buffer leak

**Verdict: FIXED** (ported by hand from `a843952b3` on `bugs/qwen-new-p10-158`; the newest line
still carried the leak, so this is a re-application, not ALREADY-FIXED).

## The bug on this branch

`plugins/FreeBoy/FreeBoy.cpp:280-283` (pre-fix, HEAD `ded355440`):

```cpp
int size = 0;
char * dst = 0;
base64::decode( _this.attribute( "sampleShape"), &dst, &size );
m_graphModel.setSamples( (float*) dst );
```

`lmms::base64::decode` allocates its output with `new T[n]` at `include/base64.h:44-56` and
hands the raw pointer back to the caller. `graphModel::setSamples` copies
`length()` floats into its own `std::vector` (`src/gui/widgets/Graph.cpp:524-529`), so the
decoded buffer is dead the moment the call returns - and nothing ever released it. Every
project load, instrument reload or settings restore of a FreeBoy track leaked one
`sizeof(sampleShape)` buffer.

## The fix

`plugins/FreeBoy/FreeBoy.cpp:280-284`: the decoded buffer is owned by
`std::unique_ptr<char[]>` for the whole of `loadSettings`, so it is released when the function
returns, after `setSamples` has copied. `<memory>` was already included at line 31.
No other behaviour changes: the same bytes are decoded, copied and the same samples reach the
graph model. Nothing on an audio thread is touched.

## The regression

`tests/src/plugins/PluginPortsMigrationTest.cpp`:

- `array_allocation_probe` (lines 105-175): a thread-local window that records every
  `operator new[]`/`operator delete[]` of one chosen byte size. It hooks the array forms only,
  so scalar `new` traffic in Qt and the plugin cannot be confused with the decoded buffer.
- `PluginPortsMigrationTest::freeBoyShapeBufferIsReleased()` (lines 901-943): loads the
  migrated FreeBoy module, restores a 32-float `sampleShape`, and asserts that at least one
  array allocation of that size happened and that none of them is still outstanding, then
  round-trips `sampleShape` through `saveState`.

There is no CTest test named `FreeBoy` in this build (`ctest -N -R FreeBoy` lists none; the
only affected family is `PluginPortsMigrationTest`, i.e. 1 CTest test / 48 Qt slots).

## Evidence

| Stage | Exact command | Exit | Observed |
|---|---|---:|---|
| RED (production code unfixed) | `cmake --build build --target PluginPortsMigrationTest > '.../logs/build-1.log' 2>&1` then `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > '.../logs/test-1.log' 2>&1` (from `build/tests`) | 8 | `FAIL! ... freeBoyShapeBufferIsReleased() Compared values are not the same / Actual (report.outstanding): 1 / Expected 0` at `PluginPortsMigrationTest.cpp(937)`; Qt totals `44 passed, 2 failed, 2 skipped` (second failure = pre-existing missing LADSPA fixture, see below) |
| GREEN (fix applied) | `cmake --build build --target zene PluginPortsMigrationTest > '.../logs/build-2.log' 2>&1` then `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > '.../logs/test-2.log' 2>&1` | build 0, ctest 8 | `PASS! freeBoyShapeBufferIsReleased()`; the run still failed only on the pre-existing `nonRealtimeLadspaEffectsAreInvalid` fixture failure described below |
| GREEN, whole family | `cmake --build build --target zene PluginPortsMigrationTest > '.../logs/build-5.log' 2>&1` then `ctest -V -R '^PluginPortsMigrationTest$' > '.../logs/test-5.log' 2>&1` | 0 | 1/1 CTest test passed; Qt totals `46 passed, 0 failed, 2 skipped` |
| Reverse patch (proof the test catches the bug) | `git diff -- plugins/FreeBoy/FreeBoy.cpp > '.../logs/fix.patch' && git apply -R '.../logs/fix.patch'` (`EXIT=0`), `cmake --build build --target zene PluginPortsMigrationTest > '.../logs/build-4.log' 2>&1` (`EXIT=0`), `ctest -R '^PluginPortsMigrationTest$' --output-on-failure > '.../logs/test-4.log' 2>&1` | 8 | `FAIL! ... freeBoyShapeBufferIsReleased() ... Actual (report.outstanding): 1` |
| Restored patch | `git apply '.../logs/fix.patch'` (`EXIT=0`), `cmake --build build --target zene PluginPortsMigrationTest > '.../logs/build-5.log' 2>&1`, `ctest -V -R '^PluginPortsMigrationTest$' > '.../logs/test-5.log' 2>&1` | 0 | `PASS! freeBoyShapeBufferIsReleased()`, 46/46 slots green |

`git apply` / `git apply -R` both exited 0.

### Gates

| Gate | Command | Exit |
|---|---|---:|
| Complexity | `bash tests/complexity-gate.sh --check > '.../logs/gate-1.log' 2>&1` | 1 |
| File length | `bash tests/file-length-gate.sh --check > '.../logs/gate-2.log' 2>&1` | 1 |

Both gates **already fail on this branch without my change**: the same two commands were re-run
on a pristine tree (`.../logs/gate-3-baseline.log`, `.../logs/gate-4-baseline.log`) after
`git checkout -- plugins/FreeBoy/FreeBoy.cpp tests/src/plugins/PluginPortsMigrationTest.cpp
tests/upstream-modifications.txt`, and both exited 1 there as well. `diff` of the `REGRESSION`
lines between the with-change and pristine logs is empty for both gates, and none of the listed
regressions is `plugins/FreeBoy/FreeBoy.cpp` or
`tests/src/plugins/PluginPortsMigrationTest.cpp`. The failures are pre-existing ratchet drift on
the newest line (`smfinterchangereader.cpp`, `TakeLane.cpp`, `CollabTransportTest.cpp`,
`ControlCommandsMixerTools.cpp`, ...). No gate limit or baseline was touched, because the
drift is not this cluster's to re-anchor.

### Note on `nonRealtimeLadspaEffectsAreInvalid`

The first family runs failed on that slot too: `build/tests/ladspa-channel-fixture/` was empty
(`decimator_1202.so` had never been linked in this build tree), so the test's own fixture
precondition was missing - unrelated to FreeBoy and present in the RED run as well. Building
the fixture target (`cmake --build build --target decimator_1202 > '.../logs/build-3.log'`,
`EXIT=0`) made the family fully green; no source was changed for it.

## Files

- `plugins/FreeBoy/FreeBoy.cpp` - the fix (registered in `tests/upstream-modifications.txt`)
- `tests/src/plugins/PluginPortsMigrationTest.cpp` - the regression (registered in
  `tests/upstream-modifications.txt`)
- `tests/upstream-modifications.txt` - registration entries
- `docs/qwen-fixes-2026-10/cluster-new-p10-158-port.md` - this report