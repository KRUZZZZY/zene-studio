FIXED — cluster ci-623e9e3a015cd5b1: hosted linux-x86_64 Qt 5 build stops on an ambiguous `QStringList` brace assignment in `ImportTrackIoTest`

Verdict: **REAL**, a test-source portability defect (not infrastructure, not a product
runtime bug). Reproduced on this box's Qt 5.15 toolchain against the exact recorded compile
line of the failing translation unit at c20ef591b5.

## Evidence

The hosted job (`build / linux-x86_64`, run 43611324689 / job 132496796910) builds against
Qt 5.15 and stopped at 61 %:

```
tests/src/core/ImportTrackIoTest.cpp:344:71: error: ambiguous overload for 'operator='
  (operand types are 'QStringList' and '<brace-enclosed initializer list>')
  344 |   plan.documents = {UnparsableTrack, trackDocument(Lead)};
```

Cause, in the source and in Qt 5 itself:

* `include/ControlProjectTransplant.h:76` — `TrackPlan::documents` is a `QStringList`.
* `tests/src/core/ImportTrackIoTest.cpp:344` — assigns a braced-init-list to it.
* `/usr/include/qt/QtCore/qstringlist.h:111` — `class QStringList : public QList<QString>`.
* `/usr/include/qt/QtCore/qstringlist.h:124,126` — `QStringList` declares
  `operator=(const QList<QString>&)` and `operator=(QList<QString>&&)` **in addition** to its
  own copy/move assignment (111, implicit). A braced-init-list converts equally to
  `QList<QString>` and `QStringList`, so all four candidates are viable and overload
  resolution is ambiguous. Qt 6 declares no assignment operators on `QStringList`, so every
  Qt 6 host here compiles the same line (probe below: Qt 6 exit 0, Qt 5 exit 1).

The fix is the portable spelling the sibling cluster ci-48dcc062d04c645f used, an explicit
temporary selecting a single assignment:

```
tests/src/core/ImportTrackIoTest.cpp:344
-       plan.documents = {UnparsableTrack, trackDocument(Lead)};
+       plan.documents = QStringList{UnparsableTrack, trackDocument(Lead)};  // explicit: a braced assignment is ambiguous on Qt 5.15
```

