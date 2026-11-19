FALSE-POSITIVE

# Cluster 1727: QDomNodeList::at() is bounds-checked; no defect

**Verdict:** FALSE-POSITIVE. The reported defect is that `Song::loadProject` calls `at(0)` on an
`elementsByTagName("patterntrack")` result that can be empty, and that this is an unchecked
dereference. It is not. `QDomNodeList::at()` is `item()`, which is bounds-checked and returns a
null node for an out-of-range index. The null chain that follows is harmless: every step returns
a null or empty value and the loader adds 0 to its loop bound.

**Product change:** none. This report is the only file in the commit.

## Evidence

| # | Claim | Source (file:line or log) | What it shows |
|---|---|---|---|
| E1 | Call site | `src/core/Song.cpp:2138` | `n += nd.toElement().elementsByTagName("patterntrack").at(0).toElement().firstChildElement().childNodes().count();` inside the trackcontainer loop, for `type == Track::Type::Pattern` tracks |
| E2 | `at()` is `item()` | `/usr/include/qt6/QtXml/qdom.h:230-231` | `QDomNode item(int index) const;` and `inline QDomNode at(int index) const { return item(index); }` |
| E3 | Bounds check and null return | `logs/qt-at-disassembly-2.log` (`QDomNodeList::item`, libQt6Xml.so.6.12.0) | `e4f0: jge e520` (index >= length goes to the null path), `e4f4: js e520` (negative index goes to the null path), `e520: movq $0x0,0x0(%rbp)` writes a null node. The list pointer is also null-tested at `e4d0: test %rbx,%rbx` / `e4d3: je e520`. |
| E4 | Empty-list chain is null with count 0 | `logs/qdom-at-probe-2.log` (PySide6 + Qt 6.12.0) | No `patterntrack`: `at(0).isNull()` True, `at(0).toElement().firstChildElement().isNull()` True, `childNodes().count()` = 0. With content: `at(0)` non-null, `childNodes().count()` = 3. Empty `patterntrack`: `at(0)` non-null, `firstChildElement()` null, count 0. |
| E5 | Real missing-patterntrack load reaches the call site | `logs/load-branch-proof-2.log` | gdb breakpoint at `Song::loadProject+0x9d8` (0x55555629eaa8 - 0x55555629e0d0 = 0x9d8), `index=0 length=0`, `#1 main`, then `hello.lua finished`, then `[Inferior 1 (process 405) exited normally]`. The gdb `length` value is only the breakpoint's own print. It is not proof that the list is empty; E4 is the proof of the empty-list chain. |
| E6 | Control also hits the call site and exits normally | `logs/load-branch-control-2.log`, `logs/control-identity.log` | Same breakpoint address, `control index=0 length=0`, `hello.lua finished`, `[Inferior 1 (process 458) exited normally]`. The input is named by the gdb `show args` line after `run`: `Argument list ... hello.lua .../logs/scratch/control.mmp`. `control-identity.log` shows `logs/scratch/control.mmp` and `tests/emptyproject.mmp` both have sha256 `d4075c73c93ed972dc567e76a512425a77a268b179f3ee9d00a89f2da3279c5c` and `cmp -s` exit 0, so the control input is `tests/emptyproject.mmp`. |
| E7 | Missing and empty runs print the non-fatal diagnostic | `logs/load-missing-2.log`, `logs/load-empty-2.log` | Each prints `called Track::getClip( 1 ), but Clip 1 doesn't exist`, then `hello.lua finished`. |
| E8 | Control does not print it | `logs/load-control-2.log`, `logs/load-branch-control-2.log` | No `getClip` line (`rg getClip` finds no match in `load-control-2.log` or `load-branch-control-2.log`). |
| E9 | Absent child is skipped in the track loop | `src/core/Track.cpp:947-955` | `if( node.isElement() )` guards the child loop; a missing `patterntrack` child is not visited. |
| E10 | `namedItem` is guarded | `src/tracks/PatternTrack.cpp:229-234` | `QDomNode node = _this.namedItem(...)` followed by `if( node.isElement() )` before `restoreState`. |

Why the null chain is safe: `Song.cpp:2138` calls `at(0)` only on a `QDomNodeList` (E2, E3). On
an empty list `at(0)` returns a null `QDomNode` (E3, E4). `toElement()`, `firstChildElement()` and
`childNodes()` on a null node return null or an empty list, and `count()` returns 0 (E4). So
`n += 0`. No pointer is dereferenced. The "diagnostic" in E7 comes from a different path: when
the loader does reach `Track::getClip(1)` (see Other findings), `Track.cpp:1163-1171` prints the
message and creates the missing clip.

## Test matrix

