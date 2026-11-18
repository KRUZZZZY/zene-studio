FIXED

# cluster new-p11-92-port-port-port: a long DrumSynth file line silently stopped parameter parsing

## What this job is

A **port job**, not a review. The fix of cluster `new-p11-92-port-port` was accepted on
`bugs/qwen-new-p11-92-port-port`, but it could not ride the newest train: it makes DrumSynth's parse globals
`thread_local`, while the newest line's `tests/src/core/DrumSynthTest.cpp` still declares them as plain
`extern float timestretch;` etc., so `DrumSynthTest` fails to **link** (TLS mismatch) and train 10 could not be
promoted (train doctor, test run 2026-10-21).

This tree is a fresh branch off the newest line, so the fix was **re-applied by hand** (`git show` of each
upstream commit; no merge, no cherry-pick, no rebase):

* `dc3c2983a` — fix: long DrumSynth lines and thread-safe parsing (`src/core/DrumSynth.cpp`)
* `c73c0d0cd` — test: deterministic DrumSynth long-line and concurrent-parse regressions
  (`tests/src/core/DrumSynthTest.cpp`)
* `372039ad0`, `8bbe3bd16` — docs and the final deterministic form of the concurrency case

The port keeps the thread-safe parsing **and** makes the test declarations `extern thread_local`, which is what
the newest line needs to link.

## Verdict: REAL, and now fixed on this branch

The finding is real on this branch. Pre-fix evidence (product file at the branch base `0a8842d39`):

* `src/core/DrumSynth.cpp:261` (base) — `std::array<char, 200> line = {};` and
  `src/core/DrumSynth.cpp:287` (base) — `is.getline(line.data(), line.size());`
  A line longer than 199 bytes sets the stream's failure state, so the scan ends there and every setting
  behind it silently falls back to its default.
* `src/core/DrumSynth.cpp:290-291` (base) — `std::strtok(line.data(), " \t=")` / `std::strtok(nullptr, "\n\r\0")`.
  `strtok` keeps its position in **one process-wide pointer**, so two threads tokenising at the same time take
  each other's tokens.
* `src/core/DrumSynth.cpp:78-87` (base) — the parse/render state (`envpts`, `envData`, `chkOn`, `sliLev`,
  `timestretch`, `DD`, `clippoint`, `DF`, `phi`, `wavewords`, `wavemode`, `mem_*`) was **process-wide**, so two
  threads parsing a `.ds` file at the same time overwrote each other's settings.

Post-fix, in this branch:

* `src/core/DrumSynth.cpp:267` — `std::string line;` (dynamically sized; no fixed buffer, no failure state)
* `src/core/DrumSynth.cpp:288` — `std::getline(is, section, ']');` (dynamic section name)
* `src/core/DrumSynth.cpp:294-341` — the line is tokenised with `std::string` (`find_first_not_of(" \t=")`,
  `find_first_of(" \t=")`, `find_first_of("\n\r")`), with no `strtok` anywhere in the file
* `src/core/DrumSynth.cpp:81-90` — the parse/render state is `thread_local`
* `tests/src/core/DrumSynthTest.cpp:146-148` — the test declarations are `extern thread_local float envpts[8][3][32];`,
  `extern thread_local float timestretch;`, `extern thread_local int chkOn[8];` — **this is the part the port had to
  add**; with plain `extern` the test does not link against the fixed product (see RED stage 1).

The concrete user-visible damage of the pre-fix parser: a `.ds` file whose `[General]` section holds a line longer
than 199 bytes loses its `Version` key, `DrumSynth::GetDSFileSamples` returns 0 frames (`src/core/DrumSynth.cpp:427-433`)
and the drum sounds different or not at all.

## The fix (hand-ported, same shape as the accepted upstream fix)

1. `src/core/DrumSynth.cpp` — the section name and the setting lines are read into `std::string` instead of a
   200-byte `std::array`; the tokeniser reproduces `strtok`'s key/value split, its `"\n\r\0"` value terminator and
   its trailing-whitespace trim, so a file that parsed before parses the same.
2. `src/core/DrumSynth.cpp` — the tokeniser uses `std::string` only; the process-wide `strtok` pointer is gone.
3. `src/core/DrumSynth.cpp` — the mutable parse/render state is `thread_local`, so two threads parse two `.ds`
   files at the same time without seeing each other's settings.
