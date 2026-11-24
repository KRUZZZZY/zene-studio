FIXED

# Cluster new-p9-276: WinMM MIDI output allocates on the playback path

## Decision and evidence

REAL, and fixed here. `MidiWinMM::processOutEvent()` ran a per-event name search: it built a
fresh `QStringList` of destination device names for every output event and then called
`contains()` on it once per open device. `processOutEvent()` is reachable from the audio
thread (`InstrumentTrack::processOutEvent` → `MidiPort::processOutEvent` →
`MidiClient::processOutEvent`), so both container operations ran on the playback path.

- Pre-fix code, `git show HEAD:src/core/midi/MidiWinMM.cpp`:
  `:57` the function, `:61` `QStringList outDevs;`, `:68` `outDevs += it.key();`,
  `:76` `if( outDevs.contains( *it ) )`. This is the finding's `55-78` range.
- The playback path itself: `src/tracks/InstrumentTrack.cpp:720` (`m_midiPort.processOutEvent`),
  `include/MidiPort.h:159` (`MidiPort::processOutEvent`), `include/MidiClient.h:66`
  (`processOutEventAt` → `processOutEvent`) and `src/tracks/InstrumentTrackNotes.cpp:252`.
- The same file's own realtime comment states the rule the old code broke:
  `src/tracks/InstrumentTrackNotes.cpp:392` "Audio thread (processOutEvent): a handful of
  copies of a POD event, no allocation."

Not ALREADY-FIXED: at HEAD the allocation is still there (the three lines quoted above are
the branch tip). Not FALSE-POSITIVE: the allocation is on a path the audio thread runs, and
the audio thread may not allocate.

## The fix

The routing decision is precomputed and published; the playback path only reads it.

- `include/MidiWinMMOutRouting.h` (new, fork-NEW product header): `MidiWinMMOutRoute`
  (a port identity plus a device handle), `MidiWinMMOutRouting::buildRoutes()` (the
  name→device search, run once, on the publisher's thread), `publish()` (swaps the whole
  immutable stop list in with one release store; publishing an identical list is a no-op, so
  no allocation and no pointer churn), `snapshot()` and `forEachDevice()` (one acquire load
  and a scan over a POD array: no allocation, no free, no lock, no logging).
- `src/core/midi/MidiWinMM.cpp:62-73`: `processOutEvent()` sends through
  `m_outputRouting.forEachDevice( port, ... )`; the `QStringList`, the `+=` and the
  `contains()` are gone.
- `src/core/midi/MidiWinMM.cpp:79-90`: `refreshOutputRouting()` rebuilds the stop list from
  `m_outputDevices` (in `QMap` order, which is the order the name search sent in) and
  `m_outputSubs`, then publishes it. It is called from every place that edits either map:
  `applyPortMode` (`:115`), `removePort` (`:133`), `subscribeWritablePort` (`:186`),
  `closeDevices` (`:286`) and `openDevices` (`:323`).
- `include/MidiWinMM.h:137` declares `refreshOutputRouting()`, `:155` the
  `m_outputRouting` member, `:37` the include.

Equivalence with the code it replaced, carried into the test: stops are emitted device by
device in device-map order; two open devices that share a name both receive, because each
name is looked up for every device; a port listed twice for one name still gets one send per
device (the old `break`); a name with no open device routes nowhere; a port that is not
subscribed receives nothing.

Threading and ownership (reasoned, not measured — local TSan does not run on this box): the
publisher is the GUI thread, which is the only thread that edits `m_outputSubs` /
`m_outputDevices`; readers (audio thread and the non-audio feedback caller) take one acquire
load and scan an immutable array. A published snapshot stays alive until the routing object
is destroyed (with the client), so a reader that loaded an older one is never reading freed
memory; the memory cost is proportional to the routing *changes* in a session, not to events,
and a republish of unchanged routing allocates nothing. A `MidiPort*` in a snapshot is only
compared, never dereferenced, so a port that dies before the next republish can only fail to
match.

Not done, deliberately: the finding also suggests "send through a bounded queue". That is a
transport change (a worker thread and a second producer of `midiOutShortMsg`) with observable
timing/ordering effects for every WinMM device, and it does not remove the allocation this
finding names — the queue's own bookkeeping does. The allocation is fixed at its cause; the
queue is left alone. Recorded under Other findings for triage.

## Platform note, and what is verified where

`LMMS_HAVE_WINMM` is off in this tree (`build/lmmsconfig.h:40`,
`CMakeLists.txt:271`-`:291`), so the WinMM translation unit is empty here and the WinMM
callback cannot be driven locally. This is the same situation cluster new-p14-179 (same file)
and Q050-488 handled: the platform-neutral half is extracted into a header the tests can
exercise, and the product source is asserted to still go through it. The precedent is in
`tests/upstream-modifications.txt` (the WinMM short-message entry, cluster new-p14-179).

