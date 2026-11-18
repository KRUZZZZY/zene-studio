FIXED

# cluster ci-151e338efff6843e - CI regression: build / windows-arm64

## Verdict

FIXED. The finding is REAL on this branch: at the pinned train commit
`40b4322860e9c2133f412a4b921b63b057dd710c` the hosted `build / windows-arm64` job (run
43569206372, job 132369662814) stopped in `plugins/Eq/EqSpectrumView.cpp` while building
the `eq` plug-in:

```
plugins/Eq/EqSpectrumView.cpp:272:2: error: no matching constructor for initialization of 'QList<float>'
  272 |         m_spectrumBands( MAX_BANDS, 0.f )
      |         ^                ~~~~~~~~~~~~~~
  ... note: candidate template ignored: deduced conflicting types for parameter
      'InputIterator' ('int' vs. 'float')
plugins/Eq/EqSpectrumView.cpp:296:44: error: no member named 'data' in 'QList<float>'
plugins/Eq/EqSpectrumView.cpp:315:39: error: no member named 'data' in 'QList<float>'
3 errors generated.
```

The job builds against MSYS2 clangarm64 Qt 5.15
(`.github/workflows/deps-msys2-clangarm64.txt:17-18` pins `mingw-w64-clang-aarch64-qt5-base`
and `qt5-svg`). The two API uses the file relies on are Qt 6-only:

* `QList(qsizetype size, parameter_type value)` - Qt 6's contiguous `QList`
  (`/usr/include/qt6/QtCore/qlist.h:311`). Qt 5's node-based `QList` has no such
  constructor; the two-argument one belongs to Qt 5 `QVector`
  (`/usr/include/qt5/QtCore/qvector.h:71`, `QVector(int size, const T &t)`).
* `T *data()` / `const T *constData()` - Qt 6's `QList` (`/usr/include/qt6/QtCore/qlist.h:497-499`).
  Qt 5's `QList` has no `data()` at all (the local Qt 5.15.13 header, the same generation
  the job uses, offers only iterators/`at()`), while Qt 5 `QVector` has `data()`
  (`qvector.h:134-135`).

Qt 6 made `QVector` an alias of `QList` (`/usr/include/qt6/QtCore/qcontainerfwd.h:40`), so
the Qt 6 build on this box compiled the identical lines. The failure is therefore the
product code's, not the test suite's or the runner's: the snapshot container was chosen
from the Qt 6 API surface and never built on Qt 5. It is not infrastructure.

## Cause and fix

`plugins/Eq/EqSpectrumView.h:136` (before the fix) declared the GUI's snapshot as
`QList<float> m_spectrumBands;`, introduced by `a7ab89bfe7` ("publish the EQ spectrum as a
snapshot for the GUI"). The view constructs it with
`m_spectrumBands(MAX_BANDS, 0.f)` (`EqSpectrumView.cpp:272`) and hands its contiguous
buffer to `EqAnalyser::copySpectrum(float* bands, float& energy)` twice
(`EqSpectrumView.cpp:296`, `:315`) - so the container is required to be contiguous,
sized, and value-initialised on every supported Qt.

`QVector<float>` is exactly that on both: Qt 5 `QVector` declares
`QVector(int size, const T &t)` and `T *data()`; on Qt 6 `QVector` is `QList`, the same
type the file already compiled against. The fix is one declaration (plus its include and
a comment saying why), at the cause:

```diff
 #include <QPainterPath>
+#include <QVector>
 #include <QWidget>
...
-	QList<float> m_spectrumBands;
+	QVector<float> m_spectrumBands;
```

The sibling `QList<float> m_bandHeight` (`EqSpectrumView.h:134`) is deliberately unchanged:
it only uses `append()` and `operator[]` (the loop at `EqSpectrumView.cpp:284` and
`:332-347`), both of which Qt 5's `QList` declares - no `data()` and no size+value
constructor - so it is not part of this defect and must not be churned.

Behaviour is unchanged: `QList<float>` on Qt 6 and `QVector<float>` on Qt 6 are the same
type, and on Qt 5 `QVector` is the container that provides the contiguous buffer the
analyser already assumed. Nothing on the audio thread changed (`copySpectrum()` and the
`publish()` path are untouched), no undoable action or command is involved, and no saved
data or project file format is touched - this is a member type of a GUI view, not
serialised state.

## Regression

`tests/Qt5QListApiCheck.py` (new, registered as ctest `Qt5QListApiCheck` in
`tests/CMakeLists.txt`). Two deterministic halves:

1. A source scan of `plugins/Eq` that flags a `QList`-typed name used with
   `data()`/`constData()` or constructed with two top-level arguments, with red/green
   fixtures (`QList<float>(4, 0.f)` flagged, `QVector<float>(4, 0.f)` not) run first. This
   half is red on the Qt 6 hosts here, where the defect compiles.
2. A compile probe: where `pkg-config` finds a Qt 5 tree (this box has Qt 5.15.19), every
   hand-written `plugins/Eq` translation unit from `build/compile_commands.json` is
   recompiled with `-fsyntax-only` against those Qt 5 headers, which reproduces the hosted
   job's compiler input. The probe's own control compiles a known-bad
   `QList<float>(8, 0.f).data()` fixture (must be rejected) and the known-good
   `QVector<float>` fixture (must be accepted). Without Qt 5 the probe reports itself
   skipped, never passed.

No timing, sleeps or threads: the only inputs are the checked-in sources and the recorded
compile line of each translation unit.

The file is named `Qt5QListApiCheck.py` - its ctest name - because the negative-control
harness selects the tests a branch changed by comparing each changed test file's stem with
the ctest names. The first round's proof ran `AudioAnalysisTest` alone (log then), which
passes with and without the product change and therefore proved nothing; with the rename
the harness selects both `AudioAnalysisTest` and `Qt5QListApiCheck`, and the second fails
without the fix. Re-running the harness's own `prove()` on this branch (base
`40b4322860`, `build/fix-logs/49-negctl-sim.log`) returns
`{'ok': True, 'tests': ['AudioAnalysisTest', 'Qt5QListApiCheck'], 'reverted_build_rc': 0,
'reverted_rc': 8, 'restored_rc': 0, 'tree_clean': True}` and writes
`negctl/test-reverted.log` (Qt5QListApiCheck FAILED, the three source findings plus the
Qt 5 compile errors) and `negctl/test-restored.log` (2/2 passed).

