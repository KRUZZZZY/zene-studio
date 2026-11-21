# Cluster new-p9-277: WinMM device maps and port pointers race with callbacks

**Verdict: BLOCKED (platform constraint — not a FALSE-POSITIVE, not ALREADY-FIXED)**

This revision corrects the evidence per QA review `new-p9-277.r2.md`. The
`BLOCKED` disposition is retained; the unsupported archive-symbol claim from round 1
is **retracted** (no such archive exists on this build), and the no-archive exit
code in the evidence table is corrected to match what `bash` reports on this
worktree (EXIT 2 for `ls`, EXIT 0 for the empty `find`).

## The finding is present in the source

`src/core/midi/MidiWinMM.cpp` implements the Windows-only (`WinMM`) MIDI backend.
Two concurrency defects are visible in the source:

1. **Map/list race.** The WinMM input callback path runs on the multimedia callback
   thread: `inputCallback` (`:177-183`) calls `handleInputEvent`. `handleInputEvent`
   reads the device map and subscription map and then iterates a subscription list:

   - `MidiWinMM.cpp:200` — `const QString d = m_inputDevices.value( hm );`
   - `MidiWinMM.cpp:201` — `!m_inputSubs.contains( d )`
   - `MidiWinMM.cpp:206-207` — `const MidiPortList & l = m_inputSubs[d];`
     then iterates `l.begin()..l.end()`.

   The GUI thread mutates the very same maps/lists:
   - `subscribeReadablePort` / `subscribeWritablePort` at `:149-152` / `:167-170`
     (`m_inputSubs[dest].removeAll(port)` + `push_back`).
   - `removePort` at `:112-120` (`removeAll` on every sub-list).
   - `updateDeviceList` → `closeDevices` at `:234-241` / `:245-268`, which
     `m_inputSubs.clear(); m_outputSubs.clear();` (`:247-248`),
     `m_inputDevices.clear(); m_outputDevices.clear();` (`:266-267`), and calls
     `midiInReset`/`midiInClose` (`:256-257`).

   A device refresh or a subscription change overlapping an incoming callback
   therefore races on non-atomic `QMap`/`QList` state.

2. **Raw-port lifetime race.** The callback dereferences raw `MidiPort*` values:
   `MidiWinMM.cpp:217` and `:221` (`(*it)->processInEvent(...)`). Port destruction
   unregisters the port: `MidiPort::~MidiPort()` calls
   `m_midiClient->removePort( this )` at `MidiPort.cpp:131`. `removePort` on the
   WinMM client (`MidiWinMM.cpp:110-123`) does unsynchronised `removeAll` loops, so a
   port can be freed concurrently with (or between the checks and) the callback's
   use — an invalid iteration / call through a freed port.

## Why it is BLOCKED on this worktree

The entire `MidiWinMM` translation unit and class are compiled out on Linux, so the
buggy code cannot be built, exercised, or regression-tested here:

- `CMakeLists.txt:271` — `IF(LMMS_BUILD_WIN32 OR LMMS_BUILD_CYGWIN)` sets
  `SET(LMMS_HAVE_WINMM TRUE)` at `:279`; the non-Windows `ELSE()` branch reports
  `STATUS_WINMM "<not supported on this platform>"` (`:291`) and never defines the
  macro.
- `build/lmmsconfig.h:40` — `/* #undef LMMS_HAVE_WINMM */` in this configured Linux
  build.
- `src/core/midi/MidiWinMM.cpp:27` / `:308` — the whole implementation body is inside
  `#ifdef LMMS_HAVE_WINMM`; on Linux the TU is empty.
- `include/MidiWinMM.h:30` / `:151` — the class declaration, its members
  `m_inputDevices` / `m_outputDevices` / `m_inputSubs` / `m_outputSubs`
  (`:133-139`), and the cited methods are all inside `#ifdef LMMS_HAVE_WINMM`.

Consequently no deterministic RED/GREEN test can reference `MidiWinMM`, its members
or the `HMIDIInProc` callback on this platform, and a fix would require Win32
multimedia APIs (`HMIDIIN`/`HMIDIOUT`, the callback thread) and a Windows build to
verify. Regression testing for this cluster is routed to a **Windows build lane**
where `LMMS_HAVE_WINMM` is defined and the callback thread can be driven.

