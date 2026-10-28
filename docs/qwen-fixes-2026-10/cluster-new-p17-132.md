Verdict: FALSE-POSITIVE

new-p17-132: FALSE-POSITIVE on this toolchain (Qt 6.12.0). The walk refuses whitespace-only CDATA in the content element's interior today, so the finding's splice does not happen. The refusal rests on Qt's `isWhitespace()` returning false for CDATA sections, which the Qt documentation I could find does not state. Explicit `!xml.isCDATA()` hardening is recommended as a follow-up and is not made here (see "Not changed" below).

## Evidence

**Code, `src/core/ProjectContainerEntries.cpp`:**

- `:315` is the `indexOf( '<', startTagEnd )` the finding names. It is only correct if the interior holds no `<` byte.
- `:319` is `if( token == Characters && xml.isWhitespace() ) { continue; }`. A CDATA section is a `Characters` token; it is skipped only if Qt reports it as whitespace.
- `:321-324` refuses every other interior token with "the interior of <%1> is not whitespace-only".
- `:298-299` states the contract the splice relies on: once only whitespace has been seen, the first `<` after the start tag is the close tag's `<`.
- Both callers go through the same walk: `locateContentInterior` (`:339-353`) is called from the deriver at `:536` and from the reassembler at `:582`.

**Qt token stream, measured on this machine** (`logs/qt-cdata-probe.cpp`, output in `logs/qt-cdata-probe-output.log`; Qt 6.12.0 from `/usr/include/qt6/QtCore/qconfig.h:73`):

```
DOC <song><![CDATA[  ]]><t/></song>        -> chars ws=0 cdata=1 text=[  ]
DOC <song>\n  <![CDATA[  ]]>\n</song>       -> chars ws=1 cdata=0 ... chars ws=0 cdata=1 text=[  ] ... chars ws=1 cdata=0
DOC <song><![CDATA[]]></song>               -> chars ws=0 cdata=1 text=[]
DOC <song>  <![CDATA[ ]]>  </song>          -> chars ws=1 cdata=0 ... chars ws=0 cdata=1 text=[ ] ... chars ws=1 cdata=0
DOC <song>&#32;</song>                      -> chars ws=0 cdata=0 text=[ ]
```

Every CDATA section, including whitespace-only ones, reports `isWhitespace()==0`, so `:319` does not skip it and `:321` refuses it. The finding's premise ("accepts whitespace CDATA as whitespace") does not hold on this Qt.

**Refusal reached in the tests:** the only emitter of "not whitespace-only" is `src/core/ProjectContainerEntries.cpp:321`, and the new tests below pass only by matching that text, so the refusal is the one that fires.

## What I added

`tests/src/core/ProjectContainerEntriesTest.cpp` gains two guard tests (no product change):

- `derivingRefusesWhitespaceCDATAInTheInterior`: the finding's shape, `<song><![CDATA[  ]]><trackcontainer/></song>`. Derive must refuse with "not whitespace-only" and leave the destination untouched.
- `reassemblingRefusesWhitespaceCDATAInTheInterior`: a skeleton whose `<song>` holds whitespace CDATA. Reassembly must refuse with "not whitespace-only" and leave the destination untouched.

Both are deterministic: no timing, no sleeps, no seams. They pass on the unmodified code, so they are guards against regression, not RED proof of a bug on this branch.

**Mutation proof that they catch the finding's mechanism.** I temporarily changed `:319` to `if( token == Characters && ( xml.isWhitespace() || xml.isCDATA() ) ) { continue; }`, which is the finding's bug (whitespace CDATA accepted as whitespace). The two new tests failed. I then reverted that line by hand; the product file is back to HEAD.

## Commands and exit codes

Scratch and build settings: the ccache config (`/home/kruzzzzy/.cache/ccache-bunny/ccache.conf`) sets `temporary_dir` to a path this sandbox cannot write. I set `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp` for the builds below. No config was changed.

