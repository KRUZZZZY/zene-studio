# Cluster new-p10-363

Verdict: **FIXED**

`ScriptClockGrid` previously overflowed while calculating the next boundary at
`qint64` max. A large crossing excess was also narrowed to `int` before it was
added to the coalesced count. The grid now tracks whether a later boundary is
representable, uses unsigned distance arithmetic for large spans, and saturates
the coalesced count.

## File and line evidence

- `src/core/ScriptClock.cpp:94` checks the next boundary before multiplication
  and records the saturated position if no later boundary fits.
- `src/core/ScriptClock.cpp:113` bounds the crossing excess before adding it
  and safely advances the next boundary.
- `include/ScriptClock.h:107` carries whether each stored next boundary exists.
- `tests/src/core/ScriptClockTest.cpp:220` covers regridding at `qint64` max;
  `:232` covers a max-sized jump and the saturated coalesced count.

## Regression evidence

| Evidence | Exact command and result |
|---|---|
| Initial RED | `ctest -R '^ScriptClockTest$' --output-on-failure > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-363.r1/logs/test-1.log" 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-363.r1/logs/test-1.log"; exit "$rc"` — exit 8. It ran 1/1 CTest case; both new assertions failed on the original code. |
| Initial GREEN | `ctest -R '^ScriptClockTest$' --output-on-failure > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-363.r1/logs/test-2.log" 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-363.r1/logs/test-2.log"; exit "$rc"` — exit 0, 1/1 passed. |
| Inverse-patch RED | `ctest -R '^ScriptClockTest$' --output-on-failure > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-363.r1/logs/test-3.log" 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-363.r1/logs/test-3.log"; exit "$rc"` — exit 8; both new assertions failed after reversing the production patch. |
| Inverse-patch GREEN | `ctest -R '^ScriptClockTest$' --output-on-failure > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-363.r1/logs/test-4.log" 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-363.r1/logs/test-4.log"; exit "$rc"` — exit 0 after reapplying the fix, 1/1 passed. |
| Final GREEN | `ctest -R '^ScriptClockTest$' --output-on-failure > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-363.r1/logs/test-5.log" 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-363.r1/logs/test-5.log"; exit "$rc"` — exit 0, 1/1 passed. |
| Affected-family test count | 1 CTest case (`ScriptClockTest`), 1 executed and passed after the fix. |
| Complexity gate | `bash tests/complexity-gate.sh --check > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-363.r1/logs/gate-3.log" 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-363.r1/logs/gate-3.log"; exit "$rc"` — exit 0. |
| File-length gate | `bash tests/file-length-gate.sh --check > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-363.r1/logs/gate-4.log" 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-363.r1/logs/gate-4.log"; exit "$rc"` — exit 0. |
| Files committed | `include/ScriptClock.h`, `src/core/ScriptClock.cpp`, `tests/src/core/ScriptClockTest.cpp`, `docs/qwen-fixes-2026-10/cluster-new-p10-363.md`. |

## Build and proof commands

Commands below ran from the worktree root unless a different directory is shown.

```sh
cmake --build build --target ScriptClockTest > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-363.r1/logs/build-1.log" 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-363.r1/logs/build-1.log"; exit "$rc"
```

Exit 0.

```sh
cmake --build build --target zene ScriptClockTest > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-363.r1/logs/build-2.log" 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-363.r1/logs/build-2.log"; exit "$rc"
```

Exit 0.

```sh
cmake --build build --target zene ScriptClockTest > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-363.r1/logs/build-3.log" 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-363.r1/logs/build-3.log"; exit "$rc"
```

Exit 0. The restored proof build command was:

```sh
cmake --build build --target zene ScriptClockTest > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-363.r1/logs/build-4.log" 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-363.r1/logs/build-4.log"; exit "$rc"
```

Exit 0.

```sh
git diff -- include/ScriptClock.h src/core/ScriptClock.cpp > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-363.r1/logs/fix.patch" && git apply -R "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-363.r1/logs/fix.patch"; rc=$?; printf 'EXIT=%s\n' "$rc" >> "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-363.r1/logs/proof-1.log"; exit "$rc"
```

Exit 0; the reverted build and RED CTest are recorded in `build-3.log` and
`test-3.log`.

```sh
git apply "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-363.r1/logs/fix.patch"; rc=$?; printf 'EXIT=%s\n' "$rc" >> "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-363.r1/logs/proof-2.log"; exit "$rc"
```

Exit 0; the restored build and GREEN CTest are recorded in `build-4.log` and
`test-4.log`. The final rebuild of `zene` and `ScriptClockTest` exited 0 in
`build-5.log`.

The exact final rebuild command was:

```sh
cmake --build build --target zene ScriptClockTest > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-363.r1/logs/build-5.log" 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-363.r1/logs/build-5.log"; exit "$rc"
```

Exit 0.

CTest commands ran from `build/tests`. The final command was:

```sh
ctest -R '^ScriptClockTest$' --output-on-failure > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-363.r1/logs/test-5.log" 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-363.r1/logs/test-5.log"; exit "$rc"
```

Exit 0.

The final gate logs are `gate-3.log` and `gate-4.log` in the specified log folder.
