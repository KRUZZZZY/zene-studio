# Zene Studio 0.4.1-alpha — release notes

0.4.1 is an interim snapshot of the 0.5.0 line, released so the work since 0.4.0 can be downloaded
before 0.5.0 is finished. It is the automatic merge train `050/m0-train-16` (`cf3618e61`) plus this
version bump. Most of it is bug fixes: about 1,000 reviewed bug-fix branches, and roughly 50 completed
0.5.0 queue units. This is an alpha. What is known to be missing is in `docs/KNOWN-LIMITATIONS.md`, and
the failures known at this commit are listed under "Known issues" below.

## Upgrading from 0.4.0

- **Projects:** opt-in v2 project save/load (Q050-007). The default format is unchanged, so a 0.4.0
  project opens and saves as before. Missing instruments keep their data and show a placeholder instead
  of being dropped (Q050-018).
- **Recovery:** startup recovery can be undone (Q050-046); opening a project and running a script are
  undo checkpoints (Q050-044); recorded audio is flushed more often, so a crash loses less (Q050-045).

## Bug fixes

About 1,000 bug-fix branches merged since 0.4.0. They came from the program's bug sweeps. Each one was
fixed on its own branch, independently reviewed, and, where the fix changes product code, carries a
regression test shown to fail without it. They were merged in trains 2 to 16, each gated on the test
suite. Highlights:

- **Audio thread:** recorder setup moved off the audio thread (Q050-002); the Carla processing mutex
  (Q050-041); lock-free audio wakeups on older macOS (Q050-167); freeze-region audio and recovery
  (Q050-008).
- **MIDI:** MIDI export tempo, timing, program and channel (Q050-020); the selected MIDI clock source is
  honoured (Q050-026); MPE release velocity (Q050-039); note chase on unmute (Q050-040); plug-in MIDI and
  media drops (Q050-031).
- **Import and plug-ins:** DAWproject DEFLATE and tolerant import reporting (Q050-001); mixer and hardware
  I/O kept on track import (Q050-027); Neural Amp model loading no longer blocks (Q050-030); invalid remote
  knob indices rejected (Q050-029); browser analysis kept to its window (Q050-048).

## New and completed

- **Editing:** articulation map editor (Q050-015); chord progression panel (Q050-019); clip gain,
  envelope and polarity edited on the waveform (Q050-125); note editor input ergonomics (Q050-064); the
  full warp mode set (Q050-036).
- **Recording and session:** recorded session content lands in arrangement clips; punch recording has its
  own ruler range (Q050-147); remaining record time is shown (Q050-179).
- **Mixing and routing:** mono and discrete bus formats (Q050-166); automation lanes shown as track
  children (Q050-022); audio streaming between Ableton Link peers (Q050-024).
- **Expression:** MPE expression event streams (Q050-016).
- **Browser:** tag and embedded-metadata search (Q050-075); category filter (Q050-170); file URLs on
  browser and sample-clip drags (Q050-133).
- **Scripting and safety:** sandboxed Lua module imports (Q050-051); live-code panel (Q050-154); crash-aware
  plug-in quarantine (Q050-049); preset-based range and section export (Q050-009).
- **Stem separation:** a quality choice, with output comparison (Q050-062).
- **Accessibility and layout:** keyboard traversal through mixer controls (Q050-068); spoken timeline
  position, selection and markers (Q050-080); touch-first workspace layout (Q050-235).
- **Documentation:** plug-in formats and VST3 host claims, stem separation availability, rack, VCA and
  Follow Action limitations, warp and MIDI Clock pages, the free/activation model, and a guide to patterns,
  arrangement clips, launcher cells and mixer routing.

## Known issues

- **Test suite (measured on the train, Linux, `-j4`):** 722 of 729 tests pass. The 7 failures are all
  already on the known-failure baseline from earlier trains, so this train added none:
  `ScriptMemoryBudgetTest`, `AbletonLinkSessionTest` (segfault), `ControlRenderSurround`,
  `ControlSocketPathSafety`, `ControlAbsentSocket`, `MmpzGitDepthTest`, `ControlMcpGroupCoverage`. They
  are queued as bugs.
- **Gates:** `run-all-gates.sh` — 11 of 14 executed gates pass. The ctest gate fails on the 7 tests above.
  Complexity (4) and file-length (7) are red on purpose on the 0.5.0 line; their items are queued.
- **Hosted CI:** this line had not been built on the hosted matrix (Qt 5.15, MSVC, macOS) before this tag.
  The machine it was developed on is Qt 6 only. A platform has a package only if its job goes green on the
  tag. Treat any missing package as a known gap, not a silent omission.
- Everything in `docs/KNOWN-LIMITATIONS.md` still applies.
