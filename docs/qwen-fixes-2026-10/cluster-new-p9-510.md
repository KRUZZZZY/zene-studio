FIXED

# Cluster new-p9-510: editor open can crash when no error string is supplied

## Finding

Luna BUG 5: `Vst3EditorSession::open(void*, QString* error)` wrote the refusal reason through `error` with no null
check. The finding's line 179 does not match this branch: at the base `c6ee4fc7bc` line 179 is the `m_fds` member. The
defect is in `open()`, where the unguarded writes were at base lines 195 (no edit controller), 203 (no native window),
209 (no editor view), 215 (cannot embed) and 227 (attach refused).

## Verdict and evidence

**Real, and fixed on this branch.**

Before the fix (base `c6ee4fc7bc`, `plugins/Vst3Effect/Vst3EditorSession.cpp`):

- `:195` `*error = QStringLiteral("the plug-in has no edit controller");`
- `:203` `*error = noPluginWindowSystemError();`
- `:209`, `:215`, `:227`: the same unguarded write for the other three refusals.

The test shows it: with `error == nullptr`, the first refusal segfaults (`test-red-1.log`, the backtrace ends in
`Vst3EditorSession::open(void*, QString*)`).

After the fix, every refusal goes through one null-safe helper:

- `plugins/Vst3Effect/Vst3EditorSession.cpp:66-70` `refuseOpen(QString* error, const QString& why)`: writes only when
  `error != nullptr`, then returns false. This is the only remaining `*error` write in the file, and it is guarded.
- `:202` no edit controller, `:209` no native window (`noPluginWindowSystemError()`), `:214` no editor view, `:219`
  cannot embed (message text unchanged, still names the platform and platform type), `:230` attach refused. The
  refusal strings are byte-identical to the base strings.
- `plugins/Vst3Effect/Vst3EditorSession.h:64` documents that `error` may be null.

Success path, `isOpen()` early return and `applyScale()` are unchanged.

## Changes committed

- `plugins/Vst3Effect/Vst3EditorSession.cpp`: `refuseOpen` helper; the five `open()` refusals routed through it.
- `plugins/Vst3Effect/Vst3EditorSession.h`: comment only, the `error` may-be-null contract.
- `tests/src/plugins/Vst3HostTest.cpp`: new test `testEditorRefusalWithoutAnErrorStringDoesNotCrash`, plus two fakes
  (`RefusingPlugView`, `ViewingController`) and the includes `Vst3EditorSession.h` and `pluginterfaces/gui/iplugview.h`.
