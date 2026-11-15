FIXED

# cluster ci-aa1160348d9be203 - CI regression: build / linux-arm64

## Verdict

FIXED. The finding is REAL on this branch: at the pinned train commit
`c20ef591b571e3c3dc7b108107d166497f1b34be` (train `050/m0-train-31`) the hosted
`build / linux-arm64` job (run 43611324689, job 132496796804) stopped at 55 % while
building `ImportTrackIoTest`:

```
tests/src/core/ImportTrackIoTest.cpp: In member function 'void
    ImportTrackIoTest::anUnparsableDocumentDoesNotShiftTheOthers()':
tests/src/core/ImportTrackIoTest.cpp:344:71: error: ambiguous overload for
    'operator=' (operand types are 'QStringList' and '<brace-enclosed initializer list>')
  344 |                 plan.documents = {UnparsableTrack, trackDocument(Lead)};
      |                                                                       ^
    note: candidate: 'QStringList& QStringList::operator=(const QList<QString>&)'
      /usr/include/aarch64-linux-gnu/qt5/QtCore/qstringlist.h:124:18
    note: candidate: 'QStringList& QStringList::operator=(QList<QString>&&)'
      /usr/include/aarch64-linux-gnu/qt5/QtCore/qstringlist.h:126:18
    note: candidate: 'QStringList& QStringList::operator=(const QStringList&)'
    note: candidate: 'QStringList& QStringList::operator=(QStringList&&)'
gmake[2]: *** [tests/CMakeFiles/ImportTrackIoTest.dir/build.make:99:
    tests/CMakeFiles/ImportTrackIoTest.dir/src/core/ImportTrackIoTest.cpp.o] Error 1
```

The job is a Qt 5 build: it installs `.github/workflows/deps-ubuntu-24.04-gcc.txt:47-48`
(`qtbase5-dev`, `qtbase5-dev-tools`) and does not pass `-DWANT_QT6=ON`
(`.github/workflows/build.yml:307-324`), while `CMakeLists.txt:142` declares
`option(WANT_QT6 ... OFF)`. On Qt 5, `QStringList` is its own class
(`qstringlist.h:111`) that declares `operator=(const QList<QString>&)` and
`operator=(QList<QString>&&)` beside the implicit copy/move assignment, so a bare braced
list is equally convertible to `QList<QString>` and to `QStringList` and the call is
ambiguous. Qt 6 aliases `QStringList` to `QList<QString>`
(`/usr/include/qt6/QtCore/qcontainerfwd.h:41`), whose initializer-list assignment is a
better match, so every Qt 6 host here compiled the identical line.

This is therefore a defect in a test translation unit, not in the product and not in the
runner: the line was written against the Qt 6 type surface and never built with Qt 5.
It was reproduced locally against Qt 5.15.19 headers, with the hosted job's exact compiler
message (log `repro-qt5-1.log`, exit 1).

## Cause and fix

`tests/src/core/ImportTrackIoTest.cpp:344`, in
`anUnparsableDocumentDoesNotShiftTheOthers()`, assigned a bare braced list to
`TrackPlan::documents`:

```diff
-		plan.documents = {UnparsableTrack, trackDocument(Lead)};
+		plan.documents = QStringList{UnparsableTrack, trackDocument(Lead)};
```

`TrackPlan::documents` is declared `QStringList documents;` at
`include/ControlProjectTransplant.h:76`. Naming the destination type in the braced list
removes the ambiguity on Qt 5 and changes nothing on Qt 6: both construct a
`QStringList` holding the same two elements in the same order, so the plan given to
`control::transplant::buildAsOneStep()` is identical. The two sibling uses in the same
test (`plan.documents.append(...)` at `:309` and `:326`) are not assignments and are
unaffected. `plan.need = {0, 1}` at `:345` assigns a `std::vector<int>`, not a
`QStringList`, and stays legal.

There is no product-code hunk: the whole defect is the test source's spelling. That is
why the revert proof in step 4b below reverses `tests/src/core/ImportTrackIoTest.cpp`
rather than a `src/` file; it is still the exact translation unit the hosted build
rejected.

## Regression

`tests/Qt5QStringListAssignCheck.py` (new, registered as ctest
`Qt5QStringListAssignCheck` - test #961 in this build - in `tests/CMakeLists.txt` beside
the other Qt 5 portability checks). Four deterministic parts:

