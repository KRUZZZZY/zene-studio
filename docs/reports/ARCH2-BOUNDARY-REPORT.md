# ARCH-2 — the `zene::api` boundary for the control registry

Board card **#672** (row 48, ARCH-2), in 0.3.0 scope by owner decision D11.
Lane branch `030/arch2-api`; base commit `f16baff79` (the 0.3.0 release tip,
7/7 CI green); worktree `projects/lmms-fl-research/zene-030/warch2`; Qt6 Linux
build configured as the release tip configures (`-DWANT_QT6=ON
-DCMAKE_BUILD_TYPE=RelWithDebInfo -DUSE_WERROR=ON -DTARGET_UARCH=official
-DUSE_COMPILE_CACHE=ON -DWANT_DEBUG_CPACK=ON -DWANT_VST3=ON -DWANT_CLAP=ON
-DWANT_VST3_TEST_INSTRUMENT=ON`, i.e. the release configuration: session view
and telemetry on, stem split off, wasmtime found).

**The deliverable is the boundary, not a directory move.** One sentence: the
control registry and its contract tables now compile in a target of their own
whose include path has no Qt widget directory and which does not link
QtWidgets, and a registered ctest (`ZeneApiBoundary`) re-measures that property
on the artifacts every suite run; the command surface is byte-identical before
and after.

## 1. What was measured before anything changed

### 1.1 The closure

The card's method defines the closure as `src/core/ControlRegistry*.cpp`, the
registration composition, the reversibility tables and `include/ControlRegistry*.h`
("derive the exact closure"). Derived by header dependency, it is **42
translation units**:

