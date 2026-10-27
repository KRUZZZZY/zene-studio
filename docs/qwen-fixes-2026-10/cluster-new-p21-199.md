Verdict: FALSE-POSITIVE

new-p21-199: the cache guard does reject a cached master index, but the finding's stated consequence is false. No output or cost changes, so no product change was made and no regression test can fail without the fix.

Evidence:

- The cache guard is `if (cached > 0 && cached < channels && mixer->mixerChannel(cached)->id() == channelId)` at [src/core/InstrumentOutputBuses.cpp:222](src/core/InstrumentOutputBuses.cpp:222). A cached 0 is rejected. The comment at [src/core/InstrumentOutputBuses.cpp:223](src/core/InstrumentOutputBuses.cpp:223) says "The master (index 0) is a valid target too", so the guard contradicts its own comment. That part of the finding is accurate.
- Master is always index 0. [src/core/Mixer.cpp:1052-1053](src/core/Mixer.cpp:1052) creates the master channel first in the constructor, and `m_mixerChannels[0]` is treated as master throughout the file (for example [src/core/Mixer.cpp:1691](src/core/Mixer.cpp:1691), [src/core/Mixer.cpp:1758](src/core/Mixer.cpp:1758)).
- The finding says every send for a master-routed bus does "a full linear scan". That is false. The fallback loop at [src/core/InstrumentOutputBuses.cpp:224-228](src/core/InstrumentOutputBuses.cpp:224) tests `i = 0` first. For a master-routed bus the id matches there, so the loop stores 0 and returns on its first iteration. A cache hit would save one comparison and one store.
- The returned index is the same with or without the cache hit. For `cached == 0`, the hit path returns 0 only when `mixerChannel(0)->id() == channelId`, which is exactly the first scan iteration. For any other `channelId` the hit fails the id test and falls through to the scan. This holds even with duplicate ids, because the scan also returns 0 first whenever index 0 matches. `routedIndex` (public, [include/InstrumentOutputBuses.h](include/InstrumentOutputBuses.h)) and `send`/`deliver` therefore never see a difference.
- Reset behaviour is unaffected: `storeRoute` sets `cachedIndex` back to -1 at [src/core/InstrumentOutputBuses.cpp:148](src/core/InstrumentOutputBuses.cpp:148), and the cache is only read in `mixerIndexOf`.
- The real-time path is unchanged: the audio-thread code does not change, and nothing new is allocated, locked or logged.

Why there is no RED/GREEN test and no step 4b: the definition of done requires a test that fails without the fix. Nothing observable differs between the two versions, so no public API (`routedIndex`, `send`, `deliver`, `forEachLatentRoute`) can tell them apart. The only way to see a difference would be to count scans or read `cachedIndex`. That needs a new probe or `friend` in the header on a real-time path, to test a defect with no effect. I did not add one. The three suggested test files (`BusesTestInstrument`, `ControlPluginBusesTest`, `InstrumentRoutingTest`) were not built or run, because there is no product change for them to check.

Recommendation (not applied): changing line 222 to `cached >= 0` is behaviour-identical and would make the guard match its comment. It can go in with a later cosmetic cleanup. The finding's performance claim should be dropped either way.

Commands and results (LOG FOLDER = `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-199.r1/logs`; run from the worktree unless noted):

| Evidence | Command | Result |
| --- | --- | --- |
| Cache reads and master-channel creation | `rg -n "cachedIndex\|cached > 0\|cached >= 0\|valid target\|for \(int i = 0; i < channels" src/core/InstrumentOutputBuses.cpp > logs/001-source-audit.log; rg -n "create master channel\|createChannel\(\);" src/core/Mixer.cpp >> logs/001-source-audit.log` | EXIT=0 (both commands, `logs/001-source-audit.log`) |
| Feature harness itself | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/002-feature-check-before-report.log 2>&1` | EXIT=1. `PermissionError` creating `<worktree>-feature-check`, a sibling of the worktree outside the write-jail. The harness did not run any gate, so the rows below come from running each of its commands by hand. |
| fork-sources | `bash tests/fork-sources-gate.sh > logs/003-gate-fork-sources.log 2>&1` | EXIT=0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh > logs/004-gate-all-sources-reproduce.log 2>&1` | EXIT=0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh > logs/005-gate-no-upstream-regression.log 2>&1` | EXIT=0 |
| check-namespace | `tests/scripted/check-namespace > logs/006-gate-check-namespace.log 2>&1` | EXIT=0 |
| no-tautology | `bash tests/no-tautology-gate.sh > logs/007-gate-no-tautology.log 2>&1` | EXIT=0 |
| doc-figures | `python3 tests/doc-figures-gate.py > logs/008-gate-doc-figures.log 2>&1` | EXIT=0 |
| complexity (ratchet, `--check`) | `bash tests/complexity-gate.sh --check > logs/009-gate-complexity.log 2>&1` | EXIT=1. 6 `REGRESSION` lines, all train debt (the brief says this gate is red on purpose). The branch changes no source file against its base, so none of them is new to this branch; the harness PASS rule, as its docstring states it, gives PASS. |
| file-length (ratchet, `--check`) | `bash tests/file-length-gate.sh --check > logs/010-gate-file-length.log 2>&1` | EXIT=1. 22 `REGRESSION` lines, same reasoning as complexity: PASS under the harness rule. |

Files committed for this verdict: `docs/qwen-fixes-2026-10/cluster-new-p21-199.md`. No product, test or registration file changed.

## Self-check

Result of `submit_check.py` (`logs/011-submit-check.log`, EXIT=1): commits, clean tree, report and claim-only closure PASS. The one FAIL is `gates (feature_check.py)`. That row runs `feature_check.py`, which exits 1 with `PermissionError` creating `<worktree>-feature-check` (a sibling directory outside the write-jail). It is an environment limit, not a gate finding. The gates were run one by one (table above): six PASS, and the two ratchets are red on train debt that this branch does not touch. The harness itself needs to be run from outside the jail, or with its log folder pointed inside the worktree, before this row can turn green.

- **Regression test fails with the change reverted and passes with it:** not applicable. No product change was made, so there is nothing to revert. No test was added. The reason: the cache guard at `src/core/InstrumentOutputBuses.cpp:222` gives the same index with or without the cached-0 hit. Every public path (`routedIndex` at :233-240, `send` at :250-270, `deliver` at :272-284) returns the same value either way. The only thing that differs is an internal `cachedIndex` store and a scan count. No test can observe that without adding a probe to the real-time path, which I did not do.
- **Test is deterministic:** not applicable (no test). The reasoning above is deterministic: it is a code-path argument, not a timing claim.
- **Fix at the cause and complete:** not applicable (no fix). I checked the cache's only reader and writers. `cachedIndex` is read only in `mixerIndexOf` (:221) and written in `mixerIndexOf` (:227) and `storeRoute` (:148). Its callers are `routedIndex` (:239) and `send` (:260). `routedBuffer` (:242-248) does not touch the cache.
- **No collateral damage:** nothing changed. `cachedIndex` is internal state. It is not serialised: `saveSettings` (:185-198) and `loadSettings` (:200-214) read only `routeOf`, and the save format is unaffected.
- **Verdict evidence (FALSE-POSITIVE):** the guard and the comment that contradicts it are at `src/core/InstrumentOutputBuses.cpp:222-223`. The scan that makes the consequence false is at :224-228: the index-0 match returns on the first iteration. Master is index 0 by construction at `src/core/Mixer.cpp:1052-1053`. The equivalence argument is in the Evidence section above.
