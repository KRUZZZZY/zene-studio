FALSE-POSITIVE

# cluster gui-1030-session-labels-and-symbols-make-sense — "The Sync control is labeled Ableton Link"

## Verdict

**FALSE-POSITIVE.** The behaviour the sweep observed is real (the arranger's session-sync toggle reads
"Ableton Link" in a build with the real Link backend), but it is the **intended, documented, tested
contract**, not a defect. The sweep's expectation ("No 'Ableton Link' label (the sync button says
'Sync')") comes from the patrol script, not from a product spec, and contradicts the shipped design.
No product code was changed. Changing the label would reverse a deliberate decision, which the
owner's instruction for this run forbids ("a triaged finding is not a license to change intended
behavior").

## Evidence

| Claim | Where |
| --- | --- |
| The label is chosen by backend on purpose: real Link says "Ableton Link", the zene-link-style fallback says "Sync". | `src/gui/SessionSyncAction.cpp:61-72` (`sessionSyncText`), comment at `:95` ("The label and tooltip name the backend this build speaks") |
| The decision is documented: "In the interface the feature is called **Ableton Link**, in full, with its Enabled/Disabled state, as Ableton's Link guidelines ask." | `docs/LINK-SYNC.md:88-92` |
| Commit that made it: "the arranger toolbar's session-sync toggle is labelled 'Ableton Link' (the name in full, as Ableton's Link guidelines ask) wherever the build speaks real Link ... The fallback build keeps 'Sync'." | `git show 8ae3c5da4e` (WF-051) |
| User-facing docs repeat it. | `data/manual/index.html:496`, `README.md:182-183`, `docs/KNOWN-LIMITATIONS.md:1033-1035` |
| A test pins it, so it cannot drift silently. | `tests/src/gui/SessionSyncActionTest.cpp` — `theTextReadsTheSession` (the `Ableton Link` / `Ableton Link: 3 peer(s), 120.5 BPM` cases), `theActionNamesAbletonLink` |
| The "Sync" expectation has no product source: it is only in the patrol's script. | `~/.local/share/zene-bunny-fix/terra-pilot/session/expected-uses.json:46-48` (id `labels`); no spec file in this tree says the button must read "Sync" |
| The tooltip half of the finding ("every icon has a tooltip") does not apply to this control: it always has one, on both backends. The finding itself says only a *mixer* icon hover was sampled and "not every icon was individually checked". | `src/gui/SessionSyncAction.cpp:74-88` (`sessionSyncToolTip`, non-empty in both branches), set by `show()` at `:50` |

The fallback ("Sync") path is intact: `SessionSyncAction.cpp:66` and `:70` return "Sync" / "Sync: %n peer(s), %1 BPM"
when `model != "ableton-link"`, also covered by `theTextReadsTheSession`.

## Reproduction (current tree, no product change)

This build reports `model: "ableton-link"`, because `theActionNamesAbletonLink` ran instead of skipping.
The run shows the observed label, and shows that the label is the asserted contract.

| Step | Command | EXIT |
| --- | --- | --- |
| Build (first attempt failed only because ccache's temp dir under `~/.local/share/zene-bunny-fix/dsh/home` is not writable in this jail) | `cmake --build build --target SessionSyncActionTest -j4 > logs/build-1.log 2>&1` | 2 (ccache: Permission denied) |
| Build (ccache temp dir redirected to `$TMPDIR`, removed afterwards) | `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-gui1030 cmake --build build --target SessionSyncActionTest -j4 > logs/build-2.log 2>&1` | 0 |
| Test (from `build/tests`) | `ctest -R '^SessionSyncActionTest$' --output-on-failure > logs/test-1.log 2>&1` | 0 — 1 test executed, 100% passed |
| Per-case run | `QT_QPA_PLATFORM=offscreen ./SessionSyncActionTest -v2` | `Totals: 7 passed, 0 failed, 0 skipped` |

`logs/` is `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1030-session-labels-and-symbols-make-sense.r1/logs`.

## Regression test / RED-GREEN

Not applicable: there is no defect and no fix, so there is no failing-then-passing test (step 4b is
skipped, and I do not fabricate one). The existing `SessionSyncActionTest` already pins the contract in
both directions (Link backend names "Ableton Link", fallback says "Sync"). Adding a test that asserted
"Sync" for the Link backend would contradict it.

## If the owner wants the sweep's wording instead

That is a product decision and a reversal of WF-051: it would change `SessionSyncAction.cpp:66,69`, the
three `SessionSyncActionTest` cases, the `Project > Sync` entry text (`src/gui/LibraryCommandMenus.cpp`),
`docs/LINK-SYNC.md:88`, the manual and README. It would also drop the product naming that Ableton's
Link guidelines ask for. The cheaper alternative is to correct the patrol's `expected-uses.json:48` to
expect "Ableton Link" where the build carries the library and "Sync" where it does not.

## Files committed

Only this report: `docs/qwen-fixes-2026-10/cluster-gui-1030-session-labels-and-symbols-make-sense.md`.
No product, test or registration file changed, so `tests/fork-sources.txt` / `tests/upstream-modifications.txt` need no entry.

## Self-check

- **Regression test fails reverted / passes fixed:** not applicable. The verdict is FALSE-POSITIVE, so no product
  change exists to revert. The existing contract test passes on the unchanged tree (`ctest` EXIT=0, 1 test run;
  7 cases passed, 0 skipped).
- **Deterministic test:** no new test was added. The existing cases feed `sessionSyncText` literal `QJsonObject`
  states (the seam), so they don't depend on timing for that check. `anEnableMadeElsewhereShowsWhileUnchecked`
  does use `QTRY_VERIFY_WITH_TIMEOUT`, and I did not touch it.
- **Fix at the cause and complete:** nothing was changed. Siblings of the label, checked and consistent with
  "Ableton Link on the Link backend, Sync on the fallback": `SessionSyncAction.cpp:66,69-70` (text),
  `:77-87` (tooltip), `src/gui/LibraryCommandMenus.cpp:207` (the `Project > Sync` entry, "Ableton Link").
- **No collateral damage:** no code, saved-data or project-format change.
- **FALSE-POSITIVE contract:** `docs/LINK-SYNC.md:88-92`, commit `8ae3c5da4e` (WF-051), `SessionSyncAction.cpp:61-72`,
  `tests/src/gui/SessionSyncActionTest.cpp` (`theTextReadsTheSession`, `theActionNamesAbletonLink`); the contrary
  "Sync" expectation lives only in the patrol's `expected-uses.json:46-48`.
- **Gates not run (environment):** `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` exits 1
  with `PermissionError: [Errno 13] ... zene-qwen-gui-1030-session-labels-and-symbols-make-sense-feature-check`.
  The script creates a sibling directory of the worktree for its logs, which is outside this lane's write-jail
  (it also checks out a shared cache worktree). The log is `logs/gates-1.log`. So no gate table exists for this
  run. The commit changes only one Markdown file, with no code, so no complexity or file-length item can be new in it.
  The loop's gate run, outside the jail, will have to supply the result. `submit_check.py`
  (`logs/submit-1.log`, EXIT=1 before this commit) reported its `gates` row FAIL for the same reason.
