# Autosave and last-session recovery

Branch `post-alpha/autosave`, base `0c23587d2` ("test: drive the remote-plugin host<->client
contract end to end"). Worktree `projects/lmms-fl-research/zene-pa-autosave`.

## Verdict

Autosave already exists in this tree, in both halves, and it works: a periodic write to
`recover.mmp` plus a "recover last session" prompt on the next launch. What was missing was not
the mechanism but its **safety**: the prompt offered any `recover.mmp` on disk, with no idea which
project it belonged to or whether it was newer than the project file. A stale file from an
unrelated project was therefore offered forever as "the project of this session" (and suppressed
"open last project" until the user discarded it), and launching with a project named on the
command line was hijacked by a recovery of something else.

This branch adds the missing identity: an autosave now records which project it came from in a
side file, and the startup decision refuses a recovery that is stale or that names a different
project. **Nothing in the project format changes** — the side file is separate, and a recovery
file written by upstream (no side file) is still offered exactly as before.

Two premises in the task brief did not survive checking, and are corrected here rather than
worked around:

* `docs/KNOWN-LIMITATIONS.md:61-64` **does** describe autosave recovery ("Autosave writes the
  project to `recover.mmp` ... the next start offers to recover it. Recovery is only as fresh as
  the last autosave, so keep saving."). The write half and the prompt are both shipped and
  documented; the gap register entry (`BACKLOG.md:234-238`) is a product-gap statement, not a
  statement that no code exists.
* The "smallest honest version" the backlog names — "periodic autosave to a sidecar + 'recover
  last session' on relaunch" — therefore describes what already shipped. The honest remainder was
  the correctness of the offer, which is what this branch does.

## 1. What exists today (verified, base commit `0c23587d2`)

| what | where |
|---|---|
| The recovery path | `include/ConfigManager.h:205-208` — `recoveryFile()` returns `m_workingDir + "recover.mmp"` |
| …its directory | `src/core/ConfigManager.cpp:708-716` — `QStandardPaths::DocumentsLocation + "/lmms/"` (Linux: `~/Documents/lmms/`), or `applicationDirPath()/lmms-workspace/` in portable mode |
| The periodic write | `src/gui/MainWindow.cpp:1456-1475` — `MainWindow::autoSave()` calls `Engine::getSong()->saveProjectFile(recoveryFile)` (line 1466) |
| …triggered by | a `QTimer` on the **GUI thread**: connected at `src/gui/MainWindow.cpp:211-225`, started at `src/core/main.cpp:959` |
| …at what interval | `ui/saveinterval` minutes, floor 1 minute, default 2 (`include/MainWindow.h:94-95`, `:99-108`); a deferred save retries every 10 s (`include/MainWindow.h:97`) |
| …guarded by | not exporting, not loading, no remote-plugin main-thread wait, no mouse button held, and not playing unless `ui/enablerunningautosave` (`MainWindow.cpp:1458-1464`) |
| The setting | `src/gui/modals/SetupDialog.cpp:132-135` (keys `ui/enableautosave` default `"1"`, `ui/enablerunningautosave` default `"0"`), group box and interval slider at `:396-431` |
| The startup prompt | `src/core/main.cpp:818-895` — a `QMessageBox` with Recover / Discard / hidden Exit |
| …its condition | `src/core/main.cpp:821-822` — `QFileInfo(recoveryFile).exists() && isFile()`, and nothing else |
| …what it suppresses | `src/core/main.cpp:930-933` — "open last project" is skipped whenever a recovery file exists |
| Clean-exit cleanup | `src/gui/MainWindow.cpp:1318-1322` — `sessionCleanup()` removes `recover.mmp`; called from `closeEvent` (`:1297-1312`) and the File→Save paths |

**No allocation or audio-thread work is involved**: the timer lives on the GUI thread and the only
work it does is a file write. No audio-thread callback was added by this branch.

## 2. Today's behaviour, reproduced

The GUI had to be driven headlessly, and two product-startup blockers sit in the way of that on
this box. Both are real behaviour, not test scaffolding, and both are worth knowing:

1. `MainWindow::finalize()` (`src/gui/MainWindow.cpp:480-487`) shows the **first-run `SetupDialog`
   with `exec()`** unless config `app/configured` is set — a modal dialog with nobody to click it,
   so the process hangs before the project ever loads.
2. `Engine` must open an audio device; with no sound server the audio-failure
   `QMessageBox::critical` + `SetupDialog::exec()` (`src/gui/MainWindow.cpp:491-499`) block the same
   way. The tree has a dummy device — `AudioDummy::name()` is `"Dummy (no sound output)"`
   (`include/AudioDummy.h:51-54`) — which is what the harness selects.

Harness: `/tmp/autosave-lane/recovery-run.sh` — a throwaway `HOME`, a config with
`app/configured=1`, `audioengine/audiodev="Dummy (no sound output)"` and `ui/saveinterval=1`,
and `xvfb-run` (a real X server; the `offscreen` QPA platform made no difference). The run ends
with `SIGKILL` — the crash this feature is about, with no `closeEvent` and no cleanup.

### Baseline binary (`md5 e42c030fd001`), scenario `fresh`

```
$ bash /tmp/autosave-lane/recovery-run.sh build/lmms fresh /tmp/autosave-lane/b-fresh tests/emptyproject.mmp 90
### lmms exit=137  (137 = SIGKILL after 90s = the crash)
### after the crash:
-rw-rw-r-- 1 kruzzzzy kruzzzzy 21399 2026-09-11 21:55:52.337456579 +0100 recover.mmp
### sidecar (.../recover.mmp.info):
  (absent - upstream behaviour)
### every recovery mtime over the run:
  ... 21:55:49 recovery_absent
      21:55:54 recovery_mtime=2026-09-11     <- first write, ~63 s after launch
```

So a crash loses at most the configured interval: launched 21:54:49, written 21:55:52, with
`saveinterval=1` (the shortest the setting accepts; the shipped default is 2 minutes). The file is
a real project document (`creatorversion="0.1.0-alpha"`).

### Baseline binary, scenario `stale` — the gap

`recover.mmp` from 2026-01-01 naming this very project, whose file on disk was saved 2026-06-01:

```
### before launch:
-rw-rw-r-- 1 kruzzzzy kruzzzzy 15949 2026-01-01 00:00:00.000000000 +0000 recover.mmp
### lmms exit=137
### after the crash:
-rw-rw-r-- 1 kruzzzzy kruzzzzy 15949 2026-01-01 00:00:00.000000000 +0000 recover.mmp   <- unchanged
### recovery-line stderr:
  (none)
```

Every sample over 75 s shows the same mtime: the process never got past the modal prompt, so the
autosave timer never even started, and a command-line project was never reached. That is the bug —
an eight-month-old file is presented as "the project of this session".

## 3. What this branch adds

New product source, both registered in `tests/fork-sources.txt`:

* `include/ProjectRecovery.h`, `src/core/ProjectRecovery.cpp` — a nested namespace
  `lmms::ProjectRecovery` holding pure data types (`RecoveryInfo`, `RecoveryDecision`,
  `RecoveryVerdict`), the sidecar reader/writer, and **the decision as a pure function**
  `decideRecovery(const RecoveryInfo&, const QString& projectBeingOpened)`. No GUI, no audio, no
  file access in the decision itself.
* `tests/src/core/ProjectRecoveryTest.cpp` (+ `tests/CMakeLists.txt`) — 28 assertions, headless
  (`QTEST_GUILESS_MAIN`), including the two negative controls below.

Changed inherited files (declared in `tests/upstream-modifications.txt` in the same commit):

* `src/gui/MainWindow.cpp` — `autoSave()` writes `recover.mmp.info` beside the recovery file after
  a successful save, recording the source project path and the UTC time; `sessionCleanup()` removes
  both files (leaving the sidecar behind would let the next launch decide about a file that is
  gone).
* `src/core/main.cpp` — the startup prompt is now gated by `decideRecovery()`, names the project
  the recovery belongs to, and prints one line to stderr when a recovery file on disk is refused,
  so a bug report can show why no dialog appeared.

Rules, in the order the function applies them:

| verdict | when | offered |
|---|---|---|
| `NotPresent` | no recovery file | no |
| `Empty` | present but zero bytes (a truncated write) | no |
| `OtherProject` | it names a project different from the one this launch is opening | no |
| `Stale` | the project file it came from is **at least as new** (mtime `<=`) | no |
| `Offer` | otherwise, including **no sidecar at all** (an upstream-written recovery) | yes |

The last row is the behaviour-preserving one: not knowing which project a recovery is must not
throw away a user's unsaved session, so a legacy recovery file is offered exactly as before.

## 4. Proven

**The decision, headless.** `tests/src/core/ProjectRecoveryTest.cpp`:

```
$ cd build/tests && ./ProjectRecoveryTest > /tmp/autosave-lane/test-green3.log 2>&1; echo "TEST_EXIT=$?"
TEST_EXIT=0
Totals: 28 passed, 0 failed, 0 skipped, 0 blacklisted, 6ms
```

**Negative controls — the safety properties are load-bearing, not asserted.** Inverting one
comparison in the production source and rebuilding only this test target turns the relevant cases
red; restoring the file and rebuilding turns them green again.

```
# mutation 1: staleness comparison `<=` -> `>`
MAYBE-REVERTED
22 passed, 6 failed, 0 skipped, 0 blacklisted            MUTATED_TEST_EXIT=6
FAIL!  : aRecoveryOlderThanTheProjectIsNotOffered() Compared values are not the same
FAIL!  : aRecoveryAsNewAsTheProjectIsNotOffered() Compared values are not the same
FAIL!  : readingRealFilesRefusesAStaleRecovery() Compared values are not the same
# mutation 2: project-identity comparison `!=` -> `==`
20 passed, 8 failed, 0 skipped, 0 blacklisted            MUTATED_TEST_EXIT=8
FAIL!  : aRecoveryOfAnotherProjectIsNotOfferedAsRecoveryOfThisOne() Compared values are not the same
FAIL!  : readingRealFilesRefusesARecoveryOfAnotherProject() Compared values are not the same

$ cp /tmp/autosave-lane/ProjectRecovery.cpp.orig src/core/ProjectRecovery.cpp   # restored
$ diff <(md5sum < src/core/ProjectRecovery.cpp) <(md5sum < /tmp/autosave-lane/ProjectRecovery.cpp.orig)
RESTORED-IDENTICAL
$ cd build/tests && ./ProjectRecoveryTest; echo $?
0
```

Two of the mutations' red cases are file-system `QTemporaryDir` cases with real mtimes, and two are
hand-built inputs; the freshness test compares *instants*, so a clock in a different zone does not
flip it.

**The build and the suite, unpiped.** `tools/local-ci.sh` (the CI `linux-x86_64` job's exact
`CMAKE_OPTS`; the script is not executable in git — mode `100644` — so it is invoked as
`bash tools/local-ci.sh`, and it prints its one deviation itself: `-DWANT_QT6=ON`, because this box
has no Qt5 development files):

```
$ JOBS=4 bash tools/local-ci.sh --build-dir build --jobs 4 > /tmp/autosave-lane/local-ci-final3.log 2>&1; echo EXIT=$?
configure EXIT=0
build     EXIT=0
ctest     EXIT=0     (from build/tests; the top-level dir reports 0 tests)
ctest totals: 100% tests passed, 0 tests failed out of 26
EXIT=0
```

Baseline on the same box and command, at the base commit, for comparison: `configure 0`,
`build 0`, `ctest 0`, **25/25 tests** — this branch adds the 26th. The other six matrix jobs are
not reproducible here (`linux-arm64`, `mingw64`, both `macos-*`, `msvc-x64`, `windows-arm64`).

**Live: the write half, and the new side file** (same harness, changed binary
`md5 d8191d4de6ac`):

```
### after the crash:
-rw-rw-r-- 1 kruzzzzy kruzzzzy 21399 2026-09-11 22:32:24.315512661 +0100 recover.mmp
-rw-rw-r-- 1 kruzzzzy kruzzzzy   155 2026-09-11 22:32:24.319612175 +0100 recover.mmp.info
$ cat .../recover.mmp.info
# zene-studio autosave identity - a side file, never part of a project
version=1
project=/tmp/autosave-lane/f-fresh/Mine.mmp
savedUTC=2026-09-11T21:32:24Z
```

**Live: the stale recovery is refused and the session proceeds** (the same inputs that hung the
baseline):

```
$ bash /tmp/autosave-lane/recovery-run.sh build/lmms stale /tmp/autosave-lane/f-stale tests/emptyproject.mmp 75
### recovery-line stderr:
zene: not offering recovery file .../recover.mmp: recovery of 'Mine.mmp' is not newer than the saved project (2026-01-01T00:00:00Z <= 2026-05-31T23:00:00Z)
### every recovery mtime over the run:
  ... 22:33:52 recovery_mtime=2026-01-01
      22:33:57 recovery_mtime=2026-09-11     <- the timer ran and autosaved, ~60 s after launch
```

**Live: a recovery of a different project does not hijack the project named on the command line**:

```
$ bash /tmp/autosave-lane/recovery-run.sh build/lmms other /tmp/autosave-lane/n-other tests/emptyproject.mmp 75
zene: not offering recovery file .../recover.mmp: recovery belongs to 'SomeoneElses.mmp', not to '/tmp/autosave-lane/n-other/Mine.mmp'
  (this run is from the same harness on the pre-refactor binary; re-verified after the refactor by the
   stale case above, which exercises the same startup path)
```

(LMMS itself renames the file it is about to overwrite to `recover.mmp.bak`, so the refused
recovery is not destroyed — it is on disk as `recover.mmp.bak` in both scenarios.)

**The quality gates.** `no-upstream-regression-gate` PASS (31 files in the ledger, every change to
inherited code declared), `file-length-gate` PASS, `duplication-gate` PASS, `no-tautology-gate`
PASS, `complexity-gate` PASS (CCN ≤ 10 per method — the first version of `parseRecoveryIdentity`
and `decideRecovery` was 13 and 11 and **failed this gate**; both were refactored into small
predicates rather than added to the baseline). `complexity-gate.sh` rewrites
`tests/complexity-baseline.tsv` cosmetically whenever it runs; that rewrite was reverted so this
slice carries no unexplained baseline change, and the gate passes with the committed baseline
either way.

## 5. Not proven, and the limits worth knowing

* **The dialog itself was not exercised.** The GUI prompt is a modal `QMessageBox` and nothing
  headless clicks a button. What is proven is the decision (28 headless assertions) and the
  startup path around it (the refusal line and the app proceeding, above). The Recover/Discard
  buttons call the same code they did before.
* **The gate only bites on recovery files this build wrote.** An upstream-written `recover.mmp`
  has no side file, so it is offered unconditionally — deliberately, to avoid discarding a user's
  session. The safety property therefore does not retroactively protect files written by the
  shipped alpha's own build.
* **Freshness is mtime-based.** A clock jump, or a project file on a network share with coarse
  timestamps, can make a live recovery look stale (or a stale one look live). Equal timestamps are
  treated as stale on purpose: the saved project then already holds at least as much.
* **A crash during playback can still lose more than the interval.** `ui/enablerunningautosave`
  defaults to `0`, so while playing the timer defers (retrying every 10 s) and only writes once
  playback stops. That is upstream behaviour and this branch does not change the default — a user
  who wants the tighter window can enable "Allow autosave while playing" in
  Edit → Settings → Performance. The default is left alone because saving during playback is a
  disk write on the audio-adjacent path, and changing a shipped default is not behaviour-preserving.
* **One configuration only.** Everything above is Linux x86_64, gcc 13, Qt 6.4.2, `WANT_QT6=ON`;
  the other six CI jobs are unverified here. `QSaveFile` (used for the sidecar) is cross-platform,
  but that is an argument, not a run.
* **No coverage gate run.** `tests/coverage-gate.sh` needs a full instrumented build and its
  baseline is per-file; it was not run, so the new files have no coverage record yet (the same
  situation `include/LatencyCompensation.h` was in when it was added to the scope file).

## 6. Files and commits

Product-new (in `tests/fork-sources.txt`): `include/ProjectRecovery.h`,
`src/core/ProjectRecovery.cpp`, `tests/src/core/ProjectRecoveryTest.cpp`.

Inherited and declared (`tests/upstream-modifications.txt`): `src/core/main.cpp`,
`src/gui/MainWindow.cpp`.

Build wiring: `src/core/CMakeLists.txt`, `tests/CMakeLists.txt`.

Commits on `post-alpha/autosave`:

* `6bd375916` — `feat(autosave): gate the recovery prompt on project identity and freshness`
  (the change, the test, both ledgers and the build wiring in one commit: an inherited file's
  declaration ships with the change, not after it).
* the tip of the branch — `docs(autosave): what recovery already exists, what was added, what is
  proven` (this file).

Nothing was pushed and no remote was touched.

## Appendix: reproducing the headless runs

The harness itself is **not committed** (it is throwaway evidence tooling, not product), so the
configuration it needs is recorded here instead. In a throwaway `HOME`:

```xml
<?xml version="1.0"?>
<!DOCTYPE lmms-config-file>
<lmms version="1.3.0" configversion="3">
  <paths workingdir="__WORK__/" gigdir="__WORK__/samples/gig/" sf2dir="__WORK__/samples/soundfonts/"
         vstdir="__WORK__/plugins/vst/" ladspadir="__WORK__/plugins/ladspa/" theme="data:/themes/default/"/>
  <ui enableautosave="1" enablerunningautosave="0" saveinterval="1"/>
  <app configured="1" openlastproject="0"/>
  <audioengine audiodev="Dummy (no sound output)"/>
</lmms>
```

```sh
export HOME=<throwaway> XDG_DOCUMENTS_DIR=$HOME/Documents XDG_CONFIG_HOME=$HOME/.config \
       XDG_DATA_HOME=$HOME/.local/share XDG_CACHE_HOME=$HOME/.cache XDG_RUNTIME_DIR=$HOME/rt
export QT_QPA_PLATFORM=xcb        # offscreen QPA also hangs here
# scenario "stale": a recovery of THIS project, older than the project file
mkdir -p "$HOME/Documents/lmms"
cp tests/emptyproject.mmp "$HOME/Documents/lmms/recover.mmp"
printf 'version=1\nproject=%s/Proj.mmp\nsavedUTC=2026-01-01T00:00:00Z\n' "$PWD" > "$HOME/Documents/lmms/recover.mmp.info"
touch -d '2026-01-01 00:00:00' "$HOME/Documents/lmms/recover.mmp"
cp tests/emptyproject.mmp "$HOME/Proj.mmp"; touch -d '2026-06-01 00:00:00' "$HOME/Proj.mmp"

xvfb-run -a -s "-screen 0 1280x1024x24" \
  timeout -s KILL 75 build/lmms --config "$HOME/rc.xml" "$HOME/Proj.mmp" \
  > out.log 2>&1 ; echo "exit=$?   # 137 = SIGKILL = the crash"
ls -l --time-style=full-iso "$HOME/Documents/lmms/"
```

Three details are load-bearing and were each found the hard way: `app/configured=1` (otherwise the
first-run `SetupDialog` blocks), a valid `audioengine/audiodev` (otherwise the audio-failure dialog
blocks), and an explicit `paths/workingdir` (a config without `<paths>` makes `setWorkingDir("")`
collapse the working directory, after which `recoveryFile()` is the bare relative string
`recover.mmp` — a pre-existing trap worth its own fix, not fixed here).