| # | Step | Command (log in `logs/`) | EXIT |
|---|---|---|---|
| 1 | Build, first attempt (ccache permission failure, not code) | `cmake --build build --target ProjectContainerEntriesTest -j4 > logs/build-1-red.log 2>&1` | 2 |
| 2 | RED: build test on unmodified product code | `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp cmake --build build --target ProjectContainerEntriesTest -j4 > logs/build-2-red.log 2>&1` | 0 |
| 3 | RED: run family from `build/tests` | `ctest -R '^ProjectContainerEntriesTest$' --output-on-failure > logs/test-2-red.log 2>&1` | 0 (1/1 ctest test passed; the new tests passed too, so they are guards) |
| 4 | Mutant (finding simulated at `:319`): build | `cmake --build build --target ProjectContainerEntriesTest -j4 > logs/build-3-mutant.log 2>&1` | 0 |
| 5 | Mutant: run family | `ctest -R '^ProjectContainerEntriesTest$' --output-on-failure > logs/test-3-mutant.log 2>&1` | 8 (`16 passed, 2 failed`: `derivingRefusesWhitespaceCDATAInTheInterior` at test line 343, `reassemblingRefusesWhitespaceCDATAInTheInterior` at test line 369) |
| 6 | Revert mutant (manual edit back to HEAD line), rebuild | `cmake --build build --target ProjectContainerEntriesTest -j4 > logs/build-4-green.log 2>&1` | 0 |
| 7 | GREEN: run family from `build/tests` | `ctest -R '^ProjectContainerEntriesTest$' --output-on-failure > logs/test-4-green.log 2>&1` | 0 |
| 8 | GREEN: run binary directly | `QT_QPA_PLATFORM=offscreen ./ProjectContainerEntriesTest > logs/run-4-green-direct.log 2>&1` | 0 (`Totals: 18 passed, 0 failed`) |
| 9 | Gate: `feature_check.py` | `python3 .../feature_check.py <worktree> > logs/gates-1.log 2>&1` | 1 (cannot run here: it creates `<worktree>-feature-check/` beside the worktree; `PermissionError`, outside the write area. Not changed.) |
| 10 | Gate equivalents, run by hand with the same commands as the script's `SIMPLE`/`RATCHET` lists | `bash -c "<cmd>" > logs/gate-<name>.log 2>&1` | fork-sources 0, all-sources-reproduce 0, no-upstream-regression 0, check-namespace 0, no-tautology 0, doc-figures 0, complexity 1, file-length 1 |
| 11 | Completion check | `python3 .../submit_check.py <worktree> new-p17-132 > logs/submit-check-1.log 2>&1` | 1 (before commit: "nothing committed", "report missing", "gates" — see Self-check) |

Step 10 detail: the two ratchet gates (complexity, file-length) list 6 and several items, all in files this branch does not touch (`src/wasm/WasmSandbox.cpp`, `tests/src/gui/XyPadPairTest.cpp`, `src/core/TakeLane.cpp`, and others). None references `ProjectContainerEntriesTest.cpp` or `ProjectContainerEntries.cpp`. The script's own rule (fail only on items in changed files) therefore passes both, but I have not run the script itself.

Test-family count: 1 ctest test (`ProjectContainerEntriesTest`, #104); 18 QTest functions executed (16 pre-existing + 2 new), 0 failed on the final tree.

## Not changed, and why

- **No product change.** The bug does not reproduce, and the procedure says a not-real finding gets no fix. The explicit guard `token == Characters && xml.isWhitespace() && !xml.isCDATA()` at `:319` would be a no-op on Qt 6.12.0. It is worth making the refusal independent of Qt's undocumented flag, and the two new tests would protect that change. I recommend it as a follow-up; it is the owner's call.
- **Qt version.** The comment at `src/core/ProjectContainerEntries.cpp:259-260` cites a measurement on Qt 6.4.2 (the walk's contract comment is at `:298-299`). I measured on 6.12.0 only. I did not verify the CDATA behaviour on 6.4.2.

## Self-check

- **RED / GREEN.** No product fix exists, so no product patch was reverted for step 4b. Instead, the mutant (step 4-5) simulates the finding's mechanism and fails both new tests (2 failed, exit 8); the reverted tree passes (steps 6-8, exit 0). The unmodified-code run (steps 2-3) passed, which is why the tests are described as guards.
- **Deterministic.** Yes. No timing, sleeps, threads or seams. Inputs are fixed byte strings through the public `deriveContainerEntries` and `reassembleContainerDocument`.
- **Fix at the cause, complete.** Not applicable: no change needed. Both callers of the affected walk were read: the deriver (`:536`) and the reassembler (`:582`), and both go through `walkContentInterior` (`:305-331`).
- **Collateral damage.** None: the product tree is unchanged, so saved projects and existing behaviour are unaffected.
- **FALSE-POSITIVE evidence.** `src/core/ProjectContainerEntries.cpp:315-324` plus the probe output above, which shows Qt reports whitespace CDATA as `isWhitespace()==0`.
- **Gates.** Six of eight gates exit 0 when run by hand; complexity and file-length exit 1 but only on items outside the changed files. `feature_check.py` itself cannot run in this write area; `submit_check.py` therefore still reports the gate item as failing.

Files committed for this verdict: `tests/src/core/ProjectContainerEntriesTest.cpp` and `docs/qwen-fixes-2026-10/cluster-new-p17-132.md`. No product file changed.
