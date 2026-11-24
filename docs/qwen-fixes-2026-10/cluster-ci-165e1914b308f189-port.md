ALREADY-FIXED

# Cluster ci-165e1914b308f189-port — Port: CI regression: build / windows-arm64

This unit ports cluster `ci-165e1914b308f189` to this branch. That cluster's hosted `windows-arm64` (Clang) build
failed in the vendored sfizz `atomic_queue.h`:

```
atomic_queue.h:394:31: error: a template argument list is expected after a name prefixed by the
template keyword [-Wmissing-template-arg-list-after-template-kw]
  394 |         return Base::template do_pop_any(states_[index], elements_[index]);
```

The original fix and its regression are already on this branch, so this port has nothing left to do.

## Evidence (file:line)

- `plugins/SfzPlayer/sfizz/external/atomic_queue/include/atomic_queue/atomic_queue.h:394,400,522,528` now read
  `Base::do_pop_any(...)` / `Base::do_push_any(...)`, with no `template` keyword before a deduced call.
  This is the fix described in `docs/qwen-fixes-2026-10/cluster-ci-165e1914b308f189.md`.
- The fix came in with commit `3cc4c0a075` ("fix(train-sol): Combined five pending CI repairs…", cherry-picked
  from `ee9b376701`), which touches `atomic_queue.h`, `tests/clang-template-keyword-check.py`,
  `tests/CMakeLists.txt`, `tests/fork-sources.txt` and `tests/upstream-modifications.txt`.
  `git merge-base --is-ancestor 3cc4c0a075 HEAD` → EXIT=0.
- The regression check is registered as ctest `ClangTemplateKeywordCheck` at `tests/CMakeLists.txt:7106-7111`.

## Commands and EXIT codes (this session; logs in `~/.config/kads/zene/state/runs/ci-165e1914b308f189-port.r2/logs/`)

| # | Command | EXIT | Log |
| --- | --- | --- | --- |
| 1 | `git merge-base --is-ancestor 3cc4c0a075 HEAD` | 0 | (shell) |
| 2 | `cd build/tests && ctest -R ClangTemplateKeywordCheck --output-on-failure` | 0 (`100% tests passed out of 1`) | `test-1.log` |
| 3 | `python3 tests/clang-template-keyword-check.py --root . --compiler clang++` | 0 (both PASS lines: tree scan clean; clang++ compiles the real header with `AtomicQueue2`/`AtomicQueueB2` instantiated) | `test-2-clang.log` |
| 4 | `python3 tests/clang-template-keyword-check.py --self-test` | 0 (red fixture flagged, green fixture clean) | `test-3-selftest.log` |
| 5 | real `compile_commands.json` line for `plugins/SfzPlayer/sfizz/src/sfizz/FilePool.cpp`, compiler swapped to `/usr/bin/clang++`, `-o` removed, `-fsyntax-only` added | 0, no diagnostics | `build-4-clang-filepool.cmd`, `build-4-clang-filepool.log` |
| 6 | control: the same checker pointed at the **pre-fix** header (`git show 3cc4c0a075^:…/atomic_queue/{atomic_queue.h,defs.h}` extracted to a scratch dir in the log folder, deleted afterwards): `python3 tests/clang-template-keyword-check.py --root . --compiler clang++ --atomic-queue-include <scratch>` | **1**, the exact CI diagnostic at `atomic_queue.h:394:31`, then `:400:24` and `:522:31` | `test-5-prefix-header-clang.log` |

Row 6 shows the existing regression check is still live on this box. With the old header Clang rejects it with
the CI error. With the header on this branch, Clang accepts it (row 3).

No product code was changed, so procedure step 4b (revert the product patch and watch the test go red) does not
apply. Row 6 is the equivalent red control.

## Self-check

- **Verdict evidence:** the code reference is `atomic_queue.h:394,400,522,528` (fixed spelling present), and the
  fix commit is an ancestor of HEAD (row 1, EXIT=0).
- **Regression test:** the existing `ClangTemplateKeywordCheck` passes here (row 2, EXIT=0) and fails on the
  pre-fix header (row 6, EXIT=1). It is deterministic: a source scan plus a `-fsyntax-only` compile, with no
  timing and no threads.
- **Fix at the cause and complete:** all four sites of the ill-formed construct (`atomic_queue.h:394,400,522,528`)
  carry the fixed spelling. The checker's tree-wide scan of every tracked C/C++ file for `::template name`,
  `.template name` or `->template name` not followed by `<` finds none (row 3, first PASS line). So no sibling of
  the construct was reintroduced on this branch.
- **Sibling commands:** this unit changes no command file. The only file committed is this report. The
  `app.*` / `arrangement.*` commands listed by `submit_check.py` come from the train-wide diff against the base
  (`feature_check` reports 576 changed files inherited by this branch), not from this unit. They are unaffected,
  and no control command was added, so the ControlCommandsSnapshot canary is unchanged.
- **No collateral damage, realtime, undo, commands:** no product file was changed in this unit. The only new
  file is this report.
- **Registration and gates:** `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` → EXIT=0,
  every row PASS (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology,
  doc-figures, complexity, file-length; `RESULT: PASS`), log `gates-6.log`.

Delegation: Haiku ran rows 2–5 (ctest, the Clang checker, its self-test, the Clang FilePool.cpp compile) and
the gate table, and reported their exit codes. I re-read the logs myself. I did row 1 and the pre-fix control (row 6) myself, since
each was a single command. Sonnet was not used: there was no fix or test to write.

## Other findings

None.