## Evidence table

| Check | Exact command | Result / EXIT |
| --- | --- | --- |
| Config define absent | `grep -n WINMM build/lmmsconfig.h` | `40:/* #undef LMMS_HAVE_WINMM */` — **EXIT 0** (macro undefined) |
| CMake platform guard | `grep -n "LMMS_HAVE_WINMM" CMakeLists.txt` | only `279:\tSET(LMMS_HAVE_WINMM TRUE)`, under `IF(LMMS_BUILD_WIN32 OR LMMS_BUILD_CYGWIN)` at `:271` — **EXIT 0** |
| Impl guard coverage | `grep -n "LMMS_HAVE_WINMM" src/core/midi/MidiWinMM.cpp` | `27:#ifdef`, `308:#endif` — **EXIT 0** |
| Header guard coverage | `grep -n "LMMS_HAVE_WINMM" include/MidiWinMM.h` | `30:#ifdef`, `151:#endif` — **EXIT 0** |
| No core archive (retraction) | `ls build/src/core/*.a` | `No such file or directory` — **EXIT 2**; the core sources are an OBJECT library, not an archive |
| No core archive (explicit) | `find build -name '*.a' -path '*core*'` | no matches printed — **EXIT 0**; confirms no static archive under `build/.../core` |
| Core target | `grep -n "ADD_LIBRARY(lmmsobjs" src/CMakeLists.txt` | `137:ADD_LIBRARY(lmmsobjs OBJECT` — **EXIT 0** |
| RED command | — | **Cannot run** — no `MidiWinMM` symbol exists to link a test against on Linux |
| GREEN command | — | **Cannot run** — same reason |
| Reversed-fix proof | — | **N/A** — no production change is made; nothing to reverse |
| Affected family | — | 0 CTest tests (no WinMM test exists, and none can be built here) |
| Complexity gate | `bash tests/complexity-gate.sh --check` | **EXIT 1** — pre-existing, unrelated (see below) |
| File-length gate | `bash tests/file-length-gate.sh --check` | **EXIT 1** — pre-existing, unrelated (see below) |

Round-1 retraction: the original report claimed
`nm build/src/core/libzene-core.a | grep -i MidiWinMM` was empty. That archive does
not exist (the core is the `lmmsobjs` OBJECT library at `src/CMakeLists.txt:137`;
`find build -name '*.a' -path '*core*'` matches nothing). The piped `nm|grep` result
therefore proved nothing, and the claim was removed.

Round-3 correction: the round-2 evidence table recorded `ls build/src/core/*.a`
as EXIT 1; under bash the same command exits 2 (`No such file or directory`). The
evidence table now shows the bash exit codes (EXIT 2 for `ls`, EXIT 0 with no
matches for `find`) and the OBJECT-library row is retained as the underlying
explanation. No source/config evidence changed.

## Gate failures are unrelated to this cluster

Both ratchets are already red on this branch, caused by other clusters' accepted
work, not by this report (this commit is documentation-only):

- File-length: new over-500 files including `src/core/SmfInterchangeReader.cpp`
  (introduced by cluster `new-p9-435` / `new-p8-232`), `src/core/TakeLane.cpp`,
  and several `tests/src/core/*Test.cpp`.
- Complexity: new over-CCN-10 functions in `tests/src/gui/XyPadPairTest.cpp`,
  `src/core/DawProjectReadTracks.cpp`, `src/core/ControlCommandsMixerTools.cpp`,
  `src/core/ControlCommandsMixerStrip.cpp`, `src/core/ProjectRevisions.cpp`,
  `src/gui/ControlCommandsShortcuts.cpp`.

Fixing these would be out-of-scope edits to other clusters' files; the gate limits
and baselines are not touched. Round-3 logs (re-captured to confirm the EXIT
codes are stable on this worktree):
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-277.r3/logs/evidence-1.log`,
`.../gate-1.log`, `.../gate-2.log`.

## Conclusion

No production change is made. The finding is a legitimate Windows-only concurrency
bug that is present in the source (map/list race and raw-port lifetime race), but
this Linux worktree cannot build, reproduce, or regression-test it. It should be
resolved on a Windows build lane where `LMMS_HAVE_WINMM` is defined.

Files committed:

- `docs/qwen-fixes-2026-10/cluster-new-p9-277.md`
