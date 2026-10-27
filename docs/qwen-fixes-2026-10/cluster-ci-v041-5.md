FIXED

# Cluster ci-v041-5 - mingw64: SYNC_WITH_SHM_FIFO redefined, RemotePluginBase dllimport mismatch

Branch `bugs/qwen-ci-v041-5`, base `5c1c6715d9` (050/m0-train-19). Round 1 (the round-0 attempt left no
commits on this branch, so this is the full change).

## Verdict: REAL - two build-configuration defects, both in the test-only targets

Both errors come from `tests/CMakeLists.txt`, which compiles `src/common/RemotePluginBase.cpp` outside
`lmmsobjs`:

1. **`"SYNC_WITH_SHM_FIFO" redefined`.** `RemotePluginShmFifoTest` and `RemotePluginShmFifoCompileCheck` pass
   `SYNC_WITH_SHM_FIFO` on the command line (`tests/CMakeLists.txt:2582`, `:2593`; CMake makes that `1`). On a host
   without `<sys/ipc.h>`/`<semaphore.h>` (Windows), `include/RemotePluginBase.h:48-50` defined it again, as
   empty. That is an incompatible redefinition, which `-Werror` turns into an error. On Linux the branch is not
   taken, so this box never saw it.
2. **`redeclared without dllimport attribute`.** `RemotePluginShmFifoCompileCheck` did not set
   `LMMS_STATIC_DEFINE` and is not `lmmsobjs`. So the generated `lmms_export.h` takes its consumer branch, and on
   Windows `LMMS_EXPORT` becomes `__declspec(dllimport)`. `RemotePluginBase.cpp` then *defines* the exported
   members out of line: the constructor at `:57`, the destructor at `:88` and `frameFromInts` at `:102`. These are
   exactly the three CI lines. On ELF both branches are `visibility("default")`, so Linux cannot see the
   difference.

Sibling compile sites checked, and why they are unaffected:
- `lmmsobjs` defines `lmmsobjs_EXPORTS` (`src/CMakeLists.txt:347-349`), which gives dllexport.
- `RemoteVstPlugin` (`plugins/VstBase/RemoteVstPlugin/CMakeLists.txt:67`) and `RemoteZynAddSubFx`
  (`plugins/ZynAddSubFx/CMakeLists.txt:178`) set `BUILD_REMOTE_PLUGIN_CLIENT`. That makes `LMMS_EXPORT` empty
  (`include/RemotePluginBase.h:66-68`).
- `RemotePluginShmFifoTest` already had `LMMS_STATIC_DEFINE`.
- No other file defines `SYNC_WITH_SHM_FIFO` (`rg "define SYNC_WITH_SHM_FIFO|PRIVATE.*SYNC_WITH_SHM_FIFO"`).

## Fix (smallest, platform-neutral, no behaviour change on any host)

- `include/RemotePluginBase.h:49-51`: the no-`sys/ipc` fallback defines `SYNC_WITH_SHM_FIFO` only under `#ifndef`.
  Every product build is unchanged: none of them predefines the macro, and on Linux the branch is not taken.
- `tests/CMakeLists.txt:2591-2593`: `RemotePluginShmFifoCompileCheck` gets `LMMS_STATIC_DEFINE`. That is the same
  static branch its sibling `RemotePluginShmFifoTest` already uses, and the precedent at
  `tests/CMakeLists.txt` `Vst3EffectIntegrationTest` (`PLUGIN_STATIC_DEFINE`, msvc C2491). The target is an
  OBJECT compile check that nothing links, so nothing that imports the symbols changes.

No Qt-6-only API and no Windows macro identifiers were added. Nothing touches realtime paths, undo, commands or
saved data.

## Regression (tests/src/core/RemotePluginShmFifoTest.cpp + RemotePluginShmFifoNoSysIpcProbe.cpp)

- `RemotePluginShmFifoNoSysIpcProbe` is a new OBJECT library linked into `RemotePluginShmFifoTest`. It compiles
  `RemotePluginBase.h` against a generated shadow `lmmsconfig.h` that `#undef`s `LMMS_HAVE_SYS_IPC_H`. A plain
  `#undef` in the TU is not enough: `lmmsconfig.h` has no include guard, and the header re-includes it. So this
  box takes the Windows header branch with the build's `SYNC_WITH_SHM_FIFO`. The tests build with `-Werror`, so
  without the fix the build stops with the CI's own message. The test slot
  `headerKeepsTheBuildsShmFifoDefinitionWithoutSysIpc` also checks at runtime that the macro is still `1`.
- `targetsCompilingRemotePluginBaseUseTheStaticExportBranch`: `file(GENERATE)` records
  `$<IN_LIST:LMMS_STATIC_DEFINE, COMPILE_DEFINITIONS>` for both targets that compile `RemotePluginBase.cpp`, and
  the test requires 1 for each. GCC/ELF cannot observe dllimport, so this checks the configuration that decides
  the export branch.

## Evidence (logs in `~/.local/share/zene-bunny-fix/qwen/runs/ci-v041-5.r1/logs/`)

ccache could not write its temp dir under the write-jail, so builds ran with `CCACHE_DISABLE=1`.

