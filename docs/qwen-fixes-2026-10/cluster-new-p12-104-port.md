# Cluster new-p12-104-port: delay LFO uninitialized state (port)

Verdict: **ALREADY-FIXED**

## Summary

This is the port of the accepted fix `2d73d6142` ("fix: initialize delay LFO state
(cluster new-p12-104)") from `bugs/qwen-new-p12-104`. That commit added default member
initializers to `plugins/Delay/Lfo.h` because at the time the `Lfo` constructor initialized
none of `m_frequency`, `m_phase`, `m_increment`.

On this line the same defect was already fixed earlier and independently, by
`4b631748e` ("fix: initialize Delay LFO state (cluster new-p9-247)"), which is an ancestor of
`HEAD` (`git merge-base --is-ancestor 4b631748e HEAD` -> exit 0). That fix initializes the
same three members, in the constructor's member-initializer list instead of in the header,
which covers the only construction path `Lfo` has. So the header-side hardening from
`2d73d6142` would be redundant here, and no product code was changed.

## file:line evidence

| Location | State on this branch |
|---|---|
| `plugins/Delay/Lfo.h:72-74` | `double m_frequency;` / `double m_phase;` / `double m_increment;` - still no in-class initializers |
| `plugins/Delay/Lfo.cpp:34-41` | `Lfo::Lfo(int samplerate) : m_frequency(0.0), m_phase(0.0), m_increment(0.0), m_twoPiOverSr(...), m_samplerate(samplerate)` - all five members initialized |
| `plugins/Delay/Lfo.h:38` | `Lfo( int samplerate );` is the only constructor, so every instance runs the initializer list above; there is no second construction path |
| `plugins/Delay/Lfo.h:46,53` (`setFrequency`) and `plugins/Delay/Lfo.cpp:46-52` (`tick`) | read `m_frequency`, `m_phase`, `m_increment` - all defined values now |
| `tests/upstream-modifications.txt:2186` | already records the `new-p9-247` change to `plugins/Delay/Lfo.cpp` |
| `tests/src/plugins/PluginPortsMigrationTest.cpp:911-928` | `lfoStartsFromInitializedState()` - the poisoned-`0xFF`-storage regression from `new-p9-247`, which asserts `std::isfinite(firstOutput)` and `firstOutput == 0.0f` after `setFrequency(2.0)` + `tick()` |

Note: the merge conflict named in the port job (`tests/src/plugins/PluginPortsMigrationTest.cpp`)
exists because the newest line carries the same poisoned-storage regression at a different
line offset; the regression survives, so nothing had to be re-added.

## Sensitivity proof (the existing regression really catches this bug)

The product fix was temporarily reversed by hand in `plugins/Delay/Lfo.cpp` (constructor back
to the pre-`new-p9-247` body that assigns only `m_samplerate` and `m_twoPiOverSr`), then
`git diff -- plugins/Delay/Lfo.cpp > logs/fix.patch` captured it.

With the reversal in place, GCC itself flags the finding at the exact call site:

```
../../tests/src/plugins/../../../plugins/Delay/Lfo.h:46:89: error:
  '*(lmms::Lfo*)<unknown>.lmms::Lfo::m_frequency' is used uninitialized [-Werror=uninitialized]
../../tests/src/plugins/../../../plugins/Delay/Lfo.h:53:36: error:
  '*(lmms::Lfo*)<unknown>.lmms::Lfo::m_phase' is used uninitialized [-Werror=uninitialized]
```

so that TU could not build with the project's `-Werror`. To get a runtime RED the single test
object was recompiled with `-Wno-error=uninitialized` added to the exact `compile_commands.json`
command line (no other flag changed, same output object), then relinked. Result:

```
FAIL!  : PluginPortsMigrationTest::lfoStartsFromInitializedState() 'std::isfinite(firstOutput)' returned FALSE. ()
Totals: 43 passed, 2 failed, 2 skipped, 0 blacklisted, 5055ms
```

Restoring the product code makes the same slot pass again
(`PASS : PluginPortsMigrationTest::lfoStartsFromInitializedState()`).

## Evidence table

| Run | Command (from the worktree root unless noted) | Exit | Result |
|---|---|---:|---|
| Discovery | `ctest -N -R 'PluginPortsMigrationTest\|Lfo\|DelayEffect'` (in `build/tests`) | 0 | 4 tests: #225 `EnvelopeAndLfoParametersTest`, #340 `LfoGraphTest`, #477 `PluginPortsMigrationTest`, #480 `DelayLfoAutomationTest` |
| Baseline build | `cmake --build build --target PluginPortsMigrationTest DelayLfoAutomationTest` | 0 | ok |
| Baseline test | `ctest -R '^PluginPortsMigrationTest$\|^DelayLfoAutomationTest$\|^EnvelopeAndLfoParametersTest$' --output-on-failure` (in `build/tests`) | 8 | `EnvelopeAndLfoParametersTest` not built yet; `PluginPortsMigrationTest` fails only `nonRealtimeLadspaEffectsAreInvalid()`; `lfoStartsFromInitializedState()` PASSES |
| Build missing target | `cmake --build build --target EnvelopeAndLfoParametersTest` | 0 | ok |
| RED build (product fix reversed) | `cmake --build build --target PluginPortsMigrationTest` | 2 | `-Werror=uninitialized` at `Lfo.h:46` and `Lfo.h:53`, inlined from `PluginPortsMigrationTest.cpp:921` |
| Blocked ctest attempt | `ctest --test-dir build/tests -R '^PluginPortsMigrationTest$' --output-on-failure` | 97 | repo guard `bunny-guard: run ctest from <lane>/build/tests`; re-run correctly below |
| RED object recompile | the `PluginPortsMigrationTest.cpp` entry of `build/compile_commands.json` re-run with ` -Wno-error=uninitialized` appended | 0 | warnings only, object written |
| RED relink | `cmake --build build --target PluginPortsMigrationTest` | 0 | links the poisoned-object binary |
| **RED** | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` (in `build/tests`) | **8** | `FAIL! lfoStartsFromInitializedState() 'std::isfinite(firstOutput)' returned FALSE` |
| Restore fix | `git apply -R .../logs/fix.patch` | 0 | product code back to `HEAD` (`git status` clean) |
| GREEN build | `cmake --build build --target PluginPortsMigrationTest DelayLfoAutomationTest EnvelopeAndLfoParametersTest` | 0 | rebuilt with the project's normal `-Werror` flags |
| **GREEN** | `ctest -R '^PluginPortsMigrationTest$\|^DelayLfoAutomationTest$\|^EnvelopeAndLfoParametersTest$' --output-on-failure` (in `build/tests`) | **8** | `lfoStartsFromInitializedState()` PASSES again; 3/3 CTest targets execute |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 1 | 6 pre-existing `REGRESSION:` entries, none in `plugins/Delay` or `tests/src/plugins` (see caveat) |
| File-length gate | `bash tests/file-length-gate.sh --check` | 1 | 14 pre-existing `REGRESSION:` entries, none in `plugins/Delay` or `tests/src/plugins` (see caveat) |
| Fork-sources gate | `bash tests/fork-sources-gate.sh` | 0 | `PASS: every tracked source in scope is registered` |

Exact invocations, all output redirected to
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-104-port.r1/logs`:

```sh
ctest -N -R 'PluginPortsMigrationTest|Lfo|DelayEffect' > LOGS/discovery-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"   # EXIT=0
cmake --build build --target PluginPortsMigrationTest DelayLfoAutomationTest > LOGS/build-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"   # EXIT=0
ctest -R '^PluginPortsMigrationTest$|^DelayLfoAutomationTest$|^EnvelopeAndLfoParametersTest$' --output-on-failure > LOGS/test-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"   # EXIT=8
cmake --build build --target EnvelopeAndLfoParametersTest > LOGS/build-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"   # EXIT=0
git diff -- plugins/Delay/Lfo.cpp > LOGS/fix.patch; rc=$?; printf 'EXIT=%s\n' "$rc"   # EXIT=0
cmake --build build --target PluginPortsMigrationTest > LOGS/build-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"   # EXIT=2
ctest --test-dir build/tests -R '^PluginPortsMigrationTest$' --output-on-failure > LOGS/test-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"   # EXIT=97, blocked by bunny-guard
python3 <replay the compile_commands.json entry for PluginPortsMigrationTest.cpp with ' -Wno-error=uninitialized' appended> > LOGS/manual-compile-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"   # EXIT=0
cmake --build build --target PluginPortsMigrationTest > LOGS/build-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"   # EXIT=0
ctest -R '^PluginPortsMigrationTest$' --output-on-failure > LOGS/test-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"   # EXIT=8  <- RED
git apply -R LOGS/fix.patch; rc=$?; printf 'EXIT=%s\n' "$rc"   # EXIT=0
cmake --build build --target PluginPortsMigrationTest DelayLfoAutomationTest EnvelopeAndLfoParametersTest > LOGS/build-5.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"   # EXIT=0
ctest -R '^PluginPortsMigrationTest$|^DelayLfoAutomationTest$|^EnvelopeAndLfoParametersTest$' --output-on-failure > LOGS/test-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"   # EXIT=8, lfo slot PASSES again
bash tests/complexity-gate.sh --check > LOGS/gate-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"   # EXIT=1
bash tests/file-length-gate.sh --check > LOGS/gate-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"   # EXIT=1
bash tests/fork-sources-gate.sh > LOGS/gate-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"   # EXIT=0
```

## Caveats (honest, not caused by this port)

- **Affected family: 3 CTest tests execute** (#225, #477, #480). The ctest exit is 8, not 0,
  because `PluginPortsMigrationTest::nonRealtimeLadspaEffectsAreInvalid()` fails with
  `(SWH Decimator test fixture was not loaded)` - no SWH LADSPA plugin is built in this build
  tree (`build/plugins/` has no `swh` directory). It fails identically in `test-1.log`, which ran
  before any edit, and touches no Lfo code.
- **Both ratchet gates exit 1 on this branch**, on the inherited `050/m0-train-5` tree with an
  otherwise clean working tree (`git status --porcelain` empty at `ded355440`). The 6 complexity
  regressions are in `tests/src/gui/XyPadPairTest.cpp`, `src/core/DawProjectReadTracks.cpp`,
  `src/core/ControlCommandsMixerTools.cpp`, `src/core/ControlCommandsMixerStrip.cpp`,
  `src/core/ProjectRevisions.cpp`, `src/gui/ControlCommandsShortcuts.cpp`; the 14 file-length
  regressions are in `src/core/*` and `tests/src/core/*`. No gate limit or baseline was touched
  and this port changes no source file, so both gates produce byte-identical output before and
  after this commit.
- The port's own header-side change (in-class initializers in `plugins/Delay/Lfo.h`) was
  deliberately **not** applied: `Lfo` has exactly one constructor and it already initializes the
  three members, so the header change would be dead code and would require a fresh
  `tests/upstream-modifications.txt` entry for no behavioural gain.

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p12-104-port.md` (this report; docs are out of the
  fork-sources gate scope, which covers `src/ include/ plugins/ tests/ tools/ modules/`)

No product or test source file was modified: the fix and its regression already exist on this
line.