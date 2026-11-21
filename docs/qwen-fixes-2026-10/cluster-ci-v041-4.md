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
0 matches, EXIT=1 (ripgrep's "no match" code) — `logs/evidence-4-scan.log`.

## 2. Why the finding's CMake `-Wno` flag is not needed

The ill-formed construct is gone from the source, so no warning suppression is
needed. Clang treats it as an error (`-Wmissing-template-arg-list-after-template-kw`),
so a `-Wno-...` (or `-Wno-error`) flag could not waive the CI line in any case.
The vendored code was fixed at the cause instead, the way the earlier cluster's
upstream check describes: upstream atomic_queue removed the construct when it
restructured these functions (the v1.9.0 tag has no `do_pop_any`/`do_push_any`;
the vendored copy is the sfizz 1.2.3 pin), and `Base` is the current
instantiation's base, so the unqualified-through-base call resolves to the same
function — no semantics, code generation, realtime or save/load behaviour change.

## 3. Commands and EXIT codes

- `ctest -R ClangTemplateKeywordCheck` on the fixed tree: `100% tests passed out
  of 1`, EXIT=0 (`logs/test-1-green.log`).
- `tests/clang-template-keyword-check.py` run directly: `PASS: no 'template'
  keyword ...` and `PASS: /usr/bin/clang++ compiles the real atomic_queue
  header`, EXIT=0 (`logs/test-1b-clang-green.log`).
- Re-inserting the keyword at :394, :400, :522, :528 each turns the ctest red —
  `0% tests passed, 1 tests failed out of 1`, EXIT=8
  (`logs/test-red-394.log`, `logs/test-red-400.log`, `logs/test-red-522.log`,
  `logs/test-red-528.log`).
- The same four mutations run through the script's scanner half: `FAIL: 1
  'template' keyword use(s) ...`, EXIT=1 each
  (`logs/test-red-394-clang.log`, `logs/test-red-400-clang.log`,
  `logs/test-red-522-clang.log`, `logs/test-red-528-clang.log`).
- After restoring, the script's compile half passes: both PASS lines, EXIT=0
  (`logs/test-7-compile-green.log`).
- Each mutation on the compile-only scratch copy: scanner PASS, then the real
  `/usr/bin/clang++` rejects the header — `atomic_queue.h:N:col: error: a
  template argument list is expected after a name prefixed by the template
  keyword [-Wmissing-template-arg-list-after-template-kw]` — EXIT=1 for
  :394 (col 31), :400 (col 24), :522 (col 31), :528 (col 24)
  (`logs/test-compile-red-394.log`, `logs/test-compile-red-400.log`,
  `logs/test-compile-red-522.log`, `logs/test-compile-red-528.log`).
- Restore checks on the working tree: `git apply --check` of each of the four
  mutation patches EXIT=0 and `git diff --quiet` on atomic_queue.h EXIT=0, i.e.
  the header is back to the committed, fixed state (`logs/restore-check.log`).
- Second green run of the guard test: `100% tests passed out of 1`, EXIT=0
  (`logs/test-6-green.log`); script directly, both PASS lines, EXIT=0
  (`logs/test-6b-clang-green.log`).
- `feature-check kads-ci-v041-4`: all eight gates PASS
  (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace,
  no-tautology, doc-figures, complexity, file-length), `RESULT: PASS`, EXIT=0
  (`logs/gates-1.log`).

## 4. Unverified

The hosted `macos-arm64` and `windows-arm64` builds were not run here; hosted CI
must confirm them. The local compiler used for the compile half is
`/usr/bin/clang++`, `clang version 23.1.5`, target
`x86_64-pc-linux-gnu` (`logs/clang-version.log`, `logs/evidence-5-clang.log`).

## Other findings

None.
