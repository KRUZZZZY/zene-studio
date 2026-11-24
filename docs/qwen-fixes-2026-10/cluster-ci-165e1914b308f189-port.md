ALREADY-FIXED

# Cluster ci-165e1914b308f189-port — Port: CI regression: build / windows-arm64

This unit ports cluster `ci-165e1914b308f189` to this branch. That cluster's hosted `windows-arm64` (Clang) build
failed in the vendored sfizz `atomic_queue.h`:

```
atomic_queue.h:394:31: error: a template argument list is expected after a name prefixed by the
template keyword [-Wmissing-template-arg-list-after-template-kw]
  394 |         return Base::template do_pop_any(states_[index], elements_[index]);
```

The product fix is already on this branch, so the product code stays **ALREADY-FIXED**. In round 3 (this round)
the regression check this branch owns was strengthened: before, its real-header compile probe was skipped on GCC
hosts. Now it also runs there, through Clang.

## Evidence (file:line)

- `plugins/SfzPlayer/sfizz/external/atomic_queue/include/atomic_queue/atomic_queue.h:394,400,522,528` now read
  `Base::do_pop_any(...)` / `Base::do_push_any(...)`, with no `template` keyword before a deduced call. The fix
  arrived in commit `3cc4c0a075` (cherry-picked from `ee9b376701`), which is an ancestor of this branch.
- The regression check is ctest `ClangTemplateKeywordCheck`, in `tests/CMakeLists.txt` (the block after
  `ExportedClassTlsCheck`, about lines 7098-7120), and the script `tests/clang-template-keyword-check.py`.

## Round 3: the check now runs the Clang compile on GCC hosts

