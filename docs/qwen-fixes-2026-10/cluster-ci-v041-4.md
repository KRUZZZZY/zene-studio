ALREADY-FIXED

# Cluster ci-v041-4: `template` keyword on deduced atomic_queue calls — already fixed

## 1. Verdict evidence

The four ill-formed calls the finding names are already gone from the source.
`plugins/SfzPlayer/sfizz/external/atomic_queue/include/atomic_queue/atomic_queue.h`
lines 394, 400, 522 and 528 now read `Base::do_pop_any(` / `Base::do_push_any(`
(`logs/evidence-1-lines.log`):

```
394:        return Base::do_pop_any(states_[index], elements_[index]);
400:        Base::do_push_any(std::forward<U>(element), states_[index], elements_[index]);
522:        return Base::do_pop_any(states_[index], elements_[index]);
528:        Base::do_push_any(std::forward<U>(element), states_[index], elements_[index]);
```

The `Base::template do_pop_atomic<T, NIL>` / `Base::template do_push_atomic<T, NIL>`
calls at :353, :358, :438 and :443 are well-formed: they carry an explicit template
argument list, which is exactly when the `template` keyword is permitted (and
required). They were left untouched.

Commit `3cc4c0a0756ac4570a3ab7a742eb7a0ad1de7107` ("fix(train-sol): Combined five
pending CI repairs ...") contains the change: its diff replaces the four
`Base::template do_pop_any(` / `Base::template do_push_any(` lines with
`Base::do_pop_any(` / `Base::do_push_any(` (`logs/evidence-2-gitlog.log`,
`logs/evidence-3-show.log`; the full sha is from `git rev-parse 3cc4c0a075`,
`logs/revparse-3cc4c0a075.log`).

The change is documented as an intentional local modification:
`plugins/SfzPlayer/LICENSE-NOTICE.md:11-13` records that the ill-formed
`Base::template do_pop_any(...)` / `Base::template do_push_any(...)` calls (a
`template` keyword with no explicit template argument list, which current Clang
rejects) "lost the keyword; the queue semantics are unchanged". The earlier
report `docs/qwen-fixes-2026-10/cluster-ci-165e1914b308f189.md` is the defect
analysis that named these same four lines and proposed exactly this fix.

Guard: ctest `ClangTemplateKeywordCheck` (`tests/CMakeLists.txt:6883-6897`,
`tests/clang-template-keyword-check.py`) scans every tracked C/C++ source for a
`template` keyword before a name with no argument list and, on a compiler that
enforces the rule, also compiles the real atomic_queue header with
`AtomicQueue2` and `AtomicQueueB2` instantiated; its fixtures prove the scanner
can fail.

A repo-wide scan for `::template name(` over `*.{h,hpp,c,cc,cpp,hh}` returned
0 matches, EXIT=1 (ripgrep's "no match" code) — `logs/evidence-4-scan-r2.log`.

## 2. Why the finding's CMake `-Wno` flag is not needed

The ill-formed construct is gone from the source, so no warning suppression is
needed. Clang diagnoses it as a default-error warning
(`-Wmissing-template-arg-list-after-template-kw`), so `-Wno-error` alone would
not clear it, while `-Wno-missing-template-arg-list-after-template-kw` would;
suppression is unnecessary because the source is fixed. The vendored code was
fixed at the cause instead, the way the earlier cluster's upstream check
describes: upstream atomic_queue removed the construct when it restructured
these functions (the v1.9.0 tag has no `do_pop_any`/`do_push_any`; the vendored
copy is the sfizz 1.2.3 pin). `Base` is the current instantiation's base, and
the call is still `Base::`-qualified; only the `template` keyword was dropped,
so it resolves to the same function and the queue semantics are unchanged.

## 3. Commands and EXIT codes

Each EXIT below is copied from the `EXIT=` line at the end of the cited `-r2`
log; the earlier round's logs are superseded. `L` is the log directory
`/home/kruzzzzy/.config/kads/zene/state/runs/ci-v041-4.r1/logs` and
`H=plugins/SfzPlayer/sfizz/external/atomic_queue/include/atomic_queue/atomic_queue.h`.

- Green ctest, from the build tree:

  ```
  cd build/tests && ctest -R ClangTemplateKeywordCheck --output-on-failure > $L/test-green-r2.log 2>&1; echo EXIT=$? >> $L/test-green-r2.log
  ```

  `100% tests passed, 0 tests failed out of 1`, EXIT=0
  (`logs/test-green-r2.log`).

- Green script run directly, from the worktree root:

  ```
  python3 tests/clang-template-keyword-check.py --root . --compiler /usr/bin/clang++ > $L/test-green-clang-r2.log 2>&1; echo EXIT=$? >> $L/test-green-clang-r2.log
  ```

  Both `PASS` lines, EXIT=0 (`logs/test-green-clang-r2.log`).

- Red ctest for each re-inserted keyword, one mutation at a time (`N` is 394,
  400, 522 or 528; each patch is one line, `git diff --stat` reporting 1 file
  changed, 1 insertion(+), 1 deletion(-)):

  ```
  git apply $L/mut-$N.patch
  cd build/tests && ctest -R ClangTemplateKeywordCheck --output-on-failure > $L/test-red-$N-r2.log 2>&1; echo EXIT=$? >> $L/test-red-$N-r2.log
  ```

  The reversal after each red run is the one shown in the Restore item below.
  `0% tests passed, 1 tests failed out of 1`, EXIT=8 for all four
  (`logs/test-red-394-r2.log`, `logs/test-red-400-r2.log`,
  `logs/test-red-522-r2.log`, `logs/test-red-528-r2.log`).

- The same four mutations through the script's scanner half:

  ```
  python3 tests/clang-template-keyword-check.py --root . --compiler /usr/bin/clang++ > $L/test-red-$N-clang-r2.log 2>&1; echo EXIT=$? >> $L/test-red-$N-clang-r2.log
  ```

  `FAIL: 1 'template' keyword use(s) ...`, EXIT=1 each
  (`logs/test-red-394-clang-r2.log`, `logs/test-red-400-clang-r2.log`,
  `logs/test-red-522-clang-r2.log`, `logs/test-red-528-clang-r2.log`).

- Restore after each mutation (reversal plus a clean-tree check):

  ```
  git apply -R $L/mut-$N.patch; echo "rev-$N EXIT=$?" >> $L/restore-r2.log
  git diff --quiet -- $H; echo "diff-$N EXIT=$?" >> $L/restore-r2.log
  ```

  All eight lines in `logs/restore-r2.log` are EXIT=0, i.e. the header is back
  to the committed, fixed state after every mutation.

- Compile-half unmutated scratch control, run before the mutations:

  ```
  mkdir -p $L/aq-scratch && cp -r plugins/SfzPlayer/sfizz/external/atomic_queue/include $L/aq-scratch/include
  python3 tests/clang-template-keyword-check.py --root . --compiler /usr/bin/clang++ --atomic-queue-include $L/aq-scratch/include > $L/test-compile-green-r2.log 2>&1; echo EXIT=$? >> $L/test-compile-green-r2.log
  ```

  Both `PASS` lines, EXIT=0 (`logs/test-compile-green-r2.log`).

- Compile-half red runs on the scratch copy, after resetting it from `$H` and
  applying the same one-line change as in the mutation patches:

  ```
  cp $H $L/aq-scratch/include/atomic_queue/atomic_queue.h
  sed -i "${N}s/Base::\(do_p\)/Base::template \1/" $L/aq-scratch/include/atomic_queue/atomic_queue.h
  python3 tests/clang-template-keyword-check.py --root . --compiler /usr/bin/clang++ --atomic-queue-include $L/aq-scratch/include > $L/test-compile-red-$N-r2.log 2>&1; echo EXIT=$? >> $L/test-compile-red-$N-r2.log
  ```

  EXIT=1 for each, with the real `/usr/bin/clang++` rejecting the header:
  `atomic_queue.h:N:col: error: a template argument list is expected after a
  name prefixed by the template keyword
  [-Wmissing-template-arg-list-after-template-kw]` — :394 (col 31), :400
  (col 24), :522 (col 31), :528 (col 24)
  (`logs/test-compile-red-394-r2.log`, `logs/test-compile-red-400-r2.log`,
  `logs/test-compile-red-522-r2.log`, `logs/test-compile-red-528-r2.log`).
  The scratch copy was then removed with `rm -rf $L/aq-scratch`.

- Final green ctest, from the build tree:

  ```
  cd build/tests && ctest -R ClangTemplateKeywordCheck --output-on-failure > $L/test-green-final-r2.log 2>&1; echo EXIT=$? >> $L/test-green-final-r2.log
  ```

  `100% tests passed, 0 tests failed out of 1`, EXIT=0
  (`logs/test-green-final-r2.log`).

- Clean-tree check:

  ```
  git status --porcelain > $L/status-p4b.log 2>&1; echo EXIT=$? >> $L/status-p4b.log
  ```

  Empty output, EXIT=0 (`logs/status-p4b.log`).

- Repo-wide scan and compiler version, re-run with the EXIT line inside the log:

  ```
  rg -n '::template [A-Za-z_]+\(' -g '*.{h,hpp,c,cc,cpp,hh}' . > $L/evidence-4-scan-r2.log 2>&1; echo EXIT=$? >> $L/evidence-4-scan-r2.log
  /usr/bin/clang++ --version > $L/clang-version-r2.log 2>&1; echo EXIT=$? >> $L/clang-version-r2.log
  ```

  The scan found 0 matches, EXIT=1 (`logs/evidence-4-scan-r2.log`); the compiler
  version log ends EXIT=0 (`logs/clang-version-r2.log`).

- Repository feature gates, re-run on the committed report:

  ```
  python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-ci-v041-4 > $L/gates-r2.log 2>&1; echo EXIT=$? >> $L/gates-r2.log
  ```

  | gate | result |
  | --- | --- |
  | fork-sources | PASS (exit 0) |
  | all-sources-reproduce | PASS (exit 0) |
  | no-upstream-regression | PASS (exit 0) |
  | check-namespace | PASS (exit 0) |
  | no-tautology | PASS (exit 0) |
  | doc-figures | PASS (exit 0) |
  | complexity | PASS (no new item in your files) |
  | file-length | PASS (no new item in your files) |

  RESULT: PASS, EXIT=0 (`logs/gates-r2.log`).

## 4. Unverified

The hosted `macos-arm64` and `windows-arm64` builds were not run here; hosted CI
must confirm them. The local compiler used for the compile half is
`/usr/bin/clang++`, `clang version 23.1.5`, target
`x86_64-pc-linux-gnu` (`logs/clang-version-r2.log`, EXIT=0;
`logs/evidence-5-clang.log`).

## Self-check

- **Regression test fails with the change reverted, passes with it.**
  Re-inserting the `template` keyword (the reverse of the fix) makes both halves
  fail: ctest `EXIT=8` (`logs/test-red-394-r2.log`,
  `logs/test-red-400-r2.log`, `logs/test-red-522-r2.log`,
  `logs/test-red-528-r2.log`) and the scanner `EXIT=1`
  (`logs/test-red-394-clang-r2.log` and the other three `-clang-r2.log` files).
  With the committed source, ctest is `EXIT=0` (`logs/test-green-r2.log`) and
  the scanner is `EXIT=0` (`logs/test-green-clang-r2.log`).

- **Deterministic.** The guard is a static source scan plus a compile of the
  real header; it uses no wall-clock timing, sleeps or races. The only seam is
  the injected scratch include (`--atomic-queue-include $L/aq-scratch/include`);
  the unmutated control is `EXIT=0` (`logs/test-compile-green-r2.log`) and the
  mutated runs are `EXIT=1` (`logs/test-compile-red-$N-r2.log`).

- **Fix at the cause and complete.** The four ill-formed calls at
  `atomic_queue.h:394,400,522,528` no longer carry the keyword, and the
  well-formed explicit-argument calls at :353, :358, :438, :443 are unchanged.
  A repo-wide scan for `::template name(` returned 0 matches, `EXIT=1`
  (ripgrep no-match, `logs/evidence-4-scan-r2.log`), so no caller or sibling
  uses the rejected form.

- **No collateral damage.** Only the redundant keyword was dropped; each call
  stays `Base::`-qualified and resolves to the same function, so the queue
  semantics and the on-disk/on-wire data are unchanged. The scanner reports no
  other occurrence repo-wide, so the added check does not flag other callers
  (`logs/evidence-4-scan-r2.log`, EXIT=1).

- **ALREADY-FIXED evidence.** `plugins/SfzPlayer/sfizz/external/atomic_queue/include/atomic_queue/atomic_queue.h:394,400,522,528`
  read `Base::do_pop_any(` / `Base::do_push_any(` (`logs/evidence-1-lines.log`),
  changed by commit `3cc4c0a075` (`logs/evidence-3-show.log`) and recorded in
  `plugins/SfzPlayer/LICENSE-NOTICE.md:11-13`.

- **Sibling commands.** No `app.*` or `arrangement.*` command file is touched
  by this cluster; the fix is confined to the vendored atomic_queue header, so
  the listed sibling commands are not affected.

## Other findings

None.
