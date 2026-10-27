# FALSE-POSITIVE: cluster new-p22-49 (busJson null dereference of buses.bus(index))

Finding (Luna patrol, historical line 98; at branch tip `5c1c6715d9` it is `src/core/ControlCommandsPluginBuses.cpp:98`):
"busJson dereferences buses.bus(index) without a null check; a malformed or inconsistent output-bus state can make
bus(index) null."

## Why it is not real

1. **`bus()` returns null only for an out-of-range index.** `src/core/InstrumentOutputBuses.cpp:121-124`:
   `return index >= 0 && index < busCount() ? &m_layout[...] : nullptr;`.
2. **`busCount()` is the size of the same container.** `include/InstrumentOutputBuses.h:85`:
   `busCount() const -> int { return static_cast<int>(m_layout.size()); }`. A "malformed" layout with a missing or
   null entry is not representable: `m_layout` is a `std::vector<InstrumentOutputBus>` (`include/InstrumentOutputBuses.h:149`),
   so every element `[0, size())` has a valid address. `setLayout` (`src/core/InstrumentOutputBuses.cpp:109-118`)
   only truncates or assigns that vector.
3. **The only caller of `busJson` is bounded by `busCount()`.** `src/core/ControlCommandsPluginBuses.cpp:112`:
   `for (int i = 0; i < buses.busCount(); ++i) { list.append(busJson(buses, i)); }`. `busJson` is called from
   nowhere else (`rg -n "busJson\("` shows definition and this call only). An empty layout (the "partially initialized"
   case) makes the loop run zero times.
4. **No second thread can shrink the layout between the loop test and the dereference.**
   - Layout writers are control-thread only, per the documented contract `include/InstrumentOutputBuses.h:62-63`
     ("configuration is control thread only").
   - The writers are `plugins/ClapInstrument/ClapInstrument.cpp:139` (load) and `:223` (`reprepare()`). `reprepare()`
     is reached from `ClapInstrument::poll()` (`ClapInstrument.cpp:381-389`), which is a `QTimer` timeout connected at
     `ClapInstrument.cpp:156-159`, i.e. the GUI event loop (`plugins/ClapInstrument/ClapInstrument.h:150`, "GUI thread").
   - The control socket reads from a `QSocketNotifier` on the same event loop (`src/core/ControlServerSocket.cpp:452`),
     so `busesState` and the writers run on one thread. The Windows transport (`src/core/ControlServerWin32.cpp`)
     dispatches on a separate thread, but it marshals to the server thread (`include/ControlServer.h:217-222`
     "Run dispatchLine() ... on the SERVER's thread"), so the same single-thread invariant holds.
5. **The sibling dereferences are bounded the same way.** `src/core/ControlCommandsPluginBuses.cpp:289-292`
   (`bus < buses.busCount()` before `buses.bus(bus)->name`) and `plugins/Lv2Instrument/Lv2Instrument.cpp:265-267`
   (`bus < m_outputBuses.busCount()`).

## What I did not change

No product code. The finding's fix (a null check that skips or reports an empty entry) would guard a state that the
loop bound and the container make unreachable, so it would be dead code with no behavioural effect. A regression test
would pass before and after any fix, so I wrote none (nothing to prove RED→GREEN).

Revisit if the contract changes, e.g. if layout is ever published from a worker thread while `busesState` reads it.
Then the reader would need a snapshot, not a null check.

## Commands run and EXIT codes

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p22-49.r1/logs/`.

| Command (run from the worktree) | EXIT | Log |
| --- | --- | --- |
| `python3 .../qwen/feature_check.py <worktree>` | 1 | `feature-check-1.log` (`PermissionError`: the script writes `<worktree>/../zene-qwen-new-p22-49-feature-check`, outside the write area; no gate ran) |
| `bash tests/fork-sources-gate.sh` | 0 | `gate-fork-sources-3.log` |
| `bash tests/all-sources-reproduce.sh` | 0 | `gate-all-sources-3.log` |
| `bash tests/no-upstream-regression-gate.sh` | 0 | `gate-no-upstream-3.log` |
| `tests/scripted/check-namespace` | 0 | `gate-check-namespace-3.log` |
| `bash tests/no-tautology-gate.sh` | 0 | `gate-no-tautology-3.log` |
| `python3 tests/doc-figures-gate.py` | 0 | `gate-doc-figures-3.log` |
| `bash tests/complexity-gate.sh --check` | 1 | `gate-complexity-3.log` (6 `REGRESSION` lines, all in files this branch does not change) |
| `bash tests/file-length-gate.sh --check` | 1 | `gate-file-length-3.log` (22 `REGRESSION` lines, all in files this branch does not change) |

Note on the ratchets: `feature_check.py` marks complexity and file-length as PASS when no item is new in a file the branch
changed. The branch changes only this report, so no ratchet item is in a changed file; by that rule both rows PASS. The
script itself could not run here, so the table is my reconstruction from its component commands, not its output.
Two earlier loop attempts (`gate-*-2.log`) failed with exit 127 because my shell loop did not word-split the command; those
results are not used.

Ratchet items listed (none in a changed file): `src/core/ControlCommandsMixerStrip.cpp`,
`src/core/ControlCommandsMixerTools.cpp`, `src/core/ControlCommandsSessionRecordLand.cpp`,
`src/core/DawProjectReadTracks.cpp`, `src/wasm/WasmSandbox.cpp`, `tests/src/gui/XyPadPairTest.cpp`.

## Evidence table

| Item | Value |
| --- | --- |
| RED command / EXIT | Not run: no product change to revert, and a not-real verdict needs no regression test |
| GREEN command / EXIT | Not run for the same reason |
| Affected test family, tests executed | Not run (no product code changed) |
| Gate exit (feature_check.py) | 1, `PermissionError` before any gate ran |
| Gate exit (component gates) | 0 for six simple gates; 1 for complexity and file-length, both red on the train base and not in changed files |
| Files committed | `docs/qwen-fixes-2026-10/cluster-new-p22-49.md` only |

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.** N/A: there is no product change,
  and the finding is closed as a false positive, so no test is needed to prove a fix.
- **The test is deterministic.** N/A (no test added).
- **The fix is at the cause and complete.** N/A (no fix). The sibling callers were checked in items 3 and 5 above.
- **No collateral damage.** Nothing changed, so old projects and saved data are untouched.
- **A FALSE-POSITIVE verdict cites the code or contract that shows it.** Yes: `InstrumentOutputBuses.cpp:121-124`,
  `InstrumentOutputBuses.h:85,62-63,149`, `ControlCommandsPluginBuses.cpp:112`, `ClapInstrument.cpp:156-159,381-389`,
  `ControlServerSocket.cpp:452`.
- **Audio thread.** No change. The layout is not read on the audio thread by `busesState`.
- **Gates.** Six simple gates EXIT 0; complexity and file-length EXIT 1 (train-red, nothing new in changed files);
  the script itself did not run (see above).
