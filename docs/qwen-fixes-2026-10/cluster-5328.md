FIXED

# Cluster 5328 — `ProjectFile.write` destroys the previous file when the write fails

`LuaProjectFile::write()` (`src/core/ScriptBindings.cpp:1069-1084`) opened the target with
`QFile::open(QIODevice::WriteOnly | QIODevice::Truncate)`. `Truncate` empties the file at open
time, before `write()`/`flush()` can fail, so a short write (full disk, `RLIMIT_FSIZE`, quota)
left a truncated or empty file where a complete one used to be.

The fix replaces that whole-or-nothing claim with a real one: the contents are written to a
`QSaveFile` (a sibling temporary file) and only `commit()` — which renames the temporary over
the target — makes them visible. A short `write()` calls `cancelWriting()` and returns `false`,
so the previous contents survive untouched. A directory target is refused at open time with
the same `ProjectFile.write: cannot open ...` error as before, because `QSaveFile` cannot
atomically replace a directory and would otherwise fail only at commit.

The regression is `projectFileWriteKeepsTheOriginalWhenTheKernelCutsItShort()` in
`tests/src/core/ScriptBindingsTest.cpp`.

## Evidence in the tree

- `src/core/ScriptBindings.cpp:1078` (before the change, on this branch): the truncating
  `file.open(QIODevice::WriteOnly | QIODevice::Truncate)` that the finding describes. The
  reverted-state run below reaches this line and fails, which is what makes the verdict
  FIXED rather than ALREADY-FIXED.
- `src/core/ScriptBindings.cpp:1076-1084` (after the change): the directory guard, then
  `QSaveFile file(resolved); file.open(QIODevice::WriteOnly)`, `cancelWriting()` on a short
  write, and `return file.commit();`.
- `tests/src/core/ScriptBindingsTest.cpp`: `ScopedFileSizeLimit` (POSIX only) and the test.

## Verification commands

Exit codes are as printed by `quiet-run`; a copy of this list is saved with the logs as
`017-EVIDENCE.txt`. Logs `01`-`10` are this session's earlier evidence; logs `001`-`007`
were re-run while writing this report, so each command and exit code has a saved log.

| step | command (cwd) | exit | log |
|---|---|---|---|
| build, product reverted | `cmake --build build --target ScriptBindingsTest -j$(nproc)` (worktree) | 0 | `001-11-build-reverted.log` |
| test, product reverted | `ctest -R '^ScriptBindingsTest$' --output-on-failure` (`build/tests`) | 8 | `003-12b-test-reverted.log` |
| restore fix | `git apply .../fix.patch` (worktree) | 0 | `017-EVIDENCE.txt` (sha256 73a86ab6…) |
| build, fix restored | `cmake --build build --target ScriptBindingsTest -j$(nproc)` (worktree) | 0 | `004-13-build-restored.log` |
| test, fix restored | `ctest -R '^ScriptBindingsTest$' --output-on-failure` (`build/tests`) | 0 | `005-14-test-restored.log` |
| slot detail | `./ScriptBindingsTest projectFileWriteKeepsTheOriginalWhenTheKernelCutsItShort projectFileWrapper` (`build/tests`) | 0 | `007-16-slot.log` |
| gate | `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> 5328` | see below | `005-15-feature-check.log` |

Earlier session logs, same results:

- `01-build-red.log` — test built with the product change still present.
- `02-test-red.log` — `299 - ScriptBindingsTest (Failed)`, `projectFileWriteKeepsTheOriginalWhenTheKernelCutsItShort`.
- `03-build-green.log` — built with the fix; `04-test-green.log` — `1/1 Test #299 ... Passed`.
- `05-slots-green.log` — `Totals: 4 passed, 0 failed`.
- `05-build-reverted.log` — rebuilt with the product change reverted; `06-test-reverted.log` — `299 - ScriptBindingsTest (Failed)`.
- `07-build-restored.log` — rebuilt with the fix restored; `08-test-restored.log` — `1/1 Test #299 ... Passed`.
- `08b-slot-restored.log` — `Totals: 3 passed, 0 failed`.
- `09-feature-check.log` — every gate `PASS ... exit 0`, `RESULT: PASS`.
- `10-test-final.log` — `1/1 Test #299 ... Passed`.