| Step | Command | Result |
| --- | --- | --- |
| RED (before fix) | `cmake --build build --target RemotePluginShmFifoTest -j4` (05-build-red.log) | EXIT=2: `include/RemotePluginBase.h:49:9: error: 'SYNC_WITH_SHM_FIFO' redefined [-Werror]` |
| RED, dllimport half (earlier build, probe not yet effective) | `./RemotePluginShmFifoTest` | `FAIL! targetsCompilingRemotePluginBaseUseTheStaticExportBranch` Actual `ZENE_SHM_FIFO_COMPILE_CHECK_IS_STATIC_EXPORT`: 0 |
| GREEN | build `RemotePluginShmFifoTest RemotePluginAudioPortsTest` (06), `ctest -R '^RemotePluginShmFifoTest$\|^RemotePluginAudioPortsTest$'` (07) | build EXIT=0; ctest EXIT=0, 100% of 2 |
| 4b revert | `git apply -R logs/fix.patch` | EXIT=0 |
| 4b reverted build/test (stale binary removed first) | build (10), `ctest -R '^RemotePluginShmFifoTest$'` (11) | build EXIT=2 (`redefined [-Werror]`); ctest EXIT=8, Not Run, 0% of 1 |
| 4b header half only | `git apply --include=include/RemotePluginBase.h logs/fix.patch`, build (12), ctest (13) | apply EXIT=0, build EXIT=0, ctest EXIT=8: `Actual (ZENE_SHM_FIFO_COMPILE_CHECK_IS_STATIC_EXPORT): 0`, 10 passed 1 failed |
| 4b reapply | `git apply --include=tests/CMakeLists.txt logs/fix.patch`; `git apply -R --check logs/fix.patch` | EXIT=0; EXIT=0 (whole patch present) |
| 4b reapplied build/test | build `zene RemotePluginShmFifoTest RemotePluginAudioPortsTest` (14), ctest (15) | build EXIT=0; ctest EXIT=0, 100% of 2; binary `Totals: 11 passed, 0 failed` |
| Header dependents | build `FakeRemotePluginClient RemotePluginClientE2ETest vstbase vestige vsteffect zynaddsubfx NativeLinuxRemoteVstPlugin64` (16) | EXIT=0, 0 errors |
| Family | `ctest -R 'RemotePlugin'` (17) | EXIT=0, 100% of 3 |
| Gates | `feature_check.py <worktree>` (19, 20) | EXIT=0, 8/8 PASS (see Self-check) |

Not checked here: Windows/mingw/MSVC and Qt 5.15 cannot be built on this box. The owner's push to a `ci/*`
branch will confirm the fix on hosted CI. MSVC reports the same redefinition as warning C4005, and the guard removes it there
too.

## Self-check

- **Gates.** `feature_check.py` exits by creating `<worktree>/../<name>-feature-check`, which is outside this
  lane's write-jail (`PermissionError`, log 18, GATE_EXIT=1). I ran the same script unchanged except for its log
  directory, which I pointed at `logs/feature-check` (an exec wrapper; logs 19, 20). Result: 8/8 PASS, EXIT=0
  (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures,
  complexity, file-length).
- **Registration.** `tests/fork-sources.txt` adds the probe source and this report. `tests/upstream-modifications.txt`
  adds `include/RemotePluginBase.h` and `tests/CMakeLists.txt`, each with a reason.
- **Committed files.** `include/RemotePluginBase.h`, `tests/CMakeLists.txt`,
  `tests/src/core/RemotePluginShmFifoTest.cpp`, `tests/src/core/RemotePluginShmFifoNoSysIpcProbe.cpp`,
  `tests/fork-sources.txt`, `tests/upstream-modifications.txt`, `docs/qwen-fixes-2026-10/cluster-ci-v041-5.md`.
- **submit_check.py gates row (log 21: FAIL).** That row runs the unmodified `feature_check.py`, which dies on the same
  `PermissionError` before any gate runs: it cannot create `../zene-qwen-ci-v041-5-feature-check` (checked by
  re-running it directly). This is the lane's write-jail, not a gate failure. The gates themselves are 8/8 PASS,
  EXIT=0 (log 20, run after the report existed). The loop's own run, outside the jail, should see the same.
- **Fails reverted / passes fixed.** Without the product change (`git apply -R logs/fix.patch`, EXIT=0), the build
  fails (EXIT=2, `RemotePluginBase.h:49:9: 'SYNC_WITH_SHM_FIFO' redefined [-Werror]`) and ctest gives EXIT=8, Not
  Run. With only the header half back, ctest gives EXIT=8 on
  `ZENE_SHM_FIFO_COMPILE_CHECK_IS_STATIC_EXPORT: 0`. With the whole patch back, the build gives EXIT=0 and ctest
  EXIT=0 (2/2, 11/11 cases).
- **Deterministic.** Both checks are compile-time facts: the macro value seen through a shadow `lmmsconfig.h` that
  has no `LMMS_HAVE_SYS_IPC_H`, and `$<IN_LIST:LMMS_STATIC_DEFINE,...>` from `file(GENERATE)`. There is no timing,
  sleep or thread.
- **At the cause, siblings consistent.** Every compile site of `RemotePluginBase.cpp` was checked: `lmmsobjs`
  (dllexport), `RemoteVstPlugin`/`RemoteZynAddSubFx` (`BUILD_REMOTE_PLUGIN_CLIENT`, empty export),
  `RemotePluginShmFifoTest` (already static), and `RemotePluginShmFifoCompileCheck` (now static). The header has
  the only in-tree `#define SYNC_WITH_SHM_FIFO`. Every target that includes the header was rebuilt (logs 14, 16),
  and the `RemotePlugin` ctest family is 3/3.
- **No collateral damage.** No product build predefines `SYNC_WITH_SHM_FIFO`, so `#ifndef` changes nothing for
  them. On Linux the branch is not taken at all. `LMMS_STATIC_DEFINE` only affects an OBJECT compile check that
  nothing links. No data format, limit, command, undo or audio-thread path is touched.
- **ALREADY-FIXED / FALSE-POSITIVE.** Not applicable: the verdict is FIXED, and the RED build above reproduces the
  CI error on this box.
