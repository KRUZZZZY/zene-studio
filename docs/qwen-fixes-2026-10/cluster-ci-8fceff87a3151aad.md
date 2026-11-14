FIXED - cluster ci-8fceff87a3151aad: windows-arm64 Qt 5.15 build fails on `&QSpinBox::valueChanged` in FollowActionEditor.cpp

Verdict: **REAL**, reproduced on this box with Qt 5.15.19 headers (the same headers that job compiles
against), fixed at the call site, and now held in place by ctest `Qt5OverloadedSignalCheck`.

## Evidence the bug is real

- CI log (`~/.local/share/zene-bunny-fix/qwen/train-ci/3ab01006f613b223654f56481b789b605c444a84/run-43466002708-job-132025175021.log:1505`)
  has exactly one compile error on the whole 6447-step build:
  `src/gui/FollowActionEditor.cpp:207:11: error: no matching member function for call to 'connect'`,
  with the compiler's own note "candidate template ignored: couldn't infer template argument 'Func1'".
- Cause, in the product: `src/gui/FollowActionEditor.cpp:207` (pre-fix)
  `QObject::connect(jump, &QSpinBox::valueChanged, jump, [...])`. Qt 5.15 declares the signal twice,
  `void valueChanged(int);` and the deprecated `void valueChanged(const QString &);`
  (`/usr/include/qt/QtWidgets/qspinbox.h:108` and `:112`), so `&QSpinBox::valueChanged` is an overload set,
  not a member pointer, and the PMF `connect` template cannot deduce `Func1`. Qt 6 removed the QString
  overload (`/usr/include/qt6/QtWidgets/qspinbox.h:73` only), which is why the Qt 6 build on this host and
  every Qt 6 CI job stayed green - the failure is Qt-5-only and platform-independent.
- Reproduced locally against Qt 5.15 (`02-qt5-syntax-prefix.log`, EXIT=1):
  `g++ -std=c++20 -fsyntax-only $(pkg-config --cflags Qt5Widgets Qt5Xml) -Iinclude -Isrc -Ibuild/include -Ibuild/src src/gui/FollowActionEditor.cpp`
  → `src/gui/FollowActionEditor.cpp:207:25: error: no matching function for call to
  'lmms::gui::FollowActionEditor::connect(QSpinBox*&, <unresolved overloaded function type>, ...)'`,
  the same diagnostic the job printed. After the fix the identical command is EXIT=0
  (`03-qt5-syntax-postfix.log`).
- Classification: **product** (a Qt-5-incompatible call site), not test and not infrastructure. The job's
  Qt 5.15 is a supported configuration (`.github/workflows/build.yml` builds windows-arm64 with msys2
  clangarm64 Qt 5, mkspec `win32-clang-g++` shown in the CI command line).
- The same trap exists for the other Qt 5 signals that kept a deprecated QString/pointer overload:
  `QDoubleSpinBox::valueChanged`, `QComboBox::{activated,currentIndexChanged,highlighted}`,
  `QButtonGroup::{buttonClicked,buttonPressed,buttonReleased,buttonToggled}`, `QProcess::finished`,
  `QCompleter::{activated,highlighted}`, `QSignalMapper::mapped`, `QSqlDriver::notification`,
  `QTextBrowser::highlighted` (derived from the installed Qt 5.15 headers; each verified to fail deduction).
  A tree-wide sweep found **one** unresolved site, the one CI failed on; every other use already names its
  overload with `QOverload`/`qOverload`/`static_cast` (68 such connect calls). `QSocketNotifier::activated`
  is deliberately NOT in the set: its Qt 5 second overload is private (`Q_QDOC`/`BUILDING_QSOCKETNOTIFIER`),
  and `&QSocketNotifier::activated` compiles on Qt 5.15 (probe EXIT=0).

## The fix (smallest, no behaviour change)