Evidence. Every command below wrote its output to a numbered file; the assigned log folder
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/ci-151e338efff6843e.r1/logs`
refuses writes in this session (`touch` inside it returns EACCES although the directory is
`drwxr-xr-x` and owned by this user - only the worktree and its `build/` are writable), so
the same files are in `build/fix-logs/` inside the worktree, named as listed here:

| step | command | exit | log |
| --- | --- | --- | --- |
| RED (fix reverted) | `python3 tests/Qt5QListApiCheck.py` | 1 | `29-red.log`, `44-ctest-red-renamed.log`, `negctl-sim/negctl/test-reverted.log` |
| GREEN (fix applied) | `python3 tests/Qt5QListApiCheck.py` | 0 | `30-green.log`, `negctl-sim/negctl/test-restored.log` |
| rebuild both targets | `cmake --build build --target zene AudioAnalysisTest` | 0 | `31-build-final.log` |
| RED ctest | `cd build/tests && ctest -R '^(AudioAnalysisTest|Qt5QListApiCheck)$' --output-on-failure` | 8 (1 of 2 failed: `Qt5QListApiCheck`) | `13-ctest-red.log`, `46-ctest-reverted.log` |
| GREEN ctest | same | 0 (2 of 2 passed) | `32-ctest-final.log`, `48-ctest-restored.log` |
| Family (final) | the six Qt 5 portability checks plus `AudioAnalysisTest` | 0 (7 of 7 passed) | `21-ctest-qt5family.log`, `51-ctest-family-final.log` |
| gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 (8 of 8 PASS) | `35-feature-check-final.log`, `50-feature-check-after-rename.log` |

The exact RED/GREEN commands with the fix patch (4b proof):

```
git diff -- plugins/Eq/EqSpectrumView.h > build/fix-logs/fix.patch   # 24 lines
git apply -R build/fix-logs/fix.patch                                # exit 0
python3 tests/Qt5QListApiCheck.py                                 # exit 1
  plugins/Eq/EqSpectrumView.cpp:296: QList has no data() in Qt 5; use QVector
  plugins/Eq/EqSpectrumView.cpp:315: QList has no data() in Qt 5; use QVector
  plugins/Eq/EqSpectrumView.cpp:272: QList(count, value) does not exist in Qt 5; use QVector
  .../EqSpectrumView.cpp:272:9: error: no matching function for call to 'QList<float>::QList(const int&, float)'
  FAIL: 4 Qt-5-incompatible QList use(s)
git apply build/fix-logs/fix.patch                                   # exit 0
python3 tests/Qt5QListApiCheck.py                                 # exit 0
  PASS: the Eq plugin uses no QList API Qt 5 lacks, and every Eq translation unit compiles against Qt 5
