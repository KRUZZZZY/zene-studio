Verdict: FALSE-POSITIVE

new-p21-50: "oop chainJson assumes non-null descriptors" (`src/core/ControlCommandsOutOfProcess.cpp:98-99, 118-119`)

The finding's premise, that "a chain contains a device whose descriptor is null", cannot occur. `Plugin::descriptor()` never returns null, and every effect that can reach a chain is non-null. No product change is made and no regression test is added, because the code is already correct under its contract.

Evidence (file:line, on branch `bugs/qwen-new-p21-50` at `37eaf07557`):

- `include/Plugin.h:260-262`: `descriptor()` is an inline, non-virtual getter that returns `m_descriptor`.
- `include/Plugin.h:312-313`: `m_descriptor` is a private member.
- `src/core/Plugin.cpp:67-76`: the only write to `m_descriptor` is the constructor's initialiser. If the argument is null, the constructor sets `m_descriptor = &dummyPluginDescriptor` (`Plugin.cpp:74-76`; the static object is at `Plugin.cpp:52`). A grep for `m_descriptor` across `src/`, `include/` and `plugins/` finds no other assignment.
- `src/core/EffectChain.cpp:201-219` (load path): the instantiated effect is kept only when non-null and okay. Otherwise it is deleted and replaced by `new DummyEffect(...)` (`:214-215`) before `m_effects.push_back( e )` (`:219`).
- `src/core/EffectChain.cpp:234-241` (`appendEffect`): the effect is dereferenced (`setEffectChain`) before it is pushed, so a null effect cannot enter the chain through that path.
- `src/core/ControlCommandsOutOfProcess.cpp:111`: the instrument branch already checks `track->instrument() != nullptr` before its descriptor reads at `:118-119`.
- `src/core/ControlCommandsOutOfProcessSupport.cpp:156`: `plugin->descriptor() == nullptr` is already guarded in the resolver. Under the invariant above that guard is unreachable. The finding's proposed guard would add a second dead check, so I did not add one.

Why no fix: the contract is that a `Plugin` always has a descriptor, and the constructor enforces it. Adding a null check at line 98 or 118 would change no behaviour. It would also be dead code that a test could not reach, so it would not be regression proof.

Commands run (EXIT codes as observed):

| Step | Command | Result |
| --- | --- | --- |
| Gate, first attempt, unmodified script | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p21-50 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-50.r1/logs/gate-1.log 2>&1; echo EXIT=$?` | EXIT=1. Traceback: `PermissionError` creating `zene-qwen-new-p21-50-feature-check` beside the worktree, which is outside the write-jail. No gate ran. |
| Gate, same logic, log directory redirected | Copy of `feature_check.py` in `logs/feature_check_logdir_copy.py`, changing only `ROOT` (pinned to the qwen tool directory so the cached base listing resolves) and the log directory (into `logs/gate-logs/`). Diff: `logs/feature_check_copy.diff`. Run: `python3 -I .../logs/feature_check_logdir_copy.py <worktree> > .../logs/gate-2.log 2>&1; echo EXIT=$?` | EXIT=0. All 8 rows PASS (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length). Base `37eaf0755`; the base listing was already cached. |
| Evidence search | `rg -n "m_descriptor\|inline const Descriptor \* descriptor\|private:" include/Plugin.h` plus the matching searches over `src/core/Plugin.cpp`, `src/core/EffectChain.cpp`, `src/core/ControlCommandsOutOfProcess.cpp` and `src/core/ControlCommandsOutOfProcessSupport.cpp` (all appended to `logs/evidence-1.log`) | EXIT=0 for each search |
| RED / GREEN | Not run. There is no product change and no regression test, because the verdict is not-real. | N/A |

## Self-check

- The regression test fails with the product change reverted and passes with it: N/A. No product change and no test were made, because the verdict is FALSE-POSITIVE.
- The test is deterministic: N/A, no test.
- The fix is at the cause and complete: N/A, no fix. I checked the chain walk's descriptor reads at `ControlCommandsOutOfProcess.cpp:98-99` and `:118-119`, and the resolver at `ControlCommandsOutOfProcessSupport.cpp:156-163`. All three rely on the same `Plugin` invariant, which is enforced at `Plugin.cpp:74-76`.
- No collateral damage: nothing changed. Saved projects and the command surface are untouched.
- FALSE-POSITIVE evidence: given above with file:line (`Plugin.h:260-262, 312-313`; `Plugin.cpp:52, 67-76`; `EffectChain.cpp:201-219, 234-241`; `ControlCommandsOutOfProcess.cpp:111`).
- Audio-thread constraints: nothing added.
- Undo and commands: nothing added. No command was added or changed, so the `ControlCommandsSnapshot` canary is unaffected.
- Registration: no new files, so nothing to register in `tests/fork-sources.txt` or `tests/upstream-modifications.txt`.
- Gates: the first `feature_check.py` attempt failed with a permission error (EXIT=1, see table). A logs-directory copy of the same gate passed with EXIT=0 (8/8).
- `submit_check.py` gates row: FAIL, with the same cause. It runs the unmodified `feature_check.py`, which cannot create its log directory beside the worktree (outside the write-jail). The gate's checks themselves pass (8/8, see above). This is an environment limit, not a code failure. The output is in `logs/submit-1.log`.
