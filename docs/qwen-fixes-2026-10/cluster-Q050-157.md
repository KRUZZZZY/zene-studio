IMPLEMENTED

# cluster Q050-157 - Plug-in descriptor lifetime check

Unit: P1, size S, fix, area L10 (member `R-effects-8`). Branch `feat/050-Q050-157-oc`, base `050/m0-train-19`
(`5c1c6715d`). Logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-157.r1/logs`. The ASan build script is
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-157.r1/scratch/asan-reproducer.sh`. Feature record:
`docs/050-features/Q050-157.md`.

**Result: the ASan reproducer is clean, so no product code changed.** Rescans retire the cache-served descriptor store
and never free it (`src/core/PluginFactory.cpp:234`, `src/core/PluginFactoryBackgroundScan.cpp:363`). Retired stores
are kept until the factory is destroyed. The branch adds the reproducer as a regression test, a manual sentence and the
feature record.

Finding on the path: a cache-served descriptor reaches a live instance only through the instance's **key**
(`Plugin::instantiateWithKey`, `src/core/Plugin.cpp:212`). TripleOscillator ignores the key, so the brief's suggested
instrument fixture never holds a cache-served descriptor. My first version of the test showed this: the instance
descriptor was not the served one. Effects keep the key, so the reproducer uses the Amplifier effect.

## Acceptance

| # | Criterion | Test | Evidence (command / exit) |
|---|---|---|---|
| 1 | Run the ASan cache-served-load/rescan/save reproducer and establish a failing or clean result | `PluginScanCacheTest::testCacheServedEffectSurvivesRescanAndSave` | ASan binary built by `asan-reproducer.sh` (`build-asan-4.log`, EXIT 0). `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1:use_sigaltstack=0 ./PluginScanCacheTest-asan testCacheServedEffectSurvivesRescanAndSave` gave EXIT **0**, `3 passed` and no ASan report (`test-asan-9.log`). The whole binary under ASan gave EXIT **0**, `19 passed, 0 failed` (`test-asan-8.log`). The test checks `servedFromCache == 1`, `scanned == 0` and `effect->key().desc == served`. It then runs `plugin.rescan`, then `plugin.scan_start` until `outcome == "done"`, and checks the factory now has a different descriptor. It reads the live key's name, display name, description, author and logo, saves with `project.save` (legacy) and checks the file holds the effect. The project reloads onto the current descriptor, and `plugin.rescan` runs again after release. **Clean.** |
| 1b | The reproducer detects the defect it looks for | same, mutated | I replaced the retirement in `discoverPlugins()` with a free, rebuilt the ASan binary (`build-asan-mut-2.log`, EXIT 0) and ran the slot. EXIT **1**: `ERROR: AddressSanitizer: heap-use-after-free`, read in the test slot, `freed by thread T0 ... lmms::PluginFactory::discoverPlugins()`, allocated in `descriptorFromCacheRecord` (`test-asan-mut-2.log`). Then I restored the source (`git diff --stat src` empty), rebuilt and reran: EXIT **0** (`test-asan-9.log`). |
| 2 | If it fails, retain descriptor ownership until all instances release it and rerun the reproducer | n/a (criterion 1 is clean) | The ownership already exists, so there was nothing to change. In the normal build, `ctest -R '^PluginScanCacheTest$'` from `build/tests` gave EXIT **0**, 19/19 (`test-8.log`). |

ASan note: the lane guard refuses `cmake` configure (`bunny-guard: configuring is the parent's job`,
`configure-asan-1.log`, EXIT 97). So the ASan binary is the normal test relinked with the ASan runtime. Five units are
instrumented: the test, `Plugin.cpp`, `PluginFactory.cpp`, `Effect.cpp` and `EffectChain.cpp`. Every allocation and
free in the process is intercepted. ASan also needs `use_sigaltstack=0` here, because `CrashReporter`'s per-thread
`thread_local` alt stack (`src/core/CrashReporter.cpp:100`) makes ASan's thread-exit teardown abort (`test-asan-1.log`).
That is a tool interaction, documented in the feature record.

## Self-check

`python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> Q050-157` (`submit-check-2.log`, EXIT 1) passes
commits, clean tree, report, feature record, manual and tests. It WARNs on GUI/agent (no new action, see below), and
its only FAIL is "gates (feature_check.py)" with an empty detail. That FAIL is the PermissionError described next, not
a gate result. The
`feature_check.py` gates, run with only the log directory redirected (the script writes to
`<worktree>/../zene-feat-q050-157-feature-check`, outside this lane's write jail, so the original fails with
PermissionError; `feature-check-1.log`), all PASS. The rows are fork-sources, all-sources-reproduce,
no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity and file-length. EXIT 0 before the docs
commit (`feature-check-2.log`) and again after it (`feature-check-3.log`).

- **Each new test FAILS with the product change reverted, then PASSES restored.** There is no product change (the
  reproducer is clean), so there are no hunks to reverse. Instead I ran the equivalent proof by mutation: freeing the
  retired store made the new test fail under ASan with EXIT 1 (heap-use-after-free). Restored, it passes with EXIT 0.
  See 1b above.
- **Every acceptance criterion is proven by a test I ran.** Criterion 1: `PluginScanCacheTest-asan
  testCacheServedEffectSurvivesRescanAndSave`, EXIT 0, and the full binary, EXIT 0. Criterion 1b: mutated, EXIT 1.
  Criterion 2: not triggered; the regression test in ctest gives EXIT 0.
- **One undo step / reversibility.** No command was added or changed, and nothing edits the project.
  `plugin.rescan` and `plugin.scan_start` keep their existing `irreversible` class. No snapshot regeneration was needed
  (no metadata change), and the doc-figures gate passes.
- **Audio thread.** No product code changed. Scans and background commits (the only code that creates or retires
  descriptor stores) run on the main thread. TSan does not run on this box; this is reasoned from the code.
- **Old projects / round-trip.** No format change. The test saves through `project.save` (legacy) and reloads the file,
  and the reloaded effect is the amplifier on the current descriptor.
- **Portability.** The new test code uses only Qt 5/6-common API (`QJsonObject`, `QTRY_VERIFY_WITH_TIMEOUT`,
  `QTemporaryDir`), no `std::filesystem`, and no reserved identifiers. The module file name is `amplifier.dll` under
  `LMMS_BUILD_WIN32`, and the slot `QSKIP`s on Windows hosts like the rest of the suite.
- **GUI and agent path.** No new user action. The existing GUI items (Plug-in Modules / Plug-in Manager -> Rescan,
  Rescan in Background, `src/gui/CommandLists.cpp:131-132,148-149`) dispatch the registered `plugin.rescan` and
  `plugin.scan_start`, which the test invokes through `ControlRegistry`. `PluginQuarantineMenuTest:136-138` already
  triggers the GUI Rescan action. submit_check's GUI/agent WARN rows come from this: no GUI or command file changed.

## Known limits

- Retired stores are kept until the factory is destroyed, so memory grows by one small store per rescan. This is the
  patrol finding the brief cites and was not changed (freeing would need reference counting across instance keys,
  browser rows and drag-and-drop keys).
- The ASan build instruments five units, not the whole tree.
- The Windows test host skips the slot (it cannot load plug-in modules).