```

Rebuilding without the fix also leaves `AudioAnalysisTest` green and `Qt5QListApiCheck`
red (log `13-ctest-red.log`), so the new test is the one that fails, not the analyser
regression it sits beside. The A/B render of the Eq plug-in is unaffected:
`build/tests/PluginPortsMigrationTest migratedPluginsPreserveBehaviour` exit 0
(`34-migration-eq.log`), and `partc_ref_eq` rebuilt from the changed header exit 0
(`16-build-partc-eq.log`).

## Siblings checked

* `plugins/Eq/EqSpectrumView.h` is included by `EqControls.h` (thus `EqControls.cpp`),
  `EqControlsDialog.cpp` and its own `EqSpectrumView.cpp`; all were rebuilt:
  `zene`, `partc_ref_eq` (the reference module that compiles the Eq sources), and
  `AudioAnalysisTest` (which `#include`s `EqSpectrumView.cpp`). The Qt 5 probe compiles
  all six hand-written Eq translation units, not only the changed one.
* Whole-tree scan with the same rule, per file: no other `QList` in the product sources is
  used with `data()`/`constData()`, and the only two-argument `QList(...)` candidate is
  `tests/src/tracks/TakeLaneCompPlaybackTest.cpp:516`
  (`QCOMPARE(playFrom(track, 144), (QList<int>{60}))`), a comparison rather than a
  construction. `QList<float> m_bandHeight` is the only sibling in this file and stays
  legal on Qt 5 (only `append()`/`operator[]`, both Qt 5 `QList` members). The
  `QVector<float>` uses in `src/core/BrowserPeakCache.cpp` and `include/Graph.h` already
  name the portable container.

## Registration and gates

* `tests/Qt5QListApiCheck.py` registered in `tests/fork-sources.txt` (new file).
* `plugins/Eq/EqSpectrumView.h` is upstream-inherited (`tests/all-sources.txt:1175`), so its
  change is declared in `tests/upstream-modifications.txt` with the reason.
* `tests/CMakeLists.txt` registers `Qt5QListApiCheck` wherever `PYTHON3_EXECUTABLE` exists,
  next to the other Qt 5 portability checks, so the Qt 6 hosts run it too.
* `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>`: exit 0, one
  PASS/FAIL table (fork-sources, all-sources-reproduce, no-upstream-regression,
  check-namespace, no-tautology, doc-figures, complexity, file-length - all PASS, no new
  ratchet item in the changed files).

## Self-check

* The regression test FAILS with the product change reverted and PASSES with it: RED
  `python3 tests/Qt5QListApiCheck.py` exit 1 with the three source findings and the
  Qt 5 compile errors at `EqSpectrumView.cpp:272/296/315`; GREEN exit 0 after
  `git apply build/fix-logs/fix.patch` (see the table above). The ctest form, which is what
  the harness runs, is the same: `ctest -R '^(AudioAnalysisTest|Qt5QListApiCheck)$'` exit
  **8** with `plugins/Eq/EqSpectrumView.h` reverted (log `46-ctest-reverted.log`: 1 of 2
  failed, Qt5QListApiCheck) and exit **0** restored (`48-ctest-restored.log`: 2/2). The
  harness's own `prove()` on this branch returns `ok: True` (`49-negctl-sim.log`).
* The test is deterministic: no wall-clock timing, sleeps or races. It reads the
  checked-in Eq sources plus the recorded `compile_commands.json` line for each translation
  unit, retargets the Qt 6 include directories at the pkg-config Qt 5 tree, and compiles
  with `-fsyntax-only`; the only "seam" is the Qt 5 development tree itself, and its
  absence is reported as `probe: skipped`, not as a pass.
* The fix is at the cause and complete: the container is `QVector<float>` (contiguous,
  sized, value-initialised on Qt 5 and Qt 6), the include is explicit, every translation
  unit that includes the header was rebuilt, the whole Eq plug-in compiles against Qt 5,
  and the only sibling `QList` member is legal on both. No caller of
  `copySpectrum(float*, float&)` changed - its signature and the published-snapshot
  protocol are untouched.
* No collateral damage: the spectrum view paints the same buffer with the same values
  (Qt 6 `QVector` is `QList`; Qt 5 `QVector<float>` holds the same `float`s), no project
  or preset data contains this type, and no limit or check was added that could hit other
  callers. `AudioAnalysisTest` (the analyser's snapshot/FFT regressions) and
  `PluginPortsMigrationTest migratedPluginsPreserveBehaviour` (the Eq A/B render) both pass.
* Not an ALREADY-FIXED/FALSE-POSITIVE verdict: the code above is the defect and it was
  reproduced on this branch's compiler input.
* Realtime: nothing added runs on the audio thread; `publish()`, `analyze()` and
  `copySpectrum()` are untouched, and the change is a GUI member's type.
* Undo/commands: no command, undo step or reversibility class is involved.