1. **Whole-tree source scan.** Every first-party C/C++ file (`.h .hpp .hxx .cpp .cc .cxx`,
   excluding the vendored trees named by the `tests/all-sources.txt` recipe) is read with
   comments and string/char literals blanked. A class index records every member declared
   `QStringList`, and each assignment `name = {` is resolved to the nearest prior
   declaration of that name. It is flagged when the assignment is to a `QStringList`
   member of the receiver's class or to a `QStringList` local. A declaration
   (`QStringList names = {...}`) is legal on Qt 5 and is not flagged, so the check
   reports exactly the ambiguous assignment form.
2. **Red/green scan fixtures, run on every invocation.** A fixture with a member
   assignment and a local assignment must produce two findings; a fixture using
   `QStringList{...}` and a declaration must produce none. This keeps the scanner's
   premise checked on the Qt 6 hosts, where the real defect compiles.
3. **Qt 5 premise probe.** Where `pkg-config` finds `Qt5Core/Qt5Gui/Qt5Widgets/Qt5Xml/Qt5Test`
   (this box: 5.15.19), a known-bad bare-assignment fixture must be rejected with
   "ambiguous" and a known-good explicit `QStringList{...}` fixture must compile. With no
   Qt 5 tree the probe reports itself skipped, never passed.
4. **Translation-unit confirmation.** Each scan finding is looked up in
   `build/compile_commands.json`; its recorded command line is recompiled against the
   pkg-config Qt 5 flags (Qt 6 include paths removed, the unit's included `.moc`
   regenerated with Qt 5 `moc`), with `-fsyntax-only`. A unit that compiles clean is a
   scan false positive and is dropped; only `ambiguous ... operator=` counts as confirmed.
   Without `compile_commands.json` or Qt 5 the finding is still reported (as
   unconfirmed), so the scan half never silently disappears.

No timing, sleeps or threads: the inputs are the checked-in sources, the recorded compile
command and a fixed Qt 5 development tree, and the absence of the latter is reported as
skipped, not as a pass.

The file is named after its ctest because the negative-control harness selects tests from
changed test-file stems.

## Evidence

Every command below wrote its output to a numbered file under
`$LOGS=/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/ci-aa1160348d9be203.r2/logs`.

| step | command | exit | log |
| --- | --- | --- | --- |
| hosted compiler input, original TU, Qt 5.15.19 | `c++ <recorded flags, Qt 5 includes> -fsyntax-only tests/src/core/ImportTrackIoTest.cpp` (exact command in `$LOGS/scratch/cmd.txt`) | 1 (ambiguous `operator=`, 4 candidates) | `repro-qt5-1.log` |
| same command, fixed TU | same | 0 | `repro-qt5-2-fixed.log` |
| scanner self-test | `python3 tests/Qt5QStringListAssignCheck.py --self-test` | 0 (fixtures 2/0, Qt 5 probe good/bad) | `selftest-4.log` |
| RED ctest, fix reverted | `ctest -R '^Qt5QStringListAssignCheck$' --output-on-failure` | 8 (1 finding, 1 Qt 5-confirmed) | `test-reverted-red-2.log`, `test-red-1.log` |
| GREEN ctest, fix applied | same | 0 | `test-restored-green-2.log`, `test-green-1.log` |
| rebuild affected target | `cmake --build build --target ImportTrackIoTest -j2` | 0 | `build-final-1.log` |
| family (final) | `ctest -R 'Qt5\|^ImportTrackIoTest$' --output-on-failure` | 0 (8/8 passed) | `test-family-2.log` |
| Qt 5 family (earlier run) | `ctest -R Qt5 --output-on-failure` | 0 (7/7 passed) | `test-family-qt5.log` |
| gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 (8/8 PASS) | `feature-check-2.log` |

The exact RED/GREEN commands with the step-4b patch:

```
git diff -- tests/src/core/ImportTrackIoTest.cpp > $LOGS/fix.patch   # 13 lines, test hunk only
git apply -R $LOGS/fix.patch                                         # exit 0
cd build/tests && ctest -R '^Qt5QStringListAssignCheck$' --output-on-failure
  tests/src/core/ImportTrackIoTest.cpp:344: bare braced list assigned to QStringList (TrackPlan)
  Qt5QStringListAssignCheck: 1 finding(s), 1 confirmed by a Qt 5 compile   # ctest exit 8
git apply $LOGS/fix.patch                                            # exit 0
cmake --build build --target ImportTrackIoTest -j2                   # exit 0
cd build/tests && ctest -R '^Qt5QStringListAssignCheck$' --output-on-failure
  1/1 Test #961: Qt5QStringListAssignCheck ... Passed                # ctest exit 0
```

