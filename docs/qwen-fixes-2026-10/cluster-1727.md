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
| E3 | Bounds check and null return | `logs/qt-at-disassembly.log` (`QDomNodeList::item`, libQt6Xml.so.6.12.0) | `e4f0: jge e520` (index >= length goes to the null path), `e4f4: js e520` (negative index goes to the null path), `e520: movq $0x0,0x0(%rbp)` writes a null node. The list pointer is also null-tested at `e4d0: test %rbx,%rbx` / `e4d3: je e520`. |
| E4 | Empty-list chain is null with count 0 | `logs/qdom-at-probe.log` (PySide6 + Qt 6.12.0) | No `patterntrack`: `at(0).isNull()` True, `at(0).toElement().firstChildElement().isNull()` True, `childNodes().count()` = 0. With content: `at(0)` non-null, `childNodes().count()` = 3. Empty `patterntrack`: `at(0)` non-null, `firstChildElement()` null, count 0. |
| E5 | Real missing-patterntrack load reaches the call site | `logs/load-branch-proof.log` and `logs/load-branch-proof-summary.log` | gdb breakpoint at `Song::loadProject+0x9d8` (0x55555629eaa8 - 0x55555629e0d0 = 0x9d8), `index=0 length=0`, `#1 main`, then `hello.lua finished`, then `[Inferior 1 (process 306) exited normally]`. The gdb `length` value is only the breakpoint's own print. It is not proof that the list is empty; E4 is the proof of the empty-list chain. |
| E6 | Control also hits the call site and exits normally | `logs/load-branch-control.log` | Same breakpoint address, `control index=0 length=0`, `hello.lua finished`, `[Inferior 1 (process 365) exited normally]`. **Caveat:** the log does not name its input file. `logs/fixtures.log` records that `control.mmp` was written to `logs/scratch`. The packet names `tests/emptyproject.mmp` as the control input; that link is not shown in any log and is unverified. |
| E7 | Missing and empty runs print the non-fatal diagnostic | `logs/load-missing-patterntrack.log`, `logs/load-empty-patterntrack.log` | Each prints `called Track::getClip( 1 ), but Clip 1 doesn't exist`, then `hello.lua finished`. |
| E8 | Control does not print it | `logs/load-control.log` | No `getClip` line (`grep -c getClip` = 0 in `load-control.log` and in `load-branch-control.log`). |
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
| Normal control | control project (see E6 caveat) | Call site hit with `index=0 length=0`; `hello.lua finished`; exit normal; no `getClip` line | `load-branch-control.log`, `load-control.log` |
| Failure: missing `patterntrack` | `scratch/missing-patterntrack.mmp` | Call site hit at `+0x9d8`; `getClip(1)` diagnostic; `hello.lua finished`; exit normal | `load-branch-proof.log`, `load-missing-patterntrack.log` |
| Empty `patterntrack` | `scratch/empty-patterntrack.mmp` | `getClip(1)` diagnostic; `hello.lua finished`. No gdb run logged for this case. | `load-empty-patterntrack.log` |
| Reconfiguration | n/a | Not applicable: no reconfiguration path is in scope and no product code changed. | n/a |
| Audio thread untouched | n/a | **Not tested.** No audio-thread code is changed. The gdb hits show the call on `Thread 1` (the loader thread), not on the audio thread. | `load-branch-proof.log` |
| Platform-independent contract | n/a | **Not tested on other platforms.** The disassembly and the probe are from Linux with Qt 6.12.0 (`/usr/lib/libQt6Xml.so.6.12.0`). The `at()` contract comes from the Qt header (E2), which is the same API on each platform. | `qt-at-disassembly.log`, `qdom-at-probe.log` |

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

Each row is copied from the run log named. Where a log does not record an exit code, the table says so.

| Step | Log | Result recorded in log |
|---|---|---|
| Build `zene` target | `logs/build-1.log` | `[100%] Built target zene`. No exit code recorded in the log. |
| Write scratch fixtures | `logs/fixtures.log` | `wrote control.mmp, missing-patterntrack.mmp, empty-patterntrack.mmp`. No exit code recorded. |
| Qt `at()` probe | `logs/qdom-at-probe.log` | Output as E4. No exit code recorded. The first attempt of the probe helper raised a TypeError and was rerun; the logged run is final. |
| Disassembly of `QDomNodeList::item` | `logs/qt-at-disassembly.log` | As E3. No exit code recorded. |
| Missing-`patterntrack` load, gdb | `logs/load-branch-proof.log` | `[Inferior 1 (process 306) exited normally]` |
| Control load, gdb | `logs/load-branch-control.log` | `[Inferior 1 (process 365) exited normally]` |
| Control load | `logs/load-control.log` | `hello.lua finished`. No gdb exit line. |
| Missing-`patterntrack` load | `logs/load-missing-patterntrack.log` | `hello.lua finished`. No gdb exit line. |
| Empty-`patterntrack` load | `logs/load-empty-patterntrack.log` | `hello.lua finished`. No gdb exit line. |
| Commit this report | `git commit` (below) | See the commit record. |

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
   returns a null node for an out-of-range index (`qt-at-disassembly.log`, `e4f0`/`e4f4`/`e520`). The caller is
   `src/core/Song.cpp:2138`. The empty chain is null with count 0 (`qdom-at-probe.log`). The loader skips absent children
   (`src/core/Track.cpp:947-955`) and guards `namedItem` (`src/tracks/PatternTrack.cpp:229-234`).