| Case | Input | Result | Evidence |
|---|---|---|---|
| Normal control | control project `tests/emptyproject.mmp`, copied byte-identical to `scratch/control.mmp` (`control-identity.log`; input named by the `show args` line in `load-branch-control-2.log`) | Call site hit with `index=0 length=0`; `hello.lua finished`; exit normal; no `getClip` line | `load-branch-control-2.log`, `control-identity.log`, `load-control-2.log` |
| Failure: missing `patterntrack` | `scratch/missing-patterntrack.mmp` | Call site hit at `+0x9d8`; `getClip(1)` diagnostic; `hello.lua finished`; exit normal | `load-branch-proof-2.log`, `load-missing-2.log` |
| Empty `patterntrack` | `scratch/empty-patterntrack.mmp` | `getClip(1)` diagnostic; `hello.lua finished`. No gdb run logged for this case. | `load-empty-2.log` |
| Reconfiguration | n/a | Not applicable: no reconfiguration path is in scope and no product code changed. | n/a |
| Audio thread untouched | n/a | Not applicable: no product code changed; the gdb log shows the call on Thread 1, the loader thread, not the audio thread. | `load-branch-proof-2.log` |
| Platform-independent contract | n/a | Not applicable: no product code changed; the Qt `at()` contract is platform-independent. | `qt-at-disassembly-2.log`, `qdom-at-probe-2.log` |

## Other findings (not fixed here)

1. **Non-fatal `called Track::getClip( 1 )` diagnostic.** The malformed logs (E7) print it because
   `Track::getClip` (`src/core/Track.cpp:1163-1171`) prints `called Track::getClip( %zu ), but Clip %zu doesn't exist`
   when `clipNum >= m_clips.size()`, then returns `createClip(...)`. It creates the missing clip and
   continues. The load finishes. This is a noisy message, not a crash. Not fixed in this packet.
2. **Count pass adds `patterntrack` child counts to the trackcontainer loop bound.** `Song.cpp:2128`
   sets the bound `n=tclist.count()`, and `Song.cpp:2138` adds the `patterntrack` child count to `n`.
   The loop therefore runs over more entries than the number of trackcontainers when a pattern track
   has children. Not fixed in this packet; it is a separate question about intent and is noted for the lead.

## Command and EXIT table

Commands were run from the worktree root. Each evidence command writes its own `echo EXIT=$?` line into
its log. Every evidence row that cites a log cites the `logs/*-2.log` run, except `control-identity.log`,
which has no `-2` variant and is listed in the table below. The earlier logs (`build-1.log`, `load-*.log`
without `-2`, `load-branch-proof-summary.log`) are kept on disk for reference only. No evidence row cites them,
and their commands were not recorded.

```sh
export LOG=/home/kruzzzzy/.config/kads/zene/state/runs/1727.r1/logs
cmake --build build --target zene -j4 > "$LOG/build-2.log" 2>&1; echo EXIT=$? >> "$LOG/build-2.log"
objdump -d --demangle /usr/lib/libQt6Xml.so.6.12.0 | rg -A45 'QDomNodeList::item\(int\) const' > "$LOG/qt-at-disassembly-2.log" 2>&1; echo EXIT=$? >> "$LOG/qt-at-disassembly-2.log"
python3 "$LOG/probe_qdom.py" > "$LOG/qdom-at-probe-2.log" 2>&1; echo EXIT=$? >> "$LOG/qdom-at-probe-2.log"
python3 "$LOG/make_fixtures.py" "$PWD/tests/emptyproject.mmp" "$LOG/scratch" > "$LOG/fixtures-2.log" 2>&1; echo EXIT=$? >> "$LOG/fixtures-2.log"
sha256sum "$LOG/scratch/control.mmp" tests/emptyproject.mmp > "$LOG/control-identity.log" 2>&1; cmp -s "$LOG/scratch/control.mmp" tests/emptyproject.mmp; echo CMP_EXIT=$? >> "$LOG/control-identity.log"
HOME="$LOG/scratch/home" QT_QPA_PLATFORM=offscreen gdb -nx -batch -ex "break 'lmms::Song::loadProject(QString const&, QList<QString> const&)'" -ex 'show args' -ex "run --allowroot --run-script data/scripts/hello.lua $LOG/scratch/missing-patterntrack.mmp" -ex 'show args' -ex "break *((long)&'lmms::Song::loadProject(QString const&, QList<QString> const&)' + 0x9d8)" -ex 'continue' -ex 'printf "index=%d length=%ld\n", $edx, *(long*)(*(long*)$rsi+0x50)' -ex 'bt 3' -ex 'delete 2' -ex 'continue' --args build/zene > "$LOG/load-branch-proof-2.log" 2>&1; echo EXIT=$? >> "$LOG/load-branch-proof-2.log"
HOME="$LOG/scratch/home" QT_QPA_PLATFORM=offscreen gdb -nx -batch -ex "break 'lmms::Song::loadProject(QString const&, QList<QString> const&)'" -ex 'show args' -ex "run --allowroot --run-script data/scripts/hello.lua $LOG/scratch/control.mmp" -ex 'show args' -ex "break *((long)&'lmms::Song::loadProject(QString const&, QList<QString> const&)' + 0x9d8)" -ex 'continue' -ex 'printf "control index=%d length=%ld\n", $edx, *(long*)(*(long*)$rsi+0x50)' -ex 'delete 2' -ex 'continue' --args build/zene > "$LOG/load-branch-control-2.log" 2>&1; echo EXIT=$? >> "$LOG/load-branch-control-2.log"
HOME="$LOG/scratch/home" QT_QPA_PLATFORM=offscreen build/zene --allowroot --run-script data/scripts/hello.lua "$LOG/scratch/control.mmp" > "$LOG/load-control-2.log" 2>&1; echo EXIT=$? >> "$LOG/load-control-2.log"
HOME="$LOG/scratch/home" QT_QPA_PLATFORM=offscreen build/zene --allowroot --run-script data/scripts/hello.lua "$LOG/scratch/missing-patterntrack.mmp" > "$LOG/load-missing-2.log" 2>&1; echo EXIT=$? >> "$LOG/load-missing-2.log"
HOME="$LOG/scratch/home" QT_QPA_PLATFORM=offscreen build/zene --allowroot --run-script data/scripts/hello.lua "$LOG/scratch/empty-patterntrack.mmp" > "$LOG/load-empty-2.log" 2>&1; echo EXIT=$? >> "$LOG/load-empty-2.log"
git add docs/qwen-fixes-2026-10/cluster-1727.md; echo GIT_ADD_EXIT=$? >> "$LOG/git-record-3.log"
git commit -m 'docs(cluster-1727): align report evidence with the -2 logs' -m 'Co-Authored-By: Claude Haiku 5.5 <noreply@anthropic.com>'; echo GIT_COMMIT_EXIT=$? >> "$LOG/git-record-3.log"
git status --short; echo GIT_STATUS_EXIT=$? >> "$LOG/git-record-3.log"
git rev-parse HEAD; echo GIT_REV_EXIT=$? >> "$LOG/git-record-3.log"
```

