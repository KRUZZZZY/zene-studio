# Cluster new-p9-277: WinMM device maps and port pointers race with callbacks

**Verdict: BLOCKED (platform constraint — not a FALSE-POSITIVE, not ALREADY-FIXED)**

## Why this cannot be fixed or regression-tested on this worktree

The finding targets `src/core/midi/MidiWinMM.cpp` (and its header `include/MidiWinMM.h`),
a Windows-only (`WinMM`) MIDI backend. On this Linux worktree the entire class is
compiled out, so the buggy code does not exist in the build and cannot be exercised:

- `CMakeLists.txt:271-279` sets `LMMS_HAVE_WINMM TRUE` only under
  `LMMS_BUILD_WIN32` / `LMMS_BUILD_CYGWIN`. This is a Linux build, so it is undefined.
- `src/core/midi/MidiWinMM.cpp:27` wraps the whole implementation in
  `#ifdef LMMS_HAVE_WINMM`, closing at `:308`. On Linux the TU is empty.
- `include/MidiWinMM.h:30` wraps the class declaration, `m_inputSubs` /
  `m_outputSubs` / `m_inputDevices` / `m_outputDevices` (`:133-139`) and the
  `subscribeReadablePort` / `subscribeWritablePort` methods in `#ifdef LMMS_HAVE_WINMM`,
  closing at `:151`. The class does not exist on Linux.

Therefore:
1. There are **no** `MidiWinMM` symbols in the built `libzene-core.a`
   (`nm build/src/core/libzene-core.a | grep -i MidiWinMM` → empty).
2. `LMMS_HAVE_WINMM` is **not** defined in `build/src/lmmsconfig.h`.
3. No deterministic regression test can reference `MidiWinMM`, its members, or its
   WinMM `HMIDIInProc` callbacks — none of them are compiled on this platform.
4. A fix would require Windows-only APIs (`HMIDIIN`/`HMIDIOUT`, the multimedia
   callback thread) that cannot be built or verified here.

The reported race is real Windows-concurrency behaviour (an `HMIDIInProc` callback
reading `m_inputSubs` / `m_inputDevices` while GUI-side `subscribeReadablePort`
or a device-list refresh mutates and clears them). It is out of scope for a Linux
worktree and would need a Windows build + a Windows-targeted harness to reproduce,
fix, and prove with RED/GREEN. Reproducing it would require fabricating a Windows
toolchain and mocking the Win32 multimedia subsystem, which is not available here
and would not be a faithful regression.

## Evidence table

| Check | Exact command and result |
| --- | --- |
| WinMM enabled? | `grep -n "LMMS_HAVE_WINMM" CMakeLists.txt` → only `:279 SET(LMMS_HAVE_WINMM TRUE)` inside `IF(LMMS_BUILD_WIN32 OR LMMS_BUILD_CYGWIN)` (Linux falls to `ELSE()` at `:290`). |
| Config define present? | `grep -i winmm build/src/lmmsconfig.h` → **no match** (undefined on this Linux build). |
| Symbols built? | `nm build/src/core/libzene-core.a 2>/dev/null \| grep -i MidiWinMM` → **empty** (class compiled out). |
| Guard coverage | `grep -n "LMMS_HAVE_WINMM" src/core/midi/MidiWinMM.cpp include/MidiWinMM.h` → `MidiWinMM.cpp:27`/`308`, `include/MidiWinMM.h:30`/`151` — the class, its members, and the cited methods are all inside the guard. |
| RED command | **Cannot run** — no `MidiWinMM` symbol to link a test against on Linux. |
| GREEN command | **Cannot run** — same reason. |
| Reversed-fix proof | **N/A** — no fix is made; nothing to reverse. |
| Affected family | 0 CTest tests (there is no WinMM test, and one cannot be built here). |
| Complexity gate | **N/A** (no production code changed). |
| File-length gate | **N/A** (no production code changed). |

## Conclusion

No change is made to production code. The finding is a legitimate Windows-only
concurrency bug, but this Linux worktree cannot build, reproduce, or regression-test
it, and inventing a platform-faking harness would not constitute a faithful fix.
Recommend resolving on a Windows build lane where `LMMS_HAVE_WINMM` is defined and
the WinMM callback thread can be exercised.

Files committed:

- `docs/qwen-fixes-2026-10/cluster-new-p9-277.md`