`ImportTrackIoTest` itself passes in the final family run
(`tests/src/core/ImportTrackIoTest.cpp`'s slot is unchanged apart from the spelling).

## Siblings checked

* The scan covered 7,072 first-party source files and 183 classes. A deliberately broad
  prototype reported 20 brace-initialised `QStringList` sites; 19 of them are
  declarations or default member initialisers, which Qt 5 accepts (for example
  `src/gui/modals/FileDialog.cpp:91`: `QStringList usableFileSystems = {...};`), or
  vendored source the final scan excludes by the same list `tests/all-sources.txt` uses.
  After declaration filtering exactly one finding remains: `ImportTrackIoTest.cpp:344`,
  the line the hosted job rejected. All other `QStringList` assignments in the tree are
  explicit or come from an expression.
* `TrackPlan::documents` is the only `QStringList` member in
  `include/ControlProjectTransplant.h`. `TrackPlan::need` is `std::vector<int>`; the test's
  `plan.need = {0, 1}` is unaffected on both Qt versions.
* The changed test still passes on the local Qt 6 build (family run above), and the fixed
  translation unit compiles silently against Qt 5.15.19 (`repro-qt5-2-fixed.log`).

## Registration and gates

* `tests/Qt5QStringListAssignCheck.py` is registered in `tests/fork-sources.txt` (new
  file) and `tests/CMakeLists.txt` registers the ctest wherever `PYTHON3_EXECUTABLE`
  exists, so the Qt 6 hosts run the source half too.
* `tests/CMakeLists.txt` and `tests/fork-sources.txt` are declared in
  `tests/upstream-modifications.txt` with registration-only reasons.
  `tests/src/core/ImportTrackIoTest.cpp` is a tests/ path already listed in
  `tests/fork-sources.txt`, so it needs no ledger entry.
* `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>`: exit 0,
  8/8 PASS (fork-sources, all-sources-reproduce, no-upstream-regression,
  check-namespace, no-tautology, doc-figures, complexity, file-length); no new ratchet
  item in the changed files.

## Self-check

* **The regression test FAILS with the fix reverted and PASSES with it.** Reverting the
  one-line test-source change gives ctest exit **8** with the finding at
  `ImportTrackIoTest.cpp:344` and "1 confirmed by a Qt 5 compile"
  (`test-reverted-red-2.log`; the first proof is `test-red-1.log`). Restoring it gives
  ctest exit **0** (`test-restored-green-2.log`, `test-green-1.log`). There is no product
  hunk to reverse because the defect is the test source's spelling; the same revert
  reproduces the hosted compiler error in the checker's Qt 5 compile probe. The affected
  target rebuilds and its own slot passes.
* **The test is deterministic.** It reads checked-in sources and the recorded
  `compile_commands.json`; its only external seam is the fixed Qt 5 development tree
  found by `pkg-config`, and a missing tree is reported as `skipped`, not as a pass. There
  is no wall-clock timing, sleep, thread or network access.
* **The fix is at the cause and complete.** The ambiguous conversion is removed by naming
  the destination type; the resulting `QStringList` contains the same elements, so
  `buildAsOneStep()` sees the same plan. Both Qt 5.15.19 and the local Qt 6 tree accept
  the line (the target builds and passes on Qt 6; the Qt 5 compile probe passes on the
  fixed translation unit). A whole-tree scan for the same shape found no other assignment
  to a `QStringList` member or local; declarations and the vendored trees' initialisers
  are valid Qt 5 and were not churned.
* **No collateral damage.** No product code, project format, saved data, command, undo
  path or audio-path code changed. The test's assertions and data are untouched, the
  target passes, and no new limit or check was added that another caller could exceed.
* **Not ALREADY-FIXED or FALSE-POSITIVE.** The code at `ImportTrackIoTest.cpp:344` is the
  defect, and the exact hosted compiler error was reproduced locally against Qt 5.15.19
  before the change (`repro-qt5-1.log`).
* **Realtime and undo.** Nothing added runs on the audio thread; the change is a test
  source's initialiser spelling. No command, undo step or reversibility class is touched.
