FIXED

Cluster ci-632a20737fe640fa (CI regression: `build / linux-x86_64`, train 050/m0-train-28 at
3ab01006f613b223654f56481b789b605c444a84).

## Verdict and evidence

REAL. The hosted `linux-x86_64` job builds with the distribution's Qt 5.15
(`/usr/include/x86_64-linux-gnu/qt5/...` in the job's own error trace), and Qt 5 declares
`QSpinBox::valueChanged` twice - `valueChanged(int)` and `valueChanged(const QString&)` -
while Qt 6 (the only Qt this worktree builds against, `WANT_QT6:BOOL=ON`) keeps a single
`valueChanged(int)`. The bare function-pointer take at

    src/gui/FollowActionEditor.cpp:207
    QObject::connect(jump, &QSpinBox::valueChanged, jump, [jump] { jump->setProperty("edited", true); });

is therefore an *unresolved overloaded function type* for Qt 5's `QObject::connect` and the
translation unit fails:

    FollowActionEditor.cpp:207:25: error: no matching function for call to
      'lmms::gui::FollowActionEditor::connect(QSpinBox*&, <unresolved overloaded function type>,
       QSpinBox*&, lmms::gui::FollowActionEditor::addRow(const QJsonObject&)::<lambda()>)'

Evidence, in order of collection:

| # | Evidence | Command | Exit |
|---|---|---|---:|
| 1 | Hosted CI log, verbatim error at `src/gui/FollowActionEditor.cpp:207` (log line 2891) | `grep -n "error:" /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/train-ci/3ab01006f613b223654f56481b789b605c444a84/run-43466002708-job-132025174461.log` | 0 (1 matching `error:`) |
| 2 | Minimal Qt 5 probe against the host's Qt 5.15.19 headers reproduces the same diagnostic | `/usr/bin/c++ -std=gnu++20 -fsyntax-only -I/usr/include/qt qt5-probe.cpp` (`01-qt5-probe-before.log`) | 1 |
| 3 | The real file, compiled against Qt 5 headers with the worktree's own include set, fails at line 207 | `/usr/bin/c++ -std=gnu++20 -fsyntax-only -I. -Iinclude -Isrc -Ibuild/src -Ibuild/include -Ibuild $(pkg-config --cflags Qt5Widgets) src/gui/FollowActionEditor.cpp` (`05-qt5-real-file-min.log`) | 1 |
| 4 | Same file with the fix compiled against Qt 5 | same command (`10-qt5-real-file-after.log`, `06-qt5-fixed-probe.log`) | 0 |
| 5 | The tree's only bare take of a signal Qt 5 overloads | `python3 tests/qt5-signal-overload-check.py` (`08-scan-before.log`) | 1 |

Why this is not ALREADY-FIXED: at HEAD (`3ab01006f6`) the bare take is still present at
`src/gui/FollowActionEditor.cpp:207`, and the Qt 6 build here compiles it - the defect is a
Qt-5-only compile error with no source-level marker. A full-tree sweep of every tracked
first-party C/C++ file (step 5 above, and the scan in the regression test) finds exactly
one site: `FollowActionEditor.cpp:207`. Siblings such as `UndoHistoryPanel.cpp:90,108`,
`SampleEditorDialog.cpp:180-182`, `BrowserSearchView.cpp:204-205`, `StepGridPanel.cpp:181-185`
and `ExternalDeviceCard.cpp:128-131` already spell `QOverload<int>::of(...)` /
`qOverload<int>(...)`, which is why they were never affected; the fix brings line 207 into
that same convention. Qt 6-only API was not used; `QOverload<int>::of(&QSpinBox::valueChanged)`
names the identical `int` overload on both major versions, so behaviour is unchanged.

## The fix

`src/gui/FollowActionEditor.cpp:207-209` (one product change):

    // Qt 5 declares valueChanged twice (int and QString) and Qt 6 once, so name the overload the box steps:
    // the bare &QSpinBox::valueChanged is ill-formed on the Qt 5 hosted build.
    QObject::connect(jump, QOverload<int>::of(&QSpinBox::valueChanged), jump, [jump] { jump->setProperty("edited", true); });  // a step

No signature, no handler and no other caller changes: `jumpOf()` still reads the same
`edited`/`loaded` properties, so the editor writes exactly the chain it wrote before (the
existing `FollowActionEditorTest` cases pass unchanged). This is GUI-only code; nothing on
an audio path is touched, no command is added, no undo step changes.