- `tests/upstream-modifications.txt`: cluster reason appended to the existing `Vst3HostTest.cpp` line (the ledger's convention is one line per path with reasons appended).
- `docs/qwen-fixes-2026-10/cluster-new-p9-510.md`: this report.

## Regression test matrix

| Path | Covered by | Null error | Named reason |
| --- | --- | --- | --- |
| No edit controller (`HostedPlugin` unloaded, and `Vst3EditorSession(nullptr)`) | yes | refuses, no crash | "no edit controller" |
| No native window (`PluginWindowSystem::None`) | yes | refuses, no crash | "no native window a plug-in editor can embed in" |
| `createView` returns nothing | yes (`MappingController`, which returns nullptr) | refuses, no crash | "offers no editor view" |
| View cannot embed (`isPlatformTypeSupported` false) | yes (`RefusingPlugView(false, true)`) | refuses, no crash | "cannot embed on" |
| Attach refused (`attached` not kResultOk) | yes (`RefusingPlugView(true, false)`) | refuses, no crash | "refused to attach" |
| Success path with a real view | not added here; existing `Vst3EditorTest` (7 tests) covers it | n/a | n/a |

The test uses no wall clock, sleeps or threads. Its inputs are injected: a stand-in window pointer (`0x2a`, never
dereferenced by the fakes), `setWindowSystem()` to fix the platform type, and fake controllers and views whose
answers are fixed in their constructors. Each refusal is run twice, once with `nullptr` and once with a string.

## Evidence and commands

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-510.r3/logs/` (named below without the path).

| Step | Command (run from the worktree or `build/tests`) | Exit | Log |
| --- | --- | --- | --- |
| Build, first try | `cmake --build build --target zene Vst3HostTest -j4` | 2 (ccache could not write its temp file: permission denied) | `build-red-1.log` |
| Build, retry with ccache temp on disk | `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp cmake --build build --target zene Vst3HostTest -j4` | 0 | `build-red-2.log` |
| **RED** (fix not applied, product files at base) | `cd build/tests && ctest -R '^Vst3HostTest$' --output-on-failure` | **8** (SIGSEGV in `Vst3EditorSession::open`, `testEditorRefusalWithoutAnErrorStringDoesNotCrash`) | `test-red-1.log` |
| Build with fix | `CCACHE_TEMPDIR=... cmake --build build --target zene Vst3HostTest -j4` | 0 | `build-green-1.log` |
| **GREEN** | `cd build/tests && ctest -R '^Vst3HostTest$' -V` | **0** (`PASS testEditorRefusalWithoutAnErrorStringDoesNotCrash`; `Totals: 23 passed, 0 failed`) | `test-green-2-verbose.log` |
| 4b: save the fix | `git diff -- plugins/Vst3Effect/Vst3EditorSession.cpp plugins/Vst3Effect/Vst3EditorSession.h > fix.patch` | 0 | `fix.patch` |
| 4b: revert the fix | `git apply -R fix.patch`, rebuild (`build-4b-revert-1.log`), `ctest -R '^Vst3HostTest$' -V` | apply 0, build 0, **ctest 8** (SIGSEGV) | `test-4b-revert-1.log` |
| 4b: reapply the fix | `git apply fix.patch`, rebuild (`build-4b-reapply-1.log`), `ctest -R '^Vst3HostTest$' -V` | apply 0, build 0, **ctest 0** (23 passed) | `test-4b-reapply-1.log` |
| Family build | `cmake --build build --target Vst3BusMapTest Vst3ChunkProbeTest Vst3EffectIntegrationTest Vst3EditorTest Vst3InstrumentFixtureProbe Vst3InstrumentTest Vst3InstrumentIntegrationTest -j4` | 0 | `build-family-1.log` |
| Family run | `cd build/tests && ctest -R 'Vst3' -V` | **0**, 8 of 8 tests passed | `test-family-1.log` |

Family test count: 8 ctest entries executed (`ctest-list-vst3.log`). Function-level totals: Vst3BusMapTest 10,
Vst3HostTest 23, Vst3ChunkProbeTest 10, Vst3EffectIntegrationTest 7, Vst3EditorTest 7, Vst3InstrumentTest 15,
Vst3InstrumentIntegrationTest 17, 89 in all. Vst3InstrumentFixtureProbe reports a pass through ctest only; it prints no per-function total.

Gates:

| Gate | Command | Exit | Log |
| --- | --- | --- | --- |
| `feature_check.py` as written | `python3 .../qwen/feature_check.py <worktree>` | 1: `PermissionError` creating `<worktree>-feature-check/` in the projects directory, outside the write-jail. The gate itself did not run. | `feature-check-1.log` |
| `feature_check.py` `main()` with only the log directory redirected into the run folder (driver `fc-driver.py`, the script unmodified) | `python3 fc-driver.py <worktree>` | **0, RESULT PASS** on the commit-candidate tree | `feature-check-3.log` |

The 8 gate rows, all PASS: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace,
no-tautology, doc-figures, complexity (no new item in changed files), file-length (no new item in changed files).
An earlier run before the ledger edit (`feature-check-2.log`) also passed. The ledger edit was made after that run, so the gate was re-run.

`submit_check.py` before commit (`submit-check-1.log`, exit 1): the FAILs for commits and clean tree are resolved by this
commit. Its gate step runs `feature_check.py`, so it hits the same write-jail `PermissionError` as above.

## Self-check

- **Regression test fails without the change and passes with it.** RED: `test-red-1.log`, ctest exit 8, SIGSEGV in
  `open()`. Reverted via `git apply -R fix.patch`: exit 8 again (`test-4b-revert-1.log`). Reapplied: exit 0, 23 passed
  (`test-4b-reapply-1.log`).
- **Deterministic.** No timing, sleep or thread. Injected: a fixed window pointer, `setWindowSystem()` to fix the
  platform type, and fake controllers/views whose answers are fixed by their constructors.
- **Fix at the cause, and complete.** All five unguarded `*error` writes in `open()` now go through `refuseOpen`.
  Callers checked: `HostedPlugin::openEditor` (`Vst3EditorSession.cpp`, the wrapper near the end of the file) passes `error`
  through unchanged and does no write of its own. The existing `Vst3EditorTest` callers pass a real string and still
  pass (7/7).
  - **Sibling not changed, flagged.** `plugins/ClapEffect/ClapHostGui.cpp:66,71,112,120,128,138` has the same
    unguarded `*error` pattern in the CLAP editor. The finding names only the VST3 file, so I left CLAP alone. It
    needs a separate cluster or Luna's decision.
- **No collateral damage.** Refusal strings are unchanged. The success path and `applyScale()` are untouched. The header
  change is a comment. Old projects and saved data are not involved: this is the editor-open control path, not
  project load.
- **Verdict is FIXED, not ALREADY-FIXED or FALSE-POSITIVE.** The defect was present at base `c6ee4fc7bc` (above), so no
  "already fixed" citation is claimed.
- **Realtime, undo, commands.** `open()` is a UI/control-thread path. `refuseOpen` writes only on refusal, and that
  is not on the audio thread. No new allocation on the audio path. No undo step. No control command added.
- **Registration.** `plugins/Vst3Effect/Vst3EditorSession.cpp` and `.h` are already in `tests/fork-sources.txt` (lines
  1301-1302). `tests/src/plugins/Vst3HostTest.cpp` is an inherited file with an existing ledger line; my cluster reason
  is appended to it.
- **Not checked.** Only Linux (offscreen Qt) was built and run. macOS and Windows were not built. The test passes
  `PluginWindowSystem::X11` explicitly, and the fakes do not depend on platform, so I expect no platform difference,
  but that is unverified.
- **Not run.** `bash tests/run-all-gates.sh` (the full gate suite). Only the `feature_check` set and the Vst3 family
  were run.

## Working-tree note

Before this run the worktree had an unmerged `plugins/Vst3Effect/Vst3EditorSession.cpp` (conflict markers left by
the previous session: a `refuseOpen` helper on one side, the Q050-490 `platformTypeOf` on the other). I saved a copy
to the log folder (`prev-attempt-conflicted-Vst3EditorSession.cpp.txt`), reset the file to base, and wrote the fix
from there. The previous session's `refuseOpen` idea is kept; its X11-only refusal message is replaced by the
platform-specific one from base.