The gate run recorded in `005-15-feature-check.log` exited 1 only because the report did not
exist yet and the working tree still held the fix (`FAIL report ... missing`, `FAIL working
tree clean`); its own gate item passed: `PASS gates (feature_check.py)`. The same check is
re-run after the commit and recorded in `018-submit-check-final.log` (exit noted there).

## Test matrix

| state | build | `ScriptBindingsTest` | slot output |
|---|---|---|---|
| product change reverted, test kept | exit 0 | exit 8, `projectFileWriteKeepsTheOriginalWhenTheKernelCutsItShort` FAIL | — |
| fix restored | exit 0 | exit 0, 1/1 passed | exit 0, 4 passed |
| fix restored (earlier session) | exit 0 (`07`) | exit 0, 1/1 passed (`08`) | exit 0, 3 passed (`08b`) |

What the test proves: (a) a normal 20 000-byte write through the Lua `projectFile:write`
succeeds and reads back; (b) with `RLIMIT_FSIZE` capped at 8 bytes the kernel cuts the
20 000-byte write — `write` returns `false`, the script still completes, and the file still
holds its 18 000 original bytes; (c) after the cap is lifted a shorter write replaces the
file wholly.

## Platform gating

The short-write reproduction is POSIX-only: it relies on `getrlimit`/`setrlimit(RLIMIT_FSIZE)`
and ignores `SIGXFSZ`, all behind `#if defined(Q_OS_UNIX)`; elsewhere the test calls
`QSKIP("RLIMIT_FSIZE is a POSIX facility")`. The product code (`QSaveFile`) is portable and
unguarded; only the fault injection is gated.

The directory guard is deliberate: `QFile` failed at `open()` on a directory, and `QSaveFile`
would instead create a sibling temporary and fail at `commit()`, changing when the error is
raised. `QFileInfo(resolved).isDir()` keeps the old `cannot open` mapping.

## Self-check

1. **Regression test fails with the product change reverted, passes restored.**
   Reverted: build exit 0 (`001`), ctest exit 8 with `FAIL! ... projectFileWriteKeepsTheOriginalWhenTheKernelCutsItShort` (`003`).
   Restored: build exit 0 (`004`), ctest exit 0, `1/1 Test #299 ... Passed` (`005`). Earlier logs `06` (fail) and `08` (pass) agree.
2. **Deterministic.** No sleeps, no wall clock, no threads in the test. The fault is injected
   with values: `ScopedFileSizeLimit(8)` over an 18 000-byte original and a 20 000-byte
   payload, `SIGXFSZ` ignored so the kernel turns the overflow into a short `write`; work
   happens in a `QTemporaryDir`. `QVERIFY2(limit.active(), ...)` fails the test rather than
   silently passing if the limit cannot be set.
3. **At the cause, complete.** The cause is truncation at open; `QSaveFile` defers visibility
   to `commit()` and `cancelWriting()` discards the temp on a short write. `LuaProjectFile::write`
   is the only implementation behind the `projectFile:write` binding; `read`, `list`,
   `projectDir` and the path resolver are untouched, and no other caller of `write` relied on
   truncate-at-open. Sibling bindings that read an existing file keep reading it, since the
   commit is atomic.
4. **No collateral damage.** A normal write over 16 KiB still succeeds and round-trips (case a);
   a shorter write after the limit replaces the file wholly (case c); a directory target still
   throws `ProjectFile.write: cannot open ...` at the same point as before. Existing project
   files are only replaced by a complete write, so saved data loads as before; no new size
   limit or rejection was added for legitimate writers.
5. **Verdict is FIXED, not ALREADY-FIXED or FALSE-POSITIVE.** The truncating open is present
   at `src/core/ScriptBindings.cpp:1078` on this branch; the reverted run (`003`) exercises it
   and fails, so the defect is real here and the diff is what closes it.
6. **Sibling commands.** No command file was changed by this fix (`git diff` touches
   `src/core/ScriptBindings.cpp` and the test only), so the `app.*`/`arrangement.*` list the
   checker printed comes from other commits on the branch, not from this diff; none of them
   goes through `ProjectFile.write` and none is affected.
