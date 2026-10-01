# Zene Studio 0.4.0-alpha — release notes

0.4.0 is the release where the engine work of 0.3.0 becomes reachable from the interface, and where the
contract every command keeps is held by the test suite itself. It follows the plan in
`plans/RELEASE-0.4.0-PLAN.md` (workspace): M0 consolidate, M1 contract and safety, M2 the engine
completions the interface depends on, M3 the interface. Nothing here is a released-grade guarantee: this
is an alpha, and what is known to be missing is written down in `docs/KNOWN-LIMITATIONS.md`.

## Upgrading from 0.3.0

- **The Focus Desk is on by default.** A new configuration opens the Focus Desk workspace; turning it off
  (View menu) is remembered, and a configuration that already saved "off" keeps the classic workspace.
  `Tab` moves keyboard focus, as in every Qt application; switching views has its own shortcuts.
- **Project files gain top-level `z:` sections** (ARCH-4): scenes (`<z:scenes>`), take lanes (`<z:lanes>`),
  provenance (`<z:provenance>`), and a document index when a file carries sections this build does not
  understand. An older project opens and re-saves byte-for-byte when it uses none of them; one that does is
  upconverted on the next save. The Session View is always built (`WANT_SESSION_VIEW` is gone).
- **Writes are atomic** (rename, fsync, `.new` adoption), and a partially written file is never
  mistaken for the project.

## M1 — the contract, held by the suite

- Every reply is checked against its command's declared result schema in the tests
  (`ZENE_CONTROL_CHECK_RESULTS`); the grandfather list of known violations is **empty**.
- Gate 16 (checked coverage): all but three of the release surface's commands reach a checked success
  path in the suite, the three named in `tests/checked-coverage-unreached.txt`.
- ThreadSanitizer and RealtimeSanitizer CI jobs over the concurrency and audio-thread suites
  (`lmms::RealtimeScope` marks the render thread's work and each worker job).
- An engine CPU benchmark tracked as a number (R7.4), and a zoomed-out piano-roll frame benchmark.
- One published figure for the command surface, kept honest by `tests/doc-figures-gate.py`:
  **<!-- canary -->391 ids / 58 groups<!-- /canary -->**.

## M2 — engine completions

- **Recording**: input monitoring In / Auto / Off per track; latency-compensated recording (the take lands
  on the beat it was played on, including a take that starts inside an audio period - BUGS_FOUND 11.17);
  one capture publisher for every backend, JACK included; punch in/out gates the capture.
- **Comping**: take lanes play back, consolidate, and MIDI comping.
- **Automation and modulation**: ramps by default, per-sample modulation for armed routes.
- **Edit groups** carry every edit type; **scene Follow Actions**; the clip-launch grid.
- **Plugin editors**: a run loop, VST3 and CLAP editor embedding, `plugin.editor_*`.
- Rubber Band time-stretch (optional; WSOLA stays the default); stem separation on by default, its model
  fetched on first use; TPDF / noise-shaped dither; microtuning over MTS-ESP and Scala (`mts.*`); a
  live-coding clock for scheduled Lua (`livecode.*`); a waveform generator and operators (`sample.*`); the
  cycle-permitted mixer submode (`feedback.*`).

## M3 — the interface

Registry-first actions: every menu item runs the command it declares (the agent-surface gate holds it).
New in the interface in this release, each over its registered commands:

- **Recording**: Record into this clip (a sample clip's menu; `clip.set_record`), the monitor button,
  Punch in/out on the transport, Arm MIDI Capture / Capture MIDI, File ▸ Recover Recordings.
- **Arranging and editing**: take lanes with audition and consolidate; folder tracks and visibility sets;
  a clip's Gain and fades, Trim to playhead and Slip by a beat; the piano roll's note transforms and Groove
  menu; freeze / bounce and DAWproject import / export.
- **Session**: the clip-launch grid and its transport bar (Follow Actions, record to arrangement).
- **Mixing**: VCA controls, channel pan, delay compensation in the strip tooltip, a channel's Rack menu
  (parallel chains, routing, macros and their bindings), Allow feedback sends, effect chain presets, an
  effect's Hosting (in-process or a separate process).
- **Modulation and tuning**: Edit ▸ Modulators, Edit ▸ Session Tuning, Edit ▸ Tempo Map (with Standard MIDI
  File import / export).
- **Rendering**: File ▸ Render Presets, the export dialog's dither and resampling controls.
- **Hardware and safety**: Edit ▸ MIDI Clock, MIDI Controller Reconnect, controller templates, Plugin
  Quarantine; Help ▸ Crash Reports; File ▸ Script Memory Budget.
- **Shell**: the Focus Desk (default on), the command palette, the keyboard-shortcuts page, the start hub,
  the undo history panel; an accessibility pass (every widget named, knobs keyboard-operable, WCAG AA
  contrast held by `ThemeContrastTest` with no known failures).

## Measured on the release tip

- `ctest` from `<build>/tests`: every test passes (the figure is in the tag's commit message).
- `bash tests/run-all-gates.sh`: every executed gate passes; gate 2 (coverage) runs on CI.
- Hosted CI: the build matrix (linux x86_64 / arm64, macOS x86_64 / arm64, mingw64, msvc x64,
  windows-arm64) and the dispatch-only quality gates - see "Known issues" for what is not green.

## Known issues

- **linux-arm64 renders run far slower than on x86_64**, and Rubber Band allocates on the audio thread
  there (BUGS_FOUND 11.20, open).
- **Golden-audio comparisons differ on some hosted Debug runners** (BUGS_FOUND 11.15, open; the check is
  not loosened).
- Still reachable only through the control socket: rack key/velocity zones (the engine does not route by
  zone yet), the telemetry transport policy, the control-server shutdown hook, and the per-note tuning edits.
- Everything else missing is in `docs/KNOWN-LIMITATIONS.md`.