4. `tests/src/core/DrumSynthTest.cpp` — the three globals the test pokes are declared `extern thread_local`
   (the port's job), and the two upstream regression cases are ported into this branch's version of the file.
5. `tests/upstream-modifications.txt` — the divergence of `src/core/DrumSynth.cpp` is declared with its reason.

## Regression (deterministic) and proof

Two new cases in `tests/src/core/DrumSynthTest.cpp` (the file that already covers `src/core/DrumSynth.cpp`):

* `longLinesDoNotStopProfileParsing` — renders a `.ds` file with an over-long line **in front of** and **between**
  the settings and compares the complete rendered sample vectors with the same file without the long line.
* `concurrentParsesMatchSingleThreadedParses` — renders two fixtures with different settings serially for
  reference, then in two threads. It checks each concurrent render's frame count against its reference and
  compares each thread's plain and long-line sample vectors. The test binary carries a
  stand-in for the C library's `strtok()` that keeps the process-wide pointer and adds a **rendezvous** at the
  tokenisation boundary (a condition variable released by the second arrival — no timeout, no sleep, no wall
  clock), so the interleaving the pre-fix parser was exposed to happens at a known point instead of at a random
  one. The fixture's over-long line keeps the case deterministic where the stand-in is left out (Windows).

### Commands and exit codes (all logs in the run's log folder)

| Stage | Command | Exit | Result |
|---|---|---|---|
| RED 1 — the port's own blocker (test with `extern thread_local` against the **unfixed** product) | `cmake --build build --target DrumSynthTest` | **2** | `ld: _ZN4lmms6envptsE: TLS reference in .../DrumSynthTest.cpp.o mismatches non-TLS definition in .../DrumSynth.cpp.o` — exactly the train-blocking failure |
| RED 2 — assertion-level RED (product with the `thread_local` part only, **old parser**) | `cmake --build build --target DrumSynthTest` | 0 | builds |
| RED 2 | `cd build/tests && ctest -R '^DrumSynthTest$' --output-on-failure` | **8** | `14 passed, 2 failed`: `longLinesDoNotStopProfileParsing` (Actual `leading.size()` 0, Expected `plain.size()` 2400) and `concurrentParsesMatchSingleThreadedParses` (`'longLine == plain' returned FALSE`) |
| GREEN | `cmake --build build --target zene DrumSynthTest DrumSynthLevelTest SampleBufferResampleTest` | 0 | — |
| GREEN | `cd build/tests && ctest -R '^DrumSynth(Test|LevelTest)$' --output-on-failure` | **0** | `100% tests passed out of 2` |
| GREEN (direct binary) | `cd build/tests && ./DrumSynthTest` | **0** | `Totals: 16 passed, 0 failed` (14 pre-existing + the 2 new cases) |
| 4b — reverse the whole product diff | `git diff -- src/core/DrumSynth.cpp > fix.patch && git apply -R fix.patch` | 0 | applied |
| 4b | `cmake --build build --target DrumSynthTest` | **2** | the TLS link error again (the test binary cannot be built without the fix) |
| 4b | `cd build/tests && ctest -R '^DrumSynthTest$' --output-on-failure` | **8** | `Unable to find executable: .../build/tests/DrumSynthTest`, `0% tests passed` |
| 4b — reverse **only the parser hunk** (the `thread_local` part kept, so the test *runs*) | `git apply -R fix-parser-only.patch` | 0 | applied |
| 4b | `cmake --build build --target DrumSynthTest` | 0 | builds |
| 4b | `cd build/tests && ctest -R '^DrumSynthTest$' --output-on-failure` | **8** | `14 passed, 2 failed` — the same two assertion failures as RED 2 |
| 4b — restore | `git apply fix-parser-only.patch` | 0 | applied |
| 4b | `cmake --build build --target DrumSynthTest` → `ctest -R '^DrumSynthTest$'` | 0 / **0** | `100% tests passed out of 1` |
| Affected family | `cd build/tests && ctest -R 'DrumSynth\|SampleBufferResample\|ImportDetection' --output-on-failure` | **0** | `100% tests passed out of 4` (DrumSynthLevelTest, ImportDetectionTest, SampleBufferResampleTest, DrumSynthTest) |
| Determinism | `for i in $(seq 1 30); do ./DrumSynthTest; done` | 0 | `RUNS=30 FAILURES=0` |

## Gates

| Gate | Command | Exit | Result |
|---|---|---|---|
| all gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | **0** | `PASS` on every row: `fork-sources`, `all-sources-reproduce`, `no-upstream-regression`, `check-namespace`, `no-tautology`, `doc-figures`, `complexity`, `file-length` |
| divergence ledger | `bash tests/no-upstream-regression-gate.sh` | **0** | `PASS: every change to upstream-inherited code ... is declared` |
| pre-submit | `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p11-92-port-port-port` | **0** | `RESULT: PASS - submit` |

**Two of the gates were red on the branch base, before any change of this cluster — and one of them was not
mine to leave red.** `bash tests/fork-sources-gate.sh` (Gate 9) runs `tests/all-sources-reproduce.sh`, and the
whole-tree manifest `tests/all-sources.txt` no longer equalled the output of the recipe in its own header:
the train-10 snapshot commit `5ef150729` ("all-sources re-derived") wrote **1148 vendored
`plugins/SfzPlayer/sfizz/**` entries** into the entry list, while the header's recipe — added with the sfizz
feature in `39f297929` — excludes that vendored tree on purpose ("Vendored third-party trees are excluded on
purpose - they are not this repo's code and no gate here rewrites them"). Proof that it was pre-existing: with my
three files restored to the branch base (working tree clean), the gate exits **1** with the same message, and the
whole manifest/recipe diff is those 1148 sfizz paths and nothing else.

The second commit of this branch therefore re-derives the entry list with the recipe from the manifest's own
header — the only difference is the removal of the 1148 vendored sfizz entries. No baseline entry
(`tests/file-length-baseline*.tsv`, `tests/complexity-baseline*.tsv`) names any of them, so no ratchet is
re-anchored, and Gate 9's other checks (every tracked source registered, no stale entry) still pass:
`1958 fork-sources entry(ies), 1214 all-sources (whole-tree), 48 tools-sources, 0 stale entry(ies)`.

The whole-tree (`--scope all`) ratchets are red on this train for unrelated reasons (`tests/src/wasm/WasmWorkerPoolTest.cpp`,
`thirdparty/mts-esp/libMTSClient.cpp`, `include/RemotePluginBase.h` …) — they are red with the base manifest as
well and are not part of the `feature_check.py` command, which measures the fork scope.

`complexity` and `file-length` pass in the fork scope: the new functions have CCN ≤ 10, and
`tests/src/core/DrumSynthTest.cpp` measures 440 code lines (limit 500) after the port.

## Self-check

**1. Does the regression test FAIL with the product change reverted and PASS with it?**
Yes, twice over, with the exit codes in the table above.
* Reverting the **whole** product diff (`git apply -R fix.patch`): the build fails with exit **2** and the link
  error `TLS reference in ...DrumSynthTest.cpp.o mismatches non-TLS definition in ...DrumSynth.cpp.o`; `ctest`
  exits **8** (`Unable to find executable`). This is the very failure that blocked train 10, so the port's
  `extern thread_local` declarations are part of the proof, not a workaround.
* Reverting the **parser** hunk only (the `thread_local` part kept, so the test still builds and *runs*):
  `ctest` exits **8** with `14 passed, 2 failed` — `longLinesDoNotStopProfileParsing`
  (`Actual (leading.size()): 0`, `Expected (plain.size()): 2400`) and `concurrentParsesMatchSingleThreadedParses`
  (`'longLine == plain' returned FALSE`).
* With the fix restored: `ctest -R '^DrumSynthTest$'` exits **0** (`Totals: 16 passed, 0 failed`), the family
  `ctest -R 'DrumSynth|SampleBufferResample|ImportDetection'` exits **0** (4/4).

**2. Is the test deterministic — no wall-clock timing, sleeps or races?**
Yes. The only synchronisation is the `strtok` stand-in's rendezvous in the test binary: two `std::condition_variable`
barriers that are released by the **second arrival**, never by a timeout. There is no sleep, no wall-clock read and
no scheduler-dependent wait anywhere in the two cases. The 30 consecutive runs of the whole binary
(`RUNS=30 FAILURES=0`, `test-repeat-30.log`) show the case does not depend on the machine's load. The seam is the
stand-in itself: it replaces the C library's `strtok()` for the test binary and keeps the library's process-wide
pointer semantics, so the parser under test behaves exactly as it did before the fix.

**3. Is the fix at the cause and complete — every caller and sibling checked?**
Yes.
* `DrumSynth::GetPrivateProfileString` (`src/core/DrumSynth.cpp:257`) is the **only** entry point for a `.ds`
  setting. Its callers — `GetPrivateProfileInt` (`:351`), `GetPrivateProfileFloat` (`:365`) and
  `DrumSynth::GetEnv` (`:176`) — all go through it, so they inherit the fix unchanged; `GetEnv`'s own 8-byte token
  buffer and its `tokenTooLong` guard are untouched.
* The `buffer` contract is unchanged: the value is still copied with `std::strncpy(buffer, valueToken.c_str(), maxlen)`
  into the caller's buffer of `size` bytes, and the added `buffer[maxlen] = '\0'` only makes the termination the
  `std::memset` already provided explicit.
* The globals changed to `thread_local` (`src/core/DrumSynth.cpp:81-90`) are referenced **only** inside
  `src/core/DrumSynth.cpp` and in `tests/src/core/DrumSynthTest.cpp` (a repository-wide grep for `envpts`,
  `envData`, `chkOn`, `sliLev`, `timestretch`, `DD[`, `clippoint`, `DF[`, `phi[`, `wavewords`, `wavemode`, `mem_t`,
  `mem_o`, `mem_n`, `mem_b`, `mem_tune`, `mem_time` finds no other user). The test's three declarations were
  updated to `extern thread_local` in the same change, so declaration and definition agree.
* The one production consumer of the whole thing is `decodeSampleDS` (`src/core/SampleDecoder.cpp:109-134`), which
  parses **and** renders inside one call; the state never crosses a thread boundary, so `thread_local` cannot
  split a render in two. There is no DrumSynth instrument plugin in this tree, so no audio-thread path touches
  these globals.

**4. No collateral damage — do old projects and saved data still load as before?**
Yes.
* For every `.ds` file that the old parser could read to the end, the new tokeniser produces the same key and the
  same value: the key is the text up to the first space/tab/`=`, the value is the rest of the line up to `\r`/`\n`,
  with the trailing spaces/tabs trimmed — exactly what `strtok(line, " \t=")` + `strtok(nullptr, "\n\r\0")` plus the
  old manual trailing-whitespace trim did. CRLF files end their value at the `\r` in both implementations.
* The only behaviour that changes is the bug itself: a line longer than 199 bytes no longer ends the scan. The
  200-byte limit was never a documented contract — it was the buffer size — and the fix only *adds* settings that
  the file states; no setting that used to be read changes value.
* The `thread_local` change does not change any single-threaded result: the state is still initialised to the same
  values and still reset the same way at the start of every `GetDSFileSamples` call. The whole existing DrumSynth
  test family (`DrumSynthTest` 16 cases, `DrumSynthLevelTest`, `ImportDetectionTest`,
  `SampleBufferResampleTest`) passes unchanged.
* No new limit or check was added that another caller could legitimately hit: the only new bound is "the line is as
  long as it wants to be", which is unbounded on purpose.

**5. Verdict evidence (ALREADY-FIXED / FALSE-POSITIVE would need it).**
Not applicable: the verdict is **FIXED**, proved by the file:line evidence in the "Verdict" section and by the
RED/GREEN table above. The branch did **not** already contain the fix — the base `src/core/DrumSynth.cpp` still had
`std::array<char, 200> line` and `std::strtok` at lines 261/290-291, and the base
`tests/src/core/DrumSynthTest.cpp:116-118` still declared the globals as plain `extern`, which is why
`DrumSynthTest` did not link against the fixed product.

**6. Realtime, undo and commands.**
* Nothing on an audio thread allocates, frees, locks, waits or logs: the changed code is the `.ds` **file parser**,
  reached from the import/load path (`SampleDecoder::decodeSampleDS`) and never from `processImpl`/`playImpl`. The
  `thread_local` state is constant-initialised (no dynamic initialiser, no per-thread allocation) and is touched
  only inside the parse/render call.
* No new control command was added (the `ControlCommandsSnapshot` canary is untouched).
* No undo step was added: a bug fix in a file parser has no command, no undoable state.

## Files committed

* `src/core/DrumSynth.cpp` — the fix (dynamic line reading, `std::string` tokeniser, `thread_local` parse state)
* `tests/src/core/DrumSynthTest.cpp` — `extern thread_local` declarations (the port's blocker) and the two
  deterministic regression cases
* `tests/upstream-modifications.txt` — the declared divergence of `src/core/DrumSynth.cpp`
* `tests/all-sources.txt` — the whole-tree scope manifest re-derived from its own header recipe, so Gate 9 and
  the recipe check are green again (pre-existing train-level defect, separate commit)
* `docs/qwen-fixes-2026-10/cluster-new-p11-92-port-port-port.md` — this report

Two commits, both on `bugs/qwen-new-p11-92-port-port-port`:

1. `fix: long DrumSynth lines and thread-safe parsing (cluster new-p11-92-port-port-port)` — the fix, the
   regression tests and this report.
2. `chore: re-derive tests/all-sources.txt from its own header recipe (cluster new-p11-92-port-port-port)` — the
   gate repair described above.
