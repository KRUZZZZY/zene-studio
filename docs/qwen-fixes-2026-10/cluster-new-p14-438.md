FIXED

# Cluster new-p14-438: a malformed plug-in factory can crash the host

## Finding

`plugins/ClapEffect/ClapLoader.cpp` stored any non-null `clap_plugin_factory_t*` without
checking its callbacks. A module whose factory has a null `get_plugin_count`,
`get_plugin_descriptor` or `create_plugin` passed the loader, and the scan
(`ClapHostLifecycle.cpp`, `listClasses`) and the instance path (`ClapHost.cpp`,
`HostedPlugin::load`) each called the null pointer.

Both callers reach the factory only through `loader::load`, so the check belongs in the loader.

## Evidence (current tree)

- Callers that call the factory unchecked:
  - `plugins/ClapEffect/ClapHostLifecycle.cpp:147-151` (scan: `get_plugin_count`, `get_plugin_descriptor`)
  - `plugins/ClapEffect/ClapHost.cpp:204, 207, 226` (instance: `get_plugin_count`, `get_plugin_descriptor`, `create_plugin`)
- The contract: `plugins/CarlaBase/carla/source/includes/clap/plugin-factory.h:14-35` declares all three
  callbacks in `clap_plugin_factory_t` without an optional marker. Carla's own loader checks all three
  before use (`plugins/CarlaBase/carla/source/discovery/carla-discovery.cpp:2418-2420`).
- The fix:
  - `plugins/ClapEffect/ClapLoader.h:80` adds `Code::FactoryInvalid`, placed before `Code::Count`. No existing value shifts.
  - `plugins/ClapEffect/ClapLoader.cpp:156` adds the token `factory-invalid` and its summary.
  - `plugins/ClapEffect/ClapLoader.cpp:176` adds `factoryCallbacksPresent()`, which requires all three callbacks non-null.
  - `plugins/ClapEffect/ClapLoader.cpp:316-322` refuses the factory with `FactoryInvalid` and the sentence
    `'<path>' exports an invalid clap.plugin-factory factory (a required callback is null)`, before the factory pointer is stored (`:323`).
  - The refusal follows the same pattern as the existing `EntryInvalid` check. The entry is already initialized at this point, so the caller's `Library` destructor (or `reset()`) runs `deinit()`, as it does for `FactoryMissing`.

## Regression test

- Fixtures: `tests/data/clap-test-plugin/clap-test-broken.c` gains a `NULL_FACTORY_COUNT`,
  `NULL_FACTORY_DESCRIPTOR` and `NULL_FACTORY_CREATE` mode. Each is a current `clap_entry`
  whose `init()` succeeds and whose factory is present with exactly one required callback null.
  The fixture build list is in `tests/data/clap-test-plugin/CMakeLists.txt`, and the paths and dependencies are wired in `tests/CMakeLists.txt` (`ClapLoaderErrorTest` block).
- Test: `tests/src/plugins/ClapLoaderErrorTest.cpp`, `testNullFactoryCallbackIsFactoryInvalid()` (`:339`).
  It checks, for each of the three modules: the ladder refuses with `factory-invalid` and keeps no factory
  (`library.factory == nullptr`); the instance path refuses with the same token and an "invalid" sentence; the scan path reports the same token and the same sentence.
- Determinism: no clock, sleep, thread or race. The three modules are fixed structural fixtures.
- Ordering: the ladder assertion runs first and calls nothing from the factory, so the unfixed build fails with a clean assertion rather than a crash.

## Commands and exit codes

Build logs and test logs are in `~/.local/share/zene-bunny-fix/qwen/runs/new-p14-438.r1/logs/`.
Builds ran with `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp/ccache-new-p14-438`: the default ccache temp dir
(`.../zene-bunny-fix/dsh/home/ccache-tmp-r6`) is not writable from this sandbox.

