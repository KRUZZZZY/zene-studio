Verdict: FALSE-POSITIVE

new-p22-50: partsState dereferences zones.zone(i) without a null check (src/core/ControlCommandsPluginParts.cpp, finding line 117; the dereference is now line 118)

## Why the finding does not hold

The finding says `zoneCount()` and the zone storage "can disagree in malformed project state", so `zone(i)` can return null. In this tree there is only one storage and one count, and every path that builds or changes it keeps them equal:

- **One store.** `RackZones` keeps its zones in one `std::vector<RackZone> m_zones` (include/RackZones.h:179). There is no separate count member.
- **The count is the store's size.** `zoneCount()` returns `m_zones.size()` (src/core/RackZones.cpp:103-106).
- **`zone(i)` is null only outside the count.** `zone(i)` returns `nullptr` only when `i < 0 || i >= zoneCount()`; otherwise it returns `&m_zones[i]` (src/core/RackZones.cpp:108-112; declared include/RackZones.h:123-124).
- **The loop stays inside the count.** `partsState` iterates `for (int i = 0; i < zones.zoneCount(); ++i)` and calls `zone(i)` on that same index (src/core/ControlCommandsPluginParts.cpp:116-118). Each `zone(i)` in that loop therefore returns a valid pointer. The null check the finding asks for would never fire.
- **Malformed project files cannot split the two.** `RackZones::loadSettings` validates each `<zone>` child, skips the invalid ones, collects the rest into a local `read` vector (src/core/RackZones.cpp:228-230, push at :263), and installs it with one swap: `edit([this, &read] { m_zones.swap(read); })` (src/core/RackZones.cpp:266). A malformed file yields a shorter list, not an inconsistent one.
- **Every other writer is one swap or one edit.** `assign` swaps the whole list (src/core/RackZones.cpp:158-164, swap at :163). `addZone`, `insertZone`, `removeZone`, `clear` and `reindexChains` change `m_zones` only inside `edit(...)`: src/core/RackZones.cpp:120, :132, :139, :186, :204.
- **Writers run on the UI thread.** Mutating control commands go through `ControlRegistry::runOnUiThread` (src/core/ControlRegistry.cpp:249-259, with `BlockingQueuedConnection` at :258; dispatch at :296). `partsState` runs there too. The audio thread only reads the list (for example src/core/RackZoneFeed.cpp:57 and :76). No path changes `m_zones` while `partsState` runs.

So the null dereference in the finding cannot occur in this tree. The only way to reach it would be a concurrent removal from another thread, which would break the UI-thread-only mutation rule above. That is a different contract, and no current code does it.

## Decision and scope

- No product code changed. A fix would be dead code, and a defensive check on a path that cannot fail would also hide a real invariant break if one were ever introduced.
- No regression test was added. The test rule applies to fixes. A test that passes both with and without a check here would prove nothing about the bug.
- Sibling loops use the same pattern and are safe by the same argument: src/core/ControlCommandsPluginParts.cpp:282, src/core/ControlCommandsRackLayers.cpp:100 and :135, src/core/InstrumentMidiParts.cpp:82, src/core/InstrumentLayers.cpp:311 and :324.

## Commands run and exit codes

All commands ran from the worktree `zene-qwen-new-p22-50`. Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p22-50.r1/logs`.

| Command | EXIT | Log |
| --- | --- | --- |
| `python3 .../qwen/feature_check.py <worktree>` | 1 | `gates-1.log` (see note 1) |
| `bash tests/fork-sources-gate.sh` | 0 | `gate-fork-sources-2.log` |
| `bash tests/all-sources-reproduce.sh` | 0 | `gate-all-sources-2.log` |
| `bash tests/no-upstream-regression-gate.sh` | 0 | `gate-no-upstream-2.log` |
| `tests/scripted/check-namespace` | 0 | `gate-check-namespace-2.log` |
| `bash tests/no-tautology-gate.sh` | 0 | `gate-no-tautology-2.log` |
| `python3 tests/doc-figures-gate.py` | 0 | `gate-doc-figures-2.log` |
| `bash tests/complexity-gate.sh --check` | 1 | `gate-complexity-2.log` (see note 2) |
| `bash tests/file-length-gate.sh --check` | 1 | `gate-file-length-2.log` (see note 2) |
| `git status --short` (after the gates) | — | clean |

Notes:

1. `feature_check.py` stopped before running any gate: it writes its logs to `<worktree>/../<name>-feature-check`, and that path is outside this session's write area (`PermissionError: ... zene-qwen-new-p22-50-feature-check`, log `gates-1.log`). I ran the same six simple gate commands and the two ratchet commands directly, as listed above.
2. The complexity and file-length gates exit 1 on the existing baseline: 7 complexity and 22 file-length `REGRESSION` lines. None names a file this branch touches (`ControlCommandsPluginParts.cpp` or `docs/`). The branch changes no product file, so the script's rule "fail only on items new in your files" is satisfied. The raw exit codes are still 1, as the brief said the train would be.

## Self-check

- **Commits on the branch.** This commit adds this report and nothing else.
- **Working tree clean.** Yes after the commit. Nothing else was modified.
- **Report present.** `docs/qwen-fixes-2026-10/cluster-new-p22-50.md`, verdict on the first line.
- **Tests added or changed.** None. The verdict is FALSE-POSITIVE and no product code changed, so no regression test was written (see "Decision and scope"). `submit_check.py` marks this as claim-only closure.
- **Gates (`feature_check.py`).** Not passed by the script itself. It exits 1 with `PermissionError` before any gate runs, because it writes logs to `<worktree>/../<name>-feature-check` (`gates-1.log`). That path is outside this session's write area. I ran its six simple gates and two ratchet gates directly: the six simple gates exit 0; complexity and file-length exit 1 on existing baseline items, none in a file this branch touches. The gate runner needs to be re-run from an unrestricted shell to get its own PASS table.
- **Regression test RED then GREEN.** Not applicable: no fix, so no RED/GREEN pair.
- **Deterministic test.** Not applicable: no test.
- **Fix at the cause and complete.** Not applicable: no fix. The sibling loops were checked and each stays in range (see "Decision and scope").
- **No collateral damage.** Not applicable: no behaviour changed.
- **FALSE-POSITIVE evidence.** Given above as file:line: one storage (`include/RackZones.h:179`), count equals its size (`src/core/RackZones.cpp:103-106`), `zone(i)` is null only out of range (`src/core/RackZones.cpp:108-112`), the loop is bounded by the same count (`src/core/ControlCommandsPluginParts.cpp:116-118`), malformed files are loaded into a local vector and swapped in once (`src/core/RackZones.cpp:230`, `:263`, `:266`), and writers run on the UI thread (`src/core/ControlRegistry.cpp:249-259`).