So the Windows translation unit itself was type-checked with a Win32 stub header: the
`compile_commands.json` flags of `src/core/midi/MidiClient.cpp`, `-fsyntax-only`, a stand-in
`windows.h`/`mmsystem.h` with the `midiIn*`/`midiOut*` declarations and a stand-in
`lmmsconfig.h` that sets `LMMS_HAVE_WINMM` — `EXIT 0`, log `winmm-syntax-check-2.log`. That
the check really compiled the WinMM body (and not the empty `#ifdef` branch) was confirmed by
preprocessing the same command: `midiOutShortMsg( static_cast<HMIDIOUT>`,
`m_outputRouting.forEachDevice`, `refreshOutputRouting` and `winmmShortMessage( event )` all
appear in the 4.9 MB of preprocessed output. The real Windows build is the train's hosted
portability job, not this lane.

## Test matrix

Regression test: `tests/src/core/MidiWinMMShortMessageTest.cpp` (the existing test file for
this output path; it already asserts the output path's packing).

| path | slot |
|---|---|
| per-event allocation (the bug) | `outputPathDoesNotAllocate`: 1000 events over the published snapshot allocate 0 and free 0; the pre-fix per-event search, reproduced in the test as the control, must allocate > 0 or the criterion is reported as measuring nothing |
| normal path / equivalence | `routingMatchesTheNameSearchItReplaced`: every port's device list must equal what the pre-fix name search produced, over shared-name devices, two ports, an unopen name and a duplicated port entry |
| reconfiguration | `routingFollowsReconfiguration`: no publish → no sends; publish; unsubscribe; resubscribe plus a second device of the same name; device-list clear (the `closeDevices`/`openDevices` refresh); identical republish keeps the reader's snapshot pointer |
| exhaustion / bounds | `routingStaysAllocationFreeAtScale`: 32 devices × 16 ports (512 stops, one duplicate collapsed), still 0 allocations and 0 frees |
| audio-thread constraints | the two allocation slots above count allocations *and* frees on the calling thread (the repo's `AllocationProbe`); no lock, wait or log is reachable from `forEachDevice()` (header-only: one atomic load plus an array scan) |
| product wiring | `outputPathRoutesThroughThePublishedSnapshot`: reads `src/core/midi/MidiWinMM.cpp` and requires the `QStringList outDevs` / `outDevs.contains` search to be gone, `m_outputRouting.forEachDevice` to be there, and at least 6 `refreshOutputRouting()` mentions (the definition plus the five edit sites) |
| supported-platform build | the Win32 stub `-fsyntax-only` check above; the test's own guards make a missing `MidiWinMMOutRouting.h` a failure, not a silent skip |

No timing, sleeps, threads or randomness: the allocation probe counts on the calling thread
and every expectation is a literal.

## Commands and exit codes

All logs: `/home/kruzzzzy/.config/kads/zene/state/runs/new-p9-276.r1/logs/`.
Builds and tests of the test target use `CCACHE_DISABLE=1`: ccache was serving a cached object
for `MidiWinMMShortMessageTest.cpp` compiled while `MidiWinMMOutRouting.h` did not exist, which
made `__has_include` look false in a "rebuilt" binary (see Other findings).

| step | command | exit |
|---|---|---|
| RED (before the fix) | `cmake --build build --target MidiWinMMShortMessageTest -j4` (build-red-1) | 0 |
| | `ctest -R MidiWinMMShortMessageTest --output-on-failure` (test-red-1) | 8 (5 failed) |
| GREEN | build (build-green-2) | 0 |
| | `ctest -R MidiWinMMShortMessageTest` (test-green-2) | 0 |
| 4b reverse | `git apply -R .../logs/fix.patch` (apply-reverse-2) | 0 |
| | build (build-red-3) | 0 |
| | `ctest -R MidiWinMMShortMessageTest` (test-red-3) | 8 (5 failed, 8 passed) |
| 4b forward | `git apply .../logs/fix.patch` (apply-forward-2) | 0 |
| | build (build-green-6) | 0 |
| | `ctest -R MidiWinMMShortMessageTest` (test-green-6) | 0 (13/13) |
| zene rebuild | `cmake --build build --target zene -j4` (build-zene-1) | 0 |
| family | `ctest -R "MidiEventToByteSeqTest|MidiInputGateTest|MidiWinMMShortMessageTest"` (test-family-1) | 0 (3/3) |
| Windows TU type-check | `-fsyntax-only` with the Win32 stub (winmm-syntax-check-2) | 0 |
| gates | `python3 .../qwen/feature_check.py <worktree>` (gates-1) | 0, every row PASS |

The `Midi*` ctest family is 51 tests in this tree but only a few test executables are built
(`ctest -R Midi` reports the rest `***Not Run`, i.e. no binary — test-family-midi-1.log). The
built, connected ones were built and run: the table row above. Nothing else on this platform
compiles the changed WinMM code, so no other test can regress from this change; `zene` itself
was rebuilt (0).

## Self-check

1. **Red/green, exit codes.** `fix.patch` is `git diff` of the three product files only
   (`include/MidiWinMMOutRouting.h`, `include/MidiWinMM.h`, `src/core/midi/MidiWinMM.cpp`).
   With the product change reverted (`git apply -R .../logs/fix.patch`, EXIT 0; build
   `build-red-3`, EXIT 0) the test exits **8**: the product source assertion fails and the four
   routing slots fail with "include/MidiWinMMOutRouting.h is missing". With the change
   (`git apply .../logs/fix.patch`, EXIT 0; build `build-green-6`, EXIT 0) the test exits **0**
   (13 passed). Before the first commit the same cycle ran as test-red-1 (8) → test-green-2 (0).
   The test does not pass without the fix.
2. **Determinism.** No timing, sleeps, threads, timers or randomness: the allocation counts are
   thread-local increments from the repo's `AllocationProbe`, every routing expectation is a
   literal device/port list, and the equivalence slots compare against the pre-fix algorithm
   reproduced in the test.
3. **Completeness.** Every mutation of `m_outputSubs` / `m_outputDevices` republishes
   (`applyPortMode`, `removePort`, `subscribeWritablePort`, `closeDevices`, `openDevices`) and
   the wiring slot counts them, so a future edit site that forgets to republish fails the test.
   `MidiWinMM::processOutEvent` is the only caller of the routing, and the header's declaration
   and definition changed together.
4. **Collateral damage.** No limit is introduced: `buildRoutes()` allocates a `std::vector`
   sized by the real device/port counts, so no device count or subscription count that worked
   before is refused now. Saved data, undo and commands are untouched (no `Song*`, `*Record*`,
   `*Undo*`, `*Transport*`, `Take*` or `AudioEngine*` file is in the diff). Behaviour outside
   the allocation is identical: same devices, same order, same multiplicity, verified by the
   equivalence slot against the pre-fix algorithm.
5. **Realtime.** Nothing added on the playback path allocates, frees, locks, waits or logs:
   `forEachDevice()` is one `std::memory_order_acquire` load of an `std::atomic` pointer and a
   loop over a POD array, and `midiOutShortMsg` is called exactly where it was. Publishing
   happens on the thread that edits the subscription and device maps (the GUI thread).
6. **Registration.** `include/MidiWinMMOutRouting.h` is a fork-NEW product file, so it is in
   `tests/fork-sources.txt` and in `tests/all-sources.txt` (sorted place, the manifest still
   reproduces); the two inherited files it changes are in `tests/upstream-modifications.txt`
   with their reason. `feature_check.py` is all PASS (gates-1.log), including fork-sources,
   all-sources-reproduce, no-upstream-regression, complexity and file-length.
7. **Verdict.** FIXED, not ALREADY-FIXED (the branch tip still had the `QStringList` search)
   and not FALSE-POSITIVE (the path runs on the audio thread, which may not allocate).
8. **Sibling commands.** No command file changed: the diff touches no `ControlCommands*.cpp`,
   no command registry entry and no command schema (`git diff --name-only` lists only the three
   WinMM product files, the WinMM test source, the three registration manifests and this
   report). `app.*`, `arrangement.*` and the other commands the check names are therefore
   unaffected, and `ControlCommandsSnapshot`'s canary is untouched.
9. **Old projects and saved data.** Nothing on a save/load path changed: `MidiWinMM` has no
   `saveSettings`/`loadSettings` (the port's own settings and the project file are untouched),
   and the routing is rebuilt from live state at startup and on every edit, so a project that
   loaded before loads the same way and routes the same way.

## Other findings

- `src/core/midi/MidiApple.cpp:71-90` is the same algorithm, line for line, on the macOS
  backend: a per-event `QStringList` of destination names plus `contains()` per device. It is
  outside this finding's file and cannot be compiled or tested on this host, so it is listed
  for triage, not changed here. If it is accepted, the same `MidiWinMMOutRouting` shape applies
  (keyed on `MIDIEndpointRef`).
- `src/core/midi/MidiWinMM.cpp:213` (input path) reads `const int par2 = ev >> 16;` without
  `& 0xff` while the line above masks `par1`. Pre-existing, unrelated to the output path; kept
  from the new-p14-179 note for triage.
- The WinMM input callback still reads the subscription and device maps concurrently with GUI
  edits (cluster new-p9-277's subject). This change removes the *output* side's dependence on
  those maps on the playback path; the input side is untouched.
- Toolchain trap worth knowing: `ccache` was returning a cached object for the test source from
  when the new header did not exist, so `__has_include` evaluated false in a "rebuilt" binary
  and produced a fake failure. All builds here set `CCACHE_DISABLE=1`.