- `src/gui/FollowActionEditor.cpp:210`: the signal pointer becomes
  `QOverload<int>::of(&QSpinBox::valueChanged)`, the same resolution `BrowserSearchView.cpp:204`,
  `StepGridPanel.cpp:181` and the other 18 `QSpinBox` connects in this tree already use. The connected
  signal is the identical `valueChanged(int)` a step emits on Qt 6, and typing is already covered by the
  separate `QLineEdit::textEdited` connection two lines below (`:213`), so both platforms set the same
  `edited` property for the same user actions. No Qt-6-only API, no platform guard needed (the overload
  selection is valid on Qt 5 and Qt 6).
- Callers/siblings checked: `wireRow` (`:93`) and `numberBox` (`:118`) in the same file already resolve
  their overloads; `jumpOf`/`valueOf` are untouched. Nothing else in the file, and no other file in the
  tree, names an overloaded Qt 5 signal without resolution (the sweep above is the check).

## Regression test

`tests/qt5-overloaded-signal-check.py`, registered as ctest **`Qt5OverloadedSignalCheck`**
(`tests/CMakeLists.txt:6436-6440`, beside `MocQt5TrailingReturnCheck`), and registered in
`tests/fork-sources.txt:3390`. It is source-only: no compiler, no Qt headers, no build, no timing - it
scans `src/ include/ plugins/ tests/` (excluding vendored trees) for a `connect(...)` whose argument is
`&Class::signal` for one of the Qt 5 overloaded signals above without a `QOverload`/`qOverload`/
`static_cast` in front of it. Before scanning it runs its own red/green fixtures: five red connects must be
flagged at lines 4-8 with the exact declarations, and the green fixture (explicit resolutions, an
unambiguous same-named signal such as `QSlider::valueChanged`, a string-based `SIGNAL(...)` connect, and a
comment naming the signal) must be clean - a scanner that cannot fail fails. The tree-wide sweep is
EXIT=0 with the fix and reports `src/gui/FollowActionEditor.cpp:207` without it.

## Commands and exit codes (logs kept in `$TMPDIR/ci-8fceff87a3151aad.r1-logs/`)

The requested LOG FOLDER was not writable in this session
(`touch /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/ci-8fceff87a3151aad.r1/logs/x` →
`Permission denied`; its ACL is `user::rwx, group::r-x, other::r-x` and even `python3 open(..., 'w')` and the
write tool get EACCES, while other paths under `$HOME` are denied too). Every log below was therefore
written to `$TMPDIR/ci-8fceff87a3151aad.r1-logs/`, which is the on-disk scratch location the brief allows,
and then copied (unchanged, `cp` EXIT=0) into the worktree's gitignored
`build/qwen-ci-8fceff87a3151aad.r1-logs/` so they survive the scratch cleanup.

