# 0.5.0 work queue

**Top feature category — Rune UI rework (owner, 2026-11-14):** Q050-505–516,
all difficulty **3**, with dependency order, preparation and existing Terra/release
gates preserved. [Specification](RUNE-REWORK-SPEC.md),
[interaction contract](RUNE-INTERACTION-CONTRACT.md), and
[Terra scenarios](rune-review-scenarios.json). The canonical scheduling catalogue
is `plans/queue-050.jsonl` (515 rows after this addition; split stages are additional
runtime inventory records). Earlier counts below are historical.

**Owner-approved additions (2026-10-27):** thirteen units Q050-490–502 (plug-in windows on Windows/macOS, macOS Audio Units, accessibility, command-only GUI gaps, limitations docs, an update check, Wayland plug-in windows); see the section at the end.

**Owner-approved amendments (2026-10-25):** four reliability features Q050-486–489; Q050-139 → Q050-180 (completion stage Q050-180.1), Q050-181 → Q050-253. The current scheduling source is `plans/queue-050.jsonl`; see the review application report.

Built by `research/feature-research-2026-10-07/queue-050/build_queue.py` from 1548 candidates (the 2026-10-07 research consolidation and `plans/BACKLOG-050-FEATURES.md`), each verified against `zene-features-merge` (050/features-2026-10-06). Every candidate is in one unit below or in `QUEUE-050-NOT-DOING.md`; the accounting is in `queue-050/COVERAGE.md`. Work units in order; one lane at a time.

| # | pri | area | type | size | title | items | lane |
|---|---|---|---|---|---|---:|---|
| Q050-001 | P0 | L12 | completion | M | DAWproject DEFLATE and tolerant import reporting | 1 | importers-tolerant |
| Q050-002 | P0 | L03 | fix | M | Move recorder setup off the audio thread | 1 | alloc-probe-fix |
| Q050-003 | P0 | L10 | docs | S | VST3 host claims and output-bus documentation | 1 | effect-multiout |
| Q050-004 | P0 | L16 | completion | S | Label collaboration as experimental and document its limits | 1 | collab-experimental |
| Q050-005 | P0 | L10 | fix | S | Granular pitch-shifter buffer resize fix | 1 | alloc-probe-fix |
| Q050-006 | P0 | L05 | fix | M | Restore realtime-safe Rubber Band on Linux ARM | 1 | alloc-probe-fix |
| Q050-007 | P0 | L12 | completion | M | Opt-in v2 project save/load | 1 | container-v2-optin |
| Q050-008 | P0 | L15 | fix | L | Repair freeze-region audio and recovery | 3 |  |
| Q050-009 | P0 | L12 | fix | L | Preset-based range and section export in the GUI | 5 |  |
| Q050-010 | P0 | L15 | fix | L | Move long-running jobs off the interface thread | 3 |  |
| Q050-011 | P0 | L14 | fix | S | Keep accessible target sizes after knob updates | 3 |  |
| Q050-012 | P0 | L16 | feature | L | Replicate common GUI edits through collaboration | 3 |  |
| Q050-013 | P0 | L02 | fix | L | Complete audio-cell playback across track types | 3 |  |
| Q050-014 | P0 | L10 | docs | S | Document supported plug-in formats and caveats | 3 |  |
| Q050-015 | P0 | L07 | completion | M | Articulation map editor | 1 |  |
| Q050-016 | P0 | L07 | completion | L | MPE expression event streams | 3 |  |
| Q050-017 | P0 | L16 | completion | L | Complete direct-GUI edit replication coverage | 2 |  |
| Q050-018 | P0 | L12 | fix | M | Preserve missing-instrument data and report placeholders | 2 |  |
| Q050-019 | P0 | L07 | completion | M | Chord progression panel | 2 |  |
| Q050-020 | P0 | L12 | fix | M | MIDI export tempo, timing, program, and track channel | 2 |  |
| Q050-021 | P0 | L04 | fix | S | Keep stopped seek and playback position aligned | 1 |  |
| Q050-022 | P0 | L06 | feature | M | Show automation lanes as track children | 1 |  |
| Q050-023 | P0 | L12 | fix | L | Reference project copy and audition with EditLocks | 4 |  |
| Q050-024 | P0 | L16 | feature | L | Stream audio between Ableton Link peers | 1 |  |
| Q050-025 | P0 | L12 | fix | M | Preserve DAWproject MIDI channels end to end | 1 |  |
| Q050-026 | P0 | L15 | fix | S | Honor the selected MIDI clock source | 1 |  |
| Q050-027 | P0 | L09 | fix | M | Preserve mixer and hardware I/O on track import | 1 |  |
| Q050-028 | P0 | L15 | fix | S | Wake the ALSA MIDI output thread on queued events | 1 |  |
| Q050-029 | P0 | L15 | fix | S | Reject invalid remote knob indices | 1 |  |
| Q050-030 | P0 | L10 | fix | S | Neural Amp model-load responsiveness | 1 |  |
| Q050-031 | P0 | L04 | fix | M | Safely accept plug-in MIDI and media drops | 1 |  |
| Q050-032 | P0 | L08 | fix | S | Select a mixer strip on any control press | 1 |  |
| Q050-033 | P0 | L12 | fix | L | Preserve source timing in cross-project imports | 1 |  |
| Q050-034 | P0 | L02 | completion | M | Land recorded session content in arrangement clips | 1 |  |
| Q050-035 | P0 | L05 | docs | S | Correct stem separation availability docs | 1 |  |
| Q050-036 | P0 | L05 | completion | L | Complete the specified warp mode set | 1 |  |
| Q050-037 | P0 | L06 | fix | M | Define the automation value before its first point | 1 |  |
| Q050-038 | P0 | L06 | feature | M | Add a Shaper modulator and refresh its spec | 1 |  |
| Q050-039 | P0 | L07 | fix | M | MPE release-velocity capture fix | 1 |  |
| Q050-040 | P0 | L07 | fix | S | Unmute note-chase fix | 1 |  |
| Q050-041 | P0 | L10 | fix | S | Carla processing mutex fix | 1 |  |
| Q050-042 | P0 | L12 | fix | S | Preserve long patterns in legacy project upgrades | 1 |  |
| Q050-043 | P0 | L15 | fix | S | Bound SessionTuning reader retries | 1 |  |
| Q050-044 | P0 | L17 | fix | M | Undo checkpoints for project opening and scripts | 1 |  |
| Q050-045 | P0 | L03 | fix | M | Reduce the crash-loss window for recorded audio | 1 |  |
| Q050-046 | P0 | L03 | fix | M | Offer undoable recovery at startup | 1 |  |
| Q050-047 | P0 | L10 | docs | S | Correct rack UI limitations documentation | 1 |  |
| Q050-048 | P0 | L11 | fix | S | Bound browser feature analysis to the analysis window | 1 |  |
| Q050-049 | P0 | L10 | feature | M | Crash-aware plug-in quarantine | 1 |  |
| Q050-050 | P0 | L10 | fix | S | WASM worker log snapshot race | 1 |  |
| Q050-051 | P0 | L17 | feature | M | Sandboxed Lua module imports | 1 |  |
| Q050-052 | P1 | L12 | completion | L | Project media portability across Save As, relink, and handoff | 6 |  |
| Q050-053 | P1 | L14 | completion | L | Linux screen-reader core-flow verification | 5 |  |
| Q050-054 | P1 | L03 | feature | L | Record and comp MIDI takes on instrument tracks | 3 |  |
| Q050-055 | P1 | L13 | completion | L | Context-aware shortcuts and plug-in actions | 4 |  |
| Q050-056 | P1 | L14 | completion | L | Windows and macOS screen-reader compatibility | 4 |  |
| Q050-057 | P1 | L06 | feature | L | Add an audio envelope follower modulation source | 3 |  |
| Q050-058 | P1 | L10 | feature | L | Nested reusable instrument racks | 3 |  |
| Q050-059 | P1 | L10 | feature | L | Modular instrument graph and starter modules | 4 |  |
| Q050-060 | P1 | L12 | completion | L | DAWproject state fidelity and Pro Tools handoff | 2 |  |
| Q050-061 | P1 | L13 | completion | L | Customizable menus, toolbars, and mouse modifiers | 3 |  |
| Q050-062 | P1 | L05 | feature | L | Choose stem-separation quality and compare output | 3 |  |
| Q050-063 | P1 | L05 | completion | L | Author warp markers with on-clip preview | 4 |  |
| Q050-064 | P1 | L07 | completion | M | Note editor input ergonomics | 3 |  |
| Q050-065 | P1 | L03 | completion | L | Place retrospective captures on take lanes | 3 |  |
| Q050-066 | P1 | L10 | feature | L | Drum-rack pad workflow | 5 |  |
| Q050-067 | P1 | L13 | feature | M | Common DAW shortcut presets and migration guide | 2 |  |
| Q050-068 | P1 | L14 | feature | M | Keyboard traversal through mixer controls | 3 |  |
| Q050-069 | P1 | L13 | completion | M | Live theme and palette updates | 2 |  |
| Q050-070 | P1 | L07 | completion | L | Project key and scale-degree context | 3 |  |
| Q050-071 | P1 | L10 | completion | L | Third-party plug-in compatibility matrix | 2 |  |
| Q050-072 | P1 | L14 | feature | M | Speak pattern, track, and mixer state changes | 3 |  |
| Q050-073 | P1 | L05 | completion | L | Edit clip fades and apply them in batches | 2 |  |
| Q050-074 | P1 | L15 | feature | L | Expose track CPU, dropout and audio-device diagnostics | 4 |  |
| Q050-075 | P1 | L11 | completion | S | Expose tag and embedded-metadata search in the browser | 2 |  |
| Q050-076 | P1 | L12 | feature | L | Stem delivery with bus selection and pre/post-FX taps | 3 |  |
| Q050-077 | P1 | L05 | completion | L | Preserve frame precision through warped timeline edits | 2 |  |
| Q050-078 | P1 | L07 | feature | L | Keyboard-first tracker editor | 2 |  |
| Q050-079 | P1 | L10 | completion | M | Reason Rack compatibility fixture | 2 |  |
| Q050-080 | P1 | L14 | feature | M | Speak timeline position, selection, and markers | 3 |  |
| Q050-081 | P1 | L15 | feature | L | Add controller scripting and a mapping editor | 3 |  |
| Q050-082 | P1 | L02 | docs | S | Correct the Follow Action editor limitation in the docs | 2 |  |
| Q050-083 | P1 | L07 | completion | L | Multi-track MIDI editor | 2 |  |
| Q050-084 | P1 | L10 | feature | L | Layered-instrument note and output routing | 2 |  |
| Q050-085 | P1 | L02 | feature | M | Complete keyboard and MIDI launcher mappings | 2 |  |
| Q050-086 | P1 | L14 | feature | M | Screen-reader access to hosted plug-in parameters | 2 |  |
| Q050-087 | P1 | L10 | feature | L | Device-chain patcher canvas | 3 |  |
| Q050-088 | P1 | L08 | completion | L | Console channel-strip controls and glue compression | 2 |  |
| Q050-089 | P1 | L09 | completion | L | Hardware insert latency detail and real-time export | 2 |  |
| Q050-090 | P1 | L10 | feature | M | Plug-in manager vendor and background scan | 2 |  |
| Q050-091 | P1 | L12 | feature | M | Multi-part MusicXML import/export round trip | 2 |  |
| Q050-092 | P1 | L03 | docs | S | Update capture and recording documentation | 2 |  |
| Q050-093 | P1 | L05 | completion | L | Detect tempo and auto-warp imported loops | 3 |  |
| Q050-094 | P1 | L03 | completion | M | Make audio record modes honor history | 2 |  |
| Q050-095 | P1 | L06 | feature | M | Expose master and channel controls as modulation targets | 2 |  |
| Q050-096 | P1 | L07 | completion | M | Chord context and stamping in the new editor | 2 |  |
| Q050-097 | P1 | L10 | feature | L | Mapped sampler-zone editor | 2 |  |
| Q050-098 | P1 | L10 | completion | M | Yabridge compatibility slice | 1 |  |
| Q050-099 | P1 | L14 | feature | L | Keyboard editing for waveform and MPE lanes | 3 |  |
| Q050-100 | P1 | L14 | feature | M | Spoken meters and peak watching | 2 |  |
| Q050-101 | P1 | L05 | feature | L | Transcribe drum hits to MIDI | 1 |  |
| Q050-102 | P1 | L09 | completion | L | Named multichannel I/O and record mapping | 2 |  |
| Q050-103 | P1 | L10 | feature | M | Sampler envelopes and released-voice policy | 2 |  |
| Q050-104 | P1 | L17 | completion | M | Scoped agent access with shared undo | 2 |  |
| Q050-105 | P1 | L05 | completion | M | Control slice markers from MIDI | 2 |  |
| Q050-106 | P1 | L05 | feature | M | Split stereo files and convert clips to mono | 2 |  |
| Q050-107 | P1 | L06 | feature | L | Expand built-in modulation sources | 2 |  |
| Q050-108 | P1 | L08 | completion | L | Per-channel loudness, true-peak, and VU metering | 3 |  |
| Q050-109 | P1 | L12 | feature | L | MPC project import: supported track, sample, and pattern content | 1 |  |
| Q050-110 | P1 | L02 | feature | M | Record MIDI directly into launcher cells | 1 |  |
| Q050-111 | P1 | L04 | feature | L | Add per-clip realtime effects | 1 |  |
| Q050-112 | P1 | L10 | feature | M | Musical time for hosted effects | 1 |  |
| Q050-113 | P1 | L11 | completion | L | Search plug-ins, devices, and chain presets from the browser | 2 |  |
| Q050-114 | P1 | L12 | feature | L | Open multiple editable projects in tabs | 1 |  |
| Q050-115 | P1 | L12 | feature | L | OMF session import | 1 |  |
| Q050-116 | P1 | L12 | feature | L | AAF export and MXF-backed media | 2 |  |
| Q050-117 | P1 | L03 | fix | L | Apply backend latency to every recording route | 2 |  |
| Q050-118 | P1 | L08 | feature | L | Immersive bed and object mixer foundation | 1 |  |
| Q050-119 | P1 | L08 | completion | M | Multichannel cue, listen, and recorder paths | 1 |  |
| Q050-120 | P1 | L10 | feature | L | Scriptable effect and MIDI devices | 2 |  |
| Q050-121 | P1 | L11 | completion | M | Show usage and unused assets in Project Media | 1 |  |
| Q050-122 | P1 | L14 | completion | L | Screen-reader recording-to-export workflow | 2 |  |
| Q050-123 | P1 | L03 | completion | L | Show take lanes and waveforms in the timeline | 2 |  |
| Q050-124 | P1 | L15 | feature | L | Freeze mixer buses | 1 |  |
| Q050-125 | P1 | L05 | completion | M | Edit clip gain, envelope, and polarity on the waveform | 2 |  |
| Q050-126 | P1 | L12 | completion | L | DAWproject audio clips and media round trip | 1 |  |
| Q050-127 | P1 | L03 | docs | S | Correct comping documentation | 2 |  |
| Q050-128 | P1 | L04 | completion | L | Add relocatable video and SMPTE timecode support | 1 |  |
| Q050-129 | P1 | L08 | completion | M | Follow arranger selection in the mixer | 2 |  |
| Q050-130 | P1 | L04 | completion | M | Edit tempo events and ramps in the arranger | 1 |  |
| Q050-131 | P1 | L05 | docs | S | Benchmark real-model stem separation speed | 1 |  |
| Q050-132 | P1 | L07 | feature | L | MIDI note-effect chain | 1 |  |
| Q050-133 | P1 | L11 | feature | S | Add file URLs to browser and sample-clip drags | 1 |  |
| Q050-134 | P1 | L12 | feature | M | Switch tabs with one active engine and restore each project | 1 |  |
| Q050-135 | P1 | L14 | feature | L | Stage 1: keyboard and switch-accessible step sequencer | 1 |  |
| Q050-136 | P1 | L14 | feature | L | Accessible feedback for supported control surfaces | 1 |  |
| Q050-137 | P1 | L07 | completion | L | MPE multi-note curve editor | 1 |  |
| Q050-138 | P1 | L11 | feature | L | Expand the bundled multisample instrument library | 1 |  |
| Q050-139 | — | — | consolidated | — | See Q050-180; original scope preserved | — | — |
| Q050-140 | P1 | L12 | feature | M | Background autosave with off-thread file writes | 1 |  |
| Q050-141 | P1 | L13 | feature | L | Touch-first layout and large-target mode | 1 |  |
| Q050-142 | P1 | L02 | feature | M | Launcher cell editing and arrangement placement | 1 |  |
| Q050-143 | P1 | L07 | feature | L | Chord-following accompaniment core | 1 |  |
| Q050-144 | P1 | L13 | completion | L | Editable, importable, and exportable workflows | 2 |  |
| Q050-145 | P1 | L14 | docs | S | Document Zene's current free and activation model | 1 |  |
| Q050-146 | P1 | L03 | fix | S | Keep wide and stereo capture blocks aligned | 1 |  |
| Q050-147 | P1 | L03 | completion | M | Give punch recording its own ruler range | 1 |  |
| Q050-148 | P1 | L05 | docs | S | Clarify pitch-preserving warp documentation | 1 |  |
| Q050-149 | P1 | L05 | feature | L | Add six-stem HTDemucs extraction | 1 |  |
| Q050-150 | P1 | L05 | feature | M | Use pitch-preserving practice speed with an A/B loop | 1 |  |
| Q050-151 | P1 | L05 | feature | M | Follow live tempo during practice | 1 |  |
| Q050-152 | P1 | L06 | fix | S | Close the modulation target destruction race | 1 |  |
| Q050-153 | P1 | L06 | feature | L | Edit several automation lanes together | 1 |  |
| Q050-154 | P1 | L07 | completion | M | Live-code panel | 1 |  |
| Q050-155 | P1 | L09 | completion | L | N-channel routing graph and per-channel sends | 1 |  |
| Q050-156 | P1 | L10 | feature | M | Record directly into a sampler | 1 |  |
| Q050-157 | P1 | L10 | fix | S | Plug-in descriptor lifetime check | 1 |  |
| Q050-158 | P1 | L14 | feature | M | Chord-aware keyboard navigation in the MIDI editor | 1 |  |
| Q050-159 | P1 | L14 | docs | S | Explain patterns, arrangement clips, launcher cells, and mixer routing | 1 |  |
| Q050-160 | P1 | L15 | feature | M | Add pre-fader track freeze | 1 |  |
| Q050-161 | P1 | L15 | fix | S | Remove per-period automation ramp locking | 1 |  |
| Q050-162 | P1 | L05 | feature | M | Add synced transient-repeat stretching | 1 |  |
| Q050-163 | P1 | L05 | feature | M | Slice audio into drum-rack pads | 1 |  |
| Q050-164 | P1 | L06 | feature | S | Expose per-sample modulation in the panel | 1 |  |
| Q050-165 | P1 | L07 | completion | M | Tracker effect cells | 1 |  |
| Q050-166 | P1 | L09 | completion | M | Mono and discrete bus formats | 1 |  |
| Q050-167 | P1 | L15 | fix | S | Keep legacy macOS audio wakeups lock-free | 1 |  |
| Q050-168 | P1 | L04 | completion | M | Consolidate MIDI arrangement clips | 1 |  |
| Q050-169 | P1 | L01 | docs | S | Correct hosted-instrument PDC documentation | 1 |  |
| Q050-170 | P1 | L11 | completion | M | Filter browser results by category | 1 |  |
| Q050-171 | P1 | L04 | docs | S | Correct VCA edit-group limitation notes | 1 |  |
| Q050-172 | P1 | L10 | feature | M | Device quick controls on mixer strips | 1 |  |
| Q050-173 | P1 | L11 | completion | M | Show, sort, and edit supported audio metadata | 1 |  |
| Q050-174 | P1 | L01 | feature | M | Low-latency monitoring for latent instruments | 1 |  |
| Q050-175 | P1 | L10 | feature | M | Chorus and phaser stock effects | 1 |  |
| Q050-176 | P1 | L13 | completion | S | Zoom history with back and forward | 1 |  |
| Q050-177 | P1 | L05 | feature | L | Transcribe polyphonic audio to MIDI | 1 |  |
| Q050-178 | P1 | L16 | feature | L | Build a browser remote-control client | 1 |  |
| Q050-179 | P1 | L03 | completion | S | Show remaining record time in the UI | 1 |  |
| Q050-180 | P1 | L04 | completion | L | Support multiple positioned video clips with correct lane and render behavior | 2 | |
| Q050-181 | — | — | consolidated | — | See Q050-253; original scope preserved | — | — |
| Q050-182 | P2 | L05 | completion | L | Edit and audition transient and slice markers | 3 |  |
| Q050-183 | P2 | L15 | feature | L | Switch audio devices live with route and status feedback | 5 |  |
| Q050-184 | P2 | L07 | feature | L | Audio-to-chord analysis | 3 |  |
| Q050-185 | P2 | L15 | docs | L | Write the Linux audio onboarding guide | 4 |  |
| Q050-186 | P2 | L16 | feature | L | Resume media and collaboration state after disconnect | 4 |  |
| Q050-187 | P2 | L10 | feature | L | Host LV2 plug-in UIs with scaling | 3 |  |
| Q050-188 | P2 | L17 | completion | L | Editable mastering proposals | 3 |  |
| Q050-189 | P2 | L05 | feature | M | Build selected-stem submixes | 3 |  |
| Q050-190 | P2 | L15 | feature | L | Offer one-click setup for common controllers | 2 |  |
| Q050-191 | P2 | L03 | feature | L | Add per-device recording offset calibration | 3 |  |
| Q050-192 | P2 | L11 | completion | M | Search sounds by detected key and tempo | 3 |  |
| Q050-193 | P2 | L11 | completion | L | Drag track templates onto tracks and set type defaults | 3 |  |
| Q050-194 | P2 | L10 | feature | M | Scale open plug-in editors | 4 |  |
| Q050-195 | P2 | L15 | completion | L | Complete MIDI transport and MTC sync | 3 |  |
| Q050-196 | P2 | L15 | docs | M | Document PipeWire setup and backend boundaries | 2 |  |
| Q050-197 | P2 | L07 | completion | L | Instrument key names and kit remapping | 2 |  |
| Q050-198 | P2 | L14 | feature | M | Learn list with progress, patcher, and plug-in tours | 3 |  |
| Q050-199 | P2 | L15 | completion | M | Qualify JACK audio through PipeWire | 2 |  |
| Q050-200 | P2 | L15 | feature | L | Package Flatpak support and explain plug-in limits | 2 |  |
| Q050-201 | P2 | L03 | completion | M | Split loop recordings into take lanes | 3 |  |
| Q050-202 | P2 | L14 | feature | M | Migration and accessibility keymap setup | 2 |  |
| Q050-203 | P2 | L16 | feature | L | Add anchored comment threads to shared projects | 2 |  |
| Q050-204 | P2 | L04 | completion | M | Import MIDI and media onto chosen arranger tracks | 2 |  |
| Q050-205 | P2 | L15 | feature | M | Add an opt-in OSC control server | 2 |  |
| Q050-206 | P2 | L05 | completion | L | Add per-clip effect controls and output routing | 2 |  |
| Q050-207 | P2 | L07 | completion | M | Cross-track ghosts and articulation lanes | 2 |  |
| Q050-208 | P2 | L10 | feature | M | Plug-in picker aliases, tags, and recent use | 2 |  |
| Q050-209 | P2 | L10 | feature | M | Hosted instrument MIDI event delivery | 2 |  |
| Q050-210 | P2 | L16 | feature | L | Transfer and hash-check project media between peers | 2 |  |
| Q050-211 | P2 | L07 | completion | M | Step input and recording in the new editor | 2 |  |
| Q050-212 | P2 | L09 | feature | L | Track creation output routing and auto-channel assignment | 2 |  |
| Q050-213 | P2 | L10 | feature | L | Multisample velocity and round-robin mapping | 2 |  |
| Q050-214 | P2 | L10 | feature | M | Plug-in editor gesture undo | 2 |  |
| Q050-215 | P2 | L12 | completion | L | AAF import fidelity and missing-media preflight | 2 |  |
| Q050-216 | P2 | L12 | feature | M | Queue a section-by-track render matrix | 2 |  |
| Q050-217 | P2 | L13 | feature | L | MIDI command bindings and workflow header buttons | 2 |  |
| Q050-218 | P2 | L15 | completion | M | Soak multitrack recording on real audio hardware | 2 |  |
| Q050-219 | P2 | L04 | completion | M | Move arrangement sections across tracks | 2 |  |
| Q050-220 | P2 | L06 | feature | M | Add interactive shape drawing and curve preview | 2 |  |
| Q050-221 | P2 | L07 | feature | L | Seeded melodic pattern tools | 2 |  |
| Q050-222 | P2 | L10 | feature | M | Device-card drag and drop | 2 |  |
| Q050-223 | P2 | L11 | feature | M | Add FFmpeg-backed audio decoding | 2 |  |
| Q050-224 | P2 | L15 | completion | L | Add crash context and per-thread signal stacks | 2 |  |
| Q050-225 | P2 | L16 | feature | L | Attribute peer edits for targeted rollback | 2 |  |
| Q050-226 | P2 | L10 | feature | L | Real-time pitch-correction effect | 1 |  |
| Q050-227 | P2 | L08 | completion | L | Persistent mixer docks and pinned strips | 2 |  |
| Q050-228 | P2 | L12 | feature | M | Portable one-file project handoff bundle | 1 |  |
| Q050-229 | P2 | L04 | completion | M | Link sample and automation clips across tracks | 2 |  |
| Q050-230 | P2 | L07 | feature | L | Mini-notation parser and scheduler | 1 |  |
| Q050-231 | P2 | L08 | feature | L | Cross-track masking report and undoable EQ proposals | 1 |  |
| Q050-232 | P2 | L09 | feature | L | Stream peer audio over Link | 1 |  |
| Q050-233 | P2 | L10 | feature | L | Multi-mono plug-in wrapper | 1 |  |
| Q050-234 | P2 | L12 | feature | M | Compare and transfer project alternatives beside the session | 1 |  |
| Q050-235 | P2 | L14 | feature | L | Touch-first workspace layout | 2 |  |
| Q050-236 | P2 | L17 | feature | L | Navigable lyric transcript lane | 1 |  |
| Q050-237 | P2 | L02 | feature | L | Add overdub and live-loop advancement | 1 |  |
| Q050-238 | P2 | L03 | feature | L | Print external MIDI instruments before mixdown | 1 |  |
| Q050-239 | P2 | L06 | feature | L | Add performance controls for warped clips | 1 |  |
| Q050-240 | P2 | L07 | feature | M | Per-note sample start and reverse | 1 |  |
| Q050-241 | P2 | L07 | feature | M | Pattern speed multiplier | 1 |  |
| Q050-242 | P2 | L07 | completion | M | Pattern recurrence controls | 1 |  |
| Q050-243 | P2 | L07 | completion | M | Pattern audition loop region | 1 |  |
| Q050-244 | P2 | L11 | feature | M | Place browser samples into note editing | 1 |  |
| Q050-245 | P2 | L12 | feature | L | SMPTE and MTC video scoring workflow | 1 |  |
| Q050-246 | P2 | L15 | completion | M | Measure and correct external MIDI output timing | 1 |  |
| Q050-247 | P2 | L15 | docs | S | Document Windows plug-ins on Linux | 1 |  |
| Q050-248 | P2 | L03 | feature | L | Add a quick-record setup flow | 1 |  |
| Q050-249 | P2 | L07 | completion | M | Arranger chord-event editing | 1 |  |
| Q050-250 | P2 | L07 | completion | M | Dock note-editor side panels | 1 |  |
| Q050-251 | P2 | L08 | feature | L | Linked immersive deliverable buses | 1 |  |
| Q050-252 | P2 | L09 | completion | L | External Instrument device | 2 |  |
| Q050-253 | P2 | L10 | feature | L | Play per-clip effect chains with latency compensation | 2 | |
| Q050-254 | P2 | L10 | feature | L | Region effect ranges | 1 |  |
| Q050-255 | P2 | L04 | completion | M | Edit folder summary clip spans | 2 |  |
| Q050-256 | P2 | L05 | feature | L | Group-quantize an edit set from one anchor | 1 |  |
| Q050-257 | P2 | L06 | feature | L | Store parameter automation with MIDI clips | 1 |  |
| Q050-258 | P2 | L06 | feature | L | Overlay automation over arrangement clips | 1 |  |
| Q050-259 | P2 | L06 | feature | L | Add copy/paste and Write on Play automation workflow | 1 |  |
| Q050-260 | P2 | L07 | feature | M | MIDI phrase-channel routing | 1 |  |
| Q050-261 | P2 | L07 | completion | M | Find-and-change condition tools | 1 |  |
| Q050-262 | P2 | L08 | completion | M | Surround-aware send panning | 1 |  |
| Q050-263 | P2 | L10 | feature | M | Per-slice SlicerT controls | 1 |  |
| Q050-264 | P2 | L10 | feature | M | Named bank/program maps for instruments | 1 |  |
| Q050-265 | P2 | L10 | feature | L | Out-of-process VST3 and CLAP hosting | 1 |  |
| Q050-266 | P2 | L10 | feature | L | Out-of-process LV2 and remaining host families | 1 |  |
| Q050-267 | P2 | L10 | feature | L | Zene Rack plug-in module and state bridge | 1 |  |
| Q050-268 | P2 | L11 | feature | M | Save and browse reusable automation and MIDI clips | 1 |  |
| Q050-269 | P2 | L12 | feature | M | Hardware video decoding with software fallback | 1 |  |
| Q050-270 | P2 | L15 | feature | L | Build the expressive pad model and input path | 1 |  |
| Q050-271 | P2 | L16 | feature | L | Connect project revisions and merge review to collaboration | 1 |  |
| Q050-272 | P2 | L10 | completion | M | Stock-device freshness review | 1 |  |
| Q050-273 | P2 | L02 | feature | S | Save a launcher clip to the shared library | 1 |  |
| Q050-274 | P2 | L03 | completion | S | Add touch swipes to comp selection | 1 |  |
| Q050-275 | P2 | L03 | feature | M | Add named alternate clip playlists | 1 |  |
| Q050-276 | P2 | L03 | feature | M | Switch recording inputs live while idle | 1 |  |
| Q050-277 | P2 | L03 | feature | M | Capture an exact number of externally clocked bars | 1 |  |
| Q050-278 | P2 | L03 | feature | M | Resample the master into a sampler track | 1 |  |
| Q050-279 | P2 | L03 | feature | M | Timestamp MIDI input through tempo changes | 1 |  |
| Q050-280 | P2 | L04 | completion | M | Loop automation clips and make loop passes unique | 1 |  |
| Q050-281 | P2 | L04 | docs | S | Correct tempo-map ramp documentation | 1 |  |
| Q050-282 | P2 | L04 | feature | L | Follow live tempo during recording | 1 |  |
| Q050-283 | P2 | L04 | feature | L | Create a neutral configurable hybrid track | 1 |  |
| Q050-284 | P2 | L04 | feature | L | Play audio and MIDI clips on hybrid tracks | 1 |  |
| Q050-285 | P2 | L04 | feature | L | Edit and record audio and MIDI on hybrid tracks | 1 |  |
| Q050-286 | P2 | L04 | feature | M | Tag and comment on tracks and clips | 1 |  |
| Q050-287 | P2 | L04 | completion | S | Pin any track in the arranger | 1 |  |
| Q050-288 | P2 | L05 | docs | S | Correct Sample Editor capability docs | 1 |  |
| Q050-289 | P2 | L05 | feature | M | Estimate stem jobs and use available GPU providers | 1 |  |
| Q050-290 | P2 | L05 | feature | L | Play granular or spectral samples with per-slice modulation | 1 |  |
| Q050-291 | P2 | L06 | feature | L | Convert MIDI CC lanes and arrangement automation | 1 |  |
| Q050-292 | P2 | L06 | feature | M | Copy automation from reference projects | 1 |  |
| Q050-293 | P2 | L07 | completion | S | Selection-based MIDI transformations | 1 |  |
| Q050-294 | P2 | L07 | feature | L | Melodic step-sequencer view | 1 |  |
| Q050-295 | P2 | L07 | completion | S | Pitch-curve vibrato controls | 1 |  |
| Q050-296 | P2 | L08 | completion | L | End-to-end surround processing and delivery | 1 |  |
| Q050-297 | P2 | L08 | completion | M | Bulk send and VCA assignment for selected tracks | 1 |  |
| Q050-298 | P2 | L08 | feature | L | Binaural monitoring for surround masters | 1 |  |
| Q050-299 | P2 | L08 | completion | S | Selectable PDC modes and transport control | 1 |  |
| Q050-300 | P2 | L08 | completion | L | Offline stem EQ proposal for a stereo master | 1 |  |
| Q050-301 | P2 | L08 | completion | S | Alphabetical auxiliary-bus picker | 1 |  |
| Q050-302 | P2 | L08 | completion | M | Inspector focus channel strip | 1 |  |
| Q050-303 | P2 | L09 | completion | S | Bulk sidechain routing from selected strips | 1 |  |
| Q050-304 | P2 | L09 | completion | M | Latency-align unconnected plug-in channels | 1 |  |
| Q050-305 | P2 | L09 | fix | S | Verify and synchronize live pin edits | 1 |  |
| Q050-306 | P2 | L09 | fix | S | Make hardware ping cancellation reset-safe | 1 |  |
| Q050-307 | P2 | L09 | completion | L | Stage 2: Multichannel recording and surround completion | 1 |  |
| Q050-308 | P2 | L09 | docs | S | Correct MIDI Clock menu documentation | 1 |  |
| Q050-309 | P2 | L10 | feature | L | Modular instrument performance panel | 1 |  |
| Q050-310 | P2 | L10 | feature | L | Versioned native plug-in SDK | 1 |  |
| Q050-311 | P2 | L10 | feature | M | Built-in synth pressure and timbre | 1 |  |
| Q050-312 | P2 | L10 | feature | M | Deactivate tracks and devices | 1 |  |
| Q050-313 | P2 | L10 | fix | S | Zyn MIDI mutex latency fix | 1 |  |
| Q050-314 | P2 | L10 | fix | S | WASM offline-render memory pointer fix | 1 |  |
| Q050-315 | P2 | L10 | feature | L | CV and gate output device | 1 |  |
| Q050-316 | P2 | L11 | feature | L | Package an optional tagged factory loop library | 1 |  |
| Q050-317 | P2 | L12 | feature | L | Sequence songs in a mastering project document | 1 |  |
| Q050-318 | P2 | L13 | feature | M | Starter workflow library | 1 |  |
| Q050-319 | P2 | L13 | feature | M | Beginner and expert interface presets | 1 |  |
| Q050-320 | P2 | L14 | feature | S | Announce launched and stopped clips and scenes | 1 |  |
| Q050-321 | P2 | L14 | feature | L | Touch keyboard and drum-pad surfaces | 1 |  |
| Q050-322 | P2 | L14 | feature | M | Open track and ruler context menus from the keyboard | 1 |  |
| Q050-323 | P2 | L14 | docs | L | Task-indexed quick-start and area reference | 1 |  |
| Q050-324 | P2 | L14 | docs | S | Add an accessibility feedback route | 1 |  |
| Q050-325 | P2 | L14 | docs | M | Correct stale socket-only and no-interface claims | 1 |  |
| Q050-326 | P2 | L15 | feature | L | Show safe-start recovery and Windows crash reports | 2 |  |
| Q050-327 | P2 | L15 | feature | M | Freeze selected tracks as one parallel task | 1 |  |
| Q050-328 | P2 | L15 | feature | L | Add PortAudio multichannel and verify Linux device paths | 1 |  |
| Q050-329 | P2 | L15 | feature | L | Add multichannel SoundIo and SDL routing | 1 |  |
| Q050-330 | P2 | L15 | feature | L | Add Windows ASIO device selection | 1 |  |
| Q050-331 | P2 | L15 | feature | M | Persist controller bindings with presets | 1 |  |
| Q050-332 | P2 | L16 | feature | L | Support up to 50 collaboration peers | 1 |  |
| Q050-333 | P2 | L16 | feature | L | Add a self-hosted collaboration backend | 1 |  |
| Q050-334 | P2 | L17 | feature | L | Public live-session TypeScript SDK | 1 |  |
| Q050-335 | P2 | L11 | feature | M | Add starter projects, patterns, and tutorials | 1 |  |
| Q050-336 | P2 | L15 | docs | S | Correct ALSA capture support documentation | 1 |  |
| Q050-337 | P2 | L15 | completion | M | Send MCU strip LCD feedback | 1 |  |
| Q050-338 | P2 | L08 | completion | M | Persistent gang mode for selected channels | 1 |  |
| Q050-339 | P2 | L08 | completion | M | Track delay for audio tracks and mixer channels | 1 |  |
| Q050-340 | P2 | L08 | completion | M | Input meters for capture channels and armed tracks | 1 |  |
| Q050-341 | P2 | L08 | completion | S | Crossfader slider in the mixer | 1 |  |
| Q050-342 | P2 | L11 | completion | L | Build and cache local timbre embeddings | 1 |  |
| Q050-343 | P2 | L08 | docs | S | Document the sample generator menu route | 1 |  |
| Q050-344 | P3 | L10 | docs | S | Record ARA and pitch-editing deferral | 11 |  |
| Q050-345 | P3 | L10 | feature | L | WASM effect inputs: samples, sidechain, and MIDI | 3 |  |
| Q050-346 | P3 | L07 | completion | M | Named stacked MIDI controller lanes | 3 |  |
| Q050-347 | P3 | L17 | feature | M | MCP command profiles | 2 |  |
| Q050-348 | P3 | L13 | feature | M | Track color defaults and folder inheritance | 2 |  |
| Q050-349 | P3 | L05 | feature | M | Copy and paste sample ranges and make a new clip | 2 |  |
| Q050-350 | P3 | L10 | feature | M | Plug-in editor and instance diagnostics | 2 |  |
| Q050-351 | P3 | L07 | feature | M | Same-key sustain and retrigger handling | 2 |  |
| Q050-352 | P3 | L10 | feature | L | Scripted instrument and MIDI module types | 2 |  |
| Q050-353 | P3 | L05 | fix | M | Apply zero-crossing snap to all cut gestures | 2 |  |
| Q050-354 | P3 | L07 | completion | M | Note trim and retrograde tools | 2 |  |
| Q050-355 | P3 | L15 | feature | L | Add high-resolution MIDI controller mappings | 2 |  |
| Q050-356 | P3 | L15 | docs | S | Document Zene’s MIDI 2.0 support position | 1 |  |
| Q050-357 | P3 | L15 | completion | M | Add large-project arranger and engine benchmarks | 2 |  |
| Q050-358 | P3 | L10 | feature | L | WebCLAP runtime bridge | 1 |  |
| Q050-359 | P3 | L10 | fix | S | Carla Rack VST3 opening stability | 1 |  |
| Q050-360 | P3 | L09 | feature | M | CV/Gate output instrument | 1 |  |
| Q050-361 | P3 | L13 | feature | M | Resizable Song Editor sidebar | 1 |  |
| Q050-362 | P3 | L06 | docs | S | Correct sample-accuracy limitation docs | 1 |  |
| Q050-363 | P3 | L07 | feature | M | MIDI clipboard interchange | 1 |  |
| Q050-364 | P3 | L10 | feature | L | Sandboxed third-party device SDK | 1 |  |
| Q050-365 | P3 | L10 | feature | M | Rack macros for any automatable target | 1 |  |
| Q050-366 | P3 | L10 | feature | M | Rack chain-select crossfades | 1 |  |
| Q050-367 | P3 | L10 | fix | S | VST2 editor teardown stability | 1 |  |
| Q050-368 | P3 | L12 | feature | M | Audio export metadata, cover art, and chapter tags | 2 |  |
| Q050-369 | P3 | L14 | feature | S | Add a List button to the automation editor | 1 |  |
| Q050-370 | P3 | L14 | feature | M | Keep command results and show list progress | 1 |  |
| Q050-371 | P3 | L05 | feature | M | Detect and repair clicks in sample ranges | 1 |  |
| Q050-372 | P3 | L08 | completion | S | Optional master oscilloscope in the shell | 1 |  |
| Q050-373 | P3 | L09 | feature | M | Route MIDI effects between instrument tracks | 1 |  |
| Q050-374 | P3 | L10 | feature | L | Custom script-device controls | 1 |  |
| Q050-375 | P3 | L10 | feature | L | Dynamic EQ modes and band solo | 2 |  |
| Q050-376 | P3 | L10 | feature | M | SoundFont sample extraction | 1 |  |
| Q050-377 | P3 | L12 | feature | L | Export a selected video range with muxed audio | 1 |  |
| Q050-378 | P3 | L12 | feature | M | Preview projects from the Start Hub | 1 |  |
| Q050-379 | P3 | L12 | feature | S | Use the platform Projects folder as the new-project default | 1 |  |
| Q050-380 | P3 | L12 | feature | S | Split and combine multichannel audio files | 1 |  |
| Q050-381 | P3 | L16 | feature | L | Elect a successor when the collaboration host leaves | 1 |  |
| Q050-382 | P3 | L05 | feature | L | Add spectrogram view and time-frequency selection | 1 |  |
| Q050-383 | P3 | L06 | feature | L | Extend per-note modulation to built-in instruments | 1 |  |
| Q050-384 | P3 | L07 | feature | M | Playback MIDI parameter strip | 1 |  |
| Q050-385 | P3 | L10 | feature | L | Multi-system chip instrument | 1 |  |
| Q050-386 | P3 | L10 | feature | L | Chip and modular tracker editor | 1 |  |
| Q050-387 | P3 | L12 | feature | M | Import project tracks with mixer setups | 1 |  |
| Q050-388 | P3 | L14 | docs | S | Update Safe Start recovery guidance | 1 |  |
| Q050-389 | P3 | L15 | feature | L | Make Qt 6 the default release build | 1 |  |
| Q050-390 | P3 | L17 | feature | M | Deterministic musical intents | 1 |  |
| Q050-391 | P3 | L08 | fix | S | Verify plug-in delay compensation in instrument racks | 1 |  |
| Q050-392 | P3 | L02 | feature | M | Quantize arrangement marker jumps | 1 |  |
| Q050-393 | P3 | L02 | feature | S | Manage scenes from the launcher grid | 1 |  |
| Q050-394 | P3 | L03 | completion | M | Preserve comp slip for warped takes | 1 |  |
| Q050-395 | P3 | L03 | feature | S | Save takes under a project-relative recording folder | 1 |  |
| Q050-396 | P3 | L04 | feature | S | Resize selected arrangement clips together | 1 |  |
| Q050-397 | P3 | L04 | feature | S | Set new clip length from the snap grid | 1 |  |
| Q050-398 | P3 | L04 | feature | S | Collapse and restore individual track rows | 1 |  |
| Q050-399 | P3 | L04 | feature | L | Keep timeline landmarks through ripple edits and export chapters | 1 |  |
| Q050-400 | P3 | L04 | feature | L | Store named arrangement variants in one project | 1 |  |
| Q050-401 | P3 | L04 | feature | S | Select all clips on the active track | 1 |  |
| Q050-402 | P3 | L05 | feature | L | Extreme offline time stretching with Paulstretch | 1 |  |
| Q050-403 | P3 | L05 | completion | L | Edit multichannel clips without dropping channels | 1 |  |
| Q050-404 | P3 | L06 | feature | M | Let modulators drive rack macros | 1 |  |
| Q050-405 | P3 | L06 | feature | M | Automate effect bypass state | 1 |  |
| Q050-406 | P3 | L06 | feature | M | Add a one-shot volume-envelope effect | 1 |  |
| Q050-407 | P3 | L06 | feature | S | Label the automation editor in song-relative bars | 1 |  |
| Q050-408 | P3 | L06 | feature | S | Use consistent parameter value formatting | 1 |  |
| Q050-409 | P3 | L07 | feature | L | Scriptable MIDI transform device | 1 |  |
| Q050-410 | P3 | L07 | feature | M | Negative-delay articulations | 1 |  |
| Q050-411 | P3 | L08 | feature | L | Arrangement analyzer lane for loudness and speech intelligibility | 1 |  |
| Q050-412 | P3 | L08 | completion | M | Live spectrogram and per-channel loudness analysis | 1 |  |
| Q050-413 | P3 | L08 | feature | M | Non-invasive chain-slot signal probe | 1 |  |
| Q050-414 | P3 | L08 | feature | L | Higher-order Ambisonics bus format and encoder | 1 |  |
| Q050-415 | P3 | L09 | completion | L | Many-to-many channel assignment matrix | 1 |  |
| Q050-416 | P3 | L09 | completion | M | Measure and compensate external-instrument latency | 1 |  |
| Q050-417 | P3 | L09 | completion | M | Save sidechain routing with effect presets | 1 |  |
| Q050-418 | P3 | L09 | completion | S | Global and per-port MIDI output offsets | 1 |  |
| Q050-419 | P3 | L09 | completion | S | Ganged send level adjustment | 1 |  |
| Q050-420 | P3 | L09 | completion | M | Machine presets for hardware insert routing | 1 |  |
| Q050-421 | P3 | L10 | feature | M | Pitch-shifter algorithm and quality controls | 1 |  |
| Q050-422 | P3 | L10 | feature | L | Low-latency convolution mode | 1 |  |
| Q050-423 | P3 | L10 | completion | M | Speech-processing chain preset | 1 |  |
| Q050-424 | P3 | L10 | feature | L | Rule-based guitar accompaniment player | 1 |  |
| Q050-425 | P3 | L10 | feature | L | Phrase sampler | 1 |  |
| Q050-426 | P3 | L10 | feature | L | Optional acoustic drum instrument | 1 |  |
| Q050-427 | P3 | L10 | completion | M | Stock instrument loudness guidance | 1 |  |
| Q050-428 | P3 | L10 | feature | M | SoundFont audition mode | 1 |  |
| Q050-429 | P3 | L10 | feature | S | Keyboard preset audition | 1 |  |
| Q050-430 | P3 | L10 | feature | M | Independent synth and filter envelopes | 1 |  |
| Q050-431 | P3 | L10 | feature | M | Embedded Zyn editor mode | 1 |  |
| Q050-432 | P3 | L10 | feature | S | MIDI output bank select | 1 |  |
| Q050-433 | P3 | L11 | feature | S | Carry effect-chain presets with project handoff | 1 |  |
| Q050-434 | P3 | L12 | feature | S | Choose whether MIDI import applies tempo and meter | 1 |  |
| Q050-435 | P3 | L12 | feature | L | Broadcast MXF ingest and ADR overlays | 1 |  |
| Q050-436 | P3 | L12 | feature | S | Reorder render queue jobs | 1 |  |
| Q050-437 | P3 | L12 | feature | M | Restore deleted tracks without undoing later work | 1 |  |
| Q050-438 | P3 | L12 | docs | S | Document the existing revisions UI | 1 |  |
| Q050-439 | P3 | L12 | feature | M | Inspect project settings, assets, metadata, and cleanup | 1 |  |
| Q050-440 | P3 | L12 | feature | S | Search project notes in the library | 1 |  |
| Q050-441 | P3 | L12 | feature | M | Batch rename selected tracks and clips | 1 |  |
| Q050-442 | P3 | L13 | fix | S | High-contrast track movement grip | 1 |  |
| Q050-443 | P3 | L14 | feature | M | Touch gestures for the patcher | 1 |  |
| Q050-444 | P3 | L14 | feature | L | Stage 2: gaze and switch input for sequencer and melody | 1 |  |
| Q050-445 | P3 | L15 | feature | M | Provide a native Linux package for bridge users | 1 |  |
| Q050-446 | P3 | L15 | feature | M | Add named hardware synth patch maps | 1 |  |
| Q050-447 | P3 | L15 | completion | M | Provision ONNX Runtime in release jobs | 1 |  |
| Q050-448 | P3 | L15 | feature | M | Measure MIDI round-trip latency | 1 |  |
| Q050-449 | P3 | L15 | feature | L | Add project-rate audio conversion | 1 |  |
| Q050-450 | P3 | L15 | feature | S | Bind transport actions to MIDI controllers | 1 |  |
| Q050-451 | P3 | L15 | feature | M | Show live JACK and PipeWire graph connections | 1 |  |
| Q050-452 | P3 | L15 | feature | M | MIDI-map Beat+Bassline pattern steps | 1 |  |
| Q050-453 | P3 | L16 | feature | M | Add room chat and presence indicators | 1 |  |
| Q050-454 | P3 | L04 | completion | S | Honor zero-crossing snap for knife splits | 1 |  |
| Q050-455 | P3 | L06 | feature | M | Complete automation write-pass helpers | 1 |  |
| Q050-456 | P3 | L07 | completion | M | Ornaments and flams | 1 |  |
| Q050-457 | P3 | L10 | feature | M | Built-in mixer-strip dynamics | 1 |  |
| Q050-458 | P3 | L04 | completion | M | Split audio by level while retaining segments | 1 |  |
| Q050-459 | P3 | L04 | feature | M | Save custom toolbars for commands and workflows | 1 |  |
| Q050-460 | P3 | L05 | feature | L | Repair selected spectral regions | 1 |  |
| Q050-461 | P3 | L07 | completion | M | Project groove amount control | 1 |  |
| Q050-462 | P3 | L07 | feature | L | SysEx and program-change lanes | 1 |  |
| Q050-463 | P3 | L07 | feature | M | Live-input note repeater | 1 |  |
| Q050-464 | P3 | L10 | feature | L | Formula and script modulators | 1 |  |
| Q050-465 | P3 | L10 | feature | L | Stock spectral effects | 1 |  |
| Q050-466 | P3 | L10 | feature | M | Hosted-instrument compatibility options | 1 |  |
| Q050-467 | P3 | L01 | feature | M | Compensate recorded automation points | 1 |  |
| Q050-468 | P3 | L04 | completion | S | Bind Tab navigation to transients | 1 |  |
| Q050-469 | P3 | L09 | completion | L | Receive note and MIDI events inside a chain | 1 |  |
| Q050-470 | P3 | L09 | completion | S | Show calibrated reference on meter views | 1 |  |
| Q050-471 | P3 | L10 | feature | L | Per-clip effect-chain storage | 1 |  |
| Q050-472 | P3 | L10 | feature | M | Per-device oversampling controls | 1 |  |
| Q050-473 | P3 | L10 | feature | S | Collapse device and rack sections | 1 |  |
| Q050-474 | P3 | L10 | feature | L | Recorded insert-take editor | 1 |  |
| Q050-475 | P3 | L12 | feature | L | Background jobs appear and progress in task.list | 1 |  |
| Q050-476 | P3 | L13 | completion | S | Crosshair guides across editing canvases | 1 |  |

## Q050-001 DAWproject DEFLATE and tolerant import reporting

**P0 · L12 projects, files, import/export, interchange, recovery · completion · M** · existing lane `importers-tolerant`

Goal: Open valid DEFLATE-compressed DAWproject files and return a complete report for corrupt or unsupported entries.

Why: The input marks DAWproject DEFLATE as a high-demand broken/missing importer case for one DAW, and the owner-decided importer-tolerance lane covers this parser/reporting work.

Acceptance:
- Import STORE and DEFLATE fixtures with bounded decompression and verify equivalent project state.
- Import corrupt, oversized, and unsupported-entry fixtures and verify the importer lists every deviation without crashing or silently dropping content.
Items:
  - `R-interop-1` DAWproject DEFLATE container support - missing (src/core/DawProjectZip.cpp:495-509 refuses DEFLATE entries; docs/DAWPROJECT-INTERCHANGE.md:124-128 documents STORE-only handling.); implement: The reader accepts stored ZIP entries but rejects valid DEFLATE-compressed DAWproject files.

## Q050-002 Move recorder setup off the audio thread

**P0 · L03 recording, takes, comping, inputs · fix · M** · existing lane `alloc-probe-fix`

Goal: Starting an armed recording does not allocate memory or create its drain thread from the audio render callback.

Why: The row cites accumulator allocation and thread startup from SampleTrack::play (medium demand, 1 DAW), a confirmed realtime safety gap.

Acceptance:
- The fixed AllocationProbe counts an allocation injected from a non-test translation unit.
- A recorder-start regression exercises the callback path and proves accumulator and drain-thread setup happen before playback; recording still produces the expected take.
Items:
  - `R-recording-1` Recording allocates and starts a thread from the render path - broken (src/tracks/SampleTrack.cpp:201-207 (new SampleRecordHandle in SampleTrack::play); src/core/SampleRecordHandle.cpp:45-56 (make_unique accumulator; its comment says the constructor runs on the audio thread); include/SampleRecordAccumulator.h:89-96 (allocates a 16384-frame ring and starts std::thread)); fix: Starting a clip recording allocates a ring buffer and spawns a drain thread on the audio thread, which can glitch playback.

## Q050-003 VST3 host claims and output-bus documentation

**P0 · L10 devices, instruments, effects, plug-in hosting · docs · S** · existing lane `effect-multiout`

Goal: Users can find accurate VST3 editor, output-bus, and instrument-latency support information.

Why: The README says the editor does not open and no multi-out exists, while the host has an editor and the owner-decided effect-multiout lane closes the output gap.

Acceptance:
- Update the README VST3 paragraph after output-bus routing lands and retain the third-party-untested caveat.
- Check the editor and instrument-latency wording against the merged host behavior.
Items:
  - `R-platform-4` VST3 hosting remains a repeated user request - have (plugin.list; docs/KNOWN-LIMITATIONS.md:159-167,177-181 (VST3 effects load, editor opens via plugin.editor_open); README.md:62-66 says the editor does not open and there is no multi-out or instrument latency compensation); docs: VST3 effects and instruments are hosted, but the README still says the plug-in editor never opens and that multi-out and instrument PDC are absent, which the limitations page and later lanes contradict.

## Q050-004 Label collaboration as experimental and document its limits

**P0 · L16 collaboration and sharing · completion · S** · existing lane `collab-experimental`

Goal: Users can see collaboration's experimental status, choose to start it deliberately, and understand its LAN and NAT limits before sharing.

Why: Low demand across 1 DAW, but the manual at data/manual/index.html:194 promises a phone client that KNOWN-LIMITATIONS.md:2392 says is absent, and the owner made experimental/off-by-default handling a P0 lane.

Acceptance:
- A fresh collaboration-enabled launch reports collab.get_state mode idle and opens no listener before explicit host or join; the panel shows Experimental.
- The collab command descriptions and docs identify collaboration as experimental and opt-in, and explain that carrier-grade or double NAT blocks inbound peer-to-peer connections without a relay.
- The manual describes the remote role as a Zene peer and removes the unsupported phone-browser promise while no browser client exists.
- The docs retain the current limits on media transfer and reconnect rather than implying they are available.
Items:
  - `R-collaboration-43` Phone remote collaboration client - partial (data/manual/index.html:194; docs/KNOWN-LIMITATIONS.md:2392; command ids collab.invite and collab.join); docs: The remote role exposes transport, markers and faders to a peer, but no phone or HTML client exists despite the manual's phone claim.

## Q050-005 Granular pitch-shifter buffer resize fix

**P0 · L10 devices, instruments, effects, plug-in hosting · fix · S** · existing lane `alloc-probe-fix`

Goal: Users can change sample rate without the audio callback resizing or reserving granular buffers.

Why: GranularPitchShifter resizes and reserves buffers from processImpl, a broken real-time path reported at high demand.

Acceptance:
- Change sample rate while processing with allocation and timing probes enabled; the callback must not allocate or resize.
- Verify the prepared buffers are swapped safely and the pitch-shift render remains correct.
Items:
  - `R-performance-30` Granular buffer resize timing - broken (plugins/GranularPitchShifter/GranularPitchShifterEffect.cpp:257-260 (changeSampleRate from processImpl), :266-289 (ring buffer resize, full clear, grains reserve)); fix: A sample-rate or range change resizes, clears and reserves the grain buffers on the audio thread.

## Q050-006 Restore realtime-safe Rubber Band on Linux ARM

**P0 · L05 audio editing, warping, stretching, fades · fix · M** · existing lane `alloc-probe-fix`

Goal: Linux ARM users can select Rubber Band stretching without allocating on the audio thread.

Why: Rubber Band is disabled on Linux ARM because its current path allocates on the audio thread; this is a broken feature at medium demand from 1 DAW.

Acceptance:
- Build the aarch64 configuration with Rubber Band enabled and run SampleClipRubberBandTest with zero audio-thread allocations.
- Render a fixed stretch fixture and compare timing and audio against the established Rubber Band reference.
Items:
  - `R-platform-40` Rubber Band stretch is unavailable on Linux ARM - partial (docs/KNOWN-LIMITATIONS.md:387-391 (aarch64 built with -DWANT_RUBBERBAND=OFF; Ubuntu librubberband allocates on the audio thread)); fix: Linux aarch64 users cannot choose Rubber Band because the system library allocates on the audio thread.

## Q050-007 Opt-in v2 project save/load

**P0 · L12 projects, files, import/export, interchange, recovery · completion · M** · existing lane `container-v2-optin`

Goal: Choose v2 container format for a project while legacy single-blob .mmpz remains the default and both formats reopen.

Why: The source row records medium demand from one DAW, and the owner explicitly decided to ship v2 save as an opt-in lane, which makes it P0.

Acceptance:
- Save through a visible 'Save as v2 container' choice and a project/global setting; verify ordinary Save still writes legacy .mmpz by default.
- Open legacy and v2 fixtures and compare project state after save/load; show the larger-file and older-build compatibility note.
- Exercise the same opt-in through project.save and verify project.open auto-detects both formats.
Items:
  - `R-workflow-147` Folder projects and ZIP archive - partial (project.collect_media and project.handoff (portable folder); src/core/ControlCommandsProjectArchive.cpp:170 (portable bundle out of that verb)); complete: Zene can gather a project and its media into a portable folder, but it can't pack it into a single archive.

## Q050-008 Repair freeze-region audio and recovery

**P0 · L15 platform: performance, stability, Linux audio, install, hardware controllers · fix · L**

Goal: Frozen regions render at the right tempo without duplicate audio and recover safely when a take file moves.

Why: Three freeze-region behaviors are marked broken: overlap duplication, constant-tempo frame offsets and missing audio after file moves, at medium demand.

Acceptance:
- A render test proves overlapping source clips are silent only inside the frozen region and resume outside it.
- A tempo-map test proves region start/end ticks map to the same frames in rendering and playback.
- Moving or removing a frozen take enters missing-assets/relink and offers an undoable unfreeze; regression tests cover all three cases.
Items:
  - `R-performance-1` Freeze-region overlap duplication - broken (src/core/ControlCommandsFreeze.cpp:98-120 (overlapping clips reported, not handled); docs/KNOWN-LIMITATIONS.md:804-806); fix: A clip starting before a region freeze and running into it still plays inside the region alongside the frozen render, so the overlap sounds twice.
  - `R-performance-2` Freeze-region tempo-map offsets - broken (src/core/Track.cpp:1191-1193 (frozen take offset uses Engine::framesPerTick, the constant project tempo); docs/KNOWN-LIMITATIONS.md:807-809); fix: The frozen take's frame offset is derived from the project tempo, so under a tempo map a region freeze plays from the wrong position.
  - `R-performance-3` Missing frozen audio after file moves - broken (src/core/Track.cpp:1133-1155 (moved take stays frozen, buffer null); src/core/ControlCommandsTransport.cpp:89 (frozen_audio_ready); docs/KNOWN-LIMITATIONS.md:811-812); fix: A frozen track whose WAV moved stays frozen with nothing to play; it is only reported, not relinked or recovered.

## Q050-009 Preset-based range and section export in the GUI

**P0 · L12 projects, files, import/export, interchange, recovery · fix · L**

Goal: Choose render presets in the export dialog, render a selected range, and export named sections as separate files.

Why: The group includes a broken preset/export-dialog path and socket-only range workflows, with medium demand across one or two DAWs.

Acceptance:
- Create a saved preset from the export dialog, reopen it, select it, and verify the rendered file format, rate, depth, and channel settings match.
- Invoke Render Selection from the range menu and verify render.render receives the exact start_ticks/end_ticks.
- Export multiple named sections and verify one correctly named, bounded file per section; verify stem export follows the selected export rate and bit depth.
Items:
  - `R-editing-90` Range markers and batch segment export - partial (section.add (named, coloured ranges); render.render start_ticks/end_ticks; src/gui/modals/ExportProjectDialog.cpp:77 export between loop markers); complete: Named coloured ranges and range export exist, but there is no batch export of chosen sections to separate files.
  - `R-workflow-5` Render presets do not control the export dialog - broken (src/gui/modals/ExportProjectDialog.cpp:330-332 keeps its own settings, no preset reference; src/gui/RenderPresetMenu.cpp:129 only Render Song with Preset uses render.render; docs/KNOWN-LIMITATIONS.md:835-836); fix: The export dialog ignores the applied render preset and has no preset list, and a ranged render (render.render start_ticks/end_ticks) has no interface.
  - `R-workflow-124` Ranged render and stem export are not GUI workflows - partial (src/gui/MainWindow.cpp:342 (Export Tracks...); src/gui/MenuBarExtras.cpp:136 (range.bounce); src/core/ControlCommandsRenderStems.cpp:83 (kStemSampleRate = 44100)); complete: Stem export and range bounce are in the GUI, but stems still render at a fixed 44.1 kHz and no menu item renders the selection to a file.
  - `R-workflow-160` Multisession editing and custom format export - partial (project.switch with keep_as_reference and reference.copy_* (src/gui/LibraryCommandMenus.cpp:162); src/core/ControlCommandsRenderStems.cpp:83 (stems fixed at 44.1 kHz)); complete: Two projects can be open (one as a reference) and copied between, but stem and range exports aren't configurable (fixed rate, no range render in the GUI).
  - `R-workflow-226` Render a chosen timeline range - partial (render.render start_ticks/end_ticks; render.queue_add range; range.bounce in GUI (src/gui/MenuBarExtras.cpp:136); src/gui/modals/ExportProjectDialog.cpp:330 (loop-markers export only); docs/KNOWN-LIMITATIONS.md:835-846); complete: A range can be bounced to a track and the export dialog can render between loop markers, but there is no 'render the selected range to a file' action.

## Q050-010 Move long-running jobs off the interface thread

**P0 · L15 platform: performance, stability, Linux audio, install, hardware controllers · fix · L**

Goal: Plug-in scans, renders, stem jobs and peak analysis run in the background with progress and cancellation.

Why: The queue marks long operations as blocking and identifies stem resampling on the GUI thread; demand is medium and the interface becomes unresponsive.

Acceptance:
- Control tests start, inspect, cancel and complete plug-in scan, render, stem and browser-peak tasks through app.tasks.
- GUI responsiveness tests prove the window remains interactive during each task; stem resampling begins on the worker rather than before submit on the GUI thread.
- Cancellation leaves project state and temporary files consistent; docs no longer describe these operations as synchronous.
Items:
  - `R-performance-34` Background task progress and cancel - partial (task.list, task.cancel, task.show; docs/KNOWN-LIMITATIONS.md:2463 ('The plug-in scan, render.render, render.stems and browser.peaks stay synchronous and are not in the task area.')); complete: The task area covers queued renders, conversions, stem splits and indexing, but plug-in scans, direct renders, stem exports and peak generation still block and are not cancellable there.
  - `R-ui-17` Plugin scan and long-running commands block the interface - broken (docs/KNOWN-LIMITATIONS.md:2458,2463 (plug-in scan, render.render, render.stems, browser.peaks synchronous; lists show a busy cursor)); fix: Long operations block the interface and do not appear in the task area.
  - `R-ui-18` Resampling a clip runs synchronously on the GUI thread - broken (src/gui/StemSplitController.cpp:159-160 (resampleBuffer on the GUI thread before m_manager.submit)); fix: A long clip at another rate freezes the interface while it is resampled.

## Q050-011 Keep accessible target sizes after knob updates

**P0 · L14 help, onboarding, accessibility · fix · S**

Goal: Keep knobs and other controls at their accessible-mode target size after their own sizing changes.

Why: The accessible-mode target reset is documented as broken (R-accessibility-7, score 15; 3 DAWs), blocking an existing accessibility setting.

Acceptance:
- With accessible mode enabled, change a knob's fixed size or size policy and verify its interactive target remains at the configured minimum.
- Add and run a regression test for the resize/reset sequence that currently loses the larger target.
Items:
  - `R-accessibility-7` Accessible target sizes persist after control updates - broken (docs/KNOWN-LIMITATIONS.md:2464 (accessible mode's larger targets are not re-applied if a knob resets its fixed size)); fix: Accessible mode's enlarged targets are lost when a knob later resets its own fixed size.
  - `R-accessibility-13` Low-vision scaling and legible controls - partial (window.appearance scale/high_contrast/accessible; docs/KNOWN-LIMITATIONS.md:2464); fix: Zoom, high contrast and larger targets exist, but larger targets are lost when a knob resets its size.
  - `R-effects-16` Adaptive input for plug-in controls - partial (window.appearance accessible mode (larger targets); docs/KNOWN-LIMITATIONS.md:2464 (larger targets are not re-applied when a knob resets its size)); complete: Accessible mode enlarges targets and controls are keyboard-operable, but a knob that resets its own size loses the larger target.

## Q050-012 Replicate common GUI edits through collaboration

**P0 · L16 collaboration and sharing · feature · L**

Goal: Common arranger and editor gestures made by either participant arrive as ordered shared edits instead of triggering a whole-project resync.

Why: Demand ranges from medium to high across 1-2 DAWs per item, and KNOWN-LIMITATIONS.md:2385-2386 plus ControlCommandsCollab.cpp:181-185 show unsupported GUI edits resync or revert.

Depends on: Label collaboration as experimental and document its limits
Acceptance:
- A two-instance collaboration test performs representative direct arranger, clip, and note-editor gestures on host and peer and verifies identical project state.
- Those gestures appear in the ordered collaboration event stream and do not schedule an untracked-edit snapshot resync or revert on the peer.
- The test exercises the GUI action path and the agent command path against the same resulting state.
Items:
  - `R-collaboration-31` Fender workspaces are not proven live co-editing - partial (docs/KNOWN-LIMITATIONS.md:2385-2386; src/gui/CollabPanel.cpp:154-163); complete: The peer workflow shares registered edits and presence, but unsupported GUI gestures are not applied live on peers.
  - `R-collaboration-29` Amped Studio shared links create separate copies - partial (docs/KNOWN-LIMITATIONS.md:2385-2386; src/core/ControlCommandsCollab.cpp:181-185); complete: Zene supports a live host-ordered command stream, but direct interface edits still resync or revert on peers.
  - `R-collaboration-4` BandLab live collaboration removal - partial (docs/KNOWN-LIMITATIONS.md:2385-2386; src/core/ControlCommandsCollab.cpp:181-185); complete: Live command-based editing exists, but the documented GUI edit gaps can still make peer changes revert.

## Q050-013 Complete audio-cell playback across track types

**P0 · L02 launcher / session clips / performance · fix · L**

Goal: Launch audio cells on sample and instrument columns and have their stored playback settings affect the rendered sound.

Why: The behavior is broken for instrument columns and incomplete for all audio cells; demand is high across 1 DAW, and current docs confirm silence on instrument tracks plus ignored playback controls and warping.

Acceptance:
- Render a known WAV from launcher cells on both sample and instrument columns; each produces non-silent PCM through its owning track.
- Set gain, transpose, detune, loop region and tempo warping on an audio cell; compare rendered level, pitch, loop boundaries and duration against expected PCM at more than one tempo.
- An audio-thread regression test exercises both track types and confirms zero allocations during playback with the fixed allocation probe.
Items:
  - `R-editing-10` Audio cells still silence instrument-track columns - broken (docs/KNOWN-LIMITATIONS.md:2580; docs/KNOWN-LIMITATIONS.md:2583); fix: An audio cell on an instrument track still silences that launcher column even though audio cells play on sample tracks.
  - `R-editing-19` Session slot playback controls do not reach rendering - partial (docs/KNOWN-LIMITATIONS.md:2580; docs/KNOWN-LIMITATIONS.md:2582); complete: Launched audio cells render, but audio playback ignores cell gain, transpose, detune, RAM mode and loop length.
  - `R-workflow-46` Session audio-cell playback by track type - partial (docs/KNOWN-LIMITATIONS.md:2580-2584); complete: Audio cells play on sample-track columns only, still silence instrument-track columns, and ignore gain, transpose, detune, loop length and warping.

## Q050-014 Document supported plug-in formats and caveats

**P0 · L10 devices, instruments, effects, plug-in hosting · docs · S**

Goal: Users can distinguish native format support from unverified bridge and Kontakt-library compatibility.

Why: Current docs blur native support with unverified bridge/Kontakt compatibility and still say per-file scan details are socket-only.

Acceptance:
- Update the Linux plug-in and Kontakt sections to separate format loading from tested third-party compatibility.
- Name the Plug-in Modules list as the GUI location for per-file scan status and quarantine controls.
Items:
  - `R-instruments-11` Native Linux plugin ecosystem is growing but fragmented - partial (src/core/PluginFactory.cpp:277-308; plugin.list and plugin.rescan; docs/KNOWN-LIMITATIONS.md:149-167 notes third-party VST3/CLAP effects have not been validated); docs: Local plug-in discovery and scanning exist, but supported native formats and the unverified bridge/third-party limits need a clear user-facing guide.
  - `R-instruments-26` Kontakt-scale orchestral libraries are hard to replace - partial (plugin.load; plugins/Sf2Player/; docs/PLUGIN-HOSTING-IN-RELEASE.md:492-493 shows fixture-based VST3 hosting, not validated Kontakt libraries); docs: Zene hosts standard plug-in formats and SoundFonts, but proprietary Kontakt libraries and their installers or authorization are external and unverified.
  - `R-ui-19` Searchable plugin health and metadata - have (src/gui/CommandLists.cpp:138 (Plug-in Modules list over plugin.scan_cache_list: path, status); Edit > Plugin Quarantine; docs/KNOWN-LIMITATIONS.md:1091 still says per-file records are socket-only); docs: Scan records and quarantine are in the interface; KNOWN-LIMITATIONS.md:1091 is stale.

## Q050-015 Articulation map editor

**P0 · L07 note editor, MIDI editing, scales, chords, articulations · completion · M**

Goal: Create and edit named articulation maps, assign keyswitches, programs, and controllers, and manage the map from the interface.

Why: Medium demand across eight DAWs; the documented articulation-map capability is exposed through track.articulations commands but has no user editor.

Acceptance:
- The editor creates, reorders, renames, and removes articulation entries and saves them with the project.
- A playback test verifies each selected articulation sends its configured keyswitch, program, or controller before the note.
- A headless command and undo test produce the same map as the UI.
Items:
  - `R-instruments-2` Articulation maps and keyswitch management - partial (track.articulations and track.articulations_set; src/core/ArticulationMap.cpp:50-119; src/tracks/InstrumentTrackNotes.cpp:159-183); complete: Named articulations can send keyswitch, program and controller messages and notes can be assigned, but advanced editing and per-articulation timing remain absent.

## Q050-016 MPE expression event streams

**P0 · L07 note editor, MIDI editing, scales, chords, articulations · completion · L**

Goal: Capture, edit through commands, save, and play time-varying pitch, pressure, and timbre points on individual notes.

Why: Medium demand and five DAWs; the MPE promise is per-note curves, while the merged model stores only scalar snapshots.

Acceptance:
- A headless command test writes points at known note offsets and reads back the same values.
- A project save and reload preserves point order and values while a note with no expression keeps its prior serialized form.
- A MIDI-output test verifies pitch, pressure, and timbre changes reach the instrument at their scheduled offsets.
Items:
  - `B-ED-122` Per-note expression curves - partial (src/core/ControlCommandsNoteCurves.cpp:25-29; `note.pitch_curve_get/set`; `note.expression_set` stores pressure/timbre as scalar values); complete: Notes have editable pitch curves and scalar MPE pressure/timbre, but pressure and timbre do not have time-varying curves.
  - `R-editing-55` Time-varying per-note expression - partial (note.pitch_curve_set; docs/NOTE-EDITOR.md:122; docs/NOTE-EDITOR.md:123); complete: Pitch can vary across a note, but pressure and timbre are still single per-note values rather than gesture curves.
  - `R-instruments-3` MPE expression is stored as per-note snapshots, not event streams - partial (docs/MPE.md:90-100 stores scalar pitch, pressure and timbre; docs/MPE.md:127-134 says the expression-curve UI is not implemented); complete: Per-note MPE snapshots persist and play, but time-varying expression event streams and curves are not represented.

## Q050-017 Complete direct-GUI edit replication coverage

**P0 · L16 collaboration and sharing · completion · L**

Goal: All remaining project-changing GUI gestures are represented in the collaboration stream, so participants can continue editing without silent reversions.

Why: Demand is high across 4 DAWs for shared editing, and the manual's every-edit promise conflicts with the observed command-registry bypass that reverts peer edits.

Depends on: Replicate common GUI edits through collaboration
Acceptance:
- A maintained coverage table maps every project-mutating GUI action to a streamable command or equivalent ordered transaction; an unclassified action fails the coverage check.
- Two-instance tests exercise the remaining edit families and verify equivalent host and peer state with no resync fallback.
- The manual's every-edit claim is retained only after the coverage check passes; otherwise the documented limitation names the unsupported paths.
Items:
  - `R-collaboration-1` Real-time shared-project editing - partial (docs/KNOWN-LIMITATIONS.md:2385-2386; src/core/ControlCommandsCollab.cpp:181-185); complete: Registered command edits stream through the host, while many direct GUI edits trigger resync and revert on peers.
  - `R-collaboration-8` Every edit shared with collaborators - partial (docs/KNOWN-LIMITATIONS.md:2385-2386; src/core/CollabSessionSync.cpp:103-120); complete: Edits issued through registered commands are forwarded, but many direct GUI edits instead resync and revert on peers.

## Q050-018 Preserve missing-instrument data and report placeholders

**P0 · L12 projects, files, import/export, interchange, recovery · fix · M**

Goal: Save and reopen projects with unavailable instruments without discarding the original plugin XML, and tell users which devices could not load.

Why: The input identifies a broken save path that loses instrument data, even though demand is low and only one DAW is cited.

Acceptance:
- Load a project whose instrument plugin is absent, save and reopen it, then verify the original plugin identity and XML are still available for recovery.
- Show one project-open notice listing each placeholder's name, format, and vendor without blocking headless project.open.
Items:
  - `R-instruments-1` Failed instrument loading can discard saved track data - broken (src/core/Instrument.cpp:117-127 falls back to DummyInstrument; src/tracks/InstrumentTrack.cpp:1107-1136; include/DummyInstrument.h:57-67 saves no missing plug-in state); fix: Loading a missing instrument creates a silent placeholder whose save path drops the original plug-in identity and state.
  - `R-interop-32` Project version and plug-in compatibility friction - partial (docs/KNOWN-LIMITATIONS.md:34-36, :159-167 (cross-build limits; unloadable plug-ins kept as placeholders with state); project.handoff's 'devices' list); complete: Missing plug-ins are kept as placeholders, but opening a project doesn't tell the user which devices to install.

## Q050-019 Chord progression panel

**P0 · L07 note editor, MIDI editing, scales, chords, articulations · completion · M**

Goal: Browse named progressions, see suggestions for the current scale, preview them, and write a chosen progression from the interface.

Why: Medium demand across five DAWs; the progression command exists but the promised user-facing panel is absent.

Acceptance:
- The Edit view opens a progression panel and shows suggestions for the selected scale.
- A control-socket test selects a progression, writes its notes, and verifies one undo restores the clip.
- The UI calls the same registered command as the headless workflow and documents the remaining bounds.
Items:
  - `B-ED-119` Chord suggestions and progression generator - partial (chord.progression_generate/chord.progression_list; src/gui/shell/ShellCommandMenus.cpp:242); complete: The UI generates named progressions, but it does not suggest progressions based on the current notes or harmony.
  - `R-editing-6` MIDI chord lane and progression UI - partial (docs/CHORD-TRACK.md:197-218,228-235; command id chord.progression_generate); complete: The arranger now draws the chord lane and supports chord-follow playback, but progression generation still has no dedicated interface panel.

## Q050-020 MIDI export tempo, timing, program, and track channel

**P0 · L12 projects, files, import/export, interchange, recovery · fix · M**

Goal: Export tempo-map changes and per-track MIDI identity at 480 PPQ while preserving each note's sub-tick timing.

Why: The input labels tempo export a promise gap and documents one tempo event plus coarse note timing, with medium demand across one or two DAWs.

Acceptance:
- Export a project with mid-song tempo changes and verify matching tempo events in the conductor track.
- Export two instrument tracks with different patches/channels and verify program-change, track identity, note timing, and PPQ in parsed SMF output.
Items:
  - `R-interop-25` MIDI export of tempo changes and humanization - partial (plugins/MidiExport/MidiExport.cpp:105,181 (one addTempo at tick 0); :293 (note times /48); interchange.smf_export writes the conductor track only); complete: project.export_midi writes one constant tempo and 48-PPQ times, so tempo changes and fine timing are lost.
  - `R-interop-49` Multitrack MIDI file export - partial (plugins/MidiExport/MidiExport.cpp:100-105 (one MTrack per instrument track with its name; addProgramChange commented out at :104, :180)); complete: Multitrack MIDI export keeps track names but leaves out program changes and channel identity.

## Q050-021 Keep stopped seek and playback position aligned

**P0 · L04 arranger, timeline, clip operations, markers, tempo map · fix · S**

Goal: A stopped seek moves the position from which playback and video resume.

Why: The row is marked broken with high demand: docs record that stopped seek moves a different position from the one playback and video use, affecting 1 DAW.

Acceptance:
- A control-socket regression test seeks while stopped, reads the same tick from transport state and the arranger playhead, then starts playback and verifies it begins at that tick.
- With a video attached, the first frame after playback starts corresponds to the sought position.
Items:
  - `R-workflow-6` Stopped seek desynchronizes playback and video - broken (docs/KNOWN-LIMITATIONS.md:2408-2411 (stopped seek moves the stopped timeline, play starts from the song's); src/core/ControlCommandsTransport.cpp:210-221); fix: Seeking while stopped still desynchronises playback and video.

## Q050-022 Show automation lanes as track children

**P0 · L06 automation and modulation · feature · M**

Goal: Expand an arrangement track to see and edit its automation lanes directly beneath it.

Why: Demand is medium (7 DAWs); lane_add stores followsTrack but the GUI does not render it as a child lane, an explicitly tagged promise gap.

Acceptance:
- An automation lane with followsTrack is rendered as a collapsible child of its owning track in the arranger.
- Collapsing or hiding the owner hides its child lanes; moving, deleting, undoing, and redoing the owner keep lane ownership consistent.
- A GUI test verifies child-lane selection/editing and the agent surface reports the same owner and lane state.
Items:
  - `R-workflow-197` Automation lanes under their owning tracks - partial (src/core/ControlCommandsAutomationLanes.cpp:69-77 (lane is an automation track moved under the owner, setFollowsTrack); src/gui/shell/ShellCommandMenus.cpp:99; no GUI use of followsTrack (rg)); complete: Lanes are separate automation tracks placed under and following the owner, but they do not fold with or nest inside the owning track.

## Q050-023 Reference project copy and audition with EditLocks

**P0 · L12 projects, files, import/export, interchange, recovery · fix · L**

Goal: Copy audio, note, pattern, and automation clips between reference projects safely, keep copied device identities stable, and audition a cached reference render.

Why: The rows report low demand across one or two DAWs, but the lock bypass is broken behavior and the advertised cross-project copy workflow is incomplete.

Acceptance:
- Copy automation and pattern clips through reference.copy_clips and verify target rows/parameters and device IDs remain valid after save/load.
- Attempt each copy onto a locked track and verify it is refused with no model, undo-history, or file change.
- Audition a reference project from its cached render and verify repeated playback does not re-render unchanged content.
Items:
  - `B-WF-042` Several projects open, copy between them - partial (reference.copy_tracks (src/gui/ReferenceProjectPanel.cpp:122); project.switch; project.copy_tracks/paste_tracks; docs/KNOWN-LIMITATIONS.md:2359-2361); complete: A read-only reference project and clipboard copy between instances work, but automation and pattern clips cannot be copied from the reference and copied devices renumber their fx ids.
  - `B-WF-044` Lock items or tracks - partial (track.set_locked/clip.set_locked/project.locks (src/core/ControlCommandsProjectInfo.cpp:218,222); docs/KNOWN-LIMITATIONS.md:2362 (EditLocks does not refuse reference.copy_clips onto a locked track)); fix: Track and clip locks exist, but reference.copy_clips can still write onto a locked track.
  - `R-workflow-59` Reference-project clip copy coverage - partial (src/core/ControlCommandsReference.cpp:129-138; docs/KNOWN-LIMITATIONS.md:2360-2362); complete: Reference copying takes note and audio clips but refuses automation and pattern clips, and the clip copy ignores edit locks.
  - `R-workflow-194` Reference projects cannot play in the active engine - partial (docs/KNOWN-LIMITATIONS.md:2358-2360 (reference never plays; automation and pattern clips can't be copied); reference.open); complete: The reference project can't be auditioned, and its automation and pattern clips can't be copied.

## Q050-024 Stream audio between Ableton Link peers

**P0 · L16 collaboration and sharing · feature · L**

Goal: Two Link peers on a local network can exchange audio independently of tempo, beat-phase, and start-stop synchronization.

Why: Demand is medium across 3 DAWs, and the W6 spec names Link Audio as planned while docs/LINK-SYNC.md lists tempo, phase, and transport only and contains no audio-stream command.

Acceptance:
- Two local Link-enabled instances exchange a PCM audio stream while existing Link tempo, phase, and transport sync continue to work.
- The audio callback consumes a preallocated bounded buffer with no allocation or mutex on the realtime path; a stress test reports bounded underruns and latency.
- The feature has registered enable/state commands and docs distinguish audio streaming from Link transport sync and state the LAN-first network limits.
Items:
  - `R-collaboration-9` Link Audio peer streaming - missing (docs/LINK-SYNC.md:1-19; command list search found no Link Audio stream command); implement: Ableton Link now shares tempo, phase and transport, but Zene has no peer audio-streaming path.

## Q050-025 Preserve DAWproject MIDI channels end to end

**P0 · L12 projects, files, import/export, interchange, recovery · fix · M**

Goal: Import, save, play, and export DAWproject notes without changing their MIDI channels, and report any channel that cannot be preserved.

Why: The row confirms broken DAWproject MIDI channel preservation and resulting data loss despite medium demand and one DAW.

Depends on: Add MIDI channels to note model and persistence
Acceptance:
- Round-trip a multi-channel DAWproject fixture through import, project save/load, playback capture, and export; compare every note channel.
- Verify unsupported channel data is named in the import result and user-facing report.
Items:
  - `R-interop-4` DAWproject MIDI channel preservation - broken (src/core/DawProjectReadTracks.cpp:99 parses note.channel; src/core/DawProjectSession.cpp:577-587 imports notes without it; tests/src/core/DawProjectInterchangeRoundTripTest.cpp:1185-1190 confirms channel loss.); fix: The importer reads note channels but silently drops them because the engine Note model has no channel field, and the loss is not reported.

## Q050-026 Honor the selected MIDI clock source

**P0 · L15 platform: performance, stability, Linux audio, install, hardware controllers · fix · S**

Goal: Only the configured MIDI input can drive Zene’s MIDI clock follower.

Why: The queue marks source selection as broken: MidiClock receives events without source identity despite medium demand.

Acceptance:
- A regression test configures two MIDI ports and proves clock events from the unselected port are ignored while selected-port events update clock state.
- A control-socket test reads back the configured source and follower state.
Items:
  - `R-interop-5` Selected MIDI clock source is ignored - broken (src/core/MidiClock.cpp:438-441 accepts no source identity; src/core/midi/MidiAlsaSeq.cpp:771-783 dispatches clock events without ev->source.port.); fix: The selected source port is stored but never checked, so any incoming ALSA MIDI clock can drive the follower.

## Q050-027 Preserve mixer and hardware I/O on track import

**P0 · L09 routing, buses, sends, sidechain, I/O · fix · M**

Goal: Import or paste tracks and keep their mixer channel plus named hardware input/output assignments, creating a matching channel when needed.

Why: project.import_tracks and project.paste_tracks remap copied tracks to the master (ControlCommandsImportTracks.cpp:79), dropping their mixer and hardware routes; demand is high across 2 DAWs, so this is a P0 broken-workflow fix.

Acceptance:
- A control-surface test imports and pastes tracks routed to named mixer and hardware buses, then verifies the assignments in mixer.get_state and mixer.hardware_get.
- When a referenced mixer channel is absent, import creates or resolves it deterministically by bus name; undo removes the imported tracks and any channel created for them.
Items:
  - `R-interop-7` Preserve hardware I/O on track import - partial (project.paste_tracks / project.import_tracks ('each playing into the master (mixer channels do not carry between projects)'); mixer.hardware_output, audio.io_setup); complete: Imported or pasted tracks lose their mixer channel and with it any hardware output/input assignment.

## Q050-028 Wake the ALSA MIDI output thread on queued events

**P0 · L15 platform: performance, stability, Linux audio, install, hardware controllers · fix · S**

Goal: Hardware MIDI output sends queued events promptly instead of waiting for the poll timeout.

Why: The queue marks output wake latency broken and high demand; the worker can wait 250 ms after the audio path queues an event.

Acceptance:
- A sequencer test queues an event while the worker is polling and verifies it is sent promptly rather than after the 250 ms timeout.
- The wake path is bounded and lock-free from the audio thread; queue overflow and shutdown tests remain green.
Items:
  - `R-performance-4` ALSA sequencer output wake latency - broken (src/core/midi/MidiAlsaSeq.cpp:278-282 (audio path only pushes to m_outQueue), :718-727 (queue drained, then poll with EventPollTimeOut=250 ms at :43; no wake on push)); fix: An outgoing MIDI event queued just after the drain waits up to 250 ms for the next poll timeout.

## Q050-029 Reject invalid remote knob indices

**P0 · L15 platform: performance, stability, Linux audio, install, hardware controllers · fix · S**

Goal: Invalid remote-control knob numbers are refused without overflowing the model index.

Why: The queue marks the knob index path broken because it computes the page offset before validating the input.

Acceptance:
- Regression tests pass negative, maximum-valid and first-invalid knob indices and verify only valid controls change.
- Address/overflow sanitizers report no invalid access for malformed knob requests.
Items:
  - `R-workflow-12` Invalid remote-knob index can overflow - broken (src/core/RemoteControls.cpp:131-132 computes pageWithin(m_page,models)*Knobs+knob before checking knob range); fix: The knob index is still multiplied and added before the knob is range-checked, so a malformed knob on a nonzero page can overflow a signed int.

## Q050-030 Neural Amp model-load responsiveness

**P0 · L10 devices, instruments, effects, plug-in hosting · fix · S**

Goal: Users can select a second Neural Amp model without freezing the GUI behind the previous load.

Why: setModelPath joins the prior worker on the GUI caller and the freeze is reported as broken at medium demand.

Acceptance:
- Start a deliberately slow first load and select another model; the UI must remain responsive.
- Verify cancellation or queued replacement returns the final requested model and does not leak a worker.
Items:
  - `R-ui-20` Starting a second Neural Amp load can freeze the GUI - broken (plugins/NeuralAmp/NeuralAmpEffect.cpp:106-109 (setModelPath joins the previous load thread on the caller's thread)); fix: Choosing a second model while one is loading blocks the GUI.

## Q050-031 Safely accept plug-in MIDI and media drops

**P0 · L04 arranger, timeline, clip operations, markers, tempo map · fix · M**

Goal: Dragging generated MIDI or supported audio files from a plug-in editor into the arranger creates clips at the drop position or safely rejects invalid files.

Why: The item is marked broken and reports a plug-in drag crash; TrackContainerView accepts internal string-pair drags but has no external file-URL drop handling, affecting 2 DAWs.

Acceptance:
- A GUI regression test drops valid MIDI and audio file URLs from a plug-in-style drag source and verifies clips appear at the drop tick.
- A malformed, missing, or unsupported file drop is rejected without crashing, changing project state, or leaving a partial clip.
Items:
  - `R-interop-67` MIDI drag-and-drop plug-in crash - missing (no external file-URL drop handling on arranger tracks: src/gui has hasUrls/urls() only in SessionGridDrop.cpp:63 (audio onto session slots); TrackContainerView.cpp:416-421 accepts LMMS string-pair drags only); implement: The plug-in crash doesn't apply because Zene's arranger accepts no external drags, so MIDI dragged out of a plug-in can't be dropped at all.

## Q050-032 Select a mixer strip on any control press

**P0 · L08 mixer, metering, channel strip · fix · S**

Goal: Users can trust keyboard shortcuts and MIDI actions to target the strip they just operated.

Why: The behavior is broken for 1 DAW request: pressing a strip control can leave selection on a different, visually implied channel.

Acceptance:
- Pressing a fader, knob, or other child control selects its owning strip before the control action runs.
- A regression test presses each control class and verifies selected-channel state and subsequent keyboard/action routing.
- The test passes in the headless/UI test configuration without weakening existing selection assertions.
Items:
  - `R-ui-14` Consistent track focus in MixConsole - broken (src/gui/MixerChannelView.cpp:374-376 (selection changes only on a press the strip itself receives; faders/knobs take their own presses)); fix: Clicking a fader or knob on another strip probably leaves the current channel unchanged.

## Q050-033 Preserve source timing in cross-project imports

**P0 · L12 projects, files, import/export, interchange, recovery · fix · L**

Goal: Keep imported notes and automation at their source sub-tick positions instead of rounding them onto a coarse beat grid.

Why: The row confirms timing loss from rounded imports at medium demand for one DAW, which is a broken content-preservation behavior.

Acceptance:
- Import fixtures with events between current grid points and compare output ticks or sample-frame offsets against source timing.
- Verify representable events no longer increment rounded_times and unrepresentable events are explicitly reported.
Items:
  - `R-interop-20` Accurate timing in cross-project imports - partial (docs/DAWPROJECT-INTERCHANGE.md:259 (loss #9: foreign times rounded onto the 48-per-beat grid, counted as rounded_times, src/core/DawProjectReadShared.h:195)); complete: Imports round foreign event times onto the 48-ticks-per-beat grid, which shifts events slightly.

## Q050-034 Land recorded session content in arrangement clips

**P0 · L02 launcher / session clips / performance · completion · M**

Goal: Make Arrangement Record create clips that contain the launched slot's MIDI notes or audio content, not only its recorded position and length.

Why: This is a promise gap with data-loss behavior; demand is low across 1 DAW, and ControlCommandsSessionRecordLand.cpp confirms landed clips currently carry only the recorded span.

Acceptance:
- Arm Arrangement Record, launch and stop a MIDI cell, then land it; verify the arrangement clip contains the slot's notes and preserves their timing.
- Repeat with an audio cell and verify the landed clip references or copies the same audio content and renders it.
- Undo and redo the land operation and verify the clip content is restored exactly.
Items:
  - `R-workflow-58` Arrangement Record lands clips without session content - partial (src/core/ControlCommandsSessionRecordLand.cpp:35-42 (notes NOT copied into the landed clip); command description session.arrangement_record_land); complete: Arrangement Record lands clips at the right position and length but leaves them empty of the launched slot's notes or audio.

## Q050-035 Correct stem separation availability docs

**P0 · L05 audio editing, warping, stretching, fades · docs · S**

Goal: Users and reviewers can tell that stem separation is built in, local, and available by default in the supported configuration.

Why: The current docs say stem splitting is compiled out despite the default build enabling it; demand is medium across 2 DAWs, making this a release-truth error.

Acceptance:
- Verify the default WANT_STEM_SPLIT configuration and update KNOWN-LIMITATIONS.md and release-honesty text to match it.
- Confirm the docs describe local/offline processing and contain no stale claim that stem separation is compiled out or OFF by default.
Items:
  - `R-platform-26` Local stem processing can preserve privacy and work offline - have (stem.get_state; docs/KNOWN-LIMITATIONS.md:1414-1434 (local HTDemucs, on by default, LMMS_STEM_OFFLINE); docs/KNOWN-LIMITATIONS.md:419-432 still says stem separation is compiled out and reports OFF); docs: Separation is local and can run offline, but one limitations paragraph still says it is compiled out of the builds while the stem section says it is on by default.

## Q050-036 Complete the specified warp mode set

**P0 · L05 audio editing, warping, stretching, fades · completion · L**

Goal: Users can select the specified Beats, Tones, Texture, Re-Pitch, and Complex warp modes per clip and hear their distinct behavior.

Why: Despite low demand from 1 DAW, the product spec promises five warp modes while the current enum contains only Resample, PreservePitch, and RubberBand, making this a direct spec promise gap.

Acceptance:
- Verify every specified mode appears in clip state and can be selected through the UI and agent command.
- Render deterministic fixtures for each mode and verify duration, transient behavior, and pitch behavior against mode-specific expectations.
- Save and reload a project with different warp modes on separate clips and verify the selections persist.
Items:
  - `R-editing-20` Warp mode set is narrower than the spec - partial (commands_snapshot.json: warp.stretch; docs/KNOWN-LIMITATIONS.md:1548); complete: Warp supports resample, pitch-preserving WSOLA and optional Rubber Band modes, but not the distinct Beats, Tones, Texture or Complex algorithms.

## Q050-037 Define the automation value before its first point

**P0 · L06 automation and modulation · fix · M**

Goal: Start and return to the beginning with automation holding a predictable initial value before the first node.

Why: Demand is low (1 DAW), but AutomationClip::valueAt returns 0 before the first node, so startup and return behavior are a confirmed broken playback contract.

Acceptance:
- Add a regression test showing times before the first node read the authored parameter's initial value, or an explicit clip-start value when present, rather than zero.
- Seek or start before the first node, stop, and return to start; playback reports the same value each time.
- Save and reload a project with a later first node and confirm the pre-first-point contract remains stable while interpolation at and after the first node is unchanged.
Items:
  - `R-editing-16` Give automation a defined pre-first-point value - broken (src/core/AutomationClip.cpp:589; src/core/AutomationClip.cpp:592); fix: Before its first point an automation clip returns zero regardless of the controlled parameter's authored value, which can cause a start-of-song jump.

## Q050-038 Add a Shaper modulator and refresh its spec

**P0 · L06 automation and modulation · feature · M**

Goal: Shape another modulation source with a drawable transfer curve while keeping the promised envelope follower available.

Why: Demand is low (1 DAW), but the approved modulation spec promises Shaper and Envelope Follower sources that the current shape enum lacks, making this a product promise gap.

Depends on: Add an audio envelope follower modulation source
Acceptance:
- A Shaper source can reference an existing modulator and apply its saved curve to that source; self-reference and cycles are refused.
- The command surface and Modulators panel can create, edit, read back, remove, undo, and reload the Shaper and its curve.
- A bounded audio-path test proves the follower and Shaper allocate nothing during playback, and MODULATION.md section 9 reflects current UI, sources, targets, and limits.
Items:
  - `R-effects-10` Promised Shaper and Envelope Follower modulators - partial (include/ModulationLayer.h:96-108 (no shaper or follower shape); docs/MODULATION.md:226-233 still says no UI, LFO only and block-only, which later work changed); complete: LFO-type sources exist, but the promised Shaper and an audio envelope follower do not, and MODULATION.md section 9 is out of date.

## Q050-039 MPE release-velocity capture fix

**P0 · L07 note editor, MIDI editing, scales, chords, articulations · fix · M**

Goal: Preserve the incoming MPE note-off velocity on the released note and send it through playback.

Why: Low demand and one DAW are cited, but MPE capture currently drops note-off velocity and loses note data.

Acceptance:
- A regression test sends an MPE note-on and a non-default note-off velocity and reads the same value from the saved note.
- Playback emits the matching note-off velocity to a MIDI test client.
- The zero-velocity and non-MPE paths retain their existing behavior.
Items:
  - `R-instruments-4` MPE controller note-off drops release velocity - broken (src/tracks/InstrumentTrack.cpp:324-360 tracks MPE note-off without velocity; :477-490 calls noteOff with only the offset); fix: The MPE note-off path drops release velocity even though the note model can store it.

## Q050-040 Unmute note-chase fix

**P0 · L07 note editor, MIDI editing, scales, chords, articulations · fix · S**

Goal: When chase is enabled, start notes that are already under the playhead as soon as their track or clip is unmuted during playback.

Why: Low demand across one DAW, but note chase currently handles start, jump, and loop only, leaving playback silent after unmuting under the playhead.

Acceptance:
- A regression test unmutes a track and a clip during playback and verifies the currently sounding note starts once.
- The test verifies no duplicate note-on or stuck note-off after a seek, loop, or re-mute.
- The existing chase-on-start, jump, and loop tests remain green.
Items:
  - `R-performance-6` Reason 14.1 fixed note chase after unmuting - partial (include/MidiPlayback.h:39-40 (chase on start, jump or loop); src/tracks/InstrumentTrackNotes.cpp:139-141); complete: MIDI chase sounds notes when playback starts, jumps or loops into them, but not when a muted track or clip is unmuted mid-note.

## Q050-041 Carla processing mutex fix

**P0 · L10 devices, instruments, effects, plug-in hosting · fix · S**

Goal: Users can process Carla devices without serializing the audio callback behind MIDI handling.

Why: Carla holds one mutex around process and MIDI handling, a documented real-time blocking defect at high demand.

Acceptance:
- Stress MIDI input while processing and verify the audio callback takes no shared mutex.
- Use an allocation/lock probe and render fixture to confirm event delivery remains ordered.
Items:
  - `R-performance-45` Carla processing mutex - partial (plugins/CarlaBase/Carla.cpp:536-546 (fMutex held around the whole plug-in process), :557 (handleMidiEvent takes the same mutex)); fix: Live MIDI input and Carla's processing share one mutex, so MIDI waits on a whole process call and the audio thread can wait on the MIDI thread.

## Q050-042 Preserve long patterns in legacy project upgrades

**P0 · L12 projects, files, import/export, interchange, recovery · fix · S**

Goal: Open older project files with long patterns without truncating their lengths during version upgrades.

Why: The row reports low demand from one DAW and an unverified upgrade path, but the suspected failure is project-data truncation, so it warrants a P0 verification/fix.

Acceptance:
- Add an older-version project fixture with long note clips and verify pattern length before and after upgrade, save, and reopen.
- If the regression test exposes truncation, fix the specific upgrade path and keep the fixture as a regression test.
Items:
  - `R-interop-70` Legacy project pattern truncation - unknown (src/core/DataFile.cpp:2722-2730 (legacyFileVersion) and upgrade_* routines :1429-2457; no test for long legacy pattern lengths found); fix: Legacy upgrades exist, but nobody has checked whether long piano-roll patterns survive them.

## Q050-043 Bound SessionTuning reader retries

**P0 · L15 platform: performance, stability, Linux audio, install, hardware controllers · fix · S**

Goal: Audio readers never spin indefinitely while a tuning slot is being published.

Why: The queue marks the reader retry as unbounded and high demand because the front slot is published before its writer bit is cleared.

Acceptance:
- A concurrency regression test stresses slot publication and proves readers make progress while the writer updates source metadata.
- ThreadSanitizer coverage reports no race and the audio path performs no allocation or blocking lock.
Items:
  - `R-performance-29` Unbounded tuning-reader retry - broken (include/SessionTuning.h:214-230 (reader spins while front slot has WriterBit); src/core/SessionTuning.cpp:270-274 (front published before WriterBit is cleared, with a QString assignment between)); fix: After publishing a new front slot the writer still holds its WriterBit across a QString copy, so a preempted writer can make the audio reader spin.

## Q050-044 Undo checkpoints for project opening and scripts

**P0 · L17 agent control surface and AI assistance · fix · M**

Goal: A user can undo script.run mutations and restore the unsaved session displaced by project.open with one undo action.

Why: Despite low demand in 1 DAW, ControlReversibilityTablePassive.cpp:79-95 marks project.open and script.run irreversible, so unsaved project content and script mutations have no automatic undo fallback.

Acceptance:
- Run a script that changes project state, then verify one undo restores the exact pre-script state and redo reapplies it.
- Open another project over a dirty unsaved session, then verify one undo restores the displaced session and its content.
- Bound checkpoint size; verify an over-limit checkpoint refuses the destructive action before changing either project and reports a clear error.
Items:
  - `R-workflow-60` Undo coverage for irreversible actions - partial (src/core/ControlReversibilityTablePassive.cpp:79-95 (project.open and script.run Irreversible)); complete: project.open and script.run stay irreversible; a script is undoable only if it takes its own checkpoint.

## Q050-045 Reduce the crash-loss window for recorded audio

**P0 · L03 recording, takes, comping, inputs · fix · M**

Goal: A crash loses substantially less buffered recording audio than the current journal and ring window.

Why: The recovery journal can lag by one second and leave up to 65,536 frames in the recorder ring at a crash, which is direct recording data loss.

Acceptance:
- A crash-recovery test terminates recording at controlled points and proves the recovered take loses less than one second of audio.
- The recovery report states the measured bound, and audio-thread capture remains allocation-free.
Items:
  - `R-recording-38` Crash recovery can lose recent recorded audio - partial (docs/KNOWN-LIMITATIONS.md:899-910 (journal lags up to one second; up to 65536 ring frames lost); record.recovery_get_state); complete: After a crash up to a second of journal lag plus up to 65536 ring frames of a take are lost.

## Q050-046 Offer undoable recovery at startup

**P0 · L03 recording, takes, comping, inputs · fix · M**

Goal: Users are prompted about interrupted takes at startup and can place a recovered take with a reversible action.

Why: The current recovery menu has no startup prompt and its placement action bypasses control.undo, violating the reversible-action contract.

Acceptance:
- Startup offers recovery only when record.recovery_get_state reports an interrupted take.
- Placing a recovered take is a registered command; control.undo removes the placed track or clip and redo restores it.
- A headless socket test can invoke the same placement command and read back the recovered project state.
Items:
  - `R-recording-41` Recording recovery is not offered at startup - partial (docs/KNOWN-LIMITATIONS.md:892-898 (File > Recover Recordings menu; no start-up prompt; placement not a registry command so control.undo misses it); src/gui/MainWindow.cpp:351-352); complete: The recovery menu exists but nothing prompts at start-up and placing a recovered take is not undoable.

## Q050-047 Correct rack UI limitations documentation

**P0 · L10 devices, instruments, effects, plug-in hosting · docs · S**

Goal: Users can see which rack chains, zones, and first-device actions already work in the GUI.

Why: The limitations page says chain effects and zones are socket-only although the Device panel and menu expose them.

Acceptance:
- Rewrite the stale socket-only rack bullets and verify an empty parallel chain can receive its first device from the GUI.
- Check the displayed Zones and Add Zone actions against command availability.
Items:
  - `R-workflow-123` Rack editing and macros - partial (src/gui/shell/DevicePanel.cpp:371-377 (parallel chains shown with their devices); src/gui/shell/ShellCommandMenus.cpp:439-440 (Zones list, Add Zone); src/gui/RackMenu.cpp:140 (split); docs/KNOWN-LIMITATIONS.md:262 still says chain effects, zones and Lua are socket-only); docs: The Device panel, the Rack menu and Lua's zene.command bridge now reach parallel chains, zones and macros, so KNOWN-LIMITATIONS.md:262 is stale.

## Q050-048 Bound browser feature analysis to the analysis window

**P0 · L11 browser, samples, presets, library · fix · S**

Goal: Large audio files use memory and decode time proportional to the analyzed window, while the browser still reports full-file duration.

Why: The current BrowserFeatures path decodes the whole file before analyzing only 20 seconds, a broken memory/performance behavior reported at medium demand.

Acceptance:
- A long-file regression fixture confirms BrowserFeatures decodes at most FeatureSeconds of frames rather than materializing the whole file.
- Reported duration matches the file header even though only the leading analysis window is decoded.
- A memory-bound check demonstrates that peak decode memory does not scale with the source file's full duration.
Items:
  - `R-performance-60` Bounded audio feature analysis - broken (src/core/BrowserFeatures.cpp:120-125 (SampleDecoder::decode decodes the whole file; only FeatureSeconds are analysed)); fix: Feature analysis decodes the entire file into memory to analyse its first seconds, so long files cost memory and time.

## Q050-049 Crash-aware plug-in quarantine

**P0 · L10 devices, instruments, effects, plug-in hosting · feature · M**

Goal: Users are offered quarantine for the plug-in that caused a scan or startup crash.

Why: The safe-start path does not identify the crashing plug-in and its limitation doc incorrectly says there is no quarantine GUI.

Acceptance:
- Record the active plug-in in the safe-start marker, reproduce a crash, and offer quarantine on next start.
- Update the stale limitation bullet to match the existing GUI and verify manual release still works.
Items:
  - `R-workflow-225` Remember plugins that crash during browsing - partial (src/gui/PluginQuarantineMenu.cpp:80-107 (Edit > Plugin Quarantine, manual add/release); oop.get_state crash-loop refusal; safestart.*; no automatic quarantine of the plug-in that crashed (rg auto quarantine: none); docs/KNOWN-LIMITATIONS.md:415-418 still says no GUI); complete: Users can quarantine a plug-in by hand and out-of-process hosting refuses crash-looping clients, but an in-process plug-in that crashes the host is not flagged automatically, and KNOWN-LIMITATIONS:415 still says no quarantine GUI exists.

## Q050-050 WASM worker log snapshot race

**P0 · L10 devices, instruments, effects, plug-in hosting · fix · S**

Goal: Users can read WASM worker logs while a module is logging without a data race or corrupted snapshot.

Why: takeLog reads log storage while the worker writes it without synchronization, a broken data-race path.

Acceptance:
- Stress concurrent host_log and takeLog under ThreadSanitizer and verify a consistent snapshot.
- Confirm the worker remains nonblocking and log retrieval returns a bounded result.
Items:
  - `R-workflow-259` Taking worker logs races with module logging - broken (src/wasm/WasmWorker.h:100-113 (takeLog is control-thread API) -> src/wasm/WasmWorker.cpp:389-395 -> src/wasm/WasmSandbox.cpp:561-568 reads log/logSize while host_log writes them on the worker thread (WasmSandbox.cpp:230), with no lock or atomic); fix: WasmWorker::takeLog reads the sandbox log buffer without synchronisation while the worker thread may write it (no current caller outside the class, so latent, and WASM is off in builds without wasmtime).

## Q050-051 Sandboxed Lua module imports

**P0 · L17 agent control surface and AI assistance · feature · M**

Goal: A script author can reuse Lua source files while imports remain confined to the active scripts root or package directory.

Why: Demand is low in 1 DAW, but LUA-PACKAGE-FORMAT.md describes sibling files an entry script uses while ScriptEngine.cpp removes require and all file-loading entry points.

Acceptance:
- Add a sandboxed require path for sibling modules and verify traversal, absolute paths, and symlink escapes outside the allowed root are refused.
- Verify module imports work in loose scripts and packages without restoring os, io, package, dofile, or unrestricted file loading.
- Correct LUA-PACKAGE-FORMAT.md so its package layout and import behavior match the implemented runtime.
Items:
  - `R-workflow-306` Share reusable source files across scripts - missing (src/core/ScriptEngine.cpp:182-191 removes require/dofile/loadfile/package; docs/LUA-PACKAGE-FORMAT.md:15 nonetheless speaks of 'sibling files the entry script uses'); implement: Lua scripts cannot load shared library files, so code must be copied between scripts, and the package doc implies otherwise.

## Q050-052 Project media portability across Save As, relink, and handoff

**P1 · L12 projects, files, import/export, interchange, recovery · completion · L**

Goal: Keep audio and video assets findable and portable when a project moves, is collected, or is handed to another machine.

Why: The input rates video portability high demand across five DAWs, and documents concrete missing-asset, relink, and collect_media gaps.

Acceptance:
- Open a project with a moved video and confirm project.missing_assets reports it, project.relink repairs it, and collect_media includes it with a project-relative reference.
- Save As with media collection and reopen the new project after moving its folder; verify referenced audio and video still resolve.
- Run background video.extract_audio through task.list and update the limitation text to match the background option.
Items:
  - `R-interop-63` Portable audio assets on Save As - partial (project.collect_media (an explicit action, src/core/ControlCommandsCollectMedia.cpp:135); project.save args: path only); complete: Save As to a new folder leaves media where it was; collecting it is a separate step.
  - `R-workflow-11` Video asset collection and portability - partial (docs/KNOWN-LIMITATIONS.md:2373-2374 (one video, absolute path, missing_assets/relink do not know it); rg 'video' in src/core/ControlCommandsCollectMedia.cpp, ControlProjectAssetsRelink.cpp: none); complete: Audio media collect and relink work, but the attached video is absolute-path-only and outside missing-asset detection, collect and relink.
  - `R-workflow-128` Video support has fragile media handling - partial (docs/KNOWN-LIMITATIONS.md:2373-2375; src/core/ControlCommandsVideo.cpp:223 (background extraction exists); rg video in ControlProjectAssetsRelink.cpp and ControlCommandsProjectArchive.cpp found nothing); complete: Background extraction now exists, but the video is still stored by absolute path and missing_assets, relink and collect_media don't know about it.
  - `R-other-1` Video scoring workflow - partial (video.attach, video.set_offset, video.window_show, video.extract_audio ('background' option); src/core/ControlCommandsVideo.cpp:202-235; docs/KNOWN-LIMITATIONS.md:2373-2376; no video in src/core/ControlProjectAssetsRelink.cpp); complete: One video per project plays in sync and its audio can be extracted in the background, but project.missing_assets/relink still don't know the video.
  - `R-editing-134` Expanded video playback engines - partial (video.attach/video.set_offset (FFmpeg); docs/KNOWN-LIMITATIONS.md:2373-2377 (one video per project, not in missing-assets/relink, audio extract blocks the interface)); complete: FFmpeg plays broad formats and the offset re-syncs, but the video is outside missing-asset relink and audio extraction freezes the interface.
  - `R-performance-64` Background video audio extraction - have (src/core/ControlCommandsVideo.cpp:202-235 (video.extract_audio 'background' runs on a worker in the task area); docs/KNOWN-LIMITATIONS.md:2375-2376 still says it decodes on the interface thread); docs: Background extraction with progress and cancel is built, but the limitations page still says extraction freezes the interface.

## Q050-053 Linux screen-reader core-flow verification

**P1 · L14 help, onboarding, accessibility · completion · L**

Goal: Complete the main project-editing workflow with Orca and identify which remaining controls block speech access.

Why: These items report high demand and four-DAW interest, while KNOWN-LIMITATIONS.md:2432 says no screen reader has been run.

Acceptance:
- On Linux, use Orca to open Help, create or open a project, navigate tracks and clips, make an edit, and save; record Orca and OS versions and any blocker.
- Run the accessibility walk on the controls reached and add regression coverage for each missing name, role, value, or focus path fixed.
- Publish a tested/not-tested status for this bounded workflow without claiming coverage for other platforms.
- Verify spoken control names, slider bounds, and values in the same Orca pass; add descriptions where the reader receives no useful context.
Items:
  - `B-WF-129` Screen-reader support - partial (view.announcements (src/gui/ControlCommandsAccessibleEditing.cpp:179); window.accessibility_report; docs/KNOWN-LIMITATIONS.md:2432 (no screen reader was run)); complete: Accessible names, a live announcement level and an audit exist, but no real screen reader has been run against Zene.
  - `R-accessibility-2` FL Studio screen-reader inaccessibility - partial (docs/KNOWN-LIMITATIONS.md:2432 (Qt bridge asserted, no screen reader was run); view.announcements (src/gui/ControlCommandsAccessibleEditing.cpp:179); window.accessibility_report); complete: Zene exposes names and announcements through Qt's accessibility bridge, but screen-reader use has never been tested.
  - `R-accessibility-5` A real choice of accessible DAWs - partial (docs/KNOWN-LIMITATIONS.md:2432 (Qt bridge asserted, no screen reader was run); view.announcements (src/gui/ControlCommandsAccessibleEditing.cpp:179); window.accessibility_report); complete: Zene is free and has the accessibility foundations, but it cannot be claimed as a usable accessible DAW until verified with screen readers.
  - `R-accessibility-19` Verify core flows with real screen readers - partial (docs/KNOWN-LIMITATIONS.md:2432 (Qt bridge asserted, no screen reader was run); view.announcements (src/gui/ControlCommandsAccessibleEditing.cpp:179); window.accessibility_report); complete: Qt accessibility metadata is tested but no screen reader has been run on transport and editing flows.
  - `R-accessibility-16` Spoken command names, bounds, and contextual help - partial (include/Accessibility.h:33-50; view.announcements; window.command_palette; docs/KNOWN-LIMITATIONS.md:2432); complete: Names, descriptions, values and an announcement level are supplied, but what a screen reader actually speaks has not been checked.

## Q050-054 Record and comp MIDI takes on instrument tracks

**P1 · L03 recording, takes, comping, inputs · feature · L**

Goal: Users can record MIDI from armed instrument tracks into arrangement takes and select comp regions across those takes.

Why: MIDI comping demand is high across 5 DAWs and MIDI take lanes across 4, while current take lanes are audio-oriented and transport.record omits armed instrument tracks.

Depends on: Show take lanes and waveforms in the timeline
Acceptance:
- A control-socket test records an armed instrument track through transport.record alongside an audio track and reads back the MIDI take.
- A GUI test selects a MIDI take region in the lane strip, and playback plus save/load preserve the selected MIDI comp.
- The new capture and comp actions are registered commands with schemas and undo coverage.
Items:
  - `R-editing-2` MIDI take comping - partial (docs/KNOWN-LIMITATIONS.md:367-384; docs/COMPING.md:189-194; command id comp.select); complete: MIDI takes participate in composite playback through comp.select, but the interface still has no comping gestures and COMPING.md still incorrectly says MIDI comping is absent.
  - `R-recording-3` MIDI take comping - partial (comp.assign (MIDI takes comped by note); src/gui/tracks/TrackOperationsWidget.cpp:350-358 ('Take lanes...' only on SampleTrackView); src/gui/shell/InspectorPanel.cpp:320 (strip shown only once a track has lanes)); complete: MIDI takes can be comped through the surface, but the track menu offers take lanes only on sample tracks, so MIDI comping has no direct interface entry.
  - `R-recording-48` MIDI Live Mode and synchronized capture - partial (src/gui/editors/PianoRoll.cpp:4402-4432 (MIDI record is the classic piano roll's per-clip record); transport.record captures sample tracks and automation only); complete: MIDI recording lives in the classic piano roll; there is no single record pass that captures armed instrument tracks' MIDI into arrangement clips alongside audio.

## Q050-055 Context-aware shortcuts and plug-in actions

**P1 · L13 customisation, shortcuts, themes, layout · completion · L**

Goal: Assign shortcuts to panel-specific actions and directly open or load plug-ins from the keyboard.

Why: The requests report high demand across three DAWs and context coverage across four, while shortcuts.set is global/menu-only and plug-in actions lack full key coverage.

Acceptance:
- Assign the same key to different actions in the arranger, note editor, automation editor, and perform contexts; verify only the active panel's action fires.
- Create a shortcut conflict and resolve it in the shortcuts UI; verify the chosen bindings survive restart.
- Bind a selected-track plug-in editor action and a chosen plug-in load action, then invoke each from the keyboard.
Items:
  - `R-accessibility-23` User-editable shortcut bindings - partial (shortcuts.set (menu actions only); docs/KNOWN-LIMITATIONS.md:2688-2690 (perform view keys not rebindable)); complete: Every menu action is rebindable, but window-local keys (perform view, canvases) and direct plug-in launch commands are not in the shortcut map.
  - `R-ui-7` Context-specific keyboard bindings - missing (src/gui/ControlCommandsShortcuts.cpp:247-249 (one global map; a clash is refused or forced); docs/KNOWN-LIMITATIONS.md:2689 (main-window menu actions only)); implement: Shortcuts are global; the same key cannot do different things in different panels.
  - `R-workflow-20` Keyboard-driven editing and shortcuts - partial (docs/KNOWN-LIMITATIONS.md:2689 (shortcuts.set binds main-window menu actions only); src/gui/ShortcutMap.cpp); complete: A palette and rebindable global shortcuts exist, but editors' own keys (perform view, note editor, mixer) cannot be rebound per context.
  - `R-workflow-121` Open the plug-in on the selected track - partial (src/gui/shell/DeviceActionMenu.cpp:62 and src/gui/CommandLists.cpp:117 (Open Editor in menus only); no shortcut bound to plugin.editor_open); complete: A device's editor opens from its card or menu, but no key command opens the selected track's instrument or plug-in.

## Q050-056 Windows and macOS screen-reader compatibility

**P1 · L14 help, onboarding, accessibility · completion · L**

Goal: Know which core Zene workflows work with native Windows and macOS screen readers and where support still needs work.

Why: The input requests cross-platform screen-reader support and repeatable fixes, with high demand and explicit user reports of workaround burden.

Depends on: Linux screen-reader core-flow verification
Acceptance:
- Run the same bounded core-flow script with NVDA on Windows and VoiceOver on macOS; include JAWS or Narrator only where a supported test host is available.
- Record OS, screen-reader versions, workflow results, and untested combinations in an accessibility matrix.
- Turn confirmed gaps into a per-release checklist and add regression tests for fixes.
Items:
  - `R-accessibility-1` Built-in screen-reader access across desktop platforms - partial (docs/KNOWN-LIMITATIONS.md:2432 (Qt bridge asserted, no screen reader was run); view.announcements (src/gui/ControlCommandsAccessibleEditing.cpp:179); window.accessibility_report); complete: Accessible names and Qt bridge checks exist, but no screen reader has been run on any platform.
  - `R-accessibility-6` Live 12 expands blind users' DAW choice - partial (docs/KNOWN-LIMITATIONS.md:2432 (Qt bridge asserted, no screen reader was run); view.announcements (src/gui/ControlCommandsAccessibleEditing.cpp:179); window.accessibility_report); complete: Cross-platform screen-reader support exists through Qt but is unverified on Windows, macOS and Linux.
  - `R-accessibility-10` Incremental screen-reader fixes in Ableton releases - partial (docs/KNOWN-LIMITATIONS.md:2432 (Qt bridge asserted, no screen reader was run); view.announcements (src/gui/ControlCommandsAccessibleEditing.cpp:179); window.accessibility_report); complete: Ongoing announcement and focus fixes are expected, and Zene has the audit tooling but no screen-reader-driven fix loop yet.
  - `R-workflow-112` Blind users build extensive workarounds when products omit speech - partial (data/manual/index.html:216-229 (Access: Tab reach, screen-reader names, announcements); window.accessibility_report; docs/KNOWN-LIMITATIONS.md:2432 (no screen reader was run)); complete: The accessibility layer exists (names, roles, announcements, keyboard editing), but it has never been tried with a real screen reader.

## Q050-057 Add an audio envelope follower modulation source

**P1 · L06 automation and modulation · feature · L**

Goal: Use a mixer channel's audio level to modulate a destination with adjustable attack, release, and sidechain tap.

Why: Demand is high for R-effects-26 (7 DAWs), medium for R-effects-11 (1) and low for R-mixing-14 (3); the source list has no audio follower and PeakControllerEffect is only a workaround.

Acceptance:
- The modulation create/state commands and Modulators panel can select a follower, its source channel and tap, and attack/release settings; saved projects restore those settings.
- A render test feeds known transients through a source channel and checks the follower's attack/release response and modulation of a target on another track.
- An audio-thread allocation counter reports zero allocations while the follower runs; source routing has a fixed bound and does not add a lock to the audio path.
Items:
  - `R-effects-26` Dedicated modulation systems and expressive sources - partial (include/ModulationLayer.h:96-108 (sine, triangle, square, saw, random, smooth random, steps, envelope, formula); modulator.voice_set, rack.macro_*; no audio-follower source); complete: LFO, random, step, envelope, formula and macro sources exist with per-voice modulation; an audio-following source in the layer is missing.
  - `R-effects-11` Audio envelope-follower modulation source - partial (plugins/PeakControllerEffect and src/core/PeakController.cpp (inherited audio-level controller); include/ModulationLayer.h:96-108 has no follower shape); complete: An audio-following controller exists through the inherited Peak Controller, but not as a source in the modulation layer.
  - `R-mixing-14` Cross-track modulation and flexible sidechains - partial (mixer.sidechain_to (four tap points); modulator.* (LFO, envelope, steps sources only); plugins/PeakControllerEffect (inherited audio-follower controller, not a modulator source)); complete: Audio-to-parameter modulation exists only through the inherited Peak Controller, outside the modulation layer and the agent surface.

## Q050-058 Nested reusable instrument racks

**P1 · L10 devices, instruments, effects, plug-in hosting · feature · L**

Goal: Users can save and reuse nested instrument/effect racks as a playable container.

Why: Rack commands exist but nested containers and reusable playable panels are missing; one request is high demand across 4 DAWs.

Acceptance:
- Nest one rack in another and verify path-based get/set, save/load, and undo.
- Save the nested instrument as a reusable preset and instantiate it on a second track.
Items:
  - `R-instruments-9` Reason Rack sound-building workflow - partial (rack.add_chain, rack.macro_add, rack.zone_add; docs/KNOWN-LIMITATIONS.md:256-285); complete: Racks, parallel chains, macros, zones and layer instruments exist, but the full integrated instrument-building rack surface remains incomplete.
  - `R-instruments-27` Combinator bundles complex device chains as reusable patches - partial (chain.save, chain.apply, rack.add_chain, rack.wrap; docs/KNOWN-LIMITATIONS.md:256-285); complete: Reusable chains, racks, macros and zones exist, but nested Combinator-style instrument panels and fully surfaced patch controls remain incomplete.
  - `R-mixing-3` Nested effect racks - partial (include/Rack.h:50-62 (chain 0 plus EffectChain children; no Rack child type); rack.get_state); complete: Racks can't nest.

## Q050-059 Modular instrument graph and starter modules

**P1 · L10 devices, instruments, effects, plug-in hosting · feature · L**

Goal: Users can build and save a playable instrument from connected oscillator, envelope, filter, and modulation nodes.

Why: Several high-demand requests describe the same missing modular instrument graph, while the current patcher models effect chains only (1 DAW each).

Depends on: Device-chain patcher canvas
Acceptance:
- Create a graph with at least one source, processor, and output and render non-silent audio.
- Save and reload the instrument preset with graph wiring intact; exercise creation and read-back through agent commands.
Items:
  - `R-effects-25` Modular sound-design devices - partial (patcher.set_wiring / patcher.get_state (an effect chain's node graph); docs/LUA-SCRIPT-DEVICES-DESIGN.md (script devices not implemented)); complete: An effect chain can be re-wired as a graph, but there is no modular instrument-building environment.
  - `R-effects-30` Modular device or patching environment - partial (patcher.set_wiring (effect-chain graph); no device-building environment); complete: An effect chain can be patched as a graph, but there is no Patcher-like environment for building devices.
  - `R-instruments-22` The Grid modular environment - partial (patcher.get_state and patcher.set_wiring; rack.* and modulator.* commands; no visual modular patch editor found); complete: Graph wiring, racks and modulation are present through commands, but there is no full integrated modular instrument/effect patching environment.
  - `R-instruments-79` Live-patchable modular synth with code control - partial (patcher.set_wiring edits an effect chain whose node set is fixed; script.run exists; no live modular instrument canvas found.); complete: Live effect-chain wiring and Lua command scripts exist, but users cannot build and play an editable modular instrument graph.

## Q050-060 DAWproject state fidelity and Pro Tools handoff

**P1 · L12 projects, files, import/export, interchange, recovery · completion · L**

Goal: Complete DAWproject fidelity for fades, sends, automation, and allowed plugin-state blobs, then verify the combined AAF/DAWproject handoff workflow.

Why: The umbrella interoperability row records high demand across six DAWs, while the related DAWproject row reports medium demand across six DAWs and missing session state.

Depends on: DAWproject audio clips and media round trip; AAF export and MXF-backed media; AAF import fidelity and missing-media preflight
Acceptance:
- Round-trip fades, sends, automation lanes, and supported plugin-state blobs through DAWproject and compare the resulting session state.
- Run the AAF and DAWproject fixtures used by the Pro Tools handoff workflow and verify each unsupported field is reported.
Items:
  - `R-interop-12` DAWproject interchange - partial (dawproject.import/export/read; docs/DAWPROJECT-INTERCHANGE.md:251-259 (audio clips+media, device state, automation, sends, fades not written; foreign times rounded)); complete: DAWproject round-trips notes, tempo map and strips, but drops audio clips and media, devices, automation, sends and fades.
  - `R-collaboration-2` Pro Tools interoperability and studio adoption - partial (command id interchange.aaf_import; docs/KNOWN-LIMITATIONS.md:2364-2370; command ids dawproject.import and dawproject.export); complete: AAF import and DAWproject exchange provide partial interchange, but there is no Pro Tools native session round-trip and AAF losses remain.

## Q050-061 Customizable menus, toolbars, and mouse modifiers

**P1 · L13 customisation, shortcuts, themes, layout · completion · L**

Goal: Edit and persist menu and toolbar entries and assign mouse modifiers across supported editors.

Why: The matching toolbar/menu findings cite seven DAWs, while window.header_set only changes header buttons and menus remain fixed; mouse modifiers are limited to arranger tools.

Acceptance:
- Open the menu/toolbar editor, add and reorder registered commands, hide an existing menu item, and verify the layout persists after restart.
- Set a mouse modifier in the arranger and another supported editor, restart, and confirm each invokes the mapped action.
Items:
  - `B-WF-116` Customisable toolbars and menus - partial (window.header_get/header_set (src/gui/ControlCommandsLayouts.cpp:250); no menu customisation command in commands_snapshot.json); complete: The header toolbar's buttons can be hidden and extra command buttons added, but menus cannot be customised.
  - `R-ui-5` Customizable toolbars and menus - partial (window.header_set (hide header buttons, add buttons that run commands); menus are fixed (docs/UI-SHELL.md:15)); complete: The header is customisable, but menus and other toolbars are not.
  - `R-ui-27` Customize themes, actions, and mouse behavior - partial (shortcuts.set; theme.set_token; window.layout_save; window.header_set; arrangement.modifier_tools (arranger modifier tools only)); complete: Themes, shortcuts, layouts and the header are customisable; menus and general mouse-modifier behaviour are not.

## Q050-062 Choose stem-separation quality and compare output

**P1 · L05 audio editing, warping, stretching, fades · feature · L**

Goal: Users can choose fast or higher-quality separation and compare each result with its retained source.

Why: Quality controls are high demand across 3 DAWs, but stem.job_start accepts only source and segment_frames and real-model SDR has not been measured.

Acceptance:
- Expose a quality choice or overlap/shift count in stem.job_start and the clip menu, and verify the selected values in job state.
- Measure real-model SDR on a fixed reference set for each quality level and record quality/time results.
- Keep the source associated with each separated result and verify source/result comparison in the UI and agent state.
Items:
  - `R-ai-2` Stem separation quality and controls - partial (stem.job_start args only source, segment_frames (commands_snapshot.json); doc/STEM-SPLIT.md:241-245 (real-model quality not measured)); complete: Separation runs with a fixed model and no quality settings, and real HTDemucs quality has never been measured.
  - `R-performance-55` Stem quality versus processing time - partial (stem.get_state (one pinned HTDemucs 4-stem model); include/StemSeparation/OnnxRuntimeStemSeparator.h:36); complete: There is one fixed model and setting, so users cannot trade speed for quality or compare results.
  - `R-workflow-102` Native stem separation with selectable quality and speed - partial (command stem.job_start args are 'source' and 'segment_frames' only (one HTDemucs model)); complete: Separation runs in the session but offers no fast-versus-quality choice.

## Q050-063 Author warp markers with on-clip preview

**P1 · L05 audio editing, warping, stretching, fades · completion · L**

Goal: Users can draw, move, snap, and preview warp markers directly over the waveform.

Why: Marker editing is medium demand across 10 DAWs, but markers are currently managed through forms and lists with no on-clip guides or stretch preview.

Acceptance:
- Add a marker by double-click and drag it with grid or transient snap; show each marker as a line through the waveform.
- Move a marker and verify the approximate waveform preview updates during the drag and the final render matches the committed position.
- Drive add/move through warp.add and warp.move and verify the UI and agent read back the same marker map.
Items:
  - `R-editing-63` Waveform preview while stretching - partial (src/gui/clips/SampleClipView.cpp:206; docs/KNOWN-LIMITATIONS.md:1567); complete: The waveform remains visible while editing, but the warp and stretch controls are not exposed as an interactive waveform editor.
  - `R-editing-73` Warp markers cannot be authored in the GUI - partial (src/gui/shell/ShellCommandMenus.cpp:205 Add Warp Marker... form; src/gui/CommandLists.cpp:79-87 Warp Markers list; no marker drawn or dragged on the clip (rg warp src/gui/clips)); complete: Warp markers can now be added, moved and removed through a form and a list, but they are not drawn on the clip and cannot be dragged or snapped.
  - `R-editing-84` Elastic audio with user-placed warp markers - partial (warp.add/warp.move only via form and list (src/gui/shell/ShellCommandMenus.cpp:205; src/gui/CommandLists.cpp:79-87); warp.quantize auto-places markers); complete: Markers can be placed by form or auto-quantised, but not dragged to the grid on the waveform.
  - `R-editing-138` Clear warp-marker-to-waveform guides - partial (warp markers reachable only by form and list (src/gui/shell/ShellCommandMenus.cpp:205; src/gui/CommandLists.cpp:79-87); nothing in src/gui/clips draws a marker); complete: Warp markers are not drawn on the clip, so there is no guide line from marker to waveform.

## Q050-064 Note editor input ergonomics

**P1 · L07 note editor, MIDI editing, scales, chords, articulations · completion · M**

Goal: Draw notes with a pitch preview, adjust pitch while drawing, see a cursor for each active tool, and open clips at a useful zoom and row range.

Why: High demand across two DAWs for note creation; current editor lacks pointer preview, useful per-clip defaults, and persistent tool cursors.

Acceptance:
- Pointer tests verify the preview, pitch adjustment, and draw, erase, slice, and audition cursors.
- Opening clips with different note ranges selects a useful default zoom and visible row range.
- The view settings and active tool are reflected by window.get_state.
Items:
  - `R-editing-24` Flexible piano-roll note creation - partial (docs/NOTE-EDITOR.md:73; docs/NOTE-EDITOR.md:78; docs/NOTE-EDITOR.md:87); complete: The editor has separate Play and Draw tools and modifier-based selection, but pitch audition is not integrated into note creation.
  - `R-editing-65` Piano-roll default zoom and note-entry flow - partial (docs/NOTE-EDITOR.md:43; docs/NOTE-EDITOR.md:73; docs/NOTE-EDITOR.md:154); complete: The editor offers fit-to-notes and continuous zoom, but does not automatically choose a per-clip horizontal and vertical view on every open.
  - `R-ui-29` Persistent active-tool cursor feedback - partial (src/gui/clips/ClipView.cpp:480-488 (arranger cursors follow the action); src/gui/noteeditor/ has no per-tool setCursor (only NoteEditorParts.cpp:236 on the loop ruler)); complete: The note editor canvas keeps the arrow cursor whatever tool is active (the notation case itself is deferred).

## Q050-065 Place retrospective captures on take lanes

**P1 · L03 recording, takes, comping, inputs · completion · L**

Goal: Users can place recent audio and MIDI performances onto the selected or armed track as take lanes with one undo step.

Why: Retrospective capture demand spans 4 DAWs for audio, but audio currently writes a WAV and MIDI lands separately rather than in a shared take-lane workflow.

Depends on: Record and comp MIDI takes on instrument tracks; Show take lanes and waveforms in the timeline
Acceptance:
- Control-socket tests capture recent audio and MIDI, place each on the target track's take lanes, and verify the timeline position and source data.
- One control.undo removes each placed take and redo restores it; no-capture cases return a typed refusal without creating an empty clip.
Items:
  - `R-recording-6` Audio pre-record buffer - partial (record.retro_capture_arm; record.retro_capture_to_take ('NOT inserted into the session'); src/gui/shell/ShellCommandMenus.cpp:141-143 (Keep Listening, Save Last Audio as a Take...); plans/TRACKER-050.md:140 (WF-083)); complete: The pre-record buffer exists with menu items, but the saved take is a file that the user must import by hand.
  - `R-recording-23` Flashback capture of recent performances - partial (midi.retro_capture_to_clip (MIDI lands as a clip); record.retro_capture_to_take (audio written to a file, not placed); no retro capture into take lanes); complete: Recent MIDI lands in a clip, but recovered audio is not placed and repeated passes are not organised as takes.
  - `R-recording-64` Retroactive MIDI/audio recording - partial (src/gui/shell/ShellCommandMenus.cpp:141-143 (Last MIDI to a New Clip; Save Last Audio as a Take... writes a WAV)); complete: Retrospective MIDI lands as a clip, but retrospective audio only writes a WAV file.

## Q050-066 Drum-rack pad workflow

**P1 · L10 devices, instruments, effects, plug-in hosting · feature · L**

Goal: Users can edit sample pads, per-pad chains, pad links, and one-shot slots in a drum-rack view.

Why: The pad editor is missing despite existing rack-layer primitives, and the requests include a 4-DAW comparison plus low-demand pad workflows.

Acceptance:
- Create pads from samples, add a per-pad chain, set a pad link, and play a one-shot slot.
- Transfer a pad between sampler and launcher models and verify all pad state through save/load and agent commands.
Items:
  - `B-MX-111` Drum rack with per-pad chains - partial (src/core/ControlCommandsDrumRack.cpp:24-28, 100-120, 202-208; command id track.drum_rack); complete: The command creates key-mapped sampler pads with separate mixer insert chains, but there is no dedicated visible pad-grid editor.
  - `R-instruments-55` Koala pad link and sound layering - partial (rack.layer_add; rack.layer_zone_add; no pad-to-pad trigger or sample-offset command found in the command snapshot.); complete: Static instrument layering is implemented, but one pad cannot trigger another pad or set its playback offset.
  - `R-instruments-66` Make Nano match Playfield slots - partial (rack.layer_add; rack.layer_zone_add; clip.slice_to_instrument; searched plugins/ for Nano and Playfield instruments.); complete: Instrument layers and slicing exist, but there are no interchangeable Nano and Playfield-style pad devices.
  - `R-instruments-73` Playfield with per-pad effects - partial (rack.layer_add; rack.layer_zone_add; rack.layers reports layers sharing a track channel; no per-pad effect device found.); complete: Layer zones can select instruments, but there is no sample-pad rack with independent per-pad effects.
  - `R-instruments-77` Sampler with multiple one-shot stems - partial (rack.layer_add; rack.layer_zone_add; clip.slice_to_instrument; no multi-one-shot stem rack with per-stem sample slots found.); complete: Layered instruments and slicing exist, but there is no multi-stem one-shot rack with repeated sample slots per stem.

## Q050-067 Common DAW shortcut presets and migration guide

**P1 · L13 customisation, shortcuts, themes, layout · feature · M**

Goal: Choose a familiar DAW keymap preset and look up how its common commands map to Zene.

Why: High demand spans six DAWs and the matching migration request spans four, while no built-in DAW keymap presets ship.

Depends on: Context-aware shortcuts and plug-in actions
Acceptance:
- Install at least three named keymap presets and verify each maps a curated command set without silently replacing unrelated user bindings.
- Open the migration guide and confirm it maps source-DAW concepts and commands to Zene actions.
Items:
  - `R-workflow-7` Lower the productivity dip when changing DAWs - partial (command ids shortcuts.import, app.tours, app.ask, window.layout_apply; find data -iname '*shortcut*' finds no keymap presets; data/manual/index.html has no 'coming from' guide); complete: Rebindable shortcuts, tours, Help > Ask and layouts exist, but no ready keymaps or 'coming from Ableton/FL/REAPER' guidance shorten a switch.
  - `R-workflow-33` Migrating from Ableton-style workflows can make REAPER feel steep - partial (command ids shortcuts.import, theme.import, window.layout_save; no shipped DAW keymaps in data/); complete: Themes, layouts and key maps are customisable, but no ready keymaps imitate other DAWs.

## Q050-068 Keyboard traversal through mixer controls

**P1 · L14 help, onboarding, accessibility · feature · M**

Goal: Move through mixer strips and controls with predictable keyboard focus and accessible names.

Why: The source rates keyboard mixer access as high demand across three DAWs, and KNOWN-LIMITATIONS.md:2102-2104 records unfocusable faders and LED buttons.

Acceptance:
- Verify faders and LED buttons accept focus and keyboard adjustment and appear in the accessibility walk.
- Use Ctrl+Tab and Ctrl+Shift+Tab to move to the same control on adjacent channels, and Tab within a strip; verify focus and the control name are announced.
- Add a keyboard-navigation regression test covering forward, reverse, and boundary behavior.
Items:
  - `R-ui-25` Live keyboard focus traversal and wrap - partial (docs/KNOWN-LIMITATIONS.md:2094-2104 (Tab reaches 155 named controls, knobs keyboard-operable; faders and LED buttons take no keyboard focus); window.focus_panel (F6/Shift+F6)); complete: Tab and panel traversal work, but faders and LED buttons cannot take focus.
  - `R-workflow-24` Users compare DAWs by setup burden as well as feature coverage - partial (docs/KNOWN-LIMITATIONS.md:2102-2104 (faders and LED buttons take no focus), :2432 (no screen reader was run)); complete: Accessible names and keyboard knobs exist, but faders/LED buttons lack focus and no screen reader has been run against the app.
  - `R-mixing-31` Keyboard navigation through mixer controls - partial (src/gui/MixerView.cpp:618-665 (Left/Right select channel, Alt moves it, Up/Down adjust fader); MixerChannelView.cpp:224 (strip takes focus)); complete: Keyboard users can move between channels and nudge the fader, but can't step through a row of controls (mute, solo, pan, sends) across channels.

## Q050-069 Live theme and palette updates

**P1 · L13 customisation, shortcuts, themes, layout · completion · M**

Goal: Apply imported or edited themes to the running interface without restarting Zene.

Why: High demand is reported across four DAWs, and theme.set_token/import/export currently apply changes only after restart.

Acceptance:
- Call theme.set_token while the app is running and verify open widgets re-style before restart.
- Import a theme at runtime and confirm its token values and widget palette persist after restart.
Items:
  - `R-ui-2` Customizable interface themes and palettes - partial (theme.set_token/import/export; docs/KNOWN-LIMITATIONS.md:2462 (theme changes apply at the next start)); complete: Colour roles and palettes can be edited and shared, but only after a restart.
  - `R-ui-13` Immediate theme and palette changes - partial (docs/KNOWN-LIMITATIONS.md:2462; theme.set_token ('Applies from the next start')); complete: Theme changes need a restart.

## Q050-070 Project key and scale-degree context

**P1 · L07 note editor, MIDI editing, scales, chords, articulations · completion · L**

Goal: Save project and timeline key changes and show the active scale degree in the piano-roll rows and chord-lane labels.

Why: Medium demand and four DAWs for scale-degree display; scale.set currently uses deliberately non-serialized context and the editor shows no degree labels.

Acceptance:
- Project key and timeline key changes survive save, reload, undo, and transport seeks.
- The note editor and chord ruler show degree labels that update at the active timeline position.
- Headless scale and chord commands read back the same key context as the UI.
Items:
  - `R-editing-47` Show scale degrees or musical notation in the Piano Roll - partial (docs/NOTE-EDITOR.md:104; docs/NOTE-EDITOR.md:106); complete: Scale highlighting and scale folding exist, but the key column does not show numbered scale degrees.
  - `R-editing-81` Key signature and scale-aware composition - partial (scale.set / keep_in_scale / fold scale / note.transpose_in_key; scale.set description: the scale context is 'deliberately not serialized'); complete: Scale-aware drawing, folding and transposing exist, but the key is session state and not a saved project key signature.
  - `R-editing-97` Global transpose, scale editing and Nashville numbers - partial (header transpose readout bound to Song masterPitch (src/gui/shell/TransportReadouts.cpp:390); scale.define; chord track shows chord names only); complete: Global transpose and scale definition exist; Nashville-number (or roman-numeral) display of chords is missing.

## Q050-071 Third-party plug-in compatibility matrix

**P1 · L10 devices, instruments, effects, plug-in hosting · completion · L**

Goal: Users can check tested native VST3, CLAP, and LV2 plug-ins and package combinations before adopting them.

Why: Only in-tree fixtures are proven today and the requested matrix spans 8 DAWs at medium demand.

Acceptance:
- Run and publish a reproducible matrix with host format, OS/package, load, editor, state round-trip, and known failures.
- Keep third-party failures separate from in-tree fixtures and rerun the named regressions on the next build.
Items:
  - `R-interop-22` Bridged plug-in host and packaging compatibility - partial (docs/KNOWN-LIMITATIONS.md:1639-1642, :1888 (only in-tree fixtures proven); docs/PLUGIN-HOSTING-IN-RELEASE.md); complete: Host formats exist, but there's no published list of tested host, format and package combinations.
  - `R-interop-29` ACID VST3 plug-in compatibility pain - partial (plugin.list; docs/KNOWN-LIMITATIONS.md:1888 and :156-167 (no third-party VST3/CLAP loaded; the VST3/CLAP effect factory entry point was only fixed 2026-10-07)); complete: The VST3 host has only been proven with in-tree fixtures, so its reliability with real plug-ins is unknown.

## Q050-072 Speak pattern, track, and mixer state changes

**P1 · L14 help, onboarding, accessibility · feature · M**

Goal: Hear which pattern step or track state changed while navigating and mixing.

Why: These rows report high demand for spoken step and mix state, while current announcements cover errors and recording/save events rather than those state changes.

Depends on: Stage 1: keyboard and switch-accessible step sequencer
Acceptance:
- With announcements enabled, toggle a pattern step and hear its position and state; verify the same action is announced when made through a bound controller.
- Navigate tracks and folders and hear the track name, folder context, mute, solo, record-arm, input-monitor, and polarity state.
- Verify shortcut and mouse changes use the same announcement path, and that errors-only and off settings suppress routine state announcements.
Items:
  - `R-accessibility-17` Track and folder state narration - partial (src/gui/tracks/TrackView.cpp:166 (accessible description 'In folder ...'); docs/KNOWN-LIMITATIONS.md:2432); complete: Track names and folder paths are exposed to accessibility APIs, but navigation narration with a real screen reader is unverified.
  - `R-mixing-15` Speak track state and mix changes - partial (src/gui/AccessibleAnnouncements.cpp:25-32 (speaks errors, record start/stop, save/open, long jobs; values only for the focused control)); complete: Mute, solo, arm, monitor and polarity changes on tracks aren't announced unless the control has focus.
  - `R-workflow-95` Blind FL producer seeks speech feedback for step sequencer and mixer - partial (command id view.announcements; rg a11y::announce over src/gui/editors/PatternEditor.cpp and mixer views finds none); complete: Errors, recording and focused control values are spoken, but the step sequencer's steps and the mixer's state are not narrated.

## Q050-073 Edit clip fades and apply them in batches

**P1 · L05 audio editing, warping, stretching, fades · completion · L**

Goal: Users can shape clip fades and crossfades on the timeline and apply one range and curve to several clips at once.

Why: Fade editing is high demand across 9 DAWs, and batch range operations currently accept one clip while crossfades remain numeric.

Depends on: Preserve frame precision through warped timeline edits
Acceptance:
- Drag fade handles, set exact fade lengths, and choose curve shapes without opening a blocking dialog.
- Select multiple clips and apply the same range and curve; verify all affected clips, undo, and agent command state.
- Render fade and crossfade fixtures and check that the curves meet without a discontinuity.
Items:
  - `B-ED-178` Fades from a range, batch fades - partial (sample.edit_range; window.sample_editor; src/gui/SampleEditorDialog.cpp:187-190,275; one clip argument); complete: The Sample Editor applies fades to a selected range, but there is no batch action across multiple clips.
  - `R-editing-22` Audio fades and crossfade controls - partial (docs/KNOWN-LIMITATIONS.md:335-343,2617-2624; command ids clip.set_fade and clip.crossfade); complete: Fade menus and a numeric crossfade editor provide lengths and curves, but direct fade and crossfade handles are still absent.

## Q050-074 Expose track CPU, dropout and audio-device diagnostics

**P1 · L15 platform: performance, stability, Linux audio, install, hardware controllers · feature · L**

Goal: Users can identify expensive tracks and see CPU overloads, xruns, buffer size, sample rates and latency in the UI and control surface.

Why: The current CPU view omits instrument synthesis and overload history, while xruns and device timing are not exposed; related demand is medium, and the diagnostic view closes a command/UI gap.

Acceptance:
- A generated multi-track session reports each instrument’s synthesis cost in mixer.cpu_load and track.overview.
- An overload test records song-position marks and shows them in the CPU widget; a JACK/ALSA test reports late periods, xruns, sample rates, period size and device latency.
- A headless command test reads the same diagnostic state shown in the UI.
Items:
  - `B-WF-104` CPU load per track or device - partial (mixer.cpu_load ('An instrument's own synthesis runs before the mixer and is in engine_percent, not in its channel's figure'); src/core/ControlCommandsTrackLooks.cpp:228); complete: Per-channel and per-device effect load is reported, but instrument synthesis is counted only engine-wide.
  - `R-performance-71` Visible CPU and dropout diagnostics - partial (mixer.cpu_load (per channel/device); src/gui/widgets/CPULoadWidget.cpp:91-106 (DSP total and four detail loads, no overload history); rg 'xrun' over src/gui: none); complete: Load is shown per channel and device, but overloads are not counted or marked at the song position where they happened.
  - `R-performance-48` Xrun and latency diagnostics - partial (src/gui/widgets/CPULoadWidget.cpp:91-106; src/core/AudioEngine.cpp:212 (criticalXRuns used internally only); rg 'xrun' over the command list: none; audio.io_list); complete: Engine load is visible, but no xrun/late-period count and no measured device latency is shown or answered through the surface.
  - `R-performance-78` Audio performance diagnostics - partial (mixer.cpu_load; audio.io_list; src/gui/widgets/CPULoadWidget.cpp:91-106 (no late-buffer or clock-rate readout)); complete: Load is reported, but not the device/project sample-rate agreement, buffer timing or late-buffer counts.

## Q050-075 Expose tag and embedded-metadata search in the browser

**P1 · L11 browser, samples, presets, library · completion · S**

Goal: Users can search library files by tags and embedded audio metadata from the Search view.

Why: R-workflow-4 cites high demand across five DAWs; queryArgs currently omits probe, and the limitation text still says tag search is socket-only.

Acceptance:
- A Search file metadata toggle sends probe=true to browser.query and returns results matching embedded title, artist, or numeric audio metadata.
- GUI checks confirm tag: search remains available and metadata probing stays within the command's documented probe limit.
- KNOWN-LIMITATIONS.md describes tag search as available in the GUI, metadata search as available through the new toggle, and waveform peaks as socket-only.
Items:
  - `R-workflow-4` Browser tag and metadata search - partial (src/gui/BrowserSearchView.cpp:104 (tag:/kind: search), :146 Tags column, :268-271 queryArgs sends no 'probe'; docs/KNOWN-LIMITATIONS.md:717-719 still says tag search is socket-only); complete: Tag search, kinds and a Tags column are in the browser, but searching by embedded file metadata (rate, length, artist, genre) is still socket-only and the limitations page is stale.
  - `R-workflow-113` Browser tag and metadata search are hidden from the GUI - partial (src/gui/BrowserSearchView.cpp:104 (tag: search) and :476 (Find Similar Sounds); src/gui/shell/WaveformPreview.cpp:73 (browser.peaks); queryArgs at :268 never sets 'probe'; docs/KNOWN-LIMITATIONS.md:718-720 still says tag search is socket-only); complete: Tag search and waveforms now reach the GUI, but searching embedded metadata (rate, length, title, genre) doesn't, and KNOWN-LIMITATIONS.md is stale.

## Q050-076 Stem delivery with bus selection and pre/post-FX taps

**P1 · L12 projects, files, import/export, interchange, recovery · feature · L**

Goal: Render selected tracks or mixer buses as wet, dry, or both stems through the control surface and export UI.

Why: The group includes medium demand across four DAWs and documents that the current per-track stem path lacks bus selection and wet/dry taps.

Acceptance:
- Render selected track and return/bus channels and verify each file contains only the requested signal path.
- Choose pre_fx, post_fx, and both taps and verify dry/wet output files differ only by the selected effect chain.
- Run the same export from the UI and command surface with matching format settings.
Items:
  - `R-workflow-43` One-step batch and stem delivery - partial (src/gui/MainWindow.cpp:342 Export Tracks (render.stems); docs/KNOWN-LIMITATIONS.md:1308-1312 (per track only, no bus stems); render.stems args format/out/tail_bars only); complete: One-step stem export exists, but stems cannot be chosen per track or rendered per bus.
  - `R-mixing-46` Direct aux and effect-return stem export - partial (render.stems (per track, post-fader incl. own sends); docs/KNOWN-LIMITATIONS.md:1308-1312 ('render.stems renders PER TRACK ... Bus-level stems are not claimed')); complete: Track stems include each track's own send tails, but a return/bus channel cannot be exported as its own stem.
  - `R-interop-39` Per-track wet and dry audio exports - partial (render.stems args: format, out, tail_bars only (post-fader, post-effects); record.route_source has pre-FX taps (TRACKER-050.md:159)); complete: Stems are post-effects only; there's no dry or pre-FX option.

## Q050-077 Preserve frame precision through warped timeline edits

**P1 · L05 audio editing, warping, stretching, fades · completion · L**

Goal: Users can edit warped audio at source-frame precision, including splits, comp slips, pre-fades, and crossfade ramps.

Why: Precision editing is high demand across 4 DAWs, and current limitation notes describe timing loss on warped clips while crossfade ramps remain numeric.

Acceptance:
- Apply frame-addressed edits before and after a warp change and verify split, pre-fade, and comp-slip positions remain on the requested source frames.
- Draw and move a transient on a warped/source-tempo clip and verify its waveform and marker display stay aligned at frame resolution.
- Drag a crossfade ramp on the timeline and verify precise endpoints, rendered audio, undo, and agent state read-back.
Items:
  - `R-editing-40` Precision audio editing - partial (docs/KNOWN-LIMITATIONS.md:2355; docs/KNOWN-LIMITATIONS.md:2621); complete: Frame edits on warped or source-tempo clips and direct crossfade handles are still absent from the otherwise precise clip-editing workflow.
  - `R-editing-104` Frame-precise editing on warped audio - partial (docs/KNOWN-LIMITATIONS.md:2355; docs/KNOWN-LIMITATIONS.md:2356); complete: Frame edits are refused on warped or source-tempo clips and waveform overlays still ignore the sub-tick frame lead.

## Q050-078 Keyboard-first tracker editor

**P1 · L07 note editor, MIDI editing, scales, chords, articulations · feature · L**

Goal: Sequence MIDI clips in a tracker grid with keyboard navigation and note entry across rows and voices.

Why: High demand across three DAWs; the requested keyboard-first tracker workflow has no editor in the merged tree.

Acceptance:
- A tracker view shows one row per grid step and one column per voice, with keyboard-only navigation and note entry.
- A MIDI fixture verifies row timing, pitch, velocity, save and reload, and one-step undo.
- The view edits the same clip state exposed by the control surface.
Items:
  - `R-workflow-2` Keyboard-first tracker sequencing - missing (rg -i 'tracker' over src/gui and include finds no tracker/pattern-grid editor (only MIDI clock 'tempo tracker'); include/StepRecorder.h is piano-roll step input); implement: There is no vertical, keyboard-first tracker grid for note entry.
  - `R-workflow-55` Tracker workflow helps finish arrangements - partial (src/gui/editors/PatternEditor.cpp (step pattern editor); no tracker view (rg -i tracker over src/gui)); complete: Patterns can be typed and duplicated in the step pattern editor, but there is no tracker grid.

## Q050-079 Reason Rack compatibility fixture

**P1 · L10 devices, instruments, effects, plug-in hosting · completion · M**

Goal: Users can check a tested Reason Rack Plugin VST3 combination and its editor/state behavior.

Why: Reason Rack is not covered by any third-party fixture and the request is high demand across 6 DAWs.

Depends on: Third-party plug-in compatibility matrix
Acceptance:
- Record the tested OS, host version, plug-in build, load, editor resize, state round-trip, and CPU outcome.
- Add a repeatable fixture or clearly report the exact proprietary dependency if automation is unavailable.
Items:
  - `R-interop-17` Reason Rack plug-in in other DAWs - partial (plugin.list / plugin.load (generic VST3/CLAP host); docs/KNOWN-LIMITATIONS.md:156 and :1888 (no third-party plug-in loaded)); complete: The generic VST3 host should load Reason Rack, but no third-party plug-in has been tried on the Windows or macOS builds.
  - `R-interop-55` Reason plug-in compatibility refinements - partial (plugin.list / plugin.load; docs/KNOWN-LIMITATIONS.md:156-167 (VST3/CLAP effects only reachable through the factory since 2026-10-07; no third-party plug-in loaded)); complete: The generic VST3/CLAP host paths are new and untested with third-party plug-ins; Reason file compatibility is outside Zene.

## Q050-080 Speak timeline position, selection, and markers

**P1 · L14 help, onboarding, accessibility · feature · M**

Goal: Hear the selected item, edit position, transport location, and named project markers.

Why: The input rates spoken selection feedback as high demand, and the current announcement implementation does not speak the playhead or marker names.

Acceptance:
- Select items and move the edit cursor; verify the announcement includes the item and bar/beat position.
- Start, stop, locate, and scrub the transport and verify the configured position announcement.
- Jump to a marker and play across one; verify its name is announced only when view.announcements permits it.
Items:
  - `R-editing-32` Speak item selection and edit position - partial (docs/KNOWN-LIMITATIONS.md:2426; docs/KNOWN-LIMITATIONS.md:2432; view.announcements); complete: Screen-reader announcements and editable lists exist, but spoken item position and edit-cursor location are not established as a complete workflow.
  - `R-editing-116` Navigate envelopes and markers with speech - partial (marker.jump; view.announcements; automation points named to a screen reader (docs/KNOWN-LIMITATIONS.md:2520); no announcement of markers in src (rg announce + marker found none)); complete: Automation points are spoken during keyboard editing, but jumping to or playing past a marker announces nothing.
  - `R-recording-46` Logic announces transport position changes - partial (src/gui/AccessibleAnnouncements.cpp:32 (the playhead is never spoken), :77-78 (only 'Recording'/'Recording stopped')); complete: Recording start/stop is announced but playhead position on play/stop/scrub is not.

## Q050-081 Add controller scripting and a mapping editor

**P1 · L15 platform: performance, stability, Linux audio, install, hardware controllers · feature · L**

Goal: Users can script MIDI input into commands, edit mappings and send feedback from Lua control-surface scripts.

Why: Three rows describe the same missing Lua controller-script hook, and the mapping editor would expose currently command-only controller behavior; demand ranges medium to low.

Acceptance:
- A Lua example receives MIDI from a bound port, invokes a registered command and sends feedback to that port.
- The mapping editor lists, edits and persists bindings; its actions have stable command IDs and headless tests.
- Script errors and port loss are bounded and reported without blocking the audio thread.
Items:
  - `R-workflow-36` Flexible MIDI controller mapping and scripting - partial (src/gui/ControllerTemplateMenu.cpp:72-97 (templates), command ids controller.map, remote.connect, surface.mcu_connect; docs/KNOWN-LIMITATIONS.md:1667-1704); complete: Mapping, soft takeover, feedback, remote pages, MCU and reusable templates exist, but there is no controller scripting API or custom-layout editor.
  - `R-workflow-195` Requests for broader controller support and feedback - partial (surface.mcu_* (Mackie Control); controller.feedback / soft_takeover / template_*; remote.connect output feedback; no per-device scripting API); complete: Zene has generic mapping with feedback and MCU support, but no way to write a device-specific controller script.
  - `R-workflow-211` Controller support - partial (surface.mcu_connect, remote.connect, controller.map/template_save, midi.learn_toggle, link.set_enabled; docs/specs/SPEC-lua-api-v0.md:98 (MIDI-in only a polled v0 example); docs/KNOWN-LIMITATIONS.md:1690 (OSC is out)); complete: MCU, remote pages, MIDI learn, mapping templates and Ableton Link exist, but there is no scriptable controller-integration API (user scripts handling a controller's MIDI) and no OSC.

## Q050-082 Correct the Follow Action editor limitation in the docs

**P1 · L02 launcher / session clips / performance · docs · S**

Goal: Tell users that per-cell Follow Action chains can be edited from the launcher grid.

Why: Demand is high across 5 DAWs, and the current docs understate an existing UI feature: SessionGridMenus.cpp exposes the per-cell chain editor.

Acceptance:
- Update the stale KNOWN-LIMITATIONS entry that says per-cell chain editing is socket-only.
- Check the cell context menu exposes Follow Action Chain... and ensure the docs accurately distinguish cell-chain editing from the session-wide Follow Actions switch.
Items:
  - `R-workflow-189` Launcher cell editing and launch controls - have (src/gui/FollowActionEditor.cpp:115-133; src/gui/SessionGridMenus.cpp:127-130,161,203 (cell and scene chain editor); docs/KNOWN-LIMITATIONS.md:114 still says per-cell chain editing is socket-only); docs: The launcher now has a per-cell and per-scene Follow Action chain editor, so the limitations page is stale.
  - `R-workflow-114` Clip Follow Action editing is socket-only - have (src/gui/SessionGridMenus.cpp:159-161 (per-cell Follow Actions switch and chain entry, WF-061); docs/KNOWN-LIMITATIONS.md:114 still says per-cell chain editing is socket-only); docs: Each cell's Follow Action chain can now be edited from its context menu, so the KNOWN-LIMITATIONS.md line is stale.

## Q050-083 Multi-track MIDI editor

**P1 · L07 note editor, MIDI editing, scales, chords, articulations · completion · L**

Goal: Open selected MIDI clips together and select, move, resize, and edit notes across tracks in one editor.

Why: Medium demand across six DAWs; note.across can edit several clips, but the editor selection and view remain single-clip.

Acceptance:
- The editor displays notes from multiple selected clips with track identity and supports editing across them.
- A control-socket test edits notes in two clips and verifies a single undo restores both.
- The editor opens headlessly through the same registered selection and edit commands.
Items:
  - `R-editing-76` Multi-track MIDI editing - partial (command id note.across; src/core/ControlCommandsNoteAcross.cpp:143-159; docs/NOTE-EDITOR.md:110-111); complete: note.across performs supported edits across clips and tracks, but the piano roll opens one clip and only ghosts other clips on the same track.
  - `R-editing-72` MIDI editing is region-scoped - partial (note.across (several clips, one undo); docs/KNOWN-LIMITATIONS.md:2446 (the note editor selection holds one clip)); complete: Batch commands edit several clips at once and linked clips share edits, but the note editor canvas edits one clip at a time.

## Q050-084 Layered-instrument note and output routing

**P1 · L10 devices, instruments, effects, plug-in hosting · feature · L**

Goal: Users can send chord/arpeggiator notes through instrument layers and route layer effects to separate mixer channels.

Why: Layer commands exist but chord/arp forwarding and per-layer processing/routing are missing; one request is medium demand across 7 DAWs.

Acceptance:
- Render chord and arpeggiator input through multiple layers and verify independent effects and channel routing.
- Persist layer routing and provide headless commands for read-back and changes.
Items:
  - `R-instruments-12` Instrument layers need chord and arpeggio note forwarding - partial (rack.layer_add, rack.layer_set; docs/KNOWN-LIMITATIONS.md:2324-2340); complete: Layer controls, zones and UI exist, but generated chord and arpeggio notes bypass the layers.
  - `R-instruments-72` Layered instrument composites - partial (rack.layer_add and rack.layers support instrument layers; each layer plays to the track mixer channel rather than a separate chain or strip.); complete: Instrument layering exists, but each layer cannot own a separate effect chain and mixer strip.

## Q050-085 Complete keyboard and MIDI launcher mappings

**P1 · L02 launcher / session clips / performance · feature · M**

Goal: Bind next or selected scene, individual cells and Follow Action controls to keyboard shortcuts or MIDI notes.

Why: Demand is medium across 5 DAWs; current bindings cover slots, scenes and Stop All, while next/selected-scene actions and Follow Actions are not bindable.

Acceptance:
- The shortcut map lists and invokes next-scene, selected-scene and any-cell launcher actions.
- session.bind can learn, save, read back and dispatch scene, cell, global Follow Actions and per-cell Follow Actions actions from simulated MIDI input.
- The launcher mapping UI configures the same registered actions used by shortcuts and the agent command surface.
Items:
  - `R-workflow-32` Keyboard and MIDI launcher bindings - partial (src/gui/RecordLaunchMenus.cpp:161-166 (bind note, learn, bindings); docs/KNOWN-LIMITATIONS.md:2688-2694); complete: MIDI learn binds slots, scenes and Stop All, but keys reach only Launch Scene 1-8/Stop All, perform keys are fixed, and next/selected scene cannot be bound.
  - `R-workflow-76` Follow Actions from Push - partial (command id session.bind actions are slot, scene and stop_all only; Follow Actions toggle is session.set_follow_actions); complete: Follow Actions can be switched in the launcher but not from a controller note.

## Q050-086 Screen-reader access to hosted plug-in parameters

**P1 · L14 help, onboarding, accessibility · feature · M**

Goal: Inspect and change hosted plug-in parameters through Zene's accessible device controls without opening the plug-in's foreign editor.

Why: The complex-workflow gap is rated high demand, and foreign plug-in windows are documented as uneven for screen-reader use.

Depends on: Keyboard editing for waveform and MPE lanes
Acceptance:
- Open a hosted plug-in's generic device card and reach each parameter by keyboard; verify its accessible name, current value, bounds, and changed value.
- Run the same controls with a screen reader and add a hosted-plug-in fixture test for parameter discovery and editing.
- Confirm the waveform and MPE gaps referenced by R-accessibility-3 are covered by the prerequisite lane.
Items:
  - `R-accessibility-3` Screen-reader coverage gaps in complex DAW workflows - partial (docs/KNOWN-LIMITATIONS.md:2430-2432 (waveform takes no keys, MPE lane cursor-only, no screen reader run); plug-in editors are foreign windows); complete: Canvases now have item lists and keyboard cursors, but the sample waveform, MPE lanes and third-party plug-in UIs remain uneven for screen readers.
  - `R-accessibility-9` Screen-reader-first action discovery - partial (window.command_palette; docs/KNOWN-LIMITATIONS.md:2432 (Qt bridge asserted, no screen reader was run); view.announcements (src/gui/ControlCommandsAccessibleEditing.cpp:179); window.accessibility_report); complete: Actions are discoverable by keyboard and named, but screen-reader-first parameter editing has not been verified, especially for plug-ins with inaccessible GUIs.

## Q050-087 Device-chain patcher canvas

**P1 · L10 devices, instruments, effects, plug-in hosting · feature · L**

Goal: Users can inspect and edit a device chain as a visible node-and-cable graph.

Why: The patcher graph exists only as commands and has no canvas; the related workflow is high demand across 3 DAWs.

Acceptance:
- Open a chain canvas and add, connect, disconnect, and reorder nodes; edits round-trip through patcher.get_state/patcher.set_wiring.
- Shift-clicking a cable adds a second destination, and an agent command test verifies the same graph state.
Items:
  - `B-MX-109` Free wiring of a chain - partial (src/gui/shell/ShellCommandMenus.cpp:438-443; commands_snapshot.json:17004; rg --files src/gui | rg -i 'patch|wiring' returns no patch graph view); complete: The chain wiring command is available, but Zene has no visual patch graph for free wiring.
  - `R-workflow-22` Patchable rack makes signal flow tangible - partial (src/gui/shell/ShellCommandMenus.cpp:441-443 (Wire the Chain... form); docs/KNOWN-LIMITATIONS.md:1022-1028 (no patcher canvas)); complete: Chains can be re-wired through a form and a routing matrix, but there is no visual cable/patch canvas.
  - `R-workflow-144` Fan out one modular cable to many destinations - missing (docs/KNOWN-LIMITATIONS.md:1028 (no patcher canvas, no cable or port); patcher.set_wiring is a whole-edge-list form at src/gui/shell/ShellCommandMenus.cpp:441); implement: Zene has no cable-drawing patcher canvas, so there's no cable to fan out. Wiring is edited as an edge list.

## Q050-088 Console channel-strip controls and glue compression

**P1 · L08 mixer, metering, channel strip · completion · L**

Goal: Users can optionally reach trim, polarity, three-band EQ, Console drive, and a glue compressor directly in the mixer Console workflow.

Why: A medium-demand console-strip request appears in 4 DAWs and a related glue-compressor request in 1; current strips omit these routine Console controls.

Acceptance:
- window.mixer_view toggles the console strip section and the saved view restores it.
- Strip controls set/read back trim, polarity, EQ bands, Console drive, and compressor threshold/ratio/makeup through the registered UI and agent actions.
- Deterministic render tests verify the controls affect only their intended signal properties; project save/load and undo restore settings.
Items:
  - `R-mixing-17` Console-style channel strips for tactile mixing - partial (src/gui/MixerChannelView.cpp:104-200 (strip shows send, pan, macros, inserts and fader); mixer.channel_eq / mixer.set_strip / mixer.console_apply are not knobs on the strip); complete: The strip doesn't show trim, EQ or console controls, even though the engine has them.
  - `R-mixing-32` Mixbus bus summing and console tone - partial (mixer.console_apply (Console device: drive, character clean/warm/bright); mixer.channel_eq; mixer.create_return); complete: Console saturation and tone and bus channels exist, but there's no built-in bus compressor stage.

## Q050-089 Hardware insert latency detail and real-time export

**P1 · L09 routing, buses, sends, sidechain, I/O · completion · L**

Goal: See each hardware insert's measured or overridden latency in PDC reports and strip tooltips, and capture its processed return in real-time export.

Why: Hardware inserts already feed PDC but pdc.report omits per-channel detail and offline export bypasses them (KNOWN-LIMITATIONS.md:2313-14); demand is medium and the request spans 4 DAWs.

Acceptance:
- With a measured latency and with an override, pdc.report lists the insert latency per channel and the strip PDC tooltip shows the same value.
- A real-time export test captures the hardware insert return into the rendered file; offline export continues to identify that hardware is unavailable and bypassed.
- The UI and control command expose the real-time export choice and its resulting file can be opened and measured.
Items:
  - `R-mixing-18` Hardware insert export and channel latency - partial (docs/KNOWN-LIMITATIONS.md:2313-2314 (a hardware insert is bypassed during export and reports 0 latency; pdc.report doesn't list its latency per channel); mixer.hardware_insert / hardware_insert_ping); complete: Exports drop hardware insert audio, and pdc.report hides the insert's latency.
  - `R-performance-72` Clarify plugin-delay behavior for outboard inserts - partial (mixer.hardware_insert_ping, mixer.hardware_get; docs/KNOWN-LIMITATIONS.md:2313-2314 (insert bypassed in export; pdc.report omits its latency per channel)); complete: Hardware-insert latency is measured and compensated, but pdc.report does not show it per channel, so the timing is hard to explain.

## Q050-090 Plug-in manager vendor and background scan

**P1 · L10 devices, instruments, effects, plug-in hosting · feature · M**

Goal: Users can see vendor metadata while plug-in scans run with progress and cancellation.

Why: The manager omits vendor and scanning remains synchronous; demand is medium across 5 DAWs.

Acceptance:
- Show vendor and per-file scan status in the manager, start a background rescan, cancel it, and continue using cached results.
- Exercise progress, cancel, and cache read-back through command tests.
Items:
  - `R-effects-29` Plugin-scan catalogue and manager details - partial (Plug-in Manager and Plug-in Modules lists with status, quarantine and rescan (src/gui/CommandLists.cpp:123-144, src/gui/shell/ShellCommandMenus.cpp:518); docs/KNOWN-LIMITATIONS.md:2444 plugin.list has no vendor; :2463 the scan is synchronous); complete: Scan status, quarantine and rescan are in a manager, but plugin.list lacks the vendor and the scan blocks the window.
  - `R-performance-68` Plugin scanning and freezes - partial (plugin.scan_cache_get_state (cache and quarantine); docs/KNOWN-LIMITATIONS.md:2463 (the plug-in scan stays synchronous, not in the task area)); complete: Scans are cached and bad plug-ins quarantined, but the scan still runs synchronously and cannot be followed or cancelled.

## Q050-091 Multi-part MusicXML import/export round trip

**P1 · L12 projects, files, import/export, interchange, recovery · feature · M**

Goal: Import MusicXML into MIDI clips and export each instrument track as a separate MusicXML part.

Why: The rows report medium demand across five DAWs and cite the missing importer, single-clip export limit, and absent reliability tests.

Acceptance:
- Import and export MuseScore-readable multi-part fixtures and compare notes, part names, durations, and track assignment.
- Run a round-trip fixture containing rests, tuplets, and non-quantized notes and report any unsupported notation.
Items:
  - `R-interop-13` MusicXML file interchange - partial (clip.export_musicxml (one MIDI clip, one part); TRACKER-050.md:336 (ED-086 partial); no MusicXML importer in src/); complete: MusicXML export covers one clip as one part, and nothing imports MusicXML.
  - `R-interop-62` MusicXML reliability and data-loss pain - partial (clip.export_musicxml (one clip, one part, quantised to a 64th); TRACKER-050.md:336; no MusicXML importer or round-trip test in tests/); complete: MusicXML export is single-part and untested for round trips, and there's no import.

## Q050-092 Update capture and recording documentation

**P1 · L03 recording, takes, comping, inputs · docs · S**

Goal: Users can find the existing retrospective-capture controls and understand the current input and placement behavior.

Why: Recording demand is high across 7 DAWs, while the limitations page still describes a two-track prototype and no ALSA capture.

Depends on: Place retrospective captures on take lanes
Acceptance:
- RETRO-AUDIO-CAPTURE documents the menu, record.retro_capture commands, and the new take-placement behavior.
- KNOWN-LIMITATIONS removes superseded two-track, no-ALSA-capture, and no-retrospective-audio claims while stating the remaining hardware limits.
Items:
  - `B-WF-083` Retrospective audio capture - have (record.retro_capture_arm; record.retro_capture_to_take; src/gui/shell/ShellCommandMenus.cpp:140-143; docs/RETRO-AUDIO-CAPTURE.md:39); docs: The rolling audio buffer and context-menu arm/save actions exist, but the feature document still says retrospective audio has no interface.
  - `R-recording-13` Integrated audio recording with pattern sequencing - have (record.arm_track; record.input_set; src/core/audio/AudioAlsa.cpp:610 (ALSA capture publishes frames); docs/KNOWN-LIMITATIONS.md:671-673 still says recording is a two-track prototype and ALSA has no capture path; :971-975 says retrospective audio capture is not in the release); docs: Audio recording sits beside sequencing, but two limitations bullets still describe the old two-track, no-ALSA-capture, no-retro-audio state.

## Q050-093 Detect tempo and auto-warp imported loops

**P1 · L05 audio editing, warping, stretching, fades · completion · L**

Goal: Users can drop a loop, detect its tempo and key, and choose to follow the project tempo and key or keep it unstretched.

Why: Tempo detection spans 7 DAWs, while detect.analyze/apply and warp.quantize are separate actions with no loop-drop auto-warp path; the related key-follow request is medium demand.

Acceptance:
- Drop a fixture loop with known tempo and key; verify source tempo/key are detected, the clip can follow project tempo, and transpose matches the project key.
- Verify a keep-unstretched preference and confirm the rendered loop duration matches the selected mode.
- Drive the same import/drop choice through the registered agent command and verify clip-state read-back.
Items:
  - `B-ED-149` Tempo detection and auto-warp - partial (detect.analyze/detect.apply; warp.quantize; src/gui/shell/ShellCommandMenus.cpp:205-215); complete: Tempo detection and transient quantising exist separately, but no action derives and applies a clip warp map from its detected tempo.
  - `R-workflow-67` ACIDized loop tempo and key following - partial (command ids browser.preview match tempo/key, warp.set tempo mode 'follow'/'source', clip.set_transpose; rg setSourceTempo in src/gui finds no drop-time use); complete: Loops can follow project tempo and key once set, but a dropped loop is not detected and set to follow automatically as ACID does.
  - `R-workflow-208` Automatic tempo detection when importing loops - partial (detect.analyze/detect.apply; src/gui/shell/ShellCommandMenus.cpp:214-215; docs/KNOWN-LIMITATIONS.md:1787-1790; no tempo detection in sample-clip import/drop path (rg acid|detectTempo in src/gui: none)); complete: Tempo detection exists as a separate analyse/apply-to-project action, but an imported loop does not get its source tempo detected and set so it can follow or ignore song tempo.

## Q050-094 Make audio record modes honor history

**P1 · L03 recording, takes, comping, inputs · completion · M**

Goal: Users can choose overdub, replace, or keep-history behavior for audio recording, with each kept pass preserved as a take lane.

Why: Recording-mode demand spans 6 DAWs, while transport.record_options is explicitly MIDI-only and audio takes currently replace the clip.

Acceptance:
- The record-mode control and transport.record_options expose the same choices for MIDI and audio.
- Audio recording tests prove overdub, replace, and keep-history behavior; keep-history creates separate recoverable lanes and undo restores the prior state.
Items:
  - `B-WF-082` Record modes (overdub, replace, keep history) - partial (transport.record_options; src/core/ControlCommandsRecordingPass.cpp:147-157; src/gui/RecordLaunchMenus.cpp:97-107); complete: Record options provide Overdub and Replace, but no Keep History mode that stores each pass as a new take.
  - `R-recording-5` Audio overdub, replace and keep-history record modes - partial (transport.record_options ('mode' overdub/replace for MIDI passes); src/gui/RecordLaunchMenus.cpp:100 ('Record Mode (MIDI)'); clip.set_record (an audio take replaces the clip's audio); plans/TRACKER-050.md:146 (WF-082 6/8)); complete: Overdub and replace apply only to MIDI; audio recording always replaces and keeps no history.

## Q050-095 Expose master and channel controls as modulation targets

**P1 · L06 automation and modulation · feature · M**

Goal: Modulate Song master gain and mixer channel volume or pan through the same route workflow as device parameters.

Why: Demand is medium (4 DAWs for R-effects-13 and 1 for R-effects-12); the resolver supports rack and instrument parameters but omits master gain and channel volume/pan targets.

Depends on: Add an audio envelope follower modulation source
Acceptance:
- The route command, state readback, save/load, and undo cover master gain and channel volume/pan targets.
- An LFO and the envelope follower can each drive every new target; rendered output changes by the route depth and returns to the stored base when modulation stops.
- The Modulators panel can pick, inspect, and remove these targets, and MODULATION.md section 9 lists the supported target types.
Items:
  - `R-effects-13` Envelope followers and broad modulation targets - partial (modulator.target_set (mixer rack effect parameters), modulator.instrument_target_set (instrument parameters); no master-gain or channel-volume target); complete: Modulators reach effect and instrument parameters; an envelope follower source and global targets (master gain, channel volume, pan) are missing.
  - `R-effects-12` Modulator target reach beyond mixer racks - partial (modulator.target_set (rack effect parameters) and modulator.instrument_target_set (instrument parameters); docs/MODULATION.md:232 still says instrument parameters cannot be targets); complete: Instrument parameters are now targets, but the Song master gain and channel volume still are not.

## Q050-096 Chord context and stamping in the new editor

**P1 · L07 note editor, MIDI editing, scales, chords, articulations · completion · M**

Goal: See the chord track or detected chord names above the note canvas and stamp a selected chord into the clip from the draw tool.

Why: High demand across three DAWs; chord commands exist but the new editor still lacks chord context and stamping.

Acceptance:
- The new editor displays chord names at the matching timeline positions.
- A draw-tool test stamps a chord from the published vocabulary and verifies its notes, position, and undo.
- The chord display and stamping actions are also reachable through registered commands.
Items:
  - `R-accessibility-34` Visual chord context while editing notes - partial (chord.detect/chord.get_state exist; src/gui/noteeditor/ has no chord display (rg chord); docs/NOTE-EDITOR.md:191 (chord stamping not in the new editor yet)); complete: Chords are detected and shown on the arranger's chord ruler lane, but the note editor shows no chord names or chord-track context while editing.
  - `R-editing-91` Chord entry and MIDI quantize in Piano Roll - partial (Quantise... in the note editor (src/gui/noteeditor/NoteEditorMenus.cpp:235); docs/NOTE-EDITOR.md:191 chord stamping only in the classic roll); complete: Quantise is inside the note editor, but chord entry from a root note is only in the classic piano roll.

## Q050-097 Mapped sampler-zone editor

**P1 · L10 devices, instruments, effects, plug-in hosting · feature · L**

Goal: Users can map samples to key ranges with editable start/end/loop controls.

Why: The built-in sampler plays one sample and its view has only normalized loop knobs; demand is high in 2 DAWs and low in 3.

Acceptance:
- Map two samples to disjoint key ranges, edit numeric sample positions, and render the expected zones.
- Save/load all zone and loop data and expose the map through agent commands.
Items:
  - `R-instruments-17` Integrated sample editor and instrument model - partial (plugins/AudioFileProcessor/AudioFileProcessor.cpp:44-56,194-209; clip.slice_to_instrument; sample.* commands); complete: A sampler, destructive sample editor and slicing workflow exist, but the full tracker-style mapped instrument, modulation and per-instrument effects model is absent.
  - `R-editing-119` More precise sample loop-point editing - partial (plugins/AudioFileProcessor/AudioFileProcessorView.cpp:121-131 start/end/loop knobs; plugins/AudioFileProcessor/AudioFileProcessor.cpp:72-74 models are 0..1 fractions; wave view zooms (AudioFileProcessorWaveView.cpp:362)); complete: AudioFileProcessor's waveform zooms, but loop points are 0..1 knobs with no sample-accurate numeric entry.

## Q050-098 Yabridge compatibility slice

**P1 · L10 devices, instruments, effects, plug-in hosting · completion · M**

Goal: Linux users can load the documented yabridge-wrapped VST3/CLAP combinations that pass validation.

Why: There is no yabridge support record and this high-demand request spans 8 DAWs.

Depends on: Third-party plug-in compatibility matrix
Acceptance:
- Test named yabridge versions and plug-ins through plugin.load and record load, editor, state, and audio results.
- Publish only combinations actually exercised and include the compatibility matrix result.
Items:
  - `R-interop-15` Yabridge Windows plug-in access - partial (CMakeLists.txt:796-815 (Wine-based VST2 bridge build, RemoteVstPlugin); docs/KNOWN-LIMITATIONS.md:1888 (no third-party CLAP loaded on any platform); no yabridge hits in src/ or docs); complete: Linux users get LMMS's Wine VST2 bridge, but nobody has tested yabridge-wrapped Windows VST3/CLAP plug-ins.

## Q050-099 Keyboard editing for waveform and MPE lanes

**P1 · L14 help, onboarding, accessibility · feature · L**

Goal: Navigate and edit sample ranges and selected MPE notes from the keyboard.

Why: The waveform gap is reported across four DAWs, and KNOWN-LIMITATIONS.md:2430-2431 confirms the waveform takes no keys and MPE lane keys edit only the cursor note.

Acceptance:
- With the mouse unused, move a waveform cursor, select a range, zoom, and apply a range edit; verify each operation is undoable.
- In an MPE lane, navigate and edit the selected notes rather than only the cursor note, and verify keyboard focus and screen-reader readout.
- Correct the stale keyboard-access wording in KNOWN-LIMITATIONS.md and run the focused canvas/accessibility tests.
Items:
  - `B-WF-132` Keyboard navigation of the whole interface - partial (window.focus_panel F6 (src/gui/ControlCommandsWorkspace.cpp:250); src/gui/widgets/Fader.cpp:89 (faders now TabFocus) vs docs/KNOWN-LIMITATIONS.md:2103 (stale: faders take no focus); :2430-2431); complete: Panel cycling, focusable knobs/faders and canvas item lists exist, but the sample waveform takes no keys and MPE lane keys edit only the cursor note; the limitations page still says faders take no focus.
  - `R-accessibility-21` Keyboard access to the sample waveform - partial (docs/KNOWN-LIMITATIONS.md:2430 (sample editor waveform takes no keys)); complete: The sample editor's keyboard route is its range fields; the waveform has no keyboard navigation or editing.
  - `R-accessibility-20` Keyboard gaps in waveform and lane workflows - partial (docs/KNOWN-LIMITATIONS.md:2426,2430-2431); complete: The sample waveform takes no keys, MPE lane keys edit only the cursor note, and the item list is a floating window.

## Q050-100 Spoken meters and peak watching

**P1 · L14 help, onboarding, accessibility · feature · M**

Goal: Ask for the focused channel's level and hear input or master peaks that cross a chosen threshold.

Why: Both meter-feedback requests are high demand, and the manual explicitly says current announcements never report meter movement.

Acceptance:
- Invoke the speak-meter command and shortcut; verify they report the focused channel peak and master LUFS.
- Set a peak-watch threshold and verify input and master crossings are announced once per crossing, without repeated speech for every meter update.
- Verify announcements honor view.announcements and test threshold behavior with a controlled signal.
Items:
  - `R-effects-14` Accessible plug-in controls and meter feedback - partial (src/gui/ParameterGridDialog.cpp named generic controls; src/gui/widgets/Fader.cpp:541 fader value spoken; view.announcements never speaks meters); complete: Plug-in parameters and fader values are reachable by screen reader, but meter levels cannot be read out.
  - `R-recording-30` Blind recording needs spoken peak and clipping feedback - partial (view.announcements (recording start/stop announced); meter.input; meter.get_state; no peak-threshold announcement); complete: Screen readers hear recording start and stop, but there is no spoken alert when input or master peaks cross a threshold.

## Q050-101 Transcribe drum hits to MIDI

**P1 · L05 audio editing, warping, stretching, fades · feature · L**

Goal: Users can turn drum hits in an audio recording into a timed MIDI pattern with useful velocity values.

Why: The extension is medium demand across 7 DAWs, and clip.audio_to_midi currently covers only monophonic sources.

Depends on: Transcribe polyphonic audio to MIDI
Acceptance:
- Run a drum-loop fixture and verify one MIDI event per detected hit with timing and velocity matching the reference labels.
- Verify the result is editable, undoable, and produced through the agent command.
Items:
  - `R-editing-39` Audio-to-MIDI transcription - partial (src/core/ControlCommandsAudioAnalysis.cpp:168-177; command id clip.audio_to_midi); complete: The current YIN path transcribes monophonic lines, while polyphonic and drum transcription are not implemented.

## Q050-102 Named multichannel I/O and record mapping

**P1 · L09 routing, buses, sends, sidechain, I/O · completion · L**

Goal: Configure multichannel hardware ports by their real names and see which record route and track each named input feeds.

Why: JACK/ALSA multichannel output has not been run and JACK port names ignore I/O labels (KNOWN-LIMITATIONS.md:2307-12); demand is medium from 4 DAWs.

Depends on: N-channel routing graph and per-channel sends
Acceptance:
- A JACK dummy-backend integration test with more than two ports verifies port names follow audio.io_setup channel and bus labels and audio.io_list reads them back.
- The input mapping view shows each named device input, its record route and destination track; changing a route updates record.get_state.
- A multichannel-output smoke test records the backend and channel count for a supported JACK or ALSA device.
Items:
  - `R-platform-14` Multichannel device support and input naming - partial (audio.io_setup; audio.io_list; docs/KNOWN-LIMITATIONS.md:2307-2313 (multichannel JACK/ALSA unrun; input names are labels only; JACK port names do not use them)); complete: Multichannel I/O and input naming exist, but JACK port names ignore the labels and only the Dummy device has been measured.
  - `R-recording-63` Multi-channel interface routing is difficult to understand - partial (src/gui/MixerHardwareMenu.cpp:112-113 (Interface Channels... audio.io_setup names/buses; Hardware Routing list); record.get_state routes); complete: Channel names and buses exist, but there is no single view mapping hardware inputs to tracks.

## Q050-103 Sampler envelopes and released-voice policy

**P1 · L10 devices, instruments, effects, plug-in hosting · feature · M**

Goal: Users can set automatable sample attack/release envelopes and a predictable voice-reuse policy.

Why: The sampler lacks user-adjustable envelopes and an explicit voice-reuse policy; demand is medium across 4 DAWs.

Acceptance:
- Render overlapping notes to verify attack, release, and released-voice reuse.
- Automate envelope values, save/load, and confirm the existing loop controls remain stable.
Items:
  - `R-instruments-20` Sampler workflow and drum-instrument refinement - partial (plugins/AudioFileProcessor/AudioFileProcessor.cpp:69-78,194-209 exposes sample range, loop, reverse and gain controls); complete: Core sampler controls exist, but loop-boundary automation, envelope hold and sample gain envelopes remain missing.
  - `R-instruments-75` Sampler envelopes and voice allocation - partial (plugins/AudioFileProcessor/AudioFileProcessor.h:62-78 has sample playback and a fixed release; no user-adjustable sampler attack/release or voice-reuse policy found.); complete: Sample playback exists, but sampler attack/release controls and a documented released-voice reuse policy are missing.

## Q050-104 Scoped agent access with shared undo

**P1 · L17 agent control surface and AI assistance · completion · M**

Goal: A user can allow or deny an agent connection, restrict it to read-only, see when it is connected, and undo accepted edits through the shared project history.

Why: The grouped items report medium demand across 3 and 1 DAWs, and the socket/MCP registry exposes hints without a session policy; SPEC-zene-studio.md A16 requires destructive actions to rely on undo rather than confirmation.

Acceptance:
- On agent connection, show an allow, deny, or read-only-for-this-session choice and a persistent connected indicator that clears on disconnect.
- Enforce denied and read-only policies in the control registry; under allow mode, destructive edits execute without a separate confirmation and remain undoable through the shared history.
- Verify allow, deny, read-only, disconnect cleanup, and edit-then-undo behavior through the socket/MCP path.
Items:
  - `R-workflow-47` Agent actions need scoped approval and a strong undo path - partial (tools/mcp-zene-control/zene_control/registry.py:186-187 (read_only/destructive hints only); command ids control.undo, control.transactions; no agent approval or read-only setting in commands_snapshot.json); complete: Every agent edit is undoable and MCP tools carry destructive hints, but Zene has no per-connection policy to make an agent read-only or require approval for destructive batches.
  - `R-workflow-167` Project-aware AI control requires explicit access and remains scoped - partial (tools/mcp-zene-control/zene_control/registry.py:186-195 (read_only_hint and destructive_hint per tool); --control-socket is opt-in; rg found no in-app approve or deny prompt for agent access); complete: The agent surface is opt-in and flags which tools mutate, but the DAW itself never asks the user to approve an agent's access or mutating calls.

## Q050-105 Control slice markers from MIDI

**P1 · L05 audio editing, warping, stretching, fades · completion · M**

Goal: Users can audition and move sample-slice points from MIDI-learned controls or controller pads.

Why: Controller-driven chopping is medium demand across 10 DAWs, and the selected slice point has no MIDI-learnable audition or movement control.

Depends on: Edit and audition transient and slice markers
Acceptance:
- MIDI-learn next-slice, previous-slice, selected-marker move, and audition actions; verify each mapped control selects, moves, or previews the expected slice.
- Verify marker movement is undoable, persists in the project, and is available through the controller/agent action registry.
Items:
  - `R-instruments-46` MIDI-controlled sample-slice editing - missing (clip.slice_to_instrument; searched the command snapshot and src/gui/ for MIDI-bindable slice-marker audition or movement.); implement: Slicing is available, but MIDI controllers cannot audition or move its slice markers.
  - `R-workflow-233` Controller-driven sample chopping - partial (plugins/SlicerT/SlicerT.h:70-74 (only threshold, fade, BPM and sync are models; slice points are not); clip.slice_to_instrument maps slices to MIDI keys); complete: Pads can already audition slices because each slice sits on a MIDI key, but a controller can't move slice points, which aren't automatable models.

## Q050-106 Split stereo files and convert clips to mono

**P1 · L05 audio editing, warping, stretching, fades · feature · M**

Goal: Users can export left and right channels as separate mono files or destructively convert a clip to mono.

Why: Stereo splitting is high demand from 1 DAW and mono conversion is low demand from 1, while neither operation has a sample command today.

Acceptance:
- Split a stereo fixture to two destination files and verify left/right sample data, rate, and length against the source.
- Convert a stereo clip to mono and verify the documented downmix rule, undo, and agent state read-back.
- Confirm both operations are reachable from the Sample Editor and the clip Process menu.
Items:
  - `R-editing-11` Split stereo audio into mono files - missing (commands_snapshot.json (rg -i 'split stereo|split.*mono' found no command; sample.edit_range only selects a range)); implement: Add a split-stereo operation that writes the left and right channels as separate mono files.
  - `R-editing-136` Audio clip reverse, normalize and mono conversion - partial (clip Process menu Normalize / Amplify / Reverse (src/gui/shell/ShellCommandMenus.cpp:217-219); no mono-conversion command (rg mono over sample.* commands)); complete: Reverse and normalise are in the clip menu; converting a clip to mono is missing.

## Q050-107 Expand built-in modulation sources

**P1 · L06 automation and modulation · feature · L**

Goal: Drive parameters from a multi-segment envelope, keyboard tracking, or note velocity.

Why: Demand is high (2 DAWs for R-instruments-15) and low (1 for R-instruments-69); the source enum has no MSEG, key-tracking, or velocity source and the broader request remains partial.

Depends on: Add an audio envelope follower modulation source; Expose master and channel controls as modulation targets
Acceptance:
- Source commands and the Modulators panel expose MSEG, key-tracking, and velocity sources with readback and project persistence.
- A test covers retriggering, note/key/velocity input, source bounds, and target output for overlapping notes.
- Allocation counters report zero audio-thread allocations while each source runs; invalid source/target combinations return typed refusals.
Items:
  - `R-instruments-15` Modulation as a sound-design tool - partial (modulator.create, modulator.rate_set, modulator.target_set; docs/MODULATION.md:24-34); complete: LFO, formula and per-voice modulation exist, but source types and destinations remain narrower than a full sound-design modulation system.
  - `R-instruments-69` Broader modulation controller set - partial (src/core/ControlCommandsModulator.cpp:320-323 supports LFO shapes, steps and an envelope; modulator.voice_set adds per-note triggering.); complete: LFO, step and triggered-envelope sources exist, but envelope followers, MSEG, key tracking and velocity sources are absent.

## Q050-108 Per-channel loudness, true-peak, and VU metering

**P1 · L08 mixer, metering, channel strip · completion · L**

Goal: Users can choose peak, RMS, VU, or LUFS/true-peak metering per mixer strip, with the shell loudness readout scope documented accurately.

Why: Per-channel LUFS/true peak is a five-DAW gap, the VU request has high demand, and the current shell loudness readout is master-only.

Acceptance:
- A channel meter mode can be set and read back through the agent command and strip UI, including VU and per-channel LUFS/true-peak.
- Meter tests verify 300 ms VU integration against the configured reference and calibrated per-channel loudness/true-peak results.
- Documentation distinguishes the per-strip modes from the shell readout, which remains master-only.
Items:
  - `B-MX-132` Loudness meter (LUFS, true peak) - have (src/gui/shell/TransportReadouts.cpp:392-399; `meter.get_state`; stale contrary claim at docs/KNOWN-LIMITATIONS.md:1464-1474); docs: The shell shows the live master LUFS and true-peak readout, so the limitation claiming there is no interface meter is stale.
  - `B-MX-127` Peak, RMS and loudness meter modes - partial (src/gui/ControlCommandsMixerView.cpp:247-250 supports meter_mode peak/rms/pre_fader; master-only LUFS is separate at `meter.get_state`); complete: Per-strip peak and RMS modes exist, but loudness is only measured on the master and cannot be selected as a channel meter mode.
  - `R-mixing-6` VU or RMS mixer metering - partial (docs/KNOWN-LIMITATIONS.md:2470-2477 (RMS meter mode is one period's RMS, not a 300 ms VU integration); window.mixer_view meter_mode/meter_scale; meter.set_reference (0 VU = -18 dBFS)); complete: An RMS meter mode and a VU reference exist, but there are no real VU ballistics.

## Q050-109 MPC project import: supported track, sample, and pattern content

**P1 · L12 projects, files, import/export, interchange, recovery · feature · L**

Goal: Recognize desktop-compatible MPC project files and import their supported tracks, samples, and patterns into a Zene project.

Why: The row records high demand across three DAWs and confirms that no MPC project parser exists in the merged tree.

Acceptance:
- Import representative standalone MPC fixtures and verify track count, sample references, pattern notes, and timing against fixture manifests.
- Return a user-visible and command-surface report for unsupported MPC content.
- Run the same importer through a registered project import command and compare its report with the UI.
Items:
  - `R-interop-3` MPC standalone-to-desktop project compatibility - missing (dawproject.import is the only project interchange importer found; searched src/, include/, plugins/ and tests/ for MPC project parsing.); implement: Zene has no importer for Akai MPC standalone project files.

## Q050-110 Record MIDI directly into launcher cells

**P1 · L02 launcher / session clips / performance · feature · M**

Goal: Record live MIDI into an empty instrument-track cell on its launch quantisation and make the take that cell's pattern.

Why: Demand is medium across 6 DAWs; current audio recording is limited to empty sample-track cells, while MIDI capture is only available after the fact from the retro buffer.

Acceptance:
- A headless integration test feeds known MIDI notes into an armed empty cell and verifies the take starts on the quantised boundary and preserves note timing and velocity.
- After the quantised stop, session.get_state reports the cell's pattern and the pattern contains the recorded notes.
- The cell recording action is available through the interface and agent command surface, and one undo restores the empty cell.
Items:
  - `R-recording-2` Record audio and MIDI into launcher cells - partial (session.record_slot; docs/KNOWN-LIMITATIONS.md:2557-2579 (audio into empty sample cells only); session.capture_to_slot (MIDI only after the fact from the retro buffer)); complete: Audio records straight into launcher cells, but MIDI can only be captured into a cell after playing, not recorded live on the cell's launch.

## Q050-111 Add per-clip realtime effects

**P1 · L04 arranger, timeline, clip operations, markers, tempo map · feature · L**

Goal: Users can add, order, bypass, and remove realtime effects on an individual arrangement clip.

Why: The 7-DAW medium-demand workflow request explicitly cites the absence of per-clip realtime effects.

Acceptance:
- A render regression test applies an effect to one of two clips on the same track and verifies only that clip's output changes.
- Per-clip effect state survives save/load, appears in the clip UI, and can be edited through the agent control surface.
Items:
  - `R-ui-8` Mouse-first editing with predictable zoom and gestures - partial (src/gui/ArrangerAids.cpp:111-121 (pinch zoom); docs/KNOWN-LIMITATIONS.md:2421-2422 (wheel/pinch zoom direct, not animated); no per-clip realtime effects); complete: Zoom, delete and drag are in place; event-level (per-clip) effects are missing.

## Q050-112 Musical time for hosted effects

**P1 · L10 devices, instruments, effects, plug-in hosting · feature · M**

Goal: Users can keep tempo-synced VST3/CLAP effects aligned to the project song position after latency compensation.

Why: VST3/CLAP receive tempo and play state but not compensated song position; demand is medium across 7 DAWs.

Acceptance:
- Place a tempo-synced effect after a latent slot and verify musical and seconds time stay aligned in playback and offline render.
- Check downstream PDC offsets and agent-reported plug-in timing.
Items:
  - `R-performance-32` Reliable plugin timing with high-latency effects - partial (plugins/Vst3Effect/Vst3Host.cpp:589-597,682 (tempo and playing only; projectTimeSamples is a running counter); plugins/ClapEffect/ClapHost.cpp:472-473 (HAS_TEMPO and IS_PLAYING only); pdc.report); complete: PDC aligns audio, but hosted VST3/CLAP plug-ins get no song position (no bar/beat timeline), so transport-synced plug-ins cannot lock to the grid, latency-compensated or not.

## Q050-113 Search plug-ins, devices, and chain presets from the browser

**P1 · L11 browser, samples, presets, library · completion · L**

Goal: Users can search plug-ins, device presets, and effect-chain presets beside files, then load or drop each result onto its valid target.

Why: R-workflow-108 is marked high demand, and plug-ins plus device choices are still searched in separate UI while Search currently queries files only.

Acceptance:
- The Search source returns instrument/effect plug-ins, device presets, chain presets, and file results in one query with clear result kinds.
- Enter loads each supported result in the appropriate context; drag/drop routes it to a compatible instrument, effect, or chain target.
- GUI checks cover empty searches, unavailable devices, and unsupported targets with clear status feedback.
Items:
  - `R-workflow-108` Unified searchable browser for plug-ins, samples, patches and I/O - partial (src/gui/shell/BrowserPanel.cpp:320-330 (separate sources); src/gui/BrowserSearchView.cpp:268-271 searches files only; chain presets (chain.save) and plug-ins are not in that search); complete: Plug-ins, presets and samples are separate browser sources; no single search spans plug-ins, presets, chain presets and samples.
  - `R-workflow-156` Large stock sound library and integrated browser support discovery - partial (src/gui/BrowserSearchView.cpp:110 (kinds: audio, preset, project, midi only); plug-ins are searched separately in src/gui/shell/DevicePicker.cpp); complete: The library search covers samples, presets, projects and MIDI, but plug-ins sit in a separate picker, so the browser isn't unified across device types.

## Q050-114 Open multiple editable projects in tabs

**P1 · L12 projects, files, import/export, interchange, recovery · feature · L**

Goal: Keep several editable projects open in separate tabs, each with its own document state and save status.

Why: The row reports medium demand across five DAWs, meeting the four-DAW table-stakes threshold for P1.

Acceptance:
- Open two project files in distinct tabs, edit each, and verify each tab retains its own changes and dirty state.
- Close one clean tab without closing or mutating the other project.
- Open and switch projects through registered project commands as well as the tab UI.
Items:
  - `R-workflow-10` Multiple projects open in tabs - partial (command ids reference.open (read-only second project), project.switch; src/gui/LibraryCommandMenus.cpp:162-172 (Switch to Last Project, keep as reference)); complete: A second project can be opened read-only as a reference and projects switch quickly, but there are no tabs of several fully live, editable projects.

## Q050-115 OMF session import

**P1 · L12 projects, files, import/export, interchange, recovery · feature · L**

Goal: Import supported OMF tracks and media with explicit omission reporting for content the reader cannot preserve.

Why: The row cites medium demand across six DAWs and specifically records that OMF is refused and MXF essence is missing.

Depends on: AAF export and MXF-backed media
Acceptance:
- Open representative OMF fixtures and compare supported tracks, media, and timing against their source manifests.
- Verify unsupported OMF features are listed in the import result and user-facing summary.
- Drive the importer through its registered command and verify the same omission report appears in the UI.
Items:
  - `R-interop-11` AAF/OMF/MXF project interchange - partial (interchange.aaf_import; docs/KNOWN-LIMITATIONS.md:2364-2370 (OMF refused by name, MXF essence listed missing, tempo map not followed)); complete: AAF imports, but OMF is refused, MXF media isn't decoded and nothing is exported.

## Q050-116 AAF export and MXF-backed media

**P1 · L12 projects, files, import/export, interchange, recovery · feature · L**

Goal: Export supported audio tracks to AAF and decode MXF-wrapped media referenced by an AAF session.

Why: The source rows include high demand across four DAWs and identify AAF export as absent while the current reader cannot decode MXF essence.

Acceptance:
- Export a fixture with multiple audio tracks, clips, fades, and gain; open it with an AAF reader and verify those structures and media references.
- Import an AAF fixture whose essence is MXF-wrapped and verify decoded audio is present and aligned.
- Exercise AAF export and import through the registered interchange commands as well as File > Import/Export.
Items:
  - `B-WF-038` AAF, OMF and MXF exchange - partial (interchange.aaf_import/aaf_read; src/core/ControlCommandsAafImport.cpp:8 (LibAAF reads, does not write); docs/KNOWN-LIMITATIONS.md:2365-2366 (OMF refused, MXF essence missing)); complete: AAF import works with a dry-run report, but there is no AAF export, OMF is refused and MXF essence is not decoded.
  - `R-workflow-15` Industry handoff and studio familiarity - partial (src/gui/AafImportMenu.cpp:43 (AAF import), src/gui/TrackRenderMenu.cpp:89 (DAWproject export), command id project.handoff; docs/KNOWN-LIMITATIONS.md:2365 OMF refused; no AAF export command); complete: AAF import, DAWproject and a hand-off folder exist, but there is no AAF export to give a Pro Tools studio a session.

## Q050-117 Apply backend latency to every recording route

**P1 · L03 recording, takes, comping, inputs · fix · L**

Goal: Audio and routed recordings use the actual input-device latency so takes land in time.

Why: High demand for recording alignment is cited for 1 DAW, while non-JACK backends report zero device latency and record.arm_track files are uncompensated.

Acceptance:
- Backend fixtures for ALSA, PulseAudio, and SDL report configured capture latency through record.input_get_state.
- Impulse tests prove clip recording and record.arm_track placement compensate the backend and engine terms within one frame.
Items:
  - `R-recording-25` Audio and MIDI recording is less intuitive than sequencing - partial (track.set_arm; track.set_monitor; src/gui/shell/ShellCommandMenus.cpp:135-136; docs/KNOWN-LIMITATIONS.md:682-684 (ALSA, PulseAudio, SDL report 0 device latency, so takes land late)); fix: Recording needs several separate steps, and on non-JACK backends takes land late by the device's buffering.
  - `R-recording-71` Recording latency compensation - partial (docs/KNOWN-LIMITATIONS.md:679-686 (clip capture compensated; only JACK reports device latency; record.arm_track routes write uncompensated files)); complete: Clip takes are compensated but routed takes are not, and non-JACK backends miss the device term.

## Q050-118 Immersive bed and object mixer foundation

**P1 · L08 mixer, metering, channel strip · feature · L**

Goal: Users can mix 7.1.4 height beds and position mono objects in three dimensions without a licensed Dolby renderer.

Why: The medium-demand immersive-mixing gap appears in 4 DAWs, while current support stops at fixed 7.1 beds with no object panning.

Acceptance:
- The mixer command and UI create/select a 7.1.4 bed and save/load its channel format.
- An object track exposes x/y/z position through the UI and agent state; a render test confirms placement changes the expected speaker feeds.
- Existing stereo through 7.1 projects retain their saved format and rendered output.
Items:
  - `R-mixing-1` Native Dolby Atmos mixing and ADM delivery - missing (mixer.channel_format (stereo, LCR, quad, 5.1, 7.1 beds only); src/core/MixerSurround.cpp:57-67; docs/SURROUND-EXPORT.md; no ADM or object code in src/); implement: There are no object panning, height layouts or ADM BWF delivery; Zene has fixed beds up to 7.1.

## Q050-119 Multichannel cue, listen, and recorder paths

**P1 · L08 mixer, metering, channel strip · completion · M**

Goal: Users can carry surround channels through cue sends, AFL/PFL listen, and recorder taps, with a controlled fold-down for stereo targets.

Why: A medium-demand multichannel cue/listen request appears in 6 DAWs, while these paths are currently limited to L/R.

Acceptance:
- 5.1 and 7.1 test signals retain their extra channels through cue, listen, and recorder paths to multichannel outputs.
- When the destination is stereo, each path applies the selected downmix options and reports its channel layout.
- Agent commands can configure and read back the routing; integration tests verify channel order and levels.
Items:
  - `R-mixing-5` Cue/headphone and listen-bus workflows - partial (mixer.cue_master / cue_send (cues A-D to hardware outputs), mixer.listen / listen_mode (AFL/PFL); docs/KNOWN-LIMITATIONS.md:2405 ('Cue mixes, listen and the recorder carry L/R')); complete: Stereo cue, AFL/PFL and listen workflows are built; only a surround channel's extra speakers are dropped in cue, listen and recorder paths.

## Q050-120 Scriptable effect and MIDI devices

**P1 · L10 devices, instruments, effects, plug-in hosting · feature · L**

Goal: Users can add a sandboxed script effect or MIDI processor to a device chain.

Why: The request is high demand and identifies that Lua/WASM scripts currently have no device-chain effect or MIDI path (1 DAW).

Depends on: WASM effect inputs: samples, sidechain, and MIDI
Acceptance:
- Load a documented example script as a device and verify its audio or MIDI output in a render.
- Save/reopen the device source and state; the agent surface exposes list, configure, and read-back commands.
Items:
  - `R-workflow-105` ReaScript and JSFX make the DAW programmable - partial (command ids script.run, livecode.schedule, workflow.*; docs/KNOWN-LIMITATIONS.md:1659-1660 (WASM sandbox not in any audio path)); complete: Lua scripts and recorded workflows act as custom actions, but there is no user-scripted audio/MIDI processor in the signal path and no dockable script UIs.
  - `R-effects-37` Scriptable custom audio effects - partial (docs/WASM-EFFECT-ABI.md:45-75,152-172; WASM effect ABI exists, while no JavaScript effect editor or hot-reload path was found); complete: Custom WASM effects have a declared ABI, but the requested JavaScript editor and hot-reload workflow are absent.

## Q050-121 Show usage and unused assets in Project Media

**P1 · L11 browser, samples, presets, library · completion · M**

Goal: Users can see how often project media is used and review unreferenced project assets before cleaning them up.

Why: Six DAWs are cited with medium demand, while the current Project Media view lists references and missing files but has no usage counts or unused-media cleanup.

Acceptance:
- Project Media displays a per-file usage count, including repeated references from clips and supported instruments.
- The unused-media list distinguishes unreferenced local project assets from missing or externally referenced files.
- Cleanup previews the selected files, asks for confirmation, and leaves referenced assets untouched.
Items:
  - `R-workflow-27` Project media pool and saved searches - partial (src/gui/BrowserSearchView.cpp:349-375 (Project Media list, missing count), :135-223 (collections); no unused-media command in commands_snapshot.json); complete: Project Media lists references and missing files and saved collections work, but there is no usage count or unused-file cleanup.

## Q050-122 Screen-reader recording-to-export workflow

**P1 · L14 help, onboarding, accessibility · completion · L**

Goal: Record, edit, mix, and export a project without relying on visual feedback.

Why: R-accessibility-4 asks for a recording-to-export proof, reflecting high demand for end-to-end access; the current limitation says no real reader has been run.

Depends on: Linux screen-reader core-flow verification
Acceptance:
- Use Orca, and NVDA where a supported host is available, to complete a scripted input setup, recording, edit, mix, save, and export.
- Verify the exported file opens and contains non-silent audio; record every speech or focus blocker and add regression coverage for each repaired blocker.
Items:
  - `R-accessibility-4` Blind users completing production with OSARA - partial (docs/KNOWN-LIMITATIONS.md:2432 (Qt bridge asserted, no screen reader was run); view.announcements (src/gui/ControlCommandsAccessibleEditing.cpp:179); window.accessibility_report); complete: Zene's screen-reader path has not been tested from recording to export.
  - `R-mixing-58` Blind users still seek real-world Logic mixing proof - partial (docs/KNOWN-LIMITATIONS.md:2432 ('Screen readers ... reached through Qt's bridge, which the tests assert. No screen reader was run.')); complete: Accessibility is asserted through Qt's bridge in tests, but no screen reader has been run through an end-to-end mix.

## Q050-123 Show take lanes and waveforms in the timeline

**P1 · L03 recording, takes, comping, inputs · completion · L**

Goal: Users can see take clip geometry and waveforms in timeline rows and the comp strip, while keeping the existing comp panel.

Why: Take-lane demand spans 7 DAWs, while the current interface has no timeline lane geometry or take waveforms.

Acceptance:
- The arrangement shows each lane’s clip geometry and aligned waveform, and the comp strip shows each take waveform with the active segment highlighted.
- A Qt GUI test checks waveform alignment with the time ruler and verifies that the existing comp panel remains available.
Items:
  - `B-ED-164` Take lanes - partial (src/gui/TakeLanesPanel.cpp:89-150; src/gui/CompSwipeStrip.cpp:141-167; docs/KNOWN-LIMITATIONS.md:384-386); complete: Take lanes, comp state and a visual strip are available in a separate panel, but the arranger still lacks integrated take-lane clip geometry and waveforms.
  - `R-recording-26` Prevent overlapping take comps from making edits confusing - partial (include/CompSwipeStrip.h:37-45 (segments lit per lane, audition, segment list); rg wave/peak over src/gui/CompSwipeStrip.cpp and TakeLanesPanel.cpp found nothing); complete: The comp strip shows which lane plays where, but lanes have no waveforms, so overlapping passes are hard to judge.

## Q050-124 Freeze mixer buses

**P1 · L15 platform: performance, stability, Linux audio, install, hardware controllers · feature · L**

Goal: Users can freeze a mixer bus and play its rendered output in place of the live inputs until unfrozen.

Why: The queue reports high demand for bus freeze, while current freeze commands only cover tracks and regions.

Acceptance:
- A render comparison proves bus freeze captures the bus output including routed inputs and playback substitutes the take without changing the unfrozen mix.
- Freeze/unfreeze state survives save/load and has a registered command, UI action and control-socket test.
- A realtime test confirms the render playback path allocates nothing on the audio thread.
Items:
  - `R-performance-26` Freeze or render mixer buses - partial (freeze.track, freeze.region (tracks only); rg 'freez' in src/core/Mixer.cpp: no bus freeze); complete: Instrument and sample tracks freeze, but group and return buses cannot be frozen or rendered in place.

## Q050-125 Edit clip gain, envelope, and polarity on the waveform

**P1 · L05 audio editing, warping, stretching, fades · completion · M**

Goal: Users can drag clip gain, draw gain-envelope points, and invert polarity while seeing the resulting level on the waveform.

Why: Clip gain controls are medium demand from 1 DAW and polarity is low demand across 7, while gain/envelope remain form-only and polarity is mixer-only.

Acceptance:
- Drag the clip gain handle and add/move envelope points directly on the waveform; verify the drawn waveform follows the gain curve.
- Invert one clip's polarity and verify its rendered waveform changes sign without changing the source sample.
- Verify gain, envelope, polarity, undo, and read-back through the agent command surface.
Items:
  - `R-editing-85` Direct audio-clip manipulation and visible handles - partial (fade handles on sample clips (include/SampleClipView.h:76-84, src/gui/clips/SampleClipOverlays.cpp:55-58); clip.set_gain, clip.gain_envelope_set by form; docs/KNOWN-LIMITATIONS.md:2621 crossfades edited as numbers); complete: Fade handles can be dragged, but clip gain and the gain envelope are typed forms, crossfades have no handle, and a clip cannot carry a non-destructive filter.
  - `R-editing-137` Per-clip phase inversion and gain envelope - partial (clip.gain_envelope_set drawn as a line over the waveform (src/gui/clips/SampleClipOverlays.cpp:87-101); polarity exists only on mixer strips (src/gui/shell/ShellCommandMenus.cpp:461-462)); complete: The clip gain envelope exists and is drawn over the clip, but there is no per-clip polarity flip and the waveform does not scale with the gain.

## Q050-126 DAWproject audio clips and media round trip

**P1 · L12 projects, files, import/export, interchange, recovery · completion · L**

Goal: Transfer audio clips and their media between DAWproject sessions without flattening or losing references.

Why: The row records medium demand across five DAWs and identifies missing audio/media output as a handoff gap.

Acceptance:
- Export and re-import a project with audio clips and collected media; verify clip placement, source media, and playback match the source.
- Compare import/export results for supported tracks and report any skipped audio content.
Items:
  - `R-interop-26` Cross-DAW project handoff pain - partial (dawproject.export (audio, devices and sends not written, docs/DAWPROJECT-INTERCHANGE.md:251-259); render.stems; project.handoff); complete: Moving a session to another DAW still means flattening to audio, because DAWproject drops audio clips and device state.

## Q050-127 Correct comping documentation

**P1 · L03 recording, takes, comping, inputs · docs · S**

Goal: Users can learn the existing comp gestures and see the remaining limits accurately.

Why: Comping evidence covers 7 DAWs, but KNOWN-LIMITATIONS still says no comping gesture despite the existing CompSwipeStrip.

Depends on: Show take lanes and waveforms in the timeline
Acceptance:
- KNOWN-LIMITATIONS and COMPING describe the implemented comp strip gestures and lane display.
- The docs no longer say there is no comping gesture and list only limits that remain after the lane UI work.
Items:
  - `B-ED-165` Comp by swiping - have (include/CompSwipeStrip.h:39-45; src/gui/CompSwipeStrip.cpp:309-343; docs/KNOWN-LIMITATIONS.md:384); docs: CompSwipeStrip implements drag-to-comp and keyboard gestures, while the limitations page still says no comping gesture exists.
  - `R-recording-36` Comping and take-lane polish - partial (include/CompSwipeStrip.h:37-45; comp.cycle; comp.move_edge; docs/KNOWN-LIMITATIONS.md:383 still says 'no comping gesture' anywhere in src/gui/); docs: Comping has swipe, cycle and edge editing, but the limitations page still claims there is no comping gesture.

## Q050-128 Add relocatable video and SMPTE timecode support

**P1 · L04 arranger, timeline, clip operations, markers, tempo map · completion · L**

Goal: Users can relink project-relative video media and lock video clips and markers to a chosen SMPTE frame rate and start offset.

Why: The 4-DAW medium-demand request identifies absolute video paths that missing_assets/relink cannot resolve and the absence of SMPTE locks for clips and markers.

Depends on: Support multiple positioned video clips
Acceptance:
- Move a project and its video media to a new directory, then use missing_assets/relink to locate every video clip.
- A regression test saves and reloads SMPTE frame-rate and start-offset settings for video clips and markers and verifies their displayed timecode.
Items:
  - `R-interop-16` Video timeline and timecode sync - partial (video.attach / video.set_offset; docs/KNOWN-LIMITATIONS.md:2372-2377 (one video per project, absolute path, missing_assets/relink don't know it)); complete: Only one video per project, stored by absolute path and not relinkable; clips can't be locked to SMPTE time.

## Q050-129 Follow arranger selection in the mixer

**P1 · L08 mixer, metering, channel strip · completion · M**

Goal: Users can make the mixer follow the selected arranger track and filter strips to selected or visible tracks, with a persistent preference.

Why: The selected-filter request reaches 5 DAWs, while the existing follow-channel implementation has no user-facing persistent preference.

Acceptance:
- The follow-channel preference is user-visible, saved, and reflected by ui/followchannel state.
- With Selected filtering enabled, changing arranger selection updates the visible mixer strips; disabling follow preserves manual mixer selection.
- A headless agent-surface test verifies setting, read-back, and selection/filter behavior.
Items:
  - `B-MX-021` Track that follows the selected channel - partial (src/gui/shell/ShellSelection.cpp:70-80,112-120; rg -n followchannel src include plugins tests finds only the selection implementation); complete: Channel selection can select its owning track when ui/followchannel is set, but users have no preference control for that setting.
  - `R-mixing-21` Selected-channel mixer filter - partial (docs/MIXER-METERING.md:33-45 (MX-005 'selected' filter); docs/KNOWN-LIMITATIONS.md:2479-2482 ('the arranger's track selection does not feed it')); complete: The Selected filter exists, but the arranger's track selection doesn't drive it.

## Q050-130 Edit tempo events and ramps in the arranger

**P1 · L04 arranger, timeline, clip operations, markers, tempo map · completion · M**

Goal: Users can drag tempo events and ramp handles directly in the arranger tempo lane and see the tempo map update.

Why: The tempo-map ramp command exists but the ruler lane is click/tooltip only; the partial UI request has medium demand across 4 DAWs.

Acceptance:
- A GUI test drags a tempo point and toggles or drags a ramp, then verifies transport.tempo_map_get reports the edited ticks, BPM values, and ramp state.
- The tempo lane is keyboard-accessible, hit targets remain usable at zoom levels, and undo restores the previous map.
Items:
  - `R-workflow-38` Linear tempo ramps and tempo-lane editing - partial (src/gui/TempoMapPanel.cpp:125-128 (Ramp to Next); command transport.tempo_map_add 'ramp'; src/gui/ArrangerRulerLanes.cpp:386-391 tempo lane is click/tooltip only); complete: Linear tempo ramps exist and the tempo lane draws them, but the lane cannot be edited by dragging.

## Q050-131 Benchmark real-model stem separation speed

**P1 · L05 audio editing, warping, stretching, fades · docs · S**

Goal: Users can see reproducible real-model throughput figures before deciding to separate a clip.

Why: Benchmark demand is high across 3 DAWs, while STEM-SPLIT.md contains stub-model timings only and says real-model throughput is unmeasured.

Acceptance:
- Run a documented benchmark on the pinned real model and reference audio and record wall time and real-time factor.
- Record the hardware, model revision, clip duration, and exact command in STEM-SPLIT.md; keep the stub-model figures labelled separately.
Items:
  - `R-ai-17` Fast stem extraction - unknown (doc/STEM-SPLIT.md:143-147 (stub-model timings only), :241-243 (real model not measured)); docs: Real HTDemucs throughput is unmeasured, so Zene cannot say how fast extraction is.

## Q050-132 MIDI note-effect chain

**P1 · L07 note editor, MIDI editing, scales, chords, articulations · feature · L**

Goal: Add an ordered note-effect chain that transforms clip notes before instrument playback and exposes the same settings to users and agents.

Why: Medium demand across four DAWs; input transforms exist, but there is no playback note-effect chain or hosting command.

Acceptance:
- A fixture verifies two note effects run in order and produce the expected MIDI events before the instrument.
- A control-socket test adds, reorders, bypasses, saves, and restores the chain with undo.
- The audio path remains bounded and the chain is covered by an allocation-counter test.
Items:
  - `R-editing-49` MIDI processing effects and pattern tools - partial (src/core/ControlCommandsInputTransform.cpp:145; docs/NOTE-EDITOR.md:129; commands_snapshot.json search for Note FX hosting found no command); complete: MIDI input transforms, controller lanes and note editing exist, but a non-destructive MIDI Note FX chain is absent.

## Q050-133 Add file URLs to browser and sample-clip drags

**P1 · L11 browser, samples, presets, library · feature · S**

Goal: Users can drag browser files and sample clips into plug-in editors and file-manager destinations that accept file URLs.

Why: Four DAWs are cited with medium demand, and StringPairDrag currently publishes only its private StringPair MIME type.

Acceptance:
- Drag MIME data includes text/uri-list with a valid local file URL for browser files and sample clips.
- An external file-manager drop and a compatible plug-in-editor drop receive the file URL.
- The existing StringPair MIME data still drives internal LMMS drop targets.
Items:
  - `R-workflow-183` Drag samples directly from the browser to plugins - partial (src/gui/FileBrowser.cpp:912 and src/gui/BrowserResults.cpp:130 drag a StringPairDrag; src/gui/StringPairDrag.cpp:60-63 sets only the StringPair MIME type (no setUrls)); complete: Samples drag from the browser onto tracks and built-in samplers (AudioFileProcessor, SlicerT), but the drag carries no file URL, so hosted plug-ins and the desktop cannot take it.

## Q050-134 Switch tabs with one active engine and restore each project

**P1 · L12 projects, files, import/export, interchange, recovery · feature · M**

Goal: Switch quickly among open projects while the single active engine, transport, and project-specific views follow the selected tab safely.

Why: This stage is reported at medium demand across two DAWs and completes the five-DAW multi-project tab workflow in the preceding stage.

Depends on: Open multiple editable projects in tabs
Acceptance:
- Switch between two open tabs and verify playback, tempo, selection, undo, and save target belong to the selected project.
- Switch away from a dirty project and return; verify its state is intact and its save/close behavior is explicit.
- Verify command-surface project switching addresses the same active document as the selected tab.
Items:
  - `R-workflow-41` Multiple projects open at once - partial (command ids reference.open, project.switch; src/gui/LibraryCommandMenus.cpp:162-172); complete: A read-only reference project and quick switching exist, but not several live projects in tabs with synced playback.

## Q050-135 Stage 1: keyboard and switch-accessible step sequencer

**P1 · L14 help, onboarding, accessibility · feature · L**

Goal: Select, toggle, and understand every step-sequencer cell by keyboard or adaptive switch.

Why: The sequencer request is high demand across three DAWs and also provides the first stage for R-instruments-95, an XL gaze/switch-input request.

Acceptance:
- Navigate the step grid with keyboard keys, change a cell's state, and verify focus remains visible and returns to the expected cell.
- Verify the focused cell exposes its step position, row/instrument, and on/off state to the accessibility API and a screen reader.
- Add and run a grid-navigation test that covers cell selection, state changes, and boundary movement.
- Map an adaptive switch to previous/next cell and activate selected step; verify mappings persist and a simulated switch provider drives the same actions.
Items:
  - `R-editing-71` Blind REAPER users repeatedly request an accessible step sequencer - missing (src/gui/editors/PatternEditor.cpp:57; docs/KNOWN-LIMITATIONS.md:2432); implement: The pattern editor has a canvas name and Qt accessibility bridge but no keyboard-addressable, spoken step-cell workflow.

## Q050-136 Accessible feedback for supported control surfaces

**P1 · L14 help, onboarding, accessibility · feature · L**

Goal: Use a supported hardware control surface and hear the DAW control name and value while operating it.

Why: The request is high demand across three DAWs and current evidence confirms MCU connection/state commands but no spoken control feedback.

Acceptance:
- With the MCU test driver, identify a track or supported plug-in parameter by spoken name and hear its current value and value changes.
- Verify hardware input and screen-reader feedback follow the same accessible control names used by the GUI.
- Run the focused control-surface tests and document any unsupported hardware or plug-in editors.
Items:
  - `R-instruments-10` Accessible hardware control surfaces - partial (surface.mcu_connect and surface.mcu_state; docs/KNOWN-LIMITATIONS.md:2432 says screen readers use Qt's accessibility bridge); complete: Screen-reader access and hardware control surfaces exist, but there is no spoken control and plug-in parameter feedback through the controller.

## Q050-137 MPE multi-note curve editor

**P1 · L07 note editor, MIDI editing, scales, chords, articulations · completion · L**

Goal: Draw and edit pitch, pressure, and timbre curves for one or several selected notes in the new note editor.

Why: Medium demand across four DAWs; this editor depends on the missing per-note expression stream and curve path.

Depends on: MPE expression event streams
Acceptance:
- The editor displays and edits time-varying MPE curves on multiple selected notes.
- An editor interaction test verifies point moves, undo, save and reload.
- A socket-driven playback test confirms the edited curve reaches the instrument.
Items:
  - `R-editing-43` MPE editing workflow friction - partial (docs/NOTE-EDITOR.md:113; docs/NOTE-EDITOR.md:126; docs/KNOWN-LIMITATIONS.md:2431); complete: Per-note pitch, pressure and timbre values are editable, but the expression lane edits one note at a time and does not provide pressure or timbre gesture curves.

## Q050-138 Expand the bundled multisample instrument library

**P1 · L11 browser, samples, presets, library · feature · L**

Goal: Users can load ready-to-play multisample instruments and find genre-organized stock sounds in the bundled library.

Why: This is marked high demand despite two DAWs, while the current 1,005 bundled samples do not provide the requested curated multisample set.

Depends on: Filter browser results by category
Acceptance:
- The package includes a curated set of multisample instruments with valid key/velocity zones and playable presets.
- Sounds carry genre/category metadata and appear under the matching browser filters.
- A clean-install check loads and plays representative presets, and the content manifest records provenance and license terms.
Items:
  - `R-instruments-16` Sparse bundled instruments and sample libraries - partial (data/samples/ has 1,005 files across drum, bass, instrument and effect folders; docs/KNOWN-LIMITATIONS.md:209-225 describes hosted instruments); complete: Stock instruments and a substantial sample library exist, but the library lacks broader curated ready-to-play multisample and genre collections.

## Q050-139 Consolidated into Q050-180

Owner approved on 2026-10-25; original requirements are retained in the canonical definition and review snapshot.

## Q050-140 Background autosave with off-thread file writes

**P1 · L12 projects, files, import/export, interchange, recovery · feature · M**

Goal: Keep project autosave responsive by serializing a consistent snapshot on the GUI thread and writing recovery files on a worker.

Why: The row reports high demand across two DAWs and confirms recovery writes currently block the GUI thread.

Acceptance:
- Trigger autosave on a large fixture and verify the GUI continues responding while the recovery file is written.
- Open the recovery file and compare canonical project state with the captured snapshot.
- Use the configured autosave interval in a headless fixture and verify the recovery artifact can be reopened.
Items:
  - `R-workflow-71` Background autosave - partial (src/gui/MainWindow.cpp:1743-1763 (autoSave writes the recovery file synchronously on the GUI thread; skips while mouse is pressed)); complete: Autosave exists but serialises and writes on the interface thread, so large projects stall the UI.

## Q050-141 Touch-first layout and large-target mode

**P1 · L13 customisation, shortcuts, themes, layout · feature · L**

Goal: Enable a touch-oriented layout with larger controls and usable panel spacing.

Why: High demand is reported for touch-first operation, but current evidence shows arranger pinch zoom without a tablet layout.

Acceptance:
- Enable touch layout and verify target sizes and panel spacing meet a documented touch minimum in representative editors.
- Use touch events to operate common selection and transport controls without mouse-only targets.
Items:
  - `R-ui-23` Touch-first DAW operation - partial (src/gui/ArrangerAids.cpp:111-121 (pinch zoom); plans/TRACKER-050.md:615 (WF-133: touch density + pinch, no tablet layout)); complete: Touch density and pinch zoom exist; there is no touch-first layout.

## Q050-142 Launcher cell editing and arrangement placement

**P1 · L02 launcher / session clips / performance · feature · M**

Goal: Edit launcher cells directly and carry clip material from the grid into the linear arrangement.

Why: Demand is high across 2 DAWs, while current evidence shows scene and slot launching but no direct cell editing or placement into the arrangement.

Acceptance:
- From the launcher grid, cut, copy, paste and duplicate a populated cell and insert a row; verify the affected cells through session.get_state.
- Place a selected cell into the arrangement and verify its notes or audio source and timing survive in the target clip.
- Drive the actions through registered commands and the grid UI; undo and redo restore and reapply the same cell and arrangement state.
Items:
  - `R-editing-25` MPC touch arranger and clip matrix - partial (session.launch_scene; session.launch_slot; docs/KNOWN-LIMITATIONS.md:2684); complete: The launcher and linear arranger coexist, but launcher cells do not yet provide the requested touch-oriented cut, copy, duplicate and row-edit workflow.

## Q050-143 Chord-following accompaniment core

**P1 · L07 note editor, MIDI editing, scales, chords, articulations · feature · L**

Goal: Generate a deterministic editable MIDI accompaniment that follows chord-track events and stops cleanly when the chord changes or ends.

Why: High demand across one DAW; the chord-follow command exists but there is no accompaniment player or generated performance output.

Acceptance:
- A chord fixture produces repeatable accompaniment notes at the expected chord boundaries.
- The result is editable as MIDI and a control-socket test verifies schedule, stop, and undo.
- Playback remains bounded and allocates nothing on the audio thread.
Items:
  - `R-instruments-21` Chord-aware generated accompaniment players - missing (chord.follow_set and chord.progression_generate; no accompaniment-player or style-performance command found); implement: Chord tracks and deterministic chord tools exist, but there is no responsive drummer, bass or keyboard accompaniment player.

## Q050-144 Editable, importable, and exportable workflows

**P1 · L13 customisation, shortcuts, themes, layout · completion · L**

Goal: Edit saved workflow steps and transfer workflows between Zene profiles.

Why: B-WF-114 spans four DAWs, and workflow recording/run exists while step editing and portable import/export remain documented gaps.

Acceptance:
- Record and save a workflow, then reorder and delete steps and change a step argument; run it and verify the edited sequence.
- Export a workflow, import it into a clean profile, run it, and verify action ids and arguments survive the round trip.
Items:
  - `B-WF-114` Macros and custom actions - partial (workflow.record_start/save/run (src/gui/MacroMenu.cpp:130; src/gui/ControlCommandsMacros.cpp:129); docs/KNOWN-LIMITATIONS.md:2459); complete: Recorded and saved workflows run from a Macros menu, but editing a workflow's steps and portable workflows are deferred.
  - `R-workflow-161` Native workflow macros - partial (src/gui/MacroMenu.cpp:130 (workflow.record_start); workflow.save; docs/KNOWN-LIMITATIONS.md:2459 (editing workflow steps is deferred)); complete: Users can record and save their own workflows, but they can't edit a workflow's steps.

## Q050-145 Document Zene's current free and activation model

**P1 · L14 help, onboarding, accessibility · docs · S**

Goal: Know whether the current Zene build has paid tiers or requires activation, and where policy changes will be announced.

Why: The input marks clarity about tiers and activation high demand, while its evidence finds no tier or activation checks in the product.

Acceptance:
- Document the verified current no-tier and no-activation state, citing the project's license and the absence of activation checks.
- State only current policy facts and identify the project's announcement location for any future policy change; do not promise an unapproved future pricing policy.
- Verify the wording against LICENSE.txt and a source search for activation or license-key checks.
Items:
  - `R-hosting-18` Free-tier permanence and activation clarity - partial (LICENSE.txt:21-36; rg for activation, subscription and license-key checks in src/include/plugins found no matches); docs: The current product has no tier or activation path, but its documentation does not state whether official free releases are intended to remain available.

## Q050-146 Keep wide and stereo capture blocks aligned

**P1 · L03 recording, takes, comping, inputs · fix · S**

Goal: Wide-channel routes and their stereo-bus view use the same captured frames without skew.

Why: A high-demand report for 1 DAW cites separate wide/stereo publication and drain steps as a possible misalignment, though the impact is not yet reproduced.

Acceptance:
- A block-boundary regression test compares unique wide-channel impulses with the corresponding stereo-bus frames under an interleaved drain schedule.
- If the test reproduces skew, the fix publishes a consistent block or derives the stereo view from the drained wide block.
Items:
  - `R-recording-52` Stereo and wide capture can be drained from different blocks - unknown (src/core/audio/AudioDevice.cpp:70-87 (pushInputFramesWide then pushInputFrames, two rings); src/core/AudioEngine.cpp:297-317 (wide ring drained separately); impact not reproduced); fix: The wide and stereo capture rings are published separately, so a drain between them may skew a wide route against a stereo take by one block.

## Q050-147 Give punch recording its own ruler range

**P1 · L03 recording, takes, comping, inputs · completion · M**

Goal: Users can set punch-in and punch-out positions independently of loop markers.

Why: Punch commands exist but users lack an independent ruler and handles, leaving this socket-oriented recording capability without its own interface.

Acceptance:
- The song editor draws a separate punch range with draggable handles and an arm control.
- Moving punch handles leaves loop markers unchanged, and a recording test captures only frames inside the punch range.
Items:
  - `R-recording-15` Punch recording still listed as unported - partial (transport.punch_set; transport.punch_here; docs/KNOWN-LIMITATIONS.md:880-883 (punch uses the loop range; no punch ruler with its own handles)); complete: Punch in/out works and gates capture, but the region is the loop markers rather than its own editable range.

## Q050-148 Clarify pitch-preserving warp documentation

**P1 · L05 audio editing, warping, stretching, fades · docs · S**

Goal: Users can tell which warp modes preserve pitch and what limitations remain for each mode.

Why: Demand is high from 1 DAW, while current docs say all warping changes pitch despite the exposed pitch-preserving modes.

Acceptance:
- Compare the warp documentation against the implemented warp.stretch modes and correct the blanket claim that all warping changes pitch.
- State the pitch-preservation caveats for each supported algorithm and give a command example that a reviewer can run.
Items:
  - `R-editing-29` Warping changes pitch - have (docs/KNOWN-LIMITATIONS.md:1548; docs/KNOWN-LIMITATIONS.md:640; commands_snapshot.json: warp.stretch); docs: Pitch-preserving WSOLA and Rubber Band modes exist, but an older limitations paragraph still says pitch-preserving stretch is not built.

## Q050-149 Add six-stem HTDemucs extraction

**P1 · L05 audio editing, warping, stretching, fades · feature · L**

Goal: Users can separate guitar and piano alongside drums, bass, vocals, and other with the pinned six-stem model.

Why: Six-stem support is high demand from 1 DAW, while the current result path is fixed to four stems.

Depends on: Choose stem-separation quality and compare output
Acceptance:
- Select the pinned six-stem model and verify drums, bass, vocals, other, guitar, and piano outputs against a known fixture.
- Verify model selection and all six result paths through stem.job_start, the clip menu, job state, and save/load.
Items:
  - `R-workflow-106` Six-part separation including guitar and piano - missing (command id stem.job_start: 'offline 4-stem separation (drums, bass, other, vocals)'); implement: Separation gives four stems; there is no guitar or piano stem.

## Q050-150 Use pitch-preserving practice speed with an A/B loop

**P1 · L05 audio editing, warping, stretching, fades · feature · M**

Goal: Musicians can slow or speed transport playback without changing pitch and repeat a selected passage with an A/B loop.

Why: Practice playback is high demand from 1 DAW, while clip.speed is tape varispeed that changes pitch and transport has no A/B loop.

Acceptance:
- Play a reference tone at multiple practice rates and verify pitch remains stable while playback duration changes.
- Set A/B points and verify transport repeats only the selected range without changing project tempo or source media.
- Verify rate and loop controls through the UI, agent commands, and save/load where project state is intended.
Items:
  - `R-workflow-94` AI practice and skill-development tools - partial (command ids detect.analyze (key/tempo), stem.job_start, clip.audio_to_midi (monophonic), clip.speed (tape varispeed, pitch moves); no transport practice-speed command); complete: Key/tempo detection, stems and monophonic transcription exist, but there is no pitch-preserving slow-down of the whole song for practice.

## Q050-151 Follow live tempo during practice

**P1 · L05 audio editing, warping, stretching, fades · feature · M**

Goal: Musicians can run a metronome that follows tempo detected from live audio input while practicing.

Why: Live tempo-follow is high demand from 1 DAW, while detect.analyze is offline and no input-following metronome command exists.

Depends on: Use pitch-preserving practice speed with an A/B loop
Acceptance:
- Feed a live click/performance at changing tempos and verify the metronome follows within the documented response window.
- Verify the feature can be stopped cleanly, reports its state, and does not alter the project tempo map unless requested.
- Exercise start, stop, and read-back through the agent control surface.
Items:
  - `R-workflow-101` Moises combines smart metronome, key, tempo, and separation for practice - partial (command ids transport.tempo_map_from_clip (beat mapping from a performance), detect.analyze, stem.job_start; no live tempo-following metronome or practice speed); complete: The grid and click can follow a recorded performance by beat mapping, but not a live one, and there is no practice speed change.

## Q050-152 Close the modulation target destruction race

**P1 · L06 automation and modulation · fix · S**

Goal: Remove or safely retire a modulated parameter without playback writing through a destroyed object.

Why: Demand is high (1 DAW); the publisher checks an atomic liveness token and then dereferences a raw model pointer, leaving the reported destruction race unresolved.

Acceptance:
- Add a deterministic concurrency regression that pauses after the target-liveness check, destroys or rebinds the target, then resumes modulation.
- The test proves the object remains alive through the write or that the audio thread skips it; run the test under the available address/thread sanitizer configuration.
- No locking or allocation is added to the audio-thread path.
Items:
  - `R-performance-28` Modulation target lifetime risk - unknown (src/core/ModulationLayerPublisher.cpp:141-157 (raw model pointer, token cleared on destroyed), :183-193 (token check then use)); fix: The audio thread checks the liveness token and then writes through the raw pointer, so a target destroyed between the two is a use-after-free unless destruction is serialised with the audio thread, which was not confirmed.

## Q050-153 Edit several automation lanes together

**P1 · L06 automation and modulation · feature · L**

Goal: Select and edit points across multiple automation lanes and clips in the arranger.

Why: Demand is high (1 DAW); the current editor works on one automation clip at a time and point editing lacks the requested multi-selection.

Acceptance:
- The arranger can show several automation lanes at once and select points across lane and clip boundaries.
- Move, delete, and value edits apply to the selection with clear selection feedback and a single undo step.
- A GUI interaction test covers multi-lane selection and edits, and agent commands can read back the resulting points.
Items:
  - `R-workflow-90` Automation editing friction - partial (src/gui/editors/AutomationEditor.cpp (one clip at a time); docs/KNOWN-LIMITATIONS.md:2528 (keyboard edits one point, no multi-selection)); complete: The automation editor edits one clip of one parameter at a time, and keyboard editing has no multi-point selection.

## Q050-154 Live-code panel

**P1 · L07 note editor, MIDI editing, scales, chords, articulations · completion · M**

Goal: Edit Lua live-code scripts in the interface and schedule, unschedule, and inspect them from the panel.

Why: Medium demand across two DAWs; livecode.schedule and related commands exist but are socket-only without a user interface.

Acceptance:
- The panel edits a script, shows compile errors, and schedules or cancels it through livecode commands.
- A headless control-surface test verifies the same schedule state and one undo restores the previous script.
- The panel documents script memory and schedule bounds.
Items:
  - `R-workflow-80` Lua phrase scripting and live coding - partial (command ids livecode.schedule/unschedule/get_state; rg 'livecode.' over src/gui finds no interface; note.generate (euclid/random)); complete: Lua can be scheduled on bar/beat boundaries and rhythms generated, but live coding is socket-only with no editor panel or phrase notation.

## Q050-155 N-channel routing graph and per-channel sends

**P1 · L09 routing, buses, sends, sidechain, I/O · completion · L**

Goal: Route audio buses with up to 64 channels through device pins and the mixer graph, including independent sends for each channel pair.

Why: The current mixer offers fixed surround layouts and per-device pins rather than general N-channel buses; the request has high demand even though its evidence names 1 DAW.

Depends on: Mono and discrete bus formats
Acceptance:
- A test builds a 64-channel bus graph through port.set_pin and mixer routing commands, then confirms channel-pair send state through routing.matrix and mixer.get_state.
- A render with a distinct signal on each channel confirms routing and send levels without cross-channel leakage; command changes undo in one step.
Items:
  - `R-mixing-24` Flexible multichannel routing - partial (routing.matrix + src/gui/MixerRoutingMatrix.cpp; mixer.route_to / send_to; port.set_pin; mixer.channel_format (stereo or fixed surround formats)); complete: Routing, sends and a matrix are built, but tracks and channels are stereo (or fixed surround) rather than freely n-channel.

## Q050-156 Record directly into a sampler

**P1 · L10 devices, instruments, effects, plug-in hosting · feature · M**

Goal: Users can capture an input take directly into AudioFileProcessor or SlicerT for immediate playback and chopping.

Why: Recording exists for session slots and takes but not AudioFileProcessor or SlicerT; demand is high in 1 DAW.

Acceptance:
- Record a bounded input fixture into each supported device and verify the resulting sample is loaded and saved.
- Expose the action through GUI and agent command tests.
Items:
  - `R-recording-31` Direct-to-pad standalone sampling - partial (session.record_slot; record.retro_capture_to_take; rg record over plugins/AudioFileProcessor and plugins/SlicerT found nothing); complete: Input can be recorded to tracks and cells, but not straight into a sampler or slicer for chopping.

## Q050-157 Plug-in descriptor lifetime check

**P1 · L10 devices, instruments, effects, plug-in hosting · fix · S**

Goal: Users can rescan plug-ins without invalidating descriptors held by loaded instances.

Why: The descriptor store is replaced on rescan while live plug-ins retain descriptor data; the request is high demand in 1 DAW but the failure is unconfirmed.

Acceptance:
- Run the ASan cache-served-load/rescan/save reproducer and establish a failing or clean result.
- If it fails, retain descriptor ownership until all instances release it and rerun the reproducer.
Items:
  - `R-effects-8` Active plug-in library lifetime during rescans - unknown (src/core/PluginFactory.cpp:430 replaces the cached-descriptor store on every scan; src/core/Plugin.cpp:208-211 copies pi.descriptor into the key a live instance is created with; QLibrary objects are shared_ptr and not unloaded); fix: A live instance's key may keep a descriptor pointer into the store a rescan frees; unproven either way.

## Q050-158 Chord-aware keyboard navigation in the MIDI editor

**P1 · L14 help, onboarding, accessibility · feature · M**

Goal: Navigate and select notes and chords by keyboard while hearing their musical location.

Why: The input marks non-visual note and chord navigation high demand, and the current editor documentation identifies a partial keyboard workflow.

Acceptance:
- Move through notes by pitch and time, identify notes sharing a chord onset, and select or edit them from the keyboard.
- Verify the focused note's pitch and bar/beat location are exposed to a screen reader.
- Add tests for chord grouping, selection, and navigation across note boundaries.
Items:
  - `R-editing-33` Non-visual MIDI note and chord navigation - partial (docs/NOTE-EDITOR.md:94; docs/NOTE-EDITOR.md:97; docs/KNOWN-LIMITATIONS.md:2426); complete: The editable note list and keyboard note editing are present, but chord-aware non-visual selection and navigation remain absent.

## Q050-159 Explain patterns, arrangement clips, launcher cells, and mixer routing

**P1 · L14 help, onboarding, accessibility · docs · S**

Goal: Understand how Zene's three arrangement methods differ and how each track reaches a mixer channel.

Why: The input rates this missing mental-model explanation high demand, and the manual currently covers the launcher without relating it to patterns and arrangement clips.

Acceptance:
- Add a manual section and one tour step comparing patterns, arrangement clips, and launcher cells with a concrete example.
- Verify the example traces a track's signal to its mixer channel using the current UI and command names.
Items:
  - `R-workflow-92` One DAW, two arrangement methods - partial (data/manual/index.html:155-159 covers the launcher; no manual section relating patterns, arrangement clips and mixer channels); docs: Patterns, arrangement clips and mixer routing are all there, but nothing explains how they relate for someone from a linear DAW.

## Q050-160 Add pre-fader track freeze

**P1 · L15 platform: performance, stability, Linux audio, install, hardware controllers · feature · M**

Goal: Users can freeze a track before its fader and continue adjusting its live fader, pan and sends.

Why: The queue rates pre-fader freeze high demand, while existing track freeze bakes fader, pan and sends into the take.

Acceptance:
- A render comparison proves the frozen take is pre-fader while live fader, pan and sends still affect playback.
- A UI and command test round-trip the pre-fader choice through save/load and undo.
Items:
  - `R-performance-42` Proper freeze and pre-fader freeze - partial (freeze.track (take 'carries the track's devices, fader, pan and sends'); rg 'pre.?fader' in ControlCommandsFreeze.cpp/ControlBounceSupport.cpp: none); complete: Freeze is reversible, but it always bakes the fader, pan and sends, so there is no pre-fader freeze that keeps later gain staging live.

## Q050-161 Remove per-period automation ramp locking

**P1 · L15 platform: performance, stability, Linux audio, install, hardware controllers · fix · S**

Goal: Audio rendering can read cached automation ramps without taking a mutex on every value-buffer request.

Why: The queue identifies a mutex on every valueBuffer call and rates the performance pain high demand.

Acceptance:
- A concurrent ramp test proves cache hits avoid the mutex while recomputation remains correct under updates.
- An audio-thread instrumentation test confirms no lock is taken on a cache hit and automation output matches the existing render.
Items:
  - `R-performance-43` Automation ramp lookup locking - partial (src/core/AutomatableModel.cpp:568-570 (QMutexLocker on every valueBuffer() call); include/AutomatableModel.h:579 (mutex used only there)); fix: Every per-period value-buffer lookup takes a mutex before the cache check; it is only contended between audio-path callers, but it is a lock on the render path.

## Q050-162 Add synced transient-repeat stretching

**P1 · L05 audio editing, warping, stretching, fades · feature · M**

Goal: Users can repeat or gate transient segments at a synced or free repeat rate to fill a warped clip.

Why: Transient-repeat stretching is medium demand across 9 DAWs, and warp.stretch currently accepts only resample, preserve_pitch, and rubberband.

Depends on: Complete the specified warp mode set
Acceptance:
- Stretch a transient fixture in repeat and gate modes and verify the selected free or tempo-synced rate fills the target duration.
- Verify mode selection and rate through the UI, agent state, save/load, and a rendered-audio regression.
Items:
  - `R-editing-88` Add a classic transient-repeat stretch mode - missing (warp.stretch modes are resample, preserve_pitch, rubberband only (commands_snapshot.json)); implement: There is no transient-repeat (Beats-style) stretch mode.

## Q050-163 Slice audio into drum-rack pads

**P1 · L05 audio editing, warping, stretching, fades · feature · M**

Goal: Users can send sample slices to drum-rack pads and receive a MIDI clip that plays each slice.

Why: Drum-rack slicing is medium demand across 9 DAWs, while clip.slice_to_instrument currently targets SlicerT only.

Depends on: Edit and audition transient and slice markers
Acceptance:
- Slice a fixture into a drum rack and verify each pad contains the expected audio slice and MIDI note mapping.
- Play the generated MIDI clip, then save/reload and verify the pad and note assignments remain intact.
- Expose the same target choice through the agent command.
Items:
  - `R-editing-79` More complete audio clip and sample editing - partial (window.sample_editor (zoom, sample units); arrangement.modes snap_to_zero_crossings; clip.slice_to_instrument -> SlicerT (src/gui/shell/ShellCommandMenus.cpp:210); track.drum_rack); complete: A zoomable sample editor and zero-crossing snap exist, but slices go to a SlicerT track, not into a drum-rack preset of pads.

## Q050-164 Expose per-sample modulation in the panel

**P1 · L06 automation and modulation · feature · S**

Goal: Turn per-sample evaluation on or off for a bound modulation route from the Modulators panel.

Why: Demand is low (1 DAW), but per-sample evaluation is still socket-only and the item is a UI gap for an existing capability.

Acceptance:
- The Modulators panel exposes the route's per-sample switch and shows its current value after reopening the panel.
- Toggling it changes route evaluation and survives project save/load and undo/redo.
- KNOWN-LIMITATIONS.md no longer says route-depth editing is socket-only where the GUI already provides Depth...
Items:
  - `R-ui-63` Modulator per-sample mode and depth editing are socket-only - partial (src/gui/InstrumentCommandMenus.cpp:146 (route Depth... in the interface); docs/KNOWN-LIMITATIONS.md:396-397 (per-sample switch still socket-only; depth listed as socket-only, now stale)); complete: Depth editing has reached the interface; the per-sample switch has not, and KNOWN-LIMITATIONS still lists depth.

## Q050-165 Tracker effect cells

**P1 · L07 note editor, MIDI editing, scales, chords, articulations · completion · M**

Goal: Add velocity and MIDI effect cells to tracker rows and voices so users can edit those values without leaving the grid.

Why: Low demand across one DAW for event-list tracker cells, staged after the keyboard-first tracker requested by three DAWs at high demand.

Depends on: Keyboard-first tracker editor
Acceptance:
- Tracker cells edit note, velocity, and supported effect values for the selected row and voice.
- A UI test verifies cell navigation, command readback, save, and undo.
- Malformed cell values are refused with a useful error.
Items:
  - `R-editing-143` Tracker-style MIDI entry - partial (MIDI Event List... (src/gui/noteeditor/NoteEditorMenus.cpp:253); window.item_list notes (editable list)); complete: An editable event list exists, but there is no tracker grid with channel columns, rows per step and per-step effects.

## Q050-166 Mono and discrete bus formats

**P1 · L09 routing, buses, sends, sidechain, I/O · completion · M**

Goal: Choose mono or a discrete N-channel layout for a mixer bus and read the layout back after save and reload.

Why: MixerSurround currently lists only stereo, LCR, quad, 5.1 and 7.1 (MixerSurround.cpp:59-66); demand is low from 1 DAW alone, but these formats are the prerequisite for the high-demand flexible-routing request.

Acceptance:
- mixer.channel_format accepts mono and discrete channel-count layouts and mixer.get_state reports their speaker/channel roles.
- A project round-trip preserves each layout and a render routes every discrete channel to its matching output.
Items:
  - `R-mixing-47` Explicit multichannel bus configuration - partial (src/core/MixerSurround.cpp:59-66 (stereo, lcr, quad, 5.1, 7.1 only); mixer.channel_format, mixer.downmix_options); complete: Channel formats cover named stereo/LCR/quad/5.1/7.1 layouts, but there is no mono bus format and no discrete N-channel (role-less) pass-through format.

## Q050-167 Keep legacy macOS audio wakeups lock-free

**P1 · L15 platform: performance, stability, Linux audio, install, hardware controllers · fix · S**

Goal: macOS audio rendering can wake its worker without taking a mutex or allocating on the render path.

Why: The queue identifies a mutex on the legacy macOS worker wake path, which can stall audio even though demand is low.

Acceptance:
- A macOS x86_64 test exercises the legacy wake path and verifies the render-side wake uses no mutex or allocation.
- A repeated worker-wakeup stress test completes without deadlock or missed periods; the supported deployment target is documented.
Items:
  - `R-platform-53` Legacy macOS worker wake-up locks and allocates on the render path - partial (src/core/AudioEngineWorkerThread.cpp:181-189,207-212 (below macOS 11 the wake takes a std::mutex); Brewfile:21 (qt@5 for macOS); comment cites hosted macos-x86_64 builds taking that path); fix: On the Intel macOS build each worker wake takes a mutex on the render path, which can block under contention (the notify itself does not allocate).

## Q050-168 Consolidate MIDI arrangement clips

**P1 · L04 arranger, timeline, clip operations, markers, tempo map · completion · M**

Goal: Users can consolidate a selected MIDI range into one MIDI clip and undo the edit as one step.

Why: The 7-DAW row and RangeEditsTest evidence show that clip.consolidate currently covers sample tracks only, leaving MIDI consolidation absent.

Acceptance:
- A regression test consolidates a MIDI range with notes intact and verifies undo restores the original clips.
- The range consolidate action handles MIDI tracks as well as the existing sample-track path and refuses unsupported empty or boundary cases clearly.
Items:
  - `B-ED-031` Glue / join / consolidate clips - partial (clip.consolidate; src/core/ControlCommandsRange.cpp:357-372,569-571; tests/src/core/RangeEditsTest.cpp:182-187); complete: Audio sample-track regions consolidate, but MIDI regions are explicitly excluded.

## Q050-169 Correct hosted-instrument PDC documentation

**P1 · L01 engine wiring (engine capability not reachable) · docs · S**

Goal: Users can see which hosted-instrument formats receive automatic delay compensation and where support is limited.

Why: MX-157 is partial across 6/8 DAWs, and the merged documentation both denies hosted-instrument PDC and describes CLAP/VST3/LV2 support, leaving a contradictory capability statement.

Acceptance:
- `docs/KNOWN-LIMITATIONS.md` no longer contains a blanket claim that instrument latency compensation is absent alongside the supported-host section.
- The document names CLAP, VST3 and LV2 as supported instrument hosts, and out-of-process, VST2 (Vestige) and Carla as unsupported; it states built-in instrument latency is 0.
- The documented polling, clamping, automation and monitoring limits agree with the hosted-latency behavior covered by `InstrumentLatencyPdcTest`.
Items:
  - `B-MX-157` Automatic delay compensation - partial (docs/KNOWN-LIMITATIONS.md:218 contradicts :978-1000; pdc.report; InstrumentLatencyPdcTest); docs: Native CLAP, VST3 and LV2 instrument latency now reaches PDC, but the limitations page still says instrument compensation is absent and other host types remain unsupported.

## Q050-170 Filter browser results by category

**P1 · L11 browser, samples, presets, library · completion · M**

Goal: Users can narrow the library by category alongside kind and source.

Why: Six DAWs are cited, while the current Search view offers kind and root filters but no category field.

Acceptance:
- The Search view exposes a category filter populated from catalog metadata beside the existing kind and source filters.
- Selecting a category filters matching results in both direct and indexed search; Any category restores the full result set.
- A GUI regression check covers category assignment, filtering, and saved-collection round-trip.
Items:
  - `B-WF-007` Browse by kind, category or source - partial (src/gui/BrowserSearchView.cpp:108-120; include/BrowserSearchView.h:55); complete: The browser filters by file kind and library root, but it has no category facet.

## Q050-171 Correct VCA edit-group limitation notes

**P1 · L04 arranger, timeline, clip operations, markers, tempo map · docs · S**

Goal: Readers can tell which VCA and edit-group operations are available in the interface and which limits remain.

Why: The five-DAW row is marked implemented, but KNOWN-LIMITATIONS.md still retains a pre-M3.7 claim that VCA and edit groups have no interface.

Acceptance:
- Update docs/KNOWN-LIMITATIONS.md so the pre-M3.7 interface claim is clearly historical and current VCA/edit-group UI and remaining limitations are accurate.
- Review the referenced VCA UI and command paths in the merged tree against the revised text.
Items:
  - `B-ED-034` Edit groups (tracks or clips edited together) - have (src/gui/CommandLists.cpp:60-72; src/gui/shell/ShellCommandMenus.cpp:322; docs/KNOWN-LIMITATIONS.md:231-249); docs: The interface can create groups, edit their track set and phase lock, but KNOWN-LIMITATIONS still describes edit groups as socket-only.

## Q050-172 Device quick controls on mixer strips

**P1 · L10 devices, instruments, effects, plug-in hosting · feature · M**

Goal: Users can map chosen device parameters to quick controls on a mixer strip or track header.

Why: The existing strip options do not provide mapped device controls and the feature is cited across 5 DAWs.

Acceptance:
- Map, rename, adjust, and clear a quick control from the strip UI.
- Save/load mappings and verify they target the same parameter through agent read-back.
Items:
  - `B-MX-090` Device controls on the strip or track header - partial (src/gui/MixerChannelView.cpp:198-205; include/MixerViewOptions.h:50-51,75-76); complete: Mixer strips expose four rack macro knobs, but not arbitrary selected device controls or controls on track headers.

## Q050-173 Show, sort, and edit supported audio metadata

**P1 · L11 browser, samples, presets, library · completion · M**

Goal: Users can inspect and sort embedded audio metadata in browser results and edit the writable fields supported by each format.

Why: Five DAWs are cited, and the current results table shows only name, kind, size, modified time, and user tags despite BrowserCatalog already exposing audio metadata.

Depends on: Expose tag and embedded-metadata search in the browser
Acceptance:
- With metadata probing enabled, result columns show the supported embedded fields, including title, artist, album, genre, and audio format facts where present.
- Sorting works for text and numeric metadata columns.
- Editing a supported writable field persists to the media file; reopening and searching confirms the saved value.
Items:
  - `B-WF-011` Metadata columns, sorting and editing - partial (src/gui/BrowserSearchView.cpp:144-147, 481; include/BrowserCatalog.h:121-125); complete: The result table sorts Name, Kind, Size, Modified and Tags and edits user tags, but does not display or edit embedded title, artist or album metadata.

## Q050-174 Low-latency monitoring for latent instruments

**P1 · L01 engine wiring (engine capability not reachable) · feature · M**

Goal: Users can monitor a latency-producing instrument with less delay while retaining the normally aligned playback path.

Why: MX-159 is missing across 4/8 DAWs, and the merged `transport.low_latency` path only toggles latency-bearing `Effect` objects, leaving latent instruments outside its monitoring policy.

Acceptance:
- A headless control-surface regression with a latency-reporting instrument fixture proves `transport.low_latency` applies the documented policy to the armed instrument path and reports the affected path in its result.
- An audio integration check shows monitoring avoids the extra PDC alignment delay in low-latency mode, then disabling the mode restores normal alignment and output.
- The mode remains session-only: enabling it does not alter saved project state, and non-armed paths remain unchanged under the default `scope: armed`.
Items:
  - `B-MX-159` Low-latency monitoring mode - partial (transport.low_latency; src/core/ControlCommandsLowLatency.cpp:24-26; docs/KNOWN-LIMITATIONS.md:1002); complete: The mode bypasses latent effects on monitored paths, while latent instruments still add monitoring delay.

## Q050-175 Chorus and phaser stock effects

**P1 · L10 devices, instruments, effects, plug-in hosting · feature · M**

Goal: Users can add stereo-native Chorus and Phaser devices from the stock effect catalogue.

Why: The stock list lacks native Chorus and Phaser devices and the comparison cites 4 DAWs.

Acceptance:
- Both effects appear in the catalogue and process a stereo fixture while preserving stereo separation.
- Verify parameters through the UI and an agent load/process test.
Items:
  - `B-MX-153` Modulation effects - partial (plugins/Flanger/FlangerEffect.cpp:44-47; plugins/LadspaEffect/caps/Chorus.cc:139-142; plugins/LadspaEffect/caps/Phaser.cc:124-127); complete: Flanger is native and Chorus/Phaser are bundled, but the latter are mono LADSPA variants rather than a complete stereo modulation set.

## Q050-176 Zoom history with back and forward

**P1 · L13 customisation, shortcuts, themes, layout · completion · S**

Goal: Move through several prior arrangement zoom levels and toggle between the current and previous extent.

Why: Four DAWs are counted, and arrangement.zoom_back currently swaps only with the previous zoom instead of keeping back/forward history.

Acceptance:
- Visit at least three distinct arrangement zoom levels, exercise back and forward, and verify each prior extent is restored.
- Use zoom toggle to swap between the current and prior extent.
Items:
  - `B-WF-094` Zoom history and zoom toggle - partial (arrangement.zoom_back swaps with the last zoom only: src/gui/ControlCommandsArrangerView.cpp:192-204); complete: The zoom toggle exists, but the history is one level deep and wheel/pinch zoom is not recorded (docs/KNOWN-LIMITATIONS.md:2422).

## Q050-177 Transcribe polyphonic audio to MIDI

**P1 · L05 audio editing, warping, stretching, fades · feature · L**

Goal: Users can convert a chordal audio recording into overlapping editable MIDI notes while retaining the monophonic workflow.

Why: No demand score is recorded for the 3-DAW item, but the related audio-to-MIDI request is medium demand across 7 DAWs and the current command is monophonic only.

Acceptance:
- Run a known chord fixture and verify simultaneous pitches, note starts, and note ends in the generated MIDI clip.
- Run the existing monophonic fixture and verify its result is unchanged.
- Verify the transcription is available through the agent command and survives project save/load.
Items:
  - `B-ED-160` Audio to MIDI - partial (clip.audio_to_midi; src/gui/shell/ShellCommandMenus.cpp:312); complete: Audio-to-MIDI uses monophonic YIN pitch tracking, so polyphonic transcription remains unavailable.

## Q050-178 Build a browser remote-control client

**P1 · L16 collaboration and sharing · feature · L**

Goal: A phone or tablet browser can use a remote invite to control transport, markers, and mixer faders over a browser-compatible connection.

Why: Demand is unreported across 3 DAWs, and the remote role is command-only today while KNOWN-LIMITATIONS.md:2392 confirms there is no HTML client and browsers cannot use TLS-PSK.

Depends on: Label collaboration as experimental and document its limits
Acceptance:
- A LAN remote invite opens a browser client without requiring TLS-PSK, while native collaboration authentication remains unchanged.
- The client exposes transport, markers, and faders only; an attempted project-edit command is refused by the host's remote-role allowlist.
- An integration check drives transport and a fader from the browser client and reads their state back, with expired or invalid invites refused.
Items:
  - `B-WF-052` Remote-control app - partial (collab.invite role 'remote' (transport, markers, faders); docs/KNOWN-LIMITATIONS.md:2392 (no HTML remote page, browsers cannot do TLS-PSK)); complete: A remote role exists over the P2P channel, but it needs another Zene instance; there is no phone or tablet remote client.

## Q050-179 Show remaining record time in the UI

**P1 · L03 recording, takes, comping, inputs · completion · S**

Goal: Users can see the remaining-time estimate while setting up or monitoring a recording.

Why: The queue cites a record.remaining command with no GUI caller, leaving a socket-only recording status out of the user interface.

Acceptance:
- The recording controls or status area display record.remaining and update as the recording budget changes.
- A Qt GUI test covers zero, finite, and unavailable estimates; the command's existing socket response remains unchanged.
Items:
  - `B-WF-085` Remaining record time - partial (record.remaining; rg for record.remaining under src/gui returned no UI call); complete: The command reports estimated remaining recording time, but no recording control displays it.

## Q050-180 Support multiple positioned video clips with correct lane and render behavior

Goal: Users can add, independently address, position, save and reopen multiple project video clips; the lane draws and seeks each clip, while playback and range rendering show the correct frame at each song tick without stale frames.

Acceptance:

- Attach two video files at distinct song-tick positions and offsets; save and reopen, then verify both files, metadata, positions and identities remain independently addressable.
- Draw each clip over its own lane span; clicking either span seeks to its song position, and the viewer follows the clip active at the playhead without replacing the other clip.
- Play and render a range spanning both clips; verify correct frames and project-tempo timing in each clip, and verify no stale frame appears before, after, or in a gap.
- Drive clip creation, independent position/offset edits, save and readback through registered commands and MCP; exercise the equivalent add/edit workflow through the video lane UI.
- Each mutating clip operation is one undo/redo step; load legacy single-<video> projects as one clip and preserve empty-project reset behavior.

Execution: Q050-180.1; preserve historical batch membership under Q050-168.

## Q050-181 Consolidated into Q050-253

Owner approved on 2026-10-25; original requirements are retained in the canonical definition and review snapshot.

## Q050-182 Edit and audition transient and slice markers

**P2 · L05 audio editing, warping, stretching, fades · completion · L**

Goal: Users can inspect the full transient map, adjust slice positions, and audition each slice in the sample-editing workflow.

Why: Transient and slice editing is medium demand across 3 DAWs, and current detection exposes markers without an in-context editor and fast audition path.

Acceptance:
- Verify the agent command returns all detected transients and supports adding, moving, and removing markers.
- In the waveform editor, audition a selected marker and reposition it without losing the current zoom or selection.
- Run slice-to-instrument and verify it uses the edited marker map.
Items:
  - `R-editing-7` Editable transient map and slice-to-MIDI workflow - partial (src/core/ControlCommandsAudioSlice.cpp:107-121,251-267; command ids clip.transients and clip.slice_to_instrument); complete: Full transient lists and slice-to-MIDI behavior exist, but users cannot edit the detected transient map itself.
  - `R-editing-21` Sample slicing and transient detection - partial (commands_snapshot.json: clip.transients, clip.slice_to_instrument; src/gui/SampleSelectionView.cpp:46); complete: Transient detection and slicing exist, while marker audition and navigation still take separate waveform-selection and zoom steps.
  - `R-instruments-33` Transient-aware sample slicing - partial (clip.slice_to_instrument; warp.quantize; arrangement.modes; no editable transient-slice marker surface found in src/gui/ or the command snapshot.); complete: Transient detection and slicing exist, but marker audition and manual placement remain incomplete.

## Q050-183 Switch audio devices live with route and status feedback

**P2 · L15 platform: performance, stability, Linux audio, install, hardware controllers · feature · L**

Goal: Users can change the running audio device without quitting and can inspect the backend, test output and protect routes before switching.

Why: Two rows report device switching as next-start-only, with related route-loss and generic-error gaps; demand ranges low to medium.

Acceptance:
- A live-instance test switches devices, reopens the engine and resumes the project without restarting Zene.
- Before switching, the UI lists routes that will not map and offers cancel; a failed open shows AudioEngine’s specific reason.
- The audio settings page identifies the running backend and plays a test tone; device state and refused route mappings are available through a registered command.
Items:
  - `R-performance-81` Switch audio devices while running - missing (audio.device_set ('The running device is NOT switched', applied=next_start)); implement: Changing the audio device needs a restart.
  - `R-platform-67` JACK setup is a common Rosegarden first-run hurdle - partial (src/gui/MainWindow.cpp:579-592 (generic 'Audio device setup failed' box, then the settings dialog, then restart); src/core/AudioEngine.cpp:1159-1167 (the specific reason exists); audio.device_set ('The running device is NOT switched')); complete: A failed device opens the audio settings, but the dialog does not show the backend's own reason and any change needs a restart.
  - `R-recording-76` Protect routing when an audio device changes - partial (audio.device_set (applies at next start); audio.io_setup (channel names/buses kept per device)); complete: Per-device I/O settings are kept, but a device change shows no before/after routing preview or cancel step.
  - `R-ui-57` Audio backend selection can be opaque after upgrades - partial (audio.device_list (running vs configured backend); src/gui/AudioDeviceSetupWidget.cpp:30-38; no test-tone action found); complete: Backend status is reported, but there is no one-screen device test.
  - `R-workflow-300` Playback-engine switching without quitting - missing (audio.device_set: 'The running device is NOT switched ... applied=next_start' (commands_snapshot.json)); implement: Changing the audio device or backend requires restarting Zene.

## Q050-184 Audio-to-chord analysis

**P2 · L07 note editor, MIDI editing, scales, chords, articulations · feature · L**

Goal: Analyze polyphonic audio clips with non-generative chroma analysis and write detected chord events into the chord track.

Why: Medium demand across three DAWs; current chord detection reads MIDI notes and explicitly does not analyze audio.

Acceptance:
- A fixture containing timed triads produces chord events with the expected roots and spans.
- A headless command writes the results to the chord track and the arranger displays them.
- No model download or generative service is required, and uncertain frames are reported rather than silently named.
Items:
  - `R-ai-13` Synth Players and chord recognition - partial (chord.detect/chord.progression_generate (MIDI only); include/ChordDetect.h:16 ('does not read audio')); implement: Chords are detected from MIDI only; audio chord recognition is missing (Synth Players fall under the deferred WF-144).
  - `R-editing-23` Extract chords from polyphonic audio - partial (docs/CHORD-TRACK.md:64; docs/CHORD-TRACK.md:80); complete: Chord detection works on MIDI note lists, but the detector explicitly does not analyze audio.
  - `R-editing-56` Chord assistant and audio-to-chord analysis - partial (chord.progression_generate; docs/CHORD-TRACK.md:80; docs/CHORD-TRACK.md:82); complete: Progression generation, MIDI chord detection and a visible chord track exist, but audio-to-chord analysis is absent.

## Q050-185 Write the Linux audio onboarding guide

**P2 · L15 platform: performance, stability, Linux audio, install, hardware controllers · docs · L**

Goal: Linux users can set up real-time permissions, interfaces, USB devices, JACK/PipeWire and external monitoring tools.

Why: Four rows describe missing Linux setup guidance at medium or low demand, including RT limits, device labels and USB-interface troubleshooting.

Depends on: Document PipeWire setup and backend boundaries
Acceptance:
- The guide explains Zene’s I/O labels, interface mixers/DSP, RT limits and audio-group checks, and Steam Deck USB troubleshooting.
- First-run and Help-menu links reach the same guide and identify each missing setup item with its remedy.
- Steps are verified on a supported Linux setup and avoid claiming untested hardware support.
Items:
  - `R-platform-66` Linux lacks vendor-specific interface control software - partial (audio.io_setup (channel and bus names); docs/KNOWN-LIMITATIONS.md:2312 (labels only, not JACK port names); rg over docs/ found no guidance on interface mixers or vendor DSP); docs: Zene names input and output channels, but nothing tells Linux users which interface functions must be set in alsamixer or vendor tools outside the DAW.
  - `R-platform-70` Steam Deck audio-device compatibility is a real entry barrier - partial (src/core/AudioEngine.cpp:1151-1170 (falls back to Dummy and names the device that failed); rg over docs/ found no USB-interface troubleshooting guidance); docs: Zene keeps running and names a device it could not open, but there is no guide for getting a USB interface detected on Linux handhelds and laptops.
  - `R-workflow-119` New Linux users need a reliable low-latency starter path - partial (src/gui/MainWindow.cpp:574 (first-run setup dialog); rg for rtprio, RLIMIT_RTPRIO, limits.conf or audio group in src found nothing outside 3rdparty); complete: The first-run setup chooses the audio device, but nothing checks real-time privileges, audio-group membership or the bridged plug-in setup.
  - `R-workflow-192` Real-time privileges and low-latency tuning remain onboarding work - partial (src/core/audio/AudioJack.cpp:67-70 (generic JACK failure message); rg rtprio, RLIMIT_RTPRIO and audio group over src found nothing); complete: Setup errors are generic, and nothing checks or explains missing real-time permissions.

## Q050-186 Resume media and collaboration state after disconnect

**P2 · L16 collaboration and sharing · feature · L**

Goal: After a network interruption, peers resume incomplete media transfers and continue the ordered edit stream from the last applied change, falling back to a project resync only when needed.

Why: Rated demand is medium to low across 1-3 DAWs, with B-WF-050 unrated; KNOWN-LIMITATIONS.md:2387,2392 records no media transfer or reconnect, and the large-asset report describes a 261 MB read-and-zip delay.

Depends on: Transfer and hash-check project media between peers
Acceptance:
- A large-asset fixture transfers in bounded chunks with visible progress; after a forced disconnect the receiver resumes at a verified chunk boundary and the final hash matches.
- A two-instance test drops the connection after a streamed edit, reconnects, resumes from the last applied change without duplicating edits, and uses collab.resync only when the cursor cannot be reconciled.
- The transfer does not buffer the whole asset in memory, and docs state that reconnect works only over an established/re-established route rather than adding hosted NAT traversal.
Items:
  - `B-WF-050` Shared editing with collaborators - partial (collab.host/join/invite (src/gui/CollabPanel.cpp:123); docs/KNOWN-LIMITATIONS.md:2387 (samples not transferred), :2392 (no reconnect)); complete: Experimental, off-by-default P2P shared editing ships per the owner decision, but samples are not sent to peers and a dropped peer cannot reconnect.
  - `R-collaboration-5` Collaboration media transfer and internet resilience - partial (docs/KNOWN-LIMITATIONS.md:2381-2387,2392); complete: Peer collaboration currently transfers no samples and cannot reconnect after a dropped connection; reliable NAT traversal would need a service.
  - `R-performance-38` Large-asset collaboration transfer - partial (docs/KNOWN-LIMITATIONS.md:2387 ('Samples are not transferred'); docs/KNOWN-LIMITATIONS.md:2392 (no reconnect)); complete: Collaboration sends the project but not its samples, so large assets cannot reach a peer at all.
  - `R-performance-69` Resumable collaboration streams - missing (docs/KNOWN-LIMITATIONS.md:2392 ('There is no reconnect')); implement: A dropped collaboration peer must rejoin from scratch; the session cannot resume from the last applied change.

## Q050-187 Host LV2 plug-in UIs with scaling

**P2 · L10 devices, instruments, effects, plug-in hosting · feature · L**

Goal: Users can open LV2 plug-in interfaces and keep them scaled with the host UI.

Why: LV2 is hosted but its UI is disabled, and the missing UI/scale requests are medium demand across 2 DAWs.

Depends on: Scale open plug-in editors
Acceptance:
- Enable the existing Show GUI action for an LV2 fixture and verify open, close, resize, and ui:scaleFactor behavior.
- Run the GUI under both supported UI backends and expose editor status to the agent.
Items:
  - `R-effects-50` Linux-native plug-in ecosystem - partial (LV2, LADSPA, CLAP and VST3 hosted on Linux (docs/KNOWN-LIMITATIONS.md:149-157); docs/KNOWN-LIMITATIONS.md:173 and src/gui/Lv2ViewBase.cpp:146-155 (an LV2 plug-in's own UI is never opened)); complete: Linux-native formats are hosted, but LV2 plug-ins such as LSP open only as generic controls, never their own UI.
  - `R-platform-6` Mixbus Linux plugin compatibility - partial (plugin.list (LADSPA, LV2, VST3, CLAP); src/gui/Lv2ViewBase.cpp:153 ('Show GUI' behind if (false)); docs/KNOWN-LIMITATIONS.md:173-175); complete: Zene hosts VST2, VST3, LV2 and CLAP on Linux, but an LV2 plug-in's own UI never opens, only the generic controls.
  - `R-ui-34` Consistent HiDPI and plug-in window scaling - partial (window.appearance scale; docs/KNOWN-LIMITATIONS.md:168-176 (VST3/CLAP editor zoom at open; LV2 UI not opened; not applied to an open window)); complete: Interface and VST3/CLAP editors scale, but open windows do not rescale and LV2 UIs are not hosted.

## Q050-188 Editable mastering proposals

**P2 · L17 agent control surface and AI assistance · completion · L**

Goal: A user can choose an offline mastering candidate, apply it as editable master-insert processing, and revise or undo the proposal inside the project.

Why: The grouped references report medium demand across 3, 2, and 1 DAWs; mastering.run currently writes WAV candidates and AUTO-MASTERING.md says MasteringChain is offline-only.

Acceptance:
- Expose genre and delivery-target choices on mastering.run and retain those choices with the resulting candidate.
- Expose mastering.apply as a registered command; applying a chosen candidate creates an editable MasteringChain-derived master insert initialized from that candidate, with gain, dynamics, limiter, and trim controls; one undo removes the proposal and save/reopen preserves its settings.
- Verify the in-project result is presented as an adjustable proposal and uses the existing offline chain without introducing an in-app generative model.
- Verify processing on the audio callback is allocation-free and bounded; the offline MasteringChain renderer is not called from the realtime path.
Items:
  - `R-mixing-19` AI-assisted mixing and mastering - partial (mastering.run / mastering.list_candidates (offline candidates written as WAV files and measured); docs/AUTO-MASTERING.md:34 (MasteringChain is offline-only)); complete: Mastering candidates come out as rendered files, not as adjustable settings in the project.
  - `R-mixing-22` AI mastering as an insert plugin avoids leaving the DAW - partial (mastering.run (offline job writing candidate files); plugin.load for any hosted mastering plug-in; docs/AUTO-MASTERING.md:34,76); complete: Zene can host a mastering plug-in, but its own mastering engine only runs offline, outside the project.
  - `R-mixing-26` AI mastering offers editable guidance rather than a fixed final answer - partial (mastering.list_candidates (candidates graded against delivery targets); mastering.run (WAV outputs only)); complete: Candidates are graded against loudness targets but can't be opened and adjusted as settings.

## Q050-189 Build selected-stem submixes

**P2 · L05 audio editing, warping, stretching, fades · feature · M**

Goal: Users can combine chosen separated stems into a single mix, including instrumental, a cappella, and drums-plus-bass presets.

Why: Selected-stem mixes are medium demand across 2 DAWs, and current separation always emits four independent stems with no combined output.

Depends on: Add six-stem HTDemucs extraction
Acceptance:
- Select arbitrary stems and verify the resulting mix contains exactly their summed audio and omits unselected parts.
- Verify instrumental, a cappella, and drums-plus-bass presets on a six-stem fixture.
- Confirm mix choice and result are available through the clip menu, agent state, and save/load.
Items:
  - `R-ai-8` Selectable stem families - partial (stem.job_result writes fixed drums, bass, other, vocals (commands_snapshot.json)); complete: The separator emits a fixed four-stem set with no instrument choice and no 'mix minus' output.
  - `R-editing-69` Mobile stem separation - partial (stem.job_start (4 stems: drums, bass, other, vocals); src/gui/clips/SampleClipView.cpp:131 Split to stems; docs/KNOWN-LIMITATIONS.md:1414); complete: Offline 4-stem separation ships with a clip menu entry; guitar and piano stems and a submix export are missing (the mobile part is not Zene's platform).
  - `R-workflow-138` Choose a custom stem submix instead of exporting every part - partial (src/gui/StemSplitController.cpp:117-166 (one fixed 4-stem split); stem.job_start args are source and segment_frames only); complete: The split always yields four separate stems, with no preset to combine chosen parts into one submix such as an instrumental.

## Q050-190 Offer one-click setup for common controllers

**P2 · L15 platform: performance, stability, Linux audio, install, hardware controllers · feature · L**

Goal: Users can detect a known connected controller and apply a working launcher/mixer profile in one step.

Why: Controller templates exist but need broader common profiles and still require manual port setup; demand is medium.

Acceptance:
- A device-detection test recognizes supported controller identities and offers the matching profile.
- Applying a profile configures mixer and launcher controls, shows the result in the UI and can be undone.
- Each setup action has a registered command and headless control-socket test; supported profiles are documented.
Items:
  - `R-ui-11` Out-of-box setup instead of endless customization - partial (window.start_hub; app.tours; surface.mcu_connect and remote.connect need explicit ports); complete: Start Hub and tours exist, but controllers need manual port setup.
  - `R-workflow-42` Native control-surface and MIDI learn depth - partial (command ids controller.template_*, surface.mcu_*, remote.*; src/gui/ControllerTemplateMenu.cpp); complete: Learn, templates, MCU and remote pages exist, but there are no shipped device profiles for common controllers.

## Q050-191 Add per-device recording offset calibration

**P2 · L03 recording, takes, comping, inputs · feature · L**

Goal: Users can measure or set a per-device recording offset and have it applied to recorded takes.

Why: Medium demand across 2 DAWs requests loopback measurement and manual offsets, which the current recording controls do not provide.

Depends on: Apply backend latency to every recording route
Acceptance:
- A loopback fixture with a known delay calculates and stores the expected per-device offset.
- A manual offset can be edited in audio settings, survives restart, and is applied to clip and routed recordings.
- The control-socket surface reports the selected device and effective offset.
Items:
  - `R-recording-16` Unexpected PipeWire round-trip latency - partial (docs/KNOWN-LIMITATIONS.md:679-684 (round trip from reported latencies; only JACK reports device latency); mixer.hardware_insert_ping measures a send/return round trip); complete: Recording offset is computed from reported latencies, and the existing ping measurement is not offered as a recording calibration.
  - `R-recording-35` BandLab recording latency reports - partial (clip.set_record; docs/KNOWN-LIMITATIONS.md:682-684 (non-JACK backends report no device latency)); fix: Zene shares the off-beat take symptom on backends that report no device latency.
  - `R-recording-40` Overbridge can lose the start of short recorded loops to latency - partial (include/AudioDevice.h:90-91 (device latency defaults to 0; only AudioJack.cpp:685-698 and AudioPortAudio.cpp:182-188 override); docs/KNOWN-LIMITATIONS.md:682-684); fix: On ALSA, PulseAudio and SDL the device latency term is 0, so takes land late by the device buffering, and there is no manual offset.

## Q050-192 Search sounds by detected key and tempo

**P2 · L11 browser, samples, presets, library · completion · M**

Goal: Users can filter indexed sounds by detected key or BPM and find loops that fit the project tempo and key.

Why: The requests cite medium demand across two DAWs for tempo/key matching, while the index already measures BPM and key but its query parser cannot filter either.

Depends on: Expose tag and embedded-metadata search in the browser
Acceptance:
- browser.index_query and the Search view support documented key: and bpm: expressions using the index's detected values.
- A Fits this project filter uses the current project key and tempo, with explicit behavior for unknown detections and half/double-time matches.
- Previewing or loading a matched loop retains the selected tempo/key match mode; the metadata toggle from the prerequisite lane remains available.
Items:
  - `R-workflow-78` In-app sample search with tempo and key matching - partial (src/gui/BrowserSearchView.cpp:451-452 (matched preview); browser.index_query expressions are name:/path:/tag:/kind:/ext: only though browser.reindex measures tempo and key); complete: Loops preview matched to song tempo and key, but search cannot filter by key or tempo and loading does not keep the match (online Splice itself is deferred).
  - `R-workflow-166` Project tempo/key aware sample exploration - partial (src/gui/BrowserSearchView.cpp:450-452 (preview at the song's tempo or key); :476 (Find Similar Sounds); browser.query fields are name, path, tag, kind and ext only (no key or tempo)); complete: Previews can be matched to the project's tempo and key, but the browser can't filter the library to loops whose key or tempo suits the project.
  - `R-workflow-171` Search library assets by tags and metadata - partial (src/gui/BrowserSearchView.cpp:104 (tag: search), :476 (similarity); queryArgs at :268 never sets probe; no key: or bpm: field); complete: Tag and similarity search reach the GUI, but key search and embedded-metadata search don't.

## Q050-193 Drag track templates onto tracks and set type defaults

**P2 · L11 browser, samples, presets, library · completion · L**

Goal: Users can apply saved track templates by dragging them to a track and choose a default template for newly created tracks of a type.

Why: The preset-drag requests report medium demand and the default-template request low demand; current track presets are menu-only and track.add has no type default.

Depends on: Search plug-ins, devices, and chain presets from the browser
Acceptance:
- Track preset items can be dragged to a compatible target track and apply the saved device/track state.
- An explicit option controls whether fader, pan, and trim are carried with the template.
- Setting a default for a track type affects only newly added tracks of that type; existing tracks remain unchanged.
Items:
  - `R-workflow-143` Drag-and-drop track and chain templates - partial (src/gui/shell/ShellCommandMenus.cpp:282-283 (track preset save/load); src/gui/ChainPresetMenu.cpp:111 (chain.apply); no QDrag or drop handlers in src/gui/shell DevicePanel or DeviceCard); complete: Track and chain presets apply from menus, but they can't be dragged onto another track, and a chain preset leaves out fader, pan and trim.
  - `R-workflow-182` Track presets with drag-and-drop - partial (src/gui/shell/ShellCommandMenus.cpp:282-283 (track preset save and load from menus); src/gui/ChainPresetMenu.cpp:111; no drag handlers for presets in src/gui/shell); complete: Track presets carry the device and chain but apply from menus only, with no drag and drop. VST3 migration doesn't apply.
  - `R-workflow-267` Default track and device templates - partial (project.save_as_template (src/gui/MainWindow.cpp:323; default template); track.preset_save / load; track.add has no per-type default preset); complete: There's a default project template and track presets, but no per-type default that a newly added track starts from.

## Q050-194 Scale open plug-in editors

**P2 · L10 devices, instruments, effects, plug-in hosting · feature · M**

Goal: Users can resize and rescale open VST3/CLAP editors and hosted instruments without reopening them.

Why: Editor scaling applies at open time and misses hosted instruments; the medium-demand requests span 2 DAWs.

Acceptance:
- Change application scale with several editor types open and verify their size and layout update.
- Persist editor scale and verify window.appearance and plug-in command state agree.
Items:
  - `B-MX-094` Plug-in window scaling - partial (docs/KNOWN-LIMITATIONS.md:168-174 (CLAP/VST3 effect scaling; LV2 limitation)); complete: CLAP and VST3 effect windows scale, but LV2 windows and hosted instrument editors do not have equivalent scaling.
  - `R-accessibility-26` Global UI zoom and resizable plug-in windows - partial (window.appearance scale; plugin.options editor_scale; docs/KNOWN-LIMITATIONS.md:168-175 (VST3/CLAP effects only, instruments no option, LV2 no UI)); complete: Global zoom and effect editor scaling exist, but instrument editors cannot be scaled and LV2 UIs are not opened.
  - `R-accessibility-43` HiDPI plug-in and interface scaling - partial (window.appearance scale; plugin.options editor_scale; docs/KNOWN-LIMITATIONS.md:169-175 (applies at open only; instruments and LV2 not covered)); complete: Interface zoom works, but plug-in editor zoom applies only when a window opens and not to instruments or LV2.
  - `R-ui-69` Scalable plug-in editors - partial (docs/KNOWN-LIMITATIONS.md:168-176 (editor zoom applied only when the window opens; LV2 UIs not hosted)); complete: Plug-in editors scale at open time only.

## Q050-195 Complete MIDI transport and MTC sync

**P2 · L15 platform: performance, stability, Linux audio, install, hardware controllers · completion · L**

Goal: Users can sync MIDI clock transport, MTC position and JACK transport with external peers.

Why: The queue marks MTC absent and incoming MIDI transport incomplete at medium demand; the existing Link lane does not implement these MIDI/JACK paths.

Depends on: Honor the selected MIDI clock source
Acceptance:
- A control-socket test sends START, CONTINUE, STOP and SPP from the selected MIDI source and verifies transport state.
- MTC loopback tests cover configured frame rate and SMPTE offset; clock.get_state reports chase state and position.
- A JACK transport smoke check verifies the selectable source and documents hardware-only limits.
Items:
  - `B-WF-051` Sync tempo and transport with other apps and machines - partial (link.set_enabled (src/gui/SessionSyncAction.cpp:94); clock.master_set/clock.slave_set (src/gui/MidiClockMenu.cpp:123); docs/KNOWN-LIMITATIONS.md:932 (MTC not generated)); complete: Ableton Link and MIDI clock master/slave ship; MIDI time code is neither generated nor chased, so timecode sync with video/tape machines is missing.
  - `R-interop-19` Transport integration after ReWire removal - partial (link.set_start_stop_sync (Link start/stop acts on the transport); clock.slave_set ('moving the transport FROM the incoming clock is not in this release'); docs/KNOWN-LIMITATIONS.md:937; no jack_transport use outside src/3rdparty); complete: Link shares tempo, phase and start/stop, but there's no song-position transport sync (JACK transport, or following an incoming MIDI START/SPP).
  - `R-interop-35` External MIDI transport and MTC - partial (clock.slave_set / clock.get_state ('mtc: absent'); docs/KNOWN-LIMITATIONS.md:932-938 (MTC not generated; incoming START/STOP/CONTINUE doesn't move the transport)); complete: Zene follows MIDI clock tempo, but neither generates nor follows MTC, and doesn't follow incoming transport messages.

## Q050-196 Document PipeWire setup and backend boundaries

**P2 · L15 platform: performance, stability, Linux audio, install, hardware controllers · docs · M**

Goal: Linux users can choose PipeWire’s JACK or PulseAudio path and understand whether a native PipeWire backend is supported.

Why: PipeWire use is absent from user docs despite medium demand; the native backend is also unevaluated in the source tree.

Depends on: Qualify JACK audio through PipeWire
Acceptance:
- The Linux audio guide gives verified pw-jack/JACK and PulseAudio setup steps and records the results of PipeWire qualification.
- The guide states whether a native PipeWire backend was evaluated and what is supported today.
Items:
  - `R-platform-16` PipeWire’s JACK shim makes existing DAWs usable - partial (rg pipewire/pw-jack over docs/, doc/, README.md, INSTALL.txt found no user guidance; CMakeLists.txt:107 (weak-linked JACK)); docs: The weak-linked JACK backend can run on PipeWire's JACK library, but no user document explains how to launch or select it.
  - `R-workflow-52` PipeWire reduces JACK/PulseAudio setup friction - partial (src/core/audio has AudioJack.cpp, AudioPulseAudio.cpp, AudioAlsa.cpp; rg -i pipewire over src/ finds only a jack2 ChangeLog line; nothing in README or docs); complete: Zene runs on PipeWire only through its JACK or Pulse compatibility layers, with no native backend and no setup guidance.

## Q050-197 Instrument key names and kit remapping

**P2 · L07 note editor, MIDI editing, scales, chords, articulations · completion · L**

Goal: Name piano-roll rows per instrument and remap a clip between drum kits by matching those names.

Why: Medium demand across three DAWs for custom note names and low demand across one DAW for kit remapping; key names have a command but no editor or remap workflow.

Acceptance:
- A UI editor assigns and saves per-track key names and a headless command reads them back.
- A remap test moves notes between two differently keyed kits by matching names and reports unmatched notes without moving them.
- Undo and project reload restore the exact mappings and note keys.
Items:
  - `R-editing-38` Custom piano-roll note names - partial (src/core/ControlCommandsKeyNames.cpp:113-119; command id key_names.set; include/NoteEditorParts.h:67); complete: Per-track custom key labels are settable and displayed in piano-roll rows, but the GUI exposes only the General MIDI drum preset and none option.
  - `R-workflow-212` Drumkit-independent pattern reuse - partial (key_names.preset gm_drums; src/core/ControlCommandsDrumRack.cpp:58 (pads start at GM kick 36); no kit-remap of notes (rg 'drum ?map|kit swap' in src: none)); complete: GM key names and GM-ordered drum racks let a GM-laid-out pattern survive a GM kit swap, but there is no remap of a pattern's notes between two kits' layouts by instrument name.

## Q050-198 Learn list with progress, patcher, and plug-in tours

**P2 · L14 help, onboarding, accessibility · feature · M**

Goal: Choose a guided lesson, resume it, and see which lessons are complete and what to learn next.

Why: The requests describe a missing patcher and plug-in onboarding path, while current tours cover only first-beat, mixing, and recording.

Acceptance:
- Show the installed tours in a Learn list with completion state that survives restart and offers the next unfinished lesson.
- Add guided lessons for connecting devices in the patcher and for locating plug-in folders, rescanning, inserting an effect, and sending to a return.
- Verify each tour step resolves to a current control and can be completed by keyboard.
Items:
  - `R-workflow-217` Learn View tutorials - partial (app.tours/app.tour; data/tours/{first-beat,mixing,recording}.json; data/manual/index.html (11 sections); src/gui/HelpCenter.cpp:243-255 (no completion state)); complete: There is an in-app manual and three guided tours, but no structured lesson list with progress/completion tracking.
  - `R-accessibility-35` Onboarding through a dense modular interface - partial (data/tours/ has first-beat, mixing, recording only; patcher.get_state/patcher.set_wiring); complete: Guided tours exist, but none covers the patcher or device routing.
  - `R-workflow-261` VST routing onboarding - partial (plugin.paths_get/paths_set/rescan in GUI (src/gui/CommandLists.cpp:131-132); data/tours/mixing.json (Returns, Devices steps); data/manual/index.html mentions VST/CLAP/LV2 once); complete: Plug-in folders, rescans and routing exist and the mixing tour covers returns, but no tour or manual section walks a new user through adding VST3/CLAP folders and inserting/routing an effect.

## Q050-199 Qualify JACK audio through PipeWire

**P2 · L15 platform: performance, stability, Linux audio, install, hardware controllers · completion · M**

Goal: Users can rely on the JACK backend through PipeWire for capture, multichannel routing and low-latency work.

Why: Both rows identify PipeWire JACK as untested despite medium demand for multi-interface and low-latency use.

Acceptance:
- With pipewire-jack installed, the JACK capture and multichannel checks pass and their commands are recorded.
- A low-quantum run records xruns and round-trip latency at stated settings; the report names the interface, quantum and sample rate.
- Any unsupported measurement is marked unverified with the exact environment limitation.
Items:
  - `R-platform-29` PipeWire supports multiple interfaces and JACK clients - partial (CMakeLists.txt:106-107 (JACK, weak-linked); tests/control-jack-capture.py:6,50 (runs against jackd only); docs/KNOWN-LIMITATIONS.md:2307-2309 (JACK multichannel not run)); complete: PipeWire is reachable through the JACK backend, but neither PipeWire's JACK library nor multiple interfaces through it has been exercised.
  - `R-performance-51` PipeWire low-latency stability - partial (src/core/audio/ (AudioJack.cpp, AudioPulseAudio.cpp, AudioAlsa.cpp; no PipeWire backend); docs/KNOWN-LIMITATIONS.md:2307-2310 (JACK multichannel written but never run)); complete: Zene reaches PipeWire only through its JACK or PulseAudio backends, and no low-latency run under pipewire-jack is recorded.

## Q050-200 Package Flatpak support and explain plug-in limits

**P2 · L15 platform: performance, stability, Linux audio, install, hardware controllers · feature · L**

Goal: Linux users can install the Flatpak build, discover supported shared plug-ins and understand sandbox restrictions.

Why: The repository has AppImage/makeself packaging but no Flatpak target or user guidance; documentation demand is medium and implementation demand is low.

Acceptance:
- A Flatpak build installs and discovers a test plug-in through the supported Linux Audio extension point.
- Packaging checks cover permissions, plug-in paths and sandbox behavior.
- The install guide lists supported Linux formats and explains Flatpak plug-in limitations.
Items:
  - `R-hosting-10` Flatpak Linux Audio extension support - missing (cmake/linux/LinuxDeploy.cmake:1-4 packages AppImage or makeself; rg for Flatpak and org.freedesktop.LinuxAudio in packaging/source found no matches); implement: There is no Flatpak package or shared Linux Audio extension discovery path in the current tree.
  - `R-hosting-7` Flatpak plug-in installation guidance - missing (cmake/linux/LinuxDeploy.cmake:1-4 documents AppImage/makeself packaging; rg for Flatpak installation guidance returned no matches); docs: The tree lacks a supported Flatpak package and user guidance for sandbox plug-in paths and permissions.

## Q050-201 Split loop recordings into take lanes

**P2 · L03 recording, takes, comping, inputs · completion · M**

Goal: Loop recording creates a distinct lane for each pass without requiring a manual split command.

Why: Three queue items ask for loop takes across 3 DAWs, but loop-wrapped takes currently need manual comp.split_passes.

Depends on: Make audio record modes honor history
Acceptance:
- A three-pass loop-record test creates three separate takes and lane state survives save and reload.
- The chosen record mode controls whether earlier passes are muted, and the result is visible in comp.get_state and the take-lane UI.
Items:
  - `B-ED-167` Loop recording into takes - partial (comp.split_passes; src/gui/shell/ShellCommandMenus.cpp:311; src/core/ControlCommandsRecordingPass.cpp:25-29); complete: A completed looped take can be split into one take per pass, but recording itself still produces a continuous take that needs a manual split action.
  - `R-recording-8` Loop overdubbing while recording - partial (comp.split_passes (manual split of a loop-wrapped take into lanes); clip.set_loop; transport.record_options (MIDI overdub only); plans/TRACKER-050.md:153 (ED-167 3/8)); complete: Loop recording keeps one long audio take that must be split by hand, and audio cannot overdub onto a loop.
  - `R-recording-72` Take lanes and looped takes - partial (comp.split_passes (manual, from the sample clip menu per plans/TRACKER-050.md:153); include/TakeLanesPanel.h); complete: Take lanes and pass splitting exist, but loop recording does not create a lane per pass automatically.

## Q050-202 Migration and accessibility keymap setup

**P2 · L14 help, onboarding, accessibility · feature · M**

Goal: Import a familiar or accessible keymap and find setup guidance for NVDA, VoiceOver, and Orca.

Why: Both items have medium demand across three DAWs and point to shortcut migration and screen-reader setup as recurring onboarding work.

Acceptance:
- Ship a recommended accessibility keymap and migration keymaps for named source DAWs using the existing import/export path.
- Verify importing preserves a backup of the previous map and that each preset can be exported and re-imported.
- Add in-app setup guidance for supported screen readers and links to the tested workflow matrix.
Items:
  - `R-workflow-49` Reduce migration time away from music-making - partial (command ids app.tours (data/tours: first-beat, mixing, recording), app.ask, shortcuts.import; no shipped DAW keymaps or migration guide in data/); complete: Tours and Help > Ask exist, but there are no migration guides or familiar-DAW keymaps to make a switch predictable.
  - `R-workflow-272` Accessible installation and keymap packaging - partial (src/gui/CommandLists.cpp:198 (shortcut import and export); settings.preset_*; no accessibility keymap or screen-reader setup page shipped (data/ has none)); complete: Keymaps can be imported, but Zene ships no screen-reader keymap or settings preset and no setup guide for screen readers.

## Q050-203 Add anchored comment threads to shared projects

**P2 · L16 collaboration and sharing · feature · L**

Goal: Collaborators can add, reply to, move, resolve, and filter comments anchored to project time, with optional peer mentions.

Why: Demand is medium to low across 1 DAW per item, and ControlCommandsCollab.cpp:161-198 and CollabPanel.cpp:91-130 have no comment or annotation workflow.

Depends on: Label collaboration as experimental and document its limits
Acceptance:
- A comment round-trip test saves and reloads a bar-and-beat anchor, edited anchor position, replies, resolve state, and filter results.
- Host and peer see the same comment-thread changes through registered commands with stable schemas.
- The collaboration panel exposes the thread workflow and optional mentions without changing project audio or undoing musical edits.
Items:
  - `R-collaboration-10` Timeline comment threads - missing (src/core/ControlCommandsCollab.cpp:161-198; src/gui/CollabPanel.cpp:91-130; no comment command found); implement: The collaboration panel handles hosting, invites and peers but has no timeline annotation or threaded comment workflow.
  - `R-collaboration-30` Project comment anchoring and mentions - missing (src/core/ControlCommandsCollab.cpp:161-198; src/gui/CollabPanel.cpp:91-130; no comment command found); implement: There is no project comment or annotation system to anchor comments to bars, beats or time positions.

## Q050-204 Import MIDI and media onto chosen arranger tracks

**P2 · L04 arranger, timeline, clip operations, markers, tempo map · completion · M**

Goal: Users can drop a MIDI file onto an instrument track to create clips at the drop tick and drop media into a track gap to create a correctly placed track and clip.

Why: The 3-DAW medium-demand request can import MIDI only as new tracks today, while the related low-demand row shows arranger drops do not create position-aware clips in track gaps.

Depends on: Safely accept plug-in MIDI and media drops
Acceptance:
- A GUI regression test drops a MIDI file onto an existing instrument track and verifies MIDI clips are created there at the pointer tick.
- A GUI regression test drops audio into a gap between tracks and verifies a suitable track is inserted there with the clip at the drop tick.
- Existing project-wide MIDI import still creates the same tracks and clips as before.
Items:
  - `R-interop-18` MIDI file import to a chosen track - partial (project.import adds new tracks only; src/gui/editors/TrackContainerView.cpp:470 (importedproject drop runs ImportFilter::import into new tracks); TrackContentWidget.cpp:540 (track drops accept only pasted clips)); complete: A MIDI file can only be imported as new tracks, not dropped onto a chosen instrument track at a chosen position.
  - `R-workflow-210` Context-aware drag-and-drop import - partial (src/gui/editors/TrackContainerView.cpp:437-490 (sample drop on empty area makes an instrument track with the sample loaded; MIDI drop imports the whole file); src/gui/tracks/SampleTrackView.cpp:221 (drop on an audio track)); complete: Dropping audio onto an audio track works, but dropping between tracks does not create an audio track at that position with a clip at the drop time, and MIDI drops import a whole project rather than a clip on the target track.

## Q050-205 Add an opt-in OSC control server

**P2 · L15 platform: performance, stability, Linux audio, install, hardware controllers · feature · M**

Goal: Users and agents can control transport and faders over OSC and receive state feedback.

Why: Two queue rows describe the same missing OSC command bridge; demand is medium and no OSC command group exists.

Acceptance:
- An OSC loopback test maps /zene/<command-id> messages to registered commands and verifies transport/fader feedback.
- The server is off by default, binds only after explicit opt-in and documents its address map.
- Malformed and unsupported OSC messages return bounded errors without terminating the DAW.
Items:
  - `R-interop-42` OSC control surface - missing (docs/KNOWN-LIMITATIONS.md:1690 ('OSC is out'); no OSC commands in commands_snapshot.json); implement: There's no OSC interface; control is the JSON-RPC socket, MIDI and MCU only.
  - `R-workflow-85` OSC remote control for studio operation - missing (docs/KNOWN-LIMITATIONS.md:1690 ('OSC is out'); no osc command group); implement: There is no OSC remote control; the control socket is local JSON-RPC.

## Q050-206 Add per-clip effect controls and output routing

**P2 · L05 audio editing, warping, stretching, fades · completion · L**

Goal: Users can manage a clip's live effect chain, bypass or print it, and choose that clip's output from an Inspector.

Why: Per-clip FX and routing are medium demand across 2 DAWs; current evidence shows only destructive clip.process and no live clip chain or output choice.

Depends on: Per-clip effect-chain storage; Per-clip effect playback and latency compensation
Acceptance:
- Open the clip Inspector and verify effect order, bypass, print, and per-clip output selection are available and saved with the project.
- Print a clip effect chain and compare the printed audio to the live chain render; verify the source remains undoable.
- Exercise the same controls through clip effect commands and verify read-back and routing.
Items:
  - `R-recording-50` Per-region FX and input-ready record window - partial (src/gui/InputLevelMeter.cpp (live input meters); clip.process (destructive print through a chain); no per-clip realtime effect chain found); complete: Input meters and destructive clip processing exist; real-time per-clip effects do not.
  - `R-editing-102` Object-level edit precision and routing - partial (clip.set_fade/set_gain/gain_envelope_set/set_transpose per clip; clip.process prints a chain destructively (src/gui/shell/ShellCommandMenus.cpp:224); no per-clip effect chain or routing (rg EffectChain in SampleClip found none)); complete: Per-clip fades, gain, gain envelope and transpose exist, but a clip cannot carry its own live effect chain or routing.

## Q050-207 Cross-track ghosts and articulation lanes

**P2 · L07 note editor, MIDI editing, scales, chords, articulations · completion · M**

Goal: Choose clips from other tracks as ghosts and show color-by-channel plus articulation data across the visible clips.

Why: Medium demand on the ghosting request across one DAW and low demand on articulation lanes across one; current ghosts come only from the same track.

Depends on: Multi-track MIDI editor
Acceptance:
- The editor can select ghost source clips from other tracks and draws them at arrangement offsets.
- Color-by-channel and a multi-clip articulation lane match the notes shown in the canvas.
- A GUI test changes the ghost set and lane selection and verifies editor state readback.
Items:
  - `R-editing-92` Ghost regions and color-coded MIDI lanes - partial (src/gui/noteeditor/NoteEditorToolbar.cpp:264 ghosts = the track's other clips; color_mode velocity|pitch|track; controller lanes (ED-130)); complete: Ghost notes cover the same track only and notes cannot be coloured by channel.
  - `R-editing-120` Articulation and ghost-note editing - partial (track.articulations_set; ghosts only from the same track (src/gui/noteeditor/NoteEditorToolbar.cpp:264)); complete: Articulations per track exist, but ghost notes from other tracks and editing articulations across tracks do not.

## Q050-208 Plug-in picker aliases, tags, and recent use

**P2 · L10 devices, instruments, effects, plug-in hosting · feature · M**

Goal: Users can find devices by alias, tags, recent use, and whether a slot accepts an instrument or effect.

Why: The picker has search and folders but no aliases, smart tags, recent list, or slot-aware filter; demand is medium across 2 DAWs.

Acceptance:
- Add an alias and tag collection, then verify filtered and recent results in effect and instrument slots.
- Persist the organisation settings and expose them through plugin.organise commands.
Items:
  - `R-workflow-86` Plugin manager aliases, tags, and dynamic collections - partial (src/gui/modals/EffectPickerOrganiser.cpp (favourite, hidden, folder); no plug-in alias, recent list or smart collection command); complete: Favourites, hiding and folders exist, but plug-ins cannot be renamed, have no recent list and no tag-based dynamic collections.
  - `R-workflow-122` Plug-in and instrument discovery - partial (src/gui/shell/DevicePicker.cpp:2,167 (searchable picker by name, vendor, format and type); src/gui/PluginBrowser.cpp:257-287 (favourites, hidden, folders)); complete: The picker is searchable and can be organised, but it isn't context-aware: no recently used list and no suggestions for the slot or track type.

## Q050-209 Hosted instrument MIDI event delivery

**P2 · L10 devices, instruments, effects, plug-in hosting · feature · M**

Goal: Users can route all CCs, pitch bend, program changes, RPN/NRPN, and SysEx to CLAP/VST3 instruments by channel.

Why: CLAP drops all but CC74 and VST3/CLAP drop program changes or SysEx; demand is medium across 2 DAWs.

Acceptance:
- Use a host fixture that records each event type and channel and verify the exact delivered sequence.
- Round-trip MIDI parts and expose channel routing through the agent commands.
Items:
  - `R-interop-30` MIDI routing for multitimbral plug-ins - partial (plugin.midi_parts / midi_part_add (key/velocity zones to channels 1-16); plugins/ClapInstrument/ClapInstrument.cpp:287-296 (CLAP: only CC74 carried, pitch bend/program change/SysEx dropped)); complete: Multitimbral channel routing exists, but CLAP instruments drop most CCs and pitch bend, so arbitrary controllers don't reach them.
  - `R-interop-73` SysEx and RPN/NRPN delivery to instruments - partial (plugins/Vst3Instrument/Vst3Instrument.cpp:321-341 (CCs carried, SysEx and program change dropped); plugins/ClapInstrument/ClapInstrument.cpp:287-296 (only CC74 carried; SysEx dropped)); complete: CCs reach VST3 instruments, but SysEx is dropped everywhere and CLAP gets almost no CCs, so RPN/NRPN don't arrive.

## Q050-210 Transfer and hash-check project media between peers

**P2 · L16 collaboration and sharing · feature · L**

Goal: A joining collaborator receives missing project samples and other referenced media through the authenticated peer session and opens a correctly relinked project.

Why: Demand is low to medium across 1 DAW per item, and KNOWN-LIMITATIONS.md:2387 states that samples are not transferred while the current sync path carries registered edits.

Depends on: Label collaboration as experimental and document its limits
Acceptance:
- A host shares a fixture project with a missing WAV and soundfont; the peer receives only referenced assets over a separate peer asset channel alongside the ordered edit stream.
- The receiver verifies each asset hash before relinking, refuses a corrupted payload, and preserves the sender's source files.
- Transfer state and errors are observable through the collaboration UI and agent command surface.
Items:
  - `R-collaboration-12` Private peer-to-peer asset transfer - missing (docs/KNOWN-LIMITATIONS.md:2387; no media-transfer command found); implement: The collaboration session shares project commands and snapshots but does not transfer samples or other media.
  - `R-collaboration-36` OpenDAW shared project graph and asset channels - partial (docs/KNOWN-LIMITATIONS.md:2385-2387; docs/LINK-SYNC.md:1-19); complete: The host-ordered project stream works, but peer-to-peer sample and plug-in asset channels are absent.

## Q050-211 Step input and recording in the new editor

**P2 · L07 note editor, MIDI editing, scales, chords, articulations · completion · M**

Goal: Record MIDI notes and enter notes one step at a time from a MIDI keyboard or computer keys in the new note editor.

Why: Medium demand across one DAW; recording and step input are explicitly limited to the classic piano roll.

Acceptance:
- A MIDI-input test enters a note, advances by the selected grid step, and records a short phrase.
- Computer-key step entry and MIDI keyboard entry use the same grid and note-length settings.
- A UI and socket test verify note placement, selection, undo, and transport-safe stop.
Items:
  - `R-editing-75` More complete MIDI editor navigation and defaults - partial (window.detach (editor in its own window) and window.note_editor_set fit/zoom_to_selection; docs/NOTE-EDITOR.md:191 (no MIDI recording or step input in the new editor)); complete: Detaching, fit-on-open and zoom exist, but MIDI recording and step input still need the classic roll; the notation part is owner-deferred.
  - `R-editing-82` Audition, spray-can and step-input editing tools - partial (window.note_editor_set tool audition (Y) and paint (B); expression lanes (docs/NOTE-EDITOR.md:113-135); docs/NOTE-EDITOR.md:191 step input only in the classic roll); complete: Audition, paint (spray-like run) and expression lanes exist in the new editor; step input still needs the classic piano roll.

## Q050-212 Track creation output routing and auto-channel assignment

**P2 · L09 routing, buses, sends, sidechain, I/O · feature · L**

Goal: Choose a destination bus when adding tracks or enable a preference that creates and names a mixer channel for each new track.

Why: track.add currently takes only name and type and no auto-assignment preference exists; both workflow reports have medium demand and cite 1 DAW each.

Acceptance:
- track.add accepts an output bus and the add-track UI offers a destination choice plus an add-N-tracks-to-new-bus action.
- With auto-assignment enabled, each new instrument or sample track gets a named mixer channel and rename keeps the names synchronized; disabled mode preserves current routing.
- The command test verifies save/load, undo and redo for both explicit routing and automatic assignment.
Items:
  - `R-workflow-232` Choose destination bus when creating tracks - missing (track.add args are name and type only (commands_snapshot); src/gui/editors/SongEditor.cpp:925-944 (add-track actions with no destination)); implement: A new track can't be given its output bus when it's created.
  - `R-workflow-274` Automatic playlist-track to mixer-track linking - partial (track.set_mixer_channel; src/gui/MixerView.cpp:190-197 (new channel by hand); mixer.order_by_tracks; rg found no auto-assign setting); complete: Tracks can be routed to channels and channels ordered by tracks, but a new track doesn't get its own named channel automatically.

## Q050-213 Multisample velocity and round-robin mapping

**P2 · L10 devices, instruments, effects, plug-in hosting · feature · L**

Goal: Users can build multisample instruments with key/velocity layers, loop regions, and round-robin groups.

Why: The sampler lacks multisample mapping and round-robin selection; both requests are medium demand from 1 DAW.

Depends on: Mapped sampler-zone editor
Acceptance:
- Map a fixture set across key and velocity ranges and verify round-robin alternation in a render.
- Round-trip zones and loop regions through save/load and agent state.
Items:
  - `R-instruments-38` XSampler instrument - partial (plugins/AudioFileProcessor/AudioFileProcessor.h:69-78 exposes one sample instrument; rack.layer_zone_add and clip.slice_to_instrument provide limited mapping and slicing.); complete: Single-sample playback and basic zones exist, but a full multisample map with loop, range and round-robin editing is missing.
  - `R-instruments-45` A richer native sampler and sample-to-instrument workflow - partial (plugins/AudioFileProcessor/AudioFileProcessor.h:69-78; rack.layer_zone_add; clip.slice_to_instrument; no round-robin sample-map editor found.); complete: Sample playback, basic key zones and slicing exist, but authored multisample maps and round-robin playback do not.

## Q050-214 Plug-in editor gesture undo

**P2 · L10 devices, instruments, effects, plug-in hosting · feature · M**

Goal: Users can undo a complete VST3 or CLAP editor gesture as one project-history step.

Why: VST3 beginEdit/endEdit are no-ops and host gesture history is missing; demand is medium across 2 DAWs.

Acceptance:
- Change a plug-in value with begin/end gesture events and verify one undo restores it and redo reapplies it.
- Test GUI and agent history against the same journal checkpoint.
Items:
  - `R-workflow-64` Global undo across mixer and plugin controls - partial (src/core/AutomatableModel.cpp:340 (host knobs and faders add a journal checkpoint); plugins/Vst3Effect/Vst3ComponentHandler.h:69-84 (beginEdit/endEdit are no-ops)); complete: Mixer and host-side parameter changes are undoable, but edits made in a hosted plug-in's own editor are not grouped into undo steps.
  - `R-workflow-303` Reason Rack has its own undo history in plugin hosts - partial (control.undo / Edit > Undo History cover model edits (docs/KNOWN-LIMITATIONS.md:507-511); plugins/Vst3Effect/Vst3ComponentHandler.h:69 beginEdit is a no-op); complete: Host undo covers parameter edits made through Zene, but gestures made inside a hosted plug-in's own editor are not grouped into undo steps.

## Q050-215 AAF import fidelity and missing-media preflight

**P2 · L12 projects, files, import/export, interchange, recovery · completion · L**

Goal: Show missing AAF media before import and preserve supported multichannel, automation, gain, pan, and tempo-map content.

Why: The rows report medium demand from one DAW and enumerate specific AAF import fidelity and missing-media workflow gaps.

Acceptance:
- In File > Import AAF, show the missing-media list before import and allow the user to continue or choose a media folder.
- Import a fixture with multiple essences, selected channels, gain/pan automation, and a non-default tempo map; verify the supported content lands on the expected tracks and ticks.
- Drive the same preflight and import-result report through interchange.aaf_read and the File > Import AAF menu.
Items:
  - `R-interop-34` AAF missing-media warnings - partial (interchange.aaf_read reports MISSING media; src/gui/AafImportMenu.cpp:55-59 (File > Import AAF runs aaf_import straight away and shows missing media only afterwards)); complete: Missing media is reported after the import, not before it.
  - `R-interop-36` Partial AAF import - partial (interchange.aaf_import; docs/KNOWN-LIMITATIONS.md:2364-2370; include/AafImport.h:34-41 (no track gain/pan automation, solo, multi-essence channels or channel selection; tempo map not followed)); complete: AAF import covers a subset: clips play their first source only, automation and solo are dropped, and the global tempo is used.

## Q050-216 Queue a section-by-track render matrix

**P2 · L12 projects, files, import/export, interchange, recovery · feature · M**

Goal: Select sections or markers and tracks and queue one named render job for each selected combination.

Why: The duplicate rows report medium demand from one DAW and the render queue has no sections-by-tracks matrix.

Depends on: Preset-based range and section export in the GUI
Acceptance:
- Choose two sections and two tracks and verify four queue jobs with deterministic names and matching range/track selections.
- Run the queue and verify each output matches the corresponding section and track.
Items:
  - `R-workflow-131` Batch render and saved render queue - partial (render.queue_add (range, other project); src/gui/CommandLists.cpp:339-346 (Render Queue list)); complete: A saved, ranged render queue exists, but there's no region-by-track render matrix that batches marker regions against tracks.
  - `R-workflow-285` Region Render Matrix and subprojects - partial (project.subproject_add (src/gui/LibraryCommandMenus.cpp:233); render.queue_add with ranges; no region-by-track matrix); complete: Subprojects and ranged queued renders exist, but there's no region render matrix.

## Q050-217 MIDI command bindings and workflow header buttons

**P2 · L13 customisation, shortcuts, themes, layout · feature · L**

Goal: Launch argument-free commands, scripts, and saved workflows from MIDI controls or header buttons.

Why: Two medium-demand findings each cite one DAW, and current MIDI bindings reach launcher actions while workflows are menu-only and OSC is unavailable.

Acceptance:
- Bind a MIDI note and a MIDI CC to a built-in command, saved workflow, and script; replay each binding in a MIDI test harness and verify the selected action fires.
- Add a saved workflow to the header and verify the button runs it after restarting.
Items:
  - `R-platform-38` REAPER scripts can bind to MIDI, OSC, and UI actions - partial (src/gui/MacroMenu.cpp:2,152-159 (workflows in a menu, ShortcutMap applied); script.run; docs/KNOWN-LIMITATIONS.md:1690 ('OSC is out'); controller.map binds parameters only); complete: Workflows can carry keyboard shortcuts, but no workflow, script or command can be triggered from a MIDI note/CC or OSC message.
  - `R-workflow-132` Bind actions to keys, toolbars, MIDI, and OSC - partial (src/gui/CommandLists.cpp:194-198 (shortcuts); window.header_set (header buttons, commands without args); session.bind (MIDI notes reach launcher actions only); include/ControlRegistryGroups.h:696 (OSC is out)); complete: Keys and header buttons can run commands, but a MIDI note or CC can't trigger an arbitrary command or workflow. OSC is out by decision.

## Q050-218 Soak multitrack recording on real audio hardware

**P2 · L15 platform: performance, stability, Linux audio, install, hardware controllers · completion · M**

Goal: Users can record and replay multiple tracks on JACK/ALSA hardware without drift or unexplained gaps.

Why: The queue says multitrack hardware soak and drift are unverified while demand is medium or low.

Depends on: Add PortAudio multichannel and verify Linux device paths
Acceptance:
- A documented long-duration JACK/ALSA multitrack record/playback run reports interface, channels, sample rate, drift and any dropouts.
- The test compares recorded alignment with the hardware loopback and names limits not covered by Dummy-device tests.
Items:
  - `R-recording-58` Linux users value reliable multitrack recording - partial (docs/KNOWN-LIMITATIONS.md:2307-2308 (JACK/ALSA multichannel written but never run on hardware); record.arm_track); complete: Multitrack capture exists but has not been proven on real multichannel hardware.
  - `R-recording-39` Keep multitrack recording synchronized and dependable - unknown (docs/KNOWN-LIMITATIONS.md:679-684 (round-trip compensation proven on the Dummy loopback only); docs/KNOWN-LIMITATIONS.md:2307-2308); complete: Capture alignment is tested in software but no long-session drift test on hardware exists.

## Q050-219 Move arrangement sections across tracks

**P2 · L04 arranger, timeline, clip operations, markers, tempo map · completion · M**

Goal: Users can drag a named section in the ruler to move its material across tracks with ripple or gap behavior as one undoable edit.

Why: Section commands currently select a range but the ruler has no drag-to-move behavior; the two related requests cite medium and low demand across 1 DAW each.

Acceptance:
- A GUI test drags a section to a new tick and verifies clips on every track move together with the selected ripple or gap behavior.
- One undo restores clip positions and the section move; the ruler exposes the drag affordance.
Items:
  - `R-editing-89` Arrangement Strip for whole-song section moves - partial (section.* commands; src/gui/ArrangerRulerLanes.cpp:338 (a click selects the section as a range); range.copy/paste/delete/duplicate); complete: Sections are named and coloured and select their material as a range, but dragging a section block does not reorder the song.
  - `R-editing-144` Arranger Track sections - partial (section.* commands; src/gui/ArrangerRulerLanes.cpp:338 (a click selects the section as a range); no drag-to-move of a section); complete: Named, coloured sections exist and select their material, but a section block cannot be dragged to rearrange the song.

## Q050-220 Add interactive shape drawing and curve preview

**P2 · L06 automation and modulation · feature · M**

Goal: Sketch a curve in the automation editor, preview it, and bake it with optional stepped values.

Why: Demand is medium (1 DAW for R-editing-53) and low (1 for R-workflow-215); automation.draw_shape exists, but the editor lacks the requested drag readout, preview overlay, and value quantization.

Acceptance:
- Dragging the shape tool draws the selected shape over the span and shows a live value readout.
- The Draw Shape dialog previews changes without mutating the clip; value quantization produces exact steps when the user commits.
- A fixed random seed reproduces the same points, and one undo restores the clip before the edit.
Items:
  - `R-editing-53` Automation curve drawing tools - partial (automation.draw_shape/set_curve/transform/thin from the automation clip menu (src/gui/shell/ShellCommandMenus.cpp:126-129); include/AutomationEditor.h:103-108 edit modes Draw/Erase/DrawOutValues/EditTangents); complete: Freehand drawing, per-point curves and shape fills exist, but shapes are a typed form on the clip menu rather than a drag tool in the editor.
  - `R-workflow-215` Generate editable automation procedurally - partial (automation.draw_shape (sine/triangle/square/saw/random/adsr, cycles, min/max, step density, seed); src/gui/shell/ShellCommandMenus.cpp:126 'Draw Shape...'); complete: Seeded LFO/random curves with density are baked into automation as one undo step, but there is no live preview before baking and no value quantisation.

## Q050-221 Seeded melodic pattern tools

**P2 · L07 note editor, MIDI editing, scales, chords, articulations · feature · L**

Goal: Generate scale-aware melodic contours and Euclidean patterns reproducibly, then map variation controls to hardware.

Why: Medium demand across one DAW for melodic generation and low demand across one for Euclidean and hardware variation tools.

Acceptance:
- A fixed seed produces identical notes and a different seed changes the result while every pitch obeys the active scale.
- Euclidean pattern settings and the variation page are editable, saved, and undoable.
- A MIDI-controller test maps and reads back each variation control without adding a generative model dependency.
Items:
  - `R-editing-77` MIDI Tools and generative patterns - partial (note.generate euclid/random (src/gui/noteeditor/NoteEditorMenus.cpp:98), note.arpeggiate, note.ratchet, chord.progression_generate, Run Script on Notes (NoteEditorMenus.cpp:329)); complete: Rhythm, arpeggio, ratchet, progression and script transforms exist; there is no melodic generator (shape/seed-style pitch lines).
  - `R-instruments-65` Probability and generative note variation - partial (note.probability_set; note.randomize supports velocity and position jitter; no Euclidean generator or hardware variation page found.); complete: Per-note probability and seeded jitter exist, but Euclidean patterns and a performance variation surface are missing.

## Q050-222 Device-card drag and drop

**P2 · L10 devices, instruments, effects, plug-in hosting · feature · M**

Goal: Users can move or copy devices between chains and mixer channels by dragging their cards.

Why: Device actions already support move/copy but the cards have no drag handlers; demand is medium across 1 DAW.

Acceptance:
- Show an insert point, move by default, and copy with Ctrl within and between chains/channels.
- Verify plugin.move, plugin.copy, and chain.copy state and undo behavior through GUI and agent tests.
Items:
  - `R-ui-83` Improve nested Composite drag-and-drop - partial (rack.wrap / rack.add_chain (commands); no QDrag/dropEvent in src/gui/shell/DeviceCard.cpp, DevicePanel.cpp, EffectRackView.cpp); complete: Devices move between rack chains only by commands; there is no drag-and-drop between chains or mid-chain insert.
  - `R-workflow-145` Fast FX-chain copying and reordering - partial (src/gui/EffectDeviceMenu.cpp:254-258 (plugin.copy and chain.copy to channels); src/gui/shell/DeviceActionMenu.cpp:90 (plugin.move); no drag handlers in DevicePanel or DeviceCard); complete: Devices and whole chains copy between channels from menus, but there's no drag and drop with copy or move modifiers.

## Q050-223 Add FFmpeg-backed audio decoding

**P2 · L11 browser, samples, presets, library · feature · M**

Goal: Users can import common compressed audio formats through the existing sample-loading paths when FFmpeg is available.

Why: R-interop-37 and R-interop-56 request the same FFmpeg fallback, with medium/low demand from one DAW each; SampleDecoder currently has no FFmpeg decoder.

Acceptance:
- Decoder tests cover MP3, AAC/M4A, and a malformed or unusual MP3 fixture through SampleDecoder's fallback path.
- Builds without FFmpeg retain the current decoder behavior and report unsupported files cleanly.
- Existing WAV, Ogg, and DrumSynth decoding behavior remains unchanged.
Items:
  - `R-interop-37` Broad audio import and robust MP3 decoding - partial (src/core/SampleDecoder.cpp:47-58 (libsndfile, Ogg Vorbis and DrumSynth decoders only); SessionGridDrop.cpp:72-74 (mp3/opus expected through libsndfile); FFmpeg linked only for video (VideoDecoder.cpp)); complete: Audio import depends on libsndfile's MP3 support and has no fallback for files it can't read.
  - `R-interop-56` FFmpeg audio import - partial (src/core/SampleDecoder.cpp:47-58 (libsndfile, Ogg Vorbis and DrumSynth decoders only); FFmpeg used only by src/core/VideoDecoder.cpp; docs/KNOWN-LIMITATIONS.md:2377); complete: FFmpeg is linked for video, but audio import doesn't use it.

## Q050-224 Add crash context and per-thread signal stacks

**P2 · L15 platform: performance, stability, Linux audio, install, hardware controllers · completion · L**

Goal: Crash reports from engine and worker threads retain a usable stack and identify the faulting module.

Why: The reporter currently captures limited context and installs its alternate stack only on the calling thread; demand is medium for report context and low for thread stacks.

Acceptance:
- A non-main-thread overflow test confirms an alternate signal stack is installed when each relevant thread starts.
- The signal handler writes only bounded async-signal-safe raw context; a post-processing step resolves stack addresses and module names outside signal context.
- Crash-report tests verify thread id, fault address, frames and module attribution without changing normal shutdown behavior.
Items:
  - `R-performance-57` Crash-report context - partial (src/core/CrashReporter.cpp:85,131 (project path, thread id); include/CrashReporterFormat.h:68-73 (signal name, program counter); no backtrace or module name); complete: Crash reports record the signal, program counter, thread and project, but no backtrace and not the plug-in or module that faulted.
  - `R-platform-52` The alternate signal stack is installed only on the calling thread - partial (src/core/CrashReporter.cpp:92-96,249-252 (one sigaltstack installed on the calling thread); docs/CRASH-REPORTER.md:445-449 (overflow on another thread hangs, untested)); fix: A stack overflow on the audio or worker threads gets no alternate stack, so the reporter cannot write a report and the process may hang.

## Q050-225 Attribute peer edits for targeted rollback

**P2 · L16 collaboration and sharing · feature · L**

Goal: The host can identify a collaborator's changes, remove that peer, and roll back a selected peer edit without undoing unrelated collaborators' work.

Why: Demand is medium and low across 1 DAW per item, and the current collaboration model uses shared undo without per-peer attribution or targeted rollback (KNOWN-LIMITATIONS.md:2388-2389).

Depends on: Complete direct-GUI edit replication coverage
Acceptance:
- Streamed edits record stable author and change identifiers in a reviewable collaboration history.
- A test applies edits from two peers, rolls back one selected edit, and verifies the other peer's later independent change remains intact.
- Removing a peer blocks future edits, while rollback refuses with a typed reason when later changes make a safe inverse impossible.
Items:
  - `R-collaboration-19` Collaborator rollback and moderation - partial (src/core/ControlCommandsCollab.cpp:190-194; docs/KNOWN-LIMITATIONS.md:2388-2389); complete: Hosts can change peer roles and share host-wide undo, but cannot remove a single peer or roll back only that contributor's edits.
  - `R-collaboration-20` Recovering from destructive collaborator edits - partial (docs/KNOWN-LIMITATIONS.md:2388-2389; command ids collab.set_role and control.undo); complete: Shared undo is host-wide and edits are not attributed for targeted recovery after one collaborator makes a destructive change.

## Q050-226 Real-time pitch-correction effect

**P2 · L10 devices, instruments, effects, plug-in hosting · feature · L**

Goal: Users can correct vocal pitch against the project scale with adjustable retune speed and formant preservation.

Why: The plug-in catalogue has pitch shifting but no pitch-correction effect; demand is medium across 3 DAWs.

Acceptance:
- Render a detuned vocal fixture against a known scale and verify the corrected fundamental and formant envelope.
- Expose scale, speed, and latency state through UI and agent commands.
Items:
  - `R-effects-17` Built-in real-time pitch correction - missing (plugins/ list has GranularPitchShifter but no pitch-correction effect; bundled LADSPA sets (calf, caps, cmt, swh, tap) carry none (rg autotune|pitch correct)); implement: There is no stock real-time pitch-correction effect (scale-snapping tuner).

## Q050-227 Persistent mixer docks and pinned strips

**P2 · L08 mixer, metering, channel strip · completion · L**

Goal: Users can keep chosen mixer strips visible, resize mixer zones, and restore the layout after reopening the workspace.

Why: The dock/separator request spans 3 DAWs, and a medium-demand pinned-strip request targets the same persistent mixer-layout workflow.

Acceptance:
- Mixer zones dock and separators resize in the UI, with layout state saved and restored.
- A strip menu and window.mixer_view state expose pinned_channels; pinned strips remain visible while the main channel area scrolls.
- A UI/agent-surface test covers pin, unpin, resize, save, and restore.
Items:
  - `B-MX-007` Pinned zones, docks and separators - partial (include/ShellLayout.h:118; include/ShellPanel.h:70; src/gui/shell/ShellPanel.cpp:78-86); complete: A panel can stay pinned across views, but general dock zones and draggable separators are not implemented.
  - `R-mixing-25` Pin mixer buses and tracks while scrolling - missing (src/gui/MixerView.cpp:153-176 (one scrolling channel area, no pinned strip); window.mixer_view has no pin option; arranger pinning only (docs/KNOWN-LIMITATIONS.md:300-306, TrackPinStrip.h)); implement: Mixer strips can't be pinned while the channel area scrolls.

## Q050-228 Portable one-file project handoff bundle

**P2 · L12 projects, files, import/export, interchange, recovery · feature · M**

Goal: Package a handed-off project and its device state into one .zenebundle file that opens as a working project.

Why: The row reports medium demand across three DAWs and an existing folder handoff, with one-file packaging as the remaining gap.

Depends on: Project media portability across Save As, relink, and handoff
Acceptance:
- Bundle a project with external audio, video, and device-state files, move the bundle, and open it without the original folder.
- Verify unpacked project state and media hashes match the source handoff.
- Drive bundle creation through project.handoff and verify the UI exposes the same option.
Items:
  - `R-interop-14` Portable project bundles - partial (project.collect_media; project.handoff (folder + media/ + HANDOFF.json, src/core/ControlCommandsHandoff.cpp:102); ShellCommandMenus.cpp:498,502); complete: Projects can be gathered into a portable folder with their media, but there's no single-file bundle and plug-in presets and assets aren't included.

## Q050-229 Link sample and automation clips across tracks

**P2 · L04 arranger, timeline, clip operations, markers, tempo map · completion · M**

Goal: Users can link sample and automation clips across tracks, edit one linked member to update the group, and make a member unique.

Why: The evidence limits linked groups to MIDI and says audio cannot join them; the two related rows cite 3 and 2 DAWs, with medium demand on the cross-track request.

Acceptance:
- A regression test links sample and automation clips, including clips on different tracks, and verifies edits propagate and persist after save/load.
- The clip menu offers link creation, link removal, and Make Unique for supported linked clip types, with a visible link badge on group members.
Items:
  - `B-ED-033` Linked clips and make unique - partial (clip.link_create; clip.link_remove; include/ClipLinks.h:65-83; src/gui/shell/ShellCommandMenus.cpp:339-343); complete: Linked content and Make Unique are available for MIDI note clips, while sample clips do not share linked audio content.
  - `R-editing-66` Cross-track aliases and linked clips - partial (src/core/ControlCommandsClipLink.cpp:105-142 (no same-track check; MIDI clips only); src/gui/shell/ShellCommandMenus.cpp:339-343 Linked Clips menu; docs/KNOWN-LIMITATIONS.md:1494 (audio cannot be a member)); complete: MIDI clips already link across tracks from the clip menu, but audio and automation clips cannot be linked and nothing marks a linked clip in the arranger.

## Q050-230 Mini-notation parser and scheduler

**P2 · L07 note editor, MIDI editing, scales, chords, articulations · feature · L**

Goal: Compile bounded user-authored mini-notation into deterministic MIDI events and schedule or cancel those events safely.

Why: Medium demand across two DAWs; the editor has no phrase language or mini-notation scheduler today.

Acceptance:
- Parser tests cover notes, rests, durations, malformed input, and strict event and text bounds.
- A scheduler test produces identical events for the same script and seed and cancels them without stale notes.
- The realtime path is bounded and an agent command can inspect, schedule, and unschedule a phrase.
Items:
  - `R-editing-26` Pattern phrases and phrase scripting - missing (src/gui/editors/PatternEditor.cpp:49; commands_snapshot.json search for phrase-script or mini-notation commands found none); implement: Zene has no phrase scripting editor or runtime for scripted pattern generation.

## Q050-231 Cross-track masking report and undoable EQ proposals

**P2 · L08 mixer, metering, channel strip · feature · L**

Goal: Users can compare spectra across tracks, inspect masking bands, and choose undoable EQ-cut proposals using front/middle/back priorities.

Why: This medium-demand gap appears in 2 DAWs; current EQ comparison covers two strip EQs but has no spectral masking report or proposals.

Acceptance:
- mixer.masking_report returns deterministic clashing-band results for a fixed multi-track test signal.
- The report identifies each channel pair and proposed cut range without applying changes automatically.
- Applying and undoing a proposal changes and restores the target strip EQ through one reversible command transaction.
Items:
  - `R-mixing-7` AI EQ can resolve masking across multiple tracks - missing (mixer.eq_compare (compares two channels' strip EQ only); plugins/DynamicEq; no masking analysis in src/ or the command snapshot); implement: Zene can't detect spectral masking between tracks.

## Q050-232 Stream peer audio over Link

**P2 · L09 routing, buses, sends, sidechain, I/O · feature · L**

Goal: Send and receive real-time audio between Link peers so a peer can be monitored or recorded as an input.

Why: Link sync currently covers tempo, phase and start/stop while LinkAudio is explicitly not vendored (LINK-SYNC.md:82,308); demand is medium from 2 DAWs.

Acceptance:
- Two local Link peers exchange audio while tempo and phase stay synchronized, and the receiving peer can monitor and record the stream.
- A test checks channel count, sample-rate conversion, buffer bounds, reconnect behavior, and no allocation or locking on the audio callback.
Items:
  - `R-recording-9` Link Audio streams peer audio - missing (link.*; docs/KNOWN-LIMITATIONS.md:763-778 (tempo, phase and start/stop only); rg 'link audio' found only include/LinkAudioFollower.h (beat follower, not audio)); implement: Zene joins Link sessions for tempo and transport but cannot send or receive peer audio.

## Q050-233 Multi-mono plug-in wrapper

**P2 · L10 devices, instruments, effects, plug-in hosting · feature · L**

Goal: Users can run one linked plug-in instance per channel and unlink parameters where needed.

Why: Surround plug-ins currently act on L/R only and there is no multi-mono wrapper; demand is medium across 2 DAWs.

Acceptance:
- Process a multichannel fixture and verify instance count, linked/unlinked parameter behavior, and one latency report.
- Save/load the wrapper and test it through a mixer channel command.
Items:
  - `R-mixing-12` General-purpose multi-mono plug-in container - missing (docs/KNOWN-LIMITATIONS.md:2404 (surround devices act on L/R only); src/core/MixerSurroundMix.cpp:25-32; no multi-mono wrapper in src/ or plugins/); implement: There's no container that runs one mono instance of a plug-in per speaker with linked parameters.

## Q050-234 Compare and transfer project alternatives beside the session

**P2 · L12 projects, files, import/export, interchange, recovery · feature · M**

Goal: Open an alternative or subproject beside the active session, compare both views, and drag supported material between them.

Why: The input records medium demand from two DAWs and existing alternative/subproject commands, with side-by-side comparison and transfer still missing.

Depends on: Reference project copy and audition with EditLocks
Acceptance:
- Open an alternative in a side-by-side reference panel and verify its identity remains visible while the active session changes.
- Drag a supported clip between the two views and verify the destination project saves and reopens with it.
- Verify project.alternative_switch and the panel use the same active alternative and copy operation.
Items:
  - `R-workflow-16` Chunks for alternate songs and sequences - partial (command ids project.alternative_save/alternative_switch/alternatives, project.subproject_add; src/gui/LibraryCommandMenus.cpp:231-233); complete: Alternatives and subprojects give alternate versions in one project, but only one is open at a time with no split view or drag between them.

## Q050-235 Touch-first workspace layout

**P2 · L14 help, onboarding, accessibility · feature · L**

Goal: Choose a touch workspace with larger launcher, arranger, and mixer targets.

Why: The touch workspace is a medium-demand gap across three DAWs; current support is touch density and pinch zoom, without a dedicated layout.

Acceptance:
- Select the touch workspace from window.appearance and verify launcher cells, arranger controls, and mixer faders have larger usable targets.
- Exercise the layout with simulated touch input at a tablet-sized viewport and verify core actions remain reachable.
- Restart the app and confirm the selected layout follows the documented appearance-setting persistence.
Items:
  - `B-WF-133` Touch and tablet layout - partial (window.appearance density touch / accessible mode; docs/KNOWN-LIMITATIONS.md:2422 (pinch zoom); plans/TRACKER-050.md:615 (no tablet layout)); complete: Touch density and pinch zoom exist, but there is no tablet layout with large direct targets for launcher, arranger and mixer.
  - `R-accessibility-29` Touch and tablet workspace - partial (window.appearance density touch; docs/KNOWN-LIMITATIONS.md:2422 (pinch zoom), :2464 (target size reset)); complete: Touch density and pinch zoom exist, but there is no dedicated touch workspace.

## Q050-236 Navigable lyric transcript lane

**P2 · L17 agent control surface and AI assistance · feature · L**

Goal: A user can review and edit transcribed words on the project timeline and click a word to navigate to its audio position.

Why: R-recording-11 reports medium demand in 2 DAWs; existing lyrics are manual syllables on notes, with no timeline-aligned transcript view or navigation.

Depends on: Offline speech-to-text service
Acceptance:
- Display the transcription service's word timings in a lyrics lane aligned with the selected vocal clip; clicking a word seeks to its start time.
- Verify word edits and timing adjustments use registered commands, survive save/reopen, support undo, and remain navigable by keyboard; drive the commands through the socket in an integration test.
Items:
  - `R-recording-11` Timeline-aligned lyric transcription - missing (note.lyrics (manual syllables only); commands_snapshot.json has no transcription command; plans/TRACKER-050.md:325 (ED-088 Lyrics 2/8)); implement: Zene can attach typed lyrics to notes but cannot transcribe a vocal take into timeline-aligned words.

## Q050-237 Add overdub and live-loop advancement

**P2 · L02 launcher / session clips / performance · feature · L**

Goal: Overdub audio or MIDI in launcher cells, control record/overdub/play from a MIDI or pedal binding, and advance to the next empty cell.

Why: Demand is medium across 3 DAWs; current cell recording is one take at a time, refuses filled cells and has no overdub or cell record control.

Depends on: Record MIDI directly into launcher cells
Acceptance:
- Audio and MIDI overdub tests start from populated cells and verify existing material is retained while the new take is added.
- A MIDI note, CC or pedal binding can trigger record, overdub and play; session.bindings reports the mapping and the event changes the expected cell state.
- After a take completes, automatic advance selects or arms the next empty cell, and undo restores the prior cell content.
Items:
  - `R-recording-12` Live looping and overdub workflow - partial (session.record_slot; docs/KNOWN-LIMITATIONS.md:2566 ('no record over or overdub'), :2578 (no record button, shortcut or count-in on a cell)); complete: The launcher records into empty cells only, with no overdub, footswitch control or advance to the next free scene.

## Q050-238 Print external MIDI instruments before mixdown

**P2 · L03 recording, takes, comping, inputs · feature · L**

Goal: Users can capture hardware MIDI instrument outputs into aligned audio takes and include them in offline renders.

Why: Medium demand from 1 DAW asks for hardware MIDI outputs in final mixdowns, while offline rendering currently bypasses hardware inserts.

Depends on: Apply backend latency to every recording route
Acceptance:
- A MIDI/audio loopback fixture prints a selected hardware-instrument range into audio takes at the same timeline positions.
- The final offline render contains the printed audio, the original MIDI clips remain intact, and undo restores the pre-print state.
Items:
  - `R-interop-24` Hardware MIDI synths in final mixdowns - partial (track.midi_out; render.render / bounce.in_place run offline in a child process; docs/KNOWN-LIMITATIONS.md:2313 (a hardware insert is bypassed during an export)); complete: External MIDI synths can be routed and recorded by hand, but offline renders can't include their audio.

## Q050-239 Add performance controls for warped clips

**P2 · L06 automation and modulation · feature · L**

Goal: Adjust warp timing by dragging markers on a clip while auditioning it.

Why: Demand is medium (3 DAWs); users cite modulation and warping as creative migration motivators, while current warp menus still lack direct marker-drag performance editing.

Depends on: Expand built-in modulation sources; Expose master and channel controls as modulation targets
Acceptance:
- A user can drag an existing warp marker in the clip view during playback and hear the updated mapping without reopening a dialog.
- The gesture reads back through warp.list, supports undo/redo, and preserves marker ordering and audio boundaries.
- The new gesture uses the registered warp command path and is covered by a GUI interaction test plus a rendered audio check.
Items:
  - `R-instruments-25` Modulation and audio warping as creative tools - partial (modulator.* and warp.* command groups; docs/KNOWN-LIMITATIONS.md:392-410 lists modulation destination and timing limits); complete: Modulation and warp processing are available, but their source/destination range and creative performance controls remain limited.

## Q050-240 Per-note sample start and reverse

**P2 · L07 note editor, MIDI editing, scales, chords, articulations · feature · M**

Goal: Set a sample start offset and reverse flag on an individual note and hear those values during instrument playback.

Why: Medium demand across three DAWs; note editing and ratchets exist but there are no per-note sample-offset or reverse controls.

Acceptance:
- A note-edit command sets and reads the offset and reverse flag with typed range checks.
- A sample playback fixture verifies the audible start point and direction for one note while neighboring notes are unchanged.
- Save, reload, undo, and redo preserve both values.
Items:
  - `R-editing-57` Per-note effect commands - partial (note.edit; note.ratchet; docs/NOTE-EDITOR.md:121; commands_snapshot.json search for per-note sample-offset or reverse commands found none); complete: Probability, slide and repeat controls exist, but sample offset and reverse are not per-note event commands.

## Q050-241 Pattern speed multiplier

**P2 · L07 note editor, MIDI editing, scales, chords, articulations · feature · M**

Goal: Set a MIDI clip's playback-speed multiplier independently of its arrangement length and note-entry grid.

Why: Medium demand across three DAWs; variable clip length exists but no pattern-speed multiplier is exposed.

Acceptance:
- A playback fixture verifies the multiplier changes note timing while the clip's arrangement span is unchanged.
- The multiplier saves and reloads and is editable through a reversible agent command.
- Boundary values are covered by command-schema tests.
Items:
  - `R-editing-58` Flexible pattern length and edit step - partial (docs/NOTE-EDITOR.md:42; docs/NOTE-EDITOR.md:99; clip.set_loop); complete: MIDI clip loop length and note-entry grid are adjustable, but a per-pattern playback-speed control is absent.

## Q050-242 Pattern recurrence controls

**P2 · L07 note editor, MIDI editing, scales, chords, articulations · completion · M**

Goal: Set the recurrence length and pass mask from the note editor so notes can repeat predictably across clip passes.

Why: Medium demand across three DAWs; recurrence logic exists in the note model but its length and mask are not exposed in the editor.

Acceptance:
- The editor exposes recurrence length and pass mask and reads back their saved values.
- Playback tests verify notes sound only on the selected passes, including looped clips.
- A control-socket edit and undo restore the original recurrence settings.
Items:
  - `R-editing-60` Predictable trig conditions across pattern repetitions - partial (src/core/ControlCommandsNoteEdit.cpp:81; src/core/ControlCommandsNoteEdit.cpp:82; docs/NOTE-EDITOR.md:84); complete: Deterministic recurrence masks are supported in note.edit, but the note properties dialog does not expose the play-pass conditions.

## Q050-243 Pattern audition loop region

**P2 · L07 note editor, MIDI editing, scales, chords, articulations · completion · M**

Goal: Set a loop start and end for auditioning a selected region without moving or resizing the arrangement clip.

Why: Medium demand across three DAWs; clip loop support exists but the note editor has no selected-region audition loop control.

Acceptance:
- The editor can set and display loop start and end around a selected region.
- Playback repeats only that region while the arrangement clip position and length remain unchanged.
- A save, reload, and undo test preserves the region.
Items:
  - `R-editing-61` Loop selected pattern regions without altering arrangement - partial (include/ClipEdits.h:118; clip.set_loop; docs/NOTE-EDITOR.md:42); complete: MIDI content loops inside a clip, but the loop begins at the clip start and cannot target an arbitrary selected phrase range.

## Q050-244 Place browser samples into note editing

**P2 · L11 browser, samples, presets, library · feature · M**

Goal: Users can preview a browser sample, match it to the project, and place it directly into note editing.

Why: This request has medium demand across three DAWs; browser preview and clip slicing already exist, but there is no direct browser-to-note-editor placement path.

Depends on: Search sounds by detected key and tempo
Acceptance:
- A GUI check previews a selected sample at the chosen tempo/key match and places it from the browser into the note editor without an external file-browser step.
- The resulting note or sample instrument plays the selected source at the expected pitch, and undo removes the placement in one step.
- The placement controls remain usable for files with unknown detected key or tempo.
Items:
  - `R-editing-54` Audition and tune samples directly into MIDI editing - partial (commands_snapshot.json: browser.preview, clip.slice_to_instrument, window.note_editor_set); complete: Browser preview can match song tempo and key and samples can be sliced to instruments, but there is no direct drag-to-note-editor sample placement flow.

## Q050-245 SMPTE and MTC video scoring workflow

**P2 · L12 projects, files, import/export, interchange, recovery · feature · L**

Goal: Use a project start offset and SMPTE display, send MTC, and mux a selected audio range back into video.

Why: The row reports medium demand across three DAWs and identifies missing MTC and range-mux behavior.

Depends on: Export a selected video range with muxed audio
Acceptance:
- Set a timecode start offset and verify SMPTE transport display and emitted MTC against a known frame-rate fixture.
- Render audio back into attached video and compare the selected range's frame and sample boundaries.
- Verify timecode settings and readback through transport commands and captured MTC output.
Items:
  - `R-workflow-57` Video track and timecode workflow - partial (command ids video.attach/set_offset/window_show (HH:MM:SS:FF in the video window), video.extract_audio; docs/KNOWN-LIMITATIONS.md:932-934 (no MTC), :2373 (one video, absolute path)); complete: A synced video window with timecode and audio extraction exist, but there is no SMPTE timecode in the transport display, no MTC/LTC sync and no video export.

## Q050-246 Measure and correct external MIDI output timing

**P2 · L15 platform: performance, stability, Linux audio, install, hardware controllers · completion · M**

Goal: Users can rely on external MIDI output timing at 1/32-note spacing within the stated latency tolerance.

Why: The queue labels MIDI output timing unknown, with medium demand and no existing loopback-jitter measurement.

Acceptance:
- A MIDI loopback test measures repeated 1/32-note output timing and reports jitter in milliseconds.
- If measured jitter exceeds 1 ms, the regression test proves sub-tick scheduling fixes it; otherwise the measured pass and hardware setup are recorded.
Items:
  - `R-recording-56` External MIDI output timing jitter - unknown (track.midi_out; src/core/midi/MidiAlsaSeq.cpp:278-324 (out events queued and scheduled at tick resolution); no MIDI-out jitter measurement in tests/); fix: Zene sends hardware MIDI but has never measured its output timing, so the reported jitter may or may not apply.

## Q050-247 Document Windows plug-ins on Linux

**P2 · L15 platform: performance, stability, Linux audio, install, hardware controllers · docs · S**

Goal: Linux users can configure Zene’s Wine bridge or yabridge and diagnose common setup failures.

Why: The queue finds no Wine/yabridge guide despite medium demand from users of Windows plug-ins.

Acceptance:
- The guide covers the built-in Wine bridge, yabridge setup, plug-in paths and common launch/bitness errors.
- Each instruction is checked against the supported Linux package and bridge configuration.
Items:
  - `R-workflow-186` Yabridge setup is still difficult for newcomers - partial (CMakeLists.txt:796-808 (Wine-built VST2 bridge); rg -i 'yabridge|wine' over README.md, docs/KNOWN-LIMITATIONS.md and data/manual finds nothing); docs: Windows VST2 runs through the Wine bridge, but there is no setup guide or troubleshooting for Windows plug-ins on Linux.

## Q050-248 Add a quick-record setup flow

**P2 · L03 recording, takes, comping, inputs · feature · L**

Goal: Users can start recording quickly with a new armed track, sensible input gain, and relevant instrument presets.

Why: Medium demand from 1 DAW asks to reduce setup friction; the current meter is one-shot and no quick-record or instrument-classifier action exists.

Acceptance:
- A Quick Record action creates and arms a track, uses meter.input to propose a level, and offers presets for a selected or detected source type.
- Unknown sources receive a usable fallback, and the user can override track type, gain, and preset before recording.
Items:
  - `R-recording-28` A quick-record mode detects instrument, levels, and presets - missing (commands_snapshot.json has no instrument-classifier or quick-record command; meter.input gives a one-shot input level); implement: There is no one-step capture mode that detects the source and sets input level and a starting preset.

## Q050-249 Arranger chord-event editing

**P2 · L07 note editor, MIDI editing, scales, chords, articulations · completion · M**

Goal: Create, move, resize, and remove chord events directly in the arranger's visible chord lane.

Why: Medium demand across two DAWs; the merged tree draws the chord lane but does not support direct event creation and editing there.

Acceptance:
- A user can add and edit chord events from the arranger lane without a socket command.
- A GUI and control-socket test verify event positions, duration, readback, and undo.
- Projects with no chord events retain the existing compact ruler layout.
Items:
  - `R-editing-101` Chord-track composition workflow - partial (src/gui/ArrangerRulerLanes.cpp:271; src/gui/ArrangerRulerLanes.cpp:291; src/gui/shell/ShellCommandMenus.cpp:237); complete: Chord events are visible on the arranger ruler and harmony tools can generate and write chords, but the chord lane is read-only.

## Q050-250 Dock note-editor side panels

**P2 · L07 note editor, MIDI editing, scales, chords, articulations · completion · M**

Goal: Dock the item list and articulation panel into the Edit view's side regions beside the note editor.

Why: Medium demand across one DAW; the Edit view has side regions but the item list floats and there is no articulation panel.

Depends on: Articulation map editor
Acceptance:
- The item list and articulation panel can be docked, resized, hidden, and restored in the Edit view.
- Window state survives editor reopen and exposes the same state through window.get_state.
- A GUI test verifies the docks do not overlap the canvas at supported window sizes.
Items:
  - `R-ui-24` Dockers inside the MIDI editor - partial (docs/UI-SHELL.md:58-60 (Edit view: note editor centre plus Inspector/Devices panels); docs/KNOWN-LIMITATIONS.md:2426 (item list floats)); complete: The note editor has side panels, but tools such as the item list and articulations float.

## Q050-251 Linked immersive deliverable buses

**P2 · L08 mixer, metering, channel strip · feature · L**

Goal: Users can drive linked 7.1.4, 5.1, and stereo deliverable buses from one immersive mix using the selected downmix options.

Why: This medium-demand linked-format request is reported by 1 DAW and needs the shared height-bed/object panner first.

Depends on: Immersive bed and object mixer foundation
Acceptance:
- One source mix feeds linked 7.1.4, 5.1, and stereo buses, with routing and downmix state readable through the agent surface.
- Changing a source level or position updates each output according to the configured downmix options.
- A render test checks channel count and signal placement for each deliverable.
Items:
  - `R-mixing-9` One spatial panner from stereo to Atmos - partial (mixer.surround_pan / mixer.channel_format (one panner for LCR..7.1); mixer.downmix_options; docs/SURROUND-EXPORT.md); complete: One panner covers stereo through 7.1, but not height or larger layouts, and there are no linked multi-format deliverables.

## Q050-252 External Instrument device

**P2 · L09 routing, buses, sends, sidechain, I/O · completion · L**

Goal: Use one device to send MIDI to external gear, select its audio return, monitor it, set latency, and save bank/program and preset state.

Why: track.midi_out currently expects audio to return on a separate monitored audio track (ControlCommandsTrackMidiOut.cpp:67-68); the partial item spans 3 DAWs and a related request reports medium demand in 1 DAW.

Acceptance:
- A registered command and UI device card configure MIDI port/channel, audio input return, monitoring, latency, bank/program changes, and preset recall.
- A simulated MIDI/audio loopback test verifies the return reaches the chosen track, and save/load plus control.undo preserve the device settings.
Items:
  - `B-MX-062` External instruments - partial (src/core/ControlCommandsTrackMidiOut.cpp:67; src/gui/shell/ShellCommandMenus.cpp:269; plans/TRACKER-050.md:413); complete: MIDI can be sent to external hardware, but audio return uses a separate input track instead of one integrated external-instrument device.
  - `R-recording-44` Integrated external instrument device request - partial (plans/TRACKER-050.md:413 (MX-062 PARTIAL: track.midi_out + audio return on a monitored audio track; no single combined device)); complete: MIDI out and audio return are separate surfaces; no combined external-instrument device with latency compensation and program/bank presets.

## Q050-253 Play per-clip effect chains with latency compensation

Goal: Users can attach an optional effect chain to an audio clip, save it with the project, hear it before the track chain, and keep its output aligned with neighboring tracks.

Acceptance:

- Render a clip with an effect before the track chain; compare against an uncompensated reference and verify alignment with neighboring tracks.
- Verify clip-chain save/load, reported latency, and agent state read-back as part of the shared L10 playback/PDC lane.

Depends on: Q050-471. Q050-206 remains separate.

## Q050-254 Region effect ranges

**P2 · L10 devices, instruments, effects, plug-in hosting · feature · L**

Goal: Users can apply an effect chain to a selected timeline region with smooth entry and exit.

Why: No time-range effect chain exists in the model or command surface; this medium-demand request is from 1 DAW.

Acceptance:
- Render material before, inside, and after a region and verify bounded crossfades at both edges.
- Save/load the range and effect settings and expose them to the agent surface.
Items:
  - `R-effects-21` Track-wide region FX - missing (no time-range effect chain in commands_snapshot.json or src/core); implement: There is no effect chain bound to a timeline region across all tracks.

## Q050-255 Edit folder summary clip spans

**P2 · L04 arranger, timeline, clip operations, markers, tempo map · completion · M**

Goal: Users can select, resize, split, and keyboard-edit folder summary spans as group edits over their child tracks.

Why: Folder summary clips can move, copy, and delete but not resize or split; the two related rows cite 2 and 1 DAWs, with medium demand on the group-edit request.

Acceptance:
- FolderSummaryLaneTest covers resizing and splitting a summary span and verifies the matching child clips change together.
- Multi-selection and the keyboard route operate on folder summary spans, and each edit is one undo step.
Items:
  - `B-ED-035` Folder track as one editable summary clip - partial (docs/KNOWN-LIMITATIONS.md:310-317; FolderSummaryLaneTest); complete: Folder summary spans can move, copy, duplicate and delete child clips, but cannot resize, split or join ordinary clip selections.
  - `R-editing-51` Folder clips for group arrangement edits - partial (docs/KNOWN-LIMITATIONS.md:310-317 (folder summary clips move/copy/delete; cannot be resized or split)); complete: Folder summary clips move, copy, duplicate and delete their children's material, but cannot be resized, split or selected with ordinary clips and have no keyboard route.

## Q050-256 Group-quantize an edit set from one anchor

**P2 · L05 audio editing, warping, stretching, fades · feature · L**

Goal: Users can derive warp markers from one track's transients and apply the same timing correction across a multitrack edit set.

Why: Group quantize is medium demand across 2 DAWs, while warp.quantize is per-clip and edit-set operations are separate commands.

Depends on: Edit and audition transient and slice markers; Author warp markers with on-clip preview
Acceptance:
- Choose an anchor track, quantize its selected transients, and verify equivalent warp timing is applied to every edit-set member.
- Render a multitrack drum fixture and verify phase alignment is preserved across the group.
- Run the action from the multitrack panel and the agent command, then verify undo restores all members.
Items:
  - `R-editing-67` Centralized multitrack audio editing - partial (vca.edit_split/edit_move/edit_slip/edit_trim/edit_fade (edit set); warp.quantize is per clip (src/core/ControlCommandsWarpQuantize.cpp); clip.split_at, clip.slice_to_instrument); complete: Group-locked split/move/slip/trim/fade exist, but transient detection and quantise are per clip with no phase-coherent multitrack quantise or one combined editor.

## Q050-257 Store parameter automation with MIDI clips

**P2 · L06 automation and modulation · feature · L**

Goal: Move, copy, and loop device-parameter automation together with its owning MIDI clip.

Why: Demand is medium (2 DAWs); the current automation lane follows its track, but parameter automation does not travel with the MIDI pattern during rearrangement.

Acceptance:
- A MIDI clip can own parameter-automation data and move, copy, stretch, and loop it with the clip.
- Playback evaluates the copied or looped curve against the bound parameter; save/load and undo/redo preserve ownership and edits.
- A command-surface test exposes the same clip automation operations to an agent.
Items:
  - `R-editing-68` Automation attached to patterns or arrangement tracks - partial (automation.lane_add (lane under the track, follows it); src/core/ControlCommandsRange.cpp:495 automation_follows_clips; src/gui/shell/ShellCommandMenus.cpp:99); complete: Automation lanes sit under their track and their points follow clip moves, but device-parameter automation cannot live inside a pattern/MIDI clip (only CC lanes can).

## Q050-258 Overlay automation over arrangement clips

**P2 · L06 automation and modulation · feature · L**

Goal: Choose one automation lane to draw and edit over its track's arrangement clips.

Why: Demand is medium (2 DAWs); parameter automation is still drawn on separate tracks, and the existing clip-gain envelope only covers clip gain.

Acceptance:
- A track can display one selected parameter-automation curve over its clips while other lanes remain below or hidden.
- The overlay supports point selection and edits without making the underlying audio or MIDI clips unselectable.
- A GUI test checks overlay visibility, edit/undo, and reopening the project with the selected display mode.
Items:
  - `R-workflow-70` Automation displayed over events - partial (command id clip.gain_envelope_set (clip gain on the clip); automation lanes are separate tracks (src/core/ControlCommandsAutomationLanes.cpp:69-77)); complete: Clip gain is drawn on the clip, but track volume, pan and plug-in automation cannot be shown over the clips.

## Q050-259 Add copy/paste and Write on Play automation workflow

**P2 · L06 automation and modulation · feature · L**

Goal: Copy automation between parameters or tracks, write stopped edits when playback starts, and manage the work in a compact dockable panel.

Why: Demand is medium (2 DAWs); cross-target copy/paste and Write on Play are absent from the current automation command set, which only includes write-to-end and existing read/write modes.

Depends on: Complete automation write-pass helpers
Acceptance:
- The Automation panel can dock or float, stays compact, and selects the active parameter and lane.
- Copy/paste supports a clear time/value mapping across parameters and tracks and reports incompatible targets without silent loss.
- Write on Play records edits made while stopped when playback begins; a GUI and command-surface test cover the mode, panel state, undo, and save/load.
Items:
  - `R-workflow-69` Automation copy, compact panel, and Write on Play - partial (command ids automation.mode_set (touch/latch), automation.write_to_end, automation.transform; docs/KNOWN-LIMITATIONS.md:2551-2552 (write to end is a helper, not a pass mode)); complete: Write modes and write-to-end exist, but automation cannot be copied to another parameter or track and there is no Write on Play.

## Q050-260 MIDI phrase-channel routing

**P2 · L07 note editor, MIDI editing, scales, chords, articulations · feature · M**

Goal: Assign a MIDI channel to a clip or note and route it ahead of the instrument track's fixed part zones.

Why: Medium demand across two DAWs; plugin parts provide channel zones but notes have no per-note or per-clip channel override.

Acceptance:
- A multichannel MIDI fixture verifies clip and note overrides reach the requested channel and preserve key and velocity zones.
- The routing state saves and reloads and is editable through a reversible command.
- Unassigned notes retain the current part-zone behavior.
Items:
  - `R-interop-31` Per-instrument MIDI channel phrase routing - partial (plugin.midi_parts (fixed key/velocity zones per channel); notes carry no per-note channel); complete: Parts route by key and velocity zones, not by phrase or clip.

## Q050-261 Find-and-change condition tools

**P2 · L07 note editor, MIDI editing, scales, chords, articulations · completion · M**

Goal: Filter and apply track or clip actions by conditions and by position within a bar or phrase.

Why: Medium demand across two DAWs; Find and Change exists but lacks track and clip conditions, actions, and phrase-position filters.

Acceptance:
- The dialog supports position-in-bar and phrase filters plus route, rename, and color actions.
- A preview lists the exact target clips and tracks before an apply operation.
- A control-socket test verifies the same result and one undo restores all affected state.
Items:
  - `R-workflow-84` More capable Project and Logical Editors - partial (src/gui/noteeditor/NoteEditorMenus.cpp:256-259 (Select Matching, Find & Change); command ids workflow.*, script.run); complete: A note-level logical editor exists, but there is no track/clip-level logical editor (e.g. change outputs of matching tracks) or phrase-position filter.

## Q050-262 Surround-aware send panning

**P2 · L08 mixer, metering, channel strip · completion · M**

Goal: Users can pan sends into surround channels with the destination's speaker layout, divergence, and LFE send settings.

Why: A medium-demand send/panning gap appears in 2 DAWs; sends currently expose stereo balance instead of the surround panner.

Acceptance:
- A surround send uses the destination channel's surround panner and exposes divergence/LFE state through the agent surface.
- A multichannel render test verifies front/rear and LFE placement while stereo sends preserve their existing balance behavior.
Items:
  - `R-mixing-23` Surround panning and mix translation - partial (mixer.surround_pan; mixer.send_pan (stereo balance only); src/core/MixerSurroundMix.cpp:25-32; docs/KNOWN-LIMITATIONS.md:2401-2406); complete: There's a surround channel panner, but sends aren't speaker-aware and there's no ADM translation.

## Q050-263 Per-slice SlicerT controls

**P2 · L10 devices, instruments, effects, plug-in hosting · feature · M**

Goal: Users can adjust gain, pan, filter, and random amount on each SlicerT slice.

Why: SlicerT slices lack the requested per-slice controls; demand is medium across 2 DAWs.

Acceptance:
- Set distinct per-slice values and verify them in a rendered slice sequence.
- Save/load the settings and verify agent read-back.
Items:
  - `R-effects-33` Beat slicing and granular sample manipulation - partial (plugins/SlicerT/SlicerT.cpp:43-65 (slices without per-slice filter or pan); plugins/GranularPitchShifter; clip.set_formant); complete: Slicing, granular pitch shifting and formant shift exist, but slices have no per-slice filter, pan or randomisation.

## Q050-264 Named bank/program maps for instruments

**P2 · L10 devices, instruments, effects, plug-in hosting · feature · M**

Goal: Users can import, edit, and select named bank/program maps for hosted instruments.

Why: ArticulationMap has no bank map support; demand is medium across 2 DAWs.

Acceptance:
- Import and edit a named map, select a mapped program, and verify the emitted bank/program values.
- Save/load the map and expose selection through an agent command.
Items:
  - `R-instruments-29` Custom VST instrument maps - partial (track.articulations_set; include/ArticulationMap.h:40-48 exposes keyswitch, program and controller fields but no bank map.); complete: Named articulations and program changes exist, but users still lack a named VST bank and program map editor.

## Q050-265 Out-of-process VST3 and CLAP hosting

**P2 · L10 devices, instruments, effects, plug-in hosting · feature · L**

Goal: Users can isolate VST3 and CLAP plug-in crashes from the main application and restart an instance.

Why: VST3 and CLAP currently run in-process; the isolation request is medium demand across 2 DAWs.

Acceptance:
- Select isolation per instance, deliberately crash a fixture plug-in, and verify the project and host process remain alive.
- Restart the instance and verify saved state, status, and agent read-back.
Items:
  - `R-performance-70` Plug-in crash isolation - partial (docs/OUT-OF-PROCESS-BEYOND-ZYN.md §1 (VST2 always separate, Zyn opt-in; VST3, CLAP, LV2, LADSPA in-process 'no-client')); complete: VST2 and ZynAddSubFx can run out of process, but VST3, CLAP and LV2 plug-ins load into Zene's process, so their crashes take Zene down.

## Q050-266 Out-of-process LV2 and remaining host families

**P2 · L10 devices, instruments, effects, plug-in hosting · feature · L**

Goal: Users can opt supported LV2 plug-ins into the same crash-isolated client.

Why: Crash isolation currently covers only Zyn while LV2 and other families remain in-process; demand is medium across 2 DAWs.

Depends on: Out-of-process VST3 and CLAP hosting
Acceptance:
- Crash an LV2 fixture in the isolated client and verify recovery without host exit.
- Document unsupported format limits and retain the existing Zyn isolation path.
Items:
  - `R-hosting-1` Plug-in crash isolation - partial (docs/OOP-HOSTING.md:1-6 scopes opt-in isolation to ZynAddSubFx; VST3 and CLAP are in-process per docs/KNOWN-LIMITATIONS.md:219-225); complete: Crash isolation exists for VST2 and opt-in ZynAddSubFx, but most VST3 and CLAP hosts remain in-process.

## Q050-267 Zene Rack plug-in module and state bridge

**P2 · L10 devices, instruments, effects, plug-in hosting · feature · L**

Goal: Users can load a Zene Rack plug-in in another host and transfer its device-chain state.

Why: There is no Zene rack plug-in target and the request is medium demand across 2 DAWs.

Acceptance:
- Build a VST3 Rack module that loads a documented chain and restores it from plug-in state.
- Verify standalone project state and plug-in state convert without loss.
Items:
  - `R-hosting-8` Free DAW with a Rack Plugin edition - missing (plugins/Vst3Effect/ and plugins/ClapEffect/ host third-party effects; no Zene rack product plug-in module or build target found); implement: Zene is a standalone host and does not ship itself as a device rack plug-in inside another DAW.

## Q050-268 Save and browse reusable automation and MIDI clips

**P2 · L11 browser, samples, presets, library · feature · M**

Goal: Users can save automation and MIDI material as reusable library items and load automation onto a chosen parameter.

Why: This request has medium demand across two DAWs; browser.save_clip handles sample and MIDI clips today but has no automation-clip save path.

Acceptance:
- browser.save_clip saves an automation clip with its points and timing to a reusable file, in addition to the existing sample/MIDI paths.
- The browser lists saved automation and MIDI parts with search and a safe load/apply action for a chosen parameter.
- A round-trip test verifies the saved events and one-step undo restores the target project state.
Items:
  - `R-workflow-66` Reusable automation and MIDI content libraries - partial (command id browser.save_clip (sample clip as WAV, MIDI clip as MIDI file); no automation clip save); complete: MIDI and audio clips can be saved into the library, but automation clips cannot be stored and reused.

## Q050-269 Hardware video decoding with software fallback

**P2 · L12 projects, files, import/export, interchange, recovery · feature · M**

Goal: Use FFmpeg hardware decoding when available, fall back to software otherwise, and report the active decoder.

Why: The row reports medium demand across two DAWs and verifies that the current FFmpeg path has no hardware acceleration.

Acceptance:
- On a machine with an available hardware decoder, verify video.get_state reports it and playback decodes correctly.
- Force hardware decoder initialization failure and verify software fallback is reported and playback remains correct.
Items:
  - `R-performance-49` GPU-accelerated video engine - partial (docs/KNOWN-LIMITATIONS.md:2373-2377; src/core/VideoFfmpeg.cpp (no hwaccel/hw_device use)); complete: Video plays through FFmpeg in software; there is no GPU decoding.

## Q050-270 Build the expressive pad model and input path

**P2 · L15 platform: performance, stability, Linux audio, install, hardware controllers · feature · L**

Goal: Performers can trigger multi-zone pads with pressure, pitch and X/Y zone switching from a supported controller.

Why: Key and velocity zones and MPE state exist, but the expressive X/Y performance surface is missing; demand is medium.

Acceptance:
- A controller-input test routes pressure, pitch and X/Y changes to the selected pad zone without dropping expression events.
- The pad model exposes its zone and expression state through registered commands with headless tests.
- Realtime tests show no audio-thread allocation or locking.
Items:
  - `R-instruments-31` Expressive multi-zone performance pads - partial (rack.layer_zone_add supports key and velocity zones; device.mpe_get_state; no MPCe X/Y multi-zone surface found in src/gui/.); complete: Layer zones and MPE input exist, but the four-zone pressure and X/Y performance surface is missing.

## Q050-271 Connect project revisions and merge review to collaboration

**P2 · L16 collaboration and sharing · feature · L**

Goal: Collaborators can review separate project versions and merge selected changes into the shared project with a reversible result.

Why: Demand is medium across 2 DAWs, and project.save_version/project.merge exist but the host-forwarded collaboration stream has no shared branch or merge-review history.

Depends on: Attribute peer edits for targeted rollback
Acceptance:
- A two-peer test creates named project versions, compares them, and presents merge conflicts before applying a merge.
- The merge uses project.save_version and project.merge through the agent command surface and records an undoable result without overwriting either source revision.
- A saved and reopened project retains the revision ancestry needed for review.
Items:
  - `R-collaboration-18` Music-project branching and merge - partial (command ids project.save_version and project.merge; src/core/ControlCommandsCollab.cpp:181-185); complete: Local project versions and offline three-way file merge exist, but the live peer session has no branch or review history.

## Q050-272 Stock-device freshness review

**P2 · L10 devices, instruments, effects, plug-in hosting · completion · M**

Goal: Users can find a curated, documented set of current stock devices and presets for common workflows.

Why: The catalogue is broad but has no recent device/preset audit; the request is medium demand from 1 DAW.

Acceptance:
- Publish a gap table for stock devices and presets, then implement the single highest-impact agreed item.
- Link every shipped change to a reproducible fixture or example project.
Items:
  - `R-instruments-49` Refresh aging stock instruments and effects - partial (plugin.list; plugins/ contains a broad stock device set; plans/TRACKER-050.md has no stock-device freshness audit.); complete: The inherited instrument and effect set is broad, but its comparative freshness has not been reviewed or refreshed.

## Q050-273 Save a launcher clip to the shared library

**P2 · L02 launcher / session clips / performance · feature · S**

Goal: Save an audio or MIDI launcher cell to the clip library and reuse it in a later cell or session.

Why: Demand is medium across 1 DAW; arrangement clips already have Save to Library, but the launcher cell menu has no equivalent.

Acceptance:
- A populated cell's context menu offers Save to Library and saves its clip through browser.save_clip.
- The saved library entry preserves the cell's audio source or pattern notes and can be inserted into another cell after reopening the project.
- A command-surface test performs the same save and read-back without opening the interface.
Items:
  - `R-workflow-276` Cue clip library reuse - partial (src/gui/SessionGridDrop.cpp:2 (drop audio from the browser onto a cell); src/gui/LibraryCommandMenus.cpp:262 (Save to Library for arrangement clips); src/gui/SessionGridMenus.cpp:145-170 (cell menu has no Save to Library)); complete: Browser clips drop into launcher cells, but a cell's own clip can't be saved back to the library.

## Q050-274 Add touch swipes to comp selection

**P2 · L03 recording, takes, comping, inputs · completion · S**

Goal: Users can select take regions by swiping on a touch screen.

Why: Touch comping has medium demand, while CompSwipeStrip currently handles mouse input and the row found no touch handlers.

Acceptance:
- A Qt touch-event test swipes across a lane and verifies comp.get_state selects the same region.
- Existing mouse and keyboard comp gestures still pass their GUI tests.
Items:
  - `R-recording-59` Quick Swipe Comping on iPad feels fast and direct - partial (src/gui/CompSwipeStrip.cpp (no touch/gesture handling; rg QTouchEvent/grabGesture finds only src/gui/ArrangerAids.cpp:114-121 pinch zoom)); complete: Swipe comping is mouse-only; touch input reaches only arranger pinch zoom.

## Q050-275 Add named alternate clip playlists

**P2 · L03 recording, takes, comping, inputs · feature · M**

Goal: Users can save, switch, and comp from named alternate clip sets on a track.

Why: Medium demand from 1 DAW asks for playlist switching, while the current comp commands expose lanes but no saved alternate clip-set playlists.

Acceptance:
- A control-socket test creates two named playlists, switches between them, and verifies each clip set survives save and reload.
- Playlist changes have an undo/redo path and can be used as comp sources.
Items:
  - `R-recording-27` Playlist-based takes and alternate edits - partial (comp.lane_add; comp.assign; comp.get_state; no playlist command in commands_snapshot.json); complete: Take lanes hold alternate takes, but there is no named playlist bank for switching whole alternate edits of a track.

## Q050-276 Switch recording inputs live while idle

**P2 · L03 recording, takes, comping, inputs · feature · M**

Goal: Users can change the capture device or input route without restarting when no recording is active.

Why: Medium demand from 1 DAW; record.input_set explicitly reports restart_required, so an idle-session input change cannot take effect.

Acceptance:
- record.input_set while idle reopens capture and updates route state without restart_required.
- A change attempted during recording is safely refused or queued, and the next take uses only the selected input.
Items:
  - `R-recording-34` Audio input changes apply only to the next recording start - partial (record.input_set ('restart_required' true; no live device swap); docs/KNOWN-LIMITATIONS.md:1538-1540); complete: Changing the capture input or channel count takes effect only after a restart.

## Q050-277 Capture an exact number of externally clocked bars

**P2 · L03 recording, takes, comping, inputs · feature · M**

Goal: Users can start an external synth and capture exactly the chosen number of bars or steps.

Why: Medium demand from 1 DAW seeks exact external-synth loop lengths; current bar capture does not coordinate the capture with MIDI clock start and stop.

Acceptance:
- A virtual MIDI clock fixture observes START and STOP at the chosen boundaries.
- The captured take has the requested musical length and remains aligned after save and reload.
Items:
  - `R-recording-42` Automatically record exact-length loops from external synths - partial (session.record_slot ('bars' bounded capture on the launch grid); clock.master_set (sends START/STOP/SPP to external gear)); complete: Bar-bounded slot capture and MIDI clock master exist, but there is no single step-count capture that starts the external sequence and stops on an exact length.

## Q050-278 Resample the master into a sampler track

**P2 · L03 recording, takes, comping, inputs · feature · M**

Goal: Users can capture the internal master output and immediately play it from a new sampler track.

Why: Medium demand from 1 DAW; record.route_source can write master audio to a WAV, but there is no workflow that turns it into a sampler clip.

Acceptance:
- A control-socket action captures a non-silent master signal and creates an AudioFileProcessor or SlicerT clip from it.
- The new track survives save/reload and one undo removes the resampled track.
Items:
  - `R-recording-45` Live record the internal master into a sampler - partial (record.route_source (source master = resampling into a WAV route); session.record_slot records input only); complete: Master can be resampled to a WAV but there is no direct capture into a sampler/slicer channel.

## Q050-279 Timestamp MIDI input through tempo changes

**P2 · L03 recording, takes, comping, inputs · feature · M**

Goal: Live MIDI notes land at the correct sample positions even when the project tempo changes during recording.

Why: Medium demand from 1 DAW targets live MIDI timing, while input timestamps are currently taken when the reader observes each byte and can have about 5 ms of jitter.

Acceptance:
- A timestamped MIDI fixture crossing a tempo-map change places notes at their expected frame positions within one frame.
- A jitter test records the measured input timestamp error and proves the bound under the ALSA raw-reader path.
Items:
  - `R-recording-47` Lower-jitter MIDI recording through tempo changes - partial (include/MidiClock.h:112-116 (MIDI input timestamps taken when the reader thread observes the byte; ALSA raw reader ~5 ms jitter)); complete: Live MIDI is time-stamped at read time with documented reader jitter; no sub-period, tempo-map-aware note placement or measured bound.

## Q050-280 Loop automation clips and make loop passes unique

**P2 · L04 arranger, timeline, clip operations, markers, tempo map · completion · M**

Goal: Users can loop automation clips and turn loop passes into separate editable clips.

Why: clip.set_loop supports MIDI and audio but not automation, and no command converts loop passes into clips; the request has medium demand in 1 DAW.

Acceptance:
- A regression test loops an automation clip and verifies the automation repeats at the requested boundaries and persists after save/load.
- A Make Unique operation materializes loop passes as separate clips and one undo restores the looped source.
Items:
  - `R-editing-94` Loop instances share edits across audio, MIDI, and automation clips - partial (clip.set_loop MIDI and audio only (src/core/ControlCommandsClipEnvelope.cpp:287-293); no loop on automation clips; no convert-loop-to-unique-clips); complete: MIDI and audio clips loop their content, but automation clips cannot loop and loop passes cannot be turned into separate clips.

## Q050-281 Correct tempo-map ramp documentation

**P2 · L04 arranger, timeline, clip operations, markers, tempo map · docs · S**

Goal: Users can find accurate documentation for tempo ramps, the tempo-map editor, and what the arranger tempo lane displays.

Why: The code and panel support linear ramp-to-next events, while docs/TEMPO-MAP.md still says there is no editor and tempo events are steps; the mismatch is reported at medium demand in 1 DAW.

Acceptance:
- Update docs/TEMPO-MAP.md so it no longer says there is no tempo-map editor or no tempo curves, and accurately describes ramp-to-next behavior and ruler display.
- Check each documented command and UI behavior against the merged tree.
Items:
  - `R-editing-99` Continuous ritardando and accelerando in Hydrogen - have (transport.tempo_map_add 'ramp' (linear tempo to the next event); docs/TEMPO-MAP.md:157 still says 'No tempo curves'); docs: Tempo ramps ship (ED-050), but docs/TEMPO-MAP.md still says ramps are not in scope.

## Q050-282 Follow live tempo during recording

**P2 · L04 arranger, timeline, clip operations, markers, tempo map · feature · L**

Goal: Users can record without a click while the transport follows live input onsets and writes tempo-map events.

Why: transport.tempo_map_from_clip maps a finished performance but no live-follow command exists; the proposed extension has medium demand in 1 DAW.

Acceptance:
- A deterministic input test feeds a known variable-tempo onset sequence and verifies bounded-latency tempo-map updates during recording.
- The feature can be disabled, reports its state, and does not alter a take when no reliable onset is detected.
Items:
  - `R-recording-51` Smart tempo follows live playing and variable performance - partial (transport.tempo_map_from_clip (beat mapping from a free performance's transients); no live tempo-follow command); complete: Tempo extraction from a recorded performance exists; live tempo following of an unclicked player does not.

## Q050-283 Create a neutral configurable hybrid track

**P2 · L04 arranger, timeline, clip operations, markers, tempo map · feature · L**

Goal: Users can create a track before choosing whether it will take audio input or host a virtual instrument.

Why: SongEditor currently offers separate instrument, sample, and automation track creation, while the related hybrid-track requests have medium demand in 1 DAW.

Acceptance:
- A GUI test creates an unconfigured track, assigns an audio input or instrument later, and verifies the track keeps its identity and arrangement position.
- Track type choice can be changed without losing project state, and the neutral track round-trips through save/load.
Items:
  - `R-workflow-286` Start recording without configuring track types - partial (src/gui/editors/SongEditor.cpp:925-944 (separate instrument, sample and automation track types)); implement: A track's type must be chosen before recording. A hybrid track (R-workflow-162) would remove that choice.

## Q050-284 Play audio and MIDI clips on hybrid tracks

**P2 · L04 arranger, timeline, clip operations, markers, tempo map · feature · L**

Goal: One arrangement track can contain and play both audio and MIDI clips through its shared mixer channel.

Why: Track code has no hybrid class and separates instrument from sample tracks; this mixed-media request has medium demand in 1 DAW.

Depends on: Create a neutral configurable hybrid track
Acceptance:
- A regression test places an audio clip and a MIDI clip on the same hybrid track and verifies both render through its one mixer channel.
- Both clip types remain visible and editable on the same arranger row after save/load.
Items:
  - `R-workflow-162` One track can carry different media types - missing (Zene tracks are typed (instrument, sample, automation); rg hybrid over track code found no hybrid track type); implement: A track can't hold audio and MIDI clips together. Each media type needs its own track type.

## Q050-285 Edit and record audio and MIDI on hybrid tracks

**P2 · L04 arranger, timeline, clip operations, markers, tempo map · feature · L**

Goal: Users can record, arrange, and switch between audio and instrument material in one shared track context.

Why: The request describes a hybrid audio/MIDI track workflow that typed instrument and sample tracks cannot provide; demand is medium in 1 DAW.

Depends on: Play audio and MIDI clips on hybrid tracks
Acceptance:
- A GUI test records or imports audio and MIDI clips into one hybrid track and verifies both remain selectable, movable, and audible.
- Changing the track's active input or instrument does not remove existing clips, and undo restores the prior configuration.
Items:
  - `R-workflow-278` Hybrid audio and instrument tracks - missing (typed tracks only (instrument and sample); see R-workflow-162); implement: No Zene track holds note and audio clips together.

## Q050-286 Tag and comment on tracks and clips

**P2 · L04 arranger, timeline, clip operations, markers, tempo map · feature · M**

Goal: Users can add saved tags and comments to tracks and clips and search for them in arranger lists and filters.

Why: The evidence finds name-only Clip List and channel search with no saved tag or comment fields; the request has medium demand in 1 DAW.

Acceptance:
- A regression test saves and reloads track and clip tags/comments and finds matches from the Clip List and track search.
- Search filters match names, tags, and comments without returning unrelated items.
Items:
  - `R-workflow-172` Search tracks and chunks by tags and comments - partial (src/gui/CommandLists.cpp:257 (Clip List found by name); src/gui/shell/ShellCommandMenus.cpp:513 (Find a Channel); no comment or tag field on tracks or clips (rg comment over track command code found nothing)); complete: Tracks, channels and clips can be found by name, but they carry no tags or comments to search.

## Q050-287 Pin any track in the arranger

**P2 · L04 arranger, timeline, clip operations, markers, tempo map · completion · S**

Goal: Users can pin any track type at the top of the arranger pin strip while scrolling.

Why: TrackPinStrip and track.set_pinned currently support folders only; the extension has medium demand in 1 DAW.

Acceptance:
- A GUI test pins instrument, sample, and automation tracks and verifies each remains visible in the pin strip while the arranger scrolls.
- track.set_pinned accepts every supported track type and pin state survives save/load.
Items:
  - `R-workflow-181` Track Pin - partial (src/gui/TrackPinStrip.cpp:2,83 (only folders pin: folder->isPinned()); track.set_pinned 'Pin or unpin a folder'); complete: Only folder tracks can be pinned above the scrolling list, not an individual track.

## Q050-288 Correct Sample Editor capability docs

**P2 · L05 audio editing, warping, stretching, fades · docs · S**

Goal: Users can see which destructive range operations the Sample Editor supports and which tools remain absent.

Why: The documentation request is medium demand from 1 DAW, but the GUI already has range operations that the current text calls socket-only.

Acceptance:
- Rewrite the cited KNOWN-LIMITATIONS section to list the current gain, silence, delete, reverse, fade, and crop range operations.
- Keep the real bounds explicit: there is no limiter, fades are linear, and noise reduction is absent; check the command and GUI names against the implementation.
Items:
  - `R-editing-74` Destructive sample-edit tools are socket-only - partial (src/gui/SampleEditorDialog.cpp:175-182 range ops (gain, silence, delete, reverse, fades, crop); sample.edit_range; clip Process menu (src/gui/shell/ShellCommandMenus.cpp:217-219); docs/KNOWN-LIMITATIONS.md:1983-2000 still says socket-only, no range); docs: The Sample Editor now gives destructive edits a GUI and a range model, but KNOWN-LIMITATIONS still says they are socket-only with no range; a limiter and noise reduction are still absent.

## Q050-289 Estimate stem jobs and use available GPU providers

**P2 · L05 audio editing, warping, stretching, fades · feature · M**

Goal: Users can see a duration estimate and use an available GPU provider for offline stem processing with CPU fallback.

Why: GPU execution and job estimates are medium demand from 1 DAW, and the current ONNX path exposes no provider setting or estimate.

Depends on: Choose stem-separation quality and compare output; Add six-stem HTDemucs extraction
Acceptance:
- Show a pre-start estimate for known clip duration, model, and quality settings and compare estimates with measured runs.
- When a supported GPU provider is available, verify it is selected and reported; when unavailable, verify explicit CPU fallback.
- Verify cancellation and provider choice through job state and the agent command.
Items:
  - `R-performance-54` Stem separation speed by hardware - partial (stem.get_state (onnxruntime-cpp, realtime false); rg 'ExecutionProvider|cuda|coreml|directml' over src/include: none; stem.job_status, stem.job_cancel); complete: Separation runs offline with progress and cancel, but on the CPU only, with no GPU path or time estimate.

## Q050-290 Play granular or spectral samples with per-slice modulation

**P2 · L05 audio editing, warping, stretching, fades · feature · L**

Goal: Users can play slices through granular or spectral playback and modulate parameters independently for each slice.

Why: Per-slice spectral/granular playback is medium demand from 1 DAW, while current slicing and warp tools have no matching sampler or modulation device.

Acceptance:
- Load a sliced sample, choose granular or spectral playback, and verify per-slice modulation changes only the selected slice.
- Render deterministic note patterns and verify playback, modulation state, save/load, and agent read-back.
Items:
  - `R-instruments-36` Sliced and spectral sampler workflow - partial (clip.slice_to_instrument; sample.spectrogram; warp.stretch; no granular sampler or per-slice modulation device found.); complete: Slicing, spectrogram display and time stretching exist, but granular resynthesis and per-slice modulation are absent.

## Q050-291 Convert MIDI CC lanes and arrangement automation

**P2 · L06 automation and modulation · feature · L**

Goal: View device automation in the note editor and convert selected CC data to or from an automation clip.

Why: Demand is medium (1 DAW); CC controllers remain inside MIDI clips while device automation lives in separate clips, so the requested in-context conversion is missing.

Acceptance:
- The note editor lane chooser can display the owning track's device-automation lane alongside clip CC lanes.
- Conversion asks for an explicit target when needed and preserves timing/value data in both directions; unmatched or unsupported mappings return a visible refusal.
- The agent command surface can perform and read back conversions, with undo and save/load coverage.
Items:
  - `R-editing-93` Closer MIDI editor and arrangement automation parity - partial (clip.controllers_set (CC lanes inside clips) vs automation clips; docs/KNOWN-LIMITATIONS.md:2510-2516); complete: CC lanes in clips and arrangement automation are separate systems with no shared editing or conversion.

## Q050-292 Copy automation from reference projects

**P2 · L06 automation and modulation · feature · M**

Goal: Reuse a reference project's automation clips with their parameters rebound in the active project.

Why: Demand is medium (1 DAW); the current limitation says reference-project automation clips cannot be copied, and the requested rebind-by-parameter behavior is absent.

Depends on: Store parameter automation with MIDI clips
Acceptance:
- reference.copy_clips copies automation clips and clip-contained automation from a reference project.
- Targets rebind by stable parameter identity; unmatched targets are listed in the result and are not silently attached to another parameter.
- Undo, save/load, and a cross-project test prove copied automation keeps its timing, values, and target.
Items:
  - `R-editing-96` Reusable clip-based automation - partial (automation clips copy and stretch (automation.transform time_scale); docs/KNOWN-LIMITATIONS.md:2360 automation clips cannot be copied from the reference project); complete: Automation clips can be copied and stretched within a project, but cannot be brought from another project or carried inside a MIDI clip.

## Q050-293 Selection-based MIDI transformations

**P2 · L07 note editor, MIDI editing, scales, chords, articulations · completion · S**

Goal: Filter note selection by muted state and every-nth position and invert the matching selection in the editor.

Why: Medium demand across one DAW; note.select_where supports several filters but not muted or every-nth matching.

Acceptance:
- Select Matching exposes muted and every-nth filters and an invert action.
- A control-socket test verifies exact selected note IDs without modifying note data.
- The selection is visible to the editor and Escape clears it.
Items:
  - `R-editing-78` Selection-based MIDI transformations - partial (note.select_where filter key/velocity/position/length/scale + invert (src/core/ControlCommandsNoteShape.cpp:257-308); Select Matching... (src/gui/noteeditor/NoteEditorMenus.cpp:256)); complete: Notes can be selected by key, velocity, position, length and scale, but not by muted state, every-nth/alternate, or by inverting the current selection.

## Q050-294 Melodic step-sequencer view

**P2 · L07 note editor, MIDI editing, scales, chords, articulations · feature · L**

Goal: Edit a MIDI clip in a step grid with scale-aware pitch, velocity, chance, ratchet, and shape-based fills.

Why: Medium demand across one DAW; the current generator creates rhythm-only notes and has no melodic step-sequencer view.

Acceptance:
- A clip opens in a step grid and edits per-step pitch, velocity, chance, and ratchet.
- Shape and random fills are repeatable under a fixed seed and produce notes visible in the piano roll.
- A control-socket test reads the same clip state and one undo restores the prior pattern.
Items:
  - `R-editing-83` Melodic pattern sequencing - partial (note.generate (one key, rhythm only); scale.define custom scales; note.randomize; inherited PatternEditor beat/bassline); complete: Rhythm generation, custom scales and randomisation exist, but there is no melodic step-sequencer mode with per-step pitch.

## Q050-295 Pitch-curve vibrato controls

**P2 · L07 note editor, MIDI editing, scales, chords, articulations · completion · S**

Goal: Replace the fixed vibrato preset with controls for rate, depth, delay, and fade-in that write the note's pitch curve.

Why: Medium demand across one DAW; the editor has one fixed vibrato preset and no parameter form.

Acceptance:
- The vibrato form exposes rate, depth, delay, and fade-in and previews the resulting curve.
- A curve test verifies each parameter changes the expected points and preserves the note after save and reload.
- The existing preset remains reproducible through its equivalent form values.
Items:
  - `R-editing-86` More expressive per-note vibrato controls - partial (note.pitch_curve_set; Pitch Curve > Vibrato is one fixed preset (src/gui/noteeditor/NoteEditorMenus.cpp:300-305)); complete: Per-note pitch curves exist, but the vibrato preset has a fixed rate and depth with no delay or fade.

## Q050-296 End-to-end surround processing and delivery

**P2 · L08 mixer, metering, channel strip · completion · L**

Goal: Users can process, meter, compensate, stem-render, and queue surround channels without losing channels beyond the stereo pair.

Why: A medium-demand end-to-end gap is reported by 1 DAW; extra speakers still lack PDC, metering, stem/render-queue, and device support.

Acceptance:
- A 5.1/7.1 test chain with known device latency retains all speaker channels and is compensated to the aligned output.
- Per-speaker meters report the test signal, and surround stem plus render-queue outputs retain the configured channel count and order.
- A multichannel device-processing test verifies every speaker channel reaches and leaves the device path.
Items:
  - `R-mixing-11` Surround mixing still lacks end-to-end channel support - partial (docs/KNOWN-LIMITATIONS.md:2401-2406 (per-track renders, stems, render queue and mastering stereo; extra channels get no PDC; devices, meters, cue, listen and recorder L/R only); docs/SURROUND-EXPORT.md); complete: Surround stops at the mix bus: extra channels get no PDC, device processing, metering, stems or cue paths.

## Q050-297 Bulk send and VCA assignment for selected tracks

**P2 · L08 mixer, metering, channel strip · completion · M**

Goal: Users can assign or remove selected tracks from a bus or VCA group in one action.

Why: A medium-demand batch-assignment request comes from 1 DAW; sends already support batching while VCA assignment is single-channel.

Acceptance:
- vca.assign_many and vca.unassign_many apply to the selected channel set, with a selected-strips action for buses and VCA groups.
- One undo reverses the entire batch, and state read-back lists the resulting bus/VCA assignments.
- Tests cover mixed valid selections, duplicate targets, and unassigning a subset.
Items:
  - `R-mixing-28` Assign selected tracks to buses or VCAs in one action - partial (mixer.send_many (several channels to one bus, one undo); vca.assign takes one channel per call); complete: Bus assignment works across several channels at once, but VCA assignment is one channel per call.

## Q050-298 Binaural monitoring for surround masters

**P2 · L08 mixer, metering, channel strip · feature · L**

Goal: Users can monitor surround masters over headphones through an optional HRTF/SOFA binaural path.

Why: This medium-demand monitor gap is reported by 1 DAW; surround WAV export exists but binaural monitoring does not.

Acceptance:
- A surround master can enable/disable binaural monitoring and select a supported HRTF/SOFA profile through UI and agent state.
- A known multichannel test signal produces stereo headphone output with verified channel-to-ear routing.
- The monitor path is realtime safe and leaves exported multichannel files unchanged.
Items:
  - `R-mixing-30` Full surround and Dolby Atmos mixing and delivery - partial (mixer.channel_format / surround_pan (beds up to 7.1); WAV surround export; docs/KNOWN-LIMITATIONS.md:2394-2406; no binaural monitoring); complete: Surround beds and export exist; Atmos objects and binaural monitoring don't.

## Q050-299 Selectable PDC modes and transport control

**P2 · L08 mixer, metering, channel strip · completion · S**

Goal: Users can enable or disable PDC, constrain its maximum delay, and inspect the active mode from the transport bar.

Why: A medium-demand optional-PDC request is reported by 1 DAW; existing latency override and feedback-loop controls do not provide a global mode.

Acceptance:
- pdc.set_mode sets on, off, and constrain-below-N-frames modes, and pdc.report returns the effective mode and limit.
- The transport-bar toggle reflects command changes and its UI changes update the same registered command.
- Audio tests verify alignment when enabled, bypass when off, and the stated maximum under a constraint.
Items:
  - `R-mixing-35` Optional plugin delay compensation - partial (plugin.options latency_override (per device); feedback.enable suspends PDC only for loops; pdc.report is read-only); complete: PDC can be overridden per device, but there's no global off or 'constrain' switch for low-latency tracking.

## Q050-300 Offline stem EQ proposal for a stereo master

**P2 · L08 mixer, metering, channel strip · completion · L**

Goal: Users can separate a master render into four source groups, EQ each stem, audition the resummed result, and accept or undo the proposal.

Why: A medium-demand mastering request is reported by 1 DAW; current stem separation writes files rather than returning editable stems to mastering.

Acceptance:
- An offline fixture produces vocals, bass, drums, and other stems and applies independent EQ settings before resumming.
- The result report identifies the processing and measured change, with an auditionable proposal that can be accepted or undone.
- Tests confirm processing runs outside the realtime audio callback and cancellation preserves the original master.
Items:
  - `R-mixing-40` Stem EQ can rebalance four source groups on a stereo master - partial (stem.job_start / stem.job_result (offline 4-stem separation to files); docs/KNOWN-LIMITATIONS.md:1446-1450 (source is a file; results are files, not tracks)); complete: Separation exists offline, but no EQ works on separated stems of a master.

## Q050-301 Alphabetical auxiliary-bus picker

**P2 · L08 mixer, metering, channel strip · completion · S**

Goal: Users can find auxiliary return buses alphabetically and switch to mixer order when working by channel layout.

Why: A medium-demand picker request is reported by 1 DAW; current choices follow mixer order only.

Acceptance:
- The channel picker lists return buses alphabetically by default and offers a mixer-order option.
- Type-ahead selects the same target in either order, with a UI test covering renamed and duplicate-prefix buses.
Items:
  - `R-mixing-55` Sort auxiliary send buses - partial (src/gui/CommandFormFields.cpp:132-139 (channel choices listed in mixer order), :182-184 (editable picker with type-ahead)); complete: Send targets can be typed but are listed in mixer order, not alphabetically.

## Q050-302 Inspector focus channel strip

**P2 · L08 mixer, metering, channel strip · completion · M**

Goal: Users can use the selected track's meter, sends, and inserts from a touch-sized focus strip in the Inspector.

Why: A medium-demand focus-strip request is reported by 1 DAW; the current Inspector track section exposes only basic level/pan/channel controls.

Acceptance:
- The Inspector focus section shows the selected track's meter, sends, inserts, and existing mute/solo/volume/pan controls.
- Controls are touch-sized and remain usable at supported display scaling; state changes use the same commands as the mixer.
- A UI test changes selection and confirms the focus strip follows and reads back the selected channel.
Items:
  - `R-ui-38` Focus Channel and touch-oriented mixer - partial (src/gui/shell/InspectorPanel.cpp:322-349 (mute, solo, volume, pan, channel only); src/gui/ArrangerAids.cpp:111-121 (touch: pinch zoom only)); complete: The Inspector is a small focus strip without meter, sends or EQ, and the mixer has no touch handling.

## Q050-303 Bulk sidechain routing from selected strips

**P2 · L09 routing, buses, sends, sidechain, I/O · completion · S**

Goal: Create one sidechain send from a source to several selected receiver strips and undo the whole operation as one edit.

Why: mixer.sidechain_to currently accepts one receiver although plug-in sidechain pins are mapped automatically; demand is medium from 1 DAW.

Acceptance:
- mixer.sidechain_to accepts multiple receivers and reports each created route; one control.undo removes the complete batch.
- A Sidechain from... action on selected mixer strips lists compatible receivers and leaves their sidechain inputs visible in mixer.get_state.
Items:
  - `R-mixing-29` Automatic sidechain pin connection - partial (plugins/Vst3Effect/Vst3BusMap.cpp:44-47 and plugins/ClapEffect/ClapEffect.cpp:28,183-194 (aux input buses mapped to the sidechain automatically); mixer.sidechain_to takes one receiver); complete: Plug-in sidechain pins are mapped automatically, but each receiver needs its own sidechain_to call.

## Q050-304 Latency-align unconnected plug-in channels

**P2 · L09 routing, buses, sends, sidechain, I/O · completion · M**

Goal: Keep channels that pass around a partially connected plug-in aligned with channels processed by its reported latency.

Why: AudioPortsModel and its public pin API have no latency handling for through channels; demand is medium from 1 DAW.

Acceptance:
- A test plug-in reports a fixed nonzero latency while only some pins are connected; an impulse on a through channel arrives aligned with the processed path.
- port.set_pin changes and pdc.report read-back show the same compensated latency after save and reload.
Items:
  - `R-mixing-39` Sidechain and flexible pin routing - partial (port.set_pin / port.get_state; mixer.sidechain_to; pdc.report; no latency handling in src/core/AudioPortsModel.cpp or include/AudioPorts*.h); complete: Pin routing and sidechains exist, but channels routed past a latent device aren't delayed to match.

## Q050-305 Verify and synchronize live pin edits

**P2 · L09 routing, buses, sends, sidechain, I/O · fix · S**

Goal: Change audio pins during playback without a data race, stale routing cache, or wrong channel output.

Why: Matrix::setPin writes shared pin and routing-cache state from control and GUI callers without visible engine synchronization; the race is unreproduced, with medium demand from 1 DAW.

Acceptance:
- A stress test edits pins and channel counts while a deterministic audio render runs, checking cache and output consistency.
- Run the targeted test under ThreadSanitizer; either prove the candidate race and make it pass after synchronization, or record a measured refutation without an unnecessary code change.
Items:
  - `R-mixing-13` Audio port edits race with audio routing - unknown (src/core/AudioPortsModel.cpp:374-387 (Matrix::setPin writes pins and caches with no engine guard); src/core/ControlCommandsPorts.cpp:275 and src/gui/PinConnector.cpp:473 call it from the control and GUI threads); fix: Pin edits write matrices the audio thread reads with no synchronisation; the race hasn't been reproduced.

## Q050-306 Make hardware ping cancellation reset-safe

**P2 · L09 routing, buses, sends, sidechain, I/O · fix · S**

Goal: Cancel an insert latency measurement without a reset restarting it or sending an unexpected calibration click.

Why: HardwareInsert ping transitions use plain stores and have no cancel verb; the audible-click candidate is unreproduced, with medium demand from 1 DAW.

Acceptance:
- A state-transition test cancels while the ping is waiting and races cancellation against a device/project reset; the ping stays idle and emits no new click.
- A registered mixer.hardware_insert_ping_cancel command and the mixer UI action report the final state through mixer.hardware_get.
Items:
  - `R-performance-33` Hardware ping cancellation safety - unknown (src/core/HardwareInsert.cpp:100-116 (Requested->Waiting is a plain store on the audio thread), :227 (load resets to Idle with a plain store); no cancel verb); fix: A reset to Idle written while the audio thread is moving Requested to Waiting can be overwritten, leaving a ping running; not reproduced.

## Q050-307 Stage 2: Multichannel recording and surround completion

**P2 · L09 routing, buses, sends, sidechain, I/O · completion · L**

Goal: Record, process, meter, export and stem all surround channels, with PDC keeping channels beyond L/R aligned.

Why: KNOWN-LIMITATIONS.md:2401-06 says stems, devices, meters, PDC, cue mixes and the recorder remain L/R-only; demand is medium from 1 DAW, so the XL request is staged after routing and I/O mapping.

Depends on: N-channel routing graph and per-channel sends; Named multichannel I/O and record mapping
Acceptance:
- A multichannel recording test captures distinct 5.1 or discrete-N signals beyond L/R and verifies each channel through playback and device processing.
- Meter and pdc.report state includes extra channels; render.stems and per-track export preserve the configured channel count and aligned impulses.
- A save/load test preserves the channel layout, record routes, device state and stem output.
Items:
  - `R-recording-49` Multichannel audio and surround - partial (plans/TRACKER-050.md:424 (MX-060 surround done); docs/KNOWN-LIMITATIONS.md:2394-2406 (devices, inserts, meters, cue mixes and the recorder carry L/R only)); complete: Surround channels, panner and WAV export exist, but devices, meters, the recorder and stems stay stereo.

## Q050-308 Correct MIDI Clock menu documentation

**P2 · L09 routing, buses, sends, sidechain, I/O · docs · S**

Goal: Find the MIDI Clock source-port and drift-bound forms in the documented Sync menu workflow.

Why: The Sync menu already opens forms for source_port and drift_bound_ms while KNOWN-LIMITATIONS.md:918 still says socket-only; demand is medium from 1 DAW.

Acceptance:
- KNOWN-LIMITATIONS.md describes the Edit > MIDI Clock forms for selecting a port and drift bound and removes the stale socket-only claim.
- A documentation search finds no remaining statement that MIDI Clock source-port or drift-bound selection is socket-only.
Items:
  - `R-workflow-117` MIDI clock input selection is socket-only - have (src/gui/LibraryCommandMenus.cpp:207-208 (Sync > Send/Follow MIDI Clock... open the schema form with source_port and drift_bound_ms); docs/KNOWN-LIMITATIONS.md:918 still says socket-only); docs: The clock port and drift bound can now be set from the Sync menu's forms, so the KNOWN-LIMITATIONS.md line is stale.

## Q050-309 Modular instrument performance panel

**P2 · L10 devices, instruments, effects, plug-in hosting · feature · L**

Goal: Users can expose chosen graph parameters on a reusable instrument front panel.

Why: The existing graph has no user-designed instrument panel; this medium-demand request is from 1 DAW.

Depends on: Modular instrument graph and starter modules
Acceptance:
- Create, label, reorder, and persist user-facing controls mapped to graph parameters.
- Verify panel values through the GUI and agent state read-back.
Items:
  - `R-instruments-37` Reusable containers and custom Grid interfaces - partial (patcher.set_wiring; rack.*; rack.macro_*; src/core/ControlCommandsPatcher.cpp:53-54 says there is no patcher canvas or addable node set.); complete: Racks and effect-chain wiring exist, but reusable nested instruments and a custom patch control panel do not.

## Q050-310 Versioned native plug-in SDK

**P2 · L10 devices, instruments, effects, plug-in hosting · feature · L**

Goal: Developers can build an external native plug-in against a versioned Zene SDK contract.

Why: The internal module loader has no public SDK package; demand is medium and the request spans 1 DAW.

Acceptance:
- Build and load an external example module without private in-tree headers.
- Document ABI version negotiation and test a compatible and incompatible module.
Items:
  - `R-instruments-41` Extensible native plugin API - partial (src/core/PluginFactory.cpp:166-183 dynamically loads modules; plugin.list and plugin.load cover LADSPA, LV2, VST3 and CLAP; no public SDK package found.); complete: Dynamic native modules and standard plug-in formats exist, but a supported public native SDK and external developer kit are missing.

## Q050-311 Built-in synth pressure and timbre

**P2 · L10 devices, instruments, effects, plug-in hosting · feature · M**

Goal: Users can play MPE pressure and timbre expression through the built-in synthesizers.

Why: device.mpe_set exists but built-in synths ignore pressure and timbre; demand is medium from 1 DAW.

Acceptance:
- Send pressure and timbre through an MPE fixture and measure distinct modulation in a rendered note.
- Verify expression state and backwards-compatible project loading.
Items:
  - `R-instruments-39` Built-in synths do not consume MPE pressure or timbre - partial (device.mpe_get_state; docs/KNOWN-LIMITATIONS.md:655-670 says built-in synths do not consume pressure or timbre.); complete: MPE pressure and timbre reach hosted instruments and MIDI output, but built-in synths still do not consume them.

## Q050-312 Deactivate tracks and devices

**P2 · L10 devices, instruments, effects, plug-in hosting · feature · M**

Goal: Users can unload inactive device instances while retaining their state and showing the track as inactive.

Why: Visibility and bypass exist but no persisted unload/deactivate state exists; demand is medium from 1 DAW.

Acceptance:
- Deactivate and reactivate an instrument and effect, verifying saved state and audio return after reload.
- Show the inactive state in the UI and agent read-back.
Items:
  - `R-workflow-150` Hide and deactivate tracks/devices - partial (track.set_visible (src/gui/CommandListsCustom.cpp:60); src/gui/PluginNativeEditor.cpp:102 (bypass); src/gui/TrackRenderMenu.cpp:74 (freeze); rg deactivat over track code found nothing); complete: Tracks can be hidden, devices bypassed and tracks frozen, but nothing deactivates a track or device to unload it and free CPU while keeping its settings.

## Q050-313 Zyn MIDI mutex latency fix

**P2 · L10 devices, instruments, effects, plug-in hosting · fix · S**

Goal: Users can send MIDI to Zyn without waiting behind preset loading.

Why: Zyn MIDI handling blocks on the same mutex held by preset loading; demand is medium from 1 DAW.

Acceptance:
- Load a slow preset while sending notes and verify MIDI submission does not block on the plug-in mutex.
- Confirm queued events are delivered or explicitly counted as dropped and live processing remains nonblocking.
Items:
  - `R-performance-62` Zyn processing mutex behavior - partial (plugins/ZynAddSubFx/ZynAddSubFx.cpp:386-399 (handleMidiEvent blocks on m_pluginMutex), :326-328 (preset load holds it), :370-373 (process uses try-lock)); fix: Note events reach Zyn through a blocking lock, so a note-on on the audio path waits while a preset loads.

## Q050-314 WASM offline-render memory pointer fix

**P2 · L10 devices, instruments, effects, plug-in hosting · fix · S**

Goal: Users can render a WASM effect that grows its memory during a processing call without a stale output pointer.

Why: Offline rendering caches memoryData before callProcess and uses it afterward; the ABI growth behavior remains unverified at medium demand.

Acceptance:
- Run a fixture that grows WASM memory during callProcess and verify the output plane is read from the refreshed memory view.
- Run under ASan and verify invalid growth returns a typed render failure.
Items:
  - `R-effects-9` WASM offline-render memory pointer safety - unknown (src/wasm/WasmOfflineRender.cpp:100 takes memoryData() once per block and reads output at :136 after callProcess; docs/WASM-EFFECT-ABI.md:185-189 leaves growth during a call UNKNOWN); fix: The offline renderer reads module memory through a pointer taken before the call, which could be stale if the module grows memory.

## Q050-315 CV and gate output device

**P2 · L10 devices, instruments, effects, plug-in hosting · feature · L**

Goal: Users can send note pitch, gate, and modulation to selected hardware outputs.

Why: Hardware insert/output commands exist but no CV/gate source device was found; demand is medium from 1 DAW.

Acceptance:
- Route pitch, gate, and one modulation lane to chosen interface outputs and measure output timing.
- Expose device selection and routing through agent commands and document supported drivers.
Items:
  - `R-platform-45` Hardware-centric and Linux-native workflow - partial (README.md:22-25 (native Linux); mixer.hardware_insert, mixer.hardware_output; rg for CV output over src/ found only LV2 CV ports (src/core/lv2/Lv2Ports.cpp:152)); complete: Zene is Linux-native with hardware inserts and outputs, but it cannot send CV/gate to DC-coupled outputs for modular hardware.

## Q050-316 Package an optional tagged factory loop library

**P2 · L11 browser, samples, presets, library · feature · L**

Goal: Users can install and browse a curated local loop pack with useful tags and categories.

Why: This is stage one of R-instruments-44's XL request for tagged sounds and starter projects; its evidence reports medium demand from one DAW.

Depends on: Filter browser results by category
Acceptance:
- A locally available optional pack contains curated loops and one-shots with usable genre, tempo, and key metadata where known.
- The browser finds the pack by category, tag, tempo, and key without contacting an online content service.
- A clean-install check can omit the optional pack, and the included manifest records provenance and licenses.
Items:
  - `R-instruments-44` Bundled sound library refresh - partial (browser.roots reports factory sample and preset roots; plugins/ZynAddSubFx/zynaddsubfx/instruments/banks/ contains bundled presets.); complete: Factory samples and instruments ship, but the requested large curated loop library and starter-song collection are absent.

## Q050-317 Sequence songs in a mastering project document

**P2 · L12 projects, files, import/export, interchange, recovery · feature · L**

Goal: Arrange songs or files with per-song processing, gaps, and fades and render individual songs or the full release.

Why: The row reports medium demand from one DAW and an existing mastering document type without the release-sequencing workflow.

Acceptance:
- Create a mastering document with two songs, set processing and a gap/fade, and verify individual-song and full-release renders.
- Save/reopen the document and verify order, processing, gaps, and fades are unchanged.
- Create and edit the mastering document through project.new_document and verify its GUI reflects the same state.
Items:
  - `R-workflow-152` Integrated mastering Project page - partial (project.new_document type mastering; src/gui/shell/ShellCommandMenus.cpp:524 (Auto-Master...); src/gui/LibraryCommandMenus.cpp:233 (subprojects)); complete: There's a mastering document type and auto-mastering, but no release page that lays out several songs on one timeline with gaps and track export.

## Q050-318 Starter workflow library

**P2 · L13 customisation, shortcuts, themes, layout · feature · M**

Goal: Discover and run a curated set of common workflow macros from the Macros menu.

Why: The medium-demand request cites Pro Tools' macro library, while Zene currently ships no workflow library in data/.

Depends on: Editable, importable, and exportable workflows
Acceptance:
- A fresh profile shows the starter library in Macros; select and run each workflow against a test project.
- Remove or reset the library and confirm user-saved workflows remain intact.
Items:
  - `R-workflow-153` Integrated SoundFlow automation panel - partial (src/gui/MacroMenu.cpp:104-130 (Macros menu, record and run); data/ holds no workflows or macros directory); complete: Users can record and run workflows, but Zene ships no ready-made macro library.

## Q050-319 Beginner and expert interface presets

**P2 · L13 customisation, shortcuts, themes, layout · feature · M**

Goal: Switch between beginner and expert views that show the appropriate options and panels.

Why: The medium-demand request finds settings presets/search and one appearance mode, but no beginner/expert profiles that hide advanced options and panels.

Acceptance:
- Apply beginner and expert presets and verify advanced options and panels hide or show as expected.
- Switch modes and confirm hidden settings retain their saved values.
Items:
  - `R-ui-33` Beginner, normal and expert interface modes - partial (settings.preset_save/apply; settings.search; window.appearance accessible mode); complete: Settings presets and search exist, but there is no beginner/expert disclosure mode.

## Q050-320 Announce launched and stopped clips and scenes

**P2 · L14 help, onboarding, accessibility · feature · S**

Goal: Hear which launcher cells and scenes became active, including launches made from a bound controller.

Why: The item has medium demand and confirms session state exists, but the current announcement implementation has no launch or scene feedback.

Acceptance:
- Launch and stop cells and scenes from the UI and a bound controller; verify one announcement names the affected cell or scene and its state.
- Verify routine announcements honor view.announcements levels and do not fire for launches that are refused or not yet active.
Items:
  - `R-performance-53` Spoken clip and scene feedback - partial (view.announcements (errors, refusals, recording, save/open, finished jobs); session.get_state; no launch/scene announcement found); complete: Screen-reader announcements cover errors, recording and jobs, but not which clips and scenes are launched or stopped.

## Q050-321 Touch keyboard and drum-pad surfaces

**P2 · L14 help, onboarding, accessibility · feature · L**

Goal: Play a scalable on-screen keyboard and drum pads with multiple simultaneous touches.

Why: The request is medium demand for an accessible touch instrument, while current evidence finds no multi-touch keyboard or drum-pad surface.

Depends on: Touch-first workspace layout
Acceptance:
- Play two or more notes simultaneously on the touch keyboard and trigger independent drum pads.
- Verify controls scale, expose keyboard focus/accessibility information, and support the documented record start/stop gesture.
- Add touch-event tests for concurrent touches, note release, pad triggering, and focus.
Items:
  - `R-instruments-35` Accessible touch instrument surfaces - partial (window.accessibility_report; docs/KNOWN-LIMITATIONS.md:2092-2118; searched src/gui/ and include/ for iPad or Direct Touch surfaces.); complete: Screen-reader and accessible-mode support exist, but there is no touch-first keyboard or drum-pad performance surface.

## Q050-322 Open track and ruler context menus from the keyboard

**P2 · L14 help, onboarding, accessibility · feature · M**

Goal: Reach track-header and ruler actions without using a mouse.

Why: This is a medium-demand keyboard workflow; lists and clips support keyboard context menus, but track headers and the ruler do not.

Acceptance:
- Focus a track header or ruler position and open the same context menu with Menu or Shift+F10.
- Verify menu actions match the corresponding right-click menu and remain keyboard navigable.
- Add a regression test for both surfaces and their focus behavior.
Items:
  - `R-workflow-154` Keyboard access to track and item context menus - partial (src/gui/ListTable.cpp:234 (Menu or Shift+F10 on lists); src/gui/clips/ClipView.cpp:145,1083 (clips take focus and have a context menu); src/gui/tracks/TrackLabelButton.cpp:51 (NoFocus)); complete: Lists and focused clips open their menus from the keyboard, but track headers and the ruler can't take focus, so their context menus are mouse-only.

## Q050-323 Task-indexed quick-start and area reference

**P2 · L14 help, onboarding, accessibility · docs · L**

Goal: Find a practical task guide and a fuller reference for each major Zene Studio area.

Why: The manual is rated medium demand, and current evidence describes an 11-section manual without a task-indexed guide.

Acceptance:
- Add a searchable quick-start organized around common tasks and a reference section for each major area.
- Verify the in-app manual navigation reaches every task and reference section and links to relevant commands and tours.
- Review task steps against the merged UI and command surface before shipping the manual.
Items:
  - `R-workflow-139` Comprehensive current manual and quick-start guides - partial (data/manual/index.html (about 33 KB, 11 h2 sections); app.help reference; app.tours); complete: Zene ships an in-app manual, a command reference and tours, but the manual is short, with no task-based quick-start of the kind REAPER's guides offer.

## Q050-324 Add an accessibility feedback route

**P2 · L14 help, onboarding, accessibility · docs · S**

Goal: Find a clear way to report an accessibility blocker from the manual and project contribution pages.

Why: The item has medium demand and confirms no accessibility-report route in README.md or CONTRIBUTING.md.

Acceptance:
- Add the same accessibility feedback path to README, CONTRIBUTING, and the in-app Access topic.
- Verify the route includes a prompt for affected workflow, assistive technology, and steps to reproduce, without requiring personal medical information.
Items:
  - `R-accessibility-18` Accessibility improvements informed by user feedback - partial (window.accessibility_report; no accessibility-report route in README.md/CONTRIBUTING.md (rg accessib: no match)); docs: The audit tooling exists, but users have no documented way to report accessibility problems.

## Q050-325 Correct stale socket-only and no-interface claims

**P2 · L14 help, onboarding, accessibility · docs · M**

Goal: Read accurate documentation for features that now have GUI controls as well as command access.

Why: The item has medium demand and cites multiple KNOWN-LIMITATIONS entries contradicted by GUI code, with the planned docs pass still open.

Acceptance:
- Review every socket-only or no-interface claim in KNOWN-LIMITATIONS.md against the merged GUI and command registry.
- Correct stale claims and retain claims that are still true, with links to the current UI entry point.
- Run a focused text search and confirm every remaining claim has been checked against source.
Items:
  - `R-workflow-240` SWS documentation lags implementation - partial (docs/KNOWN-LIMITATIONS.md:114, :262, :718-720 and :918 still say socket-only for features now in the GUI; plans/TRACKER-050.md marks the docs pass as to do); docs: Zene has the same problem: KNOWN-LIMITATIONS.md lags the code in several places.

## Q050-326 Show safe-start recovery and Windows crash reports

**P2 · L15 platform: performance, stability, Linux audio, install, hardware controllers · feature · L**

Goal: After a crash, users can choose normal or safe startup in the GUI and Windows can produce a usable crash dump.

Why: The engine exposes safe-start commands but has no startup GUI offer, and the Windows reporter is a no-op; demand is low or unreported.

Acceptance:
- A startup test with a crash marker shows the recovery offer; choosing normal startup acknowledges it and loads plug-ins, while safe startup skips third-party plug-ins.
- A Windows crash-report test produces a minidump that the configured debugger can open.
- The startup and crash-report docs match the tested GUI and platform behavior; both GUI actions have registered commands and socket coverage.
Items:
  - `B-WF-123` Safe start after a crash - partial (safestart.* ; src/gui/shell/ShellCommandMenus.cpp:530 (Safe Start... list); src/core/main.cpp:340-358 (offer printed on stderr only); docs/KNOWN-LIMITATIONS.md:1141 (says nothing in src/gui shows it - now stale)); complete: A Safe Start list now exists in a menu, but the post-crash offer is still only printed on stderr and the limitations page says there is no interface at all.
  - `R-ui-68` Safe-start recovery has no GUI offer - partial (src/gui/CommandLists.cpp:176-184 (Safe Start list: Acknowledge, Skip/Load Plug-ins, Clear); src/core/main.cpp:349-358 (offer printed on stderr only); src/core/CrashReporterWindows.cpp:3 (no-op)); complete: Safe start is reachable from a list but nothing offers it at start-up, and Windows crash reports are a no-op.

## Q050-327 Freeze selected tracks as one parallel task

**P2 · L15 platform: performance, stability, Linux audio, install, hardware controllers · feature · M**

Goal: Users can freeze several selected tracks concurrently and undo the operation as one step.

Why: The queue shows freeze.track accepts one track and rates batch freeze medium demand.

Depends on: Move long-running jobs off the interface thread
Acceptance:
- A multi-track test runs freezes concurrently, reports one task with progress/cancel and creates one undo entry.
- Cancel and partial failure restore every selected track to its original state.
Items:
  - `R-performance-59` Parallel batch freeze - partial (freeze.track takes one 'track'; src/gui/TrackRenderMenu.cpp:73-74 (per-track menu item)); complete: Freeze works one track at a time; there is no multi-track freeze, let alone a concurrent one.

## Q050-328 Add PortAudio multichannel and verify Linux device paths

**P2 · L15 platform: performance, stability, Linux audio, install, hardware controllers · feature · L**

Goal: Users can route distinct PortAudio device channels and use verified multichannel JACK/ALSA I/O.

Why: The queue marks existing JACK/ALSA multichannel hardware behavior unverified and PortAudio multichannel absent, with medium demand.

Acceptance:
- A PortAudio multichannel test maps each device channel to its own track on supported Windows/macOS builds.
- A real multichannel interface run verifies JACK and ALSA input/output and records hardware, channel count and result.
- Device channel mapping is readable and configurable through a registered command and UI.
Items:
  - `R-interop-38` Multichannel hardware audio output - partial (docs/KNOWN-LIMITATIONS.md:2307-2311 (JACK/ALSA multichannel written but never run on hardware; Pulse/SDL/OSS/sndio/SoundIo stereo; PortAudio not compiled); audio.io_list / audio.io_setup); complete: Multichannel I/O exists only for JACK and ALSA, has never run on a real multichannel interface, and there's none on Windows or macOS.

## Q050-329 Add multichannel SoundIo and SDL routing

**P2 · L15 platform: performance, stability, Linux audio, install, hardware controllers · feature · L**

Goal: Users can route more than stereo through SoundIo and SDL devices on supported systems.

Why: The queue identifies SoundIo and SDL as stereo-only and rates broader backend support medium demand; this follows the shared multichannel contract.

Depends on: Add PortAudio multichannel and verify Linux device paths
Acceptance:
- SoundIo and SDL integration tests prove channels beyond 1/2 reach distinct mixer routes.
- The supported build matrix and actual hardware verification are recorded; unsupported configurations refuse clearly.
- The UI and audio.io setup state expose each discovered channel.
Items:
  - `R-platform-39` Multichannel backend support is uneven and unverified - partial (docs/KNOWN-LIMITATIONS.md:2307-2311 (JACK/ALSA multichannel unrun; PulseAudio, SDL, OSS, sndio, SoundIo stereo; PortAudio not compiled)); complete: Multichannel output exists only for JACK and ALSA, untested on hardware, and the other backends are stereo.

## Q050-330 Add Windows ASIO device selection

**P2 · L15 platform: performance, stability, Linux audio, install, hardware controllers · feature · L**

Goal: Windows users can select an ASIO or WASAPI driver before device ports are created.

Why: The queue finds no ASIO/WASAPI backend and PortAudio is not compiled in the current build; demand is medium.

Acceptance:
- Windows CI builds the chosen low-latency backend and enumerates its drivers before port setup.
- A device-selection test starts and stops an ASIO or WASAPI device and reports its ports and latency.
- The Windows audio setup guide identifies supported driver choices and failure reasons.
Items:
  - `R-platform-65` ASIO device selection and startup - missing (rg asio/wasapi over src/core/audio and CMakeLists.txt found no ASIO backend; docs/KNOWN-LIMITATIONS.md:2311 (PortAudio not compiled); audio.device_list; src/core/AudioEngine.cpp:1151-1170); implement: Windows builds have no ASIO driver path, so low-latency driver selection is unavailable there.

## Q050-331 Persist controller bindings with presets

**P2 · L15 platform: performance, stability, Linux audio, install, hardware controllers · feature · M**

Goal: Users can save a track or effect-chain preset and restore its controller bindings when loading it.

Why: Preset save currently stores device state but omits controller bindings; demand is medium.

Acceptance:
- Track and chain preset round-trip tests preserve valid bindings and rebind them to the loaded devices.
- Missing targets are reported and skipped without creating phantom mappings.
- Preset UI and commands show which bindings were restored.
Items:
  - `R-workflow-177` Store controllers with instruments and effect chains - partial (controller.template_save / template_apply (src/gui/CommandLists.cpp:227); chain.save stores device state only; track.preset_save has no MIDI bindings); complete: Mapping templates exist, but controller assignments aren't saved inside instrument, track or chain presets.

## Q050-332 Support up to 50 collaboration peers

**P2 · L16 collaboration and sharing · feature · L**

Goal: A host can serve a group of up to 50 collaborators with bounded resource use and clear refusal or backpressure beyond the limit.

Why: Demand is medium across 1 DAW, and include/CollabHost.h:56-60 currently caps a host session at eight peers.

Depends on: Label collaboration as experimental and document its limits
Acceptance:
- A load test connects 50 loopback peers and verifies command delivery, bounded queues, and host responsiveness.
- A 51st peer is refused with a typed limit result before unbounded session state is allocated.
- The docs state the supported limit and the backpressure behavior.
Items:
  - `R-collaboration-21` Large collaborator groups - partial (include/CollabHost.h:56-60); complete: The host accepts at most eight peers, below the larger group sizes requested by some users.

## Q050-333 Add a self-hosted collaboration backend

**P2 · L16 collaboration and sharing · feature · L**

Goal: An organization can keep shared project state and referenced media on its own Nextcloud/WebDAV-compatible service and let collaborators sync through that endpoint.

Why: Demand is medium across 1 DAW, and collaboration is currently direct peer-to-peer with neither transferred samples nor reconnect support (KNOWN-LIMITATIONS.md:2379-2392).

Depends on: Transfer and hash-check project media between peers; Resume media and collaboration state after disconnect
Acceptance:
- An integration test configures an organization-controlled endpoint, shares a project, and verifies authenticated state and media sync without a public Zene service.
- Conflicting edits are detected and presented for resolution rather than silently overwriting the shared revision.
- Invalid credentials, offline service, and concurrent writes produce recoverable typed outcomes and are covered in the UI and command docs.
Items:
  - `R-hosting-5` Self-hosted collaborative projects - partial (collab.host, collab.invite, collab.join; docs/KNOWN-LIMITATIONS.md:2379-2392 says collaboration is peer-to-peer, experimental, off by default, and does not transfer samples); complete: Users can host a peer-to-peer session, but collaboration remains experimental and project assets are not transferred.

## Q050-334 Public live-session TypeScript SDK

**P2 · L17 agent control surface and AI assistance · feature · L**

Goal: A third-party TypeScript client can read and edit an open Zene session and receive live project-change notifications through a documented API.

Why: Demand is medium across 1 DAW, and the existing control surface and MCP bridge expose live commands but the command snapshot has no subscription or notification command and no public TypeScript client.

Acceptance:
- Add a control-protocol subscription/event path with ordered change notifications, bounded delivery, unsubscribe, and a defined resync path after disconnect or overflow.
- Provide a versioned TypeScript client package, generated or maintained public types, a usage example, and an integration test that connects, edits a project, receives the corresponding event, and unsubscribes.
Items:
  - `R-ai-19` External live-session SDK - partial (src/core/ControlRegistry.cpp:267-274; tools/mcp-zene-control/zene_control/registry.py:1-8; no subscribe/notification command in commands_snapshot.json); complete: Third parties can read and modify a live session over the socket and MCP, but there is no change-event subscription and no published client SDK.

## Q050-335 Add starter projects, patterns, and tutorials

**P2 · L11 browser, samples, presets, library · feature · M**

Goal: New users can open starter songs, reuse drum patterns, and follow short bundled tutorials.

Why: This completes the starter-project half of R-instruments-44's XL request and R-hosting-14's low-demand tutorial request, each reported by one DAW.

Depends on: Package an optional tagged factory loop library
Acceptance:
- The bundled starter project opens with its referenced media resolved and includes reusable drum-pattern or MIDI content.
- Short tutorials are reachable from the application and use the shipped sample and project assets.
- All bundled content has a checked provenance/license manifest and works without network access.
Items:
  - `R-hosting-14` Royalty-free drum-pattern and tutorial library - partial (data/samples/ contains 1,005 bundled sample files; data/tours/ includes first-beat, mixing and recording tours); complete: Zene bundles substantial drum and instrument sample content and several tours, but no curated drum-pattern/song tutorial library.

## Q050-336 Correct ALSA capture support documentation

**P2 · L15 platform: performance, stability, Linux audio, install, hardware controllers · docs · S**

Goal: Linux users see that ALSA capture exists and understand the capture-channel and route limits.

Why: The docs deny ALSA capture despite the implemented capture path; this is a direct Linux recording promise-gap, with low demand.

Acceptance:
- KNOWN-LIMITATIONS no longer says ALSA has no capture path; it describes the implemented ALSA capture thread and verified limits.
- A documentation check ties capture claims to AudioAlsa and recording command behavior; JACK capture status is stated separately.
Items:
  - `R-recording-62` Maschine+ external audio interface compatibility still needs care - partial (src/core/audio/AudioAlsa.cpp:42-62 (ALSA capture thread exists) vs docs/KNOWN-LIMITATIONS.md:671-673 (still says ALSA has no capture path)); docs: ALSA capture is implemented but KNOWN-LIMITATIONS still says it is absent, and interface compatibility is undocumented.

## Q050-337 Send MCU strip LCD feedback

**P2 · L15 platform: performance, stability, Linux audio, install, hardware controllers · completion · M**

Goal: MCU users see track names and current values on the controller’s strip displays.

Why: MCU connection and state commands exist, but the queue identifies missing LCD SysEx feedback; demand and DAW count are unreported.

Acceptance:
- A MIDI-output test verifies the expected MCU LCD SysEx for track names and changing values.
- A connected-controller smoke test confirms display updates on selection and value changes; docs state whether HUI is supported.
Items:
  - `B-WF-126` Native control-surface support - partial (surface.mcu_connect/bank/state ('The strip LCD (SysEx) is not sent'); remote.connect; controller.*); complete: Mackie Control works for faders, V-pots, buttons and motor faders, but the strip LCD is not driven and HUI is absent.

## Q050-338 Persistent gang mode for selected channels

**P2 · L08 mixer, metering, channel strip · completion · M**

Goal: Users can link selected mixer channels so fader and pan changes move together until gang mode is disabled.

Why: The partial fader/pan gang request spans 3 DAWs, with no demand score recorded.

Acceptance:
- A UI and agent command enable, disable, and report persistent gang mode.
- Changing one selected channel's fader or pan updates the other ganged selections but leaves unselected channels unchanged.
- A regression test covers save/load and undo/redo of gang edits.
Items:
  - `B-MX-028` Gang selected channels - partial (src/core/ControlCommandsMixerTools.cpp:204-223,518-521; src/gui/shell/ShellCommandMenus.cpp:418-425); complete: Several channels can receive the same one-time fader and pan adjustment, but persistent linked control is absent.

## Q050-339 Track delay for audio tracks and mixer channels

**P2 · L08 mixer, metering, channel strip · completion · M**

Goal: Users can set and save track delay and manual latency offsets on audio tracks and mixer channels as well as instrument tracks.

Why: The partial delay/offset request spans 3 DAWs, with no demand score recorded.

Acceptance:
- Agent commands set and read back delay/offset for audio tracks, instrument tracks, and mixer channels.
- An impulse-render test verifies the requested sample offset on each supported target.
- Project save/load preserves the values and undo restores the prior offsets.
Items:
  - `B-MX-035` Track delay and manual latency offset - partial (src/core/ControlCommandsMixerTools.cpp:358-378,509-511; src/tracks/InstrumentTrack.cpp:975,1067); complete: Track delay shifts instrument notes, but there is no delay or manual latency offset for audio tracks and mixer channels.

## Q050-340 Input meters for capture channels and armed tracks

**P2 · L08 mixer, metering, channel strip · completion · M**

Goal: Users can see live level meters for individual capture channels and the inputs feeding record-armed tracks.

Why: Two DAWs request the missing per-input view beyond the current general input-level meter.

Acceptance:
- A capture test feeds different levels to two inputs and the UI shows the corresponding independent meters.
- Record-armed track meters follow their selected input and show silence when no signal is present.
- The meter state is readable by the agent surface and remains audio-thread safe.
Items:
  - `B-MX-134` Input level meters - partial (docs/KNOWN-LIMITATIONS.md:2487-2495; src/gui/MixerWindows.cpp:57-66; src/gui/InputLevelMeter.cpp); complete: Live input meters cover only the stereo engine bus and do not meter each capture channel or armed track.

## Q050-341 Crossfader slider in the mixer

**P2 · L08 mixer, metering, channel strip · completion · S**

Goal: Users can move the crossfader directly in the mixer and hear the existing crossfade behavior.

Why: Two DAWs are represented, and the existing crossfader has menu entries but no direct mixer slider.

Acceptance:
- A mixer/master-strip slider is bound to mixer.crossfader and updates its readable position while dragging.
- A render test verifies the expected A/B mix at both endpoints and the center position.
Items:
  - `B-MX-172` Crossfader - partial (mixer.crossfader (src/core/ControlCommandsMixerMeters.cpp:218); UI is menu entries only: src/gui/shell/ShellMenuEntries.cpp:121-124); complete: The crossfader engine and A/B side assignment exist, but the position is set through a menu dialog; there is no on-screen crossfader control.

## Q050-342 Build and cache local timbre embeddings

**P2 · L11 browser, samples, presets, library · completion · L**

Goal: The library index computes and persists bounded local timbre embeddings for analyzed audio files.

Why: The XL similar-sound request cites two DAWs, and the current index stores only hand-built numeric audio features, so embedding generation is the first bounded stage.

Acceptance:
- Reindexing a fixed audio fixture corpus creates deterministic embeddings on a worker thread and persists them in the local index.
- Unchanged files reuse cached embeddings; changed files are reanalyzed, and resource limits are covered by a regression check.
- Indexing requires no network access and does no embedding work on the audio thread.
Items:
  - `B-WF-020` Similar-sound search - partial (include/BrowserIndex.h:42-44, 75-79; src/core/ControlCommandsBrowserIndex.cpp:230-233); complete: Similar search ranks hand-measured length, loudness, brightness, noisiness, tempo and key rather than timbral embeddings.

## Q050-343 Document the sample generator menu route

**P2 · L08 mixer, metering, channel strip · docs · S**

Goal: Users and agents can find accurate help showing that sample generation is available from the track menu as well as the command surface.

Why: One DAW is represented, and the current help/snapshot incorrectly describe the existing track-menu action as socket-only.

Acceptance:
- The sample.generate help text names its track-menu route.
- The command snapshot documents the menu route and its registered command ID.
Items:
  - `B-MX-141` Test signal generator - have (src/gui/shell/ShellCommandMenus.cpp:285-288 exposes `sample.generate`; stale socket-only claim at src/core/ControlCommandsSample.cpp:183-191 and snapshot `sample.generate`); docs: A Sample Track menu reaches the test-signal generator, but the command description still says no interface reaches it.

## Q050-344 Record ARA and pitch-editing deferral

**P3 · L10 devices, instruments, effects, plug-in hosting · docs · S**

Goal: The 0.5.0 roadmap clearly marks ARA/Melodyne clip editing as deferred and leaves no implied support promise.

Why: Eleven overlapping ARA/Melodyne requests describe a missing host path, but the owner explicitly deferred pitch editing for 0.5.0.

Acceptance:
- Update the release plan and capability-gap page to name ARA2, Melodyne-style clip editing, track-wide ARA, and ARA processing-order extensions as deferred.
- Confirm no 0.5.0 implementation lane or public support claim remains for these candidates.
Items:
  - `R-editing-41` ARA and Melodyne integration - missing (search of src, include and plugins found no ARA or Melodyne integration; no ARA command found); implement: Zene has no ARA extension host or tight Melodyne-style audio-to-pitch editing integration.
  - `R-editing-112` ARA processing order and transcript-window refinements - missing (commands_snapshot.json: no ARA or transcription command (rg ARA|transcri)); implement: Zene has no ARA host at all, so processing order and transcript presentation have nothing to refine yet.
  - `R-editing-114` Expanded ARA2 workflow - missing (commands_snapshot.json: no ARA command; plugin hosts are CLAP/VST3/LV2 without ARA); implement: No ARA2 support, so ARA plug-ins cannot act as stretch modes or receive chord-track context.
  - `R-editing-115` ARA playback graph node - missing (commands_snapshot.json: no ARA command; no ARA node in src/core); implement: There is no ARA playback node in the engine.
  - `R-effects-18` Track-wide ARA processing - missing (commands_snapshot.json: no ARA command); implement: Track-wide ARA processing needs ARA hosting, which Zene does not have.
  - `R-effects-19` ARA support for in-context audio editing - missing (commands_snapshot.json: no ARA command); implement: No ARA support, so Melodyne-style in-context editing is not possible.
  - `R-effects-22` ARA audio-editing extensions - missing (commands_snapshot.json: no ARA command); implement: No ARA host, so ARA editing extensions cannot load.
  - `R-instruments-18` MIDI operations and ARA integrations - partial (note.edit, clip.controllers_set, interchange.smf_export; rg for ARA host classes in src/gui, src/core, include and host plug-in modules returned no matches; ARA source under plugins/CarlaBase is vendored); complete: MIDI editing and interchange are present, but Zene has no ARA host integration.
  - `R-instruments-30` Tonalic Essential guitar performance integration - missing (plugin.load supports hosted formats; rg for ARA host classes in src/gui, src/core, include and host plug-in modules returned no matches; ARA source under plugins/CarlaBase is vendored); implement: The requested Tonalic workflow depends on an ARA host path that is not present in Zene.
  - `R-recording-18` ARA 2 and integrated vocal editing - missing (rg -w ARA over src/ include/ plugins/Vst3*/ plugins/Clap*/ found nothing; commands_snapshot.json has no ARA command); implement: No ARA 2 hosting exists, so Melodyne-style plug-ins cannot read clips directly.
  - `R-workflow-118` Missing modern workflow pieces in Reason - partial (vca.* and folder tracks (grouping); device.mpe_set (MPE); video.attach; rg -w ARA over src include docs found nothing); complete: Grouping, MPE and video are in Zene, but ARA plug-in hosting isn't.

## Q050-345 WASM effect inputs: samples, sidechain, and MIDI

**P3 · L10 devices, instruments, effects, plug-in hosting · feature · L**

Goal: Scripted effects can receive a read-only sample, a separately routed sidechain, and timestamped MIDI events.

Why: Three low-demand requests from 1 DAW each identify missing inputs in the existing WASM audio-only ABI.

Acceptance:
- A fixture WASM effect reads the sample input, distinguishes the sidechain, and receives bounded MIDI events at the expected render frames.
- Invalid pointers, event overflow, and absent optional inputs return typed errors without changing the audio-thread allocation baseline.
Items:
  - `R-effects-40` Sidechain inputs for custom effects - partial (plugins/Vocoder/Vocoder.cpp:122-140 receives a sidechain buffer; docs/WASM-EFFECT-ABI.md:152-172 defines only the effect input/output planes); complete: Mixer sidechains and a sidechain-driven vocoder work, but the custom WASM effect ABI receives no separate sidechain buffer.
  - `R-effects-41` Sample-buffer inputs for custom effects - missing (docs/WASM-EFFECT-ABI.md:152-196 (audio planes and parameters only); plugin.impulse_load is ConvolutionReverb-only); implement: Custom WASM effects cannot receive a sample buffer.
  - `R-effects-42` Timeline MIDI input to audio effects - partial (plugin.host_notes; docs/WASM-EFFECT-ABI.md:152-172; no MIDI event input is defined for WASM effects); complete: Hosted plug-ins can receive note events on supported host paths, but custom WASM effects have no timeline MIDI event stream.

## Q050-346 Named stacked MIDI controller lanes

**P3 · L07 note editor, MIDI editing, scales, chords, articulations · completion · M**

Goal: Name custom controller lanes per track, show several controller curves together, and reuse their names on MIDI CC rack knobs.

Why: Low demand across one DAW for lane names, one for stacked lanes, and two for labelled rack knobs; the chooser shows one curve and many controls remain numbered CCs.

Acceptance:
- A clip can show and edit several selected controller curves at once.
- Custom controller names save per track and appear in the lane chooser, CC rack labels, and tooltips.
- A command and save/reload test verify lane values and names without changing unrelated CC events.
Items:
  - `R-editing-123` Per-track names for MIDI CC lanes - missing (docs/KNOWN-LIMITATIONS.md:2515 (only standard controllers carry names, the rest read 'CC <n>'); no command names a CC lane (rg controller name over commands)); implement: CC lanes cannot be given per-track names.
  - `R-editing-131` One-lane MIDI CC editing frustrates some Reason users - partial (docs/NOTE-EDITOR.md:129-135 (the lane chooser shows one controller curve at a time); window.note_editor_set controller_lane takes one lane); complete: Every CC, pitch bend and aftertouch can be edited in the note editor, but only one controller lane is visible at a time.
  - `R-ui-70` Show MIDI CC names beside controller knobs - partial (src/gui/MidiCCRackView.cpp:92 (knobs labelled 'CC %1'); src/gui/noteeditor/LaneScale.cpp:78 (names exist in the note editor)); complete: The note editor names controllers, but the MIDI CC Rack still shows bare numbers.

## Q050-347 MCP command profiles

**P3 · L17 agent control surface and AI assistance · feature · M**

Goal: An MCP client can expose only the command groups or named profile needed for a task and switch profiles without restarting Zene.

Why: Both items report low demand (2 and 1 DAWs), while registry.py currently builds tools from the whole command registry and has no group or profile filter.

Acceptance:
- Launch the bridge with a selected group or named profile and verify tools/list contains only commands in that selection.
- Switch profiles through the bridge and verify the tool list changes for the connected client; default startup behavior and invalid-profile errors are documented.
Items:
  - `R-ai-4` MCP tool-list context reduction - missing (tools/mcp-zene-control/zene_control/registry.py:1-8 (tools generated from the whole registry); no profile/group filter in zene_control/*.py); implement: The MCP bridge always exposes all 822 commands; clients cannot load a focused group or profile.
  - `R-ai-15` Headless agent control with selective profiles - partial (tools/mcp-zene-control/zene_control/registry.py:1-8; window.tool_menu 'Headless, nothing is shown'; zene_commands); complete: Headless control and rendering exist through the bridge, but there is no profile-based tool loading.

## Q050-348 Track color defaults and folder inheritance

**P3 · L13 customisation, shortcuts, themes, layout · feature · M**

Goal: Set new-track colors and apply a folder's color to its children when desired.

Why: Two low-demand, two-DAW reports identify per-track-only coloring and no default new-track color, leaving folder descendants and new tracks manual.

Acceptance:
- Set the default track color to none, fixed, and cycling in turn and create tracks to verify each choice.
- Apply a folder color to existing children and add a new child; verify propagation and inheritance follow the configured option.
Items:
  - `R-ui-28` Folder color inheritance - missing (src/core/ControlCommandsTrackLooks.cpp:214-217 (track.set_color colours one track); no folder-to-children colour propagation found); implement: Recolouring a folder does not recolour its child tracks.
  - `R-ui-49` Users ask to set a default track color - partial (track.set_color (per track); rg default track colour setting in src: none); complete: Tracks can be coloured, but there is no default colour for new tracks.

## Q050-349 Copy and paste sample ranges and make a new clip

**P3 · L05 audio editing, warping, stretching, fades · feature · M**

Goal: Users can copy, cut, and paste a selected source range or create a new clip that retains the source clip's properties.

Why: Range clipboard work is low demand across 3 DAWs, and the Sample Editor has range operations but no copy/paste or copy-to-new-clip path.

Acceptance:
- Copy, cut, and paste a selected range in insert and overwrite modes and compare the resulting samples with a reference buffer.
- Use Copy Selection to New Clip and verify the new clip contains only the selected samples and preserves tempo, gain, and other source properties.
- Verify undo/redo and agent command read-back.
Items:
  - `R-editing-118` Waveform editing view - partial (src/gui/SampleEditorDialog.cpp:175-182 (gain, silence, delete, reverse, fades, crop); no copy/paste of a range); complete: The in-DAW wave editor cuts, silences and crops, but cannot copy and paste a phrase within or between samples.
  - `R-workflow-228` Sample editing retains sample properties - partial (src/gui/SampleEditorDialog.cpp:67,187,275 (sample.edit_range ops: gain, silence, delete, reverse, fades, crop); no copy-selection-to-new-clip); complete: The sample editor edits in place; there is no way to copy a selected region into a new clip that keeps the source clip's gain, warp and pitch settings.

## Q050-350 Plug-in editor and instance diagnostics

**P3 · L10 devices, instruments, effects, plug-in hosting · feature · M**

Goal: Users can see per-device load, editor, latency, process-time, and sleep status.

Why: Diagnostics currently require socket-level/process-wide data and editor responsiveness is not reported; demand is low across 3 and 1 DAWs.

Acceptance:
- Show per-device diagnostics in the panel and through a command; verify a failed editor-open is reported without stopping audio.
- Check values update across load, process, editor close, and unload.
Items:
  - `R-interop-53` Host-dependent plug-in behavior - partial (plugin.host_chunking, plugin.scan_cache_list, oop.get_state, safestart.get_state (diagnostics on the socket); docs/KNOWN-LIMITATIONS.md:1630-1642 (process-wide counters, no per-device panel)); complete: Hosting diagnostics exist on the control surface, but they're process-wide with no per-device view.
  - `R-ui-84` Plug-in GUI can freeze while audio keeps running - unknown (docs/KNOWN-LIMITATIONS.md:168-176 (editor hosting); no editor-responsiveness status found); complete: Not established whether a hung plug-in editor is detected; scan status does not cover editor health.

## Q050-351 Same-key sustain and retrigger handling

**P3 · L07 note editor, MIDI editing, scales, chords, articulations · feature · M**

Goal: Hold a note until the next same-pitch note or stop, and optionally trim an earlier recorded note when the same pitch retriggers.

Why: Low demand across one DAW for sustained notes and low demand across one for repeated-note cleanup; neither same-key workflow is implemented.

Acceptance:
- A playback test verifies hold-until-next-note behavior and termination at stop.
- A recording test with the option enabled trims the earlier same-pitch note and leaves other pitches untouched.
- The note flag and recording option save, reload, and undo correctly.
Items:
  - `R-editing-111` Sustained or pseudo-pedal note type - missing (commands_snapshot.json: no note type or flag that holds until the next same-pitch note (rg sustain|pedal|legato over command descriptions); CC64 is only a controller lane); implement: There is no note mode that sustains until the next note of the same pitch or until playback stops.
  - `R-recording-78` Close repeated MIDI notes when sustain pedal retriggers - missing (commands_snapshot.json note.* has no overlap/same-pitch trim; no record-time normalisation found); implement: No option to close a held note when the same pitch retriggers.

## Q050-352 Scripted instrument and MIDI module types

**P3 · L10 devices, instruments, effects, plug-in hosting · feature · L**

Goal: Users can load a sandboxed script instrument that receives notes and exposes declared automatable parameters.

Why: The current WASM host is effect-only and these low-demand instrument requests each come from 1 DAW.

Depends on: Scriptable effect and MIDI devices
Acceptance:
- Load a fixture module, play notes, automate a declared parameter, and round-trip module state.
- The module runs within documented bounds and has an agent command test.
Items:
  - `R-instruments-43` Scriptable custom instrument - partial (script.run; plugins/WasmEffect/WasmEffect.cpp:89-117 loads a user module as an effect; no per-note script instrument implementation found.); complete: Lua can drive DAW commands and WASM can process effects, but neither exposes a user-authored note-triggered instrument.
  - `R-instruments-70` Separately linked WASM devices - partial (plugins/WasmEffect/WasmEffect.cpp:89-117 loads external WASM modules into an effect device; no WASM instrument or MIDI device type was found.); complete: Users can load WASM DSP modules as effects, but the runtime does not expose general WASM instrument and event devices.

## Q050-353 Apply zero-crossing snap to all cut gestures

**P3 · L05 audio editing, warping, stretching, fades · fix · M**

Goal: Users can snap sample-range and timeline cuts to nearby zero crossings when the mode is enabled.

Why: Zero-crossing edits are low demand across 2 DAWs, but several split/delete/knife gestures bypass the existing nearestZeroCrossing path.

Acceptance:
- With zero-crossing snap enabled, verify range.split, range.delete, vca.edit_split, clip.split_at, Shift-knife, and Sample Editor range edges use nearestZeroCrossing.
- Verify next/previous zero-crossing navigation and confirm disabled mode preserves the exact requested cut position.
Items:
  - `R-editing-124` Zero-crossing snap for all cut gestures - partial (snap_to_zero_crossings in src/core/SampleFrameEdits.cpp:243-258, called from src/gui/clips/ClipView.cpp:1033,1528 and clip.split/clip.trim; not from range.split/range.delete, vca.edit_split or clip.split_at; docs/KNOWN-LIMITATIONS.md:2357 Shift-knife does not snap); complete: Knife, edge drag, clip.split and clip.trim snap to zero crossings, but range edits, edit-group splits, split-at-transients and the destructive Shift-knife do not.
  - `R-editing-142` Zero-crossing edit navigation - partial (arrangement.modes snap_to_zero_crossings (src/core/SampleFrameEdits.cpp:186-258); clip.split/clip.trim 'zero_crossing'; Sample Editor range fields have no zero-crossing snap (src/gui/SampleEditorDialog.cpp:111-113)); complete: Arranger cuts and trims snap to zero crossings, but the Sample Editor's range cannot snap to or step between zero crossings.

## Q050-354 Note trim and retrograde tools

**P3 · L07 note editor, MIDI editing, scales, chords, articulations · completion · M**

Goal: Trim selected notes at the selection or loop end and reverse their order in time within a clip or selection.

Why: Low demand across one DAW for trimming and low demand across one for retrograde; neither note operation exists in the command surface.

Acceptance:
- Trim removes overhang at the chosen edge in one undo step and leaves notes before the edge unchanged.
- Retrograde preserves each note's duration and relative spacing while reversing note order inside the chosen range.
- Both operations are available through Notes menu commands and round-trip through undo and redo.
Items:
  - `R-editing-133` Direct MIDI note trimming - partial (note.slice cuts notes at a tick; note.resize per note; no trim-to-range command in commands_snapshot.json); complete: Notes can be sliced at a line and then deleted, but there is no one-step trim of notes that overhang a bar or selection.
  - `R-editing-139` Add quick Cubed pattern transforms - partial (note.invert, note.duplicate, note.transpose (Shift+arrows move an octave, docs/NOTE-EDITOR.md:97); no note reverse/retrograde command in commands_snapshot.json); complete: Octave shift, duplicate and vertical inversion exist; reversing a pattern in time does not.

## Q050-355 Add high-resolution MIDI controller mappings

**P3 · L15 platform: performance, stability, Linux audio, install, hardware controllers · feature · L**

Goal: Users can map SysEx, RPN/NRPN and 14-bit CC controls from external devices to Zene parameters.

Why: The queue finds no SysEx parameter maps or RPN/NRPN support, with low demand and no DAW-count evidence.

Acceptance:
- MIDI input tests assemble RPN/NRPN and paired 14-bit CC messages and bind them through learn/map commands.
- A SysEx template test verifies byte masks, parameter scaling and optional Roland/Yamaha checksum handling.
- Saved mappings reload and update the same parameter values as equivalent 7-bit CC mappings.
Items:
  - `R-editing-145` Automatable SysEx controller mappings - missing (no SysEx command in commands_snapshot.json (rg sysex over descriptions only hits surface.mcu_*); track.midi_out sends notes only); implement: There is no SysEx parameter definition (variable fields, checksum) that automation could drive.
  - `R-interop-58` Assignable RPN/NRPN controllers - partial (midi.learn_toggle / controller.map (7-bit CC learn); no NRPN/RPN handling in src/ include/ (rg -w nrpn|rpn: no hits outside 3rdparty)); complete: MIDI learn and mapping cover CCs only; RPN/NRPN and 14-bit controllers can't be assigned.

## Q050-356 Document Zene’s MIDI 2.0 support position

**P3 · L15 platform: performance, stability, Linux audio, install, hardware controllers · docs · S**

Goal: Users and contributors can see that MIDI 2.0 input is not yet supported and what the planned ALSA path covers.

Why: The queue found no UMP input beyond a plug-in dialect flag and rates current demand low; documentation is the first stage of the XL request.

Acceptance:
- The MIDI support documentation clearly distinguishes the current MIDI 1 input path from planned UMP input and states the supported-platform boundary.
Items:
  - `R-interop-21` MIDI 2.0 support - missing (no UMP/MIDI 2.0 code in src/ include/ outside src/3rdparty (only ControlCommandsHostNotes.cpp:56 reports a CLAP plug-in's midi2 dialect flag); midi.device_list is MIDI 1); implement: There's no MIDI 2.0 (UMP) device I/O and no stated plan for it.

## Q050-357 Add large-project arranger and engine benchmarks

**P3 · L15 platform: performance, stability, Linux audio, install, hardware controllers · completion · M**

Goal: Contributors can compare arranger/mixer responsiveness and engine cost on representative 100–200 track projects.

Why: Existing performance fixtures cover the piano roll and small engine sessions, but no large arranger/mixer or high plug-in-count baseline exists; demand is low.

Acceptance:
- Benchmarks measure arranger and mixer frame time at 100 and 200 tracks and engine CPU with several built-in effects per track.
- The benchmark records repeatable same-machine baselines separately from pass/fail correctness tests.
Items:
  - `R-performance-101` Large-project UI responsiveness - partial (tests/src/gui/PianoRollBenchmarkTest.cpp:2,120 (piano roll at 1k-50k notes); tests/src/gui and tests/src/core search found no arranger or mixer large-project frame-time benchmark); complete: Only the piano roll has a large-content frame-time benchmark; the arranger and mixer have no measured large-project responsiveness.
  - `R-performance-106` High plugin-count performance on Apple Silicon - partial (tests/src/core/EngineRenderBenchmarkTest.cpp:28-35 (16 tracks); tests/engine-cpu-baseline.tsv:3-5 (three demo projects); tests/src/core/PhaseDPerfBench.cpp:27; no 200-track or many-plug-in fixture); complete: Engine cost is benchmarked on 16 tracks and three demo projects, but no high track-count, high effect-count workload is measured.

## Q050-358 WebCLAP runtime bridge

**P3 · L10 devices, instruments, effects, plug-in hosting · feature · L**

Goal: Users can load a WebCLAP module and process it in the WASM sandbox.

Why: The existing WASM runtime has no WebCLAP host or catalogue entry and the request is low demand from 1 DAW.

Acceptance:
- Load a fixture .wclap, map its parameters and process callback, and verify its output against a reference.
- Display the fixture in plugin.list and the device picker, then load it from the picker and verify status and agent read-back.
- Reject malformed modules and verify memory and execution bounds.
Items:
  - `R-interop-33` WebCLAP plug-in hosting - missing (plugin.list (LADSPA/LV2/VST3/CLAP); no webclap hits in src/ include/ plugins/; src/wasm/WasmSandbox.h (a WASM runtime exists for script devices)); implement: Zene can't host WebCLAP modules, although it already embeds a WASM sandbox.

## Q050-359 Carla Rack VST3 opening stability

**P3 · L10 devices, instruments, effects, plug-in hosting · fix · S**

Goal: Users can open a VST3 inside Carla Rack without crashing Zene.

Why: The suspected Carla/VST3 crash was not reproduced and the request is low demand across 2 DAWs.

Acceptance:
- Repeat the exact Carla Rack/VST3 load sequence under a crash trace or record a bounded not-reproduced result.
- If reproduced, fix the host path or route VST3 to the native host and add a regression test.
Items:
  - `R-effects-6` Carla Rack VST3 opening stability - unknown (plugins/CarlaRack still ships; Zene also hosts VST3 natively (plugins/Vst3Effect, plugins/Vst3Instrument); the crash was not reproduced here); fix: An upstream LMMS report on the Carla Rack path that Zene still ships; not reproduced.

## Q050-360 CV/Gate output instrument

**P3 · L09 routing, buses, sends, sidechain, I/O · feature · M**

Goal: Send calibrated V/oct pitch CV, gate, and velocity signals to selected hardware outputs.

Why: The merged source has MIDI output and audio hardware routing but no CV/Gate output path; demand is low from 2 DAWs.

Acceptance:
- A dummy multichannel output test checks calibrated pitch voltage, gate timing and velocity DC on the selected channels.
- The command surface selects output buses and calibration values, and an audio-path allocation probe remains at zero during playback.
Items:
  - `R-interop-41` CV/Gate modular hardware routing - missing (no CV/Gate output in src/ include/ (rg 'control voltage|cv_gate' no hits); track.midi_out is MIDI only; mixer.hardware_output routes audio to interface outputs); implement: There's no CV/Gate path for DC-coupled interfaces, although per-channel hardware outputs exist to carry it.

## Q050-361 Resizable Song Editor sidebar

**P3 · L13 customisation, shortcuts, themes, layout · feature · M**

Goal: Resize the track-header column and retain its width with the saved layout.

Why: The low-demand request cites two DAWs and a fixed track-header width, so users cannot resize and retain it with layouts.

Acceptance:
- Drag the Song Editor track-header divider to a new width, save the layout, reopen it, and verify the width is restored.
Items:
  - `R-ui-30` Resize the Song Editor sidebar - missing (src/gui/editors/SongEditor.cpp:106-108 (track header width fixed: compact or default constant)); implement: The arranger's track-header column cannot be resized.

## Q050-362 Correct sample-accuracy limitation docs

**P3 · L06 automation and modulation · docs · S**

Goal: Read one accurate statement of which automation clips are sample-accurate and which remain limited.

Why: Demand is low (3 DAWs); later docs describe sample-accurate curve clips and a 32-knot cap while the old limitation line still says automation is not sample-accurate.

Acceptance:
- KNOWN-LIMITATIONS.md replaces the blanket statement that automation is not sample-accurate with a pointer to the implemented curve-clip behavior.
- The text states the current 32-knot capacity and that unsupported pattern clips are refused.
- A documentation search finds no remaining contradictory blanket claim.
Items:
  - `R-performance-73` Sample-accurate automation controls - partial (docs/KNOWN-LIMITATIONS.md:1812-1850 (sample-accurate by default for curve clips since R1.2; MaxKnots 32; pattern clips refused) vs docs/KNOWN-LIMITATIONS.md:612 ('Automation is not sample-accurate')); docs: Sample-accurate automation is already the default for curves; the 32-knot cap only bites near 400 BPM, but the older bullet still says automation is not sample-accurate.

## Q050-363 MIDI clipboard interchange

**P3 · L07 note editor, MIDI editing, scales, chords, articulations · feature · M**

Goal: Copy notes as Standard MIDI File clipboard data and paste MIDI data from other applications into the note editor.

Why: Low demand across three DAWs; the clipboard currently carries only Zene-specific MIME types and no MIDI interchange data.

Acceptance:
- Copy publishes both the native project MIME type and audio/midi data with note timing and channel intact.
- Paste accepts a valid MIDI clipboard and safely refuses malformed or oversized data.
- A round-trip test copies and pastes notes between two editor instances and verifies one undo.
Items:
  - `R-interop-72` Cross-application MIDI note clipboard - missing (src/core/Clipboard.cpp:57-98 (only LMMS string-pair MIME types; no audio/midi on the clipboard)); implement: Copied notes stay inside Zene, because the clipboard carries no standard MIDI.

## Q050-364 Sandboxed third-party device SDK

**P3 · L10 devices, instruments, effects, plug-in hosting · feature · L**

Goal: Developers can build and load a documented sandboxed third-party device module.

Why: The installed WASM path is an effect-only sandbox and the request for a broader safe device SDK is low demand from 1 DAW.

Acceptance:
- Build an external example module against the published interface and load/unload it through plugin commands.
- Reject malformed or over-budget modules without destabilizing the host.
Items:
  - `R-instruments-5` Safe dynamic third-party device loading - partial (wasm.list and wasm.load; docs/KNOWN-LIMITATIONS.md:440-479 describes the optional WASM effect sandbox); complete: A sandboxed WASM effect ABI exists, but it is optional at build time and does not provide a general sandboxed instrument/device SDK.

## Q050-365 Rack macros for any automatable target

**P3 · L10 devices, instruments, effects, plug-in hosting · feature · M**

Goal: Users can bind rack macros to any addressable fader, send, instrument, or effect parameter.

Why: Rack macro targets are limited to parameters inside the rack; demand is low across 1 DAW.

Acceptance:
- Bind a macro to targets outside its own rack and verify min/max and inversion behavior.
- Save/load bindings and read back target IDs through the agent surface.
Items:
  - `R-mixing-4` Rack macros for arbitrary automatable parameters - partial (docs/RACK-MACROS.md:78-82 (a target is chain, effect and parameter display name inside the rack); rack.macro_target_add); complete: Macros can only drive effect parameters inside the rack, not any automatable model (faders, sends, instrument parameters).

## Q050-366 Rack chain-select crossfades

**P3 · L10 devices, instruments, effects, plug-in hosting · feature · M**

Goal: Users can automate a chain-select value and crossfade between zoned rack chains.

Why: Rack zones currently cover key/velocity only and docs say chain crossfades are absent; demand is low from 1 DAW.

Acceptance:
- Sweep the selector across overlapping zones and verify equal-power output through a render.
- Save/load zones and expose the selector value to commands.
Items:
  - `R-mixing-2` Rack chain-select zones with crossfades - partial (rack.zone_add (key/velocity zones only); docs/RACKS.md:139-142 ('no crossfade ... An equal-power crossfade is not implemented'); include/Rack.h:64-80); complete: The chain selector picks one chain or all with a hard switch; there are no 0-127 select zones and no crossfades.

## Q050-367 VST2 editor teardown stability

**P3 · L10 devices, instruments, effects, plug-in hosting · fix · S**

Goal: Users can remove a VST2 effect while its editor is open without a crash.

Why: The suspected teardown path is identified but the crash was not reproduced; demand is low from 1 DAW.

Acceptance:
- Reproduce removal with an open editor under the available VST2 fixture or record a bounded not-reproduced result.
- If reproduced, close and detach the editor before destroying the effect and add a regression test.
Items:
  - `R-effects-5` VST2 effect-editor teardown stability - unknown (src/gui/EffectRackView.cpp:146-153 deletes the view and the effect; plugins/VstEffect/VstEffectControlDialog.cpp:255 dialog teardown; the crash was not reproduced here); fix: An upstream LMMS crash on removing a VST2 effect with its editor open; Zene shares this code and it was not tested.

## Q050-368 Audio export metadata, cover art, and chapter tags

**P3 · L12 projects, files, import/export, interchange, recovery · feature · M**

Goal: Export project metadata, cover art, and section chapters in audio files using each format's native tags.

Why: The rows cite low demand from one DAW and missing metadata/chapter tags, with no broken behavior or promise gap to raise priority.

Acceptance:
- Add an editable artwork field to project metadata, export WAV/FLAC/OGG/MP3 fixtures, and read back the expected title/comment tags and cover image.
- Export named sections as MP3 chapters and verify their time ranges and cover image in the file metadata.
Items:
  - `B-WF-047` Project metadata - partial (project.metadata (src/core/ControlCommandsProjectInfo.cpp:215; src/gui/LibraryCommandMenus.cpp:218 Project Info...); exports tag only software: src/core/audio/AudioFileWave.cpp:128, AudioFileFlac.cpp:85, AudioFileMP3.cpp:124); complete: Project metadata is edited and saved, but exports do not write title/artist/album tags from it.
  - `R-interop-46` Podcast chapter and metadata export - missing (project.metadata exists but exports don't embed it: src/core/audio/AudioFileMP3.cpp:123-124 (only an id3 comment), AudioFileWave.cpp:128 / AudioFileFlac.cpp:85 (only a software string); no chapter writer); implement: Exports don't embed metadata, chapters or cover art.

## Q050-369 Add a List button to the automation editor

**P3 · L14 help, onboarding, accessibility · feature · S**

Goal: Open the editable automation item list directly from the automation toolbar.

Why: The item is low demand across three DAWs and documents one remaining toolbar gap despite the automation item list itself being available.

Acceptance:
- Verify the automation toolbar's List button opens window.item_list for the automation surface.
- Use the button by keyboard and confirm focus returns to the automation editor after closing the list.
Items:
  - `R-accessibility-22` Toolbar access to the editable item list - partial (docs/KNOWN-LIMITATIONS.md:2426-2429 (automation list has no List button); window.item_list (src/gui/ItemListPanel.cpp:439)); complete: Most canvases have a List button, but the automation editor's list opens only with Ctrl+Shift+L or the command.

## Q050-370 Keep command results and show list progress

**P3 · L14 help, onboarding, accessibility · feature · M**

Goal: Review recent command results and see progress while a slow list operation runs.

Why: The item has low demand across three DAWs, and KNOWN-LIMITATIONS.md:2457-2458 records transient results and a busy cursor for list reads.

Acceptance:
- Run commands from menus and the palette; verify results remain in a reviewable log until dismissed or cleared.
- Run a deliberately slow list read and verify progress appears, completes, and reports failure instead of leaving a busy cursor.
- Verify progress and result history remain usable by keyboard and assistive technology.
Items:
  - `R-ui-42` Stable command feedback and task progress - partial (docs/KNOWN-LIMITATIONS.md:2457-2458 (results float briefly; list reads show a busy cursor)); complete: Command results vanish quickly and slow list reads show no progress.

## Q050-371 Detect and repair clicks in sample ranges

**P3 · L05 audio editing, warping, stretching, fades · feature · M**

Goal: Users can find clicks, navigate between them, and interpolate the selected defect in the Sample Editor.

Why: Click repair is low demand from 1 DAW, and the command surface has no click detection or declick operation.

Acceptance:
- Run the detector on a fixture with labelled clicks and verify next/previous problem navigation selects the expected spans.
- Apply interpolation to one selected span and verify the click energy falls without changing adjacent samples; confirm undo and command state.
Items:
  - `R-editing-113` Spectral repair for pops and clicks - missing (sample.spectrogram is read-only data; no repair/declick command in commands_snapshot.json); implement: There is no click/pop detection or spectral repair of sample audio.

## Q050-372 Optional master oscilloscope in the shell

**P3 · L08 mixer, metering, channel strip · completion · S**

Goal: Users can show or hide a live master waveform monitor in the shell header from the View menu.

Why: The request is low demand in 1 DAW; the existing oscilloscope is not currently constructed in the shell.

Acceptance:
- The View menu toggles the existing Oscilloscope widget in the shell header and the setting persists.
- A UI test confirms the displayed trace follows master output and hidden mode removes the widget from the layout.
Items:
  - `R-mixing-44` Master output waveform monitor - missing (src/gui/editors/SongEditor.cpp:146-150 (the inherited toolbar scope was removed in the 0.5.0 shell); src/gui/widgets/Oscilloscope.cpp is compiled but never constructed (no 'new Oscilloscope' in src/gui)); implement: The inherited master waveform scope was dropped with the old toolbar, so there's no persistent master output monitor.

## Q050-373 Route MIDI effects between instrument tracks

**P3 · L09 routing, buses, sends, sidechain, I/O · feature · M**

Goal: Send a track's post-effect MIDI notes to another instrument track for playback or recording.

Why: track.midi_out covers external ports only and the merged command snapshot has no inter-track MIDI route; demand is low from 1 DAW.

Acceptance:
- A track.midi_route command selects a destination track and the routing state is readable and undoable.
- A control-surface test verifies effect-generated notes reach the destination instrument and can be recorded without duplicate events.
Items:
  - `R-interop-45` Route MIDI effects between instrument tracks - missing (track.midi_out (external ports only); no inter-track MIDI route in src/ include/ or the command snapshot); implement: An instrument track's generated MIDI (arpeggio or chord output) can't feed another instrument track.

## Q050-374 Custom script-device controls

**P3 · L10 devices, instruments, effects, plug-in hosting · feature · L**

Goal: Users can operate script devices through XY pads, trigger buttons, and stepped dials.

Why: The design document names no implemented custom-device control vocabulary; this low-demand request is from 1 DAW.

Depends on: Scriptable effect and MIDI devices
Acceptance:
- Render each declared control type and verify values, steps, and trigger behavior through GUI and command state.
- Control metadata and values persist across save/load.
Items:
  - `R-ui-51` Expand custom-device controls - missing (docs/LUA-SCRIPT-DEVICES-DESIGN.md:1-6 (script-defined devices: design only, not implemented)); implement: There is no user-built device with custom controls such as XY graphs, pulse buttons or stepped dials.

## Q050-375 Dynamic EQ modes and band solo

**P3 · L10 devices, instruments, effects, plug-in hosting · feature · L**

Goal: Users can switch Dynamic EQ to linear-phase or mid/side operation and audition a single band.

Why: Dynamic EQ lacks linear-phase/mid-side modes and solo; its source requests are medium/low demand across 3 and 1 DAWs.

Acceptance:
- Render stereo and mid/side fixtures in each mode and verify band solo isolates the selected band.
- Check latency reporting, saved settings, and the agent state response.
Items:
  - `B-MX-143` Dynamic, linear-phase and mid/side EQ - partial (plugins/DynamicEq/DynamicEq.cpp:42-45; plans/TRACKER-050.md:463); complete: Dynamic EQ is present, but linear-phase and mid/side EQ modes are still missing.
  - `R-effects-32` Stock dynamic EQ with soloable bands - partial (plugins/DynamicEq/DynamicEq.cpp:84-105 (per-band frequency, gain, Q, threshold, dynamic; no solo)); complete: The four-band dynamic EQ has no band solo.

## Q050-376 SoundFont sample extraction

**P3 · L10 devices, instruments, effects, plug-in hosting · feature · M**

Goal: Users can export a selected SoundFont sample to a file or a new audio track.

Why: Sf2Player enumerates programs but has no sample extraction/export path; this low-demand request is from 1 DAW.

Acceptance:
- Select a sample from a SoundFont program and export it to a WAV file and a new track.
- Verify sample rate, duration, and audio contents against the source sample.
Items:
  - `R-instruments-42` Extract SoundFont samples to tracks - missing (plugins/Sf2Player/PatchesDialog.cpp:353-388 enumerates SoundFont programs; searched src/, include/, plugins/ and tests/ for SF2 sample extraction/export.); implement: The SoundFont browser lists presets but cannot extract a contained sample into an audio track.

## Q050-377 Export a selected video range with muxed audio

**P3 · L12 projects, files, import/export, interchange, recovery · feature · L**

Goal: Render audio for a chosen tick range and mux it with the matching attached-video frames in the selected container and codec.

Why: The row reports low demand from one DAW and confirms there is no video encoder in the merged tree.

Acceptance:
- Export a video fixture for a selected tick range and verify first/last frame, audio duration, codec, and container with FFmpeg probes.
- Verify the command reports a typed error for unsupported codecs or unwritable output.
- Invoke video.export with a tick range and verify the same range through the video export dialog.
Items:
  - `R-interop-43` Time-selected video export - missing (render.render (audio only); video.* has attach/extract_audio/window only; no video encoder in src/ (no avcodec_send_frame/VideoEncoder)); implement: Zene can't render the timeline with its video.

## Q050-378 Preview projects from the Start Hub

**P3 · L12 projects, files, import/export, interchange, recovery · feature · M**

Goal: Play a short project preview from the Start Hub or recent-project list before opening the full project.

Why: The row reports low demand from one DAW and confirms recent/template lists have no preview playback.

Acceptance:
- Create or record a preview, restart the app, and play/pause it from both the Start Hub and recent-project list.
- Verify preview playback does not load or change the active project.
- Verify preview generation and playback are available to the registered agent surface as well as the Start Hub.
Items:
  - `R-workflow-165` Project preview before loading - missing (src/gui/StartHub.cpp:42-78 (recent and template lists only); rg 'preview' in StartHub.cpp found nothing); implement: The Start Hub and recent-project lists can't audition a project before it opens.

## Q050-379 Use the platform Projects folder as the new-project default

**P3 · L12 projects, files, import/export, interchange, recovery · feature · S**

Goal: Save new projects under XDG_PROJECTS_DIR or ~/Projects when available, with the current Documents fallback.

Why: The row reports low demand from one DAW and a missing Linux default-location preference.

Acceptance:
- With XDG_PROJECTS_DIR set, verify the new-project working directory uses it.
- With it unset, verify ~/Projects is preferred only when present and Documents is used otherwise.
Items:
  - `R-platform-47` Use the platform Projects folder as the initial save location - missing (src/core/ConfigManager.cpp:733-736 (default working dir is DocumentsLocation + '/Zene Studio/'); rg XDG_PROJECTS over src/ include/ found none); implement: New installs always default under ~/Documents; XDG_PROJECTS_DIR or ~/Projects is not honoured.

## Q050-380 Split and combine multichannel audio files

**P3 · L12 projects, files, import/export, interchange, recovery · feature · S**

Goal: Split a multichannel recording into mono clips/tracks and combine mono inputs into one interleaved WAV.

Why: The row reports low demand from one DAW and confirms the current conversion command has no channel split/combine operations.

Acceptance:
- Split a multichannel fixture and verify one correctly ordered mono output per source channel.
- Combine mono fixtures and verify channel order, sample rate, sample count, and sample values in the interleaved WAV.
Items:
  - `R-interop-44` Split and combine multichannel audio files - missing (render.convert (format, sample rate and bit depth only); no channel split/combine in src/ or the command snapshot); implement: There's no way to split a multichannel file into mono tracks or combine mono files into one.

## Q050-381 Elect a successor when the collaboration host leaves

**P3 · L16 collaboration and sharing · feature · L**

Goal: A peer can leave or fail without ending the room because the remaining participants elect a new host and continue from the shared state.

Why: Demand is low across 1 DAW, and CollabSession.cpp:179-185 currently stops hosting by disconnecting every peer.

Depends on: Resume media and collaboration state after disconnect
Acceptance:
- A multi-instance test terminates the host and verifies that one eligible peer becomes authoritative and the others reconnect to it with the latest shared state.
- A session epoch or equivalent prevents two peers from accepting edits as host at the same time.
- A graceful host departure and an abrupt host loss both produce observable status through collab.get_state.
Items:
  - `R-collaboration-33` Hostless room continuation - missing (src/core/CollabSession.cpp:179-185); implement: The current host owns the session and stopping it disconnects every peer.

## Q050-382 Add spectrogram view and time-frequency selection

**P3 · L05 audio editing, warping, stretching, fades · feature · L**

Goal: Users can inspect one or more audio sources as spectrograms and select a time-frequency region for editing.

Why: Spectral editing is low demand across 2 DAWs, and sample.spectrogram currently returns read-only data with no editor route.

Acceptance:
- Open a known-tone fixture and verify its frequency bins and time positions in the Sample Editor spectrogram.
- Overlay up to eight sources and verify region selection reports the expected time and frequency bounds.
- Expose the selected region to the agent surface and verify stable read-back.
Items:
  - `R-editing-128` Multi-source visualization and spectral editing - partial (sample.spectrogram returns data only; plugins/SpectrumAnalyzer/SaWaterfallView.cpp is a live analyser; no spectral edit command); complete: Spectrum data and a live waterfall exist, but there is no spectrogram view of clips, no multi-source view and no spectral editing.

## Q050-383 Extend per-note modulation to built-in instruments

**P3 · L06 automation and modulation · feature · L**

Goal: Apply independent additive modulation to notes in more built-in instruments.

Why: Demand is low (2 DAWs); the existing per-voice route only works where an instrument exposes a note target, leaving built-in coverage narrow.

Acceptance:
- At least two built-in instruments expose per-note modulation targets through the same registered route and state APIs.
- A render test plays overlapping notes with different per-note modulation and confirms each note changes independently without replacing its base parameter.
- Save/load, undo/redo, and the audio-path allocation counter cover the added built-in targets.
Items:
  - `R-instruments-53` Additive, non-destructive modulation - partial (modulator.target_set adds modulation to the parameter’s existing value; modulator.voice_set supports per-note targets only when the plug-in accepts them.); complete: Global modulation is additive, but per-note modulation still depends on hosted plug-in support and does not cover all targets.

## Q050-384 Playback MIDI parameter strip

**P3 · L07 note editor, MIDI editing, scales, chords, articulations · feature · M**

Goal: Apply non-destructive velocity, length, and random modifiers to clip playback and edit them from an Inspector strip.

Why: Low demand across two DAWs; current track input transforms affect live input only and there is no playback modifier strip.

Acceptance:
- Playback tests verify velocity offset and scale, length factor, and bounded randomization without changing stored notes.
- The Inspector and agent command expose identical values and the settings survive save and undo.
- Randomization is reproducible when a seed is supplied.
Items:
  - `R-editing-121` Non-modal MIDI parameter strip in the editor - partial (track.input_transform_set acts on live input only; clip.set_transpose, track.set_delay, transport.groove; no non-destructive velocity/length modifiers on playback); complete: Transpose, delay and swing are non-destructive, but there is no per-track playback MIDI modifier strip (velocity shift/compress, length scale) without editing the notes.

## Q050-385 Multi-system chip instrument

**P3 · L10 devices, instruments, effects, plug-in hosting · feature · L**

Goal: Users can create chip-style voices from several classic sound systems.

Why: The request combines several chip emulations and a tracker workflow, with low demand from 1 DAW.

Acceptance:
- Load one fixture per supported chip system and verify its waveform, note range, and saved parameters.
- Document the chosen systems and test deterministic rendering.
Items:
  - `R-instruments-48` Multi-system chip emulation - partial (plugins/FreeBoy/GbApuWrapper.cpp; plugins/FreeBoy/FreeBoy.cpp; searched product plug-ins for a multi-system chip-tracker instrument.); complete: The product includes Game Boy emulation, but lacks a tracker instrument model for composing across multiple chip systems.

## Q050-386 Chip and modular tracker editor

**P3 · L10 devices, instruments, effects, plug-in hosting · feature · L**

Goal: Users can sequence chip voices and event-processing nodes in a tracker pattern editor.

Why: Two XL tracker requests describe sequencing chip and modular event-processing voices, with low demand from 1 DAW each.

Depends on: Multi-system chip instrument; Modular instrument graph and starter modules
Acceptance:
- Create and play a multi-row chip pattern with note and event lanes.
- Save/load the pattern and verify its rendered sequence.
Items:
  - `R-instruments-47` Modular synthesis in a tracker - partial (patcher.set_wiring edits effect-chain edges and pattern.* sequences notes; src/core/ControlCommandsPatcher.cpp:53-54 limits the graph to existing effects.); complete: Pattern sequencing and effect-chain wiring exist separately, without a tracker that sequences a polyphonic patch graph.

## Q050-387 Import project tracks with mixer setups

**P3 · L12 projects, files, import/export, interchange, recovery · feature · M**

Goal: Select tracks from another project and import their mixer channels, effect chains, and sends with those tracks.

Why: The row reports low demand across two DAWs and an existing track-import command that does not carry mixer setups.

Acceptance:
- Use an import-from-project picker to choose a group of tracks and verify their mixer routing, chain, and sends are reproduced.
- Undo and redo the import and confirm track and mixer state return together.
Items:
  - `R-workflow-282` Partial mix templates - partial (project.import_tracks (mixer channels don't carry); mixer.snapshot_recall 'parts'; chain presets); complete: Tracks import from another project, but their mixer channels (chains, sends) don't, so a partial mix template can't be pulled in.

## Q050-388 Update Safe Start recovery guidance

**P3 · L14 help, onboarding, accessibility · docs · S**

Goal: Find the Safe Start command and understand how skipped plug-ins are preserved as placeholders.

Why: The recovery feature already exists and this low-demand, two-DAW item is a documentation pointer gap.

Acceptance:
- Update the safe-start limitation/help text to link to the current Safe Start menu action and its acknowledge/clear list.
- Verify the text explains that unavailable plug-ins remain as placeholders with state.
Items:
  - `R-interop-54` Third-party plug-in recovery on project open - have (safe-start skips third-party instances (safestart.*; the Safe Start... list at src/gui/shell/ShellCommandMenus.cpp:530, src/gui/CommandLists.cpp:174-182); docs/KNOWN-LIMITATIONS.md:159-167 (unloadable devices kept as placeholders with state)); docs: Recovery works and has a menu, but docs/KNOWN-LIMITATIONS.md:1141 still says there's no way to see or clear safe-start from the interface.

## Q050-389 Make Qt 6 the default release build

**P3 · L15 platform: performance, stability, Linux audio, install, hardware controllers · feature · L**

Goal: Linux, macOS and MinGW release builds use Qt 6 by default and CI verifies that configuration.

Why: Qt 6 remains experimental and off by default while release dependency files still select Qt 5; demand is low.

Acceptance:
- A default configure selects Qt 6 without an opt-in flag.
- Linux, macOS and MinGW CI jobs build and run their supported tests on Qt 6; the remaining Qt 5 support boundary is documented.
Items:
  - `R-platform-50` Qt 6 support was still an incomplete upstream migration in late 2025 - partial (CMakeLists.txt:142 (WANT_QT6 'experimental', default OFF); .github/workflows/deps-ubuntu-24.04-gcc.txt:46-51 (qtbase5); Brewfile:21 (qt@5); .github/workflows/build.yml:1184 (only the MSVC job passes -DWANT_QT6=ON)); complete: Qt 6 builds and runs in CI, but it is still marked experimental and the Linux, macOS and MinGW release jobs ship Qt 5.

## Q050-390 Deterministic musical intents

**P3 · L17 agent control surface and AI assistance · feature · M**

Goal: An agent can request bounded musical operations by intent and inspect the deterministic registered-command plan before applying it.

Why: Demand is low across 2 DAWs; existing verbs such as chord.progression_generate, groove.apply, and note.randomize do not provide a general deterministic intent layer.

Acceptance:
- Expose named intents as stable registered commands with input/result schemas; each compiles only to existing registered commands and returns the planned operations and resulting changes.
- For fixed inputs, verify the plan is repeatable, has explicit note/time bounds, applies atomically, and can be undone as one action.
Items:
  - `R-ai-5` High-level musical commands for agents - partial (command ids chord.progression_generate, groove.apply, note.randomize; no deterministic intent command found); complete: Musical operations exist as separate commands, but agents have no deterministic intent layer to plan them as bounded sequences.

## Q050-391 Verify plug-in delay compensation in instrument racks

**P3 · L08 mixer, metering, channel strip · fix · S**

Goal: Users can rely on latency alignment when a latent plug-in follows an instrument in a layer or parallel rack chain.

Why: The request has low demand from 1 DAW; plug-in PDC is covered, but layer and parallel-rack cases lack explicit tests.

Acceptance:
- Tests place a known-latency device after an instrument in both a layer and one parallel rack chain.
- Impulse renders confirm alignment to the latest chain within one sample; if either case fails, the implementation fix and regression land together.
Items:
  - `R-interop-79` Plug-in delay compensation inside instrument racks - partial (docs/KNOWN-LIMITATIONS.md:977-994 (hosted CLAP/VST3/LV2 instrument latency in PDC, InstrumentLatencyPdcTest); docs/KNOWN-LIMITATIONS.md:2346-2348 (instrument on a layer 'not separately tested'); src/core/EffectChain.cpp:82-94; no latency code in src/core/Rack*.cpp); complete: Instrument and track-chain latency is compensated, but layers aren't tested and parallel rack chains show no alignment code.

## Q050-392 Quantize arrangement marker jumps

**P3 · L02 launcher / session clips / performance · feature · M**

Goal: Perform marker or section jumps on the next selected beat or bar boundary during arrangement playback.

Why: Demand is low across 1 DAW; session clips already launch quantized, while marker.jump moves immediately and has no queued arrangement jump.

Acceptance:
- While playback is between grid boundaries, request a quantized marker or section jump and verify the playhead moves only at the requested beat or bar.
- Verify an immediate jump remains available and that get_state reports the destination after the jump.
- Exercise the behavior through the registered command surface and a visible arrangement control.
Items:
  - `R-workflow-218` Live loop arrangement and quantized launching request - partial (session.launch_slot/session.set_quantisation (quantised clip launch); marker.jump moves at once; no quantised arranger jump (rg 'queued jump|jump.*next bar' in src: none)); complete: Clip launching is quantised, but jumping the arrangement playhead to a marker or section waits for nothing - it moves immediately.

## Q050-393 Manage scenes from the launcher grid

**P3 · L02 launcher / session clips / performance · feature · S**

Goal: Duplicate, clear, insert and remove launcher scenes from the scene menu and agent surface.

Why: Demand is low across 1 DAW; Zene has folder groups and sub-scenes, but the scene menu has no duplicate, clear, insert or delete operation.

Acceptance:
- Registered session.scene_duplicate, session.scene_clear, session.scene_insert and session.scene_remove commands update the scene list and session.get_state.
- The scene menu exposes the same operations and preserves or clears cells and scene settings as named.
- Undo and redo restore scene order, cell contents and scene settings.
Items:
  - `R-workflow-229` Scene and sample-channel groups - partial (session.launch_group (folder groups), session.subscene_create/launch, session.set_scene, session.capture_scene; src/gui/SessionGridMenus.cpp:173-214 (no duplicate/clear/insert/delete scene)); complete: Channel groups, sub-scenes and scene naming exist, but there is no duplicate, clear, insert or delete scene operation.

## Q050-394 Preserve comp slip for warped takes

**P3 · L03 recording, takes, comping, inputs · completion · M**

Goal: Users can keep comp segments aligned when the source take is warped or pitch-preserved.

Why: One low-demand DAW report identifies warped and pitch-preserving comp segments playing unslipped under the current behavior.

Acceptance:
- A render test selects a non-zero comp slip on warped and pitch-preserving takes and verifies the expected source frames play.
- comp.select readback and save/load preserve the slip without changing the source take.
Items:
  - `R-recording-61` Keep alternate takes safe through later audio edits - partial (docs/COMPING.md:60-67 (a warped or pitch-preserving take plays unslipped; comp.select refuses non-zero slip on one)); complete: Comping is non-destructive, but warped takes cannot be slipped inside the composite.

## Q050-395 Save takes under a project-relative recording folder

**P3 · L03 recording, takes, comping, inputs · feature · S**

Goal: Users can choose where takes are recorded and move the project with its audio without breaking paths.

Why: Low demand from 1 DAW asks for portable recording paths; the recorder currently uses an explicit file or its default path without a project-folder setting.

Acceptance:
- A project recording-folder setting controls the default output path and is included in project state.
- A test records a take, moves the project folder, reopens the project, and resolves the take through a relative media path.
Items:
  - `R-recording-67` Custom recording directory and relative paths - partial (src/core/ControlCommandsSessionAudioRecord.cpp:183-200 (absolute 'file' or TakeRecorder::defaultTakePath); project.collect_media (relative local: paths)); complete: Takes go to an absolute path or a global Recordings folder; there is no per-project recording folder setting.

## Q050-396 Resize selected arrangement clips together

**P3 · L04 arranger, timeline, clip operations, markers, tempo map · feature · S**

Goal: Users can resize selected audio and MIDI clips by the same factor with one gesture or Time Scale command.

Why: ClipView.cpp changes only the clip under the mouse and Time Scale acts on one clip; the request is low demand in 1 DAW.

Acceptance:
- A GUI regression test selects two clips of different lengths, applies one resize factor, and verifies both lengths change by that factor.
- A single undo restores both clips, and Time Scale commands use the same multi-selection behavior.
Items:
  - `R-editing-157` Resize multiple arrangement clips together - partial (src/gui/clips/ClipView.cpp:884-920 (a resize drag changes only the clip under the mouse); Time Scale menu acts on the clicked clip (src/gui/shell/ShellCommandMenus.cpp:301-304)); complete: Clips can be time-scaled one at a time, but not several selected clips by the same factor.

## Q050-397 Set new clip length from the snap grid

**P3 · L04 arranger, timeline, clip operations, markers, tempo map · feature · S**

Goal: A newly clicked arrangement clip uses the current snap interval as its initial length, falling back to one bar when snap is off.

Why: TrackContentWidget snaps new clip position but leaves its length at the type default; the missing behavior is low demand in 1 DAW.

Acceptance:
- A GUI regression test creates clips with two different snap sizes and verifies each initial length, then verifies one-bar fallback with snap off.
- The click remains quantized to the existing snap position and MIDI's one-bar minimum is respected where applicable.
Items:
  - `R-editing-158` New clip length follows the chosen snap size - missing (src/gui/tracks/TrackContentWidget.cpp:587-590 (a click quantises the position to the snap but creates the clip at its type's default length); src/tracks/MidiClip.cpp:140-149 (one bar minimum)); implement: A new clip is always a bar long whatever the snap size.

## Q050-398 Collapse and restore individual track rows

**P3 · L04 arranger, timeline, clip operations, markers, tempo map · feature · S**

Goal: Users can collapse an individual instrument row to minimal height and restore its previous height.

Why: Instrument rows can be manually resized but only folders have a collapse command; the request is low demand in 1 DAW.

Acceptance:
- A GUI test collapses and reopens an instrument track and verifies its pre-collapse height is restored.
- The track header or menu and track command expose the same collapse state.
Items:
  - `R-workflow-209` Collapse individual instrument tracks - partial (src/gui/tracks/TrackView.cpp:422,461,531-537 (shift-drag/step resize down to MINIMAL_TRACK_HEIGHT); arrangement.fit_tracks; folder collapse only via track.folder_set_collapsed (src/gui/TrackFolderMenu.cpp:99)); complete: Folders collapse and any track can be resized, but a single non-folder track has no one-click collapse/minimise toggle.

## Q050-399 Keep timeline landmarks through ripple edits and export chapters

**P3 · L04 arranger, timeline, clip operations, markers, tempo map · feature · L**

Goal: Markers and sections stay attached to their timeline positions through ripple/insert-time edits and markers export as audio chapter metadata.

Why: Range ripple and insert-time code move clips but not markers or sections, and no chapter export exists; the workflow request is low demand in 1 DAW.

Acceptance:
- A regression test verifies range.delete with ripple and range.insert_time move markers and sections consistently with clips and that undo restores all positions.
- Export a project with named markers and verify WAV cue and ID3 CHAP chapter metadata appears at the expected positions.
Items:
  - `R-workflow-223` Podcast-oriented track and chapter workflow - partial (marker.add/set; section.*; src/core/ControlCommandsRange.cpp:230-291 (range.delete ripple and insert_time move clips only, not markers); rg -i chapter in src: none); complete: Markers and sections exist, but ripple delete and insert-time leave markers behind, and there are no chapter markers in exports and no podcast track roles.

## Q050-400 Store named arrangement variants in one project

**P3 · L04 arranger, timeline, clip operations, markers, tempo map · feature · L**

Goal: Users can save and switch named sets of clip placements while keeping tracks and mixer state shared.

Why: Current project alternatives are whole-session copies rather than shared track/mixer placement sets; the request is low demand in 1 DAW.

Acceptance:
- A regression test creates two named arrangements with different clip placements, switches between them, and verifies tracks, instruments, and mixer state are shared.
- Save/load preserves arrangement names and placements, and each arrangement can be exported.
Items:
  - `R-workflow-298` Multiple named arrangements per project - partial (project.alternative_save/switch/alternatives (alternatives are whole-session copies in <project>-alternatives/); track.version_save/switch (per-track clip versions)); complete: Alternatives and per-track versions give variants, but there are no multiple named arrangements inside one project that share its tracks and mix.

## Q050-401 Select all clips on the active track

**P3 · L04 arranger, timeline, clip operations, markers, tempo map · feature · S**

Goal: Users can select every clip on the active track with a shortcut without selecting clips on other tracks.

Why: The editor has song-wide select-all but no active-track-only command; the request is low demand in 1 DAW.

Acceptance:
- A GUI regression test invokes the command with clips on two tracks and verifies only the active track's clips become selected.
- The shortcut appears in the Shortcuts page and does not override text-entry or editor-local shortcuts.
Items:
  - `R-workflow-305` Select all clips on the active track by shortcut - missing (src/gui/editors/SongEditor.cpp:448,797 (select all clips in the song only); arrangement.select_clip selects one next/previous clip); implement: There is no command to select every clip on the active track.

## Q050-402 Extreme offline time stretching with Paulstretch

**P3 · L05 audio editing, warping, stretching, fades · feature · L**

Goal: Users can render a source many times longer while retaining its sustained texture for ambient and experimental work.

Why: Paulstretch is low demand from 1 DAW, and the current granular effect offers only a Paulstretch-like texture rather than a dedicated processor.

Acceptance:
- Run an offline render at an extreme expansion ratio and verify the requested duration and non-silent sustained content.
- Verify cancellation, output format, and that processing stays in the offline job rather than allocating on the audio thread.
Items:
  - `R-effects-35` Paulstretch extreme time expansion - partial (plugins/GranularPitchShifter/GranularPitchShifterControlDialog.cpp:132-136 mentions only a stationary Paulstretch-like texture; no dedicated Paulstretch effect found); complete: Granular processing can approximate a stationary texture, but there is no dedicated extreme time-expansion mode.

## Q050-403 Edit multichannel clips without dropping channels

**P3 · L05 audio editing, warping, stretching, fades · completion · L**

Goal: Users can split, trim, join, and fade N-channel clips while preserving every channel.

Why: Multichannel edit operations are low demand from 1 DAW, and the current evidence confirms sample clips are stereo while the requested work needs N-channel editing.

Depends on: Add N-channel sample clip playback and persistence
Acceptance:
- Apply split, trim, join, and fade operations to an N-channel fixture and compare every resulting channel with a reference render.
- Verify project save/load, undo/redo, and command state preserve the full channel layout.
Items:
  - `R-editing-140` Edit linked multichannel audio components - missing (docs/KNOWN-LIMITATIONS.md:2367 (a multichannel AAF clip plays its first source only); docs/SURROUND-EXPORT.md:23-24 (speaker feeds, not B-format); sample clips are stereo); implement: Clips carry at most stereo audio, so linked multichannel or Ambisonics material cannot be cut or joined as one.

## Q050-404 Let modulators drive rack macros

**P3 · L06 automation and modulation · feature · M**

Goal: Use one modulator to move a rack macro and thereby control its mapped parameters together.

Why: Demand is low (1 DAW); modulation routes resolve effect or instrument models but cannot resolve macro value targets.

Acceptance:
- A modulation route can address a macro by stable rack/macro identity and read its target and depth back.
- A test proves the macro value changes and all mapped parameters follow; removing the route and undo/redo restore prior values.
- The Modulators panel can select and edit macro routes, and project save/load preserves the binding.
Items:
  - `R-instruments-76` Macro controls driven by modulators - partial (rack.macro_target_add; modulator.target_set; both bind to plug-in parameters, but modulators cannot target macro values.); complete: Macros and modulators can drive parameters independently, but a macro cannot itself be driven by a modulator.

## Q050-405 Automate effect bypass state

**P3 · L06 automation and modulation · feature · M**

Goal: Write an automation clip that switches an effect on and off over time.

Why: Demand is low (1 DAW); plugin.bypass changes an enabled flag that is stored separately from the effect's automatable parameters.

Acceptance:
- The effect enabled flag is exposed as a discrete automatable target with only valid off/on values accepted.
- Playback changes bypass at the intended points, while project save/load and undo/redo restore the prior enabled state.
- A command-surface test creates and reads the bypass automation; fractional boolean values continue to be refused.
Items:
  - `R-effects-43` Automatable effect bypass - partial (plugin.bypass; src/core/ControlAutomationSupport.cpp:168-175 enumerates device parameters from controls; include/Effect.h:316 stores enabled separately); complete: Effect bypass is command-drivable, but the enabled flag is not included in the automation parameter list.

## Q050-406 Add a one-shot volume-envelope effect

**P3 · L06 automation and modulation · feature · M**

Goal: Shape a synth's output with a drawable, preset-saveable envelope that plays once per trigger.

Why: Demand is low (1 DAW); no dedicated one-shot volume-envelope device was found despite existing automation and cyclic modulation.

Acceptance:
- The effect exposes a drawable envelope and one-shot trigger/retrigger behavior, with no cyclic restart after the envelope completes.
- A DSP test checks the envelope against a known signal and verifies retriggering and end-of-envelope behavior.
- A preset save/load check restores the curve and mode, and the normal plugin UI exposes the same settings.
Items:
  - `R-effects-36` One-shot volume-envelope device - missing (docs/MODULATION.md:24-34 describes timeline LFO modulation; no one-shot volume-envelope device found in plugins/); implement: The modulation layer and instrument envelopes do not provide a drawable one-shot volume-envelope effect.

## Q050-407 Label the automation editor in song-relative bars

**P3 · L06 automation and modulation · feature · S**

Goal: Read bar numbers that match the song editor's position for the open automation clip.

Why: Demand is low (1 DAW); the supplied evidence does not establish the current ruler behavior, so this unit starts with a targeted reproduction.

Acceptance:
- First reproduce whether the ruler uses clip-local numbering; if it does, show song-relative bar labels for clips at nonzero song positions.
- A GUI test opens clips at two different song positions and confirms ruler labels match the arrangement bar numbers.
- Zooming and horizontal scrolling preserve the correct labels.
Items:
  - `R-ui-80` Automation editor uses song-relative position labels - unknown (src/gui/editors/AutomationEditor.cpp (ruler labelling not established)); complete: Not established whether the automation editor's ruler shows song bars or clip-local bars.

## Q050-408 Use consistent parameter value formatting

**P3 · L06 automation and modulation · feature · S**

Goal: See the same human-readable value, including units such as dB, in tooltips, menus, and automation editing.

Why: Demand is low (1 DAW); AutomationEditor already calls displayValue for scale labels, but consistency with tooltip and context-menu paths is unverified.

Acceptance:
- Compare a dB-valued parameter in its tooltip, context menu, and Automation Editor and confirm each uses the model's display formatter.
- Add a GUI regression test that covers formatted values before and after an automation edit.
- If the current paths already agree, record the reproduction result and close the unit without changing the formatter.
Items:
  - `R-ui-85` Use consistent human-readable parameter values - unknown (src/gui/ModelView.cpp (value formatting paths not compared)); complete: Not established that tooltip, context menu and automation editor show the same formatted value.

## Q050-409 Scriptable MIDI transform device

**P3 · L07 note editor, MIDI editing, scales, chords, articulations · feature · L**

Goal: Run user-authored, sandboxed MIDI transform scripts in the note-effect chain with explicit memory, event-count, and time limits.

Why: Low demand across one DAW; the requested script device has no runtime interface and builds on the MIDI effect chain.

Depends on: MIDI note-effect chain
Acceptance:
- A test script transforms a note deterministically and a second script exceeds each budget and is stopped safely.
- A failed script cannot leave a note held or corrupt the project, and its state is undoable.
- Realtime allocation and locking checks cover the script-to-event path.
Items:
  - `R-instruments-71` Scriptable MIDI transforms - partial (script.run; note.randomize; chord.progression_generate; no real-time MIDI-transform plug-in interface found in plugins/ or src/.); complete: Lua scripts and note-edit commands can change project data, but scripts cannot transform incoming MIDI in real time.

## Q050-410 Negative-delay articulations

**P3 · L07 note editor, MIDI editing, scales, chords, articulations · feature · M**

Goal: Set a saved per-articulation pre-roll so its keyswitch or program event is scheduled before the note onset.

Why: Low demand across one DAW; articulation events currently use track delay only and maps have no per-entry lead time.

Depends on: Articulation map editor
Acceptance:
- A MIDI event trace shows the articulation event at the configured pre-roll before the note.
- Zero pre-roll preserves existing timing and negative values respect the track's scheduling bounds.
- The value survives save, reload, undo, and playback-loop tests.
Items:
  - `R-instruments-56` Negative-delay articulations - partial (track.articulations_set; include/ArticulationMap.h:40-48 has no per-articulation lead time; src/tracks/InstrumentTrackNotes.cpp:195-199 applies only track delay.); complete: Articulation keyswitches and program changes are sent before notes, but their lead time cannot be set per articulation.

## Q050-411 Arrangement analyzer lane for loudness and speech intelligibility

**P3 · L08 mixer, metering, channel strip · feature · L**

Goal: Users can inspect short-term loudness and a clearly labeled speech-intelligibility estimate over time for a chosen channel.

Why: This low-demand analysis request is reported by 1 DAW; current loudness is master/file based and no timeline analysis lane exists.

Acceptance:
- An offline analysis lane plots short-term loudness and the estimated intelligibility measure against arrangement time for the selected channel.
- A speech fixture produces stable results and the UI labels the intelligibility value as an estimate with its analysis settings.
- The agent command reports analysis range, channel, and result data without changing project audio.
Items:
  - `R-mixing-51` Nuendo analyzer track - partial (meter.get_state (master only, live), meter.measure_file; rg 'intelligib|loudness.?(history|graph|track|timeline)' over src/include/docs: no match); complete: Loudness can be measured live on the master or on a rendered file, but there is no analyzer lane plotting loudness over the timeline and no dialogue-intelligibility measure.

## Q050-412 Live spectrogram and per-channel loudness analysis

**P3 · L08 mixer, metering, channel strip · completion · M**

Goal: Users can inspect a live spectrogram and per-channel loudness in the existing analysis devices.

Why: This low-demand analysis request is reported by 1 DAW; existing tools are split across scopes and lack live spectrogram plus per-channel loudness together.

Acceptance:
- A live tone/noise fixture produces the expected spectrogram time/frequency bins and channel-specific loudness values.
- The analysis device exposes selection and read-back through the UI and agent surface without altering the signal path.
Items:
  - `R-mixing-52` Provide a general audio-analysis device - partial (plugins/SpectrumAnalyzer, plugins/Oscilloscope, plugins/Vectorscope; window.phase_scope; tuner.get_state; sample.spectrogram (clips only)); complete: Scope, spectrum, goniometer and pitch detection exist as separate tools, but there is no live spectrogram device and loudness is metered on the master only.

## Q050-413 Non-invasive chain-slot signal probe

**P3 · L08 mixer, metering, channel strip · feature · M**

Goal: Users can inspect level and spectrum before or after a chosen effect without inserting or moving a device.

Why: This low-demand request is reported by 1 DAW; insertable spectrum analysis exists, but no non-invasive chain-tap command is present.

Acceptance:
- The probe selects a channel and chain slot and displays pre/post level and spectrum through UI and agent state.
- An audio test confirms enabling or moving the probe leaves the rendered signal and chain order unchanged.
- A headless test verifies tap selection and read-back.
Items:
  - `R-mixing-54` Signal Probe - partial (plugins/SpectrumAnalyzer (insertable at any chain slot); mixer.strip_get (pre-fader peak/RMS); window.phase_scope (channel output only); no probe/tap command in commands_snapshot.json); complete: Signal can be inspected by inserting an analyzer device or reading strip meters, but there is no probe that taps any chain stage pre/post without adding a device.

## Q050-414 Higher-order Ambisonics bus format and encoder

**P3 · L08 mixer, metering, channel strip · feature · L**

Goal: Users can create higher-order Ambisonics buses beyond 7.1 and encode mono sources into them.

Why: The XL Ambisonics request is low demand from 1 DAW; current channel formats stop at 7.1 and no Ambisonics support is present.

Acceptance:
- A project can select and save a documented higher-order Ambisonics channel format beyond 7.1.
- An encoder device places test sources into the expected Ambisonics channels and exposes its controls through the agent surface.
- Tests cover channel count/order and save/load of format and encoder state.
Items:
  - `R-platform-55` Support high-channel-count immersive audio - partial (mixer.channel_format (stereo, lcr, quad, 5.1, 7.1); mixer.surround_pan; record.input_set (1..128 inputs); rg ambisonic over src/ include/ found nothing; docs/KNOWN-LIMITATIONS.md:2310-2311); complete: Zene handles up to 7.1 channel formats and 128 capture inputs, but has no Ambisonics format and no tracks or outputs beyond eight channels.

## Q050-415 Many-to-many channel assignment matrix

**P3 · L09 routing, buses, sends, sidechain, I/O · completion · L**

Goal: Assign each source channel to one, several, or no destination channels with a shared trim, using both the matrix view and the agent surface.

Why: port.set_pin is a per-device pin matrix, not a mixer-bus assignment matrix; demand is low and the request cites 1 DAW.

Depends on: N-channel routing graph and per-channel sends
Acceptance:
- The routing matrix and a registered command set and read back source-to-destination channel assignments and trim.
- An audio test verifies one-to-many, muted, and trimmed mappings; project save/load and one-step undo preserve the map.
Items:
  - `R-mixing-49` Many-to-many multichannel routing matrix - partial (port.set_pin (per-device pin matrix), mixer.surround_pan; docs/KNOWN-LIMITATIONS.md:2401-2406 (surround extra channels limited)); complete: Pins can be routed inside a device and surround channels can be panned, but no matrix assigns each channel of a multichannel track to one, several or no destination channels with a trim.

## Q050-416 Measure and compensate external-instrument latency

**P3 · L09 routing, buses, sends, sidechain, I/O · completion · M**

Goal: Measure a hardware instrument's MIDI-out/audio-return round trip and apply the compensation to that instrument.

Why: Current track delay is a manual per-track tick setting and the request asks for automatic external-instrument measurement; demand is low from 1 DAW.

Depends on: External Instrument device
Acceptance:
- A simulated loopback reports the measured round-trip latency on the External Instrument device and compensates playback by that amount.
- A rendered alignment test compares external-return and software-instrument transients before and after compensation, and the result persists across save/load.
Items:
  - `R-recording-66` Compensate hardware monitoring latency - partial (track.set_delay (positive 0..500 ms per instrument track, src/tracks/InstrumentTrackNotes.cpp:198); transport.low_latency; track.set_monitor); complete: Manual positive note delay and low-latency monitoring exist; no automatic external-device latency compensation.

## Q050-417 Save sidechain routing with effect presets

**P3 · L09 routing, buses, sends, sidechain, I/O · completion · M**

Goal: Save and recall a plug-in preset together with the sidechain routes required for that preset.

Why: Effect preset state and mixer sidechain routes are stored separately (KNOWN-LIMITATIONS.md:2048-65); demand is low from 1 DAW.

Acceptance:
- Save an effect preset with a sidechain source, change both plug-in state and routing, then recall and verify both restore.
- Undo and redo the linked recall as one user-visible operation without changing unrelated sends.
Items:
  - `R-effects-44` Preset recall with sidechain routing state - partial (plugin.preset_save; mixer.sidechain_to; docs/KNOWN-LIMITATIONS.md:2048-2065 describes routing state separately); complete: Device presets and sidechain routing exist independently, but a device preset does not package its host routing.

## Q050-418 Global and per-port MIDI output offsets

**P3 · L09 routing, buses, sends, sidechain, I/O · completion · S**

Goal: Apply a tempo-independent millisecond offset, including negative values, to physical MIDI output globally or per port.

Why: track.set_delay is per-track and tick-based, with no global or per-port millisecond offset; demand is low from 1 DAW.

Acceptance:
- A MIDI output test measures positive and negative offsets in milliseconds across two project tempos and verifies per-port override precedence.
- Offset settings persist in the project or user configuration as documented and are readable through the command surface.
Items:
  - `R-recording-79` Global hardware MIDI output delay - partial (track.set_delay (per-track positive 0..500 ms only, include/InstrumentTrack.h:96)); complete: Per-track positive millisecond delay exists; no global negative MIDI-output offset.

## Q050-419 Ganged send level adjustment

**P3 · L09 routing, buses, sends, sidechain, I/O · completion · S**

Goal: Adjust several sends to one return proportionally from sends-on-faders or a command.

Why: sends_on_faders and mixer.send_many exist, but mixer.adjust only adjusts channel faders; demand is low from 1 DAW.

Acceptance:
- mixer.adjust accepts send targets for a selected return and updates their levels proportionally.
- Ctrl-drag in sends-on-faders adjusts the selected sends, one undo restores all levels, and channel faders remain unchanged.
Items:
  - `R-mixing-50` More direct send and receive controls - partial (window.mixer_view (sends_on_faders, 'routing' view); src/gui/MixerView.cpp:345-347; mixer.send_many; mixer.adjust (channel faders only)); complete: Sends-on-faders and a routing view give dedicated send/receive layouts, but there is no bulk proportional adjustment of several send levels at once.

## Q050-420 Machine presets for hardware insert routing

**P3 · L09 routing, buses, sends, sidechain, I/O · completion · M**

Goal: Save named hardware-effect I/O presets in machine configuration and apply their send, return, latency and mix values to a channel.

Why: mixer.hardware_insert already stores per-channel values but there are no named machine-level presets; demand is low from 1 DAW.

Acceptance:
- A hardware insert preset can be saved, listed, applied and deleted through the command surface and the mixer UI.
- Applying a preset restores send, return, latency and mix while retaining the project's current channel routing after save/load.
Items:
  - `R-workflow-304` Save external-effect routing presets - partial (mixer.hardware_insert (send/return/mix/latency per channel), mixer.hardware_insert_ping; audio.io_setup named buses per device); complete: Hardware inserts and named per-device I/O buses exist, but an insert's setup cannot be saved as a named machine-level external effect and recalled in another project.

## Q050-421 Pitch-shifter algorithm and quality controls

**P3 · L10 devices, instruments, effects, plug-in hosting · feature · M**

Goal: Users can select a pitch-shifting algorithm and trade latency for quality.

Why: GranularPitchShifter has no alternate algorithm selector; this low-demand request is from 1 DAW.

Acceptance:
- Render a fixed pitch-shift fixture with each algorithm and report latency and error.
- Persist the selected mode and expose it through the control surface.
Items:
  - `R-effects-34` Live audio pitch shifting - partial (plugins/GranularPitchShifter/GranularPitchShifterControlDialog.cpp:119-136; no alternate pitch-shift algorithm selector found); complete: The granular shifter is present, but the requested selectable low-latency and high-quality algorithms are absent.

## Q050-422 Low-latency convolution mode

**P3 · L10 devices, instruments, effects, plug-in hosting · feature · L**

Goal: Users can choose a lower-latency convolution mode and understand its CPU tradeoff.

Why: ConvolutionReverb always reports one partition of latency; the low-demand request is from 1 DAW.

Acceptance:
- Compare impulse response, latency, and CPU for direct and partitioned modes.
- Persist the mode and display measured latency in device state.
Items:
  - `R-effects-38` Convolution and multiband effect processing - partial (plugins/ConvolutionReverb/ConvolutionReverb.h:62-80 explicitly reports one partition of latency); complete: Convolution reverb and rack band splitting exist, but the convolver is intentionally partition-late rather than zero-latency.

## Q050-423 Speech-processing chain preset

**P3 · L10 devices, instruments, effects, plug-in hosting · completion · M**

Goal: Users can start a speech-processing chain with de-essing, leveling, metering, limiting, and ducking.

Why: The necessary devices and meters exist but there is no linked speech workflow; demand is low across 1 DAW.

Acceptance:
- Apply the preset to a speech fixture and verify every listed device is present and enabled.
- Save/load the chain and document the intended level and ducking ranges.
Items:
  - `R-effects-39` Speech-oriented dynamics and metering - partial (plugins/DeEsser/DeEsser.cpp:44-74; plugins/Compressor/; include/LufsMeter.h:40-50; include/MasteringChain.h:44-67); complete: De-essing, denoising, dynamics, loudness and true-peak tools exist separately, but there is no integrated speech-chain preset or workflow.

## Q050-424 Rule-based guitar accompaniment player

**P3 · L10 devices, instruments, effects, plug-in hosting · feature · L**

Goal: Users can perform a non-ML guitar accompaniment pattern that follows chord changes.

Why: No guitar Session Player exists; the source explicitly asks for a non-ML device and is low demand from 1 DAW.

Acceptance:
- Play a documented starter style over changing chords and verify note timing and bar alignment.
- Expose chord/style controls and read-back through the agent surface.
Items:
  - `R-instruments-64` More guitar-oriented Session Players and genre coverage - partial (chord.progression_generate; note.randomize; no guitar Session Player or style-driven accompaniment device found in plugins/.); complete: Chord and note-generation helpers exist, but there is no responsive guitar accompaniment player or genre library.

## Q050-425 Phrase sampler

**P3 · L10 devices, instruments, effects, plug-in hosting · feature · L**

Goal: Users can capture, slice, and perform reusable audio phrases from a device.

Why: The product has sample slicing but no reusable phrase sampler; the request is low demand from 1 DAW.

Acceptance:
- Capture or import a phrase, slice it, trigger slices in order, and verify timing in a render.
- Save/load phrase and slice state and expose controls to agents.
Items:
  - `R-instruments-57` Built-in phrase sampler - partial (clip.slice_to_instrument; session.set_slot launches MIDI or audio clips; no phrase-sampler instrument or phrase capture command found.); complete: Session clips and sample slicing can trigger phrases, but there is no dedicated phrase capture and playback instrument.

## Q050-426 Optional acoustic drum instrument

**P3 · L10 devices, instruments, effects, plug-in hosting · feature · L**

Goal: Users can play an optional acoustic drum kit with velocity layers and round robins.

Why: No high-end acoustic multisampled kit is bundled; the request is low demand from 1 DAW.

Depends on: Multisample velocity and round-robin mapping
Acceptance:
- Load the kit and verify velocity transitions and round-robin variation in a deterministic fixture.
- Package the library as optional content with a clear size and licence manifest.
Items:
  - `R-instruments-59` High-end acoustic drum instrument gap - partial (plugin.list; plugins/Sf2Player/; plugins/AudioFileProcessor/; no bundled high-end acoustic multisample kit found.); complete: Sample instruments and compatible third-party hosts exist, but Zene does not ship a high-end acoustic drum kit.

## Q050-427 Stock instrument loudness guidance

**P3 · L10 devices, instruments, effects, plug-in hosting · completion · M**

Goal: Users can choose stock instruments with consistent default output levels.

Why: There is no cross-instrument level calibration or policy; this low-demand request is from 1 DAW.

Acceptance:
- Measure the named stock presets with the same fixture and publish their integrated loudness and peak range.
- Adjust only defaults that exceed the agreed tolerance and keep a regression render.
Items:
  - `R-instruments-62` Consistent default instrument loudness - partial (sample.normalize; plugin.load; no cross-instrument loudness calibration or default-level policy found in plugins/ or docs/.); complete: Sample normalization exists, but default output loudness is not calibrated consistently across instruments.

## Q050-428 SoundFont audition mode

**P3 · L10 devices, instruments, effects, plug-in hosting · feature · M**

Goal: Users can audition an alternate SoundFont project-wide without losing each track’s prior bank.

Why: SoundFont loading and per-instance presets exist but no temporary project-wide audition mode; demand is low from 1 DAW.

Acceptance:
- Switch the audition SoundFont and restore all original banks on cancel or project close.
- Verify track program state and saved projects remain unchanged.
Items:
  - `R-instruments-61` Audition MIDI with alternate SoundFonts - partial (plugins/Sf2Player/Sf2Player.cpp:186-191 loads the default SoundFont for new instances; plugin.preset_list and plugin.preset_load operate per instance.); complete: SoundFonts can be loaded per instrument, but there is no project-wide audition swap for all MIDI tracks.

## Q050-429 Keyboard preset audition

**P3 · L10 devices, instruments, effects, plug-in hosting · feature · S**

Goal: Users can audition plug-in presets with arrow keys and Space while browsing.

Why: Preset-list/load commands exist but plug-in preset browsing has no audition behavior; demand is low from 1 DAW.

Acceptance:
- Arrow-key navigation loads and auditions adjacent presets; Space previews and Escape restores the prior preset.
- Verify preset browsing remains available to agents.
Items:
  - `R-instruments-63` Audition presets while browsing with arrow keys - partial (plugin.preset_list; plugin.preset_load; browser.preview auditions audio files, but no plug-in preset auto-audition command or behavior was found.); complete: Presets can be listed and loaded, but keyboard navigation does not audition each preset automatically.

## Q050-430 Independent synth and filter envelopes

**P3 · L10 devices, instruments, effects, plug-in hosting · feature · M**

Goal: Users can shape amplitude and filter envelopes independently in stock synths that currently share one.

Why: At least one stock synth family still shares an envelope; the request is low demand from 1 DAW.

Acceptance:
- Expose separate envelope values on a synth that currently shares them and render distinct amplitude/filter sweeps.
- Save/load the values and provide command read-back.
Items:
  - `R-instruments-68` Independent synth and filter envelopes - partial (plugins/Monstro/Monstro.cpp:1403-1433 implements two independent envelopes; no Vaporisateur plug-in is present in plugins/.); complete: Some stock synths have independent envelopes, but this capability is not a consistent control model across instruments.

## Q050-431 Embedded Zyn editor mode

**P3 · L10 devices, instruments, effects, plug-in hosting · feature · M**

Goal: Users can edit the full ZynAddSubFX interface inside a docked device panel.

Why: The full Zyn UI opens separately from the in-app track view; this request is low demand from 1 DAW.

Acceptance:
- Open and close the full editor dock without losing the existing compact track view.
- Verify editor lifetime and project reopen behavior.
Items:
  - `R-instruments-60` Embedded ZynAddSubFX editor - partial (plugins/ZynAddSubFx/ZynAddSubFx.cpp:583-595,735-751 provides an in-app track view and a toggle that opens the full plug-in UI separately.); complete: Zyn has an in-app track view, but its complete editor still opens in a separate window.

## Q050-432 MIDI output bank select

**P3 · L10 devices, instruments, effects, plug-in hosting · feature · S**

Goal: Users can select a MIDI output bank and program by named MSB/LSB values.

Why: MIDI output exposes program selection without bank MSB/LSB; this low-demand request is from 1 DAW.

Acceptance:
- Set MSB, LSB, and program and verify the emitted MIDI order on the chosen channel.
- Save/load the setting and cover both bank-before-program and program-before-bank modes.
Items:
  - `R-editing-160` Rosegarden 26.06 focuses on notation and MIDI polish - partial (include/MidiPort.h:67 output program only; no bank-select command in commands_snapshot.json; window.note_editor_set new_velocity remembers a new-note velocity); complete: Program change goes out to external synths, but there is no bank select (MSB/LSB or alternative methods); the notation fixes are owner-deferred.

## Q050-433 Carry effect-chain presets with project handoff

**P3 · L11 browser, samples, presets, library · feature · S**

Goal: Users can hand a project to another machine with the effect-chain presets used by that project available there.

Why: This is a low-demand request from one DAW, and the current project handoff copies media while chain presets remain per-user.

Acceptance:
- A two-config-directory handoff test copies or embeds only the chain presets referenced by the session.
- Opening the handoff on a clean second profile lists and applies the transferred presets with the same device order and settings.
- A conflicting preset name or unavailable plug-in is reported without silently overwriting unrelated user presets.
Items:
  - `R-interop-59` Portable effect-chain presets - partial (docs/KNOWN-LIMITATIONS.md:866-878 (chain presets live per user in <userPresets>/chainpresets, not in the project); chain.save/apply; project.handoff copies media only); complete: Chain presets aren't carried with the project, so another machine may lack them.

## Q050-434 Choose whether MIDI import applies tempo and meter

**P3 · L12 projects, files, import/export, interchange, recovery · feature · S**

Goal: Choose from the command surface or Import dialog whether incoming MIDI tempo and time signatures change the project.

Why: The row reports low demand from one DAW and a missing import preference, with no data-loss or promise-gap signal.

Acceptance:
- Import one MIDI fixture with tempo/meter events using apply and ignore choices and verify project tempo maps accordingly.
- Confirm the File > Import control exposes the same option as project.import.
Items:
  - `R-interop-60` MIDI import tempo and time-signature policy - partial (plugins/MidiImport/MidiImport.cpp:297-351 (time signature and tempo always written as automation); project.import args: path only); complete: MIDI import always applies the file's tempo and meter, with no way to opt out.

## Q050-435 Broadcast MXF ingest and ADR overlays

**P3 · L12 projects, files, import/export, interchange, recovery · feature · L**

Goal: Ingest broadcast MXF media with FFmpeg presets and display marker-derived cue overlays for ADR work.

Why: The row reports low demand from one DAW and lists MXF ingest, export presets, and ADR overlays as absent.

Depends on: Export a selected video range with muxed audio
Acceptance:
- Import supported MXF audio/video fixtures and verify selected stream, duration, and sample/frame alignment.
- Show and hide marker-derived ADR cue overlays in the video window and verify presets persist across reopen.
- Verify the import and marker-cue state are readable through registered video commands in headless mode.
Items:
  - `R-interop-57` Broadcast video and ADR tools - partial (video.attach / video.window_show / video.extract_audio; docs/KNOWN-LIMITATIONS.md:2366 (MXF essence not decoded), :2372-2377 (one video, absolute path); no video export or ADR commands); complete: Video playback works, but there's no MXF ingest, no video export presets and no ADR cue or streamer overlays.

## Q050-436 Reorder render queue jobs

**P3 · L12 projects, files, import/export, interchange, recovery · feature · S**

Goal: Move a queued render to a chosen position from the command surface or by dragging it in the queue UI.

Why: The row reports low demand from one DAW and identifies queue reordering as the remaining gap in an otherwise working queue.

Acceptance:
- Call render.queue_move with a job and index and verify render.queue_list returns the new order.
- Drag a job in the queue UI and verify execution follows the displayed order without reopening the active project.
Items:
  - `R-workflow-227` Render queue should reuse the open project - partial (src/core/RenderQueue.cpp:238-269 (each job is a child render of a snapshot - the open session is not closed); render.queue_add/cancel/clear/list; no reorder command); complete: Queued renders already run in the background without closing the open project, but jobs cannot be reordered.

## Q050-437 Restore deleted tracks without undoing later work

**P3 · L12 projects, files, import/export, interchange, recovery · feature · M**

Goal: Review deleted tracks with source and deletion time and restore one without rolling back unrelated edits.

Why: The row reports low demand from one DAW and no deleted-track recovery bin in the current project workflow.

Acceptance:
- Delete a track, make later edits, restore the deleted track from the visible bin, and verify later edits remain.
- Close and reopen the project and confirm retained deleted-track entries remain available until explicitly removed.
- Expose list/restore operations through registered commands and verify the UI uses those same operations.
Items:
  - `R-workflow-193` Recover deleted tracks from a visible bin - partial (window.undo_history (src/gui/ControlCommandsShell.cpp:254); revisions.restore (src/gui/CommandLists.cpp:38); rg found no deleted-track bin); complete: A deleted track comes back only through undo or a revision, with no list of deleted tracks to restore from.

## Q050-438 Document the existing revisions UI

**P3 · L12 projects, files, import/export, interchange, recovery · docs · S**

Goal: Describe the Revisions list, its available sources, and restore behavior accurately in the known-limitations and user documentation.

Why: The row reports low demand from one DAW and an already-implemented Revisions list with a stale limitation statement.

Acceptance:
- Verify the documented Revisions list matches the current GUI command/menu and revisions.list sources.
- Search KNOWN-LIMITATIONS for the obsolete claim that the GUI has no revisions UI and verify it is corrected.
Items:
  - `R-workflow-219` Named project versions with a recoverable history - have (project.save_version, project.alternative_save/switch (src/gui/LibraryCommandMenus.cpp:231, src/gui/CommandLists.cpp:379-385), revisions.list/restore in GUI (src/gui/CommandLists.cpp:34-38); docs/KNOWN-LIMITATIONS.md:1783-1786 says there is no revision UI); docs: Named alternatives, numbered versions and a restorable revision list all exist in the interface, but KNOWN-LIMITATIONS still says the revision timeline has no UI.

## Q050-439 Inspect project settings, assets, metadata, and cleanup

**P3 · L12 projects, files, import/export, interchange, recovery · feature · M**

Goal: Review project settings, metadata/artwork, asset status, unused media, and editing time in one Project Inspector.

Why: The row reports low demand from one DAW and confirms existing settings/metadata/asset commands lack a unified project view.

Acceptance:
- Open the Inspector and verify settings, metadata/artwork, missing/unused assets, and tracked editing time for a fixture project.
- Clean an unused asset through a recoverable/undoable action and verify active project references remain intact.
- Verify metadata and asset queries remain available through registered project commands and the Inspector calls the same handlers.
Items:
  - `R-workflow-301` Project Inspector - partial (src/gui/LibraryCommandMenus.cpp:217-219 (Project Settings..., Project Info...); project.metadata; project.missing_assets/hash_assets; no artwork, editing-time or unused-file cleanup); complete: Settings and metadata dialogs and asset reports exist separately, but there is no single inspector with artwork, editing time and unused-media cleanup.

## Q050-440 Search project notes in the library

**P3 · L12 projects, files, import/export, interchange, recovery · feature · S**

Goal: Find projects by text stored in their project notes using browser.query/search.

Why: The row reports low demand from one DAW and existing project notes that the browser index does not search.

Acceptance:
- Add searchable notes to two projects and verify browser.query returns only the matching project.
- Edit and clear notes, rebuild/update the index, and verify stale note hits disappear.
Items:
  - `R-workflow-302` Project-level notes and searchable production notebook - partial (src/gui/ControlCommandsWindow.cpp:72 (Project Notes window, saved with the project); project.metadata 'comment'; browser.query does not search notes); complete: Rich-text project notes are kept with each project, but they cannot be searched from the library.

## Q050-441 Batch rename selected tracks and clips

**P3 · L12 projects, files, import/export, interchange, recovery · feature · M**

Goal: Rename selected tracks and clips with numbering or find/replace, optionally renaming their recorded files safely.

Why: The row reports low demand from one DAW and says the research found little direct request evidence for batch rename.

Acceptance:
- Preview a numbered and find/replace rename over mixed tracks/clips and verify the exact proposed names before applying.
- Apply, undo, and redo a rename including a recorded-file option and verify no unrelated file is changed.
- Expose the rename preview/apply operation through a registered command and verify the UI uses the same result.
Items:
  - `R-workflow-295` Batch file renaming - missing (No batch rename: rename commands are single-item (track.rename, mixer.rename_channel, ...); rg 'batch rename|renameTracks' in src/gui: none); implement: Tracks, clips and recorded files can only be renamed one at a time.

## Q050-442 High-contrast track movement grip

**P3 · L13 customisation, shortcuts, themes, layout · fix · S**

Goal: See the track movement handle clearly in standard and high-contrast themes.

Why: The one-DAW accessibility report finds fixed pixmaps that do not follow the palette and are outside the contrast audit.

Acceptance:
- Run the GUI contrast checker against the grip in standard and high-contrast palettes and verify it meets the existing threshold.
- Change the palette foreground role and confirm the grip updates with it.
Items:
  - `R-accessibility-37` Track movement handle visibility - partial (src/gui/tracks/TrackGrip.cpp:53-58 (fixed pixmaps track_op_grip/_c, not palette-driven)); fix: The track move grip is a fixed pixmap, so the high-contrast palette does not strengthen it.

## Q050-443 Touch gestures for the patcher

**P3 · L14 help, onboarding, accessibility · feature · M**

Goal: Connect devices and move patcher nodes using direct touch gestures.

Why: The touch-gesture request is low demand and depends on the shared touch workspace layout.

Depends on: Touch-first workspace layout
Acceptance:
- Use a touch drag to connect compatible ports and a long press to move a node in the patcher.
- Verify invalid connections are refused, existing wires remain intact, and touch hit targets meet the touch workspace size.
- Add Qt touch-event tests for connection, node movement, and cancellation.
Items:
  - `R-accessibility-30` Touch gestures for modular performance - partial (window.appearance density touch; docs/KNOWN-LIMITATIONS.md:2422 (pinch zoom); patcher.set_wiring); complete: Pinch zoom and touch density exist, but the patcher has no touch drag-to-connect or long-press gestures.

## Q050-444 Stage 2: gaze and switch input for sequencer and melody

**P3 · L14 help, onboarding, accessibility · feature · L**

Goal: Use calibrated gaze with switch confirmation to select sequencer cells and enter melody notes.

Why: R-instruments-95 is an XL, low-demand request for configurable gaze and switch input across the sequencer and melody controls.

Depends on: Stage 1: keyboard and switch-accessible step sequencer
Acceptance:
- Calibrate and map gaze movement to sequencer and melody targets, with a switch to confirm or cancel each action.
- Verify a simulated gaze provider can select a step and enter/release a melody note without accidental repeated triggers.
- Run tests for calibration bounds, target focus, confirmation, cancellation, and device loss.
Items:
  - `R-instruments-95` Gaze and alternative music input - missing (window.accessibility_report; searched src/gui/, include/ and plugins/ for gaze, eye-tracking or switch-input adapters.); implement: Accessible mode and keyboard control exist, but Zene has no gaze-based or switch-driven music input surface.

## Q050-445 Provide a native Linux package for bridge users

**P3 · L15 platform: performance, stability, Linux audio, install, hardware controllers · feature · M**

Goal: Linux users can install Zene in a native package that uses system bridge runtimes.

Why: Only AppImage/makeself packaging is identified, leaving native-package users without a documented bridge-friendly option; demand is low.

Acceptance:
- A native package installs and launches on its target distribution with system bridge runtimes discoverable.
- Package metadata and installation/removal steps are documented.
Items:
  - `R-hosting-13` Native-package alternative for bridge users - partial (cmake/linux/LinuxDeploy.cmake:1-4 provides AppImage/makeself packaging; no native deb/rpm package target found); complete: A portable AppImage and source installation are available, but there is no native Linux package alternative for bridge users.

## Q050-446 Add named hardware synth patch maps

**P3 · L15 platform: performance, stability, Linux audio, install, hardware controllers · feature · M**

Goal: Users can select a named bank and program on supported external hardware synths.

Why: Program-change output exists, but named hardware bank/program maps do not; demand is low.

Acceptance:
- A MIDI-output test verifies bank-select and program-change bytes for a named patch map.
- A controller action selects a map entry and state reads back the chosen bank/program.
- One hardware-map example and its editing format are documented.
Items:
  - `R-instruments-58` Hardware synth patch control and reliable MIDI timing - partial (track.articulations_set supports program changes; clip.controllers_set supports MIDI program-change points; midi.device_list lists devices.); complete: Hardware MIDI output and program changes work, but named bank maps and controller-friendly patch selection remain limited.

## Q050-447 Provision ONNX Runtime in release jobs

**P3 · L15 platform: performance, stability, Linux audio, install, hardware controllers · completion · M**

Goal: Stem separation uses the in-process ONNX backend on every supported release platform, including Windows Arm64.

Why: The queue notes that release jobs do not provision the SDK and users may fall back to Python; demand is low.

Acceptance:
- Each supported release job provisions the matching ONNX Runtime SDK and builds the in-process backend.
- Platform CI verifies stem.get_state reports the in-process backend available, including Windows Arm64.
Items:
  - `R-platform-71` Stem separation availability can lag on a supported platform - partial (stem.get_state; docs/KNOWN-LIMITATIONS.md:1428-1431 (without the ONNX Runtime SDK the external backend needs python3 with onnxruntime); rg onnx over .github/workflows/build.yml found no SDK provisioning for release jobs); complete: Stem separation has no hardware gate, but release builds do not bundle ONNX Runtime, so users on every platform need a separate Python install.

## Q050-448 Measure MIDI round-trip latency

**P3 · L15 platform: performance, stability, Linux audio, install, hardware controllers · feature · M**

Goal: Users can measure MIDI input/output round-trip latency beside audio-device latency.

Why: The queue finds no MIDI latency measurement and rates demand low; audio latency reporting is the prerequisite comparison.

Depends on: Expose track CPU, dropout and audio-device diagnostics
Acceptance:
- A MIDI loopback ping reports measured round-trip time and uncertainty for a selected port.
- The result is visible beside audio latency in the diagnostics UI and through a headless command; missing loopback hardware returns a typed refusal.
Items:
  - `R-recording-75` Audio-thread MIDI latency needs its own measurement - missing (commands_snapshot.json midi.* (clients_list, device_list, learn, reconnect, retro_capture) - no MIDI latency measurement); implement: There is no way to measure external MIDI round-trip or output latency separately from audio.

## Q050-449 Add project-rate audio conversion

**P3 · L15 platform: performance, stability, Linux audio, install, hardware controllers · feature · L**

Goal: Users can choose a project sample rate and record or play at a different device rate with reported conversion latency.

Why: No project sample-rate setting or device-rate conversion is identified, and demand is low.

Acceptance:
- Realtime input/output tests convert between project and device rates without audio-thread allocation, locking or buffer growth.
- 44.1, 48 and 96 kHz fixtures preserve pitch and duration within the stated resampler tolerance.
- The chosen project rate and converter latency persist and are readable through a registered command.
Items:
  - `R-recording-80` Realtime project sample-rate conversion - missing (rg projectSampleRate/project sample rate in src/core include: no hits; audio.device_set); implement: Projects have no own sample rate with realtime conversion to the device rate.

## Q050-450 Bind transport actions to MIDI controllers

**P3 · L15 platform: performance, stability, Linux audio, install, hardware controllers · feature · S**

Goal: Users can bind MIDI notes or CCs to play, stop and record.

Why: MCU transport buttons exist, but notes cannot bind play/stop/record; demand is low.

Acceptance:
- MIDI learn binds note and CC events to play, stop and record; tests verify each action and the saved mapping.
- Invalid bindings are refused with a typed result and the actions remain available to controller scripts.
Items:
  - `R-recording-81` Start, stop, and record from a MIDI controller - partial (src/core/McuSurface.cpp:222-224 (Mackie Play/Stop/Record); docs/KNOWN-LIMITATIONS.md:2691-2693 (play/stop cannot be bound to a note)); complete: Transport buttons work on a Mackie surface, but generic MIDI controls cannot be bound to play/stop/record.

## Q050-451 Show live JACK and PipeWire graph connections

**P3 · L15 platform: performance, stability, Linux audio, install, hardware controllers · feature · M**

Goal: Users can see which external JACK/PipeWire ports are connected to each Zene port and notice external changes.

Why: JACK startup auto-connect exists, but the queue finds no live-connection reporting; demand is low.

Acceptance:
- An integration test reads live JACK port connections and reports connect/disconnect changes after startup.
- The audio settings page distinguishes external graph links from Zene’s internal routing and exposes state through audio.io_list.
- Unsupported backends report that external connection state is unavailable.
Items:
  - `R-workflow-222` PipeWire graph links must be managed by session tools - partial (src/core/audio/AudioJack.cpp:252-262 (attemptToConnect at start), :577-597 (jack_get_ports for the setup widget); no jack_port_get_connections anywhere; audio.io_list reports internal routing only); complete: Zene can connect its ports to chosen system ports at start, but it never reads back which ports are actually connected now or whether a session manager rewired them.

## Q050-452 MIDI-map Beat+Bassline pattern steps

**P3 · L15 platform: performance, stability, Linux audio, install, hardware controllers · feature · M**

Goal: Users can learn a MIDI note or CC for a Beat+Bassline step and toggle that step from a controller.

Why: The queue finds no step-cell mapping path and rates demand low; current launcher and parameter learn do not cover pattern steps.

Acceptance:
- A MIDI learn test toggles the assigned step and leaves other steps unchanged.
- Mappings persist through save/load and are visible in the step editor and control-socket state.
- Undo restores the prior step and mapping.
Items:
  - `R-workflow-297` MIDI-map Beat+Bassline step buttons - missing (session.bind binds notes to launcher actions only; controller.map maps parameter models; no learn/mapping for pattern step cells (rg learn in src/gui/clips/MidiClipView.cpp, src/gui/editors/PatternEditor.cpp: none)); implement: Beat+Bassline step cells cannot be bound to controller pads.

## Q050-453 Add room chat and presence indicators

**P3 · L16 collaboration and sharing · feature · M**

Goal: Participants can see peer names, colors, and current panel focus and exchange bounded, transient room messages.

Why: Demand is low across 1 DAW, and CollabPanel.cpp:154-163 lists names and roles but has no chat, shared focus, or pointer-color behavior.

Depends on: Label collaboration as experimental and document its limits
Acceptance:
- The collaboration panel shows connected peers' names, colors, and shared focus, and receives room chat from another instance.
- Chat and presence have registered commands, schemas, size/rate bounds, and multi-instance coverage.
- Room chat remains transient session state and is not serialized into the project or musical undo history.
Items:
  - `R-collaboration-23` Room chat and collaborator presence - partial (src/gui/CollabPanel.cpp:154-163; docs/KNOWN-LIMITATIONS.md:2385-2386; no chat command found); complete: The panel shows connected peer names and roles, but has no room chat, pointer colors or shared focus indicator.

## Q050-454 Honor zero-crossing snap for knife splits

**P3 · L04 arranger, timeline, clip operations, markers, tempo map · completion · S**

Goal: A Shift-knife split on audio lands on the nearest eligible zero crossing when zero-crossing snap is enabled.

Why: The feature is partial in 3 DAWs, and the evidence identifies Shift-knife as the path that bypasses the existing zero-crossing helper.

Acceptance:
- A regression test compares the split tick with the nearest zero crossing when the mode is enabled and verifies unchanged grid behavior when disabled.
- In the arranger, a Shift-knife split follows the existing zero-crossing snap helper.
Items:
  - `B-ED-021` Snap edits to zero crossings - partial (arrangement.modes.snap_to_zero_crossings; docs/KNOWN-LIMITATIONS.md:2354-2357); complete: Split, trim and drag-resize snapping are implemented, but Shift-knife is documented to bypass zero-crossing snapping.

## Q050-455 Complete automation write-pass helpers

**P3 · L06 automation and modulation · feature · M**

Goal: Write automation from the start or punch point and optionally hold the last written value when playback stops.

Why: Demand is unreported (3 DAWs); the tree has write-to-end and punched-write switches but not the requested write-to-start, write-to-punch, or hold-at-stop helpers.

Acceptance:
- Agent commands provide write-to-start and write-to-punch helpers with explicit tick/value readback and undo.
- A pass test proves points outside the requested range are preserved and later points are handled according to the selected helper.
- Optional hold-at-stop behavior writes the last value at the stop position and can be enabled or disabled through the UI and command surface.
Items:
  - `B-ED-201` Write-pass helpers - partial (automation.write_to_end; automation.switches.punched_writes; docs/KNOWN-LIMITATIONS.md:2551-2555); complete: Punched writes and write-to-end exist, but there is no write-to-start or write-to-punch helper and Touch or Latch does not hold its value after stopping.

## Q050-456 Ornaments and flams

**P3 · L07 note editor, MIDI editing, scales, chords, articulations · completion · M**

Goal: Apply selectable turn, trill, and flam patterns to selected notes with bounded timing and velocity controls.

Why: Demand is not rated and two DAWs are cited; the note tools have flam but no turn or trill operation.

Acceptance:
- The Notes menu and command surface expose turn and trill patterns beside flam.
- A MIDI fixture verifies generated note positions, velocities, bounds, determinism, and one-step undo.
Items:
  - `B-ED-100` Ornaments and flams - partial (src/core/ControlCommandsNoteTools.cpp:24-33,142-180; `note.flam`; searched these note-tool sources for trill/mordent/turn patterns and found none); complete: Flam grace notes are implemented, but the broader ornament set has no turn or trill patterns.

## Q050-457 Built-in mixer-strip dynamics

**P3 · L10 devices, instruments, effects, plug-in hosting · feature · M**

Goal: Users can add a built-in dynamics processor to every mixer strip alongside its EQ.

Why: Mixer strips expose EQ but no built-in dynamics; the request cites 3 DAWs and has no demand score.

Acceptance:
- Show and configure strip dynamics on multiple channels and round-trip settings.
- A render confirms the processor affects only its channel and the control API reports its state.
Items:
  - `B-MX-085` Processing built into every strip - partial (src/core/ControlCommandsMixerTools.cpp:499; src/gui/shell/ShellCommandMenus.cpp:466; plans/TRACKER-050.md:459); complete: Each strip has a three-band EQ, while the broader built-in strip-processing set is not present.

## Q050-458 Split audio by level while retaining segments

**P3 · L04 arranger, timeline, clip operations, markers, tempo map · completion · M**

Goal: Users can split audio at threshold-defined silence boundaries and keep each resulting audio segment as an editable clip.

Why: The 2-DAW evidence confirms clip.remove_silence exists but asks for a split-by-level mode that preserves the resulting audio segments.

Acceptance:
- A regression test feeds audio with known above-threshold passages and verifies each retained segment's source window and timeline position.
- The Remove Silence dialog exposes split-by-level and undo restores the original clip.
Items:
  - `B-ED-040` Remove silence / split by level - partial (clip.remove_silence; src/gui/shell/ShellCommandMenus.cpp:309); complete: Remove Silence splits around quiet ranges and deletes them, but there is no keep-the-slices split-by-level mode.

## Q050-459 Save custom toolbars for commands and workflows

**P3 · L04 arranger, timeline, clip operations, markers, tempo map · feature · M**

Goal: Users can create, save, and restore a toolbar containing selected menu actions and recorded workflows.

Why: The two-DAW partial row finds a command palette and workflow recording but no custom-toolbar surface in src/gui.

Acceptance:
- A GUI test creates a custom toolbar with a menu action and workflow, saves the project or user configuration, reopens it, and runs both entries.
- The toolbar editor rejects unavailable or invalid actions without losing the saved toolbar.
Items:
  - `B-ED-056` Command search, macros, custom toolbar - partial (window.command_palette; workflow.record_start/workflow.record_stop; no custom-toolbar surface found in src/gui); complete: Search and recorded workflows are available, but users cannot build a custom toolbar from their actions.

## Q050-460 Repair selected spectral regions

**P3 · L05 audio editing, warping, stretching, fades · feature · L**

Goal: Users can attenuate or repair a selected time-frequency region without altering audio outside it.

Why: No demand score is recorded for the 2-DAW request; the current spectrogram data is read-only and there is no repair operation.

Depends on: Add spectrogram view and time-frequency selection
Acceptance:
- Select a marked time-frequency region and apply attenuation/repair; render and verify only the selected bins and frames change.
- Verify undo/redo, save/load, and the same operation through the registered agent command.
Items:
  - `B-ED-158` Spectral view and repair - partial (sample.spectrogram is read-only frequency data; rg sample.spectrogram under src/gui found no editor route); complete: The control surface can calculate spectrogram bands, but there is no sample spectrogram view or repair workflow.

## Q050-461 Project groove amount control

**P3 · L07 note editor, MIDI editing, scales, chords, articulations · completion · M**

Goal: Set and save one project-wide amount that scales the strength of applied groove without changing the groove pattern.

Why: Demand is not rated and two DAWs are cited; existing groove commands have no project-wide strength control.

Acceptance:
- A project control changes the applied groove amount from zero to full strength.
- A MIDI fixture verifies the same groove at zero, half, and full amount and the setting survives save and reload.
- The control is undoable and available through the agent surface.
Items:
  - `B-ED-141` Groove list management, hot-swap, global amount - partial (groove.list/extract/apply/set/remove/rename; src/gui/CommandLists.cpp:161-170; src/gui/GrooveMenu.cpp:80-105); complete: Grooves can be managed and applied with a per-action strength, but there is no global groove amount control.

## Q050-462 SysEx and program-change lanes

**P3 · L07 note editor, MIDI editing, scales, chords, articulations · feature · L**

Goal: Store and edit bounded SysEx events in MIDI clips and deliver them alongside the existing program-change lane.

Why: Demand is not rated and two DAWs are cited; clip controller lanes support program changes but have no SysEx lane.

Acceptance:
- A MIDI clip round-trip test preserves bounded SysEx payloads and program-change events.
- The note editor shows the lane and a playback test client receives events in timestamp order.
- Oversized or malformed payloads return a typed refusal without corrupting the clip.
Items:
  - `B-ED-131` SysEx and program-change lanes - partial (include/MidiClipControllers.h:49-59; `clip.controllers_set` supports CC, pitchbend, aftertouch, program; no SysEx lane kind); complete: Program-change and controller lanes are present, but MIDI clips have no SysEx lane.

## Q050-463 Live-input note repeater

**P3 · L07 note editor, MIDI editing, scales, chords, articulations · feature · M**

Goal: Repeat incoming MIDI notes at a chosen interval and count while preserving the original note's channel and release behavior.

Why: Demand is not rated and two DAWs are cited; note.repeat edits clips but the live-input transform has no repeater.

Acceptance:
- A MIDI input fixture verifies repeat count, interval, channel, note-off cleanup, and disabling the repeater.
- The processing path is bounded and passes an allocation-counter check under repeated input.
- A control-surface test sets and reads the repeater settings.
Items:
  - `B-ED-144` Note repeater - partial (src/core/ControlCommandsNoteTools.cpp:219-253 (`note.repeat` copies notes into a clip); src/core/ControlCommandsInputTransform.cpp:151-160 has no live repeat option); complete: Clip-level note repetition exists, but live MIDI input has no repeater processor.

## Q050-464 Formula and script modulators

**P3 · L10 devices, instruments, effects, plug-in hosting · feature · L**

Goal: Users can create a bounded formula/script modulator and map its output to a device parameter.

Why: The modulation layer has no formula/script modulator type and the request is associated with 2 DAWs, with no demand score supplied.

Acceptance:
- Run a fixture formula at its declared rate and verify bounds, parameter mapping, and saved source.
- Prove the modulation path allocates nothing on the audio callback.
Items:
  - `B-MX-118` Formula and script modulators - partial (src/core/ControlCommandsModulatorTools.cpp:118-123; include/ModulationLayer.h:107); complete: A bounded formula language is available, but there is no separately authored script modulator.

## Q050-465 Stock spectral effects

**P3 · L10 devices, instruments, effects, plug-in hosting · feature · L**

Goal: Users can add a focused set of real-time spectral effects from the built-in catalogue.

Why: The catalogue lacks the requested built-in spectral devices; the source compares 2 DAWs and supplies no demand score.

Acceptance:
- Add and render the agreed spectral-device MVP with parameter state saved and restored.
- Measure and report CPU and latency for the fixture preset.
Items:
  - `B-MX-154` Vocoder and spectral effects - partial (plugins/Vocoder/Vocoder.cpp:43-46; plans/TRACKER-050.md:476); complete: A built-in vocoder is present, but a broader spectral-processing effect set is not.

## Q050-466 Hosted-instrument compatibility options

**P3 · L10 devices, instruments, effects, plug-in hosting · feature · M**

Goal: Users can view and persist supported per-plug-in compatibility options on instrument devices.

Why: plugin.options currently targets effects but not hosted instruments; the request cites 2 DAWs with no demand score.

Acceptance:
- Expose only options that apply to the selected host format and persist each setting.
- Verify the plug-in options command returns the instrument values after reload.
Items:
  - `B-MX-099` Per-plug-in compatibility options - partial (src/core/ControlCommandsDeviceParams.cpp:398-403 (plugin.options applies to effects)); complete: Generic editor, keep-running and latency-override options are per effect, but hosted instruments lack the same compatibility controls.

## Q050-467 Compensate recorded automation points

**P3 · L01 engine wiring (engine capability not reachable) · feature · M**

Goal: Users can record automation on a latency-producing path and replay the recorded points in alignment with the sound they shaped.

Why: MX-160 is partial on 1/8 DAWs: merged `automation.latency_compensation` shifts playback reads, but `Song::processNextBuffer` still records at `relTime` without upstream-latency adjustment.

Acceptance:
- With `automation.latency_compensation` enabled and a known latent effect upstream, a regression records automation and verifies the recorded point timing against the latency-corrected rendered-audio reference.
- The regression covers both tick-based and block-based playback reads, and verifies compensation-off recording keeps legacy point positions.
- A control-surface check toggles the project opt-in, confirms its save/load behavior, and reads back the enabled state.
Items:
  - `B-MX-160` Latency-compensated automation - partial (automation.latency_compensation; include/AutomationLatency.h:40-47; plans/TRACKER-050.md:103); complete: Playback automation reads are shifted for upstream latency, but automation recorded into clips is not compensated.

## Q050-468 Bind Tab navigation to transients

**P3 · L04 arranger, timeline, clip operations, markers, tempo map · completion · S**

Goal: Users can press Tab and Shift+Tab in the arranger to move to the next and previous audio transient.

Why: The menu actions exist, but the one-DAW evidence finds no Tab binding in the GUI.

Acceptance:
- A GUI test verifies Tab and Shift+Tab invoke arrangement.next_transient in the corresponding direction when the arranger has focus.
- The shortcuts do not intercept text entry or unrelated editor focus.
Items:
  - `B-ED-025` Tab to transients - partial (arrangement.next_transient; src/gui/shell/ShellCommandMenus.cpp:495-496; rg over src/gui found menu actions but no Tab binding); complete: Next and Previous Transient are menu actions, but the requested Tab and Shift+Tab bindings are absent.

## Q050-469 Receive note and MIDI events inside a chain

**P3 · L09 routing, buses, sends, sidechain, I/O · completion · L**

Goal: Choose an explicit source and slot for note/MIDI events received inside a chain, in addition to current audio receive routing.

Why: The W3 chain receive command and menu cover audio but not note/MIDI input; demand is low from 1 DAW.

Acceptance:
- A chain receive command selects a source and slot and reports them in chain state.
- A MIDI test routes note-on, note-off, controller and timing events through the selected chain, and undo restores the previous receive source.
Items:
  - `B-MX-063` Receive audio or notes inside a chain - partial (src/core/ControlCommandsW3Engine.cpp:24-30; src/gui/shell/ShellCommandMenus.cpp:413; commands_snapshot.json:3117); complete: A chain can receive audio from another channel, but note or MIDI receive inside the chain is absent.

## Q050-470 Show calibrated reference on meter views

**P3 · L09 routing, buses, sends, sidechain, I/O · completion · S**

Goal: Read a visible 0 VU reference marker or calibrated scale on the meter views.

Why: meterReferenceDbfs currently has configuration and command support but no meter-scale consumer; demand is low from 1 DAW.

Acceptance:
- Changing meterReferenceDbfs moves the labeled reference marker on the mixer and track meter views.
- A UI test verifies the marker at the configured value and the default remains unchanged when calibration is unused.
Items:
  - `B-MX-043` Calibration reference level - partial (src/core/ControlCommandsMixerTools.cpp:335-345,504-506; rg -n meterReferenceDbfs src include plugins tests finds only configuration setter/getter and target serialization, with no meter scale consumer); complete: The reference dBFS value can be set and reported, but meter displays do not show or scale to the calibration reference.

## Q050-471 Per-clip effect-chain storage

**P3 · L10 devices, instruments, effects, plug-in hosting · feature · L**

Goal: Users can attach an optional effect chain to an audio clip and save it with the project.

Why: Per-clip effects are absent from the clip model and the cited request spans 1 DAW, with no demand score supplied.

Acceptance:
- Add/remove/reorder clip effects and round-trip the chain through save/load without changing projects that have no clip effects.
- Expose clip-chain state through a headless command test.
Items:
  - `B-MX-083` Effects on a single clip - partial (src/core/ControlCommandsClipProcess.cpp:24-30; plans/TRACKER-050.md:478); complete: Clip processing can render effects offline, but clips cannot host live per-clip effect chains.

## Q050-472 Per-device oversampling controls

**P3 · L10 devices, instruments, effects, plug-in hosting · feature · M**

Goal: Users can enable oversampling on stock effects that benefit from it.

Why: The cited stock effect has no per-device oversampling control; this request is from 1 DAW with no demand score.

Acceptance:
- Expose per-device quality settings and verify the selected factor changes the processing path.
- Check latency reporting, aliasing fixture output, and saved state.
Items:
  - `B-MX-100` Per-device oversampling - partial (plugins/Console/Console.cpp:70-81, 100; plans/TRACKER-050.md:482); complete: Oversampling can be set on Console only; other eligible built-in devices have no per-device control.

## Q050-473 Collapse device and rack sections

**P3 · L10 devices, instruments, effects, plug-in hosting · feature · S**

Goal: Users can collapse and expand parallel rack sections in the Device panel.

Why: Rack sections have no persisted fold state; the request cites 1 DAW and has no demand score.

Acceptance:
- Collapse state is visible, keyboard accessible, and preserved after reopening the project.
- Verify each chain can be collapsed independently.
Items:
  - `B-MX-113` Fold a device or rack - partial (src/core/ControlCommandsDeviceParams.cpp:390-395; src/gui/shell/DeviceCard.cpp:280-282); complete: Individual device cards fold, but a parallel rack section cannot be folded as a group.

## Q050-474 Recorded insert-take editor

**P3 · L10 devices, instruments, effects, plug-in hosting · feature · L**

Goal: Users can edit a recorded insert take by selecting its waveform range and landing the selection as a clip.

Why: The insert recorder exists but has no waveform-selection editor or clip-landing workflow; this request is from 1 DAW with no demand score.

Acceptance:
- Record an insert take, select a waveform range, and place it on the timeline without changing the source take.
- Save/load the take and verify the landed clip through the UI and agent commands.
Items:
  - `B-MX-156` Audio editor and recorder as an insert - partial (plugins/InsertRecorder/InsertRecorder.cpp:38-41, 85-97; plans/TRACKER-050.md:486); complete: The Recorder insert captures audio passing through it, but it does not open an in-chain waveform editor.

## Q050-475 Background jobs appear and progress in task.list

**P3 · L12 projects, files, import/export, interchange, recovery · feature · L**

Goal: Run render, stem export, plugin scan, and peak building as background jobs visible through task.list and the task area.

Why: The row reports no demand signal and one DAW, with task commands already present but major job types still synchronous.

Acceptance:
- Start each job type and verify task.list/task.show expose progress, completion, failure, and cancellation.
- While each job runs, issue a control ping and edit another project view to confirm the task does not block the interface or socket.
Items:
  - `B-WF-046` Background task list - partial (app.tasks/task.list/task.show (src/core/ControlCommandsTask.cpp:96; src/gui/TaskIndicator.cpp:294); docs/KNOWN-LIMITATIONS.md:2463); complete: A background task list with cancel ships, but the plug-in scan, render.render, render.stems and browser.peaks stay synchronous and never appear in it.

## Q050-476 Crosshair guides across editing canvases

**P3 · L13 customisation, shortcuts, themes, layout · completion · S**

Goal: Use the crosshair guide in the note and automation editors as well as the arranger.

Why: The one-DAW request identifies arranger-only crosshair support, leaving note and automation canvases uncovered.

Acceptance:
- Enable the crosshair and verify pointer guides render in note and automation canvases, follow the pointer, and can be toggled off.
Items:
  - `B-WF-109` Cross-hair cursor - partial (view.crosshair ('across the arranger'); src/gui/ArrangerAids.cpp (only GUI user)); complete: The cross-hair cursor exists in the arranger only, not in the note or automation editors.

## Owner-approved reliability work — 2026-10-25


### Q050-486 Validate and own saved plugin waveform data through one shared loader

P0 · L10 · M

Goal: Malformed or truncated saved waveform data cannot trigger an out-of-bounds read, leak overwritten decoded buffers, or introduce nonfinite samples; valid existing presets retain identical data and audio.

Acceptance:

- A direct loader regression and at least two affected plugin load paths demonstrate the representative short-buffer/ownership failures on the prior code; cover every migrated caller on fixed code.
- Tests cover empty, invalid, truncated, excessive, misaligned and nonfinite encoded data with bounded memory use and no out-of-range access; run the affected fixtures under ASan where supported.
- All migrated callers use validated lengths and owning storage; repeated loads produce no leak under the selected supported leak checker.
- Representative valid saved presets load/save with identical waveform values and render equivalently under the repository audio comparison contract.
- A project-level or existing control-socket fixture loads an affected instrument/effect, reloads its saved graph data, and verifies the resulting state and usable audio.

Full scope and implementation conditions: `research/reviews/feature-briefs/Q050-486.md`.

### Q050-487 Enforce consistent mutation, undo and modified-state bookkeeping

P1 · L17 · L

Goal: A successful edit consistently records the correct inverse/checkpoint and modified state; a failed or no-op edit leaves project state and undo history unchanged.

Acceptance:

- Reproduce at least one genuine missing-undo/modified-state failure and one no-op or failed-edit history failure against the pinned baseline; if no gaps remain, close the relevant stage with evidence.
- A mutating command exercised through the control socket preserves before/after state across undo/redo, marks genuine edits modified, and records the documented reversible/irreversible classification.
- Repeated same-value calls, typed refusals and dry runs leave state, undo history and provenance unchanged except where a documented contract explicitly permits otherwise.
- Existing regression cases remain green for undone_commands spans, coalescing, nested wrappers, async mastering completion, empty inverses on first saves, and append-only provenance.
- UI and headless paths use the same existing mutation mechanism; a migrated family no longer duplicates the bookkeeping that caused its reproduced failures.

Full scope and implementation conditions: `research/reviews/feature-briefs/Q050-487.md`.

### Q050-488 Use cooperative worker shutdown and explicit callback ownership

P1 · L15 · L

Goal: Workers stop and release resources predictably, and callbacks cannot access destroyed owners, while realtime processing remains nonblocking.

Acceptance:

- A deterministic test reproduces a concrete race, hang or stale-owner access before its corresponding stage change; repeatedly closing a worker blocked on input reaches completion within an explicit justified bound.
- Resources are closed only after the worker and dependent callbacks can no longer use them; duplicate cancel/stop and partial startup failure are tested.
- Race/lifetime tests execute under the applicable TSan/ASan configuration with no added suppressions hiding the reproduced path.
- An allocation/locking check on affected audio-thread entry points remains clean; worker wakeup and plugin IPC changes preserve audio/MIDI delivery and existing timeout semantics.
- Each supported platform changed has a real or mocked blocking-I/O shutdown regression; unavailable device/platform coverage is explicitly reported, not passed vacuously.
- The implementation plan identifies surviving versus retiring worker paths, existing feature ownership and prerequisites; no competing OSS/sndio/libsoundio or RemotePlugin modernization is introduced alongside Q050-479/Q050-483.

Full scope and implementation conditions: `research/reviews/feature-briefs/Q050-488.md`.

### Q050-489 Make session and clip state transitions preserve shared invariants

P1 · L02 · L

Goal: Launch, stop, edit, record, undo and reload transitions leave the launcher, arrangement and project model mutually consistent.

Acceptance:

- Stage one delivers a source-backed transition map and executable tests for representative reproduced failures; unresolved design claims are not counted as fixes.
- A deterministic sequence test covers launch/stop/edit/undo/redo plus save/load, verifies stable authored identities and references, and excludes transient playback fields from persistence comparisons where the contract requires.
- Control-socket tests drive at least one MIDI and one audio session workflow, including recording-to-arrangement where supported, then verify readback and rendered/audio events.
- Existing quantisation, follow-action, legato, linked-content and scene behaviour remains unchanged for valid workflows.
- Any introduced common transition API is used by the affected UI and agent routes and performs no allocation/locking on realtime paths.

Full scope and implementation conditions: `research/reviews/feature-briefs/Q050-489.md`.

### Q050-180.1 Support multiple positioned video clips with correct lane and render behavior — consolidation completion

P1 · L04 · L

Goal: Users can add, independently address, position, save and reopen multiple project video clips; the lane draws and seeks each clip, while playback and range rendering show the correct frame at each song tick without stale frames.

Acceptance:

- Attach two video files at distinct song-tick positions and offsets; save and reopen, then verify both files, metadata, positions and identities remain independently addressable.
- Draw each clip over its own lane span; clicking either span seeks to its song position, and the viewer follows the clip active at the playhead without replacing the other clip.
- Play and render a range spanning both clips; verify correct frames and project-tempo timing in each clip, and verify no stale frame appears before, after, or in a gap.
- Drive clip creation, independent position/offset edits, save and readback through registered commands and MCP; exercise the equivalent add/edit workflow through the video lane UI.
- Each mutating clip operation is one undo/redo step; load legacy single-<video> projects as one clip and preserve empty-project reset behavior.

Full scope and implementation conditions: `research/reviews/feature-briefs/Q050-180.1.md`.

## Owner-approved additions — 2026-10-27

Gaps from `research/strategy-2026-10-27/` (Luna's DAW coverage audit and accessibility research) and the plug-in stress run (`research/plugin-stress-2026-10-27/`). Owner: "go ahead and add them".

### Q050-490 Open VST3 and CLAP plug-in windows on Windows and macOS

P0 · L10 · L

Goal: A hosted VST3 or CLAP plug-in's own editor opens, resizes and closes on Windows (HWND) and macOS (NSView/Cocoa), as it already does on Linux X11.

Acceptance:

- Vst3EditorSession and ClapHostGui select the platform window type at run time (kPlatformTypeHWND / kPlatformTypeNSView; CLAP_WINDOW_API_WIN32 / CLAP_WINDOW_API_COCOA) instead of refusing every non-X11 view (plugins/Vst3Effect/Vst3EditorSession.cpp:190, plugins/ClapEffect/ClapHostGui.cpp:65).
- Open, resize (host- and plug-in-initiated), close and reopen are covered by the existing in-tree VST3 and CLAP fixtures on each platform's hosted CI job; the Linux X11 path and its tests are unchanged.
- A plug-in that cannot embed is refused with a typed error naming the platform and window API, never a crash or an empty window.
- Content scaling (plugin.options editor_scale) is applied on Windows and macOS where the API offers it, and documented where it does not.
- docs/KNOWN-LIMITATIONS.md and docs/PLUGIN-COMPATIBILITY.md state per platform what was proved on CI and what still needs a real-machine run (the owner has a Windows laptop and a Mac mini for that check).

Why: Without this, no third-party plug-in window can open on the two platforms most users run; found 2026-10-27 while checking Luna's coverage audit.

### Q050-491 Host Audio Unit (AUv2) plug-ins on macOS

P1 · L10 · L

Goal: macOS builds list, load, play, automate, save and restore Audio Unit v2 instruments and effects, with their own Cocoa views.

Acceptance:

- plugin.list reports an 'au' format on macOS builds only; discovery uses the system component registry and records each component in the scan cache with the same quarantine rules as VST3/CLAP.
- An AU instrument plays MIDI and an AU effect processes audio through the same device/chain model; parameters are reachable through plugin.param_get/param_set and automation.
- plugin.state_save/state_load and project save/reopen round-trip an AU's ClassInfo state; a component that fails to instantiate is refused with a typed error.
- The editor opens in an NSView (shared with Q050-490's macOS path).
- Tests run on the hosted macOS job against Apple's built-in AU components (AUBandpass, AUDistortion, DLSMusicDevice); non-Apple builds compile the format out; real-machine checks are listed for the owner's Mac mini.

Why: Many Mac users own plug-ins as AU; the not-doing list only ruled out AUv3 on iOS.

### Q050-492 Generic accessible parameter editor for hosted plug-ins

P1 · L14 · M

Goal: Every hosted plug-in has a host-drawn, keyboard- and screen-reader-operable parameter view that works when its own window cannot be used.

Acceptance:

- A 'Parameters' view lists every plug-in parameter with name, current value with units/text, range and type, searchable by name; read-only and output parameters are shown as such and cannot be edited.
- Arrow keys, Page keys, Home/End and typed values change a parameter; each change is one undo step and is announced through the existing accessible announcements.
- The view exposes Qt accessible name/role/value for every row and passes window.accessibility_report with no unnamed or unreachable control.
- Presets/programs exposed by the plug-in are listed and selectable from the same view.
- Works for VST3, CLAP and LV2 devices; a control-socket test drives the view on the in-tree fixtures.

Why: The accessibility research ranked inaccessible third-party plug-in windows the biggest barrier for blind producers; the same view is the fallback when an editor fails to embed.

### Q050-493 Warp controls for launcher audio cells in the GUI

P1 · L02 · M

Goal: An audio cell's gain, transpose, detune, loop region and tempo-warp settings are editable from the launcher GUI, not only over the socket.

Acceptance:

- The launcher cell's context menu or inspector exposes every warp/playback field that session.set_slot accepts (docs/050-features/Q050-013.md:29-35 lists them as socket-only).
- Edits are undoable, saved and reloaded, and reflected in the cell's display.
- A GUI test drives each control and verifies the cell's state through session.get_state; rendered audio changes as the existing Q050-013 PCM tests expect.
- The KNOWN-LIMITATIONS socket-only note for these fields is removed.

Why: Q050-013 shipped the engine; its brief states the cell warp GUI is missing.

### Q050-494 A GUI switch for sample-accurate automation curves

P2 · L06 · S

Goal: Users can see and change whether an automation clip renders sample-accurately from the automation editor.

Acceptance:

- The automation editor shows the clip's sample-accuracy mode and lets the user change it; the change is undoable and saved.
- The control uses the same path as automation.ramp_set / automation.ramp_get and agrees with them.
- A GUI test toggles the mode and verifies it through automation.ramp_get; docs/KNOWN-LIMITATIONS.md:2169-2210 is updated.

Why: Luna's audit: the mode exists but only the socket can set it.

### Q050-495 Extract a video's audio from the GUI

P2 · L04 · S

Goal: The video window offers an 'Extract audio' action that places the attached video's sound on an audio track.

Acceptance:

- A menu action in the video window or video lane runs video.extract_audio in its background mode and shows progress in the task area.
- The extracted audio lands on a new audio track aligned to the video's offset; the action is undoable.
- A GUI test runs the action on the in-tree test video fixture; builds without FFmpeg hide the action.

Why: The manual promises extraction but no GUI caller exists (DAW coverage audit).

### Q050-496 Noise reduction as a sample-editor range operation

P2 · L05 · M

Goal: Users can reduce steady background noise in a selected range of a sample from the sample editor.

Acceptance:

- sample.edit_range gains a denoise operation that processes the selected range offline with the existing RNNoise denoiser (plugins/RnnoiseDenoiser), resampling as that plug-in does.
- The Sample Editor exposes it with an amount control and preview; the edit is undoable like the other range operations.
- A test on a fixture with added noise shows a measured noise-floor drop and an unchanged length; non-speech material is documented as out of scope for RNNoise.

Why: Podcast and voice workflows lack a repair step (DAW coverage audit); the RNNoise effect exists only as a mixer insert.

### Q050-497 Earcons for focus, edit and transport feedback

P2 · L14 · M

Goal: Short, optional sounds confirm common actions so screen-reader users do not have to wait for speech.

Acceptance:

- A small earcon set (selection boundary, edit applied, refused action, record start/stop, loop wrap) plays through a monitoring path that is never recorded or rendered.
- Earcons are off by default, switchable per category, with their own level, from the accessibility settings and over the socket.
- Playing an earcon allocates and locks nothing on the audio thread (pre-loaded buffers).
- A test proves earcons do not appear in render.render output or recorded takes.

Why: The accessibility research found no earcon route (rg 'earcon' returned nothing).

### Q050-498 Switch-access scanning and dwell activation

P2 · L14 · L

Goal: Musicians who use one or two switches, or dwell-clicking with eye-gaze or head pointers, can reach and operate the main controls.

Acceptance:

- An optional scanning mode steps focus through the active panel's controls on a timer or a 'next' switch, and activates on a 'select' switch; switch keys are configurable.
- An optional dwell mode activates the control under the pointer after a configurable delay, with a visible progress ring.
- Scanning order follows the existing accessible focus order; both modes are tested on the transport, track list, mixer strip and note editor.
- Both modes are off by default and documented in the manual's accessibility section.

Why: No switch, eye-gaze or dwell path exists (accessibility research, section 5).

### Q050-499 Visual cues for deaf and hard-of-hearing users

P2 · L14 · M

Goal: Timing and level events that are normally heard can also be seen.

Acceptance:

- A visual metronome (beat flash with accent on the downbeat) follows the transport and count-in.
- Optional on-screen flashes mark clipping, recording start/stop and launcher quantise points.
- All cues respect the reduced-motion setting and the accessible palette; each is switchable and saved in settings.
- Tests check that the cues follow transport position and tempo changes.

Why: Deaf/HoH musicians rely on visual and vibrating timing cues (accessibility research, section 4); only haptics was ruled out.

### Q050-500 Reconcile KNOWN-LIMITATIONS and queue status with the current tree

P1 · L16 · M

Goal: The limitations document and the queue describe what the current line actually does.

Acceptance:

- Every KNOWN-LIMITATIONS passage named stale by the 2026-10-27 coverage audit (comping GUI, audio-cell playback, warp menus, mastering entry point, 0.3.0-alpha header, 'no third-party plug-in loaded') is corrected or removed against the current source.
- The plug-in section cites docs/plugin-compat and the 2026-10-27 stress run instead of claiming no third-party plug-in was loaded.
- Units already built (Q050-024, Q050-071, Q050-123) are marked as built in plans/QUEUE-050.md; QUEUE-050-NOT-DOING R-platform-13 no longer says broad format coverage is 'have' while macOS VST2/AU and non-X11 editors are missing.
- tests/doc-figures-gate.py and the release-honesty gate still pass.

Why: Luna's audit found several stale limitation passages and built units still listed as open.

### Q050-501 Tell users when a newer Zene version is available

P1 · L16 · M

Goal: Zene can tell a user, with their consent, that a newer release exists and take them to its download page; nothing is downloaded or installed without the user.

Acceptance:

- First run asks once whether to check for updates (default off until answered); the choice is in Settings and over the socket (app.update_check_get / app.update_check_set), and is saved.
- When enabled, at most once a day and on demand (Help > Check for updates) Zene reads the latest release of the configured feed (default: the KRUZZZZY/zene-studio GitHub releases API), compares versions semantically (pre-releases only when the user opted into them), and shows a non-modal notice with the version, release notes and a button that opens the download page.
- Offline, rate-limited, malformed or slow responses never block the interface or start-up (background request, bounded timeout) and produce at most one quiet log line; no request is ever made while the setting is off.
- Builds that are updated by something else (distro packages, Flatpak, a build-time option) hide the feature; the request carries no identifying data beyond a User-Agent naming the version and platform.
- Tests use a loopback HTTP fixture only (never a real network): newer/equal/older/pre-release/malformed/timeout responses and the off state making zero requests.
- The manual's Help section and docs/KNOWN-LIMITATIONS.md describe exactly what is sent and that installation stays manual; automatic download/installation is a later unit that needs signed installers.

Why: The owner is about to send builds to testers; without a check they will not learn about new builds. ControlCommandsAssistant.cpp:32 states 'There is no update check: nothing is fetched'.

### Q050-502 Open plug-in windows on Wayland desktops

P0 · L10 · M

Goal: A hosted plug-in's own window opens on a Linux Wayland session (KDE, GNOME), not only under X11.

Acceptance:

- Measure first: on a Wayland session, report what plugin.editor_open does today for a VST3 and a CLAP fixture (PluginNativeEditor passes view->winId() as an X11 window id at src/gui/PluginNativeEditor.cpp:244; on the wayland Qt platform that is not an X11 window).
- Make it work: either run the application on XWayland when plug-in editors are needed (QT_QPA_PLATFORM=xcb chosen at start-up unless the user overrides it, as other Linux DAWs do), or host each editor in an XWayland-backed window; the choice and its reasons are documented.
- When an editor still cannot open (no XWayland, a plug-in without X11 support), the refusal is typed and names the reason - never a crash, a hang or an empty window.
- A test covers the platform decision (wayland session with and without XWayland available, user override) without needing a compositor; the X11 path and its tests are unchanged.
- The manual and docs/KNOWN-LIMITATIONS.md state the Wayland behaviour; the owner checks it on the real KDE Wayland desktop.

Why: Most current Linux desktops default to Wayland, including the owner's KDE; the editor host is X11-only (Vst3EditorSession.cpp:190, ClapHostGui.cpp:65) and nothing selects XWayland.


## Owner-approved additions — 2026-10-28

### Q050-503 Ship .deb packages for x86_64 and arm64 (Chromebook-friendly Linux install)

P2 · L10 · M

Goal: A Linux user - including a ChromeOS user with Linux turned on - installs Zene by double-clicking a .deb, on both x86_64 and arm64, and it appears in the launcher.

Acceptance:

- build.yml's linux-x86_64 and linux-arm64 jobs also produce zene_<version>_amd64.deb and zene_<version>_arm64.deb (CPack DEB or an equivalent recipe), uploaded on tags exactly like the AppImage (the tag/dispatch-only upload rule in build.yml is kept).
- The .deb declares its runtime dependencies (Qt, libsndfile, the audio backends) so apt resolves them on Debian 12 (ChromeOS Crostini's base) and Ubuntu 24.04; installs a .desktop file, icon and MIME type for .mmp/.mmpz so it shows in the launcher and opens projects.
- Installing and removing it leaves no files behind outside the package's own list (dpkg -L / dpkg -r checked in CI on a clean container).
- Smoke test in CI on each arch: install the .deb in a clean Debian 12 container, run zene --version and a headless render of a bundled demo project under QT_QPA_PLATFORM=offscreen.
- docs: the download page / README says which file to pick (Intel/AMD vs ARM Chromebook) and the one-time 'turn on Linux' step on ChromeOS, plus the honest limits (higher latency, USB audio interfaces usually unavailable in Crostini).

Why: Owner 2026-10-28: 'the most accessible possible DAW'. On ChromeOS a .deb double-clicked in Files installs into the Linux container and lands in the launcher - easier than an AppImage; many cheap Chromebooks are ARM, and CI already builds linux-arm64.

### Q050-504 Composer view: optional classical notation in the note editor (off by default)

P0 · L07 · M

Goal: A classically trained musician can switch on a composer view that shows a grand staff, clefs and note names beside the note editor's rows, while everyone else keeps a clean piano-key column by default.

Acceptance:

- Settings has a 'Composer view (classical notation)' option in the note-editor section, OFF by default; it is saved in the config, survives restart, and is readable/settable through the existing settings.get/settings.set control commands.
- OFF (default): the note editor's left key column shows plain piano keys with only the C of each octave labelled (C3, C4, ...); per-row note names, staff and clefs are not drawn. Custom drum/key names from TrackKeyNames still show.
- ON: the left column widens and draws a grand staff (treble and bass clef) aligned to the piano-roll rows: each visible row's pitch sits as a notehead on its correct line or space, with ledger lines above/below and middle C between the staves; every row is labelled in scientific pitch (C4, F#3).
- ON: accidentals follow the editor's current scale/key (flats in flat keys, sharps otherwise) and the key signature is drawn after the clefs; with no scale set it uses sharps.
- Toggling the setting updates an open note editor immediately, without reopening it; row heights, scrolling, zoom, auditioning a key by clicking it, and accessible-mode sizes keep working in both modes.
- Unit tests: the pitch -> staff position / ledger-line / accidental mapping (incl. middle C, both clefs, a flat key and a sharp key), the setting's default is OFF, and the key column paints the staff only when ON (offscreen widget test). The test fails if the default is flipped to ON.
- README/user docs mention the option in one or two plain sentences.

Why: Owner 2026-10-30: 'a version that is meant for classical musicians, with the notes ... on the left side, and one without, or make it a toggleable option in the settings and set it to not be default, make this a feature'.