| Step | Command (run from the worktree unless noted) | EXIT | Log |
| --- | --- | --- | --- |
| RED build, attempt 1 | `cmake --build build --target ClapLoaderErrorTest -j2` | 2 | 01-build-red.log (ccache temp-file permission, not a test result) |
| RED build, attempt 2 | same, with CCACHE_TEMPDIR | 2 | 02-build-red.log (`-Werror` unused `null_factory_descriptor` in the descriptor fixture; fixed in the fixture) |
| RED build | same | 0 | 03-build-red.log |
| **RED test** (unfixed loader) | `cd build/tests && ctest -R '^ClapLoaderErrorTest$' --output-on-failure` | **8** | 04-test-red.log: `FAIL! testNullFactoryCallbackIsFactoryInvalid() '!loader::load(...)' returned FALSE` at `ClapLoaderErrorTest.cpp:360`; 16 passed, 1 failed |
| GREEN build | `cmake --build build --target zene ClapLoaderErrorTest ClapHostTest ClapEffectIntegrationTest ClapInstrumentViewTest ClapBusMapTest -j2` | 0 | 05-build-green.log |
| **GREEN test** | `cd build/tests && ctest -R '^ClapLoaderErrorTest$' --output-on-failure` | **0** | 06-test-green.log: `1/1 Passed` |
| 4b revert | `git diff -- plugins/ClapEffect/ClapLoader.cpp plugins/ClapEffect/ClapLoader.h > logs/fix.patch && git apply -R logs/fix.patch` | 0 / 0 | fix.patch |
| 4b RED build | `cmake --build build --target ClapLoaderErrorTest -j2` | 0 | 07-build-4b-reverted.log |
| **4b RED test** | `ctest -R '^ClapLoaderErrorTest$' --output-on-failure` | **8** | 08-test-4b-reverted.log: same assertion as step 04 |
| 4b re-apply | `git apply logs/fix.patch` | 0 | (no log) |
| 4b GREEN build | `cmake --build build --target ClapLoaderErrorTest -j2` | 0 | 09-build-4b-reapplied.log |
| **4b GREEN test** | `ctest -R '^ClapLoaderErrorTest$' --output-on-failure` | **0** | 10-test-4b-reapplied.log |
| CLAP family | `ctest -R '^(ClapLoaderErrorTest\|ClapHostTest\|ClapEffectIntegrationTest\|ClapInstrumentViewTest\|ClapBusMapTest)$'` | 0 | 11-test-family.log: **5 of 5 passed** |
| Gate: fork-sources | `bash tests/fork-sources-gate.sh` | 0 | 13-fork-sources.log |
| Gate: all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 | 14-all-sources-reproduce.log |
| Gate: no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 | 15-no-upstream-regression.log |
| Gate: check-namespace | `tests/scripted/check-namespace` | 0 | 16-check-namespace.log |
| Gate: no-tautology | `bash tests/no-tautology-gate.sh` | 0 | 17-no-tautology.log |
| Gate: doc-figures | `python3 tests/doc-figures-gate.py` | 0 | 18-doc-figures.log |
| Gate: complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 | 19-complexity.log: see below |
| Gate: file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 | 20-file-length.log: see below |
| feature_check.py (as specified) | `python3 .../qwen/feature_check.py <worktree>` | 1 | 12-feature-check.log: `PermissionError` creating `<worktree>-feature-check` next to the worktree (outside the write-jail) |
| feature_check.py (scratch copy) | copy in `~/.cache/zene-tmp/gate-new-p14-438`, log dir redirected into the run folder, `python3 -I` | 1 | 12-feature-check.log (overwritten): the simple gates ran; the ratchet baseline step failed because `base_listings` runs `git worktree add` inside `zene-cachyos`, outside the write-jail |
| submit_check.py (scratch copy) | `python3 -I .../submit_check.py <worktree> new-p14-438` | 1 | 21-submit-check.log |

### Gate notes

- The six simple gates exit 0. Their output was read from their logs. Exit codes come from the direct runs above.
- The two ratchets exit 1 on items that are **not** in files this change touches:
  - Complexity: `plugins/ClapEffect/ClapHost.cpp: lmms::clap::HostedPlugin::load` (CCN 17) is pre-existing, and ClapHost.cpp is not changed here. `loader::load` and `factoryCallbacksPresent` are not in the over-10 list.
  - File length: the list names other files only. `ClapLoader.cpp` is 327 lines (`wc -l`), well under 500.
- **feature_check.py's table was never produced in this sandbox.** Its log folder and its ratchet baseline both
  need write access outside the worktree. The table therefore is not reported. The per-gate exit codes above are the evidence I have.
- submit_check.py reports `scope_guard` as a ModuleNotFoundError in this environment (WARN, not a FAIL).

## Self-check

- **The regression test fails with the product change reverted and passes with it.** Yes: step 04 (RED, exit 8) and step 4b
  (revert, exit 8; re-apply, exit 0). The test is at `tests/src/plugins/ClapLoaderErrorTest.cpp:339`.
- **Deterministic.** Yes. No clock, sleep, thread or race. The three fixtures are fixed structural modules built from `clap-test-broken.c`.
- **Fix at the cause, and complete.** The cause is the unchecked factory pointer in `loader::load`, and both callers reach the factory only through it.
  The check covers all three callbacks that either caller uses (`get_plugin_count`, `get_plugin_descriptor`, `create_plugin`); the `create_plugin` path in `ClapHost.cpp:226` is covered too.
  Sibling null checks already present (`ClapHostLifecycle.cpp:152` descriptor null check, `ClapHost.cpp:208` candidate null check) are left as they were.
- **No collateral damage.**
  - Modules with all three callbacks take the same path as before. The existing CLAP fixtures and the rest of the family (`ClapHostTest`, `ClapEffectIntegrationTest`, `ClapInstrumentViewTest`, `ClapBusMapTest`) run 5/5 passing. I did not load a third-party CLAP library.
  - Projects and saved data: a plug-in is resolved by id at load time. A module whose `create_plugin` is null could never be instantiated before, because calling it crashed. So saved projects that referenced working plug-ins are unaffected.
  - Behavioural change to be aware of: a non-conforming module that lacks one of the three callbacks, but still answers `get_plugin_count` and `get_plugin_descriptor`, used to appear in the plug-in scan. It now appears as refused, with `factory-invalid`. The CLAP header requires all three, so this is the intended contract. I did not run a scan against a real third-party library to confirm the effect.
- **Verdict FIXED**, so no ALREADY-FIXED or FALSE-POSITIVE evidence is needed.
- **Realtime, undo and commands.** The loader runs on the load/scan path, not the audio thread. No allocation rules changed, undo is unaffected, and no control command was added (no canary movement).
- **Registration.** `plugins/ClapEffect/ClapLoader.{h,cpp}`, `tests/data/clap-test-plugin/*` and `tests/src/plugins/ClapLoaderErrorTest.cpp` are fork-new and already registered. The inherited file `tests/CMakeLists.txt` has a reasoned line in `tests/upstream-modifications.txt`.
- **Open item.** The completion check still reports the feature_check table as missing, because of the write-jail limit described above. Re-run `feature_check.py` from a session that can write its log folder and reach the ratchet baseline.