| Step | Log | EXIT |
|---|---|---|
| Build `zene` target | `logs/build-2.log` | 0 |
| Qt `at()` disassembly | `logs/qt-at-disassembly-2.log` | 0 |
| Qt `at()` probe | `logs/qdom-at-probe-2.log` | 0 |
| Scratch fixtures | `logs/fixtures-2.log` | 0 |
| Control identity (sha256 match, byte compare) | `logs/control-identity.log` | `CMP_EXIT=0`; both hashes `d4075c73…279c5c` |
| Missing-`patterntrack` load, gdb | `logs/load-branch-proof-2.log` | 0 (`[Inferior 1 (process 405) exited normally]`) |
| Control load, gdb | `logs/load-branch-control-2.log` | 0 (`[Inferior 1 (process 458) exited normally]`) |
| Control load | `logs/load-control-2.log` | 0 |
| Missing-`patterntrack` load | `logs/load-missing-2.log` | 0 |
| Empty-`patterntrack` load | `logs/load-empty-2.log` | 0 |
| `git add`, `git commit`, `git status`, `git rev-parse` | `logs/git-record-3.log` (labels `GIT_ADD_EXIT`, `GIT_COMMIT_EXIT`, `GIT_STATUS_EXIT`, `GIT_REV_EXIT`) | `git add` 0, `git commit` 0, `git status` 0, `git rev-parse` 0 (all from `logs/git-record-3.log`). `git rev-parse HEAD` output: `cde7c77ffa9a9c7ce3753a1a7722ef8fd7de9ea6`. The values are written after the commit runs, so they cannot appear inside the commit they describe. |

The labeled exits for the final docs-only commit that carries this correction are in `logs/git-record-4.log`, which is created after that commit.

## Self-check

1. **"The regression test FAILS with your product change reverted and PASSES with it (exit codes)."**
   There is no product change, so there is no regression test and no RED/GREEN run. A test with no
   fix would pass in both directions, which the brief calls a wrong test. None was written.
2. **"The test is deterministic: no wall-clock timing, sleeps or races (name the seam or injected values it uses)."**
   No test was added. The evidence is one-shot gdb and probe runs. Their outputs (E4-E8) do not depend on timing: the probe
   constructs fixed XML strings, and the gdb breakpoints are at fixed addresses.
3. **"The fix is at the cause and complete: every caller and sibling of what you changed was checked and stays consistent."**
   Not applicable: nothing changed. The call site is `Song.cpp:2138`; its sibling is the `namedItem` path in
   `PatternTrack.cpp:229-234`, which is guarded, and the track loop in `Track.cpp:947-955`, which skips absent children.
   The two open findings above are not fixed and are not part of this verdict.
4. **"No collateral damage: old projects and saved data still load as before, and any limit or check you added does not hit other callers that legitimately exceed it."**
   No change was made, so no project or caller is affected. No limit or check was added.
5. **"If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended contract that shows it."**
   The contract is the Qt API: `QDomNodeList::at(int)` is `item(int)` (`/usr/include/qt6/QtXml/qdom.h:230-231`), which
   returns a null node for an out-of-range index (`qt-at-disassembly-2.log`, `e4f0`/`e4f4`/`e520`). The caller is
   `src/core/Song.cpp:2138`. The empty chain is null with count 0 (`qdom-at-probe-2.log`). The loader skips absent children
   (`src/core/Track.cpp:947-955`) and guards `namedItem` (`src/tracks/PatternTrack.cpp:229-234`).