| group | files |
|---|---|
| the registry itself | `ControlRegistry.cpp`, `ControlRegistryRegistrations.cpp` (the composition), `ControlTransactions.cpp` (the transaction record's accounting), `ControlUndoCoalescing.cpp` (the rule that decides when two commands are one undo step) |
| the A16 contract table | `ControlReversibility.cpp` + 35 `ControlReversibilityTable*.cpp` |
| its vocabulary/schema helpers | `ControlSchema.cpp`, `ControlVocabulary.cpp` |

The headers in the closure are `include/ControlRegistry.h`,
`ControlRegistryGroups.h`, `ControlReversibility.h`, `ControlUndoCoalescing.h`,
`ControlVocabulary.h` (the last three are included by `ControlRegistry.h`).

### 1.2 Does anything in it reach QtWidgets? (compiler measurement)

Measured with the compiler, on the release tip's own compile commands
(`/home/kruzzzzy/.../zene-030/build/compile_commands.json`), replacing the
compile action with `-M` (the full include listing, system headers included)
and grepping for QtWidgets paths. The probe covered 179 TUs that this
configuration compiles: the 42-file closure plus the 137 `ControlCommands*.cpp`
/ `Control*Support.cpp` files:

```
$ python3 probe-widgets.py            # -M over the closure + every command/support TU
--- 5/180 reach QtWidgets
WIDGETS  ControlCommandsArrangement.cpp  .../include/TrackContentWidget.h
WIDGETS  ControlCommandsController.cpp   /usr/include/x86_64-linux-gnu/qt6/QtWidgets/qsizepolicy.h
WIDGETS  ControlCommandsSurface.cpp      /usr/include/x86_64-linux-gnu/qt6/QtWidgets/qabstractbutton.h
WIDGETS  ControlCommandsTelemetry.cpp    /usr/include/x86_64-linux-gnu/qt6/QtWidgets/qabstractscrollarea.h
WIDGETS  ControlEditSupport.cpp          .../include/TrackContentWidget.h
```

Read out: the 42-file closure is 42/42 widget-free (no Qt widget header, transitively or otherwise); of the 137 command/support TUs, 129 are widget-free, **5 reach QtWidgets** and 3 have no compile entry in this configuration (`ControlCommandsStems.cpp`, `ControlCommandsStemModel.cpp`, `ControlStemSupport.cpp` - the stem split is `WANT_STEM_SPLIT=OFF` here, so their status is unmeasured, not assumed).

The five that do reach widgets are command groups that read GUI state by design
- selection sync via `SongEditor.h` / `TrackView.h`, the controller surface's
view half, `control.surface_report`'s live menu/toolbar reflection and the
telemetry consent screen, all through `MainWindow.h`. They stay outside the
boundary (they are display-side), and the composition TU registers them from
there.

The second measurement is a compile, not a listing: every one of the 42 TUs
compiles with the QtWidgets include directory removed and `-DQT_WIDGETS_LIB`
undefined:

```
$ python3 probe-nowidget-compile.py
--- 42/42 TUs compile with NO QtWidgets include path (fsyntax-only)
```

**So the boundary is proven, not manufactured.** Nothing had to be rewritten;
what did not exist was any mechanism that *keeps* the closure widget-free, and
any target that compiles it alone. That mechanism is the deliverable below.

## 2. The change

| file | change |
|---|---|
| `src/core/CMakeLists.txt` | new `ZENE_API_SRCS` block (the 42 paths); `LIST(REMOVE_ITEM LMMS_SRCS ${ZENE_API_SRCS})` + the write-back that makes the removal effective; configure-time `build/zene-api-sources.txt` for the proof test |
| `src/CMakeLists.txt` | `ADD_LIBRARY(zene_api STATIC ...)`, `AUTOMOC OFF`, the export-macro definition, the widgets filter (include list and link list), `target_static_libraries(lmmsobjs zene_api)`, `-DZENE_API_BOUNDARY` |
| `include/zene/api/ControlApi.h` | **new**: the module's public entry point - the registry types, the contract-table types and the composition entry re-exported as `zene::api::…`, and the `#error` guard that names the defect if the boundary is ever compiled with `QT_WIDGETS_LIB` |
| `tests/zene-api-boundary.py` | **new**: the proof (six checks, each with a liveness control) |
| `tests/CMakeLists.txt` | registers `ZeneApiBoundary` |
| `tests/fork-sources.txt`, `tests/all-sources.txt` | the two new files registered in the scope manifests (additive, sorted) |

Design decisions, each for a reason that is checkable:

* **A static library, not an object library.** `lua`, `ringbuffer` and
  `luabridge` are the tree's precedent for "a set of TUs compiled by their own
  target, linked through `target_static_libraries`"; the same call on `lmmsobjs`
  propagates the boundary to the executable, the test binaries and the Part C
  reference modules with one line.
* **Nothing is compiled twice.** The boundary's TUs are removed from
  `LMMS_SRCS`, so the build does not pay for a "proof copy".
* **`AUTOMOC OFF` on the boundary.** The `Q_OBJECT` headers under `include/` are
  AUTOMOCed by `lmmsobjs` (its `include/*.h` glob is unchanged) and the boundary
  links those moc symbols from there; moc'ing them in both targets would define
  every `ControlRegistry` metaobject twice. No boundary TU declares a
  `Q_OBJECT` of its own (checked).
* **The export macro is defined.** The boundary is a split of the same logical
  library, so its TUs compile with `-Dlmmsobjs_EXPORTS` exactly as before;
  `LMMS_EXPORT` therefore still expands to default visibility on ELF and
  `dllexport` on PE, and the split moves no ABI. (On ELF it is a no-op either
  way; on the Windows jobs `__declspec(dllimport)` would have made the
  definitions uncompilable, and `LMMS_STATIC_DEFINE` would have silently
  un-exported them.)

### 2.1 Two traps the first builds measured

Both are recorded in the CMake comments, because both fail silently:

1. **`set(<var> … PARENT_SCOPE)` does not set `<var>` in the current scope.** The
   object library's source list was built by one big `set(LMMS_SRCS … PARENT_SCOPE)`,
   so a `LIST(REMOVE_ITEM LMMS_SRCS …)` written after it edits a value the
   subdirectory does not have: the boundary's files stayed in the object
   library's list. Measured - the first reconfigure listed every registry TU
   under **both** `zene_api.dir` and `lmmsobjs.dir` in `compile_commands.json`,
   i.e. compiled twice, which would have been a duplicate-symbol link failure.
   Adding a write-back while the `set()` still carried `PARENT_SCOPE` was the
   second measured failure: the local variable was *empty*, so the write-back
   emptied the object library's whole list and the build died with every moc
   symbol undefined (`lmms::RemotePlugin::processErrored`,
   `lmms::ProjectRenderer::startProcessing`, `lmms::PatternStore::play`,
   18 000+ link-error lines). The fix is the ordering now in the file:
   the big `set()` no longer carries `PARENT_SCOPE`, the removal happens on a
   real local value, and the list is handed to the parent scope once, at the end.
   Re-measured after the fix: 1944 compile commands in total, `zene_api.dir` 42,
   `lmmsobjs.dir` 520 (562 − 42), and every registry TU under exactly one target.
2. **A path-shaped regex does not filter a list of CMake targets.**
   `[Qq]t[0-9]*Widgets` matches `.../qt6/QtWidgets` but *not* `Qt6::Widgets`,
   so Qt6::Widgets stayed linked and its usage requirements put both
   `-isystem .../qt6/QtWidgets` and `-DQT_WIDGETS_LIB` straight back onto the
   boundary's compile line. Captured in the first build's
   `build/src/CMakeFiles/zene_api.dir/flags.make`. The filter is now
   `[Ww]idgets`, which matches the target name, the directory and a macOS
   framework path alike.

### 2.2 The feature-definition gap the id-set proof caught

The first *correct* build (boundary compiles, everything links, suite
untouched) still changed behaviour, and only the before/after capture saw it:
the live `control.commands_list` came back with **332 ids instead of 340** - the
eight `wasm.*` ids were gone. Cause: `LMMS_HAVE_WASM` is the one feature macro
the closure needs that does **not** reach it through the generated
`lmmsconfig.h` - it exists only as a compile definition on `lmmsobjs`
(`IF(WANT_WASM)` in `src/CMakeLists.txt`) and gates the group's registration in
`ControlRegistryRegistrations.cpp`. The boundary target, not linking `lmmsobjs`,
compiled the group out silently: every file still compiled, the whole suite
would still have been green, and the surface had lost eight commands.

Fixed by giving the boundary the same definition in the same `IF(WANT_WASM)`
block, and the fixed build was re-measured to 340 ids (§4.1). The remaining
definition/include differences between the two targets are exactly
`-DQT_WIDGETS_LIB` (intended - that is the boundary's meaning) and the
Jack/Lua-SDK macros and vendored include directories the closure provably never
uses (no closure TU or closure header mentions `USE_WEAK_JACK`,
`NO_JACK_METADATA`, `ZENE_LUA_API_VERSION_*` or `ScriptApiVersion.h`, and all 42
compile with the reduced set).

This is why the card demands an id-set equality rather than a green suite: a
green suite cannot see a command group that was compiled out of the registry.

## 3. The proof, and how CI enforces it

### 3.1 The build level (every CI job)

The boundary target's compile line, after the fix:

```
$ grep -o 'CXX_DEFINES = .*' build/src/CMakeFiles/zene_api.dir/flags.make | grep -c QT_WIDGETS_LIB
0
$ grep -o 'CXX_INCLUDES = .*' build/src/CMakeFiles/zene_api.dir/flags.make | tr ' ' '\n' | grep -i widgets
   (empty)
$ grep -o 'CXX_INCLUDES = .*' build/src/CMakeFiles/lmmsobjs.dir/flags.make | tr ' ' '\n' | grep -c -i widgets
1        # the control: the object library still sees QtWidgets, as it must
```

A widget include added to any boundary TU is therefore a **compile error** -
`#include <QWidget>` cannot resolve, and the build step of every job fails
before a test runs. This holds on all seven jobs, including the two that never
run ctest (mingw, windows-arm64).

### 3.2 The test level (`ZeneApiBoundary`, runs in the ctest step)

The build cannot see a regression that *restores* reachability without adding an
include, so `tests/zene-api-boundary.py` re-measures the property on the built
artifacts. Six checks, **each with a liveness control** (`src/gui/MainWindow.cpp`
and its object, which must show widget reachability - a check that cannot see
widgets in the control fails the test rather than passing vacuously):

1. **target membership** - every boundary source compiles in `zene_api`
   *exactly once* (object path under the target's object directory, object file
   present, no unexpected source in there);
2. **no widget include path** - no boundary compile line carries a QtWidgets
   include argument; the control's line must;
3. **headless compile probe** - the boundary's own recorded flags compile a
   probe that includes `zene/api/ControlApi.h` and the registry headers;
4. **widget-include negative controls** - the same flags must FAIL on
   `#include <QWidget>` and `#include <QApplication>`;
5. **include closure** - preprocessing every boundary TU reaches zero QtWidgets
   headers; the control reaches many. This is the check that closes the one
   residual hole in 3.1 (see §5.1): a module-qualified
   `<QtWidgets/qwidget.h>` would resolve through the Qt parent directory, and
   this check sees it;
6. **object symbols** - no boundary object has an undefined Qt widget symbol;
   the control object has hundreds. (Skipped on MSVC with the reason printed;
   skip path documented in the file.)

Portability: checks 1-4 use `-fsyntax-only` (GCC/Clang) or `/Zs` (MSVC); 5 and
6 need `-M` and `nm` and are skipped on MSVC, where the build-level gate of 3.1
is the enforcement. The test is registered in `tests/CMakeLists.txt` whenever a
`python3` is configured, and runs from `build/tests` like every other ctest.

## 4. Behaviour unchanged

### 4.1 The live command surface, before and after

Method: the repo's own harness (`tests/control_socket_harness.py`) starts the
real binary with `--control-socket` under `QT_QPA_PLATFORM=offscreen`, waits for
`engine_ready`, calls `control.commands_list`, saves the sorted id set, the
count, the group count, the payload's sha256 and the full payload, then runs
`control.quit` (the instance exits 0).

```
BEFORE  ./build/zene @ f16baff79 (worktree base build, binary b1bf266d…)
        count=340 unique=340 count_field=340 groups=53
        ids_sha256=a9c06f9a3f0c79b34144a985a3152b7344b0adf50bb092e2da968f3c662f94db
        quit ok=True exited=True code=0 in 0.2s

AFTER   ./build/zene @ the boundary build (binary 44e68230…)
        count=340 unique=340 count_field=340 groups=53
        ids_sha256=a9c06f9a3f0c79b34144a985a3152b7344b0adf50bb092e2da968f3c662f94db
        quit ok=True exited=True code=0 in 0.2s

ID SETS EQUAL: True
FULL RESULT PAYLOAD EQUAL: True     (every command object, compared sorted by key)
GROUPS EQUAL: True
```

Two *different binaries* - different sha256, different link maps - answer the
same 340 ids with the same payload, and the base capture was cross-checked
against the release tip's own binary (`zene-030/build/zene`, a separate build of
the same commit): same 340 ids, same `ids_sha256`, and the **full
`commands_list` payloads are byte-identical** when compared with each key sorted.

### 4.2 The suite

Focused first - the registry, the contract table, the undo paths:

```
$ ctest -R 'ControlRegistryTest|ReversibilityContractTest|ReversibilityUndoTest|UndoBoundsTest|ControlVerbInverseTest|ControlSurfaceReferenceTest|ControlShutdownHookTest'
1/7 ControlRegistryTest            Passed   1.44 sec
2/7 ControlSurfaceReferenceTest    Passed   1.42 sec
3/7 ControlShutdownHookTest        Passed   0.07 sec
4/7 ControlVerbInverseTest         Passed   1.41 sec
5/7 ReversibilityContractTest      Passed   1.44 sec
6/7 ReversibilityUndoTest          Passed   1.91 sec
7/7 UndoBoundsTest                 Passed   2.39 sec
100% tests passed, 0 tests failed out of 7
```

Then the whole suite, from `<build>/tests`:

```
$ cd build/tests && LD_LIBRARY_PATH=…/third_party/wasmtime/lib ctest --output-on-failure -j2
100% tests passed, 0 tests failed out of 214
Total Test time (real) = 335.59 sec
CTEST_EXIT=0
```

**214 = the release tip's 213 + the lane's one new registered test**
(`ZeneApiBoundary`, `3/214 … Passed 5.36 sec` in the same log). The count is
stated deliberately: the release tip's local count is 213, and a suite that grew
by exactly the one test this lane registers is the same suite plus the proof,
not a differently-configured one.

### 4.3 The gates

Each run as its own command, exit code taken unpiped:

| gate | command | exit |
|---|---|---|
| all-sources reproduce | `bash tests/all-sources-reproduce.sh` | 0 (`REPRODUCES: the entry list in all-sources.txt is the recipe's own output.`) |
| fork-sources (Gate 9) | `bash tests/fork-sources-gate.sh` | 0 (`662 fork-sources entry(ies), 1104 all-sources …, 0 stale`) |
| complexity (Gate 4) | `bash tests/complexity-gate.sh --check` | 0 (`PASS (check mode: no regressions…)`) |
| file length (Gate 7) | `bash tests/file-length-gate.sh --check` | 0 |
| no upstream regression (Gate 6) | `bash tests/no-upstream-regression-gate.sh` | 0 |

The full suite, `bash tests/run-all-gates.sh` (backgrounded; `LD_LIBRARY_PATH`
for the vendored libwasmtime exported first), exits **3 = PASS-WITH-SKIPS**, the
release tip's own result - one gate did not run:

```
Gate 1  unit tests (ctest):        100% tests passed, 0 tests failed out of 214
Gate 2  coverage ratchet:          SKIP (opt-in: needs --with-coverage)  <- the only skip
Gate 3  no tautological tests:     PASS
Gate 4  per-method complexity:     PASS (fork scope and tools scope)
Gate 5  mutation (RoutingGraph):   PASS: kill score 88.5% >= 80%
Gate 6  no upstream regression:    PASS (422 changed paths declared)
Gate 7  per-file length:           PASS (fork scope and tools scope)
Gate 8  token duplication:         PASS: 0.52% / 0.00% (budget 5%)
Gate 9  fork-sources registration: PASS (662 fork-NEW, 1104 inherited, 40 tooling)
Gate 10 test-source registration:  PASS
Gate 11 committed evidence/size:   PASS
Gate 12 real-time safety sweep:    PASS
RESULT: PASS-WITH-SKIPS (exit 3) — 1 of 12 gates did not run;
GATES_EXIT=3
```

The mutation sweep (Gate 5) is the one gate that writes to the tree while it
runs; it ran to completion (`git status --porcelain` empty afterwards, no
mutant left in `src/core/RoutingGraph.cpp`).

## 5. Residuals, and the next slice

### 5.1 The Qt parent directory (named, bounded, covered)

The Qt include path that Qt's own headers require is the directory *containing*
`QtCore/`, `QtGui/`, `QtXml/` - because Qt's headers include each other
module-qualified (`QtCore/qcontainertools_impl.h`, measured in `qhash.h`,
`qlist.h`, `qstringlist.h`). Removing that directory breaks the compile; keeping
it means a module-qualified `<QtWidgets/qwidget.h>` is still *resolvable* even
though the `QtWidgets` directory itself is not on the path. The unqualified form
- the only form this tree uses anywhere (2 hits of `<QtWidgets/…>` in the whole
tree, both in vendored `src/3rdparty/qt5-x11embed`) - cannot resolve. The hole
is closed by checks 5 and 6 of the ctest: a widget include that got past the
build fails there, named, with the file and the header.

### 5.2 The commands that are already widget-free but still compile in the app

The measurement in §1.2 found **129 of the 137 command/support TUs that this
configuration compiles are widget-free** (5 reach widgets, 3 are stem-split TUs
this configuration does not build). They are not moved into the boundary in
this lane: the card's closure is the registry, the brief's rule is "do not
manufacture churn", and a target three times the size would widen the
boundary's include surface (wasmtime, SDL2, lilv, the plugin SDKs) for no
property this lane proves. What that measurement *does* give the next slice is
a named, verified candidate list - the sub-rule for
`arrangement`/`controller`/`surface`/`telemetry`/`control-edit` selection sync
(a display-side group registering into the same registry) is the only design
question left in it.

### 5.3 Not verified here

No push, no merge, no CI run: the six other CI jobs (Linux arm64, macOS x86_64
and arm64, mingw, MSVC, windows-arm64) are verified by construction only, and
the Windows/MSVC-specific conclusions above (`dllexport` preservation, `/Zs`
probe path skip) are reasoned from the generated export header and the guard
paths, not executed.

## Appendix — the commands behind the numbers

```bash
# the closure's include closure and its headless compile (release tip's commands)
python3 probe-widgets.py              # -M over the closure + every command/support TU, grep QtWidgets
python3 probe-nowidget-compile.py     # 42/42 with the QtWidgets include dir removed

# the boundary target's own compile line
grep -o 'CXX_INCLUDES = .*' build/src/CMakeFiles/zene_api.dir/flags.make | tr ' ' '\n' | grep -i widgets
grep -o 'CXX_DEFINES = .*'  build/src/CMakeFiles/zene_api.dir/flags.make | tr ' ' '\n' | grep QT_WIDGETS_LIB

# target membership (one target per registry TU)
python3 -c "…"  # every registry source appears under zene_api.dir only

# the live surface, before and after
python3 capture-commands.py build/zene before.json
python3 capture-commands.py build/zene after.json

# the proof, on its own
cd build/tests && ctest -R ZeneApiBoundary -V
```
