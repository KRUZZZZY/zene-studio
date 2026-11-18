# Cluster new-p12-126-port: `MidiSndio` shutdown races on `m_quit`

Verdict: **FIXED**

Port of `9b6bb1e41` ("fix: make sndio MIDI shutdown atomic", cluster
new-p12-126) onto the newest line `050/m0-train-5`, written by hand (no merge,
cherry-pick or rebase).

The accepted fix conflicted with this line only in `src/core/midi/MidiSndio.cpp`,
because the newer line already changed the destructor body:

```
newest line   m_quit = true;            if( !wait( 1000 ) ) { terminate(); wait(); }
9b6bb1e41     m_quit.store(true);       terminate();  wait( 1000 );
```

Only the flag access itself is ported; the newer line's `wait(1000)` /
`terminate()` / `wait()` sequence is left exactly as it is, so this port is the
old fix minus the stale shutdown refactor.

`m_quit` was a `volatile bool` written by the destructor and read by the sndio
reader thread. `volatile` gives no cross-thread ordering in C++, so the pair is
a data race. It is now `std::atomic<bool>`; the destructor stores and `run()`
loads.

## File evidence

- [include/MidiSndio.h:33](../../include/MidiSndio.h:33): `#include <atomic>`.
- [include/MidiSndio.h:72](../../include/MidiSndio.h:72): `std::atomic<bool> m_quit;`
  (was `volatile bool`).
- [src/core/midi/MidiSndio.cpp:70](../../src/core/midi/MidiSndio.cpp:70):
  `m_quit.store(true);` in `~MidiSndio()`.
- [src/core/midi/MidiSndio.cpp:108](../../src/core/midi/MidiSndio.cpp:108):
  `while( !m_quit.load() && m_hdl )` in `MidiSndio::run()`.
- [tests/src/core/MidiSndioTest.cpp:48](../../tests/src/core/MidiSndioTest.cpp:48):
  the regression `quitFlagIsAtomic()`; line 50 asserts the member type.

The regression lives in `MidiSndioTest`, the dedicated sndio test target
(already registered at `tests/fork-sources.txt:2305`), not in `MidiClockTest` as
in the original commit. It needs no MIDI hardware: it reads the member type of
the class the target already instantiates.

## Evidence table

Build commands ran from the worktree root; `ctest` ran from `build/tests`. All
logs are in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-126-port.r1/logs`.

| Phase | Exact command | Log | EXIT |
|---|---|---|---:|
| Pre-fix build (test only) | `cmake --build build --target MidiSndioTest` | `build-1.log` | 0 |
| **RED, pre-fix** | `ctest -R '^MidiSndioTest$' --output-on-failure` | `test-1.log` | **8** |
| Fixed build (product + 2 tests) | `cmake --build build --target zene MidiSndioTest MidiReconnectTest` | `build-2.log` | 0 |
| GREEN, affected family | `ctest -R '^MidiSndioTest$\|^MidiReconnectTest$' --output-on-failure` | `test-2.log` | 0 |
| Reverse product patch | `git diff -- include/MidiSndio.h src/core/midi/MidiSndio.cpp > .../logs/fix.patch && git apply -R .../logs/fix.patch` | — | 0 |
| Reversed build | `cmake --build build --target zene MidiSndioTest` | `build-3.log` | 0 |
| **RED, product fix reversed** | `ctest -R '^MidiSndioTest$' --output-on-failure` | `test-3.log` | **8** |
| Reapply product patch | `git apply .../logs/fix.patch` | — | 0 |
| Restored build | `cmake --build build --target zene MidiSndioTest MidiReconnectTest` | `build-4.log` | 0 |
| **GREEN, restored** | `ctest -R '^MidiSndioTest$\|^MidiReconnectTest$' --output-on-failure` | `test-4.log` | **0** |
| Full `zene` build | `cmake --build build --target zene` | `build-5.log` | 0 |
| Full MIDI family | `ctest -R 'Midi' --output-on-failure` | `test-6.log` | 0 |

Test counts:

| Run | Command | Result |
|---|---|---|
| RED (`test-1.log`) | `ctest -R '^MidiSndioTest$'` | 1 CTest target; Qt cases: **3 passed, 1 failed** |
| GREEN (`test-4.log`) | `ctest -R '^MidiSndioTest$\|^MidiReconnectTest$'` | 2 CTest targets, **100% passed out of 2** |
| MIDI family (`test-6.log`) | `ctest -R 'Midi'` | 26 CTest targets, **100% tests passed out of 26** |

The failing assertion in both RED logs is identical:

```
FAIL!  : MidiSndioTest::quitFlagIsAtomic() '(std::is_same_v<decltype(lmms::MidiSndio::m_quit), std::atomic_bool>)' returned FALSE. ()
Loc: [../../tests/src/core/MidiSndioTest.cpp(50)]
```

The reverse-patch step is the proof that the test catches the bug: with only the
two product files reverted and the test left in place, the same case fails
(EXIT 8); with the patch reapplied it passes (EXIT 0). `test-5.log` (a first
`ctest -R 'Midi'` run, EXIT 8) is not a regression - 23 of the 26 targets
reported `***Not Run` because those test binaries had never been built in this
tree. After building them (`build-6.log`, EXIT 0) the same command gives
`test-6.log`, EXIT 0, 26/26.

## Gates

| Exact command | Worktree | Log | EXIT |
|---|---|---|---:|
| `bash tests/complexity-gate.sh --check` | this tree | `gate-1.log` | **1** |
| `bash tests/complexity-gate.sh --check` | pristine `git archive HEAD` in `/tmp` | `gate-1b-pristine-head.log` | **1** |
| `bash tests/file-length-gate.sh --check` | this tree | `gate-2.log` | **1** |
| `bash tests/file-length-gate.sh --check` | pristine `git archive HEAD` in `/tmp` | `gate-2b-pristine-head.log` | **1** |

**Both gates fail, and they fail identically on the untouched `HEAD`.** This is
a pre-existing ratchet regression on `050/m0-train-5`, not something this port
introduced, and no gate limit or baseline was touched:

- complexity: 6 `REGRESSION: new function over target` lines in both runs, and
  `diff` of the two REGRESSION lists is empty (`IDENTICAL`). All six name
  functions in files this fix does not touch (`XyPadPairTest.cpp`,
  `DawProjectReadTracks.cpp`, `ControlCommandsMixerTools.cpp`,
  `ControlCommandsMixerStrip.cpp`, `ProjectRevisions.cpp`,
  `ControlCommandsShortcuts.cpp`).
- file-length: 11 `REGRESSION: new file over 500 lines` lines in both runs, also
  `IDENTICAL` by `diff`, none of them a file this fix touches.
- The three files this port changes are 80, 131 and 63 lines and introduce no new
  function at all (the fix replaces two expressions), so neither ratchet can
  move because of it.

## Registration

- `include/MidiSndio.h` - inherited file, changed here, newly registered in
  `tests/upstream-modifications.txt`.
- `src/core/midi/MidiSndio.cpp` - inherited file, already registered (lines 56
  and 2265); the `new-p12-126-port` reason is appended to line 56.
- `tests/src/core/MidiSndioTest.cpp` - already a registered fork source
  (`tests/fork-sources.txt:2305`), no new file is introduced.

## Files committed

- `include/MidiSndio.h`
- `src/core/midi/MidiSndio.cpp`
- `tests/src/core/MidiSndioTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p12-126-port.md`
