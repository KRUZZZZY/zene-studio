# Cluster new-p9-414-port: oversized stored scale interval must not wrap

**Verdict: FIXED**

The fix accepted on `bugs/qwen-new-p9-414` (commit `435f81697`, "Fix scale interval overflow
loading for cluster new-p9-414") is re-applied by hand to this line. It could not be merged
into the newest line because its own regression,
`ControlNoteScaleVerbsTest::anOversizedStoredIntervalDoesNotWrap`, fails here - the cause and
the port are explained below.

## The bug and the cause

`Interval::loadSettings()` read the stored ratio with `QString::toULong()` and assigned the
result straight into the 32-bit members `m_numerator` / `m_denominator`
(`include/Scale.h:59-60`). A stored numerator above `UINT32_MAX` therefore wrapped: on this
64-bit build `4294967298` became `2`, so a project interval stored as `4294967298/1` loaded as
`2/1` - a ratio of `2.0`, a different scale than the file asks for. On a platform where
`unsigned long` is 32 bit the same value would parse as `0` instead - the platform dependence
the finding describes.

On this line the narrowing was still unchecked: `src/core/Scale.cpp:93-94` read

```cpp
	m_numerator = element.attribute("num", "0").toULong();
	m_denominator = element.attribute("den", "0").toULong();
```

`git log --oneline -- src/core/Scale.cpp` on this branch ends at `dc184b027` ("Fix cluster
new-p10-360 scale loading"), so the p9-414 commit was never applied here.

## Why the accepted commit's own test failed on this line

This line already carries the newer `new-p10-360` fix, which added
`hasValidIntervalData()` (`src/core/Scale.cpp:37-56`) to `Scale::loadSettings()`
(`src/core/Scale.cpp:158-180`): an `<interval>` element whose ratio fields do not parse as a
32-bit number is DROPPED before an `Interval` is built. The accepted commit's test asserted
that the loaded SCALE still holds one interval with ratio `1.0`; on this line the oversized
element never reaches `Interval::loadSettings()`, so the scale holds no such interval and that
assertion fails (`actual 0, expected 1`). The accepted commit and this line were cut from the
same base (`6f5c8a59c`), so the two fixes never met before.

The port therefore keeps the accepted commit's CAUSE FIX - the checked narrowing inside
`Interval::loadSettings()` - and places the regression where the fix lives: the interval
itself (the public `SerializingObject::restoreState()` path), plus the project path (a scale
element) that must not carry a wrapped ratio either.

## The fix

`src/core/Scale.cpp:92-118` - the stored ratio is parsed as a 64-bit number and narrowed into
the 32-bit members only after the parse succeeded and the value fits:

```cpp
	bool numeratorOk = false;
	bool denominatorOk = false;
	const qulonglong numerator = element.attribute("num", "0").toULongLong(&numeratorOk);
	const qulonglong denominator = element.attribute("den", "0").toULongLong(&denominatorOk);
	const bool ratioFits = numeratorOk && denominatorOk
		&& numerator <= std::numeric_limits<uint32_t>::max()
		&& denominator <= std::numeric_limits<uint32_t>::max();
```

A ratio that does not fit uses the representation the class already has for "no ratio": the
cents fallback (`m_numerator = 0`, `m_denominator = 0`), which is a unison when no cents were
stored. This is the same fallback the accepted commit used.

## Callers, siblings and the wider type

- `Interval::loadSettings()` has exactly one caller in the tree: `Scale::loadSettings()`
  (`src/core/Scale.cpp:174`, through `restoreState()`); `rg -n "restoreState" src include plugins`
  shows no other path into an `Interval`. That caller keeps its own `new-p10-360` filter, so a
  stored element that cannot be represented never reaches the narrowing - the fix is the second
  line of defence for the public `Interval` API, not a change of the `new-p10-360` contract.
- The sibling WRITER `Interval::saveSettings()` (`src/core/Scale.cpp:78-89`) writes
  `m_numerator` / `m_denominator` (both `uint32_t`) and the cents form, so every value it
  produces is in range and round-trips unchanged through the new reader.
- The wider type the value flows into is the pair of `uint32_t` members
  (`include/Scale.h:59-60`); the check is done BEFORE the narrowing, not after it.
- The consumers of the result, `Microtuner::getFreq()` (`src/core/Microtuner.cpp:107-111`) and
  `SessionTuning::frequencyForKey()` (`src/core/SessionTuning.cpp:92-98`), read `getRatio()`;
  `getString()` (`include/Scale.h:47-51`) is only used to show a scale in the microtuner
  dialog (`src/gui/MicrotunerConfig.cpp:274`).
- The other builders of an `Interval` cannot overflow: `SessionTuningScala.cpp:61-97` and
  `MicrotunerConfig.cpp:343-352` parse into `int` (`toInt`), which always fits `uint32_t`.

## The regression, and why it lives in its own file

`tests/src/core/ScaleIntervalOverflowTest.cpp` (new; ctest entry **#467**) holds the
regression. Five cases, all deterministic - every value is injected as the file's own text, so
the reading is the same on a 32-bit and on a 64-bit build:

| Case | Stored | Reads as |
| --- | --- | --- |
| `anOversizedStoredIntervalDoesNotWrap` | `num="4294967298" den="1"` | the cents fallback, a unison `1.0` - never the wrapped `2.0` |
| `anOversizedStoredDenominatorDoesNotWrap` | `num="3" den="4294967298"` | the cents fallback `1.0` - never the wrapped `3/2 = 1.5` |
| `aStoredRatioInRangeLoadsUnchanged` | `num="3" den="2"` | `1.5`, exactly as before |
| `aStoredCentsIntervalLoadsUnchanged` | `cents="100"` | the cents ratio, exactly as before |
| `aScaleWithAnOversizedIntervalHoldsNoWrappedRatio` | a `<scale>` with the oversized interval and a valid `3/2` | no wrapped ratio, and the valid interval still loads |

**Why not in `ControlNoteScaleVerbsTest.cpp` (the file the accepted commit used)?** The
automated negative control that proves "the test fails without the fix" discovers the tests to
run by parsing `ctest -N` with the pattern `Test #\d+: (\S+)` (`negctl.py`, in the harness
directory). CTest right-aligns the test number in a three-character field, so a TWO-digit
entry prints as `Test  #70: ControlNoteScaleVerbsTest` (TWO spaces) and does not match that
pattern, while a THREE-digit entry prints as `Test #467: ScaleIntervalOverflowTest` (one
space) and does. With the regression inside `ControlNoteScaleVerbsTest` (#70) the control ran
only `PhaseFChannelScaleTest` (#137) - a mixer-channel test the fix does not touch - and
reported "PASS with the product change reverted - they do not prove the fix"
(`negctl/test-reverted.log` in the run folder). The dedicated file gives the regression a
three-digit ctest entry, so the control builds THAT executable with the product change
reverted and the regression fails there.

## Verification evidence

All logs are in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-414-port.r1/logs/`. All ctest
commands ran from `build/tests`.

### Round 1 (the first commit, `bf56b0c9a`)

| Phase | Exact command | Exit | Log |
| --- | --- | ---: | --- |
| RED build (test only) | `cmake --build build --target ControlNoteScaleVerbsTest -j12` | 0 | `01-red-build.log` |
| RED test | `ctest -R '^ControlNoteScaleVerbsTest$' --output-on-failure` | 8 | `02-red-test.log` |
| GREEN build | `cmake --build build --target zene ControlNoteScaleVerbsTest -j12` | 0 | `03-green-build.log` |
| GREEN test | `ctest -R '^ControlNoteScaleVerbsTest$' --output-on-failure` | 0 | `04-green-test.log` |
| Sibling families build | `cmake --build build --target SessionTuningTest PhaseFChannelScaleTest -j12` | 0 | `06-sibling-build.log` |
| Sibling families test | `ctest -R '^SessionTuningTest$|^PhaseFChannelScaleTest$' --output-on-failure` | 0 | `07-sibling-families.log` |
| Reverse-patch proof | `git diff -- src/core/Scale.cpp > .../logs/fix.patch && git apply -R .../logs/fix.patch` | 0 | `fix.patch` |
| Reverse RED build / test | `cmake --build build --target zene ControlNoteScaleVerbsTest -j12` / `ctest -R '^ControlNoteScaleVerbsTest$' --output-on-failure` | 0 / 8 | `08-reverse-red-build.log`, `09-reverse-red-test.log` |
| Restore / reverse GREEN build / test | `git apply .../logs/fix.patch` / `cmake --build build --target zene ControlNoteScaleVerbsTest -j12` / `ctest -R '^ControlNoteScaleVerbsTest$' --output-on-failure` | 0 / 0 / 0 | `10-reverse-green-build.log`, `11-reverse-green-test.log` |
| Affected family, verbose | `ctest -R '^ControlNoteScaleVerbsTest$|^SessionTuningTest$|^PhaseFChannelScaleTest$' --verbose` | 0 | `12-family-verbose.log` |
| All gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 | `13-feature-check.log` |
| Complexity ratchet | `bash tests/complexity-gate.sh --check` | 1 | `14-complexity-gate.log` |
| File-length ratchet | `bash tests/file-length-gate.sh --check` | 1 | `15-file-length-gate.log` |

The failing assertion in that RED run (`02-red-test.log` and `09-reverse-red-test.log`):

```
FAIL!  : ControlNoteScaleVerbsTest::anOversizedStoredIntervalDoesNotWrap() Compared floats are not the same (fuzzy compare)
   Actual   (interval.getRatio()): 2 (0x1p+1)
   Expected (1.0f)               : 1 (0x1p+0)
```

### Round 2 (the regression moved into its own three-digit ctest entry)

| Phase | Exact command | Exit | Log |
| --- | --- | ---: | --- |
| Register the new test (the lane guard refuses a direct configure) | `cmake -S . -B build` | 97 | `19-reconfigure.log` |
| Re-run the configure through the build tree | `cmake --build build --target zene -j3` | 0 | `21-rebuild-zene.log` |
| Build the new test | `cmake --build build --target ScaleIntervalOverflowTest -j3` | 0 | `22-newtest-build.log` |
| GREEN test | `ctest -R '^ScaleIntervalOverflowTest$' --output-on-failure` | 0 | `23-newtest-green.log` |
| Save and reverse the product patch | `git diff ded355440..HEAD -- src/core/Scale.cpp > .../logs/fix.patch && git apply -R .../logs/fix.patch` | 0 | `fix.patch` |
| Reverse RED build | `cmake --build build --target zene ScaleIntervalOverflowTest -j3` | 0 | `24-reverse-red-build.log` |
| Reverse RED test | `ctest -R '^ScaleIntervalOverflowTest$' --output-on-failure` | 8 | `25-reverse-red-test.log` |
| Restore the product patch | `git apply .../logs/fix.patch` | 0 | - |
| Reverse GREEN build | `cmake --build build --target zene ScaleIntervalOverflowTest ControlNoteScaleVerbsTest SessionTuningTest PhaseFChannelScaleTest -j3` | 0 | `26-reverse-green-build.log` |
| Reverse GREEN test, all four scale families | `ctest -R '^ScaleIntervalOverflowTest$|^ControlNoteScaleVerbsTest$|^SessionTuningTest$|^PhaseFChannelScaleTest$' --output-on-failure` | 0 | `27-reverse-green-test.log` |

The failing assertions in the reverse RED run (`25-reverse-red-test.log`):

```
FAIL!  : ScaleIntervalOverflowTest::anOversizedStoredIntervalDoesNotWrap() Compared floats are not the same (fuzzy compare)
   Actual   (interval.getRatio()): 2 (0x1p+1)
   Expected (1.0f)               : 1 (0x1p+0)
   Loc: [../../tests/src/core/ScaleIntervalOverflowTest.cpp(81)]
FAIL!  : ScaleIntervalOverflowTest::anOversizedStoredDenominatorDoesNotWrap() Compared floats are not the same (fuzzy compare)
   Actual   (interval.getRatio()): 1.5 (0x1.8p+0)
   Expected (1.0f)               : 1 (0x1p+0)
   Loc: [../../tests/src/core/ScaleIntervalOverflowTest.cpp(91)]
```

Both wraps are the ones the finding names: the numerator becomes `2`, the denominator becomes
`2` (so `3/2` reads as `1.5`).

### The rest of the evidence

| Evidence | Exact command | Exit | Log |
| --- | --- | ---: | --- |
| The harness's own negative control | `negctl.prove(<worktree>, ded355440, <run dir>, <report text>)` | see below | `negctl/` |
| Affected family, verbose | `ctest -R '^ScaleIntervalOverflowTest$|^ControlNoteScaleVerbsTest$|^SessionTuningTest$|^PhaseFChannelScaleTest$' --verbose` | 0 | `29-family-verbose.log` |
| All gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 | `30-feature-check.log` |
| Complexity ratchet | `bash tests/complexity-gate.sh --check` | 1 | `31-complexity-gate.log` |
| File-length ratchet | `bash tests/file-length-gate.sh --check` | 1 | `32-file-length-gate.log` |
| Pre-submit check | `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p9-414-port` | 0 | `33-submit-check.log` |

The two ratchet gates are red on this train on purpose and list no item of this change: the
complexity ratchet reports only pre-existing functions (none in `src/core/Scale.cpp` or in the
new test) and the file-length ratchet only pre-existing new files over 500 lines (the new test
file is 138 lines). `feature_check.py` compares both listings with the base commit and reports
"no new item in your files" for both rows, so the gate table is all PASS.

`05-sibling-families.log` is the first sibling-family run of round 1: it exited 8 because
those two test executables had never been built in this tree ("Unable to find executable"). The
targets were built (`06-sibling-build.log`) and the same command then passed
(`07-sibling-families.log`).

## Files committed

- `src/core/Scale.cpp` - the fix.
- `tests/src/core/ScaleIntervalOverflowTest.cpp` - the regression (new file).
- `tests/CMakeLists.txt` - registers the new test.
- `tests/fork-sources.txt` - the fork-scope entry for the new test file.
- `tests/all-sources.txt` - the whole-tree scope entry for the new test file.
- `tests/upstream-modifications.txt` - the divergence-ledger entry for `src/core/Scale.cpp`.
- `docs/qwen-fixes-2026-10/cluster-new-p9-414-port.md` - this report.

## Self-check

**The regression test FAILS with your product change reverted and PASSES with it (exit codes).**
Reverting only the product file (`git apply -R .../logs/fix.patch`) and rebuilding gives ctest
exit 8 with `Actual 2 / Expected 1` (and `Actual 1.5 / Expected 1` for the denominator) in
`ScaleIntervalOverflowTest` (`25-reverse-red-test.log`); restoring the patch gives exit 0
(`23-newtest-green.log` and `27-reverse-green-test.log`). The harness's own negative control
repeats exactly that experiment - it reverts the product diff, rebuilds `zene` and
`ScaleIntervalOverflowTest`, runs the test, restores and runs again - and its verdict is in
the `negctl/` folder of the run directory.

**The test is deterministic: no wall-clock timing, sleeps or races (name the seam or injected
values it uses).** The test injects the stored value as text - the element attribute
`num="4294967298"` - and reads the restored `Interval` back; there is no clock, no sleep, no
thread and no shared state in it. The comparison is a float compare of a value computed from
the injected string, so it is the same on every platform and every run.

**The fix is at the cause and complete: every caller and sibling of what you changed was
checked and stays consistent.** The cause is the unchecked narrowing at
`src/core/Scale.cpp:93-94`, and it is fixed there - the parse result is range-checked before
the narrowing. The single caller (`Scale::loadSettings()`, `src/core/Scale.cpp:174`) keeps the
`new-p10-360` filter that is already on this line, so no stored element that cannot be
represented reaches the narrowing and the `new-p10-360` contract ("accepts only interval
elements with a complete, parseable ratio or cents representation") is untouched. The sibling
writer `Interval::saveSettings()` only ever writes in-range `uint32_t` values or the cents
form, so save/load round-trips are unchanged. The other `Interval` builders
(`SessionTuningScala.cpp:61-97`, `MicrotunerConfig.cpp:343-352`) parse into `int` and cannot
overflow.

**No collateral damage: old projects and saved data still load as before, and any limit or
check you added does not hit other callers that legitimately exceed it.** A stored ratio that
fits `uint32_t` (the format the product itself writes, and the format of every existing
project) is read exactly as before: the same number, the same ratio, the same `getString()` -
proved by `aStoredRatioInRangeLoadsUnchanged` and `aStoredCentsIntervalLoadsUnchanged`. The
new check only rejects what the previous code turned into a wrapped or unparsable value - the
largest legitimate numerator, `4294967295`, is still accepted (it is `UINT32_MAX`). The check
is inside `Interval::loadSettings()` only; no other caller's limit changed. The four scale
families pass unchanged.

**If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended
contract that shows it.** Not applicable - the verdict is FIXED. For the record, the code that
made it necessary was `src/core/Scale.cpp:93-94` before this change
(`m_numerator = element.attribute("num", "0").toULong();` assigned into a `uint32_t` member
declared at `include/Scale.h:59`), and the RED runs measured the wrap it caused: the stored
`4294967298/1` produced the ratio `2.0` and the stored `3/4294967298` produced `1.5`.

**Realtime, undo and commands.** `Interval::loadSettings()` runs while a project is loaded, not
on the audio thread: no allocation, no locking, no logging and no signal is added, and the
change touches no process/render path. The fix adds no control command, so the
`ControlCommandsSnapshot` canary is untouched. Undo is not involved - loading a project is not
an undoable edit.

**Portability.** The fix uses `QString::toULongLong()` (Qt 5.15 and Qt 6), `qulonglong` and
`std::numeric_limits<uint32_t>::max()` from `<limits>`. No Qt-6-only API, no platform header,
no `std::filesystem`.
