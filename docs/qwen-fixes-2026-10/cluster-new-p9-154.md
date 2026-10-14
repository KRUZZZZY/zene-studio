# Cluster new-p9-154: 24-bit GIG decode shift

**Verdict: FALSE-POSITIVE**

## Finding and evidence

The finding points to the two byte-assembly expressions in [GigPlayer.cpp](../../plugins/GigPlayer/GigPlayer.cpp#L581): left channel at lines 581–584 and right channel at lines 596–599. Each input is read through a `uint8_t*`, then integral-promoted to `int` on this 32-bit-`int` target. The shift counts are 8, 16, and 24; each byte is nonnegative and every count is less than 32.

The C++20 draft's [shift-operator rule, §7.6.7](https://timsong-cpp.github.io/cppwp/n4861/expr.shift) defines `E1 << E2` as the value congruent to `E1 × 2^E2` modulo `2^N`, where `N` is the result width. Thus shifting `0x80` left by 24 produces the signed `int` value with bit pattern `0x80000000`; the operation is not signed-overflow UB in C++20. The configured GigPlayer build uses `-std=gnu++20`.

A temporary focused Qt test reproduced the helper expression and ran it with `-fsanitize=shift -fno-sanitize-recover=shift`. With a volatile `0x80` high byte to ensure a runtime shift, it passed 1/1 tests. That test and its temporary helper/CMake wiring were removed; no production or test behavior change is warranted for this report.

## Evidence table

| Evidence | Command/result |
|---|---|
| RED | N/A: the report is a false-positive, so no failing regression is retained. The focused sanitizer probe below passed against the original shift expression. |
| GREEN | N/A: no production fix was made. |
| Focused affected-family count | `GigPlayerDecodeTest`: 1 CTest case executed, 1 passed in each exploratory run. |
| Complexity gate | `bash tests/complexity-gate.sh --check` — exit 0. |
| File-length gate | `bash tests/file-length-gate.sh --check` — exit 0. |
| Files committed | `docs/qwen-fixes-2026-10/cluster-new-p9-154.md` only. |

## Commands and exit codes

All captured output is under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-154.r1/logs`.

| Command | Working directory | Exit | Log / note |
|---|---|---:|---|
| `cmake --build build --target GigPlayerDecodeTest` | worktree | 2 | `build-1.log`; CMake had not regenerated the newly added target yet (`No rule to make target`). |
| `cmake --build build --target zene` | worktree | 0 | Triggered CMake's automatic regeneration. This intermediate build's output went to the terminal; the final build was repeated with capture below. |
| `cmake --build build --target GigPlayerDecodeTest` | worktree | 0 | `build-2.log`. |
| `cmake --build build --target zene gigplayer` | worktree | 0 | `build-3.log`. |
| `ctest -R '^GigPlayerDecodeTest$' --output-on-failure` | `build/tests` | 0 | `test-1.log`; 1/1 passed. |
| `cmake --build build --target GigPlayerDecodeTest` | worktree | 0 | `build-4.log`; rebuilt after making the high byte volatile for a runtime sanitizer check. |
| `ctest -R '^GigPlayerDecodeTest$' --output-on-failure` | `build/tests` | 0 | `test-2.log`; 1/1 passed with the volatile high byte and shift sanitizer enabled. |
| `cmake --build build --target zene gigplayer` | worktree | 0 | `build-5.log`; rebuilt after removing the exploratory code changes. |
| `bash tests/complexity-gate.sh --check` | worktree | 0 | `gate-1.log`. |
| `bash tests/file-length-gate.sh --check` | worktree | 0 | `gate-2.log`. |
