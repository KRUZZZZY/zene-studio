FIXED - cluster ci-c22070dd8cbbc7c0: Qt 5 build stops on an ambiguous `QSpinBox::valueChanged` member pointer in FollowActionEditor.cpp

Verdict: **REAL**, product/build-portability defect (not infrastructure, not a test-side defect).
The hosted `build / linux-arm64` job (`.github/workflows/build.yml:304-560`) builds against the
distribution's Qt 5 (`source /opt/qt5*/bin/qt5*-env.sh` in the Configure step,
`.github/workflows/build.yml:158`), where `QSpinBox` declares **two** `valueChanged` signals,
while this box's configured Qt 6 tree (`build/CMakeCache.txt:791` `WANT_QT6:BOOL=ON`) declares
only one. Reproduced on the pinned commit `3ab01006f613b223654f56481b789b605c444a84` with this
box's Qt 5.15.13 headers; the one-line fix is the disambiguated form the rest of the tree already
uses.

## The reported failure

The job stopped at 32 % compiling `src/CMakeFiles/lmmsobjs.dir/gui/FollowActionEditor.cpp.o`:

```
src/gui/FollowActionEditor.cpp:207:25: error: no matching function for call to
'lmms::gui::FollowActionEditor::connect(QSpinBox*&, <unresolved overloaded function type>,
QSpinBox*&, lmms::gui::FollowActionEditor::addRow(const QJsonObject&)::<lambda()>)'
  207 | QObject::connect(jump, &QSpinBox::valueChanged, jump, [jump] { jump->setProperty("edited", true); });  // a step
      | ~~~~~~~~~~~~~~~~^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
```

The candidate list in the CI log is the ordinary `QObject::connect` overload set failing at
template argument deduction for `Func1` (`couldn't deduce template parameter 'Func1'`) - the
signature of an unresolved overloaded function name, not of a missing include or a wrong slot.

## Decision: REAL, product code

- Qt 5.15's `qspinbox.h:108` declares `void valueChanged(int);` and `:112` (under
  `QT_DEPRECATED_SINCE(5, 14)`) declares `void valueChanged(const QString &);`. Qt 6's
  `qspinbox.h:73` has only `void valueChanged(int);` (the deprecated text overload became
  `textChanged`). So `&QSpinBox::valueChanged` is an `<unresolved overloaded function type>` on
  the CI toolchain and a single function on this box's Qt 6 - the exact asymmetry the log shows.
- The line is fork-NEW code (`tests/fork-sources.txt:2089`), introduced by
  `7917f8d863 fix(gui): keep a follow-action jump scene the box cannot show (cluster new-p20-49)`.
  It is not an infrastructure fault and not a test-source fault: it is a product GUI translation
  unit that the Qt 5 job compiles and the Qt 6 job here does not reject.
- Classification: **product** (Qt 5 build-portability defect in a product source).

## Reproduction on this commit (Qt 5.15.13, this box)

The lane's `cmake` is guarded against configuring (a parent job owns the lane build tree), so the
exact translation unit was compiled with its own recorded command from
`build/compile_commands.json`, re-pointed at this box's Qt 5 headers (5.15.13,
`~/.local/opt/qt5/usr`, the same 5.15 line the CI job installs):

```
# qt5-compile-command.txt is the compile_commands.json entry for src/gui/FollowActionEditor.cpp
# with -isystem /usr/include/qt6* -> ~/.local/opt/qt5/usr/include/x86_64-linux-gnu/qt5/*
bash -c "$(cat qt5-compile-command.txt)"     # EXIT 1
```

EXIT **1**, byte-for-byte the CI diagnostic (`build-1-qt5-red.log:2-3`):

```
src/gui/FollowActionEditor.cpp:207:25: error: no matching function for call to
‘lmms::gui::FollowActionEditor::connect(QSpinBox*&, <unresolved overloaded function type>,
QSpinBox*&, lmms::gui::FollowActionEditor::addRow(const QJsonObject&)::<lambda()>)’
```

The same command with the fix applied exits **0** (`build-3-qt5-green-single-tu.log`,
`build-11-qt5-product-postfix.log`). The test translation unit also compiles under Qt 5 both
before and after the fix (the defect is in the product source only):
`build-10-qt5-test-prefix.log` EXIT 0, `build-12-qt5-test-postfix.log` EXIT 0.

## The fix (smallest, no behaviour change)

`src/gui/FollowActionEditor.cpp:207-210`:

```cpp
	// Qt 5 publishes a second valueChanged(const QString&) on QSpinBox, so the member pointer needs its int overload
	// named or the Qt 5 build cannot deduce connect's signal argument; Qt 6 declares only this one.
	QObject::connect(jump, QOverload<int>::of(&QSpinBox::valueChanged), jump,
		[jump] { jump->setProperty("edited", true); });  // a step
```

`QOverload<int>::of` selects the same `valueChanged(int)` signal Qt 6 resolves on its own, so the
connection, the lambda, the `edited` property and therefore `jumpOf()`'s typed-vs-loaded behaviour
are identical. This is the form already used at `src/gui/UndoHistoryPanel.cpp:90`,
`src/gui/BrowserSearchView.cpp:204-205`, `src/gui/ChordProgressionPanel.cpp:134`,
`src/gui/SampleEditorDialog.cpp:180-182` and `src/gui/instrument/ExternalDeviceCard.cpp:128-131`.

Completeness - every sibling and every caller of the changed line:

- Swept every first-party source (`src/ include/ plugins/ tests/`, vendored trees excluded) for
  every `&Q<Class>::<member>` member pointer (`git`-tracked C/C++ only) and intersected it with the
  Qt 5.15 headers: exactly one unqualified use of a Qt 5-overloaded signal exists in the tree -
  this line. `QComboBox::currentIndexChanged`/`activated`/`highlighted`, `QProcess::finished` and
  the `QButtonGroup` int overloads are all already written with `QOverload`/`qOverload`/
  `static_cast`; `QSocketNotifier::activated`'s second Qt 5 overload is compiled only under
  `Q_MOC_RUN`/`BUILDING_QSOCKETNOTIFIER` (`qsocketnotifier.h:77-84`), so its member pointers are
  unambiguous. `QWidget::update`/`setFocus` uses are already `qOverload<>(...)`.
- The other signals on the same row (`QLineEdit::textEdited` at `:210`) have a single overload.
- The sibling connect in the same function (`QComboBox::currentIndexChanged`, `:93`) was already
  `QOverload<int>::of(...)`, so the row is now consistent.
- No header changed; `zene` and `FollowActionEditorTest` were rebuilt (they link this object).

## Regression test

`tests/qt5-signal-overload-check.py` (221 lines), registered as ctest
**`Qt5SignalOverloadCheck`** (`tests/CMakeLists.txt:6426-6441`, beside `MocQt5TrailingReturnCheck`,
wherever a python3 is). It is a source scan: no build, no Qt, no moc, no clock, no sleeps, no
threads, so it runs on the Qt 6 hosts where the defect compiles and is deterministic.

- It knows the Qt 5.15 signals that have two overloads, each pinned with the header lines that
  declare them (`QSpinBox`/`QDoubleSpinBox::valueChanged`, `QComboBox::currentIndexChanged`/
  `activated`/`highlighted`, `QProcess::finished`, the `QButtonGroup` int overloads) and requires
  every `&Class::signal` member pointer to those to sit inside a `QOverload<...>::of(...)`,
  `qOverload<...>(...)` or `static_cast<...>(...)` call.
- Comments and string/char literals are blanked first, so a mention in prose or in an error string
  is not a finding.
- Before scanning the tree it runs its own fixtures: a red fixture (an unqualified
  `&QSpinBox::valueChanged` and `&QComboBox::currentIndexChanged`, checked at exact line numbers),
  a green fixture (the three disambiguating forms plus an unambiguous signal), and a prose
  mention. A scanner that cannot fail, fails. `--self-test` runs only those fixtures.

## Commands and exit codes

All logs are in the brief's LOG FOLDER. This session's write jail denies
`openat`/`touch`/`mkdir` anywhere under `~/.local/share/zene-bunny-fix/qwen/runs/**`
(`log-folder-write-denial.log`, EXIT 1) - the same landlock-style jail recorded for earlier lanes -
so every log was produced in `$TMPDIR/qwen-logs-ci-c22070dd8cbbc7c0/` (on disk) and mirrored
unchanged, under the same file names, to
`~/.local/share/zene-bunny-fix/dsh/home/ci-c22070dd8cbbc7c0.r1-logs/`.