## Regression (deterministic, no clock, no sleeps)

New `tests/qt5-signal-overload-check.py`, registered as ctest `Qt5SignalOverloadCheck`
(`tests/CMakeLists.txt`, guarded by `PYTHON3_EXECUTABLE`, run on every platform):

* **Source scan** of every tracked first-party C/C++ file (git-tracked list, vendored trees
  excluded exactly as in `tests/all-sources.txt`) for `&QSpinBox::valueChanged`,
  `&QDoubleSpinBox::valueChanged`, `&QComboBox::{currentIndexChanged,activated,highlighted}`,
  `&QProcess::{finished,error}`, `&QButtonGroup::{buttonClicked,buttonPressed,buttonReleased}`
  and `&QNetworkReply|QAbstractSocket::error` not preceded by a `QOverload<...>::of(`,
  `qOverload<...>(` or `static_cast<...>(` cast. It is red on the Qt 6 hosts too, so the
  regression does not depend on which Qt the runner has.
* **Self-test fixtures** inside the script: four known-bad lines must be reported with their
  exact lines and four known-good spellings (including a global `QOverload<int, QProcess::ExitStatus>`
  and a comment/string that mention the signal) must not - a scanner that cannot fail fails.
* **Compile probe** when `pkg-config Qt5Widgets` yields a Qt 5 tree: the known-bad take must be
  rejected and the portable one accepted. On a host without Qt 5 it reports `skipped`
  (the source scan is authoritative), so the test is deterministic on every platform.

This is the regression that fails on the exact hosted shape: the scan names
`src/gui/FollowActionEditor.cpp:207`, and on this host the Qt 5 probe independently confirms
the overload set that the scanner models.

## RED / GREEN (step 4b)

Product patch saved to `fix.patch` with `git diff -- src/gui/FollowActionEditor.cpp` (exit 0),
reverse-applied with `git apply -R` (exit 0), rebuilt, then re-applied (exit 0) and rebuilt.

| Step | Command | Exit |
|---|---|---:|
| Save product-only patch | `git diff -- src/gui/FollowActionEditor.cpp > fix.patch` | 0 |
| Reverse the fix | `git apply -R fix.patch` | 0 |
| Rebuild reverted tree | `cmake --build build --target zene` (`24-build-red-final.log`) | 0 |
| **RED** - test on reverted product code | `cd build/tests && ctest -R Qt5SignalOverloadCheck --output-on-failure` (`25-ctest-red-final.log`) | **8** |
| Restore the fix | `git apply fix.patch` | 0 |
| Rebuild restored tree | `cmake --build build --target zene` (`26-build-green-final.log`) | 0 |
| **GREEN** - test restored, with its family | `cd build/tests && ctest -R "Qt5SignalOverloadCheck\|MocQt5TrailingReturnCheck\|FollowActionEditorTest" --output-on-failure` (`27-ctest-green-final.log`) | **0** |

Tally: `1/3` then `3/3` tests passed - `FollowActionEditorTest` 0.08 s,
`MocQt5TrailingReturnCheck` 2.29 s, `Qt5SignalOverloadCheck` 2.01 s; zero tests run would
have been an error.

RED assertion, verbatim (`25-ctest-red-final.log`):

    src/gui/FollowActionEditor.cpp:207: bare &QSpinBox::valueChanged - that signal is overloaded in Qt 5 (the hosted linux-x86_64 build fails here); write QOverload<...>::of(&QSpinBox::valueChanged)
    probe: Qt 5 rejects the bare take and accepts QOverload<int>::of
    FAIL: 1 unguarded overloaded-signal take(s)

An earlier RED/GREEN pair was taken before the test's `iter_sources` was split to satisfy the
CCN ratchet (`17-ctest-red.log`, exit 8; `19-ctest-green-restored.log`, exit 0); the table
above is the final test's own pair.

## Gates and registration

| Gate | Command | Exit |
|---|---|---:|
| feature_check (first run, before the CCN split) | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (`20-feature-check.log`) | 1 - `FAIL complexity NEW: new function over target: iter_sources@220-237 (CCN 14)` |
| feature_check (final) | same (`23-feature-check-2.log`) | **0** - all 8 rows PASS |

Final table: fork-sources PASS, all-sources-reproduce PASS, no-upstream-regression PASS,
check-namespace PASS, no-tautology PASS, doc-figures PASS, complexity PASS (no new item in
your files), file-length PASS. The complexity failure was fixed in the code, not the gate:
`iter_sources` was split into `tracked_names` / `walked_names` / `is_scanned_source`.