Same semantics (the plan's document list is unchanged), no product code touched, so save,
load and render behaviour is untouched.

## Regression instrument

`tests/Qt5QStringListAssignmentCheck.py` (new, ctest `Qt5QStringListAssignmentCheck`,
registered in `tests/CMakeLists.txt:9157-9174`), with two independent halves:

1. **Fixtures**: a BAD `QStringList s; s = {a, b};` must be rejected by the Qt 5 tree and a
   GOOD `s = QStringList{a, b};` accepted. A tree that accepts BAD is not a Qt 5 tree with
   the trap, so it is reported *skipped*, never passed (`--self-test` proves just this half).
2. **The real translation unit**: `tests/src/core/ImportTrackIoTest.cpp` is recompiled
   `-fsyntax-only` from this build's `compile_commands.json` against the Qt 5 headers, with
   the Qt 5 moc output ahead of the recorded Qt 6 autogen directory. That is the hosted job's
   compiler input.

Without a pkg-config Qt 5 tree the probe prints `probe: skipped (…)` and exits 0 — the hosted
linux-x86_64 job is Qt 5, so it is red there before this fix, as reproduced below.

## Commands and exit codes (all run in this worktree)

| # | command | exit |
|---|---|---|
| 1 | `python3 tests/Qt5QStringListAssignmentCheck.py --root . --build-dir build` — before the fix (log `002-probe-red.log`) | **1** — `ImportTrackIoTest.cpp:344:71: error: ambiguous overload for 'operator=' …` |
| 2 | `cmake --build build --target ImportTrackIoTest` — with the fix (`006-build-import-red.log`) | **0** |
| 3 | `cd build/tests && ctest -R '^ImportTrackIoTest$' --output-on-failure` (`007-ctest-import-green.log`) | **0** — 1/1 passed |
| 4 | `cmake --build build --target ImportTrackIoTest` with the fix reverted (`028-build-reverted.log`) | **0** (Qt 6 compiles it, as hosted Qt 6 lanes do) |
| 5 | `cd build/tests && ctest -R '^Qt5QStringListAssignmentCheck$' --output-on-failure` — fix reverted (`029-ctest-red-reverted.log`) | **8** — the Qt 5 probe reports the hosted error: `ImportTrackIoTest.cpp:344:71: error: ambiguous overload …` |
| 6 | `git apply fix.patch` after the revert; `cmake --build build --target ImportTrackIoTest` (`030-build-restored.log`) | **0** |
| 7 | `cd build/tests && ctest -R '^Qt5QStringListAssignmentCheck$' --output-on-failure` — fix restored (`031-ctest-green-restored.log`) | **0** — 1/1 passed (`PASS: … compiles against Qt 5`) |
| 8 | `cd build/tests && ctest -R '^ImportTrackIoTest$' --output-on-failure` (`032-ctest-import-green.log`) | **0** — 1/1 passed |
| 9 | `python3 tests/Qt5QStringListAssignmentCheck.py --self-test` (`027-probe-selftest.log`) | **0** — fixtures distinguishable |
| 10 | `cd build/tests && ctest -R 'Qt5\|Train41\|MocQt5\|MountNamespace\|ImportTrackIo` — the portability/import family (`023-ctest-family-green.log`) | **0** — 15/15 passed |
| 11 | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py .` (`025-feature-check.log`) | **0** — all eight rows PASS |

Note on step 5's ordering, executed exactly as the brief asks: the `fix.patch` was produced
with `git diff -- tests/src/core/ImportTrackIoTest.cpp`, reverse-applied (`git apply -R`,
exit 0), rebuilt, probed (exit **8** = RED), reapplied (`git apply`, exit 0), rebuilt, probed
(exit **0** = GREEN). On this Qt 6 host step 4's Qt 6 build of the reverted file still
compiles — the trap only exists in the Qt 5 headers — which is exactly why the regression is
the Qt 5 compile probe and not a Qt 6 ctest pass.

Two family tests initially failed for unrelated, pre-existing reasons and were cleared by
building their targets, not by changing them: `Train41-darwin-tu` needs the generated
`CrashReporterArmTest.moc` (`018-build-crasharm.log`, then `019-ctest-train41-darwintu.log`
PASS), and `MountNamespaceHandshakeTest` was simply not built in this tree
(`022-build-mountnamespace.log`, then the family rerun 15/15). Neither is in this cluster.

## Completeness / sibling audit

`TrackPlan::documents` (`QStringList`) is assigned or consumed at:

* `tests/src/core/ImportTrackIoTest.cpp:309, 326` — `.append(...)`, never ambiguous;
* `tests/src/core/ImportTrackIoTest.cpp:344` — this bug (fixed);
* `src/core/ControlProjectTransplantChannels.cpp:191` — `.size()` / `operator[]`;
* `src/core/ControlCommandsImportTracks.cpp:261, 289` — filled through `ChannelNeeds`.

A repo-wide scan of first-party sources (including `plugins/`, excluding vendored
`3rdparty`, `carla`, `sfizz`) for `<QStringList-typed name> = {…}` found no other braced
assignment to a `QStringList`; every other `= {…}` hit is a `QJsonObject`, a struct/aggregate
or a `QVector`/`QList` whose assignment is not overloaded the same way (`QList`/`QVector` both
compile the brace assignment under Qt 5 — verified with the same probe). The product change
is one test line; no header, no product target, no command, no undo path.

Realtime: the diff touches no audio-thread function; `tests/src/core/ImportTrackIoTest.cpp`
and `tests/Qt5QStringListAssignmentCheck.py` never run on the render thread. No new control
command. No saved-data format, limit or check changed.

## Files committed

* `tests/src/core/ImportTrackIoTest.cpp` — the portable assignment (the fix).
* `tests/Qt5QStringListAssignmentCheck.py` — new regression instrument.
* `tests/CMakeLists.txt` — registers ctest `Qt5QStringListAssignmentCheck`.
* `tests/fork-sources.txt` — registers the new probe and this report.
* `docs/qwen-fixes-2026-10/cluster-ci-623e9e3a015cd5b1.md` — this report.

## Self-check

* **The regression test FAILS with your product change reverted and PASSES with it (exit
  codes).** RED: `ctest -R '^Qt5QStringListAssignmentCheck$'` after `git apply -R fix.patch`
  + rebuild → exit **8**, log `029-ctest-red-reverted.log` carrying the hosted diagnostic
  `ImportTrackIoTest.cpp:344:71: error: ambiguous overload …`. GREEN: same ctest after
  `git apply fix.patch` + rebuild → exit **0**, log `031-ctest-green-restored.log`
  (`1/1 Passed`). The Qt 6 `ImportTrackIoTest` also passes before and after (exit 0), which
  is expected: the Qt 6 host never had the ambiguity.
* **The test is deterministic.** No timing, sleeps or races: the probe compiles a fixed
  fixture pair and one fixed translation unit through the recorded compile command. The only
  injected values are the compile database's own paths and the Qt 5 flags from pkg-config;
  the Qt 5 moc output is written into a fresh temporary directory that precedes the recorded
  Qt 6 autogen include directory.
* **The fix is at the cause and complete.** Evidence above: the exact Qt 5 overload set
  (`/usr/include/qt/QtCore/qstringlist.h:111,124,126`), the one ambiguous line
  (`tests/src/core/ImportTrackIoTest.cpp:344`), and the sibling/caller audit showing no
  second braced `QStringList` assignment in first-party sources. The new probe also guards
  the pattern itself (its BAD fixture), so a reintroduction anywhere in that translation unit
  is caught.
* **No collateral damage.** The assignment produces the same `QStringList` contents, so the
  test's assertion path is unchanged (`anUnparsableDocumentDoesNotShiftTheOthers` still
  passes, exit 0), no saved data or project format is touched, no limit or check is added, and
  nothing outside this test line changes behaviour. The new ctest is additive; it registers
  only where `PYTHON3_EXECUTABLE` exists and skips cleanly on hosts without Qt 5.
* **The verdict is REAL**, with file:line evidence above rather than an "already fixed" claim:
  the unfixed line is still present at `tests/src/core/ImportTrackIoTest.cpp:344` on this
  branch's base commit c20ef591b5 and fails the Qt 5 compile probe (exit 1, log
  `002-probe-red.log`).

## Self-check (gate table)

`python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py .` → exit **0**
(`025-feature-check.log`): fork-sources PASS, all-sources-reproduce PASS,
no-upstream-regression PASS, check-namespace PASS, no-tautology PASS, doc-figures PASS,
complexity PASS (no new item in the changed files; the probe's `main` was split into
`run_self_test`/`check_product` to stay under CCN 10 — `024-feature-check.log` shows the
earlier FAIL), file-length PASS.