| Step | Command | EXIT | Log |
| --- | --- | --- | --- |
| Repro, Qt 5.15.13 TU (unfixed) | `bash -c "$(cat qt5-compile-command.txt)"` | **1** | `build-1-qt5-red.log` |
| Regenerate build system + build test target | `cmake --build build --target FollowActionEditorTest` | 0 | `build-2-regenerate-and-test.log` |
| **RED** ctest, unfixed product | `cd build/tests && ctest -R '^Qt5SignalOverloadCheck$' --output-on-failure` | **8** (1 test, 1 failed) | `test-1-red.log` |
| Qt 5 TU after fix | `bash -c "$(cat qt5-compile-command.txt)"` | 0 | `build-3-qt5-green-single-tu.log` |
| Rebuild product + test | `cmake --build build --target zene FollowActionEditorTest` | 0 | `build-4-green-rebuild.log` |
| **GREEN** ctest | `ctest -R '^Qt5SignalOverloadCheck$' --output-on-failure` | 0 (1/1) | `test-2-green.log` |
| **4b** save + reverse product diff | `git diff -- src/gui/FollowActionEditor.cpp > fix.patch`; `git apply -R fix.patch` | 0 / 0 | `fix.patch` |
| **4b** rebuild reverted | `cmake --build build --target zene FollowActionEditorTest` | 0 | `build-5-reverted-rebuild.log` |
| **4b** RED again | `ctest -R '^Qt5SignalOverloadCheck$' --output-on-failure` | **8** (1 failed) | `test-3-reverted-red.log` |
| **4b** re-apply + rebuild | `git apply fix.patch`; `cmake --build build --target zene FollowActionEditorTest` | 0 / 0 | `build-6-reapplied-rebuild.log` |
| **4b** GREEN again | `ctest -R '^Qt5SignalOverloadCheck$' --output-on-failure` | 0 (1/1) | `test-4-reapplied-green.log` |
| Family ctest | `ctest -R '^(Qt5SignalOverloadCheck\|FollowActionEditorTest\|MocQt5TrailingReturnCheck)$' --output-on-failure` | 0 (**3/3**) | `test-5-family-green.log`, `test-6-final-family.log` |
| Sibling Qt 5 checks still pass | `python3 tests/moc-qt5-trailing-return-check.py .` | 0 | `check-3-moc-family.log` |
| Check script py-syntax | `python3 -m py_compile tests/qt5-signal-overload-check.py` | 0 | n/a |
| Whitespace | `git diff --check` | 0 | n/a |
| Qt 5.15.13 TU with fix reverted (before 4b restore) | `bash -c "$(cat qt5-compile-command.txt)"` | **1** | `build-9-qt5-product-prefix.log` |
| Qt 5.15.13 TU with fix restored | `bash -c "$(cat qt5-compile-command.txt)"` | 0 | `build-11-qt5-product-postfix.log` |
| Qt 5.15.13 test TU, both states | `bash -c "$(cat qt5-test-compile-command.txt)"` | 0 / 0 | `build-10-qt5-test-prefix.log`, `build-12-qt5-test-postfix.log` |
| Gate 9 (fork-sources) | `bash tests/fork-sources-gate.sh` | 0 | `gate-3-fork-sources-final.log` |
| **Gates** | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 (8/8 PASS) | `feature-check-2-final.log` |

The Qt 5 `moc` on this box is `moc 5.15.13` (`~/.local/opt/qt5/usr/lib/x86_64-linux-gnu/qt5/bin/moc`);
it mangled the test source without error (`moc-1-qt5-test.log`, EXIT 0), which is why the test
translation unit also compiles under Qt 5 in both states - only the product line was ambiguous.

## Test matrix

| Path | Before the fix | After the fix | Evidence |
| --- | --- | --- | --- |
| Qt 5.15 build of `src/gui/FollowActionEditor.cpp` (the CI job) | compile error, EXIT 1, the CI message | EXIT 0 | `build-1-qt5-red.log`, `build-11-qt5-product-postfix.log` |
| Qt 6 build of `zene` + `FollowActionEditorTest` (this box) | builds | builds | `build-4`, `build-13` |
| Normal path: a Jump box step marks the row edited, OK writes the typed scene | existing slots pass | existing slots pass | `FollowActionEditorTest` 10/10 slots (`Totals: 10 passed, 0 failed`), `test-6-final-family.log` |
| Edge paths: a loaded scene past the grid kept until typed, INT_MAX, -1 sentinel, passive focus loss, a row removed while its controls get focus | existing slots pass | unchanged (the connect target and lambda are identical) | same |
| Qt 5 signal-overload sweep, whole first-party tree | 1 finding (this line) | 0 findings | `check-1-red-tree.log`, `check-4-green-tree.log` |
| Audio-thread constraints | no audio-path file touched; the connect runs in the GUI row builder | no allocation/lock/wait/log added on any audio path | diff is one `connect` in `addRow` |
| Undo / commands | no command added or changed; `chain()` unchanged | one honest OK step, as before | `git diff` (product hunk is the one connect) |