| Step | Command | EXIT |
| --- | --- | --- |
| Repro, Qt 5.15 syntax (unfixed) | `g++ -std=c++20 -fsyntax-only ... $(pkg-config --cflags Qt5Widgets Qt5Xml) src/gui/FollowActionEditor.cpp` (`02-qt5-syntax-prefix.log`) | 1 (`:207:25 no matching function for call to connect`) |
| Repro, Qt 5.15 syntax (fixed) | same command (`03-qt5-syntax-postfix.log`) | 0 |
| **RED** (check, unfixed) | `python3 tests/qt5-overloaded-signal-check.py .` (`01-scan-red.log`) | 1 (1 finding: `src/gui/FollowActionEditor.cpp:207`) |
| Gate table, first run | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py .` (`05-feature-check-1.log`) | 1 (complexity: new `strip_comments` CCN 23 - fixed by splitting into helpers) |
| Build (green) | `cmake --build build --target zene FollowActionEditorTest -j8` (`09-build-green.log`) | 0 |
| **GREEN** (family) | `ctest -R '^(Qt5OverloadedSignalCheck\|MocQt5TrailingReturnCheck\|FollowActionEditorTest)$' --output-on-failure` from `build/tests` (`10-ctest-green.log`, `14-ctest-green-2.log`) | 0 (**3/3** passed) |
| 4b revert | `git diff -- src/gui/FollowActionEditor.cpp > fix.patch && git apply -R fix.patch` (`fix.patch`) | 0 |
| 4b rebuild (reverted) | `cmake --build build --target zene FollowActionEditorTest -j8` (`11-build-red.log`) | 0 |
| **4b RED** | `ctest -R '^(Qt5OverloadedSignalCheck\|FollowActionEditorTest)$' --output-on-failure` (`12-ctest-red.log`) | 8 (**1 of 2 failed**: `Qt5OverloadedSignalCheck` reports `src/gui/FollowActionEditor.cpp:207`) |
| 4b restore | `git apply fix.patch` | 0 |
| 4b rebuild (restored) | `cmake --build build --target zene FollowActionEditorTest -j8` (`13-build-green-2.log`) | 0 |
| **4b GREEN** | `ctest -R '^(Qt5OverloadedSignalCheck\|MocQt5TrailingReturnCheck\|FollowActionEditorTest)$' --output-on-failure` (`14-ctest-green-2.log`) | 0 (**3/3** passed; `FollowActionEditorTest` 0.15 s, so the box behaviour is unchanged on Qt 6) |
| Gates (before commit) | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py .` (`05-feature-check-2.log`) | 0 (all 8 rows PASS) |
| Gates (final, after commit) | same command (`16-feature-check-final.log`) | 0 (all 8 rows PASS, 5 changed files) |
| Submit check | `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py . ci-8fceff87a3151aad` (`15-submit-check.log`) | 0 (RESULT: PASS - submit; every ASK answered in Self-check) |

The `FollowActionEditorTest` family runs its 8 slots (loaded chance precision, typed chance, max/negative/
out-of-range jump scenes, a step of a jump box, in-range scenes, row removal under focus) and stays green:
the `edited`-setter semantics on Qt 6 are untouched, and the Qt 5 path now resolves to the same
`valueChanged(int)`.

## No collateral damage

- Behaviour is identical on every platform: the Qt 6 build connected `valueChanged(int)` already (the
  QString overload does not exist there), and Qt 5 now selects that same overload explicitly.
- No saved data, project format, undo/coalescing, control command or command snapshot is touched; no new
  command, no schema, no gate limit changed.
- Realtime: the changed line is GUI dialog construction (`FollowActionEditor::addRow`), not an audio-thread
  path; nothing added allocates, locks or logs on a render path.
- The regression check is registered wherever a python3 is, so windows-arm64, linux and macOS all run it;
  it opens files read-only and needs no Qt.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it (exit codes).** RED:
  `12-ctest-red.log`, ctest exit **8**, 1 of 2 failed (`src/gui/FollowActionEditor.cpp:207`). GREEN:
  `14-ctest-green-2.log`, ctest exit **0**, 3/3 passed. The revert/restore commands are the table rows
  above (`git apply -R` EXIT=0, `git apply` EXIT=0).
- **The test is deterministic: no wall-clock timing, sleeps or races.** It is a static source scan: no
  compiler invocation, no build, no Qt, no subprocess. Its only inputs are the tracked sources and its own
  red/green fixtures, and it prints file:line for each finding.
- **The fix is at the cause and complete: every caller and sibling checked.** The tree-wide sweep shows this
  was the only unresolved overloaded-signal connect (68 others already resolved; `wireRow:93` and
  `numberBox:118` in the same file included). The check's catalogue is the full set of Qt 5.15 signals with a
  deprecated second overload, excluding `QSocketNotifier::activated` (private second overload; a Qt 5.15
  compile of that pointer exits 0).
- **No collateral damage: old projects and saved data still load as before; no limit added.** Nothing on the
  save/load path changed. The connected signal on Qt 6 is byte-for-byte the same one as before, and
  `FollowActionEditorTest` (8 slots, including the loaded-precision and out-of-range jump-scene cases) stays
  green.
- **Verdict is FIXED, not ALREADY-FIXED/FALSE-POSITIVE.** The pre-fix tree compiles under Qt 6 (EXIT=0) and
  fails under Qt 5.15 (EXIT=1 at `:207:25`), so the defect was live at 3ab01006f.