The reviewer's point was that the probe was dead on this box. The check passed only `--compiler
${CMAKE_CXX_COMPILER}`, which is GCC (`/usr/bin/c++`). GCC accepts the known-bad fixture, so the probe printed
"skipped" and only the source scan ran. Changes:

- `tests/clang-template-keyword-check.py`:
  - `--compiler` is now repeatable (`action="append"`).
  - A new pure function, `select_probe_compiler(compilers, rejects)`, picks the first supplied compiler that
    rejects the bad fixture.
  - `compile_probe` uses that function, and is skipped only when none of the supplied compilers enforces the rule.
  - `compiler_rejects_bad_syntax` returns False instead of raising when the compiler binary is missing.
  - The self-test gained `compiler_selection_failures()`, three cases with an injected `rejects` seam:
    - `[g++, clang++]` → `clang++` (a GCC-then-Clang pair still runs the probe).
    - `[g++]` → None.
    - `[]` → None.
- `tests/CMakeLists.txt`: `find_program(ZENE_TEMPLATE_KW_CLANGXX NAMES clang++)`. When it is found and differs
  from `CMAKE_CXX_COMPILER`, a second `--compiler` is passed.

## Commands and EXIT codes (this session; logs in `~/.config/kads/zene/state/runs/ci-165e1914b308f189-port.r3/logs/`, each with a `.cmd` file)

| # | Command | EXIT | Log |
| --- | --- | --- | --- |
| 1 | `python3 tests/clang-template-keyword-check.py --self-test` | 0 ("red fixture flagged at the expected lines, green fixture clean, compiler-selection cases correct") | `1-selftest.log` |
| 2 | `cmake build` | 97 (refused by the environment guard; configuring is not the agent's job) | `2-cmake.log` |
| 3 | `cmake --build build --target cmake_check_build_system` (regenerates after the `tests/CMakeLists.txt` change) | 0 | `5-regen.log` |
| 4 | `cd build/tests && ctest -R ClangTemplateKeywordCheck -V` | 0. Test command carries `"--compiler" "/usr/bin/c++" "--compiler" "/usr/bin/clang++"` and prints `PASS: /usr/bin/clang++ compiles the real atomic_queue header (AtomicQueue2 and AtomicQueueB2 instantiated)`; `100% tests passed out of 1` | `6-ctest.log` |
| 5 | **RED (Clang probe)**: `python3 tests/clang-template-keyword-check.py --root . --compiler /usr/bin/c++ --compiler /usr/bin/clang++ --atomic-queue-include <logs>/prefix-headers` (the pre-fix `atomic_queue.h` and `defs.h` from `git show 3cc4c0a075^:…`) | **1**. The scan passes, then the Clang probe fails with the CI diagnostic `atomic_queue.h:394:31: error: a template argument list is expected after a name prefixed by the template keyword [-Wmissing-template-arg-list-after-template-kw]`, plus the sites at 400, 522 and 528 | `7-red.log` |
| 6 | **GREEN**: the same command without `--atomic-queue-include` (tree header) | 0 (both PASS lines) | `8-green.log` |
| 7 | control, GCC only: `… --compiler /usr/bin/c++ --atomic-queue-include <logs>/prefix-headers` | 0, `compile probe skipped: none of /usr/bin/c++ rejects the bad fixture`. This is why the check must also be handed clang++ | `9-control-gcc.log` |
| 8 | `git diff 3cc4c0a075^ 3cc4c0a075 -- …/atomic_queue.h > logs/fix.patch && git apply -R logs/fix.patch` (the four hunks reversed) | 0 | (shell) |
| 9 | **RED (ctest)**: `cd build/tests && ctest -R ClangTemplateKeywordCheck --output-on-failure` | **8**, `ClangTemplateKeywordCheck (Failed)`. The source-scan half fires first, at `atomic_queue.h:394,400,522,528` | `10-ctest-red.log` |
| 10 | `git apply logs/fix.patch` (restored; `git status` then showed only the two test files modified) | 0 | (shell) |
| 11 | **GREEN (ctest)**: `cd build/tests && ctest -R ClangTemplateKeywordCheck --output-on-failure` | 0, `100% tests passed out of 1` | `11-ctest-green.log` |
| 12 | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0, `RESULT: PASS` (all eight rows PASS) | `12-feature-check.log` |

Notes:
- In row 9 the ctest goes red on the scan half, because the script returns before the probe when the scan finds
  something.
- Row 5 isolates the Clang-probe half on the pre-fix header and shows the exact CI diagnostic.
- The scratch `prefix-headers` directory was deleted after rows 5 and 7.

## Self-check

- **Verdict evidence:** product code is ALREADY-FIXED.
  - `atomic_queue.h:394,400,522,528` carry the fixed spelling.
  - Reversing those four hunks turns the registered ctest red (row 9, EXIT=8). Restoring them turns it green
    (row 11, EXIT=0).
- **Regression test:** `tests/clang-template-keyword-check.py` (ctest `ClangTemplateKeywordCheck`) was changed
  this round.
  - Red: on the pre-fix header, EXIT=1 with the CI diagnostic (row 5) and EXIT=8 under ctest (row 9).
  - Green: on the tree header, EXIT=0 (rows 6, 4, 11).
  - It is deterministic: a source scan plus `-fsyntax-only` compiles, with no timing and no threads. The
    compiler-selection self-test injects `rejects`, so it needs no particular compiler.
- **Fix at the cause and complete:** the tree-wide scan finds no other `::template name` / `.template name` /
  `->template name` without `<` in tracked C/C++ sources (PASS line in rows 4, 6 and 11). Every caller of
  `compile_probe` (only `main`) was updated to the list form.
  - A missing clang++ is handled: `find_program` leaves it out of the command, and the script treats a missing
    binary as "does not enforce".
  - On MSVC or other hosts without clang++, behaviour is unchanged: one `--compiler`, as before.
- **Sibling commands:** this unit touches no command file. The `app.*` and `arrangement.*` commands listed by
  `submit_check.py` come from the train-wide diff that this branch inherits against its base. This unit does not
  change them, and they are unaffected. The same holds for `tests/control-audio-test-tone.py` and
  `tests/control-no-audio-device.py`, which `submit_check.py` lists as changed tests: they come from that train
  diff too, not from this unit (`git show --stat HEAD~1` lists only the checker, `tests/CMakeLists.txt` and this
  report). No control command was added, so the ControlCommandsSnapshot canary is unchanged.
- **No collateral damage, realtime, undo, commands:** only `tests/clang-template-keyword-check.py`,
  `tests/CMakeLists.txt` and this report changed. No product file, no audio-thread code, no saved-data format.
- **Registration and gates:** both changed files were already registered by `3cc4c0a075`. `feature_check.py` →
  EXIT=0, every row PASS (row 12). The new functions are small (CCN well under 10), and the script stays under
  500 lines (complexity / file-length PASS).

Delegation:
- Sonnet implemented the repeatable `--compiler`, the `select_probe_compiler` seam and self-test cases, and the
  CMake `find_program` change. It also ran rows 1-7, including extracting and then deleting the pre-fix headers.
- Haiku ran the ctest-level red/green (rows 8-11) by reversing and re-applying the `3cc4c0a075` hunks, and ran
  the gate table (row 12).
- I designed the change, reviewed the full diff, and re-read the key log lines myself: the ctest test command,
  the 394:31 diagnostic, the EXIT lines and the gate table.
- One Haiku start was wasted on a no-op.

## Other findings

None.