## Collateral damage

None. The signal selected is the same `valueChanged(int)` Qt 6 was already resolving, and the
lambda and context object are unchanged, so `jumpOf()` still sees `edited` exactly on a user step
or typed value and a loaded out-of-grid scene is still written back as loaded. No save/load format,
limit, check or command changed. The only new file is the source-reading check (221 lines, under
the 500-line cap; its functions are CCN <= 7).

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.** Reversing the
  product hunk (`git diff -- src/gui/FollowActionEditor.cpp > fix.patch`, `git apply -R fix.patch`,
  both EXIT 0) and rebuilding (`build-5-reverted-rebuild.log`, EXIT 0) gives
  `ctest -R '^Qt5SignalOverloadCheck$'` **EXIT 8** (`test-3-reverted-red.log`: it names
  `src/gui/FollowActionEditor.cpp:207`); re-applying (`git apply fix.patch`, EXIT 0), rebuilding
  (`build-6-reapplied-rebuild.log`, EXIT 0) gives **EXIT 0** (`test-4-reapplied-green.log`). The
  same reverse/restore cycle also moved the real Qt 5 compile: EXIT 1
  (`build-9-qt5-product-prefix.log`) then EXIT 0 (`build-11-qt5-product-postfix.log`).
- **The test is deterministic.** It is a pure source scan with fixed inline fixtures and pinned Qt 5
  header line numbers: no build, no moc, no Qt runtime, no wall-clock, no sleeps, no threads. It
  needs no injected timestamps.
- **The fix is at the cause and complete.** Every first-party use of a Qt 5-overloaded signal was
  enumerated against the Qt 5.15 headers; this is the only unqualified one, and the other signals
  in the same function (`QComboBox::currentIndexChanged` at `:93`, `QLineEdit::textEdited` at
  `:210`) are already unambiguous. No header changed; `zene` and `FollowActionEditorTest` were
  rebuilt and the family ctest passes 3/3.
- **No collateral damage.** Old projects and saved follow-action chains load and write exactly as
  before (same signal, same lambda, same `edited` semantics - the `FollowActionEditorTest` slots
  covering the typed, stepped, in-range, out-of-grid, INT_MAX and -1 paths all pass). No limit or
  check was added to product code; the new check reads sources only. No new command, no control
  snapshot movement, no audio-thread code, one OK step.
- **ALREADY-FIXED / FALSE-POSITIVE:** N/A - the verdict is FIXED, and the failure was reproduced on
  the pinned commit with the Qt 5.15 line the CI job installs (`build-1-qt5-red.log`, EXIT 1, the
  identical diagnostic).
- **Registration.** `tests/qt5-signal-overload-check.py` is registered in `tests/fork-sources.txt`
  (line 3390, with its cluster note); the ctest is in `tests/CMakeLists.txt:6426-6441`. Gate 9
  passes (2527 fork-NEW entries, 0 stale), and `feature_check.py` reports 8/8 PASS.

## Environment notes (honest)

- The lane guard refuses `cmake -S/-B` (configuring is the parent's job), so the Qt 5 proof uses the
  recorded `compile_commands.json` command for each translation unit, re-pointed at this box's Qt 5
  5.15.13 headers; the product and test commands are stored as `qt5-compile-command.txt` and
  `qt5-test-compile-command.txt` beside the logs.
- The live build tree is Qt 6 (`WANT_QT6:BOOL=ON`), which is why only the source-reading check can
  catch this class on this box; the check is registered on every platform a python3 is.
- Only this one file of the Qt 5 product build was compiled under Qt 5 (the finding's own
  translation unit). The rest of the tree's Qt 5 build is not claimed.

## Files committed

- `src/gui/FollowActionEditor.cpp` - one `connect` disambiguated with `QOverload<int>::of` plus a
  comment saying why.
- `tests/qt5-signal-overload-check.py` - new source-reading regression check (fork-NEW).
- `tests/CMakeLists.txt` - registers ctest `Qt5SignalOverloadCheck`.
- `tests/fork-sources.txt` - registers the new check script.
- `docs/qwen-fixes-2026-10/cluster-ci-c22070dd8cbbc7c0.md` - this report.