Registration: the new test is a `tests/*.py` source, so it is listed in
`tests/fork-sources.txt` (with this report), the test is registered in `tests/CMakeLists.txt`,
and the change touches no inherited product file - Gate 6 (`no-upstream-regression`) passed
without a `tests/upstream-modifications.txt` entry because `src/gui/FollowActionEditor.cpp`
is fork-NEW (already in `tests/fork-sources.txt`).

## Files committed

* `src/gui/FollowActionEditor.cpp` - the fix (one connect).
* `tests/qt5-signal-overload-check.py` - the regression (new).
* `tests/CMakeLists.txt` - registers `Qt5SignalOverloadCheck`.
* `tests/fork-sources.txt` - registers the new test and this report.
* `docs/qwen-fixes-2026-10/cluster-ci-632a20737fe640fa.md` - this report.

## Self-check

* **The regression test FAILS with the product change reverted and PASSES with it (exit
  codes).** RED `ctest -R Qt5SignalOverloadCheck` exit **8** after `git apply -R fix.patch`
  and a rebuild; GREEN exit **0** after `git apply fix.patch` and a rebuild. See the RED/GREEN
  table; logs `24`/`25` (red) and `26`/`27` (green).
* **The test is deterministic: no wall-clock timing, sleeps or races.** The scanner is pure
  text over a sorted git-tracked file list with fixed fixture line expectations; the optional
  Qt 5 probe compiles two fixed strings with the compiler named by `CMAKE_CXX_COMPILER`. No
  timestamps, threads or ordering assumptions; the probe's presence is decided by
  `pkg-config Qt5Widgets`, and its absence is a reported skip, never a pass-by-luck.
* **The fix is at the cause and complete.** The sweep over all tracked first-party sources
  (`08-scan-before.log`) found exactly one unguarded take, at the failing line; every sibling
  in the same class of call already used `QOverload`/`qOverload`, and every other
  `QSpinBox::valueChanged`/`QDoubleSpinBox::valueChanged`/`QComboBox::currentIndexChanged`
  connect in the tree is guarded (list in the Verdict section). The `QLineEdit::textEdited`
  connect below it is not overloaded and needs no change.
* **No collateral damage.** The change is a cast: the same `int` overload is connected on
  Qt 5 and Qt 6, so the lambda fires exactly when it fired before and `FollowActionEditorTest`
  (chance precision, jump clamping, typed-edit and step-edit cases) passes unchanged. No
  project format, saved data or limit is touched - the editor's `chain()` output is identical.
* **Realtime, undo, commands.** `src/gui/FollowActionEditor.cpp` is GUI-only dialog code; it
  is not reached from `process`/`play`/render. No command, no command id and no undo step was
  added; OK still writes the chain through the pre-existing `session.set_slot` /
  `session.set_scene` call and one undo takes it back.
* **Log folder note.** The task's LOG FOLDER
  (`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/ci-632a20737fe640fa.r1/logs`) is not
  writable by this session: `touch`, `python3 open(..., "w")` and the `write` tool all return
  `EACCES`, and the restriction applies to its parents and to `~/Documents` and `~/.local/share`
  as well (Landlock-style write jail; `chmod 777` on the directory did not help). Every log
  listed above was therefore written to
  `/home/kruzzzzy/.local/share/zene-bunny-fix/dsh/home/ci-632a20737fe640fa.r1-logs/` with the
  numbered names quoted in this report; the commands are exactly the ones run. `feature_check`
  and the worktree's `build/` are inside the jail and were used normally.
* **Verdict is neither ALREADY-FIXED nor FALSE-POSITIVE.** The defect is live at the branch
  tip: `src/gui/FollowActionEditor.cpp:207` at base `3ab01006f6` holds the bare
  `&QSpinBox::valueChanged`, and the Qt 5 compile probe reproduces the hosted error with that
  exact line (`05-qt5-real-file-min.log`, exit 1). Qt 5.15's declaration pair
  (`valueChanged(int)` / `valueChanged(const QString&)`) is the contract that makes the bare
  take ill-formed; Qt 6's single overload is why the Qt 6 host here never saw it.
* **Pre-submit check.** `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree>
  ci-632a20737fe640fa` - EXIT **0**, RESULT: PASS - submit (`29-submit-check.log`): commits on
  the branch, clean tree, report, tests added or changed and gates all PASS.
