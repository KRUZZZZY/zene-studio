include(CTest)

# The production objects are also linked into these white-box regression tests.
# Compile conditional test seams into that object target only when tests are enabled.
if(BUILD_TESTING)
	target_compile_definitions(lmmsobjs PRIVATE LMMS_TESTING)
endif()

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)

# --- MSVC + Ninja: ONE link at a time in THIS directory ----------------------
# Every target built from this file - the ~75 test executables the LMMS_TESTS
# loop creates, FakeRemotePluginClient, the partc_ref_* reference MODULEs, the
# plugin/CLAP/VST3 integration binaries and synthetic_audio_plugin - lands in
# build\tests, and every one of them links with the vcpkg toolchain's
# `vcpkg z-applocal` post-link step chained into its link rule (vcpkg.cmake's
# add_library/add_executable wrappers). z-applocal copies the imported vcpkg
# dependency DLLs into the binary's own directory. Two of those copies racing
# over the same destination file is a Win32 sharing violation, `Access is
# denied.`, reported against an edge whose own link succeeded - documented for
# partc_ref_bassbooster.dll (run 34748733618) and again for
# tests\RoutingGraphTest.exe (run 34794059702), the latter on an edge with no
# LNK/C diagnostic of any kind.
#
# A Ninja link pool of depth one orders these links, and with them their
# post-link steps, against each other and nothing else; it cannot hide a real
# link error, because every link still runs and still fails on a real failure.
# vcpkg's own knob for this class, X_VCPKG_APPLOCAL_DEPS_SERIALIZED, is NOT the
# tool: it passes USES_TERMINAL only from the toolchain's add_executable
# wrapper, never for a MODULE target.
#
# DECLARED AND APPLIED HERE, before the first target in this file is created,
# and deliberately not beside the long note that documents the race: CMake's
# CMAKE_JOB_POOL_LINK variable only initialises the JOB_POOL_LINK property of
# the targets created AFTER it is set, so a pool set next to the partc_ref loop
# would cover that loop and nothing else - one site fixed and the rest left.
# The pool is a GLOBAL property and the name travels with it: ninja errors if a
# rule names a pool JOB_POOLS does not declare. It is a Ninja-only mechanism
# (ignored by the other generators), which is why it is scoped to the
# toolchains that need it, the same way the partc_ref loop scopes its own.
if(MSVC)
	set_property(GLOBAL APPEND PROPERTY JOB_POOLS zene_tests_link=1)
	set(CMAKE_JOB_POOL_LINK zene_tests_link)
endif()

set(LMMS_TESTS
	# The allocation probe's own proof, for tests/src/core/AllocationProbe.h: an operator new
	# made inside lmmsobjs, the library every realtime test exists to check, is counted.
	# Until 2026-10-07 the probe's replacement functions were `inline` and counted only
	# the test's own file; this test failed on that probe and passes on the fixed one.
	src/core/AllocationProbeTest.cpp
	src/plugins/LadspaChannelCountsTest.cpp
	src/plugins/RnnoiseDenoiserEffectTest.cpp
	src/plugins/VstEffectControlsTest.cpp
	src/core/ArrayVectorTest.cpp
	# Short ALSA writes must advance through every remaining frame in a period.
	src/core/AudioAlsaWriteTest.cpp
	src/core/AudioBufferTest.cpp
	src/core/AudioOssWriteTest.cpp
	src/core/AudioBusHandleTest.cpp
	src/core/AudioBusTest.cpp
	src/core/AudioEngineTeardownTest.cpp
	src/core/AudioEngineProfilerTest.cpp
	# The flanger delay buffer is allocated as an array, so both release sites
	# must use array delete.
	src/core/MonoDelayTest.cpp
	src/core/AudioPortsModelTest.cpp
	src/core/AudioPortsTest.cpp
	# Pitch-preserving time stretch (0.3.0 feature-list row 30): the WSOLA DSP
	# on its own - the same input through the resampler and through the
	# stretcher, with the pitch MEASURED at both (a Goertzel bin per tone), and
	# the streaming/I8/parameter contract the render path depends on.
	src/core/AudioStretcherTest.cpp
	src/core/AudioResamplerRatioTest.cpp
	# cluster new-p10-520: zero sample rate must not poison BasicFilters'
	# m_sampleRatio (constructor) or survive setSampleRate.
	src/core/BasicFiltersSampleRateTest.cpp
	src/core/AutomatableModelTest.cpp
	# cluster new-p12-279: EffectControls::m_viewVisible must be atomic because
	# processImpl() reads it on the audio thread while the GUI writes it.
	src/core/EffectControlsVisibilityTest.cpp
	src/core/AutomationModesTest.cpp
	# Q050-037: the pre-first-point contract of an automation clip - times
	# before the first node read the authored parameter's initial value (or an
	# explicit point at the clip's start) rather than zero, on the map read AND
	# on the ramp Song::buildAutomationRamps publishes, deterministically across
	# a start/stop/return and through a whole-project save/reload.
	src/core/AutomationPreFirstValueTest.cpp
	# Sample-accurate automation (feature-list row 9, board task #646): renders
	# real blocks through Song::processNextBuffer() and measures the per-sample
	# buffer the audio path reads against the clip's own curve, frame by frame -
	# in `sample` mode and in `block` mode, which is the negative control that
	# makes the measurement non-vacuous - plus a zero-allocation probe over 64
	# blocks of the ramp path and the ramp's own fixed-capacity refusals.
	src/core/SampleAccurateAutomationTest.cpp
	# The browser's engine half (W8 tag/metadata search + the waveform peak
	# cache): the metadata probe against a byte-written RIFF/WAVE, the tag
	# store's round-trip through its file, and the peak cache's hit/miss
	# behaviour. Headless: no Engine, no window, no audio device.
	# The query half of the browser engine (name/tag/metadata match, the walk's
	# scan and depth bounds, the probe budget), split out of BrowserCatalogTest to
	# keep both files under the file-length ratchet. Headless, like its sibling.
	src/core/BrowserCatalogQueryTest.cpp
	src/core/BrowserCatalogTest.cpp
	# The trim half of a bounce (the render half is a child process, so it is not
	# reachable from a test): a range copied to another file, and the refusal
	# that keeps a self-trim from truncating the file it is reading.
	src/core/BounceInPlaceTest.cpp
	# Default bounce paths must keep separate unsaved projects from replacing each other's files.
	src/core/ControlBounceSupportTest.cpp
	# The single-track arm (track.set_arm) and its batch sibling: the capture writes a take
	# that outlives it, so control.undo refuses both instead of disarming and leaving the
	# take behind.
	src/core/ControlArmReversibilityTest.cpp
	# Coverage lane (M0): bounce.land, track.flatten, range.bounce and the subproject verbs through a stand-in render child.
	src/core/ControlBounceRenderTest.cpp
	# Cluster 917: cancellable bounce jobs apply edits on the main thread after stale-target checks.
	src/core/ControlRenderJobTest.cpp
	# Cluster new-p16-186: a render queued onto its own source project is refused, and the project kept.
	src/core/RenderSourceGuardTest.cpp
	src/core/ControlSessionRecordLandTest.cpp
	src/core/ControlMidiReconnectSharedTest.cpp
	src/core/ControlPluginBusesTest.cpp
	src/core/ControlHostedDeviceCatalogueTest.cpp
	src/core/RackZoneFeedRunTest.cpp
	src/core/ScriptDawEffectsTest.cpp
	src/core/ClipEditsTest.cpp
	src/core/ClipFadesRenderTest.cpp
	# Q050-125: a clip's polarity - its rendered sign, the source left alone, and the gain curve the
	# waveform is drawn with is the multiplier the render got.
	src/core/ClipPolarityTest.cpp
	# Owner decision 2026-10-08: the offset fade - a crossfade into a contained clip fades the container where that clip is.
	src/core/ClipOffsetFadeTest.cpp
	# Round 3: an offset fade is a pair; a crossfade a clip is too short for is reported.
	src/core/ClipOffsetFadePairTest.cpp
	# Linked / smart clips (feature-list row 6, task #645): the relation, an edit
	# that propagates across it, an unlink, and the relation surviving a
	# save/reload round trip - all driven through the control surface.
	src/core/ClipLinkTest.cpp
	# The same feature's other half: the save/reload round trip (the row's
	# acceptance criterion) and the typed refusals. Two binaries because the
	# file-length ratchet is not moved for a new feature; the fixtures both use
	# are src/core/ClipLinkTestSupport.h.
	src/core/ClipLinkPersistenceTest.cpp
	# Owner decision 2026-10-08: a trim or a split of one member of a link group applies to every member.
	src/core/LinkedClipEditsTest.cpp
	# Round 2: resize, range split, phase-locked edits, one undo step, refusals.
	src/core/LinkedClipGroupEditsTest.cpp
	src/core/ClipSerialisationTest.cpp
	src/core/SerializingObjectHookTest.cpp
	# ARCH-4 slice S4 (SPEC-ARCH-4 1.7 Requirement 6): the write-refusal gate -
	# the one-writer rule made mechanical. It reads the write path as text and
	# fails on a setter called for a value it does not own (the shape
	# tests/rt-safety-scope.txt and the file-length ratchets already use), and
	# refuses to let a value marked derived-and-not-written reach the file at
	# all. Registered right after the serialiser it protects; text scan only, no
	# Engine and no socket, so it runs wherever a binary runs.
	src/core/WriteRefusalGateTest.cpp
	src/core/ClipWarpPersistenceTest.cpp
	src/core/ConfigMigrationTest.cpp
	src/core/ControlAutomationScriptTest.cpp
	# The automation MODES' own proof (feature-list rows 10 and 63, board task
	# #647): the mode round trip for all five modes through automation.get_state,
	# and THE no-destruction property through the command surface - a curve
	# recorded with automation.add_point, the mode set to read, the control ridden
	# with plugin.param_set while this harness drives Song::processNextBuffer(),
	# and the clip compared node for node - with a write-mode leg that must change
	# it. A second binary because the file-length ratchet is not moved for a new
	# feature.
	src/core/ControlAutomationModesTest.cpp
	# The browser.* command surface (W8): the ids, the schemas, the typed
	# refusals the agent-surface sweep needs, and the A16 undo proof - a tag's
	# inverse is the paired COMMAND, so control.undo dispatches it.
	src/core/ControlBrowserCommandsTest.cpp
	# The chain-preset group (the 0.3.0 ladder's "plugin-chain presets", OWNER-31
	# item 2): the six ids and their schemas, the six contract rows, the store's
	# name rule and location, and the MEASURED effect of an apply - the second
	# track's device order and parameter values read back through dsp.get_state.
	# The socket proof of the same claim is the registered ctest
	# ControlChainPresets (tests/control-chain-presets.py).
	src/core/ControlChainPresetTest.cpp
	# Out-of-process plugin hosting BEYOND ZynAddSubFx (feature row 80 of
	# docs/FEATURE-LIST-0.3.0.md, board card #670): the family table
	# (include/OutOfProcessHosting.h), the client-process record it keys the
	# crash-loop refusal on, and the `oop.*` group's five ids driven through the
	# registry - including, where this build ships both executables, a real
	# client process hosted, SIGKILLed, counted, restarted and finally refused.
	src/core/OutOfProcessHostTest.cpp
	# The same feature's other binary: the whole loop on a REAL client process
	# (hosted through oop.set_mode, SIGKILLed, counted, restarted, refused at the
	# crash-loop bound and drivable again after oop.reset_crashes). A second
	# binary because the file-length ratchet is not moved for a new feature - the
	# ClipLink / SmfInterchange pairs in this directory are the same shape.
	src/core/OutOfProcessHostClientLoopTest.cpp
	# The chord track, its detection and its generators (feature row 35,
	# docs/CHORD-TRACK.md): the engine half (the track's ordering, bounds and
	# <chord-track> element; the detection on known note sets; the catalogue and
	# the generator's SEEDED repeatability pair - same seed identical, different
	# seed different) and the surface half (the nine ids, their schemas, the six
	# contract rows, both kinds of inverse, and the project file).
	src/core/ChordTrackTest.cpp
	src/core/ChordDetectTest.cpp
	src/core/ChordProgressionTest.cpp
	src/core/ControlChordCommandsTest.cpp
	# The write half of the same surface, in its own binary: the two generators
	# (the seeded repeatability pair read off the wire), the track's write-out
	# into a clip, and the chord track's round trip through the project file -
	# the element present when the track holds a chord, ABSENT when it does not.
	src/core/ControlChordWriteTest.cpp
	src/core/ControlDeviceCatalogueTest.cpp
	# Q050-090: plugin.list's `vendor` is the device picker's vendor. The
	# proof lived in ControlDeviceCatalogueTest.cpp and grew it past the
	# file-length ratchet, so it is its own binary now - same registry read,
	# same picker comparison, same built-in amplifier naming Zene Studio.
	src/core/ControlDeviceVendorTest.cpp
	src/core/ControlEditCommandsTest.cpp
	# The control selection store keeps the SELECTED NOTES' ids, so a removal
	# cannot shift the selection onto another note (cluster new-p12-53-port).
	src/core/NoteSelectionIdentityTest.cpp
	# The session-sync surface (D11 "Ableton Link sync"): the five link.*
	# commands, the typed refusals, the A16 inverses through control.undo, and
	# the model's own arithmetic - the revision total order and the phase a
	# peer's announcement implies. docs/LINK-SYNC.md.
	src/core/ControlLinkCommandsTest.cpp
	# The project-asset reference group (feature row 38: project collection /
	# archive, hashing, relink): the three ids with their schemas and their
	# mutating flags, the three A16 contract rows, the missing-asset DETECTION
	# with its negative control (an intact project reports no missing assets),
	# content HASHING measured against the files' own sha256, and the RELINK with
	# its inverse proved for real - relink, read the document back, control.undo,
	# compare the whole file with the bytes that were there before. It needs a
	# real Engine (the recorded action checkpoint goes on the project journal).
	src/core/ControlProjectArchiveTest.cpp
	# Replacing the default or a named template is one file-level undo step,
	# including restoration and redo of the save path's backup side effect.
	src/core/ControlProjectTemplateTest.cpp
	# render.render accepts an absolute output path according to this host's
	# filesystem rules; its source guard catches the POSIX-only regression on
	# non-Windows builders, while the runtime call exercises a Windows drive path
	# on Windows before the empty-session refusal.
	src/core/ControlRenderOutputPathTest.cpp
	src/core/ControlRegistryTest.cpp
	# The rules a caller's sample array has to satisfy before a render or a
	# process is driven with it (cluster new-p10-76-port): the ONE reader the
	# wasm.* commands use (include/ControlSampleArray.h), tested in every
	# configuration - the reader is pure Qt, so the test runs where the
	# wasmtime C API is not there to compile the commands themselves.
	src/core/ControlSampleArrayTest.cpp
	# The undo-record bookkeeping for nested and wrapped commands (one record per journal step).
	src/core/ControlRegistryNestingTest.cpp
	# Replies held to their declared resultSchema (ZENE_CONTROL_CHECK_RESULTS), and
	# every command's id is its group.verb.
	src/core/ControlResultSchemaTest.cpp
	# The coverage closure's reference (board task #680): the ids the
	# 2026-09-16 pass measured as named by no other registered test artefact
	# (controller.* surfaces/templates, dawproject.convention, the two
	# midi.reconnect/clients ids, plugin.host_chunking, the four mmpz-git
	# merge ids and the three session follow/record ids). It asserts the
	# command contract and the A16 row for each, and that each ANSWERS with a
	# typed reply - reachability and contract, not behaviour; the behavioural
	# proofs are named per group in
	# docs/reports/COVERAGE-CLOSURE-2026-09-16.md.
	src/core/ControlSurfaceReferenceTest.cpp
	# The controller surface (feature row 19, board task #651): soft-takeover
	# (a hardware control is ignored until a synthetic control-change crosses
	# the stored value, with the take-over-off case as its negative control),
	# LED/feedback output measured at the MIDI-client boundary - the strongest
	# in-process claim with no hardware - and the mapping templates
	# (save/list/read/apply, and what they report they cannot resolve).
	src/core/ControllerSurfaceTest.cpp
	# cluster new-p15-90: a replaced user LFO waveform stays owned through a
	# render lease and is reclaimed only on the control thread after release.
	src/core/LfoControllerUserWaveTest.cpp
	# CODE-8: the shutdown-hook lifetime. The hook that unlinks the control
	# socket must survive a registry instance it was not registered on, and
	# must NOT outlive the ControlServer that registered it - both halves are
	# asserted here, and the second one fails on the pre-CODE-8 behaviour
	# (a hook left behind on a destroyed server, and a re-created registry
	# running an empty list on exactly the route the socket must be unlinked by).
	src/core/ControlShutdownHookTest.cpp
	src/core/ControlTempoMapCommandsTest.cpp
	# The 0.3.0 verb wave's four ids (clip.trim, clip.slip, note.probability_set,
	# render.stems): the three EDIT verbs' inverses are exercised for real -
	# invoke, read back, control.undo, read back again - and render.stems'
	# argument contract is proved without running a render. The end-to-end stem
	# export is the ControlStemExportVerb ctest below, which drives the real binary.
	src/core/ControlVerbInverseTest.cpp
	# record.arm_track writes a take outside the project journal: feed a real
	# interleaved block, then prove control.undo refuses and names the retained file.
	src/core/ControlRecordingRoutesTest.cpp
	# The destructive waveform editor's first slice (board card #706, group
	# sample.*): the five ids with their schemas and their A16 contract rows,
	# every generator and baker proved the A16 way - invoke through the
	# registry, read the SAMPLES back from the model, control.undo ONCE, read
	# them back against the exact frames that were there - plus the typed
	# refusals (MIDI clip, empty clip, trimmed window, out-of-range arguments,
	# non-sample track, the oversize guard) each with the negative control that
	# it pushed no undo step. Needs a real Engine: the checkpoints are the
	# Track/Clip journal steps ProjectJournal owns.
	src/core/ControlSampleOperatorTest.cpp
	# 0.5.0 L04 (Q050-031): clip.import_file - a MIDI or audio file becomes a clip, a bad one changes nothing - and the SMF note reader.
	src/core/ClipImportFileTest.cpp
	# The 0.3.0 note/scale/device wave (board task #648; feature-list rows 11, 66 and
	# 81): the fifteen ids, their contract rows and the REAL inverses - the seed pair
	# (persisted + undone), the repeatability pair of the seeded roll (same seed ->
	# identical take, different seed -> a different one), the three note transforms,
	# the slide verbs from the DEFAULT-flag trap state, the scale vocabulary and
	# context, scale.snap_notes with both of its inverse answers, and the MPE switch.
	src/core/ControlNoteScaleVerbsTest.cpp
	# Board card #712, lane 3 - session-wide dynamic tuning + the mts.* group.
	# ANCHOR NOTE: `grep -i microtuner tests/CMakeLists.txt` returns NOTHING
	# (there is no microtuner-named test anywhere in tests/); this entry IS the
	# scale/microtuner-family registration the brief's anchor meant - the
	# scale.* vocabulary test (#648) - so the anchor is immediately after it,
	# as asked, on the real entry. Proof: inactive baseline == the pre-#712
	# branch; a .scl load moving BOTH sounding notes onto the one table with
	# the transport playing and control.undo moving them back; ten typed
	# refusals with no table write and no undo step; 128-key parity against
	# Microtuner::keyToFreq over the SAME Scale/Keymap; the MTS-ESP refusal.
	src/core/SessionTuningTest.cpp
	# Cluster 5313: isolate the publication-window reader test from unrelated instrument plug-ins.
	src/core/SessionTuningPublicationTest.cpp
	src/core/ControlWarpCommandsTest.cpp
	# The groove pool and quantise surface (docs/GROOVE-POOL.md): the engine
	# half (the extraction and application arithmetic and the pool's XML form,
	# with no Engine at all), and the surface half (the seven registered
	# commands, their typed refusals, both inverses, and the <groove-pool>
	# element). The end-to-end socket proof is the ControlGrooveCommands ctest
	# below, which drives the real binary.
	src/core/GrooveTemplateTest.cpp
	src/core/ControlGrooveCommandsTest.cpp
	# The crash reporter's ARM pair (crash.enable / crash.disable, board task
	# #643): install() arms and uninstall() disarms, measured on the KERNEL's
	# dispositions (handlersArmed) and on real signals in forked children - with
	# the negative control: disarmed, a real SIGSEGV/SIGABRT writes NO report and
	# still kills the child by the signal, so the absence is "the handler is gone"
	# and not "the signal was swallowed".
	src/core/CrashReporterArmTest.cpp
	src/core/CrashReporterTest.cpp
	# Safe-start mode after a crash (feature row 77, board task #666): the crash
	# MARKER the next launch reads, the acknowledgement that makes the launch
	# after that a normal one, and the load-time PREDICATE - proven against a
	# REAL signal in a forked child and against the real Plugin::instantiate()
	# with a third-party module copy.
	src/core/SafeStartTest.cpp
	# The same feature's load-path half, in its own binary (the file-length
	# ratchet is not moved for a new feature - the ClipLinkTest /
	# ClipLinkPersistenceTest arrangement): a real plugin module copy, the plugin
	# search path pinned to it, and the real Plugin::instantiate() asserted to
	# hand back the DummyPlugin while safe-start is active and to load the module
	# once the session switch is off.
	src/core/SafeStartLoadPathTest.cpp
	src/core/DataFileFormatTest.cpp
	src/core/DataFileSaveIntegrityTest.cpp
	# ARCH-4 S2a: the document index (SPEC-ARCH-4 1.4) and the ONE condition
	# under which a project may carry one - the S1 preserved set being non-empty
	# (risk 2, "a release invariant"). The index's own slots are DOM-level; the
	# owed proof is the integration pair, which loads a real project carrying an
	# unclaimed <song> section, saves it, and verifies every recorded digest
	# against the canonical rendering of each section it names - what a reader
	# in any process would compute, which is the whole point of the digest being
	# canonical - through the integration path through Song and DataFile, not a
	# reimplementation of it -
	# with the SAME project minus that section re-saving with no index and no
	# namespace, so the additivity half is pinned beside the presence half
	# rather than assumed.
	src/core/DocumentIndexTest.cpp
	# The partial load's reducer (ARCH-4 S2b): reduceDocumentSections() is what
	# makes the spec's "prove the rest was not parsed" true rather than merely
	# asserted - the unselected section never reaches a parser. Pinned here: the
	# identity answer when nothing is skipped (the bytes, not an equal document),
	# byte-exactness against a deletion of the input's own ranges, and the three
	# boundaries an off-by-one would corrupt (a self-closing section, a section
	# that is the LAST child, and a same-named element one level deeper that must
	# NOT count as one), plus the refusal path that must return bytes unchanged.
	src/core/DocumentSectionsTest.cpp
	# The `.mmpz` v2 container (ARCH-4 S2c part 1): the shape of a project's
	# bytes decided by CONTENT rather than by a file name, so a v1 `.mmpz` and a
	# v2 one are told apart by their bytes and never by an extension that
	# configuration chose (SPEC-ARCH-4 risk 5). Pinned here: all three shipping
	# shapes plus the two a caller must refuse; XML recognised after a BOM and
	# leading whitespace; the entry-name rules that stop a section being
	# silently lost (the position is part of the name, a repeated name is
	# refused rather than merged); a container with no sections refused so that
	# writing v1 stays a decision; a ZIP without the skeleton refused BY NAME
	# rather than guessed at; and the slice's own honest limit asserted - a
	# subset read keeps the skeleton and the kept entries in the container's
	# order, while the entries it did not ask for were still read.
	src/core/ProjectContainerTest.cpp
	# The `.mmpz` v2 container (ARCH-4 S2c part 2): the document-level deriver
	# and reassembler that close the gap part 1 shipped with - writeContainer and
	# readContainer had zero production callers because nothing could turn a
	# document into entries or entries back into a document (src/core/main.cpp
	# said so verbatim in its --dump refusal). Pinned here: the RAW byte-identity
	# round trip this pair chose deliberately over the spec's corrected canonical
	# oracle (SPEC-ARCH-4 :544-557 - a raw cmp over a re-serialising path would
	# flake; this path never re-serialises), proven over an indented and a compact
	# document; the entry-naming reconciliation (the index's `entry` =
	# `name.xml` is NOT the container's key - sectionEntryName's position-first
	# name is, and the fixtures write `name.xml` so a deriver that keyed on it
	# would be caught); the deriver's refusals (no <z:index>, a nameless index
	# row whose dropped position would shift every later one, a section name the
	# container cannot file, a child the index does not name); the reassembler's
	# (missing skeleton, colliding entries, positions that disagree with the
	# index - with the pair ORDER shown free, position living inside the name);
	# and the integration pair through the real writeContainer/readContainer.
	src/core/ProjectContainerEntriesTest.cpp
	# Q050-007: the container as something a user SAVES and OPENS - ordinary
	# Save still legacy, the opt-in through project.save / the Save As choice /
	# the app/savecontainerv2 setting, project.open telling both by content,
	# legacy and v2 fixtures compared after save/load, an interrupted v2 save
	# recovered, a damaged container refused.
	src/core/ProjectContainerSaveTest.cpp
	# The export dither (TPDF) and the explicit SRC quality: the statistical
	# proof of the dither, the byte-level off-by-default proof, the converter
	# ratio convention, and the quality reaching the resampler.
	src/core/ExportDitherTest.cpp
	# Noise-shaped TPDF (owner decision 13): determinism, the measured shaping, and the
	# dither mode's mapping onto the pre-mode bool API.
	src/core/ExportNoiseShaperTest.cpp
	src/core/ExportWavDitherTest.cpp
	# cluster new-p12-317: a WAV write or close that failed is a FAILED export -
	# reported by the device, by the render and by RenderManager, with the broken
	# file removed instead of left behind. The failure is provoked by capping
	# RLIMIT_FSIZE, so it is deterministic: no disk, no timing, no race.
	src/core/ExportWavWriteErrorTest.cpp
	src/core/LoudnessReportTest.cpp
	src/core/LufsMeterTest.cpp
	src/core/MasteringTest.cpp
	# Startup and CLI regressions: the selected UI scale, dump/compress refusals,
	# and session-marker cleanup on failed command startup.
	src/core/MainCliStartupTest.cpp
	# Import detection (0.3.0, feature row 34; the `detect.*` group's engine half):
	# a RIFF/WAVE synthesised BYTE BY BYTE at a known answer - click tracks at 128
	# and 90 BPM, an A major scale over an A bass - read back through the engine's
	# own SampleDecoder, so the decoder, the arithmetic and the vocabulary are all
	# on the path. It also asserts the refusals (no transients, no file) and the
	# detected-key field's XML round trip. Headless: no Engine, no window, no audio
	# device. Real-world accuracy is NOT measured here - see
	# docs/IMPORT-DETECTION.md.
	# DrumSynth level parsing and rendering (the high-level fixture catches integer
	# overflow before the renderer converts a level product to floating point).
	src/core/DrumSynthLevelTest.cpp
	src/core/ImportDetectionTest.cpp
	# ImportFilter must restore the ProjectJournal after a plugin throws.
	src/core/ImportFilterTest.cpp
	src/core/HydrogenImportTest.cpp
	src/core/MathTest.cpp
	src/plugins/EqFilterTest.cpp
	src/plugins/WatsynSelectedGraphTest.cpp
	# cluster new-p17-203: a Watsyn note that starts while a graph edit has the staging
	# wavetable half rewritten must render the last complete published wave, never the
	# half-written one.
	src/plugins/WatsynWaveSnapshotTest.cpp
	# The LIVE loudness tap (feature row 24 of docs/FEATURE-LIST-0.3.0.md, the
	# `meter.*` group's live half): what an armed tap reads, the three negative
	# controls that keep it honest - silence reads the sentinel, 10 dB more input
	# reads 10 LU higher, and a buffer fed through an armed tap is bit-identical
	# afterwards - plus zero allocations over 64 fed blocks. The socket proof of
	# the group itself is the registered ctest ControlMeterCommands
	# (tests/control-meter-commands.py).
	src/core/MeterTapTest.cpp
	# Source changes publish the tuner ring reset in the same generation.
	src/core/TunerTapTest.cpp
	# 0.5.0 L08 (MX-137): the phase view's tap - the chosen channel's newest stereo frames in a fixed ring of
	# atomics (the tuner tap's pattern), no allocation on the audio side, no torn frame for a racing reader.
	src/core/PhaseScopeTapTest.cpp
	# 0.5.0 L08 (MX-134): the input meter's live reading - the peaks since its own last read, beside meter.input's
	# take, which it leaves alone - and zero allocations in measure() (the audio thread's half).
	src/core/InputMeterTest.cpp
	src/core/MidiLearnTest.cpp
	# MIDI clock (0.3.0, the `clock.*` group's engine half): the master's
	# emitted message set for a known transport script, and the follower's
	# measurement - a rate it was told only by the pulses, a tempo CHANGE, no
	# lock without a clock, and a lock that drops when the stream stops. The
	# rate arithmetic lives here because an incoming clock cannot be injected
	# into a running instance from outside; the socket half of the proof is the
	# ControlClockCommands ctest below, which drives the real binary.
	src/core/MidiClockTest.cpp
	# Q050-026: WHICH clock the follower listens to. The source `clock.slave_set`
	# stores used to be reported and never read, and every client delivered clock
	# bytes with no sender, so any connected device could drive the tempo. Two
	# ports are configured here and both halves are held: the unchosen one cannot
	# move the follower's state (no pulses, no lock, no Song tempo) while the
	# chosen one updates it, a switch forgets the old device's measurement, an
	# unnamed raw-client stream is still followed while no source is chosen, and a
	# name the follower cannot hold is refused invalid_args.
	src/core/MidiClockSourceTest.cpp
	src/core/MidiLearnThreadTest.cpp
	src/core/MidiRetroCaptureTest.cpp
	src/core/MidiOutQueueTest.cpp
	src/core/FrozenTakeHandoffTest.cpp
	src/core/FrozenTakeRoundTwoTest.cpp
	src/core/FreezeRegionRepairTest.cpp
	src/core/AudioBusPlayHandleQueueTest.cpp
	src/core/AutomationRenderAllocationTest.cpp
	src/core/SlicerTPlayheadTest.cpp
	src/core/MidiEventToByteSeqTest.cpp
	src/core/RetroMidiCaptureCommandsTest.cpp
	src/core/MidiProbabilityPersistenceTest.cpp
	# The engine half of MIDI controller auto-reconnection (0.3.0 feature-list
	# row 18, OWNER-31 item 7): identity parsing (the volatile ALSA address is not
	# part of an identity), a loss counted once on the transition, a device that
	# comes back at a NEW address being re-attached without user action, the mode
	# that records without re-attaching, an explicit unsubscribe never being
	# re-attached, and two clients sharing one name being reported rather than
	# guessed. No physical MIDI device is needed: the client and port are
	# synthetic. The headless Engine supplies the Song and journal for the slot
	# that proves a recorded binding resolves the replacement port after track
	# deletion; the ControlMidiReconnect transcript below drives the real binary
	# and a real external ALSA client.
	src/core/MidiReconnectTest.cpp
	src/core/MixerAbRegressionTest.cpp
	src/core/MixerConcurrencyTest.cpp
	src/core/MixerRoutingBackwardCompatTest.cpp
	src/core/MpeExpressionTest.cpp
	src/core/MpeInputPathTest.cpp
	src/core/MpeNoteStorageTest.cpp
	# Q050-016 round 3: the realtime half of the per-note expression streams -
	# a copy (the audio thread's NotePlayHandle) allocates and frees nothing, an
	# edit while a copy is alive never changes the copy's stream, the final
	# release on the realtime path retires instead of freeing, and copies racing
	# real edits on another thread never tear (absolute AllocationProbe counts).
	src/core/MpeStreamSnapshotTest.cpp
	# Per-note MPE pressure and timbre REACH PLAYBACK (0.3.0 feature-list row 8,
	# task #649): one block rendered through the real note path with the
	# expression against the same block without it, through an in-tree MIT test
	# instrument (tests/src/plugins/MpeTestConsumer.cpp) that turns the two axes
	# it consumes into its own output level - plus the live half, a channel
	# pressure event arriving on the sounding note's own member channel. Without
	# the route every measured ratio is exactly 1.0 and the test fails;
	# docs/MPE.md §4 and docs/KNOWN-LIMITATIONS.md carry the claim it backs.
	src/core/MpePlaybackTest.cpp
	# The #602 modulation layer: the ENGINE half (the LFO arithmetic, the target
	# resolver, the per-block relative write and the persistence round trip) and
	# the SURFACE half (the ten modulator.*/note.expression.* commands, their
	# A16 classes and their inverses). docs/MODULATION.md is the design record.
	src/core/ModulationLayerValueTest.cpp
	src/core/ModulationLayerTest.cpp
	# The audio thread's modulation snapshot is plain bytes (no QPointer copied in the
	# seqlock) and a destroyed target is skipped through its liveness token.
	src/core/ModulationAudioViewTest.cpp
	src/core/ControlModulatorCommandsTest.cpp
	# R6.3: the success path of every command the suite had only ever refused (gate 1's checked-command ratchet).
	src/core/ControlSuccessPathsTest.cpp
	src/core/ControlNoteExpressionCommandsTest.cpp
	# The PROJECT path of the same layer (task #680): modulators authored on the
	# surface, written by project.save, the session cleared, project.open - and
	# the reopened layer asserted identical, value by value and document for
	# document. The three files above cover the layer's own serialiser, the
	# surface and a hand-authored fixture project; none of them round-trips a
	# surface-created layer through a real .mmp.
	src/core/ModulationLayerProjectRoundTripTest.cpp
	src/core/MultiTrackRecorderTest.cpp
	# The recording ENGINE surface (0.3.0, feature rows 14/16/64): the arbitrary
	# input count proved with real takes (N routes, N channels, each take
	# sample-exactly its own channel of the interleaved block), the stereo-bus
	# entry point unchanged, and the retrospective AUDIO window - off by default,
	# bounded, drop-oldest, non-allocating on the producer path, and a take that
	# holds exactly the retained frames. No sound card: the device's own answer is
	# reported by record.input_get_state and asserted over the socket in
	# ControlRecordInputs below.
	src/core/RecordingInputPathTest.cpp
	src/core/RetroAudioCaptureTest.cpp
	# ARCH-4 S1a: the namespace table and resolver. The compatibility table's
	# five GUI-section entries are facts about OTHER files, so the test reads
	# those files' nodeName() literals out of the source tree rather than
	# restating them - it needs the source root, passed below.
	src/core/NamespaceRegistryTest.cpp
	src/core/NoteRandomTest.cpp
	src/core/NoteTransformTest.cpp
	src/core/PhaseDSidechainTest.cpp
	# M0 automation prepass: full mixer renders pinned to the c405233f8 binary,
	# the immediately-previous-setter interpolation start, and the teardown guard.
	src/core/AutomationPrepassProofTest.cpp
	src/core/PhaseFChannelScaleTest.cpp
	src/core/PluginLogoResourceTest.cpp
	src/core/PluginAudioPortsTest.cpp
	src/core/PluginScanCacheTest.cpp
	# 0.5.0 Q050-018: a project with unavailable instruments/effects keeps their XML and reports placeholders
	src/core/MissingDevicesPreserveTest.cpp
	# Q050-018 review round 2: every mixer chain, save/reopen and the real GUI project-loaded notice.
	src/core/MissingDevicesRound2Test.cpp
	# ReverbSC's C allocation API: invalid channel counts and allocation failures.
	src/plugins/ReverbSCCreateTest.cpp
	src/core/VstSubPluginFeaturesTest.cpp
	# The reader half of ARCH-4 S2b, integration rather than unit: a project opened
	# with sections named to SKIP. Pinned here: the content element name DERIVED from
	# the root's type attribute (the "pattern" -> midiclip compat mapping is what
	# proves it, so reading the attribute instead would still pass a naive fixture),
	# the reduction happening on BOTH payload shapes (raw and qUncompress-ed - a
	# legacy .mmpz arrives compressed), a report that describes the DOCUMENT rather
	# than the request (a name that was asked for and never present is not "skipped"),
	# the surviving document being byte-identical to a load that never had the section,
	# and the read-only policy: a partial session refuses to save unless every writer
	# is told, and a REFUSED load is not a partial one.
	src/core/PartialLoadTest.cpp
	src/plugins/PatmanLoadTest.cpp
	# cluster new-p10-380-port-port: a short sid_fillbuffer fill must leave the un-written tail silent.
	src/plugins/SidInstrument.cpp
	src/core/ProjectOpenIntegrityTest.cpp
	src/core/ProjectRecoveryTest.cpp
	src/core/ProjectVersionTest.cpp
	src/core/RackMacrosTest.cpp
	src/core/RackTest.cpp
	src/core/RackZonesTest.cpp
	# 0.5.0 E20/E38: rack zones route notes to their chain; a zone's sample plays.
	src/core/RackZoneRoutingTest.cpp
	# 0.5.0 F2: output buses, MIDI parts, layers, the voice pool; the layer/part/voice commands
	src/core/InstrumentRoutingTest.cpp
	src/core/InstrumentLayersCommandsTest.cpp
	# cluster new-p8-131-port: a layer insert or removal publishes its zones and its layer slot as one edit.
	src/core/InstrumentLayerZonePublishTest.cpp
	# MX-157 (BUGS 11.34): a hosted instrument's latency reaches PDC, measured on rendered audio
	src/core/InstrumentLatencyPdcTest.cpp
	src/core/RecordClipTest.cpp
	src/core/InputChannelRingTest.cpp
	src/core/RecordRingBufferTest.cpp
	# Q050-045: a real SIGKILL of a recording child, recovery measured against the captured frames.
	src/core/RecordingCrashLossTest.cpp
	src/core/RetroMidiRingTest.cpp
	# The VST remote client's per-period MIDI helpers (plugins/VstBase/VstMidiEventBatch.h): an
	# allocation-free sort and a bounded queue. The client itself is an out-of-process executable.
	src/core/VstMidiEventBatchTest.cpp
	# The bounded lock-free MIDI hand-off the instrument hosts use to get events off their audio paths
	# (include/MidiHandoffQueue.h): FIFO, drop-and-count when full, no allocation, no lost accepted event.
	src/core/MidiHandoffQueueTest.cpp
	# Carla's plugin boundary uses the production MIDI encoder and fixed-capacity drain with a native API stand-in.
	src/plugins/CarlaMidiHandoffTest.cpp
	# The SF2 player's note path: the note hand-off from the note stage to the
	# render stage must not allocate on the audio thread.
	src/plugins/Sf2PlayerRealtimeTest.cpp
	# Cluster new-p17-202: the real-time-safe hand-off of Watsyn's oscillator
	# parameters (plugins/Watsyn/WatsynParamHandoff.h) - the editing thread
	# publishes a set, the audio thread takes it, and a reader taken while a
	# publication is in progress never sees half a set and never waits.
	src/plugins/WatsynParamHandoffTest.cpp
	# The system semaphore's timed acquire (the shared-memory transport's audio-thread waits use it).
	src/core/SystemSemaphoreTest.cpp
	src/core/RecordingRealtimeTest.cpp
	src/core/RelativePathsTest.cpp
	src/core/ReversibilityContractTest.cpp
	src/core/ReversibilityUndoTest.cpp
	# The device half of the A16 undo proofs (plugin.bypass, plugin.unload and the
	# device-state restore), split out of ReversibilityUndoTest to keep both files
	# under the file-length ratchet.
	src/core/ReversibilityUndoDeviceTest.cpp
	# The in-app revision timeline (feature-list row 76, OWNER-31 item 30): the
	# list/compare/restore surface over the revision artefacts the engine already
	# writes - real project documents, a real `.bak`/`.rev0`/autosave fixture, and
	# a real git repository for the history half - driven through the registry, so
	# the proof covers the path a socket client takes.
	src/core/RevisionTimelineTest.cpp
	# The keep-3 restore's staging file (cluster new-p12-185-port): a restore
	# whose replacement failed keeps the bytes it staged, and the retry of that
	# restore finds them again - including for a project file whose name holds
	# glob characters. Split out of RevisionTimelineTest to keep that file under
	# the file-length ratchet.
	src/core/ProjectRevisionRestoreStagingTest.cpp
	# ARCH-4 S1b: the elements a loader did not claim, kept verbatim so that
	# opening and re-saving cannot destroy them (SPEC-ARCH-4 1.6.1). The
	# mechanism's own slots are unit-level; the owed proof is the integration
	# pair, which writes a real project file carrying an unknown section and an
	# unknown track child, loads it through Song::loadProject and saves it
	# again - so what is pinned is the whole round trip and the report that
	# names what was left unclaimed, not the helper in isolation.
	src/core/UnclaimedElementsTest.cpp
	# ARCH-4 S1c: the <trackcontainer> children this build cannot construct -
	# a <track> whose `type` has no class here, and an element that is not a
	# track at all (SPEC-ARCH-4 1.6.2). Pins that neither is dropped and neither
	# is coerced into an instrument track, across a real load -> save -> load
	# -> save, with the negative control that a type this build CAN build is
	# claimed and never reported.
	src/core/UnclaimedTrackTypeTest.cpp
	# The bounded, coalescing undo (task #623): the declared depth caps, the
	# drag rule, the byte budget and the refusal of an evicted step.
	src/core/UndoBoundsTest.cpp
	# WF-033: the undo history saved with the project - a real save -> open -> undo
	# round trip through the control surface, checking every undo against the
	# document before the edit (ids, automation links, plug-in data, sample refs).
	src/core/UndoHistoryPersistenceTest.cpp
	# WF-033: the undo history's OWN options - the opt-in, the step and byte caps
	# and what turning persistence off costs. The format half (what a load accepts
	# and discards) stays in UndoHistoryPersistenceTest.cpp; the fixtures both
	# halves build are in tests/src/core/UndoHistoryTestSupport.h.
	src/core/UndoHistoryOptionsTest.cpp
	# Q050-044: project.open over an unsaved session and script.run each leave ONE
	# undo step that restores the whole project, and refuse when it would not fit the budget.
	src/core/SessionCheckpointTest.cpp
	# Q050-044 round 2: both retained checkpoint snapshots count toward the byte cap.
	src/core/SessionCheckpointBudgetTest.cpp
	src/core/TakeLaneCompTest.cpp
	# The take lanes' own half of the comping tests (the composite half stays
	# in TakeLaneCompTest.cpp); the fixtures both halves use are in
	# tests/src/core/TakeLaneTestSupport.h.
	src/core/TakeLaneTest.cpp
	# ARCH-4 slice S7, lanes native (SPEC-ARCH-4 1.1 sketch, migration row
	# <takelanes>+clip lane -> <z:lane id>; BRIEF-2026-09-22 LANE 13 pins this
	# entry to sit immediately after the TakeLaneTest entry above): the
	# document layer - the top-level <z:lanes> section's shape and the lane
	# entities' ids, the save/load/save byte identity of a project that uses
	# lanes, the lane-free additive pin (no section, no takelanes, no z:
	# binding), and the RISK 1 negative control that fails on new-model state
	# planted under <track>.
	src/core/LanesUpconvertTest.cpp
	# ARCH-4 slice S8, scenes native (SPEC-ARCH-4 5.1 row S8, migration row
	# <session>+<scenes>+<clips> -> <z:scenes>+<z:cell>): the document layer -
	# the top-level <z:scenes> section's shape and its (track, scene)
	# coordinates, the legacy <session> block upconverting into it, the
	# save/load/save byte identity of a project that uses scenes, the
	# scene-free additive pin (no section, no block, no z: binding), and the
	# RISK 1 negative control that fails on new-model state planted under
	# <track>.
	src/core/ScenesUpconvertTest.cpp
	src/core/RemotePluginAudioPortsTest.cpp
	src/core/RemotePluginClientE2ETest.cpp
	src/core/RenderJobQueueTest.cpp
	# Q050-167: the render-side worker wake is a post, not a lock - the legacy macOS path's
	# allocation and mutex are gone, and every period of a repeated wake delivers its jobs.
	src/core/AudioWorkerWakeTest.cpp
	src/core/RoutingGraphLiveTest.cpp
	src/core/RoutingGraphScheduleTest.cpp
	src/core/RoutingGraphTest.cpp
	src/core/PdcMixerTest.cpp
	# The patcher node-graph group (feature row 69, docs/PATCHER-GRAPH.md): the
	# two ids and their A16 rows, the read of a chain's derived graph, and the
	# EDIT - proved by rendering the channel through the mixer before and after
	# a re-wire (the re-wired render is the linear one without the bypassed
	# effect's gain), by surviving EffectChain::rebuildRoutingGraph() - what
	# plugin.load calls - and by control.undo. The graph is built from two
	# legacy test effects, because a chain of audio-plugin devices keeps its
	# plain effect loop and would prove nothing.
	src/core/PatcherCommandsTest.cpp
	src/core/VcaGroupTest.cpp
	# Mixer channel pan (MixerChannel::m_panModel): the render, the file and the command.
	src/core/MixerChannelPanTest.cpp
	# 0.5.0 L09 MX-037: the listen bus (AFL / PFL).
	src/core/MixerListenTest.cpp
	# 0.5.0 F1: hardware outputs, cue mixes, hardware inserts
	src/core/HardwareIoTest.cpp
	# 0.5.0 theme 24: the task area's reporting API and task.* commands
	src/core/TaskCenterTest.cpp
	# Destroying a pool drains tasks already queued behind a running task.
	src/core/ThreadPoolTest.cpp
	# 0.5.0 F7: surround channels, the surround panner, the downmix
	src/core/SurroundTest.cpp
	# 0.5.0 F7: the multichannel WAV export of a surround master (PCM-measured renders)
	src/core/SurroundExportTest.cpp
	# 0.5.0 L09: send pan, sends from many channels, nested / linked / shared VCA groups, mixer view modes.
	src/core/MixingRoutingTest.cpp
	# 0.5.0 L08: the strip stages (trim, polarity, mono, pan modes), meters, solo safe, mute / solo many.
	src/core/MixerStripTest.cpp
	# 0.5.0 L10: plugin.copy / chain.copy / plugin.ab, the plug-in organiser and folders.
	src/core/DeviceCopyTest.cpp
	# The organiser, plug-in folder and preferred-format halves of DeviceCopyTest, split out to keep
	# both files under the file-length ratchet.
	src/core/DeviceOrganiserTest.cpp
	# 0.5.0 L10 (rework cluster 5639): plugin.ab store undo and redo after the device was recreated.
	src/core/DeviceAbUndoTest.cpp
	# 0.5.0 L03 / L02: count-in, pre-roll, the record mode, cycling takes, the launcher's bindings.
	src/core/RecordingPassTest.cpp
	# 0.5.0 L11 / L12: search expressions, collections, track presets, importing tracks, the hand-off folder.
	src/core/LibraryProjectsTest.cpp
	# R2.4: the punch region's frame-accurate audio gate (include/PunchWindow.h).
	src/core/PunchWindowTest.cpp
	# Mixer::refreshGroups publishes each channel's VCA gain with ONE store, so a
	# concurrent reader (the audio thread) never sees a grouped channel at unity.
	src/core/VcaRefreshAtomicityTest.cpp
	# The `vca.*` command group's own suite (OWNER-31 item 11, phase-locked
	# multitrack edit groups): the fourteen ids and their schemas, the typed
	# refusals junk arguments must produce, the measured effect of the fader and
	# of membership read back through vca.get_state, and the A16 inverses
	# through control.undo - including a phase-locked move across several
	# tracks, which must be ONE undo step. VcaGroupTest above stays the engine
	# half's suite (#622) and this lane extends it for the edit set; the socket
	# proof of the same claims is the registered ctest ControlVcaCommands below.
	src/core/ControlVcaCommandsTest.cpp
	# The other half of the same proof (the two were one file at 613 lines and
	# Gate 7 refuses a NEW file over 500): the phase-locked move across several
	# tracks, the unlocked/skipped reporting, the lock's three refusals and the
	# entity's own edit set + lock in the mixer element. The helpers both halves
	# use are in src/core/ControlVcaTestSupport.h.
	src/core/ControlVcaEditGroupsTest.cpp
	# vca.set_links / vca.set_parent with a request that changes nothing: no undo step, no
	# modified mark, and the one real change still one undo step (src/core/ControlCommandsVcaNest.cpp).
	src/core/ControlVcaNestTest.cpp
	# R3.5: the phase-locked split, trim, slip and fade.
	src/core/ControlVcaLockedEditsTest.cpp
	# R3.2: clip.consolidate - the clips' own audio (gain, fades, the composite's gate, a
	# muted track included) becomes one clip; refusals change nothing; one undo restores.
	src/core/ClipConsolidateTest.cpp
	# R2.1: input monitoring on the real engine - In/Off/Auto measured on the master meter
	# with a synthetic capture feed, the instrument track's two modes, save/undo.
	src/core/InputMonitorTest.cpp
	# R2.2: a take recorded through the Dummy's loopback fixture lands on the played transient.
	src/core/RecordingLatencyTest.cpp
	# R2.3: AudioDevice::publishCaptured, the one capture publisher ALSA/JACK/SDL/PortAudio call.
	src/core/CapturePublisherTest.cpp
	# R1.1 + R1.3: the 20 Hz LFO zipper measured at the block rate, and the per-sample
	# modulation route that beats it by two orders of magnitude without allocating.
	src/core/ModulationRateTest.cpp
	# R4.1: the run loop a plugin editor asks for - descriptors and timers on the Qt loop.
	src/core/PluginEditorHostTest.cpp
	# R7.4: the engine's render cost - sixteen tracks, plain and with a sample-accurate automated
	# fader - as a tracked number (the opt-in +20% gate, tests/src/core/BenchmarkBaseline.h).
	src/core/EngineRenderBenchmarkTest.cpp
	# The Dummy device keeps real time (it drifted far below it on macOS: sleep overshoot was lost).
	src/core/DummyPacingTest.cpp
	src/core/ScriptBindingsTest.cpp
	# 0.5.0 scripting spine: zene.command reaches every registered command and keeps the sandbox.
	src/core/ScriptCommandBridgeTest.cpp
	# 0.5.0 engine wiring E13: an LV2 plug-in's own state survives a save and a load.
	src/core/Lv2StateTest.cpp
	# 0.5.0 engine wiring E9: a hosted plug-in's sidechain bus is fed the mixer's sidechain.
	src/core/SidechainInputsTest.cpp
	# 0.5.0 engine wiring E25: the lock-free MPSC queue JACK MIDI out is fed through.
	src/core/MpscQueueTest.cpp
	# 0.5.0 W4: JACK MIDI out written at each message's frame, in time order.
	src/core/MidiJackOutScheduleTest.cpp
	# 0.5.0 engine wiring E27: stem separation resamples instead of refusing.
	src/core/SampleBufferResampleTest.cpp
	# Loading an envelope must clear the previous custom LFO waveform when no file is saved.
	src/core/EnvelopeAndLfoParametersTest.cpp
	# 0.5.0 L13 WF-126: the Mackie Control protocol and a surface driving real tracks.
	src/core/McuSurfaceTest.cpp
	src/core/McuSurfaceFeedbackTest.cpp
	# 0.5.0 L04 ED-048: markers - commands, undo, jump, and the project file.
	src/core/ProjectMarkersTest.cpp
	# 0.5.0 L04 ED-011/012: the time range and the edits over it.
	src/core/RangeEditsTest.cpp
	# 0.5.0 L04 ED-042: a sample clip's gain envelope - the curve, the file, the commands.
	src/core/ClipGainEnvelopeTest.cpp
	# 0.5.0 L07 ED-130: a MIDI clip's controller lanes as the file carries them.
	src/core/MidiClipControllersLoadTest.cpp
	# 0.5.0 L04 ED-038: a clip's non-destructive transpose - command, file, played pitch.
	src/core/ClipTransposeTest.cpp
	# 0.5.0 L04 ED-020/043/044: ripple, overlap and edge snapping.
	src/core/ArrangementModesTest.cpp
	# 0.5.0 L06: automation lanes, trim, shapes, transform, thinning, curves, last touched.
	src/core/AutomationLanesTest.cpp
	# 0.5.0 L06 (Q050-022): automation lanes as track children - ownership, folding, delete-with-owner.
	src/core/AutomationLaneChildrenTest.cpp
	# ED-201: automation.write_to_end holds a value from a tick to the song's end.
	src/core/AutomationWriteToEndTest.cpp
	# ED-187: thinning keeps custom-segment endpoints and preserves the evaluated curve.
	src/core/AutomationThinTest.cpp
	# 0.5.0 L07: the note editor's commands - scale, ratchet, add_many, list, find and change, swing, key names.
	src/core/NoteEditingCommandsTest.cpp
	# Luna cluster 5429: a <key-names> <key> element without a numeric key attribute is skipped, not key 0.
	src/core/TrackKeyNamesLoadTest.cpp
	# 0.5.0 L07: note.edit's range validation - a note may not end past MaxSongLength.
	src/core/NoteEditRangeTest.cpp
	# Malformed and overflowing note keys must survive the extended note-range upgrade unchanged.
	src/core/UpgradeExtendedNoteRangeTest.cpp
	# 0.5.0 L07: what the note-shape verbs refuse - a past-the-limit note, a malformed filter field, an unholdable amount.
	src/core/NoteShapeValidationTest.cpp
	# Luna cluster new-p14-41: a note edit that changes NOTHING takes no undo step - note.scale,
	# note.transform_where and note.slide_set, with the real change as the negative control.
	src/core/NoteNoOpEditUndoStepTest.cpp
	# 0.5.0 L07 ED-145/113: a track's MIDI input transform - rules, commands, file, live input.
	src/core/MidiInputTransformTest.cpp
	# 0.5.0 L07 ED-137: quantise audio - transients become warp markers on the grid.
	src/core/WarpQuantizeTest.cpp
	# 0.5.0 L07 ED-122/078: a note's pitch curve; one note edit over several clips.
	src/core/NoteCurvesAcrossTest.cpp
	# 0.5.0 L07 (W3): the note tools - invert, arpeggiate, flam, keyed transpose, repeat, generate, move, split, detect, follow chords.
	src/core/NoteToolsTest.cpp
	# 0.5.0 L07 (W3): repeats, recurrence, articulations, chase, swing, the MIDI offset, lyrics and groups.
	src/core/NotePlaybackTest.cpp
	# cluster new-p11-38-port: the reversibility record the note commands leave behind.
	src/core/NoteCommandTransactionsTest.cpp
	# 0.5.0 Q050-040 (L07): a note under the playhead starts when its track or clip is unmuted.
	src/core/UnmuteChaseTest.cpp
	# 0.5.0 L07 (Q050-015): what each articulation sends before its note, and the map's edits moving the notes with them.
	src/core/ArticulationPlaybackTest.cpp
	# 0.5.0 L08 (W3): pan law, swap, sends switched off, returns, find, gang, order, group colours.
	src/core/MixerToolsTest.cpp
	# 0.5.0 L02 (W3): capture a scene, playlists, setlists, marker actions, release and jump.
	src/core/SessionPerformTest.cpp
	# 0.5.0 L02 (WF-064): sub-scenes - their commands, their launch and the <z:subscenes> section.
	src/core/SessionSubSceneTest.cpp
	# 0.5.0 L03 (W3): comp edges and groups, metronome options, punch here, remaining record time.
	src/core/RecordToolsTest.cpp
	# Coverage lane (M0, 2026-10-08): the Receiver and InsertRecorder devices processed through a real EffectChain.
	src/core/ReceiverEffectTest.cpp
	src/core/ProjectSectionTest.cpp
	src/core/PluginHostMetadataTest.cpp
	# The record tool commands on an instance with NO audio engine: typed refusals, no null deref.
	src/core/RecordToolsNoEngineTest.cpp
	# 0.5.0 L04 (W3): the clip list, sections, remove silence, tab to transient, note rotation.
	src/core/ArrangeToolsTest.cpp
	# 0.5.0 L05/L06 (W3): crop, unique copies, fade curves, asymmetric crossfades, automation switches, trim, moving a point.
	src/core/AudioAutomationToolsTest.cpp
	# 0.5.0 L05 (W3): the YIN pitch tracker, audio to MIDI, the spectrogram, a folder moved as one.
	src/core/AudioAnalysisTest.cpp
	# Waveform marker drags in the AudioFileProcessor view, including the
	# signed frame-offset path used by leftward mouse movement.
	src/core/AudioFileProcessorWaveViewTest.cpp
	# Cluster new-p12-315: the playhead leaves the render thread - playback must
	# not emit isPlaying synchronously, and the GUI poll delivers the coalesced value.
	src/core/AudioFileProcessorPlayheadTest.cpp
	# 0.5.0 Q050-103: the sampler's attack/release envelope and its voice-reuse policy, rendered through the real module.
	src/core/SamplerEnvelopeTest.cpp
	src/core/SamplerVoiceReuseTest.cpp
	# 0.5.0 Q050-484: the SFZ instrument (sfizz) - layers, round robins, key switches, state, the command and window routes.
	src/plugins/SfzPlayerTest.cpp
	src/plugins/SfzPlayerRealtimeTest.cpp
	# 0.5.0 W3 L13 / L14: the light palette, theme editing, density and the other interface commands.
	src/gui/WorkspaceAccessTest.cpp
	# Qt reads the startup scale before QApplication; the selected config must win over its fallback file.
	src/core/AppearanceStartupTest.cpp
	src/core/CustomAccessTest.cpp
	# 0.5.0 L12 (W3): mixer snapshots, project settings and metadata, the document type, edit locks.
	src/core/ProjectStateTest.cpp
	# 0.5.0 L11 (W3): a clip into the library, the index and similar sounds, thumbnails, the matched preview, history.
	src/core/LibraryBrowserTest.cpp
	# 0.5.0 Q050-048: browser feature analysis decodes only its window (WAV/FLAC/OGG), duration from the header.
	src/core/BrowserFeaturesWindowTest.cpp
	src/core/LadspaManagerTest.cpp
	# 0.5.0 L12 (W3): the render queue, batch conversion, the task list, flattening, tracks copied between projects.
	src/core/ProjectToolsTest.cpp
	# 0.5.0 L09 (Q050-027): imported and pasted tracks keep their mixer channel and its hardware routing.
	src/core/ImportTrackIoTest.cpp
	# 0.5.0 F3: frame cuts, zero-crossing snap, spacers, the reference project, per-type documents
	src/core/ArrangingCatchUpTest.cpp
	# The spacer, reference-project and project-switch halves of ArrangingCatchUpTest, split out to keep
	# both files under the file-length ratchet.
	src/core/ArrangingSpacerReferenceTest.cpp
	# 0.5.0 L10 (W3): rack splits, parameter search and aliases, randomise and blend, folding, device options.
	src/core/DeviceToolsTest.cpp
	# The post-fader, delta, clip-print, replace and folding halves of DeviceToolsTest, split out to keep
	# both files under the file-length ratchet.
	src/core/DeviceToolsChainTest.cpp
	# 0.5.0 L10 (W3): the de-esser, transient shaper, vocoder and console devices.
	src/core/DeviceDspTest.cpp
	# DrumSynth rejects envelope tokens longer than its fixed parser capacity.
	src/core/DrumSynthTest.cpp
	# Q050-030: selecting a Neural Amp model never waits behind a load (the effect is compiled in with a test seam).
	src/plugins/NeuralAmpModelLoadTest.cpp
	# catch-up W3: atomic snapshots for device options, the compressor key filter, LV2 latency; reduction meters on bypass.
	src/core/RealtimeSnapshotTest.cpp
	# catch-up W1: undo honesty - redo failures, list copies, the macro pair, arm many, no journal step refused.
	src/core/UndoHonestyTest.cpp
	# The state and preset redo, key-name and inverse-sweep halves of UndoHonestyTest, split out to keep
	# both files under the file-length ratchet.
	src/core/UndoHonestyStateTest.cpp
	# Luna patrol: device undo follows chains and effects rebuilt by track removal's undo.
	src/core/DeviceMovesUndoTest.cpp
	# 0.5.0 L10 (W3): formula modulators and modulators restarted by a track's notes.
	src/core/ModulatorToolsTest.cpp
	# 0.5.0 W3 engine: latency-compensated automation, post-fader inserts, receives, pre-fades.
	src/core/W3EngineTest.cpp
	# 0.5.0 ED-029 / ED-028: an audio clip's content looped and rotated inside the clip, on rendered PCM.
	src/core/AudioClipLoopTest.cpp
	# 0.5.0 L05: fade shapes and handles, automatic and range fades, splits, slices, beat mapping.
	src/core/AudioEditingTest.cpp
	# 0.5.0 W2 gaps (audit-rows-1): clip.join, ripple on move and insert, controller.map / unmap.
	src/core/ClipJoinRippleTest.cpp
	# Feature row 50 (0.3.0): the Lua API's DAW-control half - the mixer channel
	# and effect-chain bindings. Drives a REAL mixer channel and the chain the
	# control surface addresses from Lua, reads the engine back (not the script's
	# own buffer), and checks the version surface zene.apiSurface() reports.
	src/core/ScriptDawBindingTest.cpp
	# Round 6 of the script-binding rework: the removal and undo cases, split out of
	# ScriptDawBindingTest so that file stays under the file-length gate.
	src/core/ScriptDawRemovalUndoTest.cpp
	# review-51e7da51f: deterministic recorded-device registration handoff checks.
	src/core/ScriptDawModelRegistrationTest.cpp
	# cluster new-p14-285: a FULL script command queue must refuse a DAW edit and
	# say so, instead of dropping the edit while the script's mutator returns
	# normally. The queue is filled through the same public enqueue() a script
	# uses; the refusal is read from the script's own console.
	src/core/ScriptDawQueueFullTest.cpp
	# cluster new-p14-285 (round 2): a model edit travels as TWO queue commands
	# (its undo checkpoint and its write), so the pair must reserve its room as
	# a whole: a queue with one slot left must refuse the change whole, not
	# take the checkpoint and strand an empty undo step.
	src/core/ScriptDawPairReservationTest.cpp
	src/core/ScriptEngineTest.cpp
	# Q050-051: the sandboxed Lua `require` - traversal, absolute and Windows paths,
	# symlink and junction escapes, cycles, bounds, and the sandbox staying closed in modules.
	src/core/ScriptImportTest.cpp
	# board card #708: the schedule clock's PURE half - the boundary grid, the
	# registry's typed refusals and the livecode.* A16 rows, no audio device.
	src/core/ScriptClockTest.cpp
	# CODE-6: the Lua MEMORY budget beside the instruction budget. The same
	# script completes under the shipping cap and is refused, typed, under one
	# it cannot fit in - the pair is the proof, because a refusal on its own is
	# also what a broken sandbox looks like. Also asserts the surface verb
	# (script.set_memory_budget) and the measurements script.run reports.
	src/core/ScriptMemoryBudgetTest.cpp
	src/core/ScriptStabilisationTest.cpp
	src/core/SlideNotesTest.cpp
	# The Standard MIDI File conductor track (feature row 33: tempo-map export /
	# SMF cross-DAW interchange): the four interchange.* ids, their schemas and
	# their typed refusals, and the round trip that reads the FILE back and
	# compares the MAP - a tempo-and-metre map exported, the session's map
	# cleared, the file imported, and the map read off the wire again - with the
	# file's own bytes checked against the format independently of the reader.
	# The two halves are separate executables so each file stays inside the
	# new-file cap; the fixtures both use are in SmfInterchangeTestSupport.h.
	src/core/SmfInterchangeTest.cpp
	src/core/SmfInterchangeRoundTripTest.cpp
	# Q050-020: the MIDI export's conductor track, track identity and 480-ppq timing, parsed back.
	src/core/MidiExportTest.cpp
	# DAWproject import / export (feature row 37): the round trip that compares
	# the MODEL - tracks, clips, notes, mixer channels, tempo map - rather than
	# the file or its hash. A separate executable so the file stays inside the
	# new-file cap; it builds a real Engine and drives the dawproject.* commands.
	src/core/DawProjectInterchangeRoundTripTest.cpp
	# 0.5.0 Q050-001: the DAWproject container reads DEFLATE (bounded) and lists every deviation
	# (corrupt, CRC-failing, unsupported, oversized entries; missing referenced files); builds a real
	# Engine and drives dawproject.import / dawproject.read on containers it assembles byte by byte.
	src/core/DawProjectDeflateImportTest.cpp
	# 0.5.0 F4 (WF-038): AAF import through LibAAF; the fixture cases skip without the library
	src/core/AafImportTest.cpp
	# 0.5.0 F5 (ED-054): the video track; the FFmpeg cases skip without the library
	src/core/VideoTrackTest.cpp
	# cluster new-p8-242-port: the FFmpeg stream's refused seek (the video track's decoder behind it)
	src/core/VideoFfmpegTest.cpp
	# 0.5.0 L12 (Q050-042): old-version patterns keep their note and pattern lengths after upgrade and save/reopen.
	src/core/LegacyPatternUpgradeTest.cpp
	src/core/VideoTestClipTest.cpp
	src/core/StableTrackIdsTest.cpp
	# ARCH-4 slice S3: the id-family registry (clip/lane/note/scene/warp over
	# the one counter) and the per-object revision pair - absent rev == 0, an
	# unrevised project saves byte-identically with no rev/writer anywhere,
	# and the duplicate-id repair rule applied to the clip family. Registered
	# here because plans/BRIEFS-2026-09-22-WAVE1.md LANE 6 pins its anchor to
	# the StableTrackIdsTest entry above (union-of-entries; siblings append
	# theirs after, never reorder this block).
	src/core/ProjectRevIdsTest.cpp
	# ARCH-4 slice S6 (SPEC-ARCH-4 1.9 Requirement 8): the append-only
	# <z:provenance> section - changes driven through the control registry,
	# SAVED, read back with a FRESH DataFile load ("the restart") with the seq
	# chain and both digests intact, a second save proven APPEND-only (the
	# first file's entries are a byte prefix of the second's), the additive
	# rule (no recorded change, no section) and the journal-shaped bound.
	# Registered where LANE 12 pins its anchor: immediately after the
	# ProjectRevIdsTest entry above (union-of-entries; siblings append theirs
	# after, never reorder this block).
	src/core/ProvenanceSectionTest.cpp
	# The <z:provenance> version (v="2" only with nested ids) and a file holding two provenance sections.
	src/core/ProvenanceVersionTest.cpp
	src/core/StemExportTest.cpp
	# Clusters 3953/4875/1234: the render.stems verb through a stand-in export child - what a
	# failed export says, what it reports as written, and what it leaves on disk. Registered
	# beside the engine's own stem export above, so the two stem-export regressions sit
	# together; the ctest number it gets here is three digits, which is what the
	# automated proof check reads out of `ctest -N` (a two-digit number is printed with
	# a padded space and is invisible to it).
	src/core/ControlStemExportTest.cpp
	# The delay ring must rebuild its bounds and cursor when the sample rate changes.
	src/core/StereoDelay.cpp
	src/core/TelemetryTest.cpp
	# CODE-7: the telemetry transport's two structural properties - https only
	# (a plain-http endpoint is refused before the delivery seam is reached, so
	# nothing was handed over) and never blocking (a send to TEST-NET-1, which
	# never answers, returns at once; the pre-CODE-7 transport waited ten
	# seconds in a nested event loop there).
	src/core/TelemetryTransportTest.cpp
	src/core/TempoMapPersistenceTest.cpp
	src/core/TempoMapTest.cpp
	# Tap Tempo's sync button must leave the project tempo alone until a tap
	# interval has produced a BPM value.
	src/core/TapTempoTest.cpp
	src/core/TimelineTest.cpp
	src/core/TrackFolderTest.cpp
	src/core/TwoTrackRecordingHarness.cpp
	src/core/WarpMarkersTest.cpp
	# The 0.5.0 shell (docs/UI-SHELL.md): its region/view/refusal rules without a display,
	# then the rendered shell - editors moved between regions without being lost, the footer
	# following the layout, the layout surviving a restart, MDI-era geometry ignored safely,
	# and BUGS_FOUND 11.30's clamp guarantee (carried over from the retired Focus Desk tests).
	src/gui/ShellLayoutTest.cpp
	src/gui/ShellTest.cpp
	# 0.5.0 wave 3 themes 8/22: region tabs, footer/header overflow, the Tools home, personal vs project layouts
	src/gui/ShellHomesTest.cpp
	# The design tokens: every declared text/surface pair AA, the generated stylesheet AA, the
	# velocity ramp ordered in lightness, every icon a path that paints at the screen's scale.
	src/gui/DesignTokensTest.cpp
	# The 0.5.0 note editor (docs/NOTE-EDITOR.md): its geometry, folding, snapping and lane table
	# without a display, then the editor's tools driven by mouse and keys - each gesture one
	# command and one undo step - and note.edit's whole-batch validation.
	src/gui/NoteGridTest.cpp
	src/gui/NoteEditorTest.cpp
	# 0.5.0 wave 3 theme 10/17: canvases by keyboard; the editable item list, the staged audit, announcements
	src/gui/CanvasKeyboardTest.cpp
	src/gui/ItemListPanelTest.cpp
	# 0.5.0 themes 10/17: the List button on every canvas with an item list, and View > Screen Reader
	# Announcements, in the real application
	src/gui/ItemListButtonsTest.cpp
	src/gui/RowTableViewTest.cpp
	# 0.5.0 wave 3 theme 11/15: lane scales and readouts; the Sample Editor's range fields
	src/gui/LaneReadoutTest.cpp
	src/gui/SampleEditorDialogTest.cpp
	# 0.5.0 ED-176: the crossfade editor, offered where clips overlap and driven through its fields.
	src/gui/CrossfadeEditorTest.cpp
	# LADSPA Ports renders sample-rate-dependent ranges and their displayed defaults.
	src/gui/LadspaPortDialogTest.cpp
	# 0.5.0 theme 1: the note editor follows FL Studio's piano roll.
	src/gui/NoteEditorFlTest.cpp
	# 0.5.0 lane 3: the shell selection, the Inspector and the Device panel's cards and
	# modulators over a real engine (the build's plugin modules).
	src/gui/DevicePanelTest.cpp
	# 0.5.0 plugin-hosting lane: the effect rack's own "Add effect" picks from the device picker.
	src/gui/EffectRackAddTest.cpp
	# 0.5.0 Q050-087: the device-chain patcher canvas - nodes, cables, add / connect / disconnect / reorder, Shift-click fan-out.
	src/gui/PatcherCanvasTest.cpp
	# 0.5.0 Q050-071: the in-process editor probe of the third-party plug-in compatibility matrix (skips unless ZENE_COMPAT_PROBE names a subject).
	src/gui/PluginCompatEditorProbe.cpp
	# BUG new-p9-242: LV2 help subwindows are destroyed with the view that owns them.
	src/gui/Lv2ViewBaseLifetimeTest.cpp
	# 0.5.0 L11 / L12 (W3): the project menu's new entries, a track's lock, the gallery, the browser's remembered sources.
	src/gui/LibraryMenusTest.cpp
	# The shell window's own startup: a floating editor hides its whole frame, and an unattended
	# first run does not record the skipped setup as done.
	src/gui/MainWindowTest.cpp
	# The custom theme's hot reload survives the stylesheet being deleted and recreated.
	src/gui/LmmsStyleTest.cpp
	# UI precondition #1 (040/ui-precond): include/WcagContrast.h at the AA
	# boundary, and include/Accessibility.h on the shell surface the owner
	# manual-tests - the helper, then the footer switches, the chrome bands and
	# the panel frames as the sweep leaves them.
	src/gui/WcagContrastTest.cpp
	# The shipped themes' declared text/background pairs against AA (4.5:1), as a ratchet.
	src/gui/ThemeContrastTest.cpp
	src/gui/AccessibilityHelperTest.cpp
	# R8.5: every surface built on its own, walked - no unnamed focusable widget (a ratchet).
	src/gui/AccessibilityWalkTest.cpp
	# R8.5: a knob takes Tab focus, answers the arrow/page/Home/End keys and is named.
	src/gui/KnobKeyboardTest.cpp
	# LFO info text's backing rectangle follows the font rendered by the graph.
	src/gui/LfoGraphTest.cpp
	# Every control's context menu sets its automation mode (automation.mode_set).
	src/gui/AutomationModeMenuTest.cpp
	# ED-194: the automation editor's keys and item list move points through automation.move_point.
	src/gui/AutomationEditorEditingTest.cpp
	# The arranger disconnect path waits until render has finished traversing automation targets.
	src/gui/AutomationClipDisconnectTest.cpp
	# BUGS_FOUND 11.12: a removed VCA group's strip never paints its freed fader model.
	src/gui/VcaStripLifetimeTest.cpp
	# m0 blocker (trackview-uaf): a track or clip removed before its queued view is built gets no view.
	src/gui/TrackViewDeferredLifetimeTest.cpp
	# m0 blocker (trackview-uaf follow-up): a rack's queued removal of a view that is already gone does nothing.
	src/gui/RackDeferredLifetimeTest.cpp
	# BUGS_FOUND 10: removing a linked control frees its detached layout item.
	src/gui/LinkedModelGroupViewLifetimeTest.cpp
	# 0.5.0 theme 6: sends on faders - the banner names the return, every fader labelled, "No send" faders dimmed in place.
	src/gui/MixerSendsOnFadersTest.cpp
	# 0.5.0 theme 16: mixer views, densities, type badges, routing by name, mode bar, sparse routing matrix.
	src/gui/MixerModesTest.cpp
	# 0.5.0 L08 metering: ballistics (MX-128), the Selected filter (MX-005), live input meters (MX-134), the phase view (MX-137).
	src/gui/MixerMeteringTest.cpp
	# The selected mixer channel follows its identity through a removal, a removal of itself, and a reorder.
	src/gui/MixerSelectionIdentityTest.cpp
	# 0.5.0 themes 2/9: ruler lanes, anchored zoom, the drag preview, locked clips.
	src/gui/ArrangerTimelineTest.cpp
	# 0.5.0 L03: the punch ruler band (Q050-147) and the take-lane rows/waveforms (Q050-123).
	src/gui/ArrangerTakePunchTest.cpp
	# 0.5.0 L04 (Q050-031): files dragged out of a plug-in editor become clips where they are dropped.
	src/gui/ArrangerFileDropTest.cpp
	# 4218c1e30 rework: the arranger's trim of a linked MIDI clip keeps the notes of the clip and its peers.
	src/gui/MidiClipLinkedEditTest.cpp
	# new-p14-176: the beat step menu names and edits the step under the cursor on a clip with millions of steps.
	src/gui/MidiClipStepMenuTest.cpp
	# catch-up W1: an XY pad drag is one undo step.
	src/gui/XyPadPairTest.cpp
	# M3: folders from the interface - make one, move a track in, collapse, pin, take it out.
	src/gui/TrackFolderMenuTest.cpp
	# 0.5.0 ED-035: a folder's row draws its children's clips as summary clips, each moved, copied or deleted as one.
	src/gui/FolderSummaryLaneTest.cpp
	# 0.5.0 L06 (Q050-022): a lane is drawn as a collapsible child under its owner in the arranger.
	src/gui/AutomationLaneChildGuiTest.cpp
	# 0.5.0 theme 22: the main window opens where, and as large as, it was left (ui/main.geometry).
	src/gui/MainWindowGeometryTest.cpp
	# BUG new-p11-96: clicking the envelope graph turns a negative amount off.
	src/gui/EnvelopeGraphTest.cpp
	# Edit > Undo History: lists the record, undoes and redoes through the registry.
	src/gui/UndoHistoryPanelTest.cpp
	# The export dialog's dither and resampling choices, through export.set_*.
	src/gui/ExportQualityControlsTest.cpp
	# 0.5.0 F7: the export dialog's Channels row (stereo / surround)
	src/gui/ExportChannelsTest.cpp
	# The follow-action chain's rows: what the dialog writes for a loaded chance and for an edited one.
	src/gui/FollowActionEditorTest.cpp
	# M0 coverage: the Gate, Modulation and Receiver effects' control dialogs, built over the real modules.
	src/gui/PluginControlDialogsTest.cpp
	# M0 coverage: the mixer's windows (routing matrix, large meters, EQ compare), hardware / surround menus, the surround panner.
	src/gui/MixerWindowsTest.cpp
	# Regressions for stale panner fields, route refresh, and ignored non-left clicks.
	src/gui/SurroundPannerTest.cpp
	# M0 coverage: a device's menu (insert, replace, copy, A/B, parameters), the channel picker and the picker organiser.
	src/gui/EffectDeviceMenuTest.cpp
	src/gui/EffectPickerOrganiserTest.cpp
	# M0 coverage: the plug-in editor window, its host header and the Show plugin editor button.
	src/gui/PluginNativeEditorTest.cpp
	# M0 coverage: the reference project's window and its track / clip / device copies.
	src/gui/ReferenceProjectPanelTest.cpp
	# M0 coverage: the telemetry consent window, File > Import AAF..., track header icons and the level meter.
	src/gui/TelemetryConsentDialogTest.cpp
	src/gui/AafImportMenuTest.cpp
	src/gui/TrackIconsLevelBarTest.cpp
	# M0 coverage: the arranger's clip keys and the navigator strip.
	src/gui/ArrangerKeysNavigatorTest.cpp
	# M0 coverage: the audio waveform drawn behind a MIDI clip's notes.
	src/gui/NoteBackgroundAudioTest.cpp
	# M0 coverage: the arranger's range / audition / zoom tools and the video lane.
	src/gui/ArrangerOverlaysVideoLaneTest.cpp
	# The transport's Sync toggle (link.set_enabled) and its session read-out.
	src/gui/SessionSyncActionTest.cpp
	# The piano roll's Randomize / Humanize / Scale velocities items (note.randomize, note.velocity_scale).
	src/gui/NoteTransformActionsTest.cpp
	# Edit > MPE Input (device.mpe_set), following the real state.
	src/gui/MpeInputActionTest.cpp
	# Edit > Tempo Map: every edit through a transport.tempo_map_* command.
	src/gui/TempoMapPanelTest.cpp
	# Q050-154: Edit > Live Code - schedule, cancel, load and undo through livecode.*.
	src/gui/LivecodePanelTest.cpp
	# Track > Articulations: the editor leaves the map track.articulations_set leaves; one undo step per edit.
	src/gui/ArticulationMapEditorTest.cpp
	# The file browser's Tags menu (browser.tag.add / browser.tag.remove), over a throwaway store.
	src/gui/BrowserTagMenuTest.cpp
	# 0.5.0 L11 (Q050-133): a browser file drag carries a text/uri-list URL beside its StringPair payload.
	src/gui/BrowserDragUrlTest.cpp
	# File search results queued for an older query are ignored by the current browser search.
	src/gui/FileSearchJobTest.cpp
	# File revealing uses xdg-open when the MIME query cannot find a directory handler.
	src/gui/FileRevealerTest.cpp
	# The song editor's Views menu: visibility sets applied, all tracks shown, saved, removed.
	src/gui/VisibilitySetMenuTest.cpp
	# Edit > Capture MIDI names what the retrospective capture window holds.
	src/gui/CaptureMidiMenuTest.cpp
	# Q050-007: the visible Save As v2 choice - the File menu entry and the dialog's checkbox
	# driven as widgets, the file checked on disk and the project.save transaction in the log.
	src/gui/ProjectContainerSaveAsGuiTest.cpp
	# project.save_version on a project whose version number cannot be advanced: the command
	# must refuse at once and leave the file it cannot replace alone.
	src/gui/ControlCommandsSaveVersionTest.cpp
	# A MIDI-driven control's menu: soft takeover and LED feedback through controller.*.
	src/gui/MidiControllerMenuTest.cpp
	# Edit > Controller Templates over a throwaway working directory.
	src/gui/ControllerTemplateMenuTest.cpp
	# The track menu's Freeze / Bounce items and File's DAWproject items.
	src/gui/TrackRenderMenuTest.cpp
	# The piano roll's Groove menu: extract, apply with strength, quantise with strength.
	src/gui/GrooveMenuTest.cpp
	# A mixer strip's tooltip names its delay compensation.
	# A sample clip's Gain and fades (clip.set_gain / clip.set_fade), with one control.undo.
	src/gui/ClipEditsMenuTest.cpp
	# Q050-125: the clip-gain handle, envelope points and the polarity item are DRAGGED/TRIGGERED on the
	# real clip view, and the drawn waveform follows the gain curve.
	src/gui/ClipWaveformEditTest.cpp
	# A sample clip's Warp mode submenu (warp.stretch), Q050-036's five spec modes.
	src/gui/SampleClipWarpModeMenuTest.cpp
	# A track menu's Effect chain presets (chain.save / apply / remove / list) against a real effect.
	src/gui/ChainPresetMenuTest.cpp
	# A mixer channel's Rack menu (rack.* chains, routing and macros).
	src/gui/RackMenuTest.cpp
	# The pin matrix's hover cursor, and what a drag over it does.
	src/gui/PinConnectorTest.cpp
	# Edit > Modulators (window.modulators; modulator.*), against one real effect on a mixer channel.
	src/gui/ModulatorPanelTest.cpp
	# Edit > Progression (window.progression): the catalogue spelt in the selected scale
	# (chord.progression_list) and a write through chord.progression_generate, one undo.
	src/gui/ChordProgressionPanelTest.cpp
	# File > Script Memory Budget... (script.set_memory_budget).
	src/gui/ScriptBudgetActionTest.cpp
	# An effect's Hosting submenu (oop.set_mode / oop.restart), against one real effect.
	src/gui/EffectHostingMenuTest.cpp
	# Anchored effect searches continue to match favourite names with their display star.
	src/gui/EffectSelectDialogFilterTest.cpp
	# A mixer channel menu's Allow feedback sends (feedback.enable / disable).
	src/gui/FeedbackModeActionTest.cpp
	# Edit > Session Tuning (mts.load_scale / load_keymap / reset / master_set).
	src/gui/SessionTuningMenuTest.cpp
	# The Microtuner dialog's own .scl file path: a file that describes no scale is refused
	# before the Song's scale or the session table is touched.
	src/gui/MicrotunerScaleFileTest.cpp
	# File > Recover Recordings (record.recovery_*), over two journalled WAV takes.
	src/gui/RecordingRecoveryMenuTest.cpp
	# File > Render Presets (export.preset_* / render.render), over a throwaway preset store.
	src/gui/RenderPresetMenuTest.cpp
	# 0.5.0 L12 (Q050-009): Navigate > Range > Render Selection/Export Sections forwards the
	# exact range ticks and the chosen output to render.render / render.sections.
	src/gui/RenderRangeMenuTest.cpp
	# 0.5.0 L12 (Q050-009): the export dialog's render-preset picker - save from the dialog, reopen,
	# select, and render at the preset's own rate and depth.
	src/gui/ExportProjectDialogPresetTest.cpp
	# Edit > Plugin Quarantine (plugin.scan_cache_* / plugin.rescan), over a throwaway scan-cache file.
	src/gui/PluginQuarantineMenuTest.cpp
	# Help > Crash Reports (crash.enable / disable / list / acknowledge / discard), over throwaway directories.
	src/gui/CrashReportMenuTest.cpp
	# Edit > MIDI Clock (clock.master_set / slave_set / get_state), over a throwaway config.
	src/gui/MidiClockMenuTest.cpp
	# Edit > MIDI Controller Reconnect (midi.reconnect_arm / _status), over a throwaway config.
	src/gui/MidiReconnectActionTest.cpp
	src/gui/MixerLatencyTextTest.cpp
	# M3.4: the command palette - what it lists, how it ranks, what it runs.
	src/gui/CommandPaletteTest.cpp
	# 0.5.0 W1: the schema-built command form, its dispatcher, choice menus and the list dialog.
	src/gui/CommandFormTest.cpp
	# M3.7: the mixer's VCA strip and the channel's VCA menu; Q050-171: the Groups list spec and the
	# Clip menu's Move With Edit Group entry.
	src/gui/VcaStripViewTest.cpp
	# R5.3: the clip-launch grid - launch, stop and scene-launch from the grid produce the
	# socket-visible state; every cell focusable and announced; the arrow keys.
	src/gui/SessionGridViewTest.cpp
	# 0.5.0 L02 (WF-055): an empty cell records audio from its menu, says so, and the grid lands its own take.
	src/gui/SessionGridRecordTest.cpp
	# The clip launcher's Follow Actions / arrangement-record row.
	src/gui/SessionTransportBarTest.cpp
	# 0.5.0 L02 (WF-071): perform mode, the launcher's live show window, over the real application.
	src/gui/PerformViewTest.cpp
	# 0.5.0 L02 (WF-064): sub-scenes from the grid - select cells, make one, launch it - over the real application.
	src/gui/SessionSubSceneGridTest.cpp
	# 0.5.0 L14 (Q050-235): the touch workspace read once at start, and its launcher cells, scene and stop
	# actions at 44 x 44 Qt logical pixels with the press landing anywhere in that rectangle.
	src/gui/TouchWorkspaceTest.cpp
	# M3 item 6: the take lanes panel - comp, audition and consolidate from its own buttons.
	src/gui/TakeLanesPanelTest.cpp
	# 0.5.0 F2 interface: an instrument track's output buses, MIDI parts, layer rack and per-voice modulation.
	src/gui/InstrumentSetupUiTest.cpp
	# M3 item 4: the keyboard shortcuts page lists what the menus bind, filterable.
	src/gui/ShortcutsPageTest.cpp
	# shortcuts.* driven against the real menus: a value that is not key text, an undo of a set
	# that must not clear bindings it never touched, and a reset of an action no menu has.
	src/gui/ShortcutsCommandsTest.cpp
	# 0.5.0 wave 3 theme 19: the workflow recording indicator and the run report
	src/gui/WorkflowFeedbackTest.cpp
	# CPU load indicator: a steady 1% profiler reading advances the main readout, and it falls back to 0%.
	src/gui/CPULoadWidgetTest.cpp
	# 0.5.0 theme 24: the task indicator and its list
	src/gui/TaskIndicatorTest.cpp
	# 0.5.0 Q050-179 (WF-085): the header's Rec left readout shows record.remaining's estimate.
	src/gui/RecordRemainingTest.cpp
	# Version number insertion with a dotted parent path.
	src/gui/VersionedSaveDialogTest.cpp
	# M3 item 10: the start hub reopens a recent project through project.open.
	src/gui/StartHubTest.cpp
	# 0.5.0 L14 / L11: the manual and reference, hover help, tours, appearance, the library search view.
	src/gui/HelpAccessTest.cpp
	# Luna 4016: control.surface_report keeps the whole menu path of a nested action.
	src/gui/AgentSurfaceReportTest.cpp
	# Help > Online Help over the real main window: the desktop taking the URL is said in
	# the status bar, and taking it is not.
	src/gui/OnlineHelpNoticeTest.cpp
	# R7.3: the piano roll's frame time at 1k/5k/10k/50k notes (an opt-in +20% gate).
	src/gui/PianoRollBenchmarkTest.cpp
	# The piano roll's live MIDI record pass: a stopped pass leaves no pending start behind.
	src/gui/PianoRollRecordTest.cpp
	# The piano view's focus handler must tolerate its model not being assigned yet.
	src/gui/PianoViewFocusTest.cpp
	# BUG new-p10-399: painting the step recorder with a zero tick length completes.
	src/gui/StepRecorderWidgetTest.cpp
	src/gui/MidiLearnGuiTest.cpp
	# Hydrogen's TinyXML byte-escape compatibility path, including incomplete input.
	src/plugins/HydrogenImportLocalFileMngTest.cpp
	# Saved EQ bands must be drawn on the first curve paint.
	src/gui/EqCurveTest.cpp
	# The SlicerT waveform keeps its first and last slice points fixed.
	src/plugins/SlicerTWaveformTest.cpp
	# BitInvader's note state lives in the note handle: no heap allocation on the render path.
	src/plugins/BitInvaderAllocationTest.cpp
	src/tracks/AutomationTrackTest.cpp
	# Inherited clip paths that dereferenced getGUI() unguarded, run headless.
	src/tracks/HeadlessGuiGuardTest.cpp
	# The instrument track's render path: it reads no GUI state, it allocates no
	# clip container, and the MPE expression it plays is one atomic snapshot.
	src/tracks/InstrumentTrackRenderPathTest.cpp
	src/tracks/SampleClipStretchTest.cpp
	# R3.1: the composite plays, and a lane can be auditioned.
	src/tracks/TakeLaneCompPlaybackTest.cpp
	# Owner decision 12: the optional Rubber Band stretch mode, through the same clip path.
	src/tracks/SampleClipRubberBandTest.cpp
	# Q050-036: the specified warp mode set (Beats, Tones, Texture, Re-Pitch,
	# Complex) - selection through the command, one render per mode, and a
	# project-file round trip with different modes on separate clips.
	src/tracks/SampleClipWarpModesTest.cpp
	src/tracks/SampleClipWarpTest.cpp
	src/tracks/SampleClipWindowTest.cpp
	# Soundpipe lifecycle methods must report SP_OK on successful setup and teardown.
	src/plugins/ReverbSCBaseTest.cpp
	# The undo half of session.arrangement_record_land: the pass is ONE undo step, whatever it lands.
	# REGISTERED AT THE END OF THIS LIST ON PURPOSE: ctest numbers its tests in registration order,
	# and the repo's negative control (the harness that reverts a branch's product change and runs
	# the branch's own tests to prove they fail without it) reads that list with a pattern that sees
	# only THREE-DIGIT test numbers - ctest pads a two-digit number ("Test  #27:"), so the
	# ControlSessionRecordLandTest binary (number 27) is invisible to it. From the end of this list
	# the number is three digits, so the mechanical proof can see this regression. The test's own
	# header says the same thing.
	src/core/ControlSessionRecordLandUndoTest.cpp
	# cluster new-p14-38: the no-op note transforms must not advertise an undo -
	# a call that changed nothing records no transaction, so the journal's top is
	# still the last REAL edit. Its own target, so the proof is one suite.
	src/core/ControlNoteNoOpUndoTest.cpp
)

# Ogg is optional; these regressions require the Ogg decoder and compile against the concrete Ogg writer.
if(LMMS_HAVE_OGGVORBIS)
	list(APPEND LMMS_TESTS
		src/core/AudioFileOggInitTest.cpp
		src/core/SampleDecoderOggChannelTest.cpp
		src/core/SampleDecoderOggStateTest.cpp
	)
endif()

# cluster new-p14-269/271: the Ogg Vorbis decoder's channel handling and the release of its
# decoder state on every exit (the read-error path included).
# cluster new-p14-276: the bounded decoder refuses a limit it cannot turn into a frame count.
list(APPEND LMMS_TESTS src/core/SampleDecoderLimitTest.cpp)

# Offline stem separation tests (on by default, owner decision 14; see doc/STEM-SPLIT.md).
IF(LMMS_HAVE_STEM_SPLIT)
	LIST(APPEND LMMS_TESTS
		src/core/StemJobManagerTest.cpp
		src/core/StemModelStoreTest.cpp
		# Q050-062.1: the quality table and the hop formula every backend shares.
		src/core/StemQualityTest.cpp
		src/core/StemSplitPipelineTest.cpp
	)
	IF(LMMS_HAVE_ONNXRUNTIME)
		LIST(APPEND LMMS_TESTS src/core/OnnxRuntimeStemSeparatorTest.cpp)
	ENDIF()
ENDIF()

# Ableton Link (WF-051, docs/LINK-SYNC.md): the follower's beat-grid arithmetic and its
# realtime rule. Every build: the follower compiles against the stub session without Link.
LIST(APPEND LMMS_TESTS src/core/LinkPhaseLockTest.cpp)
# Q050-024 (docs/LINK-AUDIO.md): the PCM stream between instances - codec, ring, loopback exchange,
# the audio thread's allocation count, a 3 s two-thread stress, the registered commands. Loopback only.
LIST(APPEND LMMS_TESTS src/core/LinkAudioStreamTest.cpp)

# JACK port state must be replaced when a dead client is reopened.
IF(LMMS_HAVE_JACK)
	LIST(APPEND LMMS_TESTS src/core/AudioJackPortRestartTest.cpp)
ENDIF()

# Ableton Link (WF-051, docs/LINK-SYNC.md): the vendored library and its wrapper.
# Only with the library in the build (WANT_ABLETON_LINK); RUN_SERIAL below.
IF(LMMS_HAVE_ABLETON_LINK)
	LIST(APPEND LMMS_TESTS
		src/core/AbletonLinkSessionTest.cpp
		# Zene's engine in a real Link session against a peer built from Link's own
		# API (that test constructs ableton::Link itself, hence the link below).
		src/core/AbletonLinkInteropTest.cpp
	)
ENDIF()

# Peer-to-peer collaboration (0.5.0 F6): the invite, the TLS-PSK transport and the port-mapping
# codecs exist only with Qt Network.
IF(ZENE_HAVE_COLLAB)
	LIST(APPEND LMMS_TESTS
		src/core/CollabPolicyTest.cpp
		src/core/CollabTransportTest.cpp
		src/core/PortMappingCodecTest.cpp
		src/core/PortMappingOrchestratorTest.cpp
		src/core/UpnpMapperFakeRouterTest.cpp
		# collab.show_panel (requires: display) over the real main window.
		src/gui/CollabPanelTest.cpp
	)
ENDIF()

# Session View data layer tests (native engine code since ARCH-4 S10 removed
# WANT_SESSION_VIEW; see include/SessionModel.h).
	LIST(APPEND LMMS_TESTS
		src/core/SessionModelTest.cpp
		# Launch scheduler + quantisation engine (task #595): the pure launch
		# decision, the LaunchMode state machine, the lock-free hand-off.
		src/core/SessionSchedulerTest.cpp
		# Behaviour-preservation proof: a real headless render is byte-identical
		# with nothing launched, and differs with a launched clip.
		src/core/SessionSchedulerRenderTest.cpp
		# The published cell content and the view the render path reads (board
		# card #597): the hand-off, the position, the empty-cell control and the
		# reset. Its own file because SessionSchedulerTest.cpp is grandfathered
		# at 549 lines in the whole-tree file-length ratchet.
		src/core/SessionPlaybackTest.cpp
		# THE RENDER proof for #597: a launched slot holding a pattern makes a
		# silent arrangement sound. Own file because it needs a real instrument
		# (the render fixture above is deliberately plugin-free) and because it
		# measures silence on the PCM payload rather than on the file.
		src/core/SessionPlaybackRenderTest.cpp
		# Follow Actions + Arrangement Record (task #641, the #596 engine
		# halves). Two files, because they measured 667 lines as one and the
		# file-length ratchet is not moved for a new feature:
		#   - SessionFollowTest: the ten action types driven exhaustively as
		#     pure decisions, chance weighting at its boundaries, the packed
		#     fire, the plan/arming rules and the allocation probe on the audio
		#     path this feature added;
		#   - SessionArrangementRecordTest: the ring (arming, bounded drop,
		#     non-destructive snapshot, disarm-keeps-the-performance) and the
		#     scheduler-driven behaviour that feeds it - a chain running
		#     HANDS-FREE across four clips, a stop, a refused action firing once
		#     per action time, and the reset that records the stops it ends.
		src/core/SessionFollowTest.cpp
		src/core/SessionArrangementRecordTest.cpp
		# R5.2: Follow Action chains on SCENES - whole rows, precedence over the clip,
		# the v1/v2 section rule and the socket half (set_slot's missing chain arg).
		src/core/SessionSceneFollowTest.cpp
		# 0.5.0 L02 (WF-055): audio recorded into a launcher cell - the take as a unit (round trip, span, length,
		# the allocation probe on its audio-thread entry points) and on the real engine through the agent surface
		# (a known input signal, the quantised start, the landed WAV's PCM, one undo step).
		src/core/SessionAudioRecorderTest.cpp
		src/core/SessionAudioRecordTest.cpp
		# ...and a launched audio cell sounding its clip through its sample track (the landed take included).
		src/core/SessionAudioCellPlaybackTest.cpp
	# What a <session> command ACCEPTS: a wrong-typed Follow Action field is
	# refused rather than converted, a non-array `actions` does not fall back to
	# the stored chain, a cell address needs both coordinates - and the session
	# model edit that marks the project unsaved.
	src/core/SessionCommandArgsTest.cpp
	# A MALFORMED session attribute must not become a valid target: a jump
	# target, a legacy scene index and a legacy clip coordinate that cannot be
	# converted are refused instead of silently addressing scene 0 / track 0,
	# with the well-formed documents beside them as the control.
	src/core/SessionMalformedAttributeTest.cpp
	# Cluster new-p9-414-port: the persisted ratio of a scale interval must not
	# wrap when it is narrowed into the interval's 32-bit members (a stored
	# 4294967298/1 used to load as 2/1). Its own file, not the scale family's,
	# so the ctest entry carries a three-digit number and the negative control
	# can find and rebuild it.
	src/core/ScaleIntervalOverflowTest.cpp
	)


if(LMMS_HAVE_ALSA)
	list(APPEND LMMS_TESTS src/core/AudioAlsaPollDescriptorTest.cpp)
	# cluster 4584: the ALSA capture thread must not log a failed recovery -
	# the failure is parked and reported by the thread that closes the capture.
	list(APPEND LMMS_TESTS src/core/AudioAlsaCaptureErrorTest.cpp)
endif()

# The OSS MIDI output guard needs the optional backend and its API.
if(LMMS_HAVE_OSS)
	list(APPEND LMMS_TESTS src/core/MidiOssTest.cpp)
endif()

# The sndio MIDI output guard needs the optional backend and its API.
if(LMMS_HAVE_SNDIO)
	list(APPEND LMMS_TESTS src/core/MidiSndioTest.cpp)
endif()

# The crash reporter's `offered` sentinel through the CONTROL COMMAND: a
# directory where the sentinel belongs must not pass for an acknowledgement
# (cluster new-p11-19-port). Its own file, and appended LAST, so the case is
# a ctest of its own: the negative control that reverts the product change
# and re-runs the branch's tests finds it by name and rebuilds its binary.
LIST(APPEND LMMS_TESTS src/core/ControlCrashAcknowledgeTest.cpp)

# Cluster new-p15-95: the LADSPA global stereo link over a plugin whose control total
# is 65536. The LADSPA plug-in's own sources are compiled into the test (the
# SlicerT plug-in tests' shape), so the link the test drives is the shipping one. Its
# own file, and appended LAST, so the ctest entry carries a three-digit number and the
# negative control can find and rebuild it.
LIST(APPEND LMMS_TESTS src/plugins/LadspaGlobalLinkTest.cpp)

# gcov coverage instrumentation (see WANT_COVERAGE in the top-level
# CMakeLists.txt). Applied to the unit-test executables only; the Part C
# reference/migration targets below keep the plain debug build so behaviour
# comparisons stay instrumentation-independent.
set(LMMS_COVERAGE_FLAGS "")
if(WANT_COVERAGE)
	set(LMMS_COVERAGE_FLAGS "--coverage")
endif()

add_library(SampleRateDependentLadspa MODULE
	src/plugins/SampleRateDependentLadspa.cpp)
target_include_directories(SampleRateDependentLadspa PRIVATE
	${CMAKE_SOURCE_DIR}/include)
target_compile_features(SampleRateDependentLadspa PRIVATE cxx_std_20)
set_target_properties(SampleRateDependentLadspa PROPERTIES PREFIX "")
if(MSVC)
	# ladspa.h's prototype has no dllexport, which MSVC refuses to see added on the definition (C2375).
	target_link_options(SampleRateDependentLadspa PRIVATE /EXPORT:ladspa_descriptor)
endif()

function(lmms_add_coverage TARGET)
	if(LMMS_COVERAGE_FLAGS)
		target_compile_options(${TARGET} PRIVATE ${LMMS_COVERAGE_FLAGS})
		target_link_options(${TARGET} PRIVATE ${LMMS_COVERAGE_FLAGS})
	endif()
endfunction()

# The allocation probe (tests/src/core/AllocationProbe.h). Its replacement global
# operator new/new[]/delete/delete[] must be defined exactly ONCE per executable and
# must not be `inline` ([replacement.functions]); an inline definition left the test
# binaries with no operator new of their own, so allocations inside lmmsobjs were
# never counted. The definitions live in AllocationProbe.cpp, compiled once here and
# linked into every test whose source includes the header (lmms_link_allocation_probe).
# The header's counters are defined in that file too, so a test that includes the
# header without the probe linked fails to link instead of counting nothing.
add_library(zene_allocation_probe OBJECT src/core/AllocationProbe.cpp)
set_target_properties(zene_allocation_probe PROPERTIES AUTOMOC OFF)
target_compile_features(zene_allocation_probe PRIVATE cxx_std_20)
lmms_add_coverage(zene_allocation_probe)
function(lmms_link_allocation_probe TARGET SOURCE)
	# Any spelling of the include: "AllocationProbe.h" or "../core/AllocationProbe.h".
	file(STRINGS "${SOURCE}" USES_PROBE REGEX "^[ \t]*#[ \t]*include[ \t]+\"([^\"]*/)?AllocationProbe\\.h\"")
	if(USES_PROBE)
		target_link_libraries(${TARGET} PRIVATE zene_allocation_probe)
	endif()
endfunction()

# A loadable but deliberately non-plug-in module used to exercise the cached-descriptor
# path where a module is found during scan and lacks lmms_plugin_main when instantiated.
add_library(q050018_no_entry MODULE src/plugins/MissingDevicesNoEntryModule.cpp)
set_target_properties(q050018_no_entry PROPERTIES OUTPUT_NAME "q050018-no-entry")

# VstEffectControlsTest compiles the VstEffect plug-in's sources against its target's include
# paths and definitions; where that plug-in is not built (macOS: no VST2 host) there is nothing to test.
if(NOT TARGET vsteffect)
	list(REMOVE_ITEM LMMS_TESTS src/plugins/VstEffectControlsTest.cpp)
endif()

# Targets that compile plugins/NeuralAmp/nam/*.cpp pull in vendored Eigen, whose NEON kernels trip
# -Wdeprecated-enum-enum-conversion on aarch64; -DUSE_WERROR must not make that an error (the plugin
# target does the same, plugins/NeuralAmp/CMakeLists.txt).
set(ZENE_NAM_EIGEN_OPTIONS "$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-Wno-error=deprecated-enum-enum-conversion>")

# cluster new-p14-314 (+ new-p16-230, new-p14-313): the SMF note reader's own regressions, one slot per
# cluster finding, in an executable of their own because ClipImportFileTest (the import path the reader
# serves) is at the 500-line file-length cap. Appended in this late block on purpose: ctest right-aligns
# the test number, so a test numbered below 100 prints as "Test   #4:" and the mechanical negative
# control (qwen/negctl.py) cannot read its name out of ctest -N (see the note under the last append).
list(APPEND LMMS_TESTS src/core/SmfNoteReadTest.cpp)

# cluster new-p12-278: the Vibed waveform handoff proof - a note's render must
# depend on the waveform the GUI thread handed off, never on the wave graph model
# being edited underneath the audio thread. It is registered LAST on purpose: the
# mechanical negative control (qwen/negctl.py) reads the ctest test NAMES out of
# `ctest -N`, and ctest right-aligns the test number, so a test numbered below 100
# is printed as "Test   #4:" and its name is invisible to that parser. A test the
# control cannot see is a test the control cannot judge.
list(APPEND LMMS_TESTS src/plugins/VibedWaveformHandoffTest.cpp)
# The BandedWG "Bowed" checkbox: the Mallets instrument view must expose it, bound to the strike setting.
# The test compiles the Mallets sources, so it is registered only where STK is found.
if(LMMS_HAVE_STK)
	list(APPEND LMMS_TESTS src/plugins/MalletsInstrumentViewTest.cpp)
endif()

# cluster new-p14-358 / 5451: the VeSTige VST host's MIDI path. "Turn off all notes"
# has to end the notes of every MIDI channel, and the note path must reach the plug-in
# through the bounded hand-off queue instead of the plug-in mutex. It is registered LAST
# for the same reason as the test above: the mechanical negative control reads the ctest
# test NAMES out of `ctest -N`, and only the high-numbered ones are visible to it.
list(APPEND LMMS_TESTS src/plugins/VestigeMidiHandoffTest.cpp)

# The VeSTige plug-in is a VST2 host: where that host is not built (macOS: no VST2
# host) there is nothing to test.
if(NOT TARGET vestige)
	list(REMOVE_ITEM LMMS_TESTS src/plugins/VestigeMidiHandoffTest.cpp)
endif()

# Cluster new-p15-300: the compressor's control dialog shared its visualizer fields with the
# audio thread. The regression reads the plugin's header, so the test only needs the header's
# include path - the plugin itself stays a module. Registered LAST, like the test above, so
# the mechanical negative control (qwen/negctl.py) can see its name in `ctest -N`.
list(APPEND LMMS_TESTS src/plugins/CompressorVisualizerTest.cpp)

foreach(LMMS_TEST_SRC IN LISTS LMMS_TESTS)
	# TODO CMake 3.20: Use cmake_path
	get_filename_component(LMMS_TEST_NAME ${LMMS_TEST_SRC} NAME_WE)

	add_executable(${LMMS_TEST_NAME} ${LMMS_TEST_SRC})
	if(LMMS_TEST_NAME STREQUAL "RnnoiseDenoiserEffectTest")
		target_sources(${LMMS_TEST_NAME} PRIVATE
			${CMAKE_SOURCE_DIR}/plugins/RnnoiseDenoiser/RnnoiseDenoiserEffect.cpp
			${CMAKE_SOURCE_DIR}/plugins/RnnoiseDenoiser/RnnoiseDenoiserControls.cpp
			${CMAKE_SOURCE_DIR}/plugins/RnnoiseDenoiser/RnnoiseDenoiserControlDialog.cpp
			${CMAKE_SOURCE_DIR}/plugins/RnnoiseDenoiser/RnnoiseDenoiserEffect.h
			${CMAKE_SOURCE_DIR}/plugins/RnnoiseDenoiser/RnnoiseDenoiserControls.h
			${CMAKE_SOURCE_DIR}/plugins/RnnoiseDenoiser/RnnoiseDenoiserControlDialog.h)
		target_include_directories(${LMMS_TEST_NAME} PRIVATE
			${CMAKE_SOURCE_DIR}/plugins/RnnoiseDenoiser
			${CMAKE_BINARY_DIR}/plugins/RnnoiseDenoiser)
		target_compile_definitions(${LMMS_TEST_NAME} PRIVATE PLUGIN_NAME=rnnoisedenoiser)
		target_link_libraries(${LMMS_TEST_NAME} PRIVATE rnnoise_vendored)
		add_dependencies(${LMMS_TEST_NAME} rnnoise_vendored)
		if(NOT MSVC)
			target_link_libraries(${LMMS_TEST_NAME} PRIVATE m)
		endif()
	endif()
	if(LMMS_TEST_NAME STREQUAL "MasteringTest")
		# The --report regression drives the real CLI, so it runs the application
		# binary of THIS build: its path is compiled in (the build tree's layout
		# is not the test's to guess) and the target is a dependency, so the child
		# is never a missing or a stale binary.
		target_compile_definitions(${LMMS_TEST_NAME} PRIVATE
			ZENE_APP_BINARY="$<TARGET_FILE:zene>")
		add_dependencies(${LMMS_TEST_NAME} zene)
	endif()
	if(LMMS_TEST_NAME STREQUAL "MainCliStartupTest")
		target_compile_definitions(${LMMS_TEST_NAME} PRIVATE
			ZENE_APP_BINARY="$<TARGET_FILE:zene>"
			ZENE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
		add_dependencies(${LMMS_TEST_NAME} zene)
	endif()
	if(LMMS_TEST_NAME STREQUAL "HydrogenImportTest")
		target_sources(${LMMS_TEST_NAME} PRIVATE
			${CMAKE_SOURCE_DIR}/plugins/HydrogenImport/LocalFileMng.cpp)
	endif()
	if(LMMS_TEST_NAME STREQUAL "HydrogenImportLocalFileMngTest")
		target_sources(${LMMS_TEST_NAME} PRIVATE
			${CMAKE_SOURCE_DIR}/plugins/HydrogenImport/LocalFileMng.cpp)
		target_include_directories(${LMMS_TEST_NAME} PRIVATE
			${CMAKE_SOURCE_DIR}/plugins/HydrogenImport)
	endif()
	if(LMMS_TEST_NAME STREQUAL "ReverbSCCreateTest")
		target_sources(${LMMS_TEST_NAME} PRIVATE src/plugins/ReverbSCCreateTestAllocator.c)
	endif()
	# cluster new-p12-278: VibedWaveformHandoffTest compiles the plug-in's own
	# sources into the test (the SlicerT plug-in tests' shape), so the audio path it
	# drives is the shipping one and the wave graph models are reachable.
	if(LMMS_TEST_NAME STREQUAL "VibedWaveformHandoffTest")
		target_sources(${LMMS_TEST_NAME} PRIVATE
			${CMAKE_SOURCE_DIR}/plugins/Vibed/Vibed.cpp
			${CMAKE_SOURCE_DIR}/plugins/Vibed/Vibed.h
			${CMAKE_SOURCE_DIR}/plugins/Vibed/NineButtonSelector.cpp
			${CMAKE_SOURCE_DIR}/plugins/Vibed/NineButtonSelector.h
			${CMAKE_SOURCE_DIR}/plugins/Vibed/VibratingString.cpp
			${CMAKE_SOURCE_DIR}/plugins/Vibed/VibratingString.h)
		target_include_directories(${LMMS_TEST_NAME} PRIVATE
			${CMAKE_SOURCE_DIR}/plugins/Vibed ${CMAKE_BINARY_DIR}/plugins/Vibed)
		target_compile_definitions(${LMMS_TEST_NAME} PRIVATE
			PLUGIN_NAME=vibedstrings vibedstrings_EXPORTS)
	endif()
	# cluster new-p14-358 / 5451: VestigeMidiHandoffTest compiles the VeSTige plug-in's
	# own sources into the test (the SlicerT and Vibed tests' shape), so the "Turn off all
	# notes" button and the note path under test are the shipping ones. The plug-in's
	# artwork is generated in its build directory; without CMP0118 the GENERATED mark is
	# per-directory, so a fresh configure of THIS directory would refuse a file that does
	# not exist yet. add_dependencies below orders its build.
	if(LMMS_TEST_NAME STREQUAL "VestigeMidiHandoffTest")
		set_source_files_properties("${CMAKE_BINARY_DIR}/plugins/Vestige/qrc_vestige.cpp"
			PROPERTIES GENERATED TRUE)
		target_sources(${LMMS_TEST_NAME} PRIVATE
			${CMAKE_SOURCE_DIR}/plugins/Vestige/Vestige.cpp
			${CMAKE_SOURCE_DIR}/plugins/Vestige/Vestige.h
			${CMAKE_BINARY_DIR}/plugins/Vestige/qrc_vestige.cpp)
		target_include_directories(${LMMS_TEST_NAME} PRIVATE
			${CMAKE_SOURCE_DIR}/plugins/Vestige
			${CMAKE_BINARY_DIR}/plugins/Vestige)
		target_compile_definitions(${LMMS_TEST_NAME} PRIVATE PLUGIN_NAME=vestige)
		target_link_libraries(${LMMS_TEST_NAME} PRIVATE vstbase)
		add_dependencies(${LMMS_TEST_NAME} vestige)
		# The vstbase module resolves the LMMS core symbols from its host process,
		# so the test binary has to export its symbols exactly like the zene
		# executable does.
		set_target_properties(${LMMS_TEST_NAME} PROPERTIES ENABLE_EXPORTS ON)
	endif()
	if(LMMS_TEST_NAME STREQUAL "CompressorVisualizerTest")
		# Cluster new-p15-300: the regression reads the compressor's header to check that the
		# visualizer state the control dialog and the audio thread share is published
		# (atomic) instead of shared. Only the header is needed - the plugin stays a module.
		target_include_directories(${LMMS_TEST_NAME} PRIVATE
			${CMAKE_SOURCE_DIR}/plugins/Compressor)
	endif()
	if(LMMS_TEST_NAME STREQUAL "ReverbSCBaseTest")
		target_sources(${LMMS_TEST_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/plugins/ReverbSC/base.c)
		target_include_directories(${LMMS_TEST_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/plugins/ReverbSC)
	endif()
	if(LMMS_TEST_NAME STREQUAL "EqCurveTest")
		target_sources(${LMMS_TEST_NAME} PRIVATE "${CMAKE_SOURCE_DIR}/plugins/Eq/EqCurve.cpp")
		target_include_directories(${LMMS_TEST_NAME} PRIVATE "${CMAKE_SOURCE_DIR}/plugins/Eq")
		target_compile_definitions(${LMMS_TEST_NAME} PRIVATE PLUGIN_NAME=Eq)
	endif()
	if(LMMS_TEST_NAME STREQUAL "LadspaPortDialogTest")
		target_sources(${LMMS_TEST_NAME} PRIVATE
			${CMAKE_SOURCE_DIR}/plugins/LadspaBrowser/LadspaPortDialog.cpp
			${CMAKE_SOURCE_DIR}/plugins/LadspaBrowser/LadspaPortDialog.h)
		target_include_directories(${LMMS_TEST_NAME} PRIVATE
			${CMAKE_SOURCE_DIR}/plugins/LadspaBrowser)
		target_compile_definitions(${LMMS_TEST_NAME} PRIVATE
			"LADSPA_PORT_DIALOG_FIXTURE_DIR=\"$<TARGET_FILE_DIR:SampleRateDependentLadspa>\""
			"LADSPA_PORT_DIALOG_FIXTURE_FILE=\"$<TARGET_FILE_NAME:SampleRateDependentLadspa>\"")
		add_dependencies(${LMMS_TEST_NAME} SampleRateDependentLadspa)
	endif()
	# cluster new-p15-95: LadspaGlobalLinkTest compiles the LADSPA plug-in's control
	# model into the test (the SlicerT plug-in tests' shape), so the global stereo link
	# the test drives is the shipping one. LMMS_TESTING is the define that compiles the
	# control model's test seam (the visitor the link reports the ports it walks to) in;
	# the shipping plug-in is built without it and keeps the plain path.
	if(LMMS_TEST_NAME STREQUAL "LadspaGlobalLinkTest")
		target_sources(${LMMS_TEST_NAME} PRIVATE
			${CMAKE_SOURCE_DIR}/plugins/LadspaEffect/LadspaControls.cpp
			${CMAKE_SOURCE_DIR}/plugins/LadspaEffect/LadspaControls.h
			${CMAKE_SOURCE_DIR}/plugins/LadspaEffect/LadspaMatrixControlDialog.cpp
			${CMAKE_SOURCE_DIR}/plugins/LadspaEffect/LadspaMatrixControlDialog.h
			${CMAKE_SOURCE_DIR}/plugins/LadspaEffect/LadspaWidgetFactory.cpp
			${CMAKE_SOURCE_DIR}/plugins/LadspaEffect/LadspaWidgetFactory.h)
		target_include_directories(${LMMS_TEST_NAME} PRIVATE
			${CMAKE_SOURCE_DIR}/plugins/LadspaEffect)
		target_compile_definitions(${LMMS_TEST_NAME} PRIVATE LMMS_TESTING PLUGIN_NAME=ladspaeffect)
	endif()
	add_test(NAME ${LMMS_TEST_NAME} COMMAND ${LMMS_TEST_NAME})
	lmms_link_allocation_probe(${LMMS_TEST_NAME} "${CMAKE_CURRENT_SOURCE_DIR}/${LMMS_TEST_SRC}")
	if(LMMS_TEST_NAME STREQUAL "EqCurveTest")
		set_tests_properties(${LMMS_TEST_NAME} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
	endif()
	if(LMMS_TEST_NAME STREQUAL "RnnoiseDenoiserEffectTest")
		set_tests_properties(${LMMS_TEST_NAME} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
	endif()
	if(LMMS_TEST_NAME STREQUAL "VstSubPluginFeaturesTest")
		target_sources(${LMMS_TEST_NAME} PRIVATE
			${CMAKE_SOURCE_DIR}/plugins/VstEffect/VstSubPluginFeatures.cpp)
		target_include_directories(${LMMS_TEST_NAME} PRIVATE
			${CMAKE_SOURCE_DIR}/plugins/VstEffect)
		set_tests_properties(${LMMS_TEST_NAME} PROPERTIES TIMEOUT 10)
	endif()
	if(LMMS_TEST_NAME STREQUAL "LadspaPortDialogTest")
		set_tests_properties(${LMMS_TEST_NAME} PROPERTIES
			ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
	endif()
	# BUGS_FOUND 11.23: on the hosted msvc-x64 runner the 300+ test executables' linker PDBs filled the
	# build drive (run 36802800273: LNK1201 writing SessionSceneFollowTest.pdb). Tests link without a
	# PDB under MSVC; zene.exe and the plugins keep theirs, so packages and crash symbols are unchanged.
	if(MSVC)
		target_link_options(${LMMS_TEST_NAME} PRIVATE /DEBUG:NONE)
	endif()
	# Every in-process command test holds each reply to its declared resultSchema too
	# (include/ControlResultCheck.h): they call the registry directly, so the socket
	# harness's setting never reached them. ENVIRONMENT_MODIFICATION leaves each test's
	# own ENVIRONMENT property alone.
	if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.22)
		set_tests_properties(${LMMS_TEST_NAME} PROPERTIES
			ENVIRONMENT_MODIFICATION "ZENE_CONTROL_CHECK_RESULTS=set:1")
	endif()

	# TODO CMake 3.12: Propagate usage requirements by linking to lmmsobjs
	target_include_directories(${LMMS_TEST_NAME} PRIVATE $<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>)
	if(LMMS_TEST_NAME STREQUAL "CarlaMidiHandoffTest")
		target_include_directories(${LMMS_TEST_NAME} PRIVATE
			"${CMAKE_SOURCE_DIR}/plugins/CarlaBase"
			"${CMAKE_SOURCE_DIR}/plugins/CarlaBase/carla/source/includes"
			"${CMAKE_SOURCE_DIR}/plugins/CarlaBase/CarlaConfig"
		)
	endif()
	if(LMMS_TEST_NAME STREQUAL "MidiHandoffQueueTest")
		# The structural test of pop() reads the header text.
		target_compile_definitions(${LMMS_TEST_NAME} PRIVATE
			MIDI_HANDOFF_QUEUE_HEADER="${CMAKE_SOURCE_DIR}/include/MidiHandoffQueue.h")
	endif()
	if(LMMS_TEST_NAME STREQUAL "MidiReconnectTest")
		target_include_directories(${LMMS_TEST_NAME} PRIVATE "${CMAKE_SOURCE_DIR}/src/core")
	endif()
	if(LMMS_TEST_NAME STREQUAL "Sf2PlayerRealtimeTest")
		# The product's own SF2 player module: the test loads it and drives the
		# instrument through the plugin's public interface, so the allocation
		# count covers the code the product ships. Without FluidSynth the module
		# does not exist, and the test says so instead of failing.
		if(TARGET sf2player)
			target_compile_definitions(${LMMS_TEST_NAME} PRIVATE
				SF2PLAYER_MODULE="$<TARGET_FILE:sf2player>")
			add_dependencies(${LMMS_TEST_NAME} sf2player)
			# The test inspects the synth the player owns (its tuning and the
			# settings it was built with), so it links FluidSynth itself.
			target_link_libraries(${LMMS_TEST_NAME} PRIVATE fluidsynth)
			target_compile_definitions(${LMMS_TEST_NAME} PRIVATE
				SF2_TEST_SOUNDFONT="${CMAKE_SOURCE_DIR}/plugins/LadspaEffect/calf/veal/sf2/Hum.sf2")
		else()
			target_compile_definitions(${LMMS_TEST_NAME} PRIVATE SF2PLAYER_MODULE="")
		endif()
		# The loaded module resolves the host's engine classes at load time, so
		# the test executable has to export them (the granular shifter test does
		# the same).
		set_target_properties(${LMMS_TEST_NAME} PROPERTIES ENABLE_EXPORTS ON)
		set_tests_properties(${LMMS_TEST_NAME} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
	endif()

	target_static_libraries("${LMMS_TEST_NAME}" PRIVATE lmmsobjs)
	if(LMMS_TEST_NAME STREQUAL "StereoDelay")
		target_sources(StereoDelay PRIVATE "${CMAKE_SOURCE_DIR}/plugins/Delay/StereoDelay.cpp")
		target_include_directories(StereoDelay PRIVATE "${CMAKE_SOURCE_DIR}/plugins/Delay")
	endif()
	target_link_libraries(${LMMS_TEST_NAME} PRIVATE
		${QT_LIBRARIES}
		${QT_QTTEST_LIBRARY}
	)
	# OversamplingHelpers.h coverage in this DSP test needs hiir's include path.
	if(LMMS_TEST_NAME STREQUAL "AudioResamplerRatioTest")
		target_link_libraries(${LMMS_TEST_NAME} PRIVATE hiir)
	endif()
	if(LMMS_TEST_NAME STREQUAL "VstEffectControlsTest")
		# vsteffect's resource source is generated in the plug-in's build directory; without
		# CMP0118 the GENERATED mark is per-directory, so a fresh configure of THIS directory
		# would refuse a file that does not exist yet. add_dependencies below orders its build.
		set_source_files_properties("${CMAKE_BINARY_DIR}/plugins/VstEffect/qrc_vsteffect.cpp"
			PROPERTIES GENERATED TRUE)
		target_sources(${LMMS_TEST_NAME} PRIVATE
			${CMAKE_SOURCE_DIR}/plugins/VstEffect/VstEffectControls.cpp
			${CMAKE_SOURCE_DIR}/plugins/VstEffect/VstEffectControlDialog.cpp
			${CMAKE_BINARY_DIR}/plugins/VstEffect/qrc_vsteffect.cpp)
		target_include_directories(${LMMS_TEST_NAME} PRIVATE
			$<TARGET_PROPERTY:vsteffect,INCLUDE_DIRECTORIES>)
		target_compile_definitions(${LMMS_TEST_NAME} PRIVATE
			$<TARGET_PROPERTY:vsteffect,COMPILE_DEFINITIONS>)
		target_link_libraries(${LMMS_TEST_NAME} PRIVATE vstbase)
		add_dependencies(${LMMS_TEST_NAME} vsteffect)
	endif()
	if(LMMS_TEST_NAME STREQUAL "DeviceDspTest")
		target_link_libraries(${LMMS_TEST_NAME} PRIVATE hiir)
		# Cluster 409: the test compiles the convolution kernel into its binary (with the FFTW
		# calls interposed), and the kernel's plan lifetime now lives in the scope that takes
		# the engine's model-change guard - so the scope's translation unit comes with it.
		target_sources(${LMMS_TEST_NAME} PRIVATE
			${CMAKE_SOURCE_DIR}/plugins/ConvolutionReverb/ConvolutionFftw.cpp
			${CMAKE_SOURCE_DIR}/plugins/ConvolutionReverb/ConvolutionFftw.h
		)
	endif()

	# ThreadPoolTest interposes pthread_create to inject a deterministic startup
	# failure after one worker has started; the symbol must be exported to bind.
	if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND LMMS_TEST_NAME STREQUAL "ThreadPoolTest")
		set_target_properties(${LMMS_TEST_NAME} PROPERTIES ENABLE_EXPORTS ON)
		target_link_libraries(${LMMS_TEST_NAME} PRIVATE ${CMAKE_DL_LIBS})
	endif()

	# Q050-167: AudioWorkerWakeTest warms the wake on the MAIN thread with no worker parked,
	# then interposes pthread_mutex_lock to prove the render-side post takes no mutex - the
	# same shape and the same symbol export as ThreadPoolTest above. It also reads the two
	# wake functions from the tree the binary was built from, for the platforms whose libSystem
	# calls an executable cannot interpose.
	if(LMMS_TEST_NAME STREQUAL "AudioWorkerWakeTest")
		target_compile_definitions(${LMMS_TEST_NAME} PRIVATE ZENE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
		if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
			set_target_properties(${LMMS_TEST_NAME} PROPERTIES ENABLE_EXPORTS ON)
			target_link_libraries(${LMMS_TEST_NAME} PRIVATE ${CMAKE_DL_LIBS})
		endif()
	endif()

	target_compile_features(${LMMS_TEST_NAME} PRIVATE cxx_std_20)
	if(LMMS_TEST_NAME STREQUAL "PatmanLoadTest")
		target_sources(${LMMS_TEST_NAME} PRIVATE
			${CMAKE_SOURCE_DIR}/plugins/Patman/Patman.cpp
			${CMAKE_SOURCE_DIR}/plugins/Patman/Patman.h
		)
		target_include_directories(${LMMS_TEST_NAME} PRIVATE
			${CMAKE_SOURCE_DIR}/plugins/Patman
			${CMAKE_BINARY_DIR}/plugins/Patman
		)
		target_compile_definitions(${LMMS_TEST_NAME} PRIVATE PLUGIN_NAME=patman)
		add_dependencies(${LMMS_TEST_NAME} patman)
		set_tests_properties(${LMMS_TEST_NAME} PROPERTIES
			ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
	endif()
	if(LMMS_TEST_NAME STREQUAL "MalletsInstrumentViewTest")
		target_sources(${LMMS_TEST_NAME} PRIVATE
			${CMAKE_SOURCE_DIR}/plugins/Stk/Mallets/Mallets.cpp
			${CMAKE_SOURCE_DIR}/plugins/Stk/Mallets/Mallets.h
		)
		target_include_directories(${LMMS_TEST_NAME} PRIVATE
			${CMAKE_SOURCE_DIR}/plugins/Stk/Mallets
			${CMAKE_BINARY_DIR}/plugins/Stk/Mallets
		)
		target_compile_definitions(${LMMS_TEST_NAME} PRIVATE
			PLUGIN_NAME=malletsstk
			MALLETS_TEST_STK_DIR="${STK_RAWWAVE_ROOT}")
		target_link_libraries(${LMMS_TEST_NAME} PRIVATE unofficial::libstk::libstk)
		set_tests_properties(${LMMS_TEST_NAME} PROPERTIES
			ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
	endif()
	target_compile_definitions(${LMMS_TEST_NAME} PRIVATE LMMS_TESTING)
	if(LMMS_TEST_NAME STREQUAL "ControlRenderOutputPathTest" OR LMMS_TEST_NAME STREQUAL "LinkAudioStreamTest")
		target_compile_definitions(${LMMS_TEST_NAME} PRIVATE ZENE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
	endif()
	if(LMMS_TEST_NAME STREQUAL "AutomationModesTest")
		target_compile_definitions(${LMMS_TEST_NAME} PRIVATE
			AUTOMATION_CACHE_SOURCE_FILE="${CMAKE_SOURCE_DIR}/src/core/Song.cpp")
	endif()
	lmms_add_coverage(${LMMS_TEST_NAME})
	# Lua scripting examples live in the source tree; tests run from build/tests.
	target_compile_definitions(${LMMS_TEST_NAME} PRIVATE
		LUA_SCRIPT_DIR="${CMAKE_SOURCE_DIR}/data/scripts")
	# Lua scripting API version, from the same top-level CMakeLists.txt variables
	# src/CMakeLists.txt passes to lmmsobjs. Deliberate duplication of the
	# definition, not of the value: ScriptStabilisationTest compares what the
	# engine reports (compiled in lmmsobjs) against what this test binary was
	# compiled with, so a version hardcoded in either translation unit fails.
	target_compile_definitions(${LMMS_TEST_NAME} PRIVATE
		ZENE_LUA_API_VERSION_MAJOR=${ZENE_LUA_API_VERSION_MAJOR}
		ZENE_LUA_API_VERSION_MINOR=${ZENE_LUA_API_VERSION_MINOR}
		ZENE_LUA_API_VERSION_PATCH=${ZENE_LUA_API_VERSION_PATCH})
	# Test fixtures (stub ONNX model) and the reference CLI path.
	target_compile_definitions(${LMMS_TEST_NAME} PRIVATE
		LMMS_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
	# Qt's QT_TESTCASE_SOURCEDIR is only supplied by Qt's own test build system (and is
	# nullptr on Qt 6 otherwise). Tests that read source-tree files (Lv2StateTest,
	# PluginPortsMigrationTest) need it; tests/ lets ../plugins and data/ resolve.
	target_compile_definitions(${LMMS_TEST_NAME} PRIVATE
		QT_TESTCASE_SOURCEDIR="${CMAKE_CURRENT_SOURCE_DIR}")
	# The A16 contract table's histogram is a SELF-CHECKING derivation (board card
	# #677): ReversibilityContractTest::theTableHistogramIsTheDocumentedOne() reads
	# the ONE published figure straight out of the release notes and fails when the
	# table and it disagree, so neither can go stale alone. The path is passed the
	# same way LMMS_TEST_DATA_DIR is; the test runs from build/tests, so it must be
	# absolute. No other test target gets it.
	if(LMMS_TEST_NAME STREQUAL "ReversibilityContractTest")
		target_compile_definitions(${LMMS_TEST_NAME} PRIVATE
			A16_HISTOGRAM_DOC="${CMAKE_SOURCE_DIR}/docs/RELEASE-NOTES-v0.3.0-alpha.md")
	endif()
	# The ARCH-4 namespace registry's compatibility table names five GUI-owned
	# sections, and those names are the GUI classes' own nodeName() literals. The
	# test extracts them from the headers so the table cannot drift from the tree
	# unnoticed; it runs from build/tests, so the path is passed absolutely and
	# only to this target.
	if(LMMS_TEST_NAME STREQUAL "NamespaceRegistryTest")
		target_compile_definitions(${LMMS_TEST_NAME} PRIVATE
			ZENE_ARCH4_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
	endif()
	if(LMMS_TEST_NAME STREQUAL "SlicerTWaveformTest")
		target_sources(${LMMS_TEST_NAME} PRIVATE
			${CMAKE_SOURCE_DIR}/plugins/SlicerT/SlicerT.cpp
			${CMAKE_SOURCE_DIR}/plugins/SlicerT/SlicerT.h
			${CMAKE_SOURCE_DIR}/plugins/SlicerT/SlicerTView.cpp
			${CMAKE_SOURCE_DIR}/plugins/SlicerT/SlicerTView.h
			${CMAKE_SOURCE_DIR}/plugins/SlicerT/SlicerTWaveform.cpp
			${CMAKE_SOURCE_DIR}/plugins/SlicerT/SlicerTWaveform.h)
		target_include_directories(${LMMS_TEST_NAME} PRIVATE
			${CMAKE_SOURCE_DIR}/plugins/SlicerT ${CMAKE_BINARY_DIR}/plugins/SlicerT ${FFTW3F_INCLUDE_DIRS})
		target_compile_definitions(${LMMS_TEST_NAME} PRIVATE PLUGIN_NAME=slicert slicert_EXPORTS)
		target_link_libraries(${LMMS_TEST_NAME} PRIVATE ${FFTW3F_LIBRARIES} SampleRate::samplerate)
	endif()
	# BitInvader is a plug-in module, so the test compiles the product's own note code in.
	# Its generated plugin_export.h (PLUGIN_EXPORT) sits beside the plug-in's build output.
	if(LMMS_TEST_NAME STREQUAL "BitInvaderAllocationTest")
		target_sources(${LMMS_TEST_NAME} PRIVATE
			${CMAKE_SOURCE_DIR}/plugins/BitInvader/BitInvader.cpp
			${CMAKE_SOURCE_DIR}/plugins/BitInvader/BitInvader.h)
		target_include_directories(${LMMS_TEST_NAME} PRIVATE
			${CMAKE_SOURCE_DIR}/plugins/BitInvader ${CMAKE_BINARY_DIR}/plugins/BitInvader)
		target_compile_definitions(${LMMS_TEST_NAME} PRIVATE PLUGIN_NAME=bitinvader)
	endif()
	# cluster new-p17-203: the Watsyn wavetable-snapshot regression compiles the plug-in's
	# own sources into the test (the SlicerT/Vibed/BitInvader shape), so the note path under
	# test is the shipping one and the staging tables a graph edit rewrites are reachable.
	if(LMMS_TEST_NAME STREQUAL "WatsynWaveSnapshotTest")
		target_sources(${LMMS_TEST_NAME} PRIVATE
			${CMAKE_SOURCE_DIR}/plugins/Watsyn/Watsyn.cpp
			${CMAKE_SOURCE_DIR}/plugins/Watsyn/Watsyn.h)
		target_include_directories(${LMMS_TEST_NAME} PRIVATE
			${CMAKE_SOURCE_DIR}/plugins/Watsyn ${CMAKE_BINARY_DIR}/plugins/Watsyn)
		target_compile_definitions(${LMMS_TEST_NAME} PRIVATE PLUGIN_NAME=watsyn watsyn_EXPORTS)
		target_link_libraries(${LMMS_TEST_NAME} PRIVATE SampleRate::samplerate)
	endif()
	IF(LMMS_HAVE_STEM_SPLIT)
		target_compile_definitions(${LMMS_TEST_NAME} PRIVATE
			LMMS_STEM_CLI_DEFAULT="${CMAKE_SOURCE_DIR}/tools/stem_split_cli.py")
	ENDIF()

	# ScriptEngineTest loads a real instrument plugin (tripleoscillator) from the
	# build tree to prove the Lua Instrument binding (spec G4). Plugin modules
	# resolve LMMS core symbols from the host process at load time, so the test
	# binary must export its symbols exactly like the zene executable does.
	# Tests that need the build tree's plugin modules (they resolve LMMS core
	# symbols from the host process at load time, exactly like the zene
	# executable) get ENABLE_EXPORTS and the plugin directory. ScriptEngineTest
	# does it for the Lua Instrument binding; ControlDeviceCatalogueTest does it
	# so PluginFactory discovers lv2effect/lv2instrument and the LV2 half of the
	# device catalogue is real rather than empty.  PluginScanCacheTest drives the
	# scan cache against a copy of the built tripleoscillator module;
	# ReversibilityContractTest/ReversibilityUndoTest load the real binary's
	# plugins to prove the A16 inverses (#623). ControlAutomationScriptTest needs
	# them so the automation-mode commands have a real parameter to drive: the
	# no-destruction proof (feature-list rows 10 and 63, board task #647) rides an
	# instrument parameter through plugin.param_set, and without the modules the
	# song's track holds a parameterless DummyInstrument.
	if(LMMS_TEST_NAME STREQUAL "ScriptEngineTest" OR LMMS_TEST_NAME STREQUAL "PluginScanCacheTest"
		OR LMMS_TEST_NAME STREQUAL "ControlDeviceCatalogueTest"
		OR LMMS_TEST_NAME STREQUAL "ReceiverEffectTest"
		OR LMMS_TEST_NAME STREQUAL "ScriptMemoryBudgetTest"
		OR LMMS_TEST_NAME STREQUAL "ReversibilityContractTest"
		OR LMMS_TEST_NAME STREQUAL "ReversibilityUndoTest"
		OR LMMS_TEST_NAME STREQUAL "ReversibilityUndoDeviceTest"
		OR LMMS_TEST_NAME STREQUAL "ControlAutomationScriptTest"
		OR LMMS_TEST_NAME STREQUAL "ControlAutomationModesTest"
		# The automation-mode context menu is driven against a real instrument's parameter.
		OR LMMS_TEST_NAME STREQUAL "AutomationModeMenuTest"
		# The automation editor's keys move a point of one real instrument's parameter.
		OR LMMS_TEST_NAME STREQUAL "AutomationEditorEditingTest"
		OR LMMS_TEST_NAME STREQUAL "ControlChainPresetTest"
		# The track menu's chain presets save and apply a chain holding one real effect.
		OR LMMS_TEST_NAME STREQUAL "ChainPresetMenuTest"
		# The modulators panel binds a parameter of one real effect.
		OR LMMS_TEST_NAME STREQUAL "ModulatorPanelTest"
		# The Device panel draws one real effect's card and the modulator driving it.
		OR LMMS_TEST_NAME STREQUAL "DevicePanelTest"
		# The rack's "Add effect" adds one real effect from the device picker.
		OR LMMS_TEST_NAME STREQUAL "EffectRackAddTest"
		# The canvas adds one real effect (amplifier) through the device picker's hook.
		OR LMMS_TEST_NAME STREQUAL "PatcherCanvasTest"
		# The compatibility probe loads whatever third-party plug-in it is given through the build's hosting modules.
		OR LMMS_TEST_NAME STREQUAL "PluginCompatEditorProbe"
		# The Gate / Modulation / Receiver dialogs are built over one real effect each.
		OR LMMS_TEST_NAME STREQUAL "PluginControlDialogsTest"
		# The device menu acts on real amplifier, recorder and convolution reverb effects.
		OR LMMS_TEST_NAME STREQUAL "EffectDeviceMenuTest"
		OR LMMS_TEST_NAME STREQUAL "EffectPickerOrganiserTest"
		OR LMMS_TEST_NAME STREQUAL "PluginNativeEditorTest"
		OR LMMS_TEST_NAME STREQUAL "ReferenceProjectPanelTest"
		# The LV2 state round trip loads the host's real LV2 effects.
		OR LMMS_TEST_NAME STREQUAL "Lv2StateTest"
		# The rack menu binds a macro to a parameter of one real effect.
		OR LMMS_TEST_NAME STREQUAL "RackMenuTest"
		# The effect Hosting submenu reads a real effect's oop.get_state entry.
		OR LMMS_TEST_NAME STREQUAL "EffectHostingMenuTest"
		# Board card #712: SessionTuningTest's proof slot needs tracks with a
		# REAL instrument (revtest::addInstrumentTrack -> plugin.load) so both
		# sounding notes go through the engine's own render path - the same
		# reason ReversibilityUndoTest is on this list.
		OR LMMS_TEST_NAME STREQUAL "SessionTuningTest"
		# Board card #597: SessionPlaybackRenderTest's proof is that a launched
		# session slot holding a pattern makes a silent arrangement SOUND, so it
		# needs a real instrument in the pattern store - the same requirement, for
		# the same reason, as SessionTuningTest above.
		OR LMMS_TEST_NAME STREQUAL "SessionPlaybackRenderTest"
		# The hosting test loads the build's own plugin modules to get devices with
		# real plugin keys (and, where they exist, the ZynAddSubFx module and its
		# client - see the OUTOFPROC_ZYN_* definitions below).
		OR LMMS_TEST_NAME STREQUAL "OutOfProcessHostTest"
		OR LMMS_TEST_NAME STREQUAL "OutOfProcessHostClientLoopTest"
		# 0.5.0 L05: slicing to a sampler track loads the build's SlicerT module.
		OR LMMS_TEST_NAME STREQUAL "AudioEditingTest"
		# 4468: a sounding slice note, through the SlicerT module built beside the test binary.
		OR LMMS_TEST_NAME STREQUAL "SlicerTPlayheadTest"
		# 0.5.0 L10: copying and comparing devices loads the build's effect modules.
		OR LMMS_TEST_NAME STREQUAL "DeviceCopyTest"
		OR LMMS_TEST_NAME STREQUAL "DeviceOrganiserTest"
		OR LMMS_TEST_NAME STREQUAL "DeviceAbUndoTest"
		# Luna 5332: the script's removeEffect undo test loads the build's BassBooster module.
		OR LMMS_TEST_NAME STREQUAL "ScriptDawBindingTest"
		OR LMMS_TEST_NAME STREQUAL "ScriptDawRemovalUndoTest"
		OR LMMS_TEST_NAME STREQUAL "ScriptDawModelRegistrationTest"
		OR LMMS_TEST_NAME STREQUAL "UndoHonestyTest"
		OR LMMS_TEST_NAME STREQUAL "UndoHonestyStateTest"
		OR LMMS_TEST_NAME STREQUAL "ControlChainPresetTest"
		OR LMMS_TEST_NAME STREQUAL "DeviceMovesUndoTest"
		# m0 blocker follow-up (trackview-uaf): the rack race cases load the build's amplifier module.
		OR LMMS_TEST_NAME STREQUAL "RackDeferredLifetimeTest"
		# 0.5.0 L10 (W3): the device tools load the build's amplifier module.
		OR LMMS_TEST_NAME STREQUAL "DeviceToolsTest"
		OR LMMS_TEST_NAME STREQUAL "DeviceToolsChainTest"
		OR LMMS_TEST_NAME STREQUAL "DeviceDspTest"
		OR LMMS_TEST_NAME STREQUAL "RealtimeSnapshotTest"
		# 0.5.0: the note-playback tests play a real instrument (tripleoscillator) on their tracks.
		OR LMMS_TEST_NAME STREQUAL "NotePlaybackTest"
		# cluster new-p11-38-port: the note-command transaction test loads a real instrument too.
		OR LMMS_TEST_NAME STREQUAL "NoteCommandTransactionsTest"
		# cluster new-p10-380-port-port: plays a real SID note through the build's sid module.
		OR LMMS_TEST_NAME STREQUAL "SidInstrument"
		# Cluster new-p12-315: the playhead regression loads the audiofileprocessor module to render a note.
		OR LMMS_TEST_NAME STREQUAL "AudioFileProcessorPlayheadTest"
		OR LMMS_TEST_NAME STREQUAL "SamplerEnvelopeTest"
		OR LMMS_TEST_NAME STREQUAL "SamplerVoiceReuseTest"
		OR LMMS_TEST_NAME STREQUAL "SfzPlayerTest"
		OR LMMS_TEST_NAME STREQUAL "SfzPlayerRealtimeTest"
		# Cluster new-p17-205: the Watsyn save/load regression loads the watsyn module.
		OR LMMS_TEST_NAME STREQUAL "WatsynSelectedGraphTest"
		OR LMMS_TEST_NAME STREQUAL "UnmuteChaseTest"
		OR LMMS_TEST_NAME STREQUAL "ClipTransposeTest"
		OR LMMS_TEST_NAME STREQUAL "NoteEditingCommandsTest"
		OR LMMS_TEST_NAME STREQUAL "NoteEditRangeTest"
		OR LMMS_TEST_NAME STREQUAL "AutomationLanesTest"
		OR LMMS_TEST_NAME STREQUAL "AutomationWriteToEndTest"
		# 0.5.0 L06 (Q050-022): the lane-child tests load a real instrument's parameter.
		OR LMMS_TEST_NAME STREQUAL "AutomationLaneChildrenTest"
		OR LMMS_TEST_NAME STREQUAL "AutomationLaneChildGuiTest"
		# 0.5.0 W3 engine: post-fader inserts and chain.receive load the build's amplifier and receiver modules.
		OR LMMS_TEST_NAME STREQUAL "W3EngineTest"
		OR LMMS_TEST_NAME STREQUAL "MidiExportTest"
		# 0.5.0 wave 2: layers and parts load a real instrument; the reference project copies a real effect.
		OR LMMS_TEST_NAME STREQUAL "InstrumentLayersCommandsTest"
		OR LMMS_TEST_NAME STREQUAL "InstrumentLayerZonePublishTest"
		# MX-157: the hosted instrument modules (clapinstrument, vst3instrument) carry the fixtures.
		OR LMMS_TEST_NAME STREQUAL "InstrumentLatencyPdcTest"
		OR LMMS_TEST_NAME STREQUAL "InstrumentSetupUiTest"
		OR LMMS_TEST_NAME STREQUAL "ArrangingCatchUpTest"
		OR LMMS_TEST_NAME STREQUAL "ArrangingSpacerReferenceTest"
		# WF-033: the restored history must bring back a real instrument's own state.
		OR LMMS_TEST_NAME STREQUAL "MissingDevicesPreserveTest"
		OR LMMS_TEST_NAME STREQUAL "MissingDevicesRound2Test"
		OR LMMS_TEST_NAME STREQUAL "UndoHistoryPersistenceTest"
		# Q050-090: the vendor proof reads plugin.list through the registry, so
		# the binary discovers the build tree's modules like its catalogue
		# sibling does (the built-in amplifier is the case that names Zene Studio).
		OR LMMS_TEST_NAME STREQUAL "ControlDeviceVendorTest"
		# WF-033: the options half of the undo-history proof needs the same real
		# instrument (its baseline loads one) as the format half.
		OR LMMS_TEST_NAME STREQUAL "UndoHistoryOptionsTest")
		set_target_properties(${LMMS_TEST_NAME} PROPERTIES ENABLE_EXPORTS ON)
		target_compile_definitions(${LMMS_TEST_NAME} PRIVATE
			LMMS_TEST_PLUGIN_DIR="${CMAKE_BINARY_DIR}/plugins")
	endif()

	# Q050-103: the envelope and voice-reuse test renders through the build's audiofileprocessor module.
	# Q050-484: the SFZ tests load the build's sfzplayer module (sfizz) and render through it.
	if((LMMS_TEST_NAME STREQUAL "SfzPlayerTest" OR LMMS_TEST_NAME STREQUAL "SfzPlayerRealtimeTest") AND TARGET sfzplayer)
		add_dependencies(${LMMS_TEST_NAME} sfzplayer)
	endif()

	if(LMMS_TEST_NAME STREQUAL "SamplerEnvelopeTest" OR LMMS_TEST_NAME STREQUAL "SamplerVoiceReuseTest")
		add_dependencies(${LMMS_TEST_NAME} audiofileprocessor)
	endif()

	# Q050-071: the probe loads third-party plug-ins through the build's hosting modules, so they have to be built first.
	if(LMMS_TEST_NAME STREQUAL "PluginCompatEditorProbe")
		foreach(HOST_MODULE vst3effect vst3instrument clapeffect clapinstrument lv2effect lv2instrument)
			if(TARGET ${HOST_MODULE})
				add_dependencies(${LMMS_TEST_NAME} ${HOST_MODULE})
			endif()
		endforeach()
	endif()

	# Q050-087: the canvas test adds the build's amplifier module through the picker.
	if(LMMS_TEST_NAME STREQUAL "PatcherCanvasTest")
		add_dependencies(${LMMS_TEST_NAME} amplifier)
	endif()

	# Q050-040: the unmute-chase tests play a real instrument (tripleoscillator) on their tracks,
	# so a targeted build has to bring that module up first.
	if(LMMS_TEST_NAME STREQUAL "UnmuteChaseTest")
		add_dependencies(${LMMS_TEST_NAME} tripleoscillator)
	endif()

	# Cluster new-p17-205: the Watsyn save/load regression needs the watsyn module built first.
	if(LMMS_TEST_NAME STREQUAL "WatsynSelectedGraphTest")
		add_dependencies(${LMMS_TEST_NAME} watsyn)
	endif()

	# cluster new-p11-38-port: the note-command transaction test puts a real instrument on its
	# track, so the module has to be built beside the test binary.
	if(LMMS_TEST_NAME STREQUAL "NoteCommandTransactionsTest")
		add_dependencies(${LMMS_TEST_NAME} tripleoscillator)
	endif()

	# The script's removeEffect undo cases load the build's own effect modules, and a
	# targeted build of the test alone did not build them: every case skipped ("this
	# build lists no loadable effect module") and the undo regression ran nowhere. The
	# modules are built with the test, so the cases run wherever the test is built.
	if(LMMS_TEST_NAME STREQUAL "ScriptDawRemovalUndoTest")
		add_dependencies(${LMMS_TEST_NAME} bassbooster)
	endif()
	if(LMMS_TEST_NAME STREQUAL "DeviceAbUndoTest")
		# The A/B undo cases need at least one effect module so they exercise the command instead of skipping.
		add_dependencies(${LMMS_TEST_NAME} amplifier)
	endif()
	if(LMMS_TEST_NAME STREQUAL "DeviceToolsChainTest" AND TARGET amplifier)
		# clip.process regressions load the build's amplifier module; build it with this targeted test.
		add_dependencies(${LMMS_TEST_NAME} amplifier)
	endif()
	if(LMMS_TEST_NAME STREQUAL "ScriptDawModelRegistrationTest")
		add_dependencies(${LMMS_TEST_NAME} bassbooster amplifier)
	endif()
	if(LMMS_TEST_NAME STREQUAL "ScriptDawBindingTest")
		add_dependencies(${LMMS_TEST_NAME} bassbooster amplifier)
	endif()

	# Q050-020: the MIDI export test loads the midiexport module through the plug-in factory.
	if(LMMS_TEST_NAME STREQUAL "MidiExportTest")
		add_dependencies(${LMMS_TEST_NAME} midiexport)
		set_tests_properties(${LMMS_TEST_NAME} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
	endif()

	# Q050-090: the vendor proof names the built-in amplifier's maker, so the
	# module it lists is built with the test (a targeted build without it would
	# list no amplifier and fail for a reason unrelated to the vendor).
	if(LMMS_TEST_NAME STREQUAL "ControlDeviceVendorTest" AND TARGET amplifier)
		add_dependencies(${LMMS_TEST_NAME} amplifier)
	endif()

	# Q050-030: NeuralAmpModelLoadTest compiles the effect and its engine in, with the worker-delay seam
	# that exists only in this executable.
	if(LMMS_TEST_NAME STREQUAL "NeuralAmpModelLoadTest")
		set(NAM_DIR "${CMAKE_SOURCE_DIR}/plugins/NeuralAmp")
		target_sources(${LMMS_TEST_NAME} PRIVATE
			${NAM_DIR}/NeuralAmpEffect.cpp ${NAM_DIR}/NeuralAmpControls.cpp ${NAM_DIR}/NeuralAmpControlDialog.cpp
			${NAM_DIR}/nam/NamModel.cpp ${NAM_DIR}/nam/NamModelLoader.cpp)
		# plugin_export.h is generated beside the plug-in module, so it has to exist first.
		add_dependencies(${LMMS_TEST_NAME} neuralamp)
		target_include_directories(${LMMS_TEST_NAME} PRIVATE ${NAM_DIR} "${CMAKE_BINARY_DIR}/plugins/NeuralAmp"
			${NAM_DIR}/rtneural/modules/Eigen ${NAM_DIR}/rtneural/modules/json)
		target_compile_options(${LMMS_TEST_NAME} PRIVATE ${ZENE_NAM_EIGEN_OPTIONS})
		target_compile_definitions(${LMMS_TEST_NAME} PRIVATE ZENE_NAM_TEST_SEAM PLUGIN_NAME=neuralamp)
		set_tests_properties(${LMMS_TEST_NAME} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
	endif()

	# W3EngineTest loads the build's amplifier (post-fader inserts) and receiver (chain.receive) modules.
	# Without these dependencies a targeted build (cmake --build build --target W3EngineTest) never builds
	# them, and both cases skip with "this build loads no ... module" instead of running.
	if(LMMS_TEST_NAME STREQUAL "MissingDevicesPreserveTest" OR LMMS_TEST_NAME STREQUAL "MissingDevicesRound2Test")
		# The test copies target-named modules into isolated plug-in directories; target names preserve
		# each platform's prefix and suffix (e.g. lib*.so, *.dll).
		add_dependencies(${LMMS_TEST_NAME} q050018_no_entry)
		target_compile_definitions(${LMMS_TEST_NAME} PRIVATE
			LMMS_TEST_NO_ENTRY_MODULE="$<TARGET_FILE:q050018_no_entry>")
		foreach(_q050018_module kicker amplifier vst3instrument clapinstrument lv2instrument vst3effect clapeffect)
			if(TARGET ${_q050018_module})
				add_dependencies(${LMMS_TEST_NAME} ${_q050018_module})
				string(TOUPPER "${_q050018_module}" _q050018_module_upper)
				target_compile_definitions(${LMMS_TEST_NAME} PRIVATE
					"LMMS_TEST_MODULE_${_q050018_module_upper}=\"$<TARGET_FILE_NAME:${_q050018_module}>\"")
			endif()
		endforeach()
	endif()
	if(LMMS_TEST_NAME STREQUAL "W3EngineTest")
		add_dependencies(${LMMS_TEST_NAME} amplifier receiver)
	endif()
	# ControlMidiReconnectSharedTest drives the inline helpers of a header private to src/core.
	if(LMMS_TEST_NAME STREQUAL "ControlMidiReconnectSharedTest")
		target_include_directories(${LMMS_TEST_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/src/core)
	endif()

	# PluginScanCacheTest drives the scan cache and the quarantine list through
	# the real PluginFactory, against a copy of the built tripleoscillator
	# module (the enumeration path, not a stub): the module has to exist before
	# ctest runs. It also constructs the plugin browser, so it needs a Qt
	# platform plugin even on a headless machine.
	if(LMMS_TEST_NAME STREQUAL "PluginScanCacheTest")
		target_compile_definitions(${LMMS_TEST_NAME} PRIVATE
			LMMS_TEST_SCAN_PLUGIN_DIR="${CMAKE_BINARY_DIR}/plugins")
		add_dependencies(${LMMS_TEST_NAME} tripleoscillator)
		if(TARGET lv2instrument)
			add_dependencies(${LMMS_TEST_NAME} lv2instrument)
		endif()
		set_tests_properties(${LMMS_TEST_NAME} PROPERTIES
			ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
	endif()
	# DevicePanelTest exercises a real instrument and effect. Build both modules with
	# the same Plugin ABI as the host; stale modules can overwrite the live instance's
	# library owner when Plugin::instantiate() returns it.
	if(LMMS_TEST_NAME STREQUAL "DevicePanelTest")
		add_dependencies(${LMMS_TEST_NAME} tripleoscillator amplifier)
	endif()
	# Q050-047: RackMenuTest's zones and empty-parallel-chain cases load a real effect module, so a targeted
	# build (cmake --build build --target RackMenuTest) must build amplifier too; otherwise plugin.list has no
	# loadable effect and both cases skip with "this host loads no effect module".
	if(LMMS_TEST_NAME STREQUAL "RackMenuTest")
		add_dependencies(${LMMS_TEST_NAME} amplifier)
	endif()
	# OutOfProcessHostTest drives the whole out-of-process loop on a REAL client
	# when this build has both halves of it: the ZynAddSubFx module (the only
	# family in this tree with an in-process AND a client implementation) and the
	# RemoteZynAddSubFx executable. Guarded on the targets, so a build without
	# them compiles the same test and its Zyn case reports ctest *Skipped* (the
	# module path defines are simply absent) rather than the suite failing to
	# build. No other test's definitions, environment or dependencies change.
	if(LMMS_TEST_NAME STREQUAL "OutOfProcessHostTest"
			OR LMMS_TEST_NAME STREQUAL "OutOfProcessHostClientLoopTest")
		if(TARGET zynaddsubfx AND TARGET RemoteZynAddSubFx)
			target_compile_definitions(${LMMS_TEST_NAME} PRIVATE
				OUTOFPROC_ZYN_PLUGIN_PATH="$<TARGET_FILE:zynaddsubfx>"
				OUTOFPROC_ZYN_CLIENT_PATH="$<TARGET_FILE:RemoteZynAddSubFx>")
			add_dependencies(${LMMS_TEST_NAME} zynaddsubfx RemoteZynAddSubFx)
		endif()
		set_tests_properties(${LMMS_TEST_NAME} PROPERTIES
			ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
			TIMEOUT 300)
	endif()

	# SafeStartTest proves the load-time predicate on the REAL load path: it
	# copies the built midiexport module into a fixture directory that this build
	# does not ship from (which is what makes the file third-party), pins the
	# plugin search path to it, and then asserts that Plugin::instantiate()
	# hands back the engine's DummyPlugin while safe-start mode is active - and
	# really loads the module once the session switch is off. The module resolves
	# LMMS core symbols from its host, so the test binary must export its symbols
	# exactly like the zene executable does (the same setup PluginScanCacheTest
	# and PhaseDSidechainTest need). It also needs a Qt platform plugin, even
	# though it is guiless.
	if(LMMS_TEST_NAME STREQUAL "SafeStartTest" OR LMMS_TEST_NAME STREQUAL "SafeStartLoadPathTest")
		set_target_properties(${LMMS_TEST_NAME} PROPERTIES ENABLE_EXPORTS ON)
		target_compile_definitions(${LMMS_TEST_NAME} PRIVATE
			LMMS_TEST_SAFE_START_PLUGIN_DIR="${CMAKE_BINARY_DIR}/plugins")
		add_dependencies(${LMMS_TEST_NAME} midiexport)
		set_tests_properties(${LMMS_TEST_NAME} PROPERTIES
			ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
	endif()
endforeach()

# Compile the FIFO header in its shared-memory mode independently of the host's
# selected socket transport, so the ownership and semaphore regressions run on
# every platform without changing RemotePluginBase's library class layout.
add_executable(RemotePluginShmFifoTest
	src/core/RemotePluginShmFifoTest.cpp
	${CMAKE_SOURCE_DIR}/src/common/RemotePluginBase.cpp
	${CMAKE_SOURCE_DIR}/src/common/SharedMemory.cpp
	${CMAKE_SOURCE_DIR}/src/common/SystemSemaphore.cpp)
target_include_directories(RemotePluginShmFifoTest PRIVATE
	$<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>)
target_link_libraries(RemotePluginShmFifoTest PRIVATE ${QT_LIBRARIES} ${QT_QTTEST_LIBRARY})
target_compile_features(RemotePluginShmFifoTest PRIVATE cxx_std_20)
target_compile_definitions(RemotePluginShmFifoTest PRIVATE LMMS_STATIC_DEFINE LMMS_TESTING SYNC_WITH_SHM_FIFO)
add_test(NAME RemotePluginShmFifoTest COMMAND RemotePluginShmFifoTest)

# Compile RemotePluginBase.cpp in the shared-memory mode too, so its blocking
# and nonblocking writer paths are checked even on builds that select sockets.
add_library(RemotePluginShmFifoCompileCheck OBJECT ${CMAKE_SOURCE_DIR}/src/common/RemotePluginBase.cpp)
target_include_directories(RemotePluginShmFifoCompileCheck PRIVATE
	$<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>)
target_compile_features(RemotePluginShmFifoCompileCheck PRIVATE cxx_std_20)
target_compile_definitions(RemotePluginShmFifoCompileCheck PRIVATE LMMS_TESTING SYNC_WITH_SHM_FIFO)
add_dependencies(RemotePluginShmFifoTest RemotePluginShmFifoCompileCheck)

# ImportFilter's exception-path proof uses a real, isolated importer plugin
# module so the exception crosses the same PluginFactory/ImportFilter path as
# a third-party importer. The test pins Qt's plugin search path to this module
# after PluginFactory construction; LMMS_PLUGIN_DIR is not used.
add_library(throwing_import_filter_test_plugin MODULE src/core/ThrowingImportFilterPlugin.cpp)
target_include_directories(throwing_import_filter_test_plugin PRIVATE
	$<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>
)
target_link_libraries(throwing_import_filter_test_plugin PRIVATE zene ${QT_LIBRARIES})
target_compile_features(throwing_import_filter_test_plugin PRIVATE cxx_std_20)
set_target_properties(throwing_import_filter_test_plugin PROPERTIES
	OUTPUT_NAME throwingimportfilter
	LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/throwing-import-filter-test-plugin"
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/throwing-import-filter-test-plugin"
)
set_target_properties(ImportFilterTest PROPERTIES ENABLE_EXPORTS ON)
target_compile_definitions(ImportFilterTest PRIVATE
	LMMS_TEST_THROWING_IMPORT_FILTER_PLUGIN_DIR="${CMAKE_CURRENT_BINARY_DIR}/throwing-import-filter-test-plugin")
add_dependencies(ImportFilterTest throwing_import_filter_test_plugin)
set_tests_properties(ImportFilterTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# A source-contract check for the spectrum view's first-paint decay state. The
# view needs GuiApplication to construct, so this test checks the initializer
# directly without bringing up the full DAW GUI.
add_executable(SaSpectrumViewTest src/core/SaSpectrumViewTest.cpp)
add_test(NAME SaSpectrumView COMMAND SaSpectrumViewTest)
target_link_libraries(SaSpectrumViewTest PRIVATE ${QT_LIBRARIES} ${QT_QTTEST_LIBRARY})
target_compile_features(SaSpectrumViewTest PRIVATE cxx_std_20)
target_compile_definitions(SaSpectrumViewTest PRIVATE
	SA_SPECTRUM_VIEW_HEADER="${CMAKE_SOURCE_DIR}/plugins/SpectrumAnalyzer/SaSpectrumView.h")

# TapTempoTest exercises the real tool plugin's sync slot. Compile the plugin
# sources into the test binary so the test can construct TapTempo directly.
target_sources(TapTempoTest PRIVATE
	${CMAKE_SOURCE_DIR}/plugins/TapTempo/TapTempo.cpp
	${CMAKE_SOURCE_DIR}/plugins/TapTempo/TapTempoView.cpp
)
target_include_directories(TapTempoTest PRIVATE
	${CMAKE_SOURCE_DIR}/plugins/TapTempo
	${CMAKE_BINARY_DIR}/plugins/TapTempo
)
target_compile_definitions(TapTempoTest PRIVATE PLUGIN_NAME=taptempo)
set_tests_properties(TapTempoTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# The NAM harness WAV reader is private to its CLI translation unit. Include
# that source with its main renamed so this test exercises the reader directly.
add_executable(NeuralAmpWavInputTest
	src/core/NeuralAmpWavInputTest.cpp
	 "${CMAKE_SOURCE_DIR}/plugins/NeuralAmp/nam/NamModel.cpp"
	 "${CMAKE_SOURCE_DIR}/plugins/NeuralAmp/nam/NamModelLoader.cpp"
)
target_include_directories(NeuralAmpWavInputTest PRIVATE
	 "${CMAKE_SOURCE_DIR}/plugins/NeuralAmp"
	 "${CMAKE_SOURCE_DIR}/plugins/NeuralAmp/rtneural/modules/Eigen"
	 "${CMAKE_SOURCE_DIR}/plugins/NeuralAmp/rtneural/modules/json"
)
target_compile_features(NeuralAmpWavInputTest PRIVATE cxx_std_20)
target_compile_options(NeuralAmpWavInputTest PRIVATE ${ZENE_NAM_EIGEN_OPTIONS})
target_link_libraries(NeuralAmpWavInputTest PRIVATE ${QT_LIBRARIES} ${QT_QTTEST_LIBRARY} ${CMAKE_DL_LIBS})
if(NOT MSVC)
	target_link_libraries(NeuralAmpWavInputTest PRIVATE m)
endif()
add_test(NAME NeuralAmpWavInputTest COMMAND NeuralAmpWavInputTest)

# The library-level RNNoise probe is a test artifact, so build the actual
# command-line harness and exercise its output-file failure path directly.
set(RNNOISE_HARNESS_LIBRARY rnnoise_vendored)
if(NOT TARGET rnnoise_vendored)
	# The test remains available when this build disables the RNNoise plug-in.
	add_library(rnnoise_harness_vendored STATIC
		"${CMAKE_SOURCE_DIR}/plugins/RnnoiseDenoiser/rnnoise/denoise.c"
		"${CMAKE_SOURCE_DIR}/plugins/RnnoiseDenoiser/rnnoise/rnn.c"
		"${CMAKE_SOURCE_DIR}/plugins/RnnoiseDenoiser/rnnoise/pitch.c"
		"${CMAKE_SOURCE_DIR}/plugins/RnnoiseDenoiser/rnnoise/kiss_fft.c"
		"${CMAKE_SOURCE_DIR}/plugins/RnnoiseDenoiser/rnnoise/celt_lpc.c"
		"${CMAKE_SOURCE_DIR}/plugins/RnnoiseDenoiser/rnnoise/nnet.c"
		"${CMAKE_SOURCE_DIR}/plugins/RnnoiseDenoiser/rnnoise/nnet_default.c"
		"${CMAKE_SOURCE_DIR}/plugins/RnnoiseDenoiser/rnnoise/parse_lpcnet_weights.c"
		"${CMAKE_SOURCE_DIR}/plugins/RnnoiseDenoiser/rnnoise/rnnoise_data.c"
		"${CMAKE_SOURCE_DIR}/plugins/RnnoiseDenoiser/rnnoise/rnnoise_tables.c")
	set_target_properties(rnnoise_harness_vendored PROPERTIES
		SYSTEM TRUE
		POSITION_INDEPENDENT_CODE ON)
	target_include_directories(rnnoise_harness_vendored SYSTEM PUBLIC
		"${CMAKE_SOURCE_DIR}/plugins/RnnoiseDenoiser/rnnoise")
	set(RNNOISE_HARNESS_LIBRARY rnnoise_harness_vendored)
endif()

add_executable(RnnoiseHarnessCli
	"${CMAKE_SOURCE_DIR}/plugins/RnnoiseDenoiser/testdata/rnn_harness.c")
target_link_libraries(RnnoiseHarnessCli PRIVATE ${RNNOISE_HARNESS_LIBRARY})
if(NOT MSVC)
	target_link_libraries(RnnoiseHarnessCli PRIVATE m)
endif()

add_executable(RnnoiseHarnessOutputTest src/plugins/RnnoiseHarnessOutputTest.cpp)
add_test(NAME RnnoiseHarnessOutputTest COMMAND RnnoiseHarnessOutputTest)
target_link_libraries(RnnoiseHarnessOutputTest PRIVATE ${QT_LIBRARIES} ${QT_QTTEST_LIBRARY})
target_compile_features(RnnoiseHarnessOutputTest PRIVATE cxx_std_20)
target_compile_definitions(RnnoiseHarnessOutputTest PRIVATE
	RNNOISE_HARNESS_PATH="$<TARGET_FILE:RnnoiseHarnessCli>")
add_dependencies(RnnoiseHarnessOutputTest RnnoiseHarnessCli)

# ReverbSC's DC blocker allocator contract. The test build replaces malloc
# ReverbSC's DC blocker allocator contract. The test build replaces malloc
# only for dcblock.c, so the allocation failure is deterministic.
add_executable(ReverbScAllocationTest
	src/plugins/ReverbScAllocationTest.cpp
	${CMAKE_SOURCE_DIR}/plugins/ReverbSC/dcblock.c
)
add_test(NAME ReverbScAllocationTest COMMAND ReverbScAllocationTest)
target_include_directories(ReverbScAllocationTest PRIVATE
	${CMAKE_SOURCE_DIR}/plugins/ReverbSC
)
target_link_libraries(ReverbScAllocationTest PRIVATE
	${QT_LIBRARIES}
	${QT_QTTEST_LIBRARY}
)
target_compile_features(ReverbScAllocationTest PRIVATE cxx_std_20)
target_compile_definitions(ReverbScAllocationTest PRIVATE
	$<$<COMPILE_LANGUAGE:C>:malloc=sp_dcblock_test_malloc>
)

# ReverbSC's reverb core allocator contract: sp_revsc_create's own state and
# the auxiliary delay-line buffer sp_revsc_init obtains through
# sp_auxdata_alloc. The test build replaces malloc for both product sources
# (base.c supplies sp_auxdata_alloc, revsc.c the caller), so each allocation
# can be made to fail on demand.
add_executable(ReverbSCInitTest
	src/plugins/ReverbSCInitTest.cpp
	${CMAKE_SOURCE_DIR}/plugins/ReverbSC/revsc.c
	${CMAKE_SOURCE_DIR}/plugins/ReverbSC/base.c
)
add_test(NAME ReverbSCInitTest COMMAND ReverbSCInitTest)
target_include_directories(ReverbSCInitTest PRIVATE
	${CMAKE_SOURCE_DIR}/plugins/ReverbSC
)
target_link_libraries(ReverbSCInitTest PRIVATE
	${QT_LIBRARIES}
	${QT_QTTEST_LIBRARY}
)
target_compile_features(ReverbSCInitTest PRIVATE cxx_std_20)
target_compile_definitions(ReverbSCInitTest PRIVATE
	$<$<COMPILE_LANGUAGE:C>:malloc=sp_revsc_test_malloc>
)

# ReverbSC's DSP state lifetime: the effect's own sources are compiled into
# this test, and the soundpipe C files are built with malloc/calloc/free
# redirected to a shim in the test, so an allocation failure can be injected
# into one allocation of a known size and every soundpipe free is counted.
add_executable(ReverbSCPluginStateTest
	src/plugins/ReverbSCPluginStateTest.cpp
	${CMAKE_SOURCE_DIR}/plugins/ReverbSC/ReverbSC.cpp
	${CMAKE_SOURCE_DIR}/plugins/ReverbSC/ReverbSC.h
	${CMAKE_SOURCE_DIR}/plugins/ReverbSC/ReverbSCControls.cpp
	${CMAKE_SOURCE_DIR}/plugins/ReverbSC/ReverbSCControls.h
	${CMAKE_SOURCE_DIR}/plugins/ReverbSC/ReverbSCControlDialog.cpp
	${CMAKE_SOURCE_DIR}/plugins/ReverbSC/ReverbSCControlDialog.h
	${CMAKE_SOURCE_DIR}/plugins/ReverbSC/base.c
	${CMAKE_SOURCE_DIR}/plugins/ReverbSC/revsc.c
	${CMAKE_SOURCE_DIR}/plugins/ReverbSC/dcblock.c
)
add_test(NAME ReverbSCPluginStateTest COMMAND ReverbSCPluginStateTest)
target_include_directories(ReverbSCPluginStateTest PRIVATE
	$<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>
	${CMAKE_SOURCE_DIR}/plugins/ReverbSC
	${CMAKE_BINARY_DIR}/plugins/ReverbSC
)
target_static_libraries(ReverbSCPluginStateTest PRIVATE lmmsobjs)
target_link_libraries(ReverbSCPluginStateTest PRIVATE
	${QT_LIBRARIES}
	${QT_QTTEST_LIBRARY}
)
target_compile_features(ReverbSCPluginStateTest PRIVATE cxx_std_20)
target_compile_definitions(ReverbSCPluginStateTest PRIVATE
	LMMS_TESTING
	PLUGIN_NAME=reverbsc
	$<$<COMPILE_LANGUAGE:C>:malloc=sp_test_malloc>
	$<$<COMPILE_LANGUAGE:C>:calloc=sp_test_calloc>
	$<$<COMPILE_LANGUAGE:C>:free=sp_test_free>
)
set_target_properties(ReverbSCPluginStateTest PROPERTIES ENABLE_EXPORTS ON)
set_tests_properties(ReverbSCPluginStateTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Compile the real AudioFileProcessor waveform view into its regression test.
# UBSan's float-cast-overflow check makes the old negative-double to f_cnt_t
# conversion fail deterministically on platforms where that undefined cast
# happens to round-trip to a signed long in an ordinary build.
target_sources(AudioFileProcessorWaveViewTest PRIVATE
	${CMAKE_SOURCE_DIR}/plugins/AudioFileProcessor/AudioFileProcessorWaveView.cpp
	${CMAKE_SOURCE_DIR}/plugins/AudioFileProcessor/AudioFileProcessorWaveView.h)
target_include_directories(AudioFileProcessorWaveViewTest PRIVATE
	${CMAKE_SOURCE_DIR}/plugins/AudioFileProcessor)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
	target_compile_options(AudioFileProcessorWaveViewTest PRIVATE
		-fsanitize=float-cast-overflow -fno-sanitize-recover=float-cast-overflow)
	target_link_options(AudioFileProcessorWaveViewTest PRIVATE -fsanitize=float-cast-overflow)
	set_tests_properties(AudioFileProcessorWaveViewTest PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen;UBSAN_OPTIONS=halt_on_error=1")
endif()

# The expression clock crosses the 32-bit frame boundary in one rendered
# sample, so the regression exercises the real ExprSynth implementation without
# simulating a day of playback.
# The test, the CMake target and the CTest name all carry the same name, so a
# harness that rebuilds "the test whose source changed" (the negative control
# reverts the product change and rebuilds the changed test targets) rebuilds
# this one too. A test name that differs from its source file would leave a
# stale binary behind and the regression would prove nothing.
add_executable(ExprSynthTest
	src/plugins/ExprSynthTest.cpp
)
add_test(NAME ExprSynthTest COMMAND ExprSynthTest)
target_include_directories(ExprSynthTest PRIVATE
	$<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>
	${CMAKE_SOURCE_DIR}/plugins/Xpressive
)
target_static_libraries(ExprSynthTest PRIVATE lmmsobjs)
target_link_libraries(ExprSynthTest PRIVATE
	${QT_LIBRARIES}
	${QT_QTTEST_LIBRARY}
	exprtk
)
target_compile_features(ExprSynthTest PRIVATE cxx_std_20)
target_compile_definitions(ExprSynthTest PRIVATE
	LMMS_TESTING
	exprtk_disable_sc_andor
	exprtk_disable_return_statement
	exprtk_disable_break_continue
	exprtk_disable_comments
	exprtk_disable_string_capabilities
	exprtk_disable_rtl_io_file
	exprtk_disable_rtl_vecops
)
if(MSVC)
	target_compile_options(ExprSynthTest PRIVATE /bigobj)
elseif(LMMS_BUILD_WIN32 OR LMMS_BUILD_CYGWIN)
	target_compile_options(ExprSynthTest PRIVATE -Wa,-mbig-obj)
else()
	target_compile_options(ExprSynthTest PRIVATE -fexceptions)
endif()
lmms_add_coverage(ExprSynthTest)
set_tests_properties(ExprSynthTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_library(LadspaPortCountFixture MODULE src/core/LadspaPortCountFixture.cpp)
target_include_directories(LadspaPortCountFixture PRIVATE "${CMAKE_SOURCE_DIR}/include")
target_compile_features(LadspaPortCountFixture PRIVATE cxx_std_20)
if(MSVC)
	# ladspa.h's prototype has no dllexport, which MSVC refuses to see added on the definition (C2375).
	target_link_options(LadspaPortCountFixture PRIVATE /EXPORT:ladspa_descriptor)
endif()
set_target_properties(LadspaPortCountFixture PROPERTIES
	LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/ladspa-fixtures"
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/ladspa-fixtures")
add_dependencies(LadspaManagerTest LadspaPortCountFixture)
# The bundled-directory cases copy the fixture module into a directory of their
# own, so they need its path at build time (the LADSPA_PATH above is the same
# directory at run time).
target_compile_definitions(LadspaManagerTest PRIVATE
	"LADSPA_MANAGER_FIXTURE_DIR=\"$<TARGET_FILE_DIR:LadspaPortCountFixture>\"")
set_tests_properties(LadspaManagerTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LADSPA_PATH=$<TARGET_FILE_DIR:LadspaPortCountFixture>"
	TIMEOUT 20)

# The NeuralAmp engine is mono at its output. Exercise the loader's rejection of
# a final array with more than one head channel using its standalone sources.
add_executable(NamModelMonoOutputTest
	"${CMAKE_SOURCE_DIR}/plugins/NeuralAmp/tests/NamModelMonoOutputTest.cpp"
	"${CMAKE_SOURCE_DIR}/plugins/NeuralAmp/nam/NamModel.cpp"
	"${CMAKE_SOURCE_DIR}/plugins/NeuralAmp/nam/NamModelLoader.cpp"
)
target_include_directories(NamModelMonoOutputTest PRIVATE
	"${CMAKE_SOURCE_DIR}/plugins/NeuralAmp"
	"${CMAKE_SOURCE_DIR}/plugins/NeuralAmp/rtneural/modules/Eigen"
	"${CMAKE_SOURCE_DIR}/plugins/NeuralAmp/rtneural/modules/json"
)
target_compile_features(NamModelMonoOutputTest PRIVATE cxx_std_20)
target_compile_options(NamModelMonoOutputTest PRIVATE $<$<CONFIG:Debug>:-O2> ${ZENE_NAM_EIGEN_OPTIONS})
target_link_libraries(NamModelMonoOutputTest PRIVATE ${CMAKE_DL_LIBS})
if(NOT MSVC)
	target_link_libraries(NamModelMonoOutputTest PRIVATE m)
endif()
add_test(NAME NamModelMonoOutputTest COMMAND NamModelMonoOutputTest)

# ---------------------------------------------------------------------------
# The subject of the MPE playback proof (0.3.0 feature-list row 8, task #649).
#
# tests/src/plugins/MpeTestConsumer.cpp is an in-tree MIT instrument that
# CONSUMES per-note MPE channel pressure and CC74 and turns what it consumed
# into its own output level, so MpePlaybackTest can measure one audio block
# with the expression against the same block without it. Nothing else in the
# tree can be that subject: the built-in synthesisers never see MIDI at all
# (Instrument::handleMidiEvent is the base no-op; they are driven by a note's
# frequency and volume), and every instrument that WOULD consume the two axes
# (Vestige, Lv2Instrument, ClapEffect, CarlaBase, a hardware MIDI port) is a
# hosted binary that is not installed here. Without a subject the only honest
# claim would be "the events left the engine" - the drivable-but-inert shape
# this project has already paid for twice.
#
# A MODULE in its own output directory, never installed, not in the product's
# plug-in set: the test points PluginFactory at that directory through
# LMMS_PLUGIN_DIR, and nothing else scans it.
# ---------------------------------------------------------------------------
add_library(mpe_test_consumer MODULE src/plugins/MpeTestConsumer.cpp)
target_include_directories(mpe_test_consumer PRIVATE
	$<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>
)
target_compile_definitions(mpe_test_consumer PRIVATE PLUGIN_NAME=mpe_test_consumer)
target_compile_features(mpe_test_consumer PRIVATE cxx_std_20)
# The fixture subclasses lmms::Instrument, so its object code references LMMS
# symbols (Plugin's moc, Model's vtable, JournallingObject's dtor ...). Unix
# shared modules tolerate unresolved symbols; macOS, mingw and MSVC do not, so
# it links the same zene target every real plugin links
# (cmake/modules/BuildPlugin.cmake) - the same fix synthetic_audio_plugin and
# the partc_ref_* modules carry.
target_link_libraries(mpe_test_consumer PRIVATE zene ${QT_LIBRARIES})
set_target_properties(mpe_test_consumer PROPERTIES
	LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/mpe-test-consumer"
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/mpe-test-consumer"
)

# The in-tree modules these tests load are built with them, so a targeted build of the test never runs without its
# module (and a module that fails to load fails the test instead of skipping it).
if(TARGET receiver AND TARGET insertrecorder)
	add_dependencies(ReceiverEffectTest receiver insertrecorder)
endif()
if(TARGET sid AND TARGET SidInstrument)
	add_dependencies(SidInstrument sid)
endif()
if(TARGET tripleoscillator)
	add_dependencies(SessionPlaybackRenderTest tripleoscillator)
	add_dependencies(SessionTuningTest tripleoscillator)
	# Q050-022: the lane-child tests drive a real instrument's parameter.
	add_dependencies(AutomationLaneChildrenTest tripleoscillator)
	add_dependencies(AutomationLaneChildGuiTest tripleoscillator)
endif()

# These tests exercise the LV2 host through the plugin factory; build the
# modules they discover so a targeted build cannot silently skip that path.
if(LMMS_HAVE_LV2)
	if(TARGET ControlDeviceCatalogueTest AND TARGET lv2effect AND TARGET lv2instrument)
		add_dependencies(ControlDeviceCatalogueTest lv2effect lv2instrument)
	endif()
	if(TARGET Lv2StateTest AND TARGET lv2effect)
		add_dependencies(Lv2StateTest lv2effect)
	endif()
	if(TARGET ControlDeviceCatalogueTest AND TARGET lv2instrument)
		# Run only the LV2 key/catalogue slots for the focused red/green regression.
		add_test(NAME Lv2InstrumentKeyRegressionTest
			COMMAND $<TARGET_FILE:ControlDeviceCatalogueTest>
				builtinCatalogueDoesNotListSubPluginHosts
				lv2InstrumentRefusesMissingOrInvalidKeys)
		set_tests_properties(Lv2InstrumentKeyRegressionTest PROPERTIES
			ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
	endif()
endif()

# ---------------------------------------------------------------------------
# tests/src/plugins/BusesTestInstrument.cpp: an in-tree instrument that owns an InstrumentOutputBuses, so the
# plugin.output_* commands are measured on a real model in a build with no CLAP, VST3 or LV2 multi-output
# fixture (the only instruments that otherwise have extra buses). A MODULE in its own directory, never installed;
# ControlPluginBusesTest points LMMS_PLUGIN_DIR at it.
# ---------------------------------------------------------------------------
add_library(buses_test_instrument MODULE src/plugins/BusesTestInstrument.cpp)
target_include_directories(buses_test_instrument PRIVATE
	$<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>
)
target_compile_definitions(buses_test_instrument PRIVATE PLUGIN_NAME=buses_test_instrument)
target_compile_features(buses_test_instrument PRIVATE cxx_std_20)
target_link_libraries(buses_test_instrument PRIVATE zene ${QT_LIBRARIES})
set_target_properties(buses_test_instrument PROPERTIES
	LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/buses-test-instrument"
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/buses-test-instrument"
)
target_compile_definitions(ControlPluginBusesTest PRIVATE
	LMMS_BUSES_TEST_INSTRUMENT_DIR="${CMAKE_CURRENT_BINARY_DIR}/buses-test-instrument")
add_dependencies(ControlPluginBusesTest buses_test_instrument)
set_target_properties(ControlPluginBusesTest PROPERTIES ENABLE_EXPORTS ON)

# ---------------------------------------------------------------------------
# tests/src/plugins/FakeHostModule.cpp: stand-ins for the CLAP and VST3 host modules (clapeffect, vst3effect,
# vst3instrument - not clapinstrument, on purpose), so the hosted halves of the device catalogue run in a build with
# neither SDK. Targets are named fake_host_* to leave the real vst3effect/vst3instrument targets alone; the files they
# make carry the real module names, in a directory of their own that only ControlHostedDeviceCatalogueTest scans.
# ---------------------------------------------------------------------------
foreach(FAKE_HOST IN ITEMS "clapeffect|clap|Effect" "vst3effect|vst3|Effect" "vst3instrument|vst3|Instrument")
	string(REPLACE "|" ";" FAKE_HOST_PARTS "${FAKE_HOST}")
	list(GET FAKE_HOST_PARTS 0 FAKE_HOST_NAME)
	list(GET FAKE_HOST_PARTS 1 FAKE_HOST_FORMAT)
	list(GET FAKE_HOST_PARTS 2 FAKE_HOST_TYPE)
	add_library(fake_host_${FAKE_HOST_NAME} MODULE src/plugins/FakeHostModule.cpp)
	target_include_directories(fake_host_${FAKE_HOST_NAME} PRIVATE $<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>)
	target_compile_definitions(fake_host_${FAKE_HOST_NAME} PRIVATE
		PLUGIN_NAME=${FAKE_HOST_NAME} FAKE_HOST_FORMAT="${FAKE_HOST_FORMAT}" FAKE_HOST_TYPE=${FAKE_HOST_TYPE})
	target_compile_features(fake_host_${FAKE_HOST_NAME} PRIVATE cxx_std_20)
	target_link_libraries(fake_host_${FAKE_HOST_NAME} PRIVATE zene ${QT_LIBRARIES})
	set_target_properties(fake_host_${FAKE_HOST_NAME} PROPERTIES
		OUTPUT_NAME ${FAKE_HOST_NAME}
		LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/fake-hosts"
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/fake-hosts")
	add_dependencies(ControlHostedDeviceCatalogueTest fake_host_${FAKE_HOST_NAME})
endforeach()
target_compile_definitions(ControlHostedDeviceCatalogueTest PRIVATE
	LMMS_FAKE_HOSTS_DIR="${CMAKE_CURRENT_BINARY_DIR}/fake-hosts")
set_target_properties(ControlHostedDeviceCatalogueTest PROPERTIES ENABLE_EXPORTS ON)

# The proof's own wiring: the fixture directory it points LMMS_PLUGIN_DIR at,
# the module built before ctest runs, and the exports a plug-in module
# resolves its LMMS symbols from at load time (the zene executable and the
# test binaries share that requirement - src/CMakeLists.txt sets the same
# property on zene).
target_compile_definitions(MpePlaybackTest PRIVATE
	LMMS_MPE_CONSUMER_DIR="${CMAKE_CURRENT_BINARY_DIR}/mpe-test-consumer")
add_dependencies(MpePlaybackTest mpe_test_consumer)
set_target_properties(MpePlaybackTest PROPERTIES ENABLE_EXPORTS ON)
# Q050-015: the same module also logs the events an articulation sends ahead of its note.
target_compile_definitions(ArticulationPlaybackTest PRIVATE
	LMMS_MPE_CONSUMER_DIR="${CMAKE_CURRENT_BINARY_DIR}/mpe-test-consumer")
add_dependencies(ArticulationPlaybackTest mpe_test_consumer)
set_target_properties(ArticulationPlaybackTest PROPERTIES ENABLE_EXPORTS ON)


# RecordingRealtimeTest constructs a real Engine to measure the capture path
# (D9a/D9b), which needs a Qt platform plugin even though the test is guiless.
set_tests_properties(RecordingRealtimeTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(AutomationPrepassProofTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_compile_definitions(AutomationPrepassProofTest PRIVATE LMMS_AUTOMATION_PREPASS_DESTRUCTOR_TEST)
# ScriptDawBindingTest constructs a real Engine (mixer, script worker thread,
# ProjectJournal) and drives it through the Lua binding, so it needs the same
# headless Qt platform. ScriptDawRemovalUndoTest is the same kind of test.
set_tests_properties(ScriptDawBindingTest ScriptDawRemovalUndoTest ScriptDawModelRegistrationTest ScriptDawEffectsTest
	ScriptDawQueueFullTest ScriptDawPairReservationTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# The rack command-group tests (rack.* macros and key/velocity zones) construct a
# full Engine and drive the mixer's own save/load, so they need the same
# headless Qt platform as the test above - and the developer box has no display.
set_tests_properties(RackMacrosTest RackZonesTest RackZoneRoutingTest InstrumentRoutingTest InstrumentLayersCommandsTest
	InstrumentLayerZonePublishTest InstrumentLatencyPdcTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# The #602 modulation layer's two tests build a full Engine, a real Mixer and a
# rack chain, and drive the layer's own save/load, so they need the headless Qt
# platform for the same reason. ModulationLayerProjectRoundTripTest joins them:
# it drives project.save / project.open through the registry, which is the same
# dependency.
set_tests_properties(ModulationLayerValueTest ModulationLayerTest ModulationAudioViewTest
	ControlModulatorCommandsTest ControlNoteExpressionCommandsTest
	ModulationLayerProjectRoundTripTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
# SampleAccurateAutomationTest (feature row 9) builds a full Engine and the rack
# fixture's real Mixer channel, renders real audio periods through
# Song::processNextBuffer() and drives the automation.* group through the
# registry, so it needs the same headless Qt platform.
set_tests_properties(SampleAccurateAutomationTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
# AutomationPreFirstValueTest (Q050-037) builds a full Engine, drives the
# render periods itself and saves/reloads a project, so it needs the headless
# Qt platform for the same reason.
set_tests_properties(AutomationPreFirstValueTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# UndoBoundsTest constructs a full Engine, drives the control surface and starts
# a QThread-paced window test, so it needs the same headless Qt platform.
set_tests_properties(UndoBoundsTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
	TIMEOUT 300)
# UndoHistoryPersistenceTest (WF-033) builds a full Engine with a real
# instrument and drives project.save / project.open / control.undo.
set_tests_properties(UndoHistoryPersistenceTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
	TIMEOUT 300)
# UndoHistoryOptionsTest (WF-033) builds the same baseline (a real instrument)
# and drives project.save / project.open / control.undo.
set_tests_properties(UndoHistoryOptionsTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
	TIMEOUT 300)
# SessionCheckpointTest (Q050-044) builds a full Engine and drives script.run,
# project.open and control.undo / control.redo.
set_tests_properties(SessionCheckpointTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
	TIMEOUT 300)
set_tests_properties(SessionCheckpointBudgetTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
	TIMEOUT 300)
# TakeLaneCompTest (comping: the composite and the comp.* command surface, task
# #600) and TakeLaneTest (the lane list, the clip's lane tag and the
# non-destructive property of the take audio) construct real SampleTracks with
# real SampleClips over real WAVs, and TakeLaneCompTest drives the comp.* group
# through the registry, so both need the same headless Qt platform as the tests
# above - the developer box has no display.
set_tests_properties(TakeLaneCompTest TakeLaneTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# LanesUpconvertTest (ARCH-4 S7) saves and loads real project files through
# Song directly - same headless shape as the two tests above, no window.
set_tests_properties(LanesUpconvertTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# ScenesUpconvertTest (ARCH-4 S8) saves and loads real project files through
# Song directly - same headless shape as LanesUpconvertTest, no window.
set_tests_properties(ScenesUpconvertTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# ---------------------------------------------------------------------------
# PhaseDSidechainTest loads the native Compressor module.
#
# It points LMMS_PLUGIN_DIR at a private directory holding a link to the built
# Compressor, to prove sidechain ducking through the real plugin. The path comes
# in as PART_D_COMPRESSOR_LIBRARY; until now nothing defined that macro, so the
# file could not compile and was never registered - a test that looked like
# Phase D coverage and could not run in any way. $<TARGET_FILE:> resolves to the
# built module and makes the plugin a build dependency.
# ---------------------------------------------------------------------------
add_dependencies(PhaseDSidechainTest compressor)
target_compile_definitions(PhaseDSidechainTest PRIVATE
	PART_D_COMPRESSOR_LIBRARY="$<TARGET_FILE:compressor>")
# The Compressor module is dlopen()ed, so the test binary must export the
# lmms:: core symbols the module resolves from its host (same reason
# ScriptEngineTest and PluginScanCacheTest set this). Without it the load fails
# with "undefined symbol: _ZTIN4lmms16AutomatableModelE".
set_target_properties(PhaseDSidechainTest PROPERTIES ENABLE_EXPORTS ON)

# ---------------------------------------------------------------------------
# RoutingGraph on the mixer channel's effect chain (docs/ROUTING-GRAPH-LIVE.md).
#
# RoutingGraphLiveTest renders a channel whose chain is processed through a
# RoutingGraph and compares it byte-for-byte against a reference render captured
# before the graph was put on the path, then re-directs a connection and shows
# the render changes. The reference lives in tests/reference/.
# ---------------------------------------------------------------------------
target_compile_definitions(RoutingGraphLiveTest PRIVATE
	ROUTING_GRAPH_LIVE_REFERENCE_FILE="${CMAKE_CURRENT_SOURCE_DIR}/reference/routing-graph-live-render.raw"
)

# VcaGroupTest (#622) constructs a full Engine and drives Mixer::masterMix()
# synchronously, the same shape as the Phase D harnesses.
set_tests_properties(VcaGroupTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(VcaRefreshAtomicityTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
# PatcherCommandsTest constructs a full Engine, appends effects to a real mixer
# channel's chain and drives Mixer::masterMix() to measure that a re-wire changes
# the render, so it needs a Qt platform plugin for the same reason (guiless
# otherwise: nothing opens a window).
set_tests_properties(PatcherCommandsTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# ControlVcaCommandsTest drives the `vca.*` command group through the registry
# and needs an Engine (the mix and the song container); guiless, so it needs a
# Qt platform plugin for the same reason the two above do. So does its sibling,
# ControlVcaEditGroupsTest - the same group's edit half, split out for the
# 500-line file ratchet.
set_tests_properties(ControlVcaCommandsTest ControlVcaEditGroupsTest ControlVcaNestTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
# ClipLinkTest, ClipLinkPersistenceTest and ImportDetectionTest are the other way
# round, and cost a CI cycle to learn: the TEST may be guiless, but the BINARY is
# not. Each ends in QTEST_MAIN, and QTEST_MAIN expands to
# `QTEST_MAIN_SETUP()` -> `QApplication app(argc, argv)` whenever QT_WIDGETS_LIB is
# defined (qtest.h) - which it is for every test target here, because they all link
# `${QT_LIBRARIES}` (Qt::Widgets) and Qt defines that macro on the target's usage
# requirements. A QApplication constructor with no display is a qFatal:
#
#   qt.qpa.xcb: could not connect to display
#   qt.qpa.plugin: Could not load the Qt platform plugin "xcb" in "" even though it was found.
#   This application failed to start because no Qt platform plugin could be initialized.
#
# measured on linux-x86_64 run 35126160372 (job 104895806780): ClipLinkTest (#16),
# ClipLinkPersistenceTest (#17) and ImportDetectionTest (#57) each died as "Subprocess
# aborted" 0.2 s in, before their first line of test output - and the SAME job's own
# signal-backtrace step, which runs those binaries with QT_QPA_PLATFORM=offscreen,
# passed all of them (8/8, 4/4 and the ImportDetection cases). Reproduced on the
# developer box the moment DISPLAY is taken away:
#   env -u DISPLAY ./build/tests/ClipLinkTest   ->  EXIT=134, that message
#   env -u DISPLAY (build/tests) ctest -R "ClipLinkTest|ClipLinkPersistenceTest|ImportDetectionTest"
#                                               ->  0% tests passed, 3 tests failed out of 3,
#                                                   (Subprocess aborted) for #16, #17 and #57
# Every sibling that constructs an Engine or a control surface already registers this
# same property for the same reason. ImportDetectionTest does not need the platform
# for ITS OWN work (no Engine, no window, no widget - its cases read WAVs it writes
# itself), but the QApplication its QTEST_MAIN builds does, and QT_QPA_PLATFORM is
# also what lmms::isUnattendedRun() (src/core/UnattendedRun.cpp) reads to answer "can
# a human click here?" - so registering the platform is what makes a runner run the
# test in the headless state the product is told about, not merely what keeps it from
# aborting. Registration and comment only: no test's behaviour changes.
set_tests_properties(ClipLinkTest ClipLinkPersistenceTest LinkedClipEditsTest LinkedClipGroupEditsTest ClipOffsetFadeTest ClipOffsetFadePairTest ImportDetectionTest
	PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# The shell tests build widgets, so they need a Qt platform plugin. Their own
# main() defaults QT_QPA_PLATFORM to offscreen when no display is attached (the
# same guard MidiLearnGuiTest uses); registering it here is what makes a runner
# run them in the headless state the product is told about, not what keeps them
# from aborting.
set_tests_properties(ShellTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(DesignTokensTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(NoteEditorTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(LaneReadoutTest SampleEditorDialogTest CrossfadeEditorTest MixerModesTest MixerMeteringTest WorkflowFeedbackTest
	ShellHomesTest TaskCenterTest TaskIndicatorTest CPULoadWidgetTest RecordRemainingTest CanvasKeyboardTest ItemListPanelTest ItemListButtonsTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(MixerSelectionIdentityTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(DevicePanelTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(EffectRackAddTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(PatcherCanvasTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(PluginCompatEditorProbe PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(Lv2ViewBaseLifetimeTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(LibraryMenusTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(CommandPaletteTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(CommandFormTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(VcaStripViewTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(SessionGridViewTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(SessionGridRecordTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(SessionTransportBarTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(PerformViewTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(SessionSubSceneGridTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(TouchWorkspaceTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(TakeLanesPanelTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(InstrumentSetupUiTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(ShortcutsPageTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(ShortcutsCommandsTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(StartHubTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(HelpAccessTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(AgentSurfaceReportTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(OnlineHelpNoticeTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
if(ZENE_HAVE_COLLAB)
	set_tests_properties(CollabPanelTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
set_tests_properties(WorkspaceAccessTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(PianoRollBenchmarkTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(PianoRollRecordTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(StepRecorderWidgetTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
# AccessibilityHelperTest constructs the shell, so it takes the same
# platform registration (040/ui-precond). WcagContrastTest is guiless and needs
# none: QColor loads no platform plugin.
set_tests_properties(AccessibilityHelperTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(AccessibilityWalkTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(KnobKeyboardTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
add_test(NAME KnobAccessibleTargetSizingTest COMMAND KnobKeyboardTest accessibleTargetSurvivesKnobSizingReset)
set_tests_properties(KnobAccessibleTargetSizingTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen;ZENE_TEST_ACCESSIBLE=1")
set_tests_properties(AutomationModeMenuTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(AutomationEditorEditingTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(AutomationClipDisconnectTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(VcaStripLifetimeTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(TrackViewDeferredLifetimeTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(RackDeferredLifetimeTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(LinkedModelGroupViewLifetimeTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(NoteEditorFlTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(MixerSendsOnFadersTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(ArrangerTimelineTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(ArrangerTakePunchTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(ArrangerFileDropTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(MidiClipLinkedEditTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(MidiClipStepMenuTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(XyPadPairTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(TrackFolderMenuTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(FolderSummaryLaneTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(AutomationLaneChildGuiTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(MainWindowGeometryTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(UndoHistoryPanelTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(ExportQualityControlsTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(ExportChannelsTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(SessionSyncActionTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(FollowActionEditorTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(PluginControlDialogsTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(MixerWindowsTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(SurroundPannerTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(EffectDeviceMenuTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(EffectPickerOrganiserTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(PluginNativeEditorTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(ReferenceProjectPanelTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(TelemetryConsentDialogTest AafImportMenuTest TrackIconsLevelBarTest ArrangerKeysNavigatorTest NoteBackgroundAudioTest ArrangerOverlaysVideoLaneTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(NoteTransformActionsTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(MpeInputActionTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(TempoMapPanelTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(LivecodePanelTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(ArticulationMapEditorTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(BrowserTagMenuTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(BrowserDragUrlTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(FileSearchJobTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(VisibilitySetMenuTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(CaptureMidiMenuTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(ProjectContainerSaveAsGuiTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(ControlCommandsSaveVersionTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(MidiControllerMenuTest LfoControllerUserWaveTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(ControllerTemplateMenuTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(TrackRenderMenuTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(GrooveMenuTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(ClipEditsMenuTest SampleClipWarpModeMenuTest ClipWaveformEditTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(ChainPresetMenuTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(RackMenuTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(PinConnectorTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(ModulatorPanelTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(ChordProgressionPanelTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(ScriptBudgetActionTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(EffectHostingMenuTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(FeedbackModeActionTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(SessionTuningMenuTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(MicrotunerScaleFileTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(RecordingRecoveryMenuTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(RenderPresetMenuTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(RenderRangeMenuTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
# The export-dialog preset proof renders once through the dialog's own RenderManager (a real
# Engine + ProjectRenderer + AudioFileWave), so it gets the offscreen platform and a render budget.
set_tests_properties(ExportProjectDialogPresetTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 900)
set_tests_properties(PluginQuarantineMenuTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(CrashReportMenuTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(MidiClockMenuTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(MidiReconnectActionTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
# ThemeContrastTest reads the themes from the source tree; guiless like WcagContrastTest.
target_compile_definitions(ThemeContrastTest PRIVATE
	ZENE_THEMES_DIR="${CMAKE_SOURCE_DIR}/data/themes")
# 0.5.0 L10: DeviceCopyTest builds a drum rack from the shipped drum samples.
target_compile_definitions(DeviceCopyTest PRIVATE
	ZENE_DRUM_SAMPLES_DIR="${CMAKE_SOURCE_DIR}/data/samples/drums")

# MasteringTest drives a real Engine and the project render path, so it needs a
# Qt platform plugin even though the test itself is guiless (same reason as
# PluginScanCacheTest above).
set_tests_properties(MasteringTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
# RevisionTimelineTest drives a real Engine and the `revisions.*` group through
# the registry, writes real project documents and - where the machine has git -
# runs real `git` commands, so it needs a Qt platform plugin for the same reason
# the tests above do.
set_tests_properties(RevisionTimelineTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# The dither and SRC-quality tests build a real Engine and drive the real
# export path (AudioFileWave writes a WAV; Sample::play resamples), so they need
# a Qt platform plugin even though the tests themselves are guiless - the same
# reason as MasteringTest above.
set_tests_properties(ExportDitherTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(ExportWavDitherTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
# The write-error test builds the same Engine and drives the same export path
# (and one real render through RenderManager), for the same reason.
set_tests_properties(ExportWavWriteErrorTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
if(LMMS_HAVE_OGGVORBIS)
	set_tests_properties(AudioFileOggInitTest PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
set_tests_properties(AudioResamplerRatioTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# ---------------------------------------------------------------------------
# Remote plugin host<->client contract, driven end to end.
#
# RemotePluginClientE2ETest runs a real remote plugin client
# (RemoteZynAddSubFx, the only in-tree remote client that starts headless)
# through a real host, and pins the shared-audio-block layout with a
# hand-written client that speaks the same protocol and writes a known planar
# pattern (tests/src/plugins/FakeRemotePluginClient.cpp). The fake client uses
# POSIX sockets and shm_open directly, so it is only built on non-Windows
# hosts; the test reports that half as unexercised everywhere it is absent.
# ---------------------------------------------------------------------------
if(NOT WIN32)
	add_executable(FakeRemotePluginClient
		src/plugins/FakeRemotePluginClient.cpp
	)
	target_compile_features(FakeRemotePluginClient PRIVATE cxx_std_20)
	target_compile_definitions(RemotePluginClientE2ETest PRIVATE
		REMOTE_PLUGIN_E2E_FAKE_CLIENT="$<TARGET_FILE:FakeRemotePluginClient>"
	)
	add_dependencies(RemotePluginClientE2ETest FakeRemotePluginClient)
endif()

if(TARGET RemoteZynAddSubFx)
	target_compile_definitions(RemotePluginClientE2ETest PRIVATE
		REMOTE_PLUGIN_E2E_CLIENT_DIR="$<TARGET_FILE_DIR:RemoteZynAddSubFx>"
	)
	add_dependencies(RemotePluginClientE2ETest RemoteZynAddSubFx)
endif()

# The test constructs a full Engine and spawns the real client executable, so
# it needs a Qt platform plugin even though it is guiless (same reason as
# PluginPortsMigrationTest).
set_tests_properties(RemotePluginClientE2ETest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# StemExportTest drives the real render path (Engine + RenderManager +
# ProjectRenderer + AudioFileWave) on a project it writes to a temporary
# directory, so it needs the same headless Qt platform as the test above.
set_tests_properties(StemExportTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# PluginLogoResourceTest loads a QPixmap through the product's own artwork search
# path, so it needs a QGuiApplication and therefore a Qt platform plugin.  It
# exists because a resource that does not resolve is silent: embed::loadSvgPixmap
# warns and returns a 1x1 transparent pixmap, so nothing fails while every plugin
# dialog renders blank.
set_tests_properties(PluginLogoResourceTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# ---------------------------------------------------------------------------
# Part C (task #585) - behaviour-preservation harness for the AudioPorts
# plugin migration.
#
# The pre-migration DSP sources are extracted verbatim from the base commit
# 4ac5c3e38 into tests/reference/ (blob ids in tests/reference/ORIGIN.tsv,
# re-extract with tests/reference/extract-reference-sources.sh) and built here
# into partc_ref_* modules with the same PLUGIN_NAME define the real plugins
# use. PluginPortsMigrationReference renders them through the normal plugin
# entry point; PluginPortsMigrationTest renders the migrated plugins built by
# the normal build and compares both renders sample-exactly.
# ---------------------------------------------------------------------------
set(PART_C_REFERENCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/reference")

set(PART_C_REFERENCE_MODULES
	"amplifier|Amplifier|Amplifier.cpp|AmplifierControls.cpp|AmplifierControlDialog.cpp"
	"bassbooster|BassBooster|BassBooster.cpp|BassBoosterControls.cpp|BassBoosterControlDialog.cpp"
	"bitcrush|Bitcrush|Bitcrush.cpp|BitcrushControls.cpp|BitcrushControlDialog.cpp"
	"dualfilter|DualFilter|DualFilter.cpp|DualFilterControls.cpp|DualFilterControlDialog.cpp"
	"waveshaper|WaveShaper|WaveShaper.cpp|WaveShaperControls.cpp|WaveShaperControlDialog.cpp"
	"flanger|Flanger|FlangerEffect.cpp|FlangerControls.cpp|FlangerControlsDialog.cpp|MonoDelay.cpp"
	"delay|Delay|DelayEffect.cpp|DelayControls.cpp|DelayControlsDialog.cpp|Lfo.cpp|StereoDelay.cpp"
	"compressor|Compressor|Compressor.cpp|CompressorControls.cpp|CompressorControlDialog.cpp"
	"crossovereq|CrossoverEQ|CrossoverEQ.cpp|CrossoverEQControls.cpp|CrossoverEQControlDialog.cpp"
	"dynamicsprocessor|DynamicsProcessor|DynamicsProcessor.cpp|DynamicsProcessorControls.cpp|DynamicsProcessorControlDialog.cpp"
	"lomm|LOMM|LOMM.cpp|LOMMControls.cpp|LOMMControlDialog.cpp"
	"multitapecho|MultitapEcho|MultitapEcho.cpp|MultitapEchoControls.cpp|MultitapEchoControlDialog.cpp"
	"reverbsc|ReverbSC|ReverbSC.cpp|ReverbSCControls.cpp|ReverbSCControlDialog.cpp|base.c|revsc.c|dcblock.c"
	"stereoenhancer|StereoEnhancer|StereoEnhancer.cpp|StereoEnhancerControls.cpp|StereoEnhancerControlDialog.cpp"
	"stereomatrix|StereoMatrix|StereoMatrix.cpp|StereoMatrixControls.cpp|StereoMatrixControlDialog.cpp"
	# Slice 3 (task #589): analysers, Dispersion, granular shifter and Eq.
	"dispersion|Dispersion|Dispersion.cpp|DispersionControls.cpp|DispersionControlDialog.cpp"
	"vectorscope|Vectorscope|Vectorscope.cpp|VecControls.cpp|VecControlsDialog.cpp|VectorView.cpp"
	"analyzer|SpectrumAnalyzer|Analyzer.cpp|SaControls.cpp|SaControlsDialog.cpp|SaProcessor.cpp|SaSpectrumView.cpp|SaWaterfallView.cpp"
	"granularpitchshifter|GranularPitchShifter|GranularPitchShifterEffect.cpp|GranularPitchShifterControls.cpp|GranularPitchShifterControlDialog.cpp"
	"eq|Eq|EqEffect.cpp|EqControls.cpp|EqControlsDialog.cpp|EqCurve.cpp|EqParameterWidget.cpp|EqSpectrumView.cpp"
	# Slice 4 (task #589): synth instruments.
	"freeboy|FreeBoy|FreeBoy.cpp|FreeBoy.h|GbApuWrapper.cpp|GbApuWrapper.h"
	"nes|Nes|Nes.cpp|Nes.h"
	"sid|Sid|SidInstrument.cpp|SidInstrument.h"
	"opulenz|OpulenZ|OpulenZ.cpp|OpulenZ.h"
	"sfxr|Sfxr|Sfxr.cpp|Sfxr.h"
	"bitinvader|BitInvader|BitInvader.cpp|BitInvader.h"
	"watsyn|Watsyn|Watsyn.cpp|Watsyn.h"
	"xpressive|Xpressive|Xpressive.cpp|Xpressive.h|ExprSynth.cpp|ExprSynth.h"
	"vibedstrings|Vibed|Vibed.cpp|Vibed.h|NineButtonSelector.cpp|NineButtonSelector.h|VibratingString.cpp|VibratingString.h"
	"kicker|Kicker|Kicker.cpp|Kicker.h|KickerOsc.h"
	# Slice 5 (task #589): multi-oscillator and sample-playback instruments.
	"tripleoscillator|TripleOscillator|TripleOscillator.cpp|TripleOscillator.h"
	"monstro|Monstro|Monstro.cpp|Monstro.h"
	"organic|Organic|Organic.cpp|Organic.h"
	"audiofileprocessor|AudioFileProcessor|AudioFileProcessor.cpp|AudioFileProcessor.h|AudioFileProcessorView.cpp|AudioFileProcessorView.h|AudioFileProcessorWaveView.cpp|AudioFileProcessorWaveView.h"
	# Slice 6 (task #589): LADSPA host and the Lb302 bass synth.
	"lb302|Lb302|Lb302.cpp|Lb302.h"
	"ladspaeffect|LadspaEffect|LadspaEffect.cpp|LadspaEffect.h|LadspaControls.cpp|LadspaControls.h|LadspaControlDialog.cpp|LadspaControlDialog.h|LadspaMatrixControlDialog.cpp|LadspaMatrixControlDialog.h|LadspaSubPluginFeatures.cpp|LadspaSubPluginFeatures.h|LadspaWidgetFactory.cpp|LadspaWidgetFactory.h"
	# Slice 7 (task #589): the last in-tree legacy-API plugins.
	"frequencyshifter|FrequencyShifter|FrequencyShifterEffect.cpp|FrequencyShifterControls.cpp|FrequencyShifterControlDialog.cpp|HilbertTransform.h"
	"oscilloscope|Oscilloscope|Oscilloscope.cpp|OscilloscopeControls.cpp|OscilloscopeControlDialog.cpp|OscilloscopeGraph.cpp|OscilloscopeGraph.h"
	"slewdistortion|SlewDistortion|SlewDistortion.cpp|SlewDistortionControls.cpp|SlewDistortionControlDialog.cpp"
)

# Slice 8 (task #589): the external-asset hosts. Each entry is only added when
# the build actually has the dependency (mirroring the guard in the plugin's
# own CMakeLists.txt) and, for Sf2Player, when a soundfont exists to render -
# a module the harness cannot drive would compare silence with silence and
# prove nothing.
if(LMMS_HAVE_LV2)
	list(APPEND PART_C_REFERENCE_MODULES
		"lv2effect|Lv2Effect|Lv2Effect.cpp|Lv2Effect.h|Lv2FxControls.cpp|Lv2FxControls.h|Lv2FxControlDialog.cpp|Lv2FxControlDialog.h"
		"lv2instrument|Lv2Instrument|Lv2Instrument.cpp|Lv2Instrument.h")
endif()
# MX-157 (BUGS 11.34): the LV2 instrument fixture - a bundle whose instance reports a latency on its
# latency port when LV2_TEST_INSTRUMENT_LATENCY is set - is InstrumentLatencyPdcTest's LV2 subject.
# It is Lv2StateTest's subject too: a real LV2 plug-in is what makes a state restore attempt
# (and its failure) possible without a plug-in installed on the machine.
if(LMMS_HAVE_LV2 AND TARGET lv2instrument AND (TARGET InstrumentLatencyPdcTest OR TARGET Lv2StateTest))
	add_subdirectory(data/lv2-test-instrument)
	get_filename_component(LMMS_LV2_LATENCY_FIXTURE_DIR "${LMMS_LV2_TEST_INSTRUMENT_BUNDLE}" DIRECTORY)
	if(TARGET InstrumentLatencyPdcTest)
		target_compile_definitions(InstrumentLatencyPdcTest PRIVATE
			LV2_TEST_INSTRUMENT_DIR="${LMMS_LV2_LATENCY_FIXTURE_DIR}")
		add_dependencies(InstrumentLatencyPdcTest lv2instrument lv2-test-instrument)
	endif()
	if(TARGET Lv2StateTest)
		target_compile_definitions(Lv2StateTest PRIVATE
			LV2_TEST_INSTRUMENT_DIR="${LMMS_LV2_LATENCY_FIXTURE_DIR}")
		add_dependencies(Lv2StateTest lv2instrument lv2-test-instrument)
	endif()
endif()
find_file(PART_C_SF2_FILE NAMES FluidR3_GM.sf2 default-GM.sf2
	HINTS /usr/share/sounds/sf2 /usr/local/share/sounds/sf2)
if(LMMS_HAVE_FLUIDSYNTH AND PART_C_SF2_FILE)
	list(APPEND PART_C_REFERENCE_MODULES
		"sf2player|Sf2Player|Sf2Player.cpp|Sf2Player.h|PatchesDialog.cpp|PatchesDialog.h|PatchesDialog.ui")
endif()
# GigPlayer renders from a .gig instrument file. With no file to load, both the
# migrated and the reference host render silence and the sample-exact
# comparison proves nothing, so the module is only added when the dependency
# AND an instrument file are both present. tests/data/gigproof/sine440.gig is
# vendored for exactly this purpose (see its README for provenance and how to
# regenerate it); the test reports gigplayer as UNPROVEN when it is absent.
find_file(PART_C_GIG_FILE NAMES sine440.gig
	HINTS "${CMAKE_SOURCE_DIR}/tests/data/gigproof"
	      "${CMAKE_SOURCE_DIR}/../tests/data/gigproof")
if(LMMS_HAVE_GIG AND PART_C_GIG_FILE)
	list(APPEND PART_C_REFERENCE_MODULES
		"gigplayer|GigPlayer|GigPlayer.cpp|GigPlayer.h|PatchesDialog.cpp|PatchesDialog.h|PatchesDialog.ui")
endif()
# Mallets renders from the STK rawwave directory; without it the instrument
# latches m_filesMissing in its constructor and renders silence, which would
# make the sample-exact comparison vacuous. So the module is only added when
# the waves are actually present.
find_path(PART_C_STK_DIR NAMES sinewave.raw
	HINTS "${CMAKE_SOURCE_DIR}/thirdparty/stk-rawwaves"
	      "${CMAKE_SOURCE_DIR}/../thirdparty/stk-rawwaves"
	      /usr/share/stk/rawwaves /usr/local/share/stk/rawwaves)
if(LMMS_HAVE_STK AND PART_C_STK_DIR)
	list(APPEND PART_C_REFERENCE_MODULES
		"malletsstk|Stk/Mallets|Mallets.cpp|Mallets.h")
endif()

set(PART_C_REFERENCE_TARGETS "")
set(PART_C_MIGRATED_TARGETS "")
set(PART_C_REFERENCE_DEFINITIONS "")
set(PART_C_MIGRATED_DEFINITIONS "")

# --- MSVC: the reference modules link one at a time ---------------------------
# CI msvc-x64 (job 103701272689, run 34748733618, edge 824/2190) failed the build
# on one of these modules, and not on a symbol error:
#
#   FAILED: [code=1] tests/partc_ref_bassbooster.dll
#   ... cmd.exe /C "cd . && ccache "cmake" -E vs_link_dll ... -- link.exe ... && cmd.exe
#        /C "cd /D ...\build\tests && C:\vcpkg\vcpkg.exe z-applocal
#           --target-binary=...build/tests/partc_ref_bassbooster.dll
#           --installed-bin-dir=...build/vcpkg_installed/x64-windows/bin""
#   The process cannot access the file because it is being used by another process.
#
# The job's whole log carries no LNK2005, LNK1169, LNK2019 or LNK1120 (grep count
# 0), so this is NOT the duplicate-symbol collision the two VST3 modules hit
# (plugins/Vst3Effect/CMakeLists.txt) and /FORCE:MULTIPLE is the wrong tool: it
# would suppress nothing and fix nothing. The only diagnostic is a Win32 sharing
# violation, charged to the module because that is the edge the failing command
# belongs to - the vcpkg toolchain appends `vcpkg z-applocal` as a POST_BUILD step
# of every MODULE/SHARED target in a Windows triplet (scripts/buildsystems/vcpkg.cmake,
# the add_library wrapper) and CMake chains it into the link rule. z-applocal copies
# that binary's imported vcpkg dependency DLLs into the binary's own directory
# (vcpkg-tool commands.z-applocal.cpp: deploy_binary -> copy_file into
# target_binary_dir), and every module above maps to build\tests. They also link in
# one burst - all 41 of them between 09:13:40 and 09:13:47 in the passing run
# 34748729956 - several at a time, and they link *before* the 75 test executables and
# the test-double module that share that directory. So they are the ones whose
# post-link steps really do copy the shared dependency DLLs; every later link into
# build\tests finds those files present and skips them. Two of those copies racing
# over the same destination file is the sharing violation, and the module that loses
# that race is reported as a build failure even though its own link succeeded
# (bassbooster's four objects compile and its link is clean in every other msvc-x64
# run, and in the main-branch run of the identical commit).
#
# vcpkg's own knob for this class, X_VCPKG_APPLOCAL_DEPS_SERIALIZED, passes
# USES_TERMINAL only from the toolchain's add_executable wrapper - the add_library
# wrapper never sets it - so it cannot cover a MODULE target. A Ninja pool of one
# (ninja is the Windows generator) orders these links, and with them their post-link
# steps, against each other and nothing else, and it cannot hide a real link error.
# HONEST LIMIT - CLOSED. This note used to end: "this closes the window the
# race needs, the first burst of writers into a fresh build	ests. If a later
# change makes another target in this file the first writer into that directory,
# the same property belongs on it too, or on every target here via
# CMAKE_JOB_POOL_LINK." A later change did exactly that: run 34794059702 failed
# the build on `[2411/2443] Linking CXX executable tests\RoutingGraphTest.exe`
# with the same bare `Access is denied.` and no LNK/C diagnostic, and a test
# executable is not a MODULE, so the per-target property below never covered it.
# The property now lives at the top of this file, on EVERY target built from
# it, via CMAKE_JOB_POOL_LINK (see the note there); the loop below keeps its own
# explicit set only so a target created before that variable were ever set is
# still covered. Nothing here is the remedy: it is the record of the race.
#
# The declaration of the pool lives at the top of this file with the variable.
# Ninja errors if a rule names a pool JOB_POOLS does not declare, so a
# set_property(TARGET ... JOB_POOL_LINK ...) without it must never be added
# without the GLOBAL declaration beside it.

foreach(ENTRY IN LISTS PART_C_REFERENCE_MODULES)
	string(REPLACE "|" ";" FIELDS "${ENTRY}")
	list(GET FIELDS 0 PLUGIN_ID)
	list(GET FIELDS 1 PLUGIN_DIR)
	list(REMOVE_AT FIELDS 0 1)

	set(PLUGIN_SOURCES "")
	foreach(SRC IN LISTS FIELDS)
		list(APPEND PLUGIN_SOURCES "${PART_C_REFERENCE_DIR}/${PLUGIN_DIR}/${SRC}")
	endforeach()

	if(PLUGIN_ID STREQUAL "delay" OR PLUGIN_ID STREQUAL "compressor" OR PLUGIN_ID STREQUAL "eq")
		# EqFader is header-only; AUTOMOC needs the header to emit its
		# metaobject and vtable, which DelayControlsDialog.cpp and
		# CompressorControlDialog.cpp reference.
		list(APPEND PLUGIN_SOURCES "${PART_C_REFERENCE_DIR}/Eq/EqFader.h")
	endif()

	add_library(partc_ref_${PLUGIN_ID} MODULE ${PLUGIN_SOURCES})
	if(MSVC)
		# The link and its post-link applocal step, one target at a time. This is
		# now the directory-wide default via CMAKE_JOB_POOL_LINK; set explicitly
		# here too so the property is on the target whatever the variable did.
		set_property(TARGET partc_ref_${PLUGIN_ID} PROPERTY JOB_POOL_LINK zene_tests_link)
	endif()
	target_include_directories(partc_ref_${PLUGIN_ID} PRIVATE
		"${PART_C_REFERENCE_DIR}"
		"${PART_C_REFERENCE_DIR}/${PLUGIN_DIR}"
		"${PART_C_REFERENCE_DIR}/Eq"
		$<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>
	)
	target_compile_definitions(partc_ref_${PLUGIN_ID} PRIVATE PLUGIN_NAME=${PLUGIN_ID})
	target_compile_features(partc_ref_${PLUGIN_ID} PRIVATE cxx_std_20)
	target_link_libraries(partc_ref_${PLUGIN_ID} PRIVATE zene ${QT_LIBRARIES})
	# The reference sources are verbatim copies of the pre-migration plugin
	# (git blob ids in tests/reference/ORIGIN.tsv), so they cannot be edited the
	# way the migrated plugin was: rewriting one would destroy the evidence the
	# harness compares against. Under Qt 6 the frozen GUI code calls
	# QMouseEvent::x()/y(), which Qt 6 deprecates, and USE_WERROR promotes that
	# deprecation to an error on any compiler that does not treat Qt's
	# angle-bracket headers as external (MSVC's /external:anglebrackets hides it
	# there). The deprecation is not actionable in frozen evidence, so the
	# reference targets - and only they - opt out of it.
	target_compile_options(partc_ref_${PLUGIN_ID} PRIVATE
		$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-Wno-deprecated-declarations>)
	set_target_properties(partc_ref_${PLUGIN_ID} PROPERTIES PREFIX "" OUTPUT_NAME "partc_ref_${PLUGIN_ID}")

	# Slice 4 (task #589): instruments whose plugin CMakeLists.txt links extra
	# targets or sets extra defines - mirror them so the reference module is
	# built exactly like the pre-migration plugin was.
	if(PLUGIN_ID STREQUAL "slewdistortion")
		# The plugin links hiir itself; OversamplingHelpers.h pulls in the
		# hiir up/down-sampler headers.
		target_link_libraries(partc_ref_slewdistortion PRIVATE hiir)
	elseif(PLUGIN_ID STREQUAL "freeboy")
		target_link_libraries(partc_ref_freeboy PRIVATE gme)
	elseif(PLUGIN_ID STREQUAL "sid")
		target_link_libraries(partc_ref_sid PRIVATE resid)
	elseif(PLUGIN_ID STREQUAL "opulenz")
		target_link_libraries(partc_ref_opulenz PRIVATE adplug)
	elseif(PLUGIN_ID STREQUAL "watsyn")
		target_link_libraries(partc_ref_watsyn PRIVATE SampleRate::samplerate)
	elseif(PLUGIN_ID STREQUAL "xpressive")
		target_link_libraries(partc_ref_xpressive PRIVATE exprtk)
		# exprtk.hpp blows past the object-file section limit; the reference target
		# compiles the same header as the plugin, so it needs the same escape hatch
		# (CI mingw64: "/tmp/cc….s: Fatal error: … ExprSynth.cpp.obj: 'file too big'").
		# MSVC wants /bigobj, mingw wants the assembler-level flag.
		if(MSVC)
			target_compile_options(partc_ref_xpressive PRIVATE /bigobj)
		elseif(LMMS_BUILD_WIN32 OR LMMS_BUILD_CYGWIN)
			target_compile_options(partc_ref_xpressive PRIVATE -Wa,-mbig-obj)
		endif()
		target_compile_definitions(partc_ref_xpressive PRIVATE
			exprtk_disable_sc_andor
			exprtk_disable_return_statement
			exprtk_disable_break_continue
			exprtk_disable_comments
			exprtk_disable_string_capabilities
			exprtk_disable_rtl_io_file
			exprtk_disable_rtl_vecops)
	elseif(PLUGIN_ID STREQUAL "sf2player")
		# Mirror plugins/Sf2Player/CMakeLists.txt: fluidsynth + samplerate,
		# and PatchesDialog.ui is built with AUTOUIC like the real plugin.
		set_target_properties(partc_ref_sf2player PROPERTIES AUTOUIC ON)
		target_link_libraries(partc_ref_sf2player PRIVATE fluidsynth SampleRate::samplerate)
	elseif(PLUGIN_ID STREQUAL "gigplayer")
		# Mirror plugins/GigPlayer/CMakeLists.txt: libgig include dirs, the
		# forced exceptions libgig needs to not crash, samplerate link.
		set_target_properties(partc_ref_gigplayer PROPERTIES AUTOUIC ON)
		target_include_directories(partc_ref_gigplayer SYSTEM PRIVATE ${GIG_INCLUDE_DIRS})
		if(NOT MSVC)
			# -fexceptions is GNU-style: MSVC ignores it and enables C++
			# exceptions by default, so the guard mirrors plugins/GigPlayer/CMakeLists.txt.
			target_compile_options(partc_ref_gigplayer PRIVATE -fexceptions)
		endif()
		# The plugin does link_directories(${GIG_LIBRARY_DIRS}) before linking
		# ${GIG_LIBRARIES}; without the directory the reference target's link fails on
		# systems where libgig lives outside the default search path
		# (CI macos-arm64: "ld: library 'gig' not found").
		if(GIG_LIBRARY_DIRS)
			target_link_directories(partc_ref_gigplayer PRIVATE ${GIG_LIBRARY_DIRS})
		endif()
		target_link_libraries(partc_ref_gigplayer PRIVATE
			SampleRate::samplerate ${GIG_LIBRARIES})
	elseif(PLUGIN_ID STREQUAL "malletsstk")
		target_link_libraries(partc_ref_malletsstk PRIVATE unofficial::libstk::libstk)
	elseif(PLUGIN_ID STREQUAL "lv2effect" OR PLUGIN_ID STREQUAL "lv2instrument")
		# The LV2/Lilv/Suil headers the plugins include; the libraries arrive
		# through lmmsobjs, which both harness binaries link.
		target_include_directories(partc_ref_${PLUGIN_ID} PRIVATE
			${LV2_INCLUDE_DIRS} ${Lilv_INCLUDE_DIRS} ${SUIL_INCLUDE_DIRS})
	endif()

	list(APPEND PART_C_REFERENCE_TARGETS partc_ref_${PLUGIN_ID})
	list(APPEND PART_C_MIGRATED_TARGETS ${PLUGIN_ID})
	list(APPEND PART_C_REFERENCE_DEFINITIONS
		"PART_C_REF_${PLUGIN_ID}=\"$<TARGET_FILE:partc_ref_${PLUGIN_ID}>\"")
	list(APPEND PART_C_MIGRATED_DEFINITIONS
		"PART_C_MIGRATED_${PLUGIN_ID}=\"$<TARGET_FILE:${PLUGIN_ID}>\"")
endforeach()

# ExprFront's allocation-failure path is compiled from the product source so
# the test observes its constructor and destructor directly. Keep the CTest
# name aligned with the Xpressive expression frontend test family.
add_executable(ExprSynthAllocationTest
	src/plugins/ExprSynthAllocationTest.cpp
	${CMAKE_SOURCE_DIR}/plugins/Xpressive/ExprSynth.cpp
)
# The CTest name matches the target and the source file, so a harness that
# rebuilds the changed test targets rebuilds this one (see ExprSynthTest above).
add_test(NAME ExprSynthAllocationTest COMMAND ExprSynthAllocationTest)
target_include_directories(ExprSynthAllocationTest PRIVATE
	$<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>
	${CMAKE_SOURCE_DIR}/plugins/Xpressive
)
target_static_libraries(ExprSynthAllocationTest PRIVATE lmmsobjs)
target_link_libraries(ExprSynthAllocationTest PRIVATE
	${QT_LIBRARIES}
	${QT_QTTEST_LIBRARY}
	exprtk
)
target_compile_features(ExprSynthAllocationTest PRIVATE cxx_std_20)
target_compile_definitions(ExprSynthAllocationTest PRIVATE
	exprtk_disable_sc_andor
	exprtk_disable_return_statement
	exprtk_disable_break_continue
	exprtk_disable_comments
	exprtk_disable_string_capabilities
	exprtk_disable_rtl_io_file
	exprtk_disable_rtl_vecops
)
if(MSVC)
	target_compile_options(ExprSynthAllocationTest PRIVATE /bigobj)
elseif(LMMS_BUILD_WIN32 OR LMMS_BUILD_CYGWIN)
	target_compile_options(ExprSynthAllocationTest PRIVATE -Wa,-mbig-obj)
endif()
set_tests_properties(ExprSynthAllocationTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Slice 6 (task #589): LadspaEffect resolves its LADSPA libraries through
# LADSPA_PATH at runtime, and both the test and the reference renderer have to
# point at the in-tree SWH builds. The dir is taken from the plugin targets so
# no path is hardcoded; the two plugins the harness uses are also added as
# build dependencies of the test so they exist before ctest runs.
set(PART_C_LADSPA_TARGETS "")
if(TARGET amp_1181 AND TARGET dj_eq_1901)
	list(APPEND PART_C_REFERENCE_DEFINITIONS
		"PART_C_LADSPA_DIR=\"$<TARGET_FILE_DIR:amp_1181>\"")
	list(APPEND PART_C_MIGRATED_DEFINITIONS
		"PART_C_LADSPA_DIR=\"$<TARGET_FILE_DIR:amp_1181>\"")
	list(APPEND PART_C_LADSPA_TARGETS amp_1181 dj_eq_1901)
	add_library(partial_init_failure_ladspa MODULE
		src/plugins/PartialInitFailureLadspa.cpp)
	target_include_directories(partial_init_failure_ladspa PRIVATE
		${CMAKE_SOURCE_DIR}/include)
	target_compile_features(partial_init_failure_ladspa PRIVATE cxx_std_20)
	if(MSVC)
		target_link_options(partial_init_failure_ladspa PRIVATE /EXPORT:ladspa_descriptor
			/EXPORT:partial_init_failure_reset /EXPORT:partial_init_failure_instantiations
			/EXPORT:partial_init_failure_cleanups)
	endif()
	set_target_properties(partial_init_failure_ladspa PROPERTIES
		PREFIX ""
		OUTPUT_NAME "partial_init_failure"
		LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/ladspa-fixtures"
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/ladspa-fixtures")
	list(APPEND PART_C_LADSPA_TARGETS partial_init_failure_ladspa)
endif()

# Slice 8 (task #589): runtime assets. The STK rawwaves are vendored in the
# tree, so Mallets reads the same directory in both processes; the soundfont
# path is only defined when one was found, which is also what gates the
# sf2player module above.
if(TARGET malletsstk)
	list(APPEND PART_C_REFERENCE_DEFINITIONS "PART_C_STK_DIR=\"${PART_C_STK_DIR}\"")
	list(APPEND PART_C_MIGRATED_DEFINITIONS "PART_C_STK_DIR=\"${PART_C_STK_DIR}\"")
endif()
if(PART_C_SF2_FILE)
	list(APPEND PART_C_REFERENCE_DEFINITIONS "PART_C_SF2_FILE=\"${PART_C_SF2_FILE}\"")
	list(APPEND PART_C_MIGRATED_DEFINITIONS "PART_C_SF2_FILE=\"${PART_C_SF2_FILE}\"")
endif()
if(PART_C_GIG_FILE)
	list(APPEND PART_C_REFERENCE_DEFINITIONS "PART_C_GIG_FILE=\"${PART_C_GIG_FILE}\"")
	list(APPEND PART_C_MIGRATED_DEFINITIONS "PART_C_GIG_FILE=\"${PART_C_GIG_FILE}\"")
endif()

add_executable(PluginPortsMigrationReference
	src/plugins/PluginPortsMigrationReference.cpp
	src/plugins/PluginPortsHarness.h
)
target_include_directories(PluginPortsMigrationReference PRIVATE
	$<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>
)
target_static_libraries(PluginPortsMigrationReference PRIVATE lmmsobjs)
target_link_libraries(PluginPortsMigrationReference PRIVATE ${QT_LIBRARIES})
target_compile_features(PluginPortsMigrationReference PRIVATE cxx_std_20)
target_compile_definitions(PluginPortsMigrationReference PRIVATE
	LMMS_TESTING
	${PART_C_REFERENCE_DEFINITIONS}
)
set_target_properties(PluginPortsMigrationReference PROPERTIES ENABLE_EXPORTS ON)
add_dependencies(PluginPortsMigrationReference ${PART_C_REFERENCE_TARGETS})

add_executable(PluginPortsMigrationTest
	src/plugins/PluginPortsMigrationTest.cpp
	src/plugins/PluginPortsHarness.h
)
add_test(NAME PluginPortsMigrationTest COMMAND PluginPortsMigrationTest)
# The harness constructs a full Engine, which needs a Qt platform plugin even
# though the test itself is guiless. Without this, ctest aborts before the first
# assertion on any machine with no display (CI, headless shell).
set_tests_properties(PluginPortsMigrationTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
# Cluster new-p9-326: the focused OpulenZ held-note regression. It runs in the
# PluginPortsMigrationTest executable (the plugin-module host it needs lives
# there), so the registration is a second ctest entry over the same binary.
add_test(NAME OpulenZ COMMAND PluginPortsMigrationTest opulenzReappliesVelocityScalingToHeldVoice)
set_tests_properties(OpulenZ PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
add_test(NAME OpulenZSpanSize COMMAND PluginPortsMigrationTest opulenzRendersTheProvidedSpanSize)
set_tests_properties(OpulenZSpanSize PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
add_test(NAME OpulenZZeroFrameCount COMMAND PluginPortsMigrationTest opulenzZeroFrameCountStopsLoop)
set_tests_properties(OpulenZZeroFrameCount PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_include_directories(PluginPortsMigrationTest PRIVATE
	$<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>
)
target_static_libraries(PluginPortsMigrationTest PRIVATE lmmsobjs)
target_link_libraries(PluginPortsMigrationTest PRIVATE
	${QT_LIBRARIES}
	${QT_QTTEST_LIBRARY}
)
target_compile_features(PluginPortsMigrationTest PRIVATE cxx_std_20)
target_compile_definitions(PluginPortsMigrationTest PRIVATE
	LMMS_TESTING
	PART_C_REFERENCE_EXE="$<TARGET_FILE:PluginPortsMigrationReference>"
	QT_TESTCASE_SOURCEDIR="${CMAKE_CURRENT_SOURCE_DIR}"
	# Cluster new-p17-202: the Watsyn regression reads the plugin's own
	# sources, so it needs to know where they are.
	WATSYN_PLUGIN_SOURCE_DIR="${CMAKE_SOURCE_DIR}/plugins/Watsyn"
	${PART_C_MIGRATED_DEFINITIONS}
)
if(TARGET partial_init_failure_ladspa)
	target_compile_definitions(PluginPortsMigrationTest PRIVATE
		"PART_C_FAILURE_LADSPA_MODULE=\"$<TARGET_FILE:partial_init_failure_ladspa>\"")
endif()
set_target_properties(PluginPortsMigrationTest PROPERTIES ENABLE_EXPORTS ON)
add_dependencies(PluginPortsMigrationTest
	PluginPortsMigrationReference
	${PART_C_MIGRATED_TARGETS}
	${PART_C_LADSPA_TARGETS}
)

# Cluster new-p9-254 / new-p14-134: the LB302 note path must stay
# realtime-safe. The enqueue failsafe of the instrument (the bounded
# note-enqueue path that runs on the audio render thread) may neither log nor
# allocate; it counts the note it drops and a control-thread timer reports
# the count. The plugin's own sources are compiled into this test binary (the
# SlicerTZoomTest / Vst3EffectIntegrationTest pattern), so the code under test
# is the shipped one and the test loads no plugin module: it runs on every
# platform, with no platform skip.
add_executable(Lb302NotePathTest
	src/plugins/Lb302NotePathTest.cpp
	${CMAKE_SOURCE_DIR}/plugins/Lb302/Lb302.cpp
	${CMAKE_SOURCE_DIR}/plugins/Lb302/Lb302.h
)
add_test(NAME Lb302NotePathTest COMMAND Lb302NotePathTest)
set_tests_properties(Lb302NotePathTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_include_directories(Lb302NotePathTest PRIVATE
	# the generated plugin_export.h (PLUGIN_EXPORT) of the lb302 plug-in
	${CMAKE_BINARY_DIR}/plugins/Lb302
	$<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>
	${CMAKE_SOURCE_DIR}/plugins/Lb302
)
# the test compiles the plug-in sources itself, so the generated export header
# of the plug-in target has to exist first
add_dependencies(Lb302NotePathTest lb302)
target_static_libraries(Lb302NotePathTest PRIVATE lmmsobjs)
target_compile_features(Lb302NotePathTest PRIVATE cxx_std_20)
target_link_libraries(Lb302NotePathTest PRIVATE
	${QT_LIBRARIES}
	${QT_QTTEST_LIBRARY}
)
target_compile_definitions(Lb302NotePathTest PRIVATE
	LMMS_TESTING
	# this binary links the plug-in's code in, it does not import it: the
	# static branch of the generated export header is the correct one here
	# (the same reason Vst3EffectIntegrationTest sets it).
	PLUGIN_STATIC_DEFINE
	PLUGIN_NAME=lb302
)

# Regression for cluster new-p10-377: SlicerT waveform zoom clamped only at
# zero (waveform disappears) and unbounded above. The plugin is built as a
# module, so its sources are compiled into the test binary like WasmSandboxTest.
add_executable(SlicerTZoomTest
	src/plugins/SlicerTZoomTest.cpp
	${CMAKE_SOURCE_DIR}/plugins/SlicerT/SlicerT.cpp
	${CMAKE_SOURCE_DIR}/plugins/SlicerT/SlicerT.h
	${CMAKE_SOURCE_DIR}/plugins/SlicerT/SlicerTView.cpp
	${CMAKE_SOURCE_DIR}/plugins/SlicerT/SlicerTView.h
	${CMAKE_SOURCE_DIR}/plugins/SlicerT/SlicerTWaveform.cpp
	${CMAKE_SOURCE_DIR}/plugins/SlicerT/SlicerTWaveform.h
)
add_test(NAME SlicerTZoomTest COMMAND SlicerTZoomTest)
target_include_directories(SlicerTZoomTest PRIVATE
	$<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>
	${CMAKE_SOURCE_DIR}/plugins/SlicerT
	${CMAKE_BINARY_DIR}/plugins/SlicerT
)
target_static_libraries(SlicerTZoomTest PRIVATE lmmsobjs)
target_link_libraries(SlicerTZoomTest PRIVATE
	${QT_LIBRARIES}
	${QT_QTTEST_LIBRARY}
	${FFTW3F_LIBRARIES}
)
target_compile_features(SlicerTZoomTest PRIVATE cxx_std_20)
target_compile_definitions(SlicerTZoomTest PRIVATE
	LMMS_TESTING
	PLUGIN_NAME=slicert
)
set_tests_properties(SlicerTZoomTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Cluster new-p12-162-port: the Multitap Echo constructor must not read the
# low-pass entries past the graph length - on the historical line it asked the
# filter update for twenty taps while the graph fills only the sixteen entries
# it covers. The plugin sources are compiled into this test binary (the
# SlicerTZoomTest pattern) so the effect can be constructed in a byte-poisoned
# buffer: the regression then measures the constructor itself, deterministically
# and without loading a plugin module at runtime.
add_executable(MultitapEchoBoundsTest
	src/plugins/MultitapEchoBoundsTest.cpp
	${CMAKE_SOURCE_DIR}/plugins/MultitapEcho/MultitapEcho.cpp
	${CMAKE_SOURCE_DIR}/plugins/MultitapEcho/MultitapEchoControls.cpp
	${CMAKE_SOURCE_DIR}/plugins/MultitapEcho/MultitapEchoControlDialog.cpp
)
add_test(NAME MultitapEchoBoundsTest COMMAND MultitapEchoBoundsTest)
# Engine construction needs a Qt platform plugin on a headless machine.
set_tests_properties(MultitapEchoBoundsTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_include_directories(MultitapEchoBoundsTest PRIVATE
	$<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>
	${CMAKE_SOURCE_DIR}/plugins/MultitapEcho
	# the generated plugin_export.h (PLUGIN_EXPORT) of the multitapecho plugin
	${CMAKE_BINARY_DIR}/plugins/MultitapEcho
)
target_static_libraries(MultitapEchoBoundsTest PRIVATE lmmsobjs)
target_link_libraries(MultitapEchoBoundsTest PRIVATE
	${QT_LIBRARIES}
	${QT_QTTEST_LIBRARY}
)
target_compile_features(MultitapEchoBoundsTest PRIVATE cxx_std_20)
target_compile_definitions(MultitapEchoBoundsTest PRIVATE
	LMMS_TESTING
	PLUGIN_NAME=multitapecho
	# the test compiles the plugin sources itself, so PLUGIN_EXPORT must not
	# be the import attribute of the plugin's own export header
	PLUGIN_STATIC_DEFINE
)
# the test compiles the plugin sources itself, so it needs the generated
# export header of the plugin target to exist first
if(TARGET multitapecho)
	add_dependencies(MultitapEchoBoundsTest multitapecho)
endif()

# Cluster new-p12-275: TripleOscillator's user-wave buffer and anti-alias table
# were published straight onto the model from the control thread while
# playNoteImpl() read them on the audio thread. The plugin sources are compiled
# into this test binary (the SlicerTZoomTest pattern) so the regression can
# drive the publication itself and hold a render period open around it.
add_executable(TripleOscillatorUserWaveTest
	src/plugins/TripleOscillatorUserWaveTest.cpp
	${CMAKE_SOURCE_DIR}/plugins/TripleOscillator/TripleOscillator.cpp
	${CMAKE_SOURCE_DIR}/plugins/TripleOscillator/TripleOscillator.h
)
add_test(NAME TripleOscillatorUserWaveTest COMMAND TripleOscillatorUserWaveTest)
target_include_directories(TripleOscillatorUserWaveTest PRIVATE
	$<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>
	${CMAKE_SOURCE_DIR}/plugins/TripleOscillator
	${CMAKE_BINARY_DIR}/plugins/TripleOscillator
	${FFTW3F_INCLUDE_DIRS}
)
target_static_libraries(TripleOscillatorUserWaveTest PRIVATE lmmsobjs)
target_link_libraries(TripleOscillatorUserWaveTest PRIVATE
	${QT_LIBRARIES}
	${QT_QTTEST_LIBRARY}
	${FFTW3F_LIBRARIES}
)
target_compile_features(TripleOscillatorUserWaveTest PRIVATE cxx_std_20)
target_compile_definitions(TripleOscillatorUserWaveTest PRIVATE
	LMMS_TESTING
	PLUGIN_NAME=tripleoscillator
)
set_tests_properties(TripleOscillatorUserWaveTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Cluster new-p11-82: the Delay plugin's delay-time parameter is read by its
# displayed name (device cards and plugin.param_get), and its value is in
# seconds - the effect multiplies it by the sample rate. The label must name
# seconds. The plugin sources are compiled into this test binary (the WasmSandboxTest
# pattern) so controlEffectParameters - the same enumeration the device card uses -
# can read the model list without loading a .so.
add_executable(DelayControlsTest
	src/plugins/DelayControlsTest.cpp
	${CMAKE_SOURCE_DIR}/plugins/Delay/DelayEffect.cpp
	${CMAKE_SOURCE_DIR}/plugins/Delay/DelayControls.cpp
	${CMAKE_SOURCE_DIR}/plugins/Delay/DelayControlsDialog.cpp
	${CMAKE_SOURCE_DIR}/plugins/Delay/Lfo.cpp
	${CMAKE_SOURCE_DIR}/plugins/Delay/StereoDelay.cpp
	${CMAKE_SOURCE_DIR}/plugins/Eq/EqFader.h
)
add_test(NAME DelayControlsTest COMMAND DelayControlsTest)
set_tests_properties(DelayControlsTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_include_directories(DelayControlsTest PRIVATE
	$<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>
	${CMAKE_SOURCE_DIR}/plugins/Delay
	${CMAKE_BINARY_DIR}/plugins/Delay
)
target_static_libraries(DelayControlsTest PRIVATE lmmsobjs)
target_link_libraries(DelayControlsTest PRIVATE
	${QT_LIBRARIES}
	${QT_QTTEST_LIBRARY}
)
target_compile_features(DelayControlsTest PRIVATE cxx_std_20)
target_compile_definitions(DelayControlsTest PRIVATE
	LMMS_TESTING
	PLUGIN_NAME=delay
)

# Cluster 409: FFTW's planner API (creating and destroying a plan, allocating and freeing its
# buffers) must not run while the audio thread executes a plan. The convolution kernel makes and
# destroys its plans inside a ConvolutionFftwScope, which takes the engine's model-change guard -
# the guard the render thread holds for every period it renders. The plugin sources are compiled
# into this test binary (the TripleOscillatorUserWaveTest pattern) so the kernel's real plan
# lifetime runs in-process, and the scope's counters are the seam the test reads.
add_executable(ConvolutionFftwPlanScopeTest
	src/plugins/ConvolutionFftwPlanScopeTest.cpp
	${CMAKE_SOURCE_DIR}/plugins/ConvolutionReverb/ConvolutionReverb.cpp
	${CMAKE_SOURCE_DIR}/plugins/ConvolutionReverb/ConvolutionReverb.h
	${CMAKE_SOURCE_DIR}/plugins/ConvolutionReverb/ConvolutionKernel.cpp
	${CMAKE_SOURCE_DIR}/plugins/ConvolutionReverb/ConvolutionKernel.h
	${CMAKE_SOURCE_DIR}/plugins/ConvolutionReverb/ConvolutionFftw.cpp
	${CMAKE_SOURCE_DIR}/plugins/ConvolutionReverb/ConvolutionFftw.h
)
add_test(NAME ConvolutionFftwPlanScopeTest COMMAND ConvolutionFftwPlanScopeTest)
set_tests_properties(ConvolutionFftwPlanScopeTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_include_directories(ConvolutionFftwPlanScopeTest PRIVATE
	$<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>
	${CMAKE_SOURCE_DIR}/plugins/ConvolutionReverb
	${CMAKE_BINARY_DIR}/plugins/ConvolutionReverb
	${FFTW3F_INCLUDE_DIRS}
)
target_static_libraries(ConvolutionFftwPlanScopeTest PRIVATE lmmsobjs)
target_link_libraries(ConvolutionFftwPlanScopeTest PRIVATE
	${QT_LIBRARIES}
	${QT_QTTEST_LIBRARY}
	${FFTW3F_LIBRARIES}
)
target_compile_features(ConvolutionFftwPlanScopeTest PRIVATE cxx_std_20)
target_compile_definitions(ConvolutionFftwPlanScopeTest PRIVATE
	LMMS_TESTING
	PLUGIN_NAME=convolutionreverb
)

# Regression test for cluster new-p11-80 (automated LFO amount treated as
# samples vs seconds). The Delay plugin sources are compiled into the test
# binary - the WasmSandboxTest pattern - so the static and automated LFO-amount
# paths of DelayEffect::processImpl both run in-process without loading a
# plugin module, and the fix is what the comparison measures.
add_executable(DelayLfoAutomationTest
	src/plugins/DelayLfoAutomationTest.cpp
	${CMAKE_SOURCE_DIR}/plugins/Delay/DelayEffect.cpp
	${CMAKE_SOURCE_DIR}/plugins/Delay/DelayEffect.h
	${CMAKE_SOURCE_DIR}/plugins/Delay/DelayControls.cpp
	${CMAKE_SOURCE_DIR}/plugins/Delay/DelayControls.h
	${CMAKE_SOURCE_DIR}/plugins/Delay/DelayControlsDialog.cpp
	${CMAKE_SOURCE_DIR}/plugins/Delay/DelayControlsDialog.h
	${CMAKE_SOURCE_DIR}/plugins/Delay/Lfo.cpp
	${CMAKE_SOURCE_DIR}/plugins/Delay/Lfo.h
	${CMAKE_SOURCE_DIR}/plugins/Delay/StereoDelay.cpp
	${CMAKE_SOURCE_DIR}/plugins/Delay/StereoDelay.h
	# EqFader is header-only; AUTOMOC needs the header to emit its metaobject,
	# which DelayControlsDialog.cpp references (same as partc_ref_delay).
	${CMAKE_SOURCE_DIR}/plugins/Eq/EqFader.h
)
add_test(NAME DelayLfoAutomationTest COMMAND DelayLfoAutomationTest)
# Engine construction needs a Qt platform plugin on a headless machine.
set_tests_properties(DelayLfoAutomationTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_include_directories(DelayLfoAutomationTest PRIVATE
	$<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>
	${CMAKE_SOURCE_DIR}/plugins/Delay
	${CMAKE_SOURCE_DIR}/plugins/Eq
	${CMAKE_BINARY_DIR}/plugins/Delay
)
target_static_libraries(DelayLfoAutomationTest PRIVATE lmmsobjs)
target_link_libraries(DelayLfoAutomationTest PRIVATE
	${QT_LIBRARIES}
	${QT_QTTEST_LIBRARY}
)
target_compile_features(DelayLfoAutomationTest PRIVATE cxx_std_20)
target_compile_definitions(DelayLfoAutomationTest PRIVATE
	LMMS_TESTING
	PLUGIN_NAME=delay
	# Compile-in, not a module: keep the generated export macros empty so the
	# sources link into the test binary on every platform.
	PLUGIN_STATIC_DEFINE
)
add_dependencies(DelayLfoAutomationTest delay)

# Regression test for cluster new-p9-332 (the Organic plugin's oscillator
# parameters were written by the control thread while the audio thread read
# them). The Organic plugin sources are compiled into the test binary - the
# ExprSynth pattern - so the control-thread publish and the audio-thread
# adoption run in-process without loading a plugin module, and the fix is
# what the assertions measure. The header is listed so AUTOMOC emits its
# metaobject.
add_executable(OrganicParamRaceTest
	src/plugins/OrganicParamRaceTest.cpp
	${CMAKE_SOURCE_DIR}/plugins/Organic/Organic.h
)
add_test(NAME OrganicParamRaceTest COMMAND OrganicParamRaceTest)
# Engine construction needs a Qt platform plugin on a headless machine.
set_tests_properties(OrganicParamRaceTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_include_directories(OrganicParamRaceTest PRIVATE
	$<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>
	${CMAKE_SOURCE_DIR}/plugins/Organic
	${CMAKE_BINARY_DIR}/plugins/Organic
)
target_static_libraries(OrganicParamRaceTest PRIVATE lmmsobjs)
target_link_libraries(OrganicParamRaceTest PRIVATE
	${QT_LIBRARIES}
	${QT_QTTEST_LIBRARY}
)
target_compile_features(OrganicParamRaceTest PRIVATE cxx_std_20)
target_compile_definitions(OrganicParamRaceTest PRIVATE
	LMMS_TESTING
	PLUGIN_NAME=organic
	# Compile-in, not a module: keep the generated export macros empty so the
	# sources link into the test binary on every platform.
	PLUGIN_STATIC_DEFINE
)
add_dependencies(OrganicParamRaceTest organic)

# Regression test for cluster new-p16-152 (the Organic plugin dereferenced a
# null oscillator chain when its oscillator count was zero). The Organic
# plugin sources are compiled into the test binary - the ExprSynth pattern -
# so the zero-oscillator state runs in-process without loading a plugin
# module, and the fix is what the assertions measure. The header is listed so
# AUTOMOC emits its metaobject.
add_executable(OrganicZeroOscTest
	src/plugins/OrganicZeroOscTest.cpp
	${CMAKE_SOURCE_DIR}/plugins/Organic/Organic.h
)
add_test(NAME OrganicZeroOscTest COMMAND OrganicZeroOscTest)
# Engine construction needs a Qt platform plugin on a headless machine.
set_tests_properties(OrganicZeroOscTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_include_directories(OrganicZeroOscTest PRIVATE
	$<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>
	${CMAKE_SOURCE_DIR}/plugins/Organic
	${CMAKE_BINARY_DIR}/plugins/Organic
)
target_static_libraries(OrganicZeroOscTest PRIVATE lmmsobjs)
target_link_libraries(OrganicZeroOscTest PRIVATE
	${QT_LIBRARIES}
	${QT_QTTEST_LIBRARY}
)
target_compile_features(OrganicZeroOscTest PRIVATE cxx_std_20)
target_compile_definitions(OrganicZeroOscTest PRIVATE
	LMMS_TESTING
	PLUGIN_NAME=organic
	# Compile-in, not a module: keep the generated export macros empty so the
	# sources link into the test binary on every platform.
	PLUGIN_STATIC_DEFINE
)
add_dependencies(OrganicZeroOscTest organic)

# Regression test for cluster new-p9-122 (bug reports new-p9-122, new-p15-58,
# new-p16-57, new-p17-52 - all one root cause: the unsynchronized float
# peak-meter handoff between the audio thread and the GUI through EqFader,
# shared by the Eq, Delay and Compressor dialogs). The Delay plugin sources
# are compiled into the test binary - the DelayLfoAutomationTest pattern - so
# the test can drive a real DelayEffect's peak fields through the atomic
# exchange the GUI timer uses; the Eq control header gives the field-type
# checks, and the widget/audio-side access patterns are pinned in the sources
# themselves.
add_executable(EqPeakHandoffTest
	src/plugins/EqPeakHandoffTest.cpp
	${CMAKE_SOURCE_DIR}/plugins/Delay/DelayEffect.cpp
	${CMAKE_SOURCE_DIR}/plugins/Delay/DelayEffect.h
	${CMAKE_SOURCE_DIR}/plugins/Delay/DelayControls.cpp
	${CMAKE_SOURCE_DIR}/plugins/Delay/DelayControls.h
	${CMAKE_SOURCE_DIR}/plugins/Delay/DelayControlsDialog.cpp
	${CMAKE_SOURCE_DIR}/plugins/Delay/DelayControlsDialog.h
	${CMAKE_SOURCE_DIR}/plugins/Delay/Lfo.cpp
	${CMAKE_SOURCE_DIR}/plugins/Delay/Lfo.h
	${CMAKE_SOURCE_DIR}/plugins/Delay/StereoDelay.cpp
	${CMAKE_SOURCE_DIR}/plugins/Delay/StereoDelay.h
	# EqFader is header-only; AUTOMOC needs the header to emit its metaobject,
	# which DelayControlsDialog.cpp references (same as DelayLfoAutomationTest).
	${CMAKE_SOURCE_DIR}/plugins/Eq/EqFader.h
)
add_test(NAME EqPeakHandoffTest COMMAND EqPeakHandoffTest)
# Engine construction needs a Qt platform plugin on a headless machine.
set_tests_properties(EqPeakHandoffTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_include_directories(EqPeakHandoffTest PRIVATE
	$<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>
	${CMAKE_SOURCE_DIR}/plugins/Delay
	${CMAKE_SOURCE_DIR}/plugins/Eq
	${CMAKE_BINARY_DIR}/plugins/Delay
)
target_static_libraries(EqPeakHandoffTest PRIVATE lmmsobjs)
target_link_libraries(EqPeakHandoffTest PRIVATE
	${QT_LIBRARIES}
	${QT_QTTEST_LIBRARY}
)
target_compile_features(EqPeakHandoffTest PRIVATE cxx_std_20)
target_compile_definitions(EqPeakHandoffTest PRIVATE
	LMMS_TESTING
	PLUGIN_NAME=delay
	# Compile-in, not a module: keep the generated export macros empty so the
	# sources link into the test binary on every platform.
	PLUGIN_STATIC_DEFINE
)
add_dependencies(EqPeakHandoffTest delay)

if(WANT_WASM)
	# Gate tests for the WASM DSP sandbox. Kept out of LMMS_TESTS so the
	# pre-existing suite is byte-for-byte unchanged when WANT_WASM=OFF.
	# The WasmEffect plugin sources are compiled into the test binary as well
	# so registration, save/load and latency are testable without loading a
	# plugin .so (the plugin itself is still built as a module target).
	add_executable(WasmSandboxTest
		src/wasm/WasmSandboxTest.cpp
		${CMAKE_SOURCE_DIR}/plugins/WasmEffect/WasmEffect.cpp
		${CMAKE_SOURCE_DIR}/plugins/WasmEffect/WasmEffect.h
		${CMAKE_SOURCE_DIR}/plugins/WasmEffect/WasmEffectControls.cpp
		${CMAKE_SOURCE_DIR}/plugins/WasmEffect/WasmEffectControls.h
		${CMAKE_SOURCE_DIR}/plugins/WasmEffect/WasmEffectControlDialog.cpp
		${CMAKE_SOURCE_DIR}/plugins/WasmEffect/WasmEffectControlDialog.h
	)
	add_test(NAME WasmSandboxTest COMMAND WasmSandboxTest)

	# TODO CMake 3.12: Propagate usage requirements by linking to lmmsobjs
	target_include_directories(WasmSandboxTest PRIVATE
		$<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>
		${CMAKE_SOURCE_DIR}/src/wasm
		${CMAKE_SOURCE_DIR}/plugins/WasmEffect
		${CMAKE_BINARY_DIR}/plugins/WasmEffect
	)
	target_static_libraries(WasmSandboxTest PRIVATE lmmsobjs)
	target_link_libraries(WasmSandboxTest PRIVATE
		${QT_LIBRARIES}
		${QT_QTTEST_LIBRARY}
	)
	# The plugin .so resolves LMMS core symbols from the host process, exactly
	# like the real zene binary (src/CMakeLists.txt sets ENABLE_EXPORTS ON).
	# Without it p5's PluginFactory load would fail on the first core symbol.
	set_target_properties(WasmSandboxTest PROPERTIES ENABLE_EXPORTS ON)

	target_compile_features(WasmSandboxTest PRIVATE cxx_std_20)
	target_compile_definitions(WasmSandboxTest PRIVATE
		LMMS_TESTING
		PLUGIN_NAME=wasm_effect
		WASM_MODULES_DIR="${CMAKE_BINARY_DIR}/wasm-modules"
		WASM_WAT_SOURCE_DIR="${CMAKE_SOURCE_DIR}/modules/wasm"
	)
	add_dependencies(WasmSandboxTest wasm-modules)

	# #614: the conformance suite for docs/WASM-EFFECT-ABI.md. It asserts each documented
	# ABI property (and its documented limit) against modules assembled at run time from
	# .wat source with WasmSandbox::assembleWat, so it depends on no built fixture except
	# the example effect, tests/data/wasm-effect-abi/softclip.wat, which was itself
	# written from that document. Registered here, under the same WANT_WASM guard as
	# WasmSandboxTest, because it needs the wasmtime C API to link and run.
	#
	# IT HAS NOT BEEN EXECUTED. On a build without the wasmtime C API (WANT_WASM degrades
	# to OFF at CMakeLists.txt:957-963) neither this target nor WasmSandboxTest exists,
	# which is the case on the reference box; docs/KNOWN-LIMITATIONS.md records that.
	add_executable(WasmAbiConformanceTest
		src/wasm/WasmAbiConformanceTest.cpp
	)
	add_test(NAME WasmAbiConformanceTest COMMAND WasmAbiConformanceTest)
	target_include_directories(WasmAbiConformanceTest PRIVATE
		$<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>
		${CMAKE_SOURCE_DIR}/src/wasm
	)
	target_static_libraries(WasmAbiConformanceTest PRIVATE lmmsobjs)
	target_link_libraries(WasmAbiConformanceTest PRIVATE
		${QT_LIBRARIES}
		${QT_QTTEST_LIBRARY}
	)
	target_compile_features(WasmAbiConformanceTest PRIVATE cxx_std_20)
	target_compile_definitions(WasmAbiConformanceTest PRIVATE
		LMMS_TESTING
		WASM_ABI_EXAMPLE_WAT="${CMAKE_SOURCE_DIR}/tests/data/wasm-effect-abi/softclip.wat"
	)

	# CODE-5 (feature row 73): the shared worker pool, its wake-ups, and the
	# deterministic offline render - whose verdict is a MEASURED comparison
	# against this build's own run-to-run floor (docs/RENDER-DETERMINISM.md is
	# the recorded contract; byte identity and fixed thresholds are both out).
	#
	# It COMPILES the sources under test itself - the pool, the worker, the
	# offline render and the sandbox - exactly as ClapHostTest and Vst3HostTest
	# do for their hosts, instead of linking lmmsobjs: the wasm sources are Qt
	# free and reference no core symbol, so the test builds and runs without the
	# engine. (Linking lmmsobjs would drag the whole core into the test and cost
	# a full build of it.) The subject module comes from the wasm-modules target.
	add_executable(WasmWorkerPoolTest
		src/wasm/WasmWorkerPoolTest.cpp
		${CMAKE_SOURCE_DIR}/src/wasm/WasmSandbox.cpp
		${CMAKE_SOURCE_DIR}/src/wasm/WasmWorker.cpp
		${CMAKE_SOURCE_DIR}/src/wasm/WasmWorkerPool.cpp
		${CMAKE_SOURCE_DIR}/src/wasm/WasmOfflineRender.cpp
	)
	add_test(NAME WasmWorkerPoolTest COMMAND WasmWorkerPoolTest)
	target_include_directories(WasmWorkerPoolTest PRIVATE
		# the generated headers the core's own headers carry: lmms_export.h
		# (build/src) and lmmsconfig.h (build/include)
		${CMAKE_BINARY_DIR}
		${CMAKE_BINARY_DIR}/src
		${CMAKE_BINARY_DIR}/include
		${CMAKE_SOURCE_DIR}/include
		${CMAKE_SOURCE_DIR}/src/wasm
	)
	target_include_directories(WasmWorkerPoolTest SYSTEM PRIVATE ${WASMTIME_INCLUDE_DIRS})
	target_link_libraries(WasmWorkerPoolTest PRIVATE
		${QT_LIBRARIES}
		${QT_QTTEST_LIBRARY}
		${WASMTIME_LIBRARIES}
		${CMAKE_DL_LIBS}
	)
	target_compile_features(WasmWorkerPoolTest PRIVATE cxx_std_20)
	target_compile_definitions(WasmWorkerPoolTest PRIVATE
		LMMS_TESTING
		WASM_TEST_DISABLE_PARALLEL_COMPILATION
		WASM_MODULE_DIR="${CMAKE_BINARY_DIR}/wasm-modules"
	)
	add_dependencies(WasmWorkerPoolTest wasm-modules)

	# cluster new-p14-378: the fuel budget a caller asks for must bound the
	# module's STARTUP, not only its process() calls. The sandbox and the host
	# it drives are Qt free, so this test compiles those sources itself instead
	# of linking lmmsobjs - the whole core would cost a full build of it for
	# one host-side assertion.
	add_executable(WasmSandboxHostFuelTest
		src/wasm/WasmSandboxHostFuelTest.cpp
		${CMAKE_SOURCE_DIR}/src/wasm/WasmSandbox.cpp
		${CMAKE_SOURCE_DIR}/src/wasm/WasmSandboxHost.cpp
	)
	add_test(NAME WasmSandboxHostFuelTest COMMAND WasmSandboxHostFuelTest)
	target_include_directories(WasmSandboxHostFuelTest PRIVATE
		# the generated headers the core's own headers carry: lmms_export.h
		# (build/src) and lmmsconfig.h (build/include)
		${CMAKE_BINARY_DIR}
		${CMAKE_BINARY_DIR}/src
		${CMAKE_BINARY_DIR}/include
		${CMAKE_SOURCE_DIR}/include
		${CMAKE_SOURCE_DIR}/src/wasm
	)
	target_include_directories(WasmSandboxHostFuelTest SYSTEM PRIVATE ${WASMTIME_INCLUDE_DIRS})
	target_link_libraries(WasmSandboxHostFuelTest PRIVATE
		${QT_LIBRARIES}
		${QT_QTTEST_LIBRARY}
		${WASMTIME_LIBRARIES}
		${CMAKE_DL_LIBS}
	)
	target_compile_features(WasmSandboxHostFuelTest PRIVATE cxx_std_20)
	target_compile_definitions(WasmSandboxHostFuelTest PRIVATE LMMS_TESTING)
	set_tests_properties(WasmSandboxHostFuelTest PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

	# Drive the public command and inspect its JSON result with a module that emits NaN.
	add_executable(ControlWasmRenderTest
		src/core/ControlWasmRenderTest.cpp
	)
	add_test(NAME ControlWasmRenderTest COMMAND ControlWasmRenderTest)
	target_include_directories(ControlWasmRenderTest PRIVATE
		$<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>
		${CMAKE_SOURCE_DIR}/src/wasm
	)
	target_static_libraries(ControlWasmRenderTest PRIVATE lmmsobjs)
	target_link_libraries(ControlWasmRenderTest PRIVATE
		${QT_LIBRARIES}
		${QT_QTTEST_LIBRARY}
	)
	target_compile_features(ControlWasmRenderTest PRIVATE cxx_std_20)
	target_compile_definitions(ControlWasmRenderTest PRIVATE LMMS_TESTING)
endif()

if(TARGET nam_profile)
	add_executable(NeuralAmpProfileArgumentsTest src/plugins/NeuralAmpProfileArgumentsTest.cpp)
	add_test(NAME NeuralAmpProfileArgumentsTest COMMAND NeuralAmpProfileArgumentsTest)
	target_compile_features(NeuralAmpProfileArgumentsTest PRIVATE cxx_std_20)
	target_link_libraries(NeuralAmpProfileArgumentsTest PRIVATE ${QT_LIBRARIES} ${QT_QTTEST_LIBRARY})
	target_compile_definitions(NeuralAmpProfileArgumentsTest PRIVATE
		"NAM_PROFILE_EXECUTABLE=\"$<TARGET_FILE:nam_profile>\"")
	add_dependencies(NeuralAmpProfileArgumentsTest nam_profile)
endif()

# rt_alloc_probe exists only on Linux (ld --wrap); its argument checks guard the real-time
# measurement, so a malformed block size must be refused rather than reported as "no allocation".
if(TARGET rt_alloc_probe)
	add_executable(NeuralAmpAllocProbeArgumentsTest src/plugins/NeuralAmpAllocProbeArgumentsTest.cpp)
	add_test(NAME NeuralAmpAllocProbeArgumentsTest COMMAND NeuralAmpAllocProbeArgumentsTest)
	target_compile_features(NeuralAmpAllocProbeArgumentsTest PRIVATE cxx_std_20)
	target_link_libraries(NeuralAmpAllocProbeArgumentsTest PRIVATE ${QT_LIBRARIES} ${QT_QTTEST_LIBRARY})
	target_compile_definitions(NeuralAmpAllocProbeArgumentsTest PRIVATE
		"NAM_ALLOC_PROBE_EXECUTABLE=\"$<TARGET_FILE:rt_alloc_probe>\"")
	add_dependencies(NeuralAmpAllocProbeArgumentsTest rt_alloc_probe)
endif()

# Native CLAP hosting tests. They only exist when the MIT licensed CLAP headers
# (>= 1.2.10) are available; see cmake/modules/ClapHeaders.cmake. The port map
# is pure data transformation, so it is tested without loading a plug-in.
if(TARGET lmms_clap)
	set(LMMS_CLAP_EFFECT_DIR ${CMAKE_SOURCE_DIR}/plugins/ClapEffect)

	# The test subject: a real CLAP stereo gain effect built from the pinned
	# headers (tests/data/clap-test-plugin/clap-test-gain.c).
	add_subdirectory(data/clap-test-plugin)

	add_executable(ClapBusMapTest
		src/plugins/ClapBusMapTest.cpp
		${LMMS_CLAP_EFFECT_DIR}/ClapBusMap.cpp
	)
	add_test(NAME ClapBusMapTest COMMAND ClapBusMapTest)
	target_include_directories(ClapBusMapTest PRIVATE
		${CMAKE_BINARY_DIR}
		${CMAKE_SOURCE_DIR}/include
		${LMMS_CLAP_EFFECT_DIR}
	)
	target_compile_features(ClapBusMapTest PRIVATE cxx_std_20)
	target_link_libraries(ClapBusMapTest PRIVATE ${QT_LIBRARIES} ${QT_QTTEST_LIBRARY})

	add_executable(ClapHostTest
		src/plugins/ClapHostTest.cpp
		${LMMS_CLAP_EFFECT_DIR}/ClapBusMap.cpp
		${LMMS_CLAP_EFFECT_DIR}/ClapHost.cpp
		# R4.1: the editor run loop the host answers posix-fd/timer-support with
		${CMAKE_SOURCE_DIR}/src/core/PluginEditorHost.cpp
		${LMMS_CLAP_EFFECT_DIR}/ClapHostLifecycle.cpp
		${LMMS_CLAP_EFFECT_DIR}/ClapHostNotes.cpp
		${LMMS_CLAP_EFFECT_DIR}/ClapHostParams.cpp
		${LMMS_CLAP_EFFECT_DIR}/ClapHostGui.cpp
		${LMMS_CLAP_EFFECT_DIR}/ClapLoader.cpp
		# the host path's chunking counters (CODE-4), core-owned
		${CMAKE_SOURCE_DIR}/src/core/PluginHostChunking.cpp
		# the note-path counters and ports (feature row 79, board task #669),
		# core-owned for the same reason: the host writes into them.
		${CMAKE_SOURCE_DIR}/src/core/PluginHostNotes.cpp
	)
	add_test(NAME ClapHostTest COMMAND ClapHostTest)
	# Compiles core sources directly (built into THIS test, not linked as a
	# library): its own compilation must see the export header's static branch -
	# STATIC_COMPILE_DEFINITIONS only reaches targets that LINK the library.
	target_compile_definitions(ClapHostTest PRIVATE LMMS_STATIC_DEFINE)
	target_include_directories(ClapHostTest PRIVATE
		${CMAKE_BINARY_DIR}
		# the generated lmms_export.h, which the core headers carry
		${CMAKE_BINARY_DIR}/src
		${CMAKE_SOURCE_DIR}/include
		${LMMS_CLAP_EFFECT_DIR}
	)
	target_compile_features(ClapHostTest PRIVATE cxx_std_20)
	target_link_libraries(ClapHostTest PRIVATE
		${QT_LIBRARIES}
		${QT_QTTEST_LIBRARY}
		lmms_clap
		${CMAKE_DL_LIBS}
	)
	target_compile_definitions(ClapHostTest PRIVATE
		CLAP_TEST_PLUGIN_PATH="$<TARGET_FILE:clap-test-gain>"
		# The instrument subject of the note-path case (feature row 79).
		CLAP_TEST_INSTRUMENT_PATH="$<TARGET_FILE:clap-test-instrument>")
	add_dependencies(ClapHostTest clap-test-gain clap-test-instrument)

	# White-box checks for the instrument fixture's state and process callbacks.
	add_executable(ClapTestInstrumentUnitTest src/plugins/ClapTestInstrumentUnitTest.cpp)
	add_test(NAME ClapTestInstrumentStateTest COMMAND ClapTestInstrumentUnitTest state)
	add_test(NAME ClapTestInstrumentEventCountTest COMMAND ClapTestInstrumentUnitTest events)
	add_test(NAME ClapTestInstrumentActivationCountersTest COMMAND ClapTestInstrumentUnitTest activation)
	target_include_directories(ClapTestInstrumentUnitTest PRIVATE
		${CMAKE_SOURCE_DIR}/tests/data/clap-test-plugin
	)
	target_compile_features(ClapTestInstrumentUnitTest PRIVATE cxx_std_20)
	target_link_libraries(ClapTestInstrumentUnitTest PRIVATE lmms_clap)
	add_dependencies(ClapTestInstrumentUnitTest clap-test-instrument)

	# The TYPED error path of the module loader (ClapLoader.h). Subjects: the
	# five one-fault modules built above -- each fails in exactly one way, so a
	# wrong code is a host defect and not a fixture artefact -- plus
	# clap-test-gain as the control that says the typed path does not fire on a
	# module that is fine. Nothing here needs an audio device or the engine.
	#
	# WHERE IT RUNS (checked against .github/workflows/build.yml, not assumed).
	# Every job that provisions the CLAP headers builds it; of the three Windows
	# jobs, msvc-x64 is the one that runs `ctest`, so that is the job where the
	# typed path is exercised on Windows -- against the Windows half of the
	# loader (LoadLibraryW/GetProcAddress). mingw64 and windows-arm64 configure
	# with -DWANT_CLAP=ON and build the module, but run no ctest, so for them the
	# proof is that it compiles and that the release-honesty guard finds the
	# clapeffect module in their artifacts. On Linux and macOS it runs with the
	# rest of the suite.
	add_executable(ClapLoaderErrorTest
		src/plugins/ClapLoaderErrorTest.cpp
		${LMMS_CLAP_EFFECT_DIR}/ClapBusMap.cpp
		${LMMS_CLAP_EFFECT_DIR}/ClapHost.cpp
		# R4.1: the editor run loop the host answers posix-fd/timer-support with
		${CMAKE_SOURCE_DIR}/src/core/PluginEditorHost.cpp
		${LMMS_CLAP_EFFECT_DIR}/ClapHostLifecycle.cpp
		${LMMS_CLAP_EFFECT_DIR}/ClapHostNotes.cpp
		${LMMS_CLAP_EFFECT_DIR}/ClapHostParams.cpp
		${LMMS_CLAP_EFFECT_DIR}/ClapHostGui.cpp
		${LMMS_CLAP_EFFECT_DIR}/ClapLoader.cpp
		${CMAKE_SOURCE_DIR}/src/core/PluginHostChunking.cpp
		# the note-path counters and ports (feature row 79, board task #669),
		# core-owned for the same reason: the host writes into them.
		${CMAKE_SOURCE_DIR}/src/core/PluginHostNotes.cpp
	)
	add_test(NAME ClapLoaderErrorTest COMMAND ClapLoaderErrorTest)
	# Compiles core sources directly: the define must apply to THIS target's own
	# compilation - STATIC_COMPILE_DEFINITIONS only reaches linking targets.
	target_compile_definitions(ClapLoaderErrorTest PRIVATE LMMS_STATIC_DEFINE)
	target_include_directories(ClapLoaderErrorTest PRIVATE
		${CMAKE_BINARY_DIR}
		${CMAKE_BINARY_DIR}/src
		${CMAKE_SOURCE_DIR}/include
		${LMMS_CLAP_EFFECT_DIR}
	)
	target_compile_features(ClapLoaderErrorTest PRIVATE cxx_std_20)
	target_link_libraries(ClapLoaderErrorTest PRIVATE
		${QT_LIBRARIES}
		${QT_QTTEST_LIBRARY}
		lmms_clap
		${CMAKE_DL_LIBS}
	)
	target_compile_definitions(ClapLoaderErrorTest PRIVATE
		CLAP_TEST_PLUGIN_PATH="$<TARGET_FILE:clap-test-gain>"
		CLAP_TEST_BROKEN_NO_SYMBOL_PATH="$<TARGET_FILE:clap-test-broken-no-symbol>"
		CLAP_TEST_BROKEN_OLD_VERSION_PATH="$<TARGET_FILE:clap-test-broken-old-version>"
		CLAP_TEST_BROKEN_INIT_FAILS_PATH="$<TARGET_FILE:clap-test-broken-init-fails>"
		CLAP_TEST_BROKEN_NO_FACTORY_PATH="$<TARGET_FILE:clap-test-broken-no-factory>"
		CLAP_TEST_BROKEN_NULL_CALLBACK_PATH="$<TARGET_FILE:clap-test-broken-null-callback>")
	add_dependencies(ClapLoaderErrorTest clap-test-gain clap-test-broken-no-symbol
		clap-test-broken-old-version clap-test-broken-init-fails clap-test-broken-no-factory
		clap-test-broken-null-callback)

	add_executable(ClapEffectIntegrationTest
		src/plugins/ClapEffectIntegrationTest.cpp
		${LMMS_CLAP_EFFECT_DIR}/ClapBusMap.cpp
		${LMMS_CLAP_EFFECT_DIR}/ClapEffect.cpp
		${LMMS_CLAP_EFFECT_DIR}/ClapEffectControlDialog.cpp
		${LMMS_CLAP_EFFECT_DIR}/ClapEffectControls.cpp
		${LMMS_CLAP_EFFECT_DIR}/ClapHost.cpp
		${LMMS_CLAP_EFFECT_DIR}/ClapHostLifecycle.cpp
		${LMMS_CLAP_EFFECT_DIR}/ClapHostNotes.cpp
		${LMMS_CLAP_EFFECT_DIR}/ClapHostParams.cpp
		${LMMS_CLAP_EFFECT_DIR}/ClapHostGui.cpp
		${LMMS_CLAP_EFFECT_DIR}/ClapLoader.cpp
		${LMMS_CLAP_EFFECT_DIR}/ClapParameter.cpp
		${LMMS_CLAP_EFFECT_DIR}/ClapSubPluginFeatures.cpp
	)
	add_test(NAME ClapEffectIntegrationTest COMMAND ClapEffectIntegrationTest)
	# R4.4: it opens the plug-in's editor window, so it needs a Qt platform even headless.
	set_tests_properties(ClapEffectIntegrationTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
	target_include_directories(ClapEffectIntegrationTest PRIVATE
		# generated plugin_export.h (PLUGIN_EXPORT) of the clapeffect plug-in
		${CMAKE_BINARY_DIR}/plugins/ClapEffect
		$<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>
		${LMMS_CLAP_EFFECT_DIR}
	)
	# the test compiles the plug-in sources itself, so it needs the generated
	# export header of the plug-in target to exist first
	add_dependencies(ClapEffectIntegrationTest clapeffect)
	target_static_libraries(ClapEffectIntegrationTest PRIVATE lmmsobjs)
	target_compile_features(ClapEffectIntegrationTest PRIVATE cxx_std_20)
	target_link_libraries(ClapEffectIntegrationTest PRIVATE
		${QT_LIBRARIES}
		${QT_QTTEST_LIBRARY}
		lmms_clap
		${CMAKE_DL_LIBS}
	)
	target_compile_definitions(ClapEffectIntegrationTest PRIVATE
		LMMS_TESTING
		# Same construct as the VST3 twin below - this target compiles the
		# plug-in's own ClapEffect.cpp into the test executable, and that TU
		# defines clapeffect_plugin_descriptor with PLUGIN_EXPORT, so the
		# plug-in's generated plugin_export.h must not mark it dllimport. Latent
		# on msvc-x64 today only because that job configures -DWANT_CLAP=OFF
		# (build.yml); fixed here so the twins stay twins. The C2491 evidence is
		# in the VST3 block.
		PLUGIN_STATIC_DEFINE
		PLUGIN_NAME=clapeffect
		CLAP_TEST_PLUGIN_PATH="$<TARGET_FILE:clap-test-gain>"
		# The module that loads but exports no clap_entry, for the scan-diagnostics
		# case: the browser scan has to say WHY a module did not appear.
		CLAP_TEST_BROKEN_NO_SYMBOL_PATH="$<TARGET_FILE:clap-test-broken-no-symbol>")
	add_dependencies(ClapEffectIntegrationTest clapeffect clap-test-broken-no-symbol)

	# The instrument VIEW (Bugs cluster new-p10-3): the audio-output channel-count label must follow
	# a runtime clap.audio-ports layout change, exercised on the fixture's CLAP_TEST_INSTRUMENT_LAYOUT
	# mode. The test compiles the instrument module the way ClapEffectIntegrationTest compiles the
	# effect one, so it drives the class directly and needs no running transport.
	add_executable(ClapInstrumentViewTest
		src/plugins/ClapInstrumentViewTest.cpp
		${CMAKE_SOURCE_DIR}/plugins/ClapInstrument/ClapInstrument.cpp
		${CMAKE_SOURCE_DIR}/plugins/ClapInstrument/ClapInstrumentView.cpp
		${LMMS_CLAP_EFFECT_DIR}/ClapBusMap.cpp
		${LMMS_CLAP_EFFECT_DIR}/ClapHost.cpp
		${LMMS_CLAP_EFFECT_DIR}/ClapHostLifecycle.cpp
		${LMMS_CLAP_EFFECT_DIR}/ClapHostNotes.cpp
		${LMMS_CLAP_EFFECT_DIR}/ClapHostParams.cpp
		${LMMS_CLAP_EFFECT_DIR}/ClapHostGui.cpp
		${LMMS_CLAP_EFFECT_DIR}/ClapLoader.cpp
		${LMMS_CLAP_EFFECT_DIR}/ClapParameter.cpp
		${LMMS_CLAP_EFFECT_DIR}/ClapSubPluginFeatures.cpp
	)
	add_test(NAME ClapInstrumentViewTest COMMAND ClapInstrumentViewTest)
	# It builds a real instrument view, so it needs a Qt platform even headless.
	set_tests_properties(ClapInstrumentViewTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
	target_include_directories(ClapInstrumentViewTest PRIVATE
		# generated plugin_export.h (PLUGIN_EXPORT) of the clapinstrument plug-in
		${CMAKE_BINARY_DIR}/plugins/ClapInstrument
		${CMAKE_SOURCE_DIR}/plugins/ClapInstrument
		$<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>
		${LMMS_CLAP_EFFECT_DIR}
	)
	# the test compiles the plug-in sources itself, so it needs the generated
	# export header of the plug-in target to exist first
	add_dependencies(ClapInstrumentViewTest clapinstrument clap-test-instrument)
	target_static_libraries(ClapInstrumentViewTest PRIVATE lmmsobjs)
	target_compile_features(ClapInstrumentViewTest PRIVATE cxx_std_20)
	target_link_libraries(ClapInstrumentViewTest PRIVATE
		${QT_LIBRARIES}
		${QT_QTTEST_LIBRARY}
		lmms_clap
		${CMAKE_DL_LIBS}
	)
	target_compile_definitions(ClapInstrumentViewTest PRIVATE
		LMMS_TESTING
		# This target compiles the plug-in's own ClapInstrument.cpp, which defines
		# clapinstrument_plugin_descriptor with PLUGIN_EXPORT: keep it from being
		# marked dllimport (the same construct as ClapEffectIntegrationTest).
		PLUGIN_STATIC_DEFINE
		PLUGIN_NAME=clapinstrument
		CLAP_TEST_INSTRUMENT_PATH="$<TARGET_FILE:clap-test-instrument>"
	)

	# The CLAP instrument half on the control surface (feature row 79, board
	# task #669). The catalogue case points the product's plug-in search
	# directory at the directory holding clap-test-instrument.clap, so
	# plugin.list enumerates a real CLAP instrument class and plugin.load loads
	# it onto an instrument track through the registry - the same shape the VST3
	# half gets below (VST3_TEST_INSTRUMENT_DIR), and for the same reason: the
	# acceptance contract (CHARTER 3.1) counts a feature only when it can be
	# driven through the socket. The CLAP fixture needs no opt-in option: it is
	# built from the pinned headers, not from a third-party SDK checkout.
	if(TARGET clap-test-instrument AND TARGET clapinstrument AND TARGET ControlDeviceCatalogueTest)
		target_compile_definitions(ControlDeviceCatalogueTest PRIVATE
			CLAP_TEST_INSTRUMENT_DIR="$<TARGET_FILE_DIR:clap-test-instrument>"
		)
		add_dependencies(ControlDeviceCatalogueTest clap-test-instrument clapinstrument)
	endif()
	# The CLAP EFFECT half: plugin.list carries clap-test-gain and plugin.load hosts it in clapeffect.
	if(TARGET clap-test-gain AND TARGET clapeffect AND TARGET ControlDeviceCatalogueTest)
		target_compile_definitions(ControlDeviceCatalogueTest PRIVATE
			CLAP_TEST_EFFECT_DIR="$<TARGET_FILE_DIR:clap-test-gain>")
		add_dependencies(ControlDeviceCatalogueTest clap-test-gain clapeffect)
	endif()
	# MX-114 (plugin.output_*): the fixture's multi-output class is the instrument with output buses.
	if(TARGET clap-test-instrument AND TARGET InstrumentLayersCommandsTest)
		target_compile_definitions(InstrumentLayersCommandsTest PRIVATE
			CLAP_TEST_INSTRUMENT_DIR="$<TARGET_FILE_DIR:clap-test-instrument>")
		add_dependencies(InstrumentLayersCommandsTest clap-test-instrument)
	endif()
	# MX-157: the CLAP fixture's latent mode (CLAP_TEST_INSTRUMENT_LATENCY) is the CLAP subject.
	# The host module is guarded separately (WANT_CLAP=OFF or missing headers remove it), but when
	# it exists the dependency must name it too: a targeted build of the test otherwise builds the
	# fixture alone and the CLAP cases skip with "this build has no clapinstrument module".
	if(TARGET clap-test-instrument AND TARGET InstrumentLatencyPdcTest)
		target_compile_definitions(InstrumentLatencyPdcTest PRIVATE
			CLAP_TEST_INSTRUMENT_DIR="$<TARGET_FILE_DIR:clap-test-instrument>")
		add_dependencies(InstrumentLatencyPdcTest clap-test-instrument)
		if(TARGET clapinstrument)
			add_dependencies(InstrumentLatencyPdcTest clapinstrument)
		endif()
	endif()
	# The F2 interface's output-bus surface drives the same multi-output class.
	if(TARGET clap-test-instrument AND TARGET InstrumentSetupUiTest)
		target_compile_definitions(InstrumentSetupUiTest PRIVATE
			CLAP_TEST_INSTRUMENT_DIR="$<TARGET_FILE_DIR:clap-test-instrument>")
		add_dependencies(InstrumentSetupUiTest clap-test-instrument)
	endif()
endif()

# Native VST3 hosting tests — the twins of the CLAP block above, and the reason
# tests/src/plugins/Vst3{BusMap,Host,EffectIntegration}Test.cpp exist.
#
# Until 0.2.1 these three compiled NOWHERE: their CLAP twins were registered and
# green while the VST3 three sat in the tree, and tests/unregistered-tests-gate.sh
# (Gate 10) carried them in its DECLARED table as "an open item, NOT a deliberate
# exclusion" — a test that never builds is invisible to every other gate (it is
# not covered by the coverage ratchet and not compiled by the complexity
# ratchet). This registers them exactly as the CLAP three are registered.
#
# Guarded on TARGET lmms_vst3_sdk, the target cmake/modules/Vst3Sdk.cmake creates
# only when the pinned MIT SDK checkout is present (WANT_VST3=AUTO skips without
# it, WANT_VST3=OFF disables it, Vst3Sdk.cmake FATAL_ERRORs when ON and absent).
# So a configure with no SDK skips these tests instead of failing, which is the
# same shape as the CLAP block's `if(TARGET lmms_clap)`.
#
# The subject is the SDK's own `again_sampleaccurate` gain effect, built as a
# .vst3 bundle from the same pinned checkout by
# tests/data/vst3-test-effect/CMakeLists.txt.
if(TARGET lmms_vst3_sdk)
	set(LMMS_VST3_EFFECT_DIR ${CMAKE_SOURCE_DIR}/plugins/Vst3Effect)

	# The test subject: a real VST3 stereo gain effect (two parameters, Bypass
	# and Gain) built from the pinned SDK's own sample.
	add_subdirectory(data/vst3-test-effect)

	add_executable(Vst3BusMapTest
		src/plugins/Vst3BusMapTest.cpp
		${LMMS_VST3_EFFECT_DIR}/Vst3BusMap.cpp
	)
	add_test(NAME Vst3BusMapTest COMMAND Vst3BusMapTest)
	target_include_directories(Vst3BusMapTest PRIVATE
		${CMAKE_BINARY_DIR}
		${CMAKE_SOURCE_DIR}/include
		${LMMS_VST3_EFFECT_DIR}
	)
	target_compile_features(Vst3BusMapTest PRIVATE cxx_std_20)
	target_link_libraries(Vst3BusMapTest PRIVATE
		${QT_LIBRARIES}
		${QT_QTTEST_LIBRARY}
		lmms_vst3_sdk
	)

	# Host-level proof: it compiles the shipped host sources itself, exactly as
	# ClapHostTest does for the CLAP host, and loads the fixture bundle.
	add_executable(Vst3HostTest
		src/plugins/Vst3HostTest.cpp
		${LMMS_VST3_EFFECT_DIR}/Vst3BusMap.cpp
		${LMMS_VST3_EFFECT_DIR}/Vst3Host.cpp
		${LMMS_VST3_EFFECT_DIR}/Vst3HostReports.cpp
		# R4.2: the editor session the host owns, and the R4.1 run loop it answers IRunLoop with
		${LMMS_VST3_EFFECT_DIR}/Vst3EditorSession.cpp
		${CMAKE_SOURCE_DIR}/src/core/PluginEditorHost.cpp
		${LMMS_VST3_EFFECT_DIR}/Vst3MidiEvent.cpp
		${LMMS_VST3_EFFECT_DIR}/Vst3MidiMapping.cpp
		# the host path's chunking counters (CODE-4), core-owned
		${CMAKE_SOURCE_DIR}/src/core/PluginHostChunking.cpp
	)
	add_test(NAME Vst3HostTest COMMAND Vst3HostTest)
	# Compiles core sources directly (built into THIS test, not linked as a
	# library): its own compilation must see the export header's static branch -
	# STATIC_COMPILE_DEFINITIONS only reaches targets that LINK the library.
	target_compile_definitions(Vst3HostTest PRIVATE LMMS_STATIC_DEFINE)
	target_include_directories(Vst3HostTest PRIVATE
		${CMAKE_BINARY_DIR}
		# the generated lmms_export.h, which the core headers carry
		${CMAKE_BINARY_DIR}/src
		${CMAKE_SOURCE_DIR}/include
		${LMMS_VST3_EFFECT_DIR}
	)
	target_compile_features(Vst3HostTest PRIVATE cxx_std_20)
	target_link_libraries(Vst3HostTest PRIVATE
		${QT_LIBRARIES}
		${QT_QTTEST_LIBRARY}
		lmms_vst3_sdk
		${CMAKE_DL_LIBS}
	)
	lmms_link_allocation_probe(Vst3HostTest "${CMAKE_CURRENT_SOURCE_DIR}/src/plugins/Vst3HostTest.cpp")
	target_compile_definitions(Vst3HostTest PRIVATE
		VST3_TEST_PLUGIN_PATH="${LMMS_VST3_TEST_EFFECT_BUNDLE_DIR}")
	add_dependencies(Vst3HostTest vst3-test-effect)

	# CODE-4: the chunking contract on the audio path, proven against a
	# purpose-built in-tree MIT fixture (tests/data/vst3-chunk-probe) that
	# records the block sizes the host asked it for and any call that exceeded
	# the block size setupProcessing() declared. The SDK's sample plugin is the
	# wrong subject for this: its identity and parameter shape are asserted by
	# Vst3HostTest, and it cannot report what it was asked for.
	add_subdirectory(data/vst3-chunk-probe)

	add_executable(Vst3ChunkProbeTest
		src/plugins/Vst3ChunkProbeTest.cpp
		${LMMS_VST3_EFFECT_DIR}/Vst3BusMap.cpp
		${LMMS_VST3_EFFECT_DIR}/Vst3Host.cpp
		${LMMS_VST3_EFFECT_DIR}/Vst3HostReports.cpp
		# R4.2: the editor session the host owns, and the R4.1 run loop it answers IRunLoop with
		${LMMS_VST3_EFFECT_DIR}/Vst3EditorSession.cpp
		${CMAKE_SOURCE_DIR}/src/core/PluginEditorHost.cpp
		${LMMS_VST3_EFFECT_DIR}/Vst3MidiEvent.cpp
		${LMMS_VST3_EFFECT_DIR}/Vst3MidiMapping.cpp
		# the host path's chunking counters (CODE-4), core-owned
		${CMAKE_SOURCE_DIR}/src/core/PluginHostChunking.cpp
	)
	add_test(NAME Vst3ChunkProbeTest COMMAND Vst3ChunkProbeTest)
	# Compiles core sources directly: the define must apply to THIS target's own
	# compilation - STATIC_COMPILE_DEFINITIONS only reaches linking targets.
	target_compile_definitions(Vst3ChunkProbeTest PRIVATE LMMS_STATIC_DEFINE)
	target_include_directories(Vst3ChunkProbeTest PRIVATE
		${CMAKE_BINARY_DIR}
		# the generated lmms_export.h, which the core headers carry
		${CMAKE_BINARY_DIR}/src
		${CMAKE_SOURCE_DIR}/include
		${LMMS_VST3_EFFECT_DIR}
	)
	target_compile_features(Vst3ChunkProbeTest PRIVATE cxx_std_20)
	target_link_libraries(Vst3ChunkProbeTest PRIVATE
		${QT_LIBRARIES}
		${QT_QTTEST_LIBRARY}
		lmms_vst3_sdk
		${CMAKE_DL_LIBS}
	)
	target_compile_definitions(Vst3ChunkProbeTest PRIVATE
		VST3_CHUNK_PROBE_PATH="${LMMS_VST3_CHUNK_PROBE_BUNDLE_DIR}")
	add_dependencies(Vst3ChunkProbeTest vst3-chunk-probe)

	# Product-level proof: the effect is loaded into an EffectChain on an
	# AudioBus and driven through the engine, its state round-trips through the
	# .mmp, and the renders before/after are written. It compiles the plug-in's
	# own sources (as ClapEffectIntegrationTest does for the CLAP effect) so that
	# the descriptor and controls under test are the shipped ones.
	add_executable(Vst3EffectIntegrationTest
		src/plugins/Vst3EffectIntegrationTest.cpp
		${LMMS_VST3_EFFECT_DIR}/Vst3BusMap.cpp
		${LMMS_VST3_EFFECT_DIR}/Vst3Effect.cpp
		${LMMS_VST3_EFFECT_DIR}/Vst3EffectControlDialog.cpp
		${LMMS_VST3_EFFECT_DIR}/Vst3EffectControls.cpp
		${LMMS_VST3_EFFECT_DIR}/Vst3Host.cpp
		${LMMS_VST3_EFFECT_DIR}/Vst3HostReports.cpp
		# R4.2: the editor session the host owns (the R4.1 run loop comes with lmmsobjs)
		${LMMS_VST3_EFFECT_DIR}/Vst3EditorSession.cpp
		${LMMS_VST3_EFFECT_DIR}/Vst3MidiEvent.cpp
		${LMMS_VST3_EFFECT_DIR}/Vst3MidiMapping.cpp
		${LMMS_VST3_EFFECT_DIR}/Vst3Parameter.cpp
		${LMMS_VST3_EFFECT_DIR}/Vst3SubPluginFeatures.cpp
	)
	add_test(NAME Vst3EffectIntegrationTest COMMAND Vst3EffectIntegrationTest)
	target_include_directories(Vst3EffectIntegrationTest PRIVATE
		# generated plugin_export.h (PLUGIN_EXPORT) of the vst3effect plug-in
		${CMAKE_BINARY_DIR}/plugins/Vst3Effect
		$<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>
		${LMMS_VST3_EFFECT_DIR}
	)
	# the test compiles the plug-in sources itself, so it needs the generated
	# export header of the plug-in target to exist first
	add_dependencies(Vst3EffectIntegrationTest vst3effect)
	target_static_libraries(Vst3EffectIntegrationTest PRIVATE lmmsobjs)
	target_compile_features(Vst3EffectIntegrationTest PRIVATE cxx_std_20)
	target_link_libraries(Vst3EffectIntegrationTest PRIVATE
		${QT_LIBRARIES}
		${QT_QTTEST_LIBRARY}
		lmms_vst3_sdk
		${CMAKE_DL_LIBS}
	)
	target_compile_definitions(Vst3EffectIntegrationTest PRIVATE
		LMMS_TESTING
		# This target compiles the plug-in's own sources into the test executable,
		# Vst3Effect.cpp included - and that TU *defines*
		# vst3effect_plugin_descriptor with PLUGIN_EXPORT. For a consumer, the
		# generated plugin_export.h of the vst3effect target resolves
		# PLUGIN_EXPORT to __declspec(dllimport) (GENERATE_EXPORT_HEADER in
		# cmake/modules/BuildPlugin.cmake), and defining a dllimport symbol is
		# "error C2491: definition of dllimport data not allowed" - msvc-x64 job
		# 103706773250 (run 34750729517) died at plugins\Vst3Effect\Vst3Effect.cpp(45)
		# with exactly that. The static branch of that same header is the correct
		# one here: this binary links the plug-in's code in, it does not import it.
		# (LMMS_STATIC_DEFINE, which target_static_libraries already passes down to
		# these targets, is the same idea for lmms' own export header.)
		PLUGIN_STATIC_DEFINE
		PLUGIN_NAME=vst3effect
		VST3_TEST_PLUGIN_PATH="${LMMS_VST3_TEST_EFFECT_BUNDLE_DIR}"
	)
	add_dependencies(Vst3EffectIntegrationTest vst3-test-effect)
endif()

# Instrument the plugin/gate test executables as well - they compile fork
# sources directly, so their objects carry the coverage the ratchet tracks.
# PluginPortsMigrationReference is deliberately left un-instrumented: the
# reference side of the behaviour comparison must stay plain.
foreach(LMMS_GATE_TEST ClapBusMapTest ClapHostTest ClapLoaderErrorTest ClapEffectIntegrationTest
		Vst3BusMapTest Vst3HostTest Vst3EffectIntegrationTest Vst3ChunkProbeTest
		WasmSandboxTest PluginPortsMigrationTest AudioPluginTest
		synthetic_audio_plugin)
	if(TARGET ${LMMS_GATE_TEST})
		lmms_add_coverage(${LMMS_GATE_TEST})
	endif()
endforeach()

# ---------------------------------------------------------------------------
# The per-plugin "run in a separate process" toggle (post-alpha/oop-hosting).
#
# ZynAddSubFx is the one plugin family in this tree that ships both an
# in-process implementation (LocalZynAddSubFx) and a client that runs as a
# separate process (RemoteZynAddSubFx), so the opt-in toggle lives there. The
# test loads the real plugin module and drives the setting the project file
# carries: the default (no attribute) must stay in-process, the stored choice
# must bring the client up as a child process of the host, the choice must
# round-trip through the plugin's own saveSettings/loadSettings, and killing
# the client must not take the host down.
# ---------------------------------------------------------------------------
if(TARGET zynaddsubfx AND TARGET RemoteZynAddSubFx)
	add_executable(ZynSeparateProcessTest src/plugins/ZynSeparateProcessTest.cpp)
	add_test(NAME ZynSeparateProcessTest COMMAND ZynSeparateProcessTest)
	target_include_directories(ZynSeparateProcessTest PRIVATE
		$<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>
	)
	target_static_libraries(ZynSeparateProcessTest PRIVATE lmmsobjs)
	target_link_libraries(ZynSeparateProcessTest PRIVATE
		${QT_LIBRARIES}
		${QT_QTTEST_LIBRARY}
	)
	target_compile_features(ZynSeparateProcessTest PRIVATE cxx_std_20)
	target_compile_definitions(ZynSeparateProcessTest PRIVATE
		LMMS_TESTING
		ZYN_PLUGIN_PATH="$<TARGET_FILE:zynaddsubfx>"
		ZYN_REMOTE_CLIENT_PATH="$<TARGET_FILE:RemoteZynAddSubFx>"
	)
	# The plugin module's unresolved lmms symbols are bound from the loading
	# process, which is why the host has to export them (same as AudioPluginTest).
	set_target_properties(ZynSeparateProcessTest PROPERTIES ENABLE_EXPORTS ON)
	add_dependencies(ZynSeparateProcessTest zynaddsubfx RemoteZynAddSubFx)
	set_tests_properties(ZynSeparateProcessTest PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

# The local Zyn core's MIDI note counter must not underflow on an unmatched
# note-off; exercise processMidiEvent directly without starting the audio engine.
if(TARGET ZynAddSubFxCore)
	add_executable(ZynNoteOffTest src/plugins/ZynNoteOffTest.cpp)
	target_include_directories(ZynNoteOffTest PRIVATE
		$<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>
		${CMAKE_SOURCE_DIR}/plugins/ZynAddSubFx
	)
	target_static_libraries(ZynNoteOffTest PRIVATE ZynAddSubFxCore lmmsobjs)
	target_link_libraries(ZynNoteOffTest PRIVATE
		${QT_LIBRARIES}
		${QT_QTTEST_LIBRARY}
	)
	target_compile_features(ZynNoteOffTest PRIVATE cxx_std_20)
	add_test(NAME ZynNoteOffTest COMMAND ZynNoteOffTest)
	set_tests_properties(ZynNoteOffTest PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

# ---------------------------------------------------------------------------
# The Zyn instrument's audio path and its plugin lock (cluster 908).
#
# The instrument keeps its plugin instance behind a mutex that also guards the
# instance's life. Three defects live around that mutex: the audio path could
# WAIT for it (an export could stall for as long as a reload took), the preset
# load chose a plugin and used it without holding the lock (a hosting-mode
# switch could delete the instance in between), and the MIDI hand-off dropped
# the event's frame offset.
#
# The test compiles the plug-in's own ZynAddSubFx.cpp - the way the VST and CLAP
# tests compile theirs - so it can drive the class directly.
# ---------------------------------------------------------------------------
if(TARGET zynaddsubfx AND TARGET ZynAddSubFxCore)
	add_executable(ZynAddSubFxInstrumentTest
		src/plugins/ZynAddSubFxInstrumentTest.cpp
		${CMAKE_SOURCE_DIR}/plugins/ZynAddSubFx/ZynAddSubFx.cpp
		${CMAKE_SOURCE_DIR}/plugins/ZynAddSubFx/ZynAddSubFx.h
	)
	add_test(NAME ZynAddSubFxInstrumentTest COMMAND ZynAddSubFxInstrumentTest)
	target_include_directories(ZynAddSubFxInstrumentTest PRIVATE
		# the plug-in's generated plugin_export.h (PLUGIN_EXPORT)
		${CMAKE_BINARY_DIR}/plugins/ZynAddSubFx
		${CMAKE_SOURCE_DIR}/plugins/ZynAddSubFx
		$<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>
	)
	# the test compiles the plug-in sources itself, so it needs the generated
	# export header of the plug-in target to exist first
	add_dependencies(ZynAddSubFxInstrumentTest zynaddsubfx)
	target_static_libraries(ZynAddSubFxInstrumentTest PRIVATE ZynAddSubFxCore lmmsobjs)
	target_link_libraries(ZynAddSubFxInstrumentTest PRIVATE
		${QT_LIBRARIES}
		${QT_QTTEST_LIBRARY}
	)
	target_compile_features(ZynAddSubFxInstrumentTest PRIVATE cxx_std_20)
	target_compile_definitions(ZynAddSubFxInstrumentTest PRIVATE
		LMMS_TESTING
		# This target compiles the plug-in's own ZynAddSubFx.cpp, which defines
		# zynaddsubfx_plugin_descriptor with PLUGIN_EXPORT: keep it from being
		# marked dllimport (the same construct as ClapEffectIntegrationTest).
		PLUGIN_STATIC_DEFINE
		PLUGIN_NAME=zynaddsubfx)
	set_tests_properties(ZynAddSubFxInstrumentTest PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

# Synthetic plug-in host test (ported from the standards fork): builds a MODULE
# plug-in from src/plugins/SyntheticAudioPlugin.cpp and drives it through the
# AudioPlugin host path, so the host code is exercised for real.
add_library(synthetic_audio_plugin MODULE src/plugins/SyntheticAudioPlugin.cpp)
target_include_directories(synthetic_audio_plugin PRIVATE
	$<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>
)
target_compile_definitions(synthetic_audio_plugin PRIVATE PLUGIN_NAME=synthetic)
target_compile_features(synthetic_audio_plugin PRIVATE cxx_std_20)
# The test double subclasses lmms::AudioPlugin, so its object code references lmms
# symbols (AudioPortsModel's moc, Model's vtable, JournallingObject's dtor ...).
# Unix shared modules tolerate unresolved symbols; macOS, mingw and MSVC do not, so
# this target failed to link there while linking only Qt. Real plugins link the zene
# target (cmake/modules/BuildPlugin.cmake), and the partc_ref_* harnesses above were
# fixed the same way — this target was the one left behind.
target_link_libraries(synthetic_audio_plugin PRIVATE zene ${QT_LIBRARIES})
set_target_properties(synthetic_audio_plugin PROPERTIES PREFIX "")

add_executable(AudioPluginTest
	src/plugins/AudioPluginTest.cpp
	${CMAKE_SOURCE_DIR}/plugins/TapTempo/TapTempo.cpp
	${CMAKE_SOURCE_DIR}/plugins/TapTempo/TapTempoView.cpp
	${CMAKE_SOURCE_DIR}/plugins/TapTempo/TapTempo.h
	${CMAKE_SOURCE_DIR}/plugins/TapTempo/TapTempoView.h
)
add_test(NAME AudioPluginTest COMMAND AudioPluginTest)
target_include_directories(AudioPluginTest PRIVATE
	$<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>
	${CMAKE_SOURCE_DIR}/plugins/TapTempo
	${CMAKE_BINARY_DIR}/plugins/TapTempo
)
target_static_libraries(AudioPluginTest PRIVATE lmmsobjs)
target_link_libraries(AudioPluginTest PRIVATE
	${QT_LIBRARIES}
	${QT_QTTEST_LIBRARY}
)
target_compile_features(AudioPluginTest PRIVATE cxx_std_20)
target_compile_definitions(AudioPluginTest PRIVATE
	LMMS_TESTING
	PLUGIN_NAME=taptempo
	SYNTHETIC_PLUGIN_PATH="$<TARGET_FILE:synthetic_audio_plugin>"
)
set_target_properties(AudioPluginTest PROPERTIES ENABLE_EXPORTS ON)
add_dependencies(AudioPluginTest synthetic_audio_plugin patman)
set_tests_properties(AudioPluginTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
lmms_add_coverage(synthetic_audio_plugin)
lmms_add_coverage(AudioPluginTest)

# Q050-005: exercise the granular effect's sample-rate buffer handoff while a
# live render loop runs under the allocation and realtime probes.
add_executable(GranularPitchShifterRealtimeTest src/core/GranularPitchShifterRealtimeTest.cpp)
add_test(NAME GranularPitchShifterRealtimeTest COMMAND GranularPitchShifterRealtimeTest)
target_include_directories(GranularPitchShifterRealtimeTest PRIVATE
	$<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>
	${CMAKE_SOURCE_DIR}/plugins/GranularPitchShifter
)
target_static_libraries(GranularPitchShifterRealtimeTest PRIVATE lmmsobjs)
target_link_libraries(GranularPitchShifterRealtimeTest PRIVATE
	${QT_LIBRARIES}
	${QT_QTTEST_LIBRARY}
)
target_compile_features(GranularPitchShifterRealtimeTest PRIVATE cxx_std_20)
target_compile_definitions(GranularPitchShifterRealtimeTest PRIVATE
	LMMS_TESTING
	GRANULAR_PLUGIN_PATH="$<TARGET_FILE:granularpitchshifter>"
)
set_target_properties(GranularPitchShifterRealtimeTest PROPERTIES ENABLE_EXPORTS ON)
add_dependencies(GranularPitchShifterRealtimeTest granularpitchshifter)
lmms_link_allocation_probe(GranularPitchShifterRealtimeTest
	"${CMAKE_CURRENT_SOURCE_DIR}/src/core/GranularPitchShifterRealtimeTest.cpp")
set_tests_properties(GranularPitchShifterRealtimeTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
lmms_add_coverage(GranularPitchShifterRealtimeTest)

if(TARGET carlabase)
	add_executable(CarlaParamFloatModelTest src/plugins/CarlaParamFloatModelTest.cpp)
	add_test(NAME CarlaParamFloatModelTest COMMAND CarlaParamFloatModelTest)
	set_tests_properties(CarlaParamFloatModelTest PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
	target_include_directories(CarlaParamFloatModelTest PRIVATE
		$<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>
		${CMAKE_SOURCE_DIR}/plugins/CarlaBase
		${CMAKE_SOURCE_DIR}/plugins/CarlaBase/carla/source
		${CMAKE_SOURCE_DIR}/plugins/CarlaBase/carla/source/includes
		${CMAKE_SOURCE_DIR}/plugins/CarlaBase/carla/source/utils
		${CMAKE_SOURCE_DIR}/plugins/CarlaBase/carla/source/backend
		${CMAKE_SOURCE_DIR}/plugins/CarlaBase/CarlaConfig
		${CMAKE_BINARY_DIR}/plugins/CarlaBase)
	target_static_libraries(CarlaParamFloatModelTest PRIVATE lmmsobjs)
	target_link_libraries(CarlaParamFloatModelTest PRIVATE
		${QT_LIBRARIES}
		${QT_QTTEST_LIBRARY})
	target_compile_features(CarlaParamFloatModelTest PRIVATE cxx_std_20)
	target_compile_definitions(CarlaParamFloatModelTest PRIVATE
		LMMS_TESTING
		CARLA_HEADER_PATH="${CMAKE_SOURCE_DIR}/plugins/CarlaBase/Carla.h")
	set_target_properties(CarlaParamFloatModelTest PROPERTIES ENABLE_EXPORTS ON)
endif()

# The raw ReverbSC support loop is a separate C source, so compile that exact
# implementation into its focused streaming regression test.
add_executable(ReverbSCRawProcessingTest
	src/plugins/ReverbSCRawProcessingTest.cpp
	${CMAKE_SOURCE_DIR}/plugins/ReverbSC/base.c
)
add_test(NAME ReverbSCRawProcessingTest COMMAND ReverbSCRawProcessingTest)
target_include_directories(ReverbSCRawProcessingTest PRIVATE
	${CMAKE_SOURCE_DIR}/plugins/ReverbSC
)
target_link_libraries(ReverbSCRawProcessingTest PRIVATE
	${QT_LIBRARIES}
	${QT_QTTEST_LIBRARY}
)
if(UNIX)
	target_link_libraries(ReverbSCRawProcessingTest PRIVATE m)
endif()
target_compile_features(ReverbSCRawProcessingTest PRIVATE cxx_std_20)
lmms_add_coverage(ReverbSCRawProcessingTest)

# The NAM engine's block-spanning silence prewarm is tested without loading the
# plugin module, using a small model written by the test itself.
add_executable(NamModelPrewarmTest
	src/core/NamModelPrewarmTest.cpp
	${CMAKE_SOURCE_DIR}/plugins/NeuralAmp/nam/NamModel.cpp
	${CMAKE_SOURCE_DIR}/plugins/NeuralAmp/nam/NamModelLoader.cpp
)
add_test(NAME NamModelPrewarmTest COMMAND NamModelPrewarmTest)
target_include_directories(NamModelPrewarmTest PRIVATE
	${CMAKE_SOURCE_DIR}/plugins/NeuralAmp
	${CMAKE_SOURCE_DIR}/plugins/NeuralAmp/nam
	${CMAKE_SOURCE_DIR}/plugins/NeuralAmp/rtneural/modules/Eigen
	${CMAKE_SOURCE_DIR}/plugins/NeuralAmp/rtneural/modules/json
)
target_link_libraries(NamModelPrewarmTest PRIVATE
	${QT_LIBRARIES}
	${QT_QTTEST_LIBRARY}
	${CMAKE_DL_LIBS}
)
if(NOT MSVC)
	target_link_libraries(NamModelPrewarmTest PRIVATE m)
endif()
target_compile_features(NamModelPrewarmTest PRIVATE cxx_std_20)
target_compile_options(NamModelPrewarmTest PRIVATE ${ZENE_NAM_EIGEN_OPTIONS})
set_tests_properties(NamModelPrewarmTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# The NAM loader is the only place a .nam file's declared dimensions become
# Eigen matrix sizes, and it is a self-contained engine with no Qt dependency.
# Keep its malformed-dimension regression in this suite so it runs from
# build/tests in a build without the plugin module.
add_executable(NamModelLoaderTest
	src/core/NamModelLoaderTest.cpp
	${CMAKE_SOURCE_DIR}/plugins/NeuralAmp/nam/NamModel.cpp
	${CMAKE_SOURCE_DIR}/plugins/NeuralAmp/nam/NamModelLoader.cpp
)
add_test(NAME NamModelLoaderTest COMMAND NamModelLoaderTest)
target_include_directories(NamModelLoaderTest PRIVATE
	${CMAKE_SOURCE_DIR}/plugins/NeuralAmp
	${CMAKE_SOURCE_DIR}/plugins/NeuralAmp/nam
	${CMAKE_SOURCE_DIR}/plugins/NeuralAmp/rtneural/modules/Eigen
	${CMAKE_SOURCE_DIR}/plugins/NeuralAmp/rtneural/modules/json
)
target_link_libraries(NamModelLoaderTest PRIVATE ${CMAKE_DL_LIBS})
if(NOT MSVC)
	target_link_libraries(NamModelLoaderTest PRIVATE m)
endif()
target_compile_features(NamModelLoaderTest PRIVATE cxx_std_20)

# Native VST3 instrument fixture and its probe.
#
# Opt-in on purpose: cmake/modules/Vst3Sdk.cmake builds lmms_vst3_sdk whenever
# the SDK checkout exists, but an ordinary build has no business gaining a test
# instrument, so it is behind WANT_VST3_TEST_INSTRUMENT (default OFF) and is
# never installed. See tests/data/vst3-test-instrument/CMakeLists.txt and
# docs/VST3-INSTRUMENT-FIXTURE.md (which records why the fixture exists at all).
if(TARGET lmms_vst3_sdk)
	option(WANT_VST3_TEST_INSTRUMENT
		"Build the VST3 instrument test fixture and its probe (test subject only)" OFF)
	if(WANT_VST3_TEST_INSTRUMENT)
		add_subdirectory(data/vst3-test-instrument)

		# A probe, not a QTest class: it is the independent check that the
		# fixture really reads ProcessData::inputEvents and renders audio in
		# response. Helper executables are explicitly out of scope for Gate 3
		# (see the note at the bottom of tests/no-tautology-gate.sh); this one
		# still exits non-zero on a failed check, so ctest enforces it.
		add_executable(Vst3InstrumentFixtureProbe
			src/plugins/Vst3InstrumentFixtureProbe.cpp
		)
		target_include_directories(Vst3InstrumentFixtureProbe PRIVATE
			${CMAKE_SOURCE_DIR}/include
		)
		target_compile_features(Vst3InstrumentFixtureProbe PRIVATE cxx_std_17)
		target_link_libraries(Vst3InstrumentFixtureProbe PRIVATE
			lmms_vst3_sdk
			${CMAKE_DL_LIBS}
		)
		target_compile_definitions(Vst3InstrumentFixtureProbe PRIVATE
			VST3_TEST_INSTRUMENT_BUNDLE="${LMMS_VST3_TEST_BUNDLE_DIR}"
		)
		add_dependencies(Vst3InstrumentFixtureProbe vst3-test-instrument)
		add_test(NAME Vst3InstrumentFixtureProbe COMMAND Vst3InstrumentFixtureProbe)
	
		# Host-level proof of the MIDI event path: the host fills
		# ProcessData::inputEvents from pushMidiEvent(), and the timing is sample
		# accurate. It compiles the host sources itself, exactly as ClapHostTest
		# does for the CLAP host, so it exercises the shipped Vst3Host.
		add_executable(Vst3InstrumentTest
			src/plugins/Vst3InstrumentTest.cpp
			${CMAKE_SOURCE_DIR}/plugins/Vst3Effect/Vst3BusMap.cpp
			${CMAKE_SOURCE_DIR}/plugins/Vst3Effect/Vst3Host.cpp
			${CMAKE_SOURCE_DIR}/plugins/Vst3Effect/Vst3HostReports.cpp
			${CMAKE_SOURCE_DIR}/plugins/Vst3Effect/Vst3EditorSession.cpp
			${CMAKE_SOURCE_DIR}/src/core/PluginEditorHost.cpp
			${CMAKE_SOURCE_DIR}/plugins/Vst3Effect/Vst3MidiEvent.cpp
			${CMAKE_SOURCE_DIR}/plugins/Vst3Effect/Vst3MidiMapping.cpp
			# the host path's chunking counters (CODE-4), core-owned
			${CMAKE_SOURCE_DIR}/src/core/PluginHostChunking.cpp
		)
		add_test(NAME Vst3InstrumentTest COMMAND Vst3InstrumentTest)
		lmms_link_allocation_probe(Vst3InstrumentTest
			"${CMAKE_CURRENT_SOURCE_DIR}/src/plugins/Vst3InstrumentTest.cpp")
		# Compiles core sources directly: the define must apply to THIS target's
		# own compilation - STATIC_COMPILE_DEFINITIONS only reaches linking targets.
		target_compile_definitions(Vst3InstrumentTest PRIVATE LMMS_STATIC_DEFINE)
		target_include_directories(Vst3InstrumentTest PRIVATE
			# the generated lmms_export.h, which the core headers carry
			${CMAKE_BINARY_DIR}/src
			${CMAKE_SOURCE_DIR}/include
			${CMAKE_SOURCE_DIR}/plugins/Vst3Effect
			# tests/src/core/AllocationProbe.h - the allocation counter the
			# real-time assertion uses; the tests beside it include it by
			# name, so this test does too.
			${CMAKE_CURRENT_SOURCE_DIR}/src/core
		)
		target_compile_features(Vst3InstrumentTest PRIVATE cxx_std_20)
		target_link_libraries(Vst3InstrumentTest PRIVATE
			${QT_LIBRARIES}
			${QT_QTTEST_LIBRARY}
			lmms_vst3_sdk
			${CMAKE_DL_LIBS}
		)
		target_compile_definitions(Vst3InstrumentTest PRIVATE
			LMMS_TESTING
			VST3_TEST_INSTRUMENT_BUNDLE="${LMMS_VST3_TEST_BUNDLE_DIR}"
			VST3_TEST_MONO_INSTRUMENT_BUNDLE="${LMMS_VST3_MONO_TEST_BUNDLE_DIR}"
		)
		add_dependencies(Vst3InstrumentTest
			vst3-test-instrument
			vst3-test-mono-instrument)

		# Product-level proof: a MidiClip drives the real vst3instrument MODULE
		# through the track's MIDI path, and the instrument's state survives the
		# project file. It needs the plug-in target, which exists only where VST3
		# hosting is enabled at all.
		if(TARGET vst3instrument)
			add_executable(Vst3InstrumentIntegrationTest
				src/plugins/Vst3InstrumentIntegrationTest.cpp
				${CMAKE_SOURCE_DIR}/plugins/Vst3Effect/Vst3BusMap.cpp
				${CMAKE_SOURCE_DIR}/plugins/Vst3Effect/Vst3Host.cpp
				${CMAKE_SOURCE_DIR}/plugins/Vst3Effect/Vst3HostReports.cpp
				${CMAKE_SOURCE_DIR}/plugins/Vst3Effect/Vst3EditorSession.cpp
				${CMAKE_SOURCE_DIR}/plugins/Vst3Effect/Vst3MidiEvent.cpp
				${CMAKE_SOURCE_DIR}/plugins/Vst3Effect/Vst3MidiMapping.cpp
			)
			add_test(NAME Vst3InstrumentIntegrationTest
				COMMAND Vst3InstrumentIntegrationTest)
			# It constructs a full Engine and opens the instrument's generated
			# view, so it needs a Qt platform plugin even when headless.
			set_tests_properties(Vst3InstrumentIntegrationTest PROPERTIES
				ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
			# It drives plugin.editor_open/close through the registry: their replies are held
			# to their schemas, as every LMMS_TESTS entry's are.
			if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.22)
				set_tests_properties(Vst3InstrumentIntegrationTest PROPERTIES
					ENVIRONMENT_MODIFICATION "ZENE_CONTROL_CHECK_RESULTS=set:1")
			endif()
			target_include_directories(Vst3InstrumentIntegrationTest PRIVATE
				$<TARGET_PROPERTY:lmmsobjs,INCLUDE_DIRECTORIES>
				${CMAKE_SOURCE_DIR}/plugins/Vst3Instrument
				${CMAKE_SOURCE_DIR}/plugins/Vst3Effect
				${CMAKE_SOURCE_DIR}/plugins/Vst3Effect
				${CMAKE_SOURCE_DIR}/plugins/Vst3Instrument
			)
			target_static_libraries(Vst3InstrumentIntegrationTest PRIVATE lmmsobjs)
			target_link_libraries(Vst3InstrumentIntegrationTest PRIVATE
				${QT_LIBRARIES}
				${QT_QTTEST_LIBRARY}
				lmms_vst3_sdk
				${CMAKE_DL_LIBS}
			)
			target_compile_features(Vst3InstrumentIntegrationTest PRIVATE cxx_std_20)
			target_compile_definitions(Vst3InstrumentIntegrationTest PRIVATE
				LMMS_STATIC_DEFINE
				LMMS_TESTING
				VST3_INSTRUMENT_PLUGIN_PATH="$<TARGET_FILE:vst3instrument>"
				VST3_INSTRUMENT_PLUGIN_DIR="$<TARGET_FILE_DIR:vst3instrument>"
				VST3_TEST_INSTRUMENT_BUNDLE="${LMMS_VST3_TEST_BUNDLE_DIR}"
			)
			# The module resolves LMMS core symbols from the host process, exactly
			# like the real lmms binary (src/CMakeLists.txt sets ENABLE_EXPORTS ON)
			# and like PluginPortsMigrationTest does.
			set_target_properties(Vst3InstrumentIntegrationTest PROPERTIES
				ENABLE_EXPORTS ON)
			add_dependencies(Vst3InstrumentIntegrationTest
				vst3instrument
				vst3-test-instrument)
			# 0.5.0 MX-094: the effect host opens the fixture's editor at the device's zoom.
			if(TARGET vst3effect)
				add_dependencies(Vst3InstrumentIntegrationTest vst3effect)
			endif()
		endif()

		# The SOCKET-DRIVEN half of the same feature (feature row 78, board task
		# #668). Vst3InstrumentIntegrationTest above loads the module through the
		# engine; this is what makes it reachable from the control surface - the
		# device catalogue test points the product's VST3 search directory at this
		# fixture's own bundle, so plugin.list enumerates a real VST3 class and
		# plugin.load loads it onto an instrument track through the registry. The
		# acceptance contract (CHARTER 3.1) is what the case exists for: a feature
		# that cannot be driven through the socket is not in the release. Without
		# this option the define is absent and the case skips.
		# MX-157: the VST3 fixture's latent mode (VST3_TEST_INSTRUMENT_LATENCY) is the VST3 subject.
		if(TARGET vst3instrument AND TARGET InstrumentLatencyPdcTest)
			get_filename_component(LMMS_VST3_LATENCY_FIXTURE_DIR "${LMMS_VST3_TEST_BUNDLE_DIR}" DIRECTORY)
			target_compile_definitions(InstrumentLatencyPdcTest PRIVATE
				VST3_TEST_INSTRUMENT_DIR="${LMMS_VST3_LATENCY_FIXTURE_DIR}")
			add_dependencies(InstrumentLatencyPdcTest vst3instrument vst3-test-instrument)
		endif()
		if(TARGET vst3instrument AND TARGET ControlDeviceCatalogueTest)
			# The bundle's own directory: the directory the product's VST3 scan
			# walks (ConfigManager::setVSTDir() takes exactly this).
			get_filename_component(LMMS_VST3_TEST_FIXTURE_DIR
				"${LMMS_VST3_TEST_BUNDLE_DIR}" DIRECTORY)
			target_compile_definitions(ControlDeviceCatalogueTest PRIVATE
				VST3_TEST_INSTRUMENT_DIR="${LMMS_VST3_TEST_FIXTURE_DIR}"
			)
			add_dependencies(ControlDeviceCatalogueTest
				vst3instrument
				vst3-test-instrument
			)
		endif()
		# The VST3 EFFECT half: plugin.list carries the SDK's gain sample and plugin.load hosts it in
		# vst3effect.
		if(TARGET vst3effect AND TARGET vst3-test-effect AND TARGET ControlDeviceCatalogueTest)
			get_filename_component(LMMS_VST3_TEST_EFFECT_FIXTURE_DIR
				"${LMMS_VST3_TEST_EFFECT_BUNDLE_DIR}" DIRECTORY)
			target_compile_definitions(ControlDeviceCatalogueTest PRIVATE
				VST3_TEST_EFFECT_DIR="${LMMS_VST3_TEST_EFFECT_FIXTURE_DIR}")
			add_dependencies(ControlDeviceCatalogueTest vst3effect vst3-test-effect)
		endif()
endif()
endif()
# ---------------------------------------------------------------------------
# Agent control surface (SPEC A12/A16). The ControlRegistryTest unit test runs
# in the LMMS_TESTS loop above; this one starts the real binary with
# --control-socket under QT_QPA_PLATFORM=offscreen and drives it from an
# EXTERNAL Python client over the AF_UNIX socket, so the socket, the wire
# framing and the command groups are exercised end to end.
# ---------------------------------------------------------------------------
find_program(PYTHON3_EXECUTABLE NAMES python3 python3.exe python)
# Keep each NAM WAV output failure independently discoverable and attributable.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND PYTHON3_EXECUTABLE AND TARGET nam_harness)
	set(NAM_HARNESS_WAV_FAILURE_SCRIPT
		${CMAKE_SOURCE_DIR}/plugins/NeuralAmp/tests/wav_write_failure_test.py)
	foreach(case legacy-wav write-input-wav wav-run)
		add_test(NAME NAMHarnessWavWriteFailure_${case}
			COMMAND ${PYTHON3_EXECUTABLE} ${NAM_HARNESS_WAV_FAILURE_SCRIPT}
				$<TARGET_FILE:nam_harness> ${case})
		set_tests_properties(NAMHarnessWavWriteFailure_${case} PROPERTIES TIMEOUT 60)
	endforeach()
endif()
# The NeuralAmp harness regression starts the real headless harness and checks
# that equal-length WAVs at different sample rates are rejected.
if(PYTHON3_EXECUTABLE AND TARGET nam_harness)
	add_test(NAME NeuralAmpCompareRateTest
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_SOURCE_DIR}/plugins/NeuralAmp/tests/compare_rate_test.py
			$<TARGET_FILE:nam_harness>)
	set_tests_properties(NeuralAmpCompareRateTest PROPERTIES TIMEOUT 60)
endif()
# The control surface suite is POSIX-only: its clients are AF_UNIX clients, so there is no
# socket for them to drive on Windows - where the transport is a NAMED PIPE behind the same
# contract (CODE-9) and has its own Windows-only proof below. The tests were registered on
# every platform and failed on Windows in 0.07 s each - run #34731085749
# (msvc-x64: 10 control-surface failures out of 14) - which reads as a defect and is a platform
# fact. Registered on UNIX only. The suite stays ACTIVE everywhere it can run, including the
# hardware-less CI runners, where its failure mode is the real thing it exists to catch.
IF(WIN32)
	SET(CONTROL_SUITE_AVAILABLE FALSE)
ELSE()
	SET(CONTROL_SUITE_AVAILABLE TRUE)
ENDIF()
# ---------------------------------------------------------------------------
# LuaApiSurface (0.3.0 feature row 50): the version + compatibility policy as a
# RATCHET. docs/LUA-API-SURFACE.md is the API surface of the version this tree
# declares, derived from the registration sources by tests/lua-api-surface.py.
# The test re-derives it and fails on drift in EITHER direction - a removed name
# (breaking: needs a MAJOR bump) and an added one (additive: needs a MINOR bump)
# - naming the entries and quoting the policy. It needs no build of the product:
# the surface is what the sources register, so it runs on every platform and on a
# machine that cannot build the DAW. docs/LUA-COMPATIBILITY-POLICY.md, section 2.
if(PYTHON3_EXECUTABLE)
	add_test(NAME LuaApiSurface
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/lua-api-surface.py
			--check
			--root ${CMAKE_SOURCE_DIR}
			--manifest ${CMAKE_SOURCE_DIR}/docs/LUA-API-SURFACE.md
	)
endif()

# The Windows half of the control surface (CODE-9): the same JSON-RPC framing, the same
# command ids and the same typed refusal shapes, over a NAMED PIPE. This smoke test starts
# THIS binary with --control-socket \\.\pipe\<name>, connects with CreateFileW, and checks
# the framing (two requests in one write, two whole reply lines), the ids the surface
# carries, the malformed-line and unknown-command refusals, the 1 MiB request-line cap, the
# start-up refusal a launcher reads for a path that is not a pipe name, and the shutdown
# (control.quit answers, the process exits, the name is gone). It is the Windows half's
# evidence and it is CI-only: the lane that wrote the transport had no Windows toolchain
# (docs/CONTROL-NAMED-PIPE.md). Registered on Windows only, and only when a python3 is on
# the PATH - a test that could not run must not be registered as one that passed.
IF(WIN32 AND PYTHON3_EXECUTABLE)
	add_test(NAME ControlNamedPipeSmoke
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-named-pipe-smoke.py
			$<TARGET_FILE:zene>)
	set_tests_properties(ControlNamedPipeSmoke PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 600
		# 77 = "nothing to drive" (the file exits 77 when its argument is not a
		# binary): reported as Skipped, the same convention the session-lifecycle
		# transcript uses.
		SKIP_RETURN_CODE 77
	)
ENDIF()

# The golden-audio programme's MEASURING INSTRUMENT, controlled on inputs whose answer is
# known (2026-09-15, lane 030/golden-audio, board task #679). tests/golden_audio_selftest.py
# synthesises its WAVs in a temp directory - two identical renders, one sample moved by one
# LSB, a stated -0.5 dB gain, two silent files, a three-render floor - and requires the
# instrument to report exactly that, including that the deliberate gain change FAILS the
# comparison and a change below the floor does not. No binary, no engine and no socket: it
# is registered on every platform a python3 is on, because a comparison whose control only
# runs on one platform is a claim rather than a control. It is also the only ctest that
# covers the measurement core itself (tests/golden_audio_lib.py).
if(PYTHON3_EXECUTABLE)
	add_test(NAME GoldenAudioSelfTest
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/golden_audio_selftest.py)
	set_tests_properties(GoldenAudioSelfTest PROPERTIES TIMEOUT 300)
endif()

# The real-time-safety WHOLE-TREE SWEEP (2026-09-16, lane 030/rt-safety, board task #678,
# feature row 52). AGENTS.md's realtime rule - "no allocation, no locking, no unbounded
# growth on audio-thread paths" - was held per feature until now: an allocation-counter
# test proved one path at a time and nothing measured the rule across the tree
# (docs/CONVENTIONS.md row 9 said so: "partially enforced"). tests/rt-safety-sweep.py
# walks every path:symbol pair the tree DECLARES in tests/rt-safety-scope.txt, scans the
# bodies it resolves for allocating, locking and container-growing constructs, and judges
# them against tests/rt-safety-allowlist.txt - a ledger of accepted hits with a reason and
# a COUNT that is a ratchet: it may only fall.
#
# Two tests, because the programme has two claims. RtSafetySweep is the gate over THIS
# tree: it needs no binary, no engine and no socket (it reads sources), so it is
# registered wherever a python3 is - a sweep that only runs on one platform is a claim,
# not a gate. RtSafetySelfTest is the control that makes the sweep believable: it plants
# a deliberate allocation, lock and container growth on fixture audio-thread paths and
# requires the same code the gate runs to fail on each one, name it, pass a clean path,
# and stay silent about a violation outside the declared scope. A sweep that cannot fail
# proves nothing (row 52's own words), and the real tree's copy of that control is
# recorded in docs/RT-SAFETY-SWEEP.md.
if(PYTHON3_EXECUTABLE)
	add_test(NAME RtSafetySweep
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/rt-safety-sweep.py
			--tree ${CMAKE_SOURCE_DIR}
			--scope ${CMAKE_CURRENT_SOURCE_DIR}/rt-safety-scope.txt
			--allowlist ${CMAKE_CURRENT_SOURCE_DIR}/rt-safety-allowlist.txt
			--check)
	set_tests_properties(RtSafetySweep PROPERTIES TIMEOUT 120)
	add_test(NAME RtSafetySelfTest
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/rt_safety_selftest.py)
	set_tests_properties(RtSafetySelfTest PROPERTIES TIMEOUT 300)
endif()

# Multicore Slice 0+1 (SPEC-MULTICORE-SCHEDULING-DRAFT §6): the published
# schedule (required/pending/class + ready list) versus the plan-order
# execution, both walks against the committed reference render, plus the two
# runtime halves Slice 0 promises - AllocationProbe armed ON a worker thread
# and the queue-full refusal counted. Engine + mixer, same harness shape as
# RoutingGraphLiveTest, so it needs the offscreen platform like every
# engine-launching test here.
target_compile_definitions(RoutingGraphScheduleTest PRIVATE
	ROUTING_GRAPH_LIVE_REFERENCE_FILE="${CMAKE_CURRENT_SOURCE_DIR}/reference/routing-graph-live-render.raw"
)
set_tests_properties(RoutingGraphScheduleTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
	TIMEOUT 300)

if(PYTHON3_EXECUTABLE AND CONTROL_SUITE_AVAILABLE)
	add_test(NAME ControlSocketIntegration
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-socket-integration.py
			$<TARGET_FILE:zene>
			${CMAKE_CURRENT_SOURCE_DIR}/data/agent-control-fixture.mmp
	)
	set_tests_properties(ControlSocketIntegration PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 600
	)
	# Session View milestone M1 (SPEC-zene-studio A1/A2, NEXT-0.3.0-AGENT-PROMPT
	# 3.2): a saved project launches 4 clips across 2 scenes in sync at the next
	# bar, driven END TO END through --control-socket. The session.* group is
	# native engine code (ARCH-4 S10 removed WANT_SESSION_VIEW), so the test
	# registers unconditionally. It takes no fixture: it builds the session
	# through the socket, saves it, and reopens it, which is the point of the
	# milestone.
		add_test(NAME ControlSessionLaunch
			COMMAND ${PYTHON3_EXECUTABLE}
				${CMAKE_CURRENT_SOURCE_DIR}/control-session-m1.py
				$<TARGET_FILE:zene>)
		set_tests_properties(ControlSessionLaunch PROPERTIES
			ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
			TIMEOUT 300
		)
		# The five session.* ids the M1 milestone above does not drive:
		# launch_slot, stop_slot, stop_all, clear_slot and clear. The coverage
		# audit (docs/COVERAGE-MATRIX-2026-09-13.md section 3.3) measured them as
		# referenced nowhere outside src/ and include/ - the group's only proof
		# named those five as its gap. The group is native engine code (ARCH-4
		# S10 removed WANT_SESSION_VIEW), so this registers unconditionally.
		add_test(NAME ControlSessionLifecycleTranscript
			COMMAND ${PYTHON3_EXECUTABLE}
				${CMAKE_CURRENT_SOURCE_DIR}/control-session-lifecycle-transcript.py
				$<TARGET_FILE:zene>)
		set_tests_properties(ControlSessionLifecycleTranscript PROPERTIES
			ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
			TIMEOUT 300
			SKIP_RETURN_CODE 77
		)
		# The GROUP's audit proof: one row per id for all ELEVEN session.* ids
		# (docs/reports/SESSION-API-PROOF-2026-09-15.md). The two tests above
		# drive the ids their FEATURE needs and between them touch all eleven;
		# this one drives every id for the id's own sake, because the question
		# "is this id a registered command that works, or a stub that only
		# refuses?" is not answered by two feature tests whose coverage of the
		# group is incidental. Each row carries the live reply, the read-back
		# that shows the claimed effect, and the A16 class read OUT OF THE
		# RUNNING INSTANCE (control.transactions), so the table's registered
		# reference is a measurement and not a grep. The group is native engine
		# code (ARCH-4 S10 removed WANT_SESSION_VIEW), so this registers with
		# its siblings unconditionally.
		add_test(NAME ControlSessionApiProof
			COMMAND ${PYTHON3_EXECUTABLE}
				${CMAKE_CURRENT_SOURCE_DIR}/control-session-api-proof.py
				$<TARGET_FILE:zene>)
		set_tests_properties(ControlSessionApiProof PROPERTIES
			ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
			TIMEOUT 300
			SKIP_RETURN_CODE 77
		)
	# The export.* group (render settings): dither and the SRC converter. It had
	# NO registered proof at all before the 2026-09-13 coverage audit - the only
	# artefact that exercised it, tests/control-export-settings.py, was not in
	# this file, which is the one group the audit listed as satisfying neither
	# half of the 0.3.0 scope contract's criterion 3. The transcript itself is
	# unchanged in what it asserts (defaults, the set, the transaction record,
	# the undo, the typed refusal); it now starts its instance through the same
	# headless recipe the other transcripts use and in a private run directory,
	# so it can run beside a sibling lane's copy of itself.
	add_test(NAME ControlExportSettings
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-export-settings.py
			$<TARGET_FILE:zene>
			${CMAKE_CURRENT_SOURCE_DIR}/data/agent-control-fixture.mmp
	)
	set_tests_properties(ControlExportSettings PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 300
	)
	# freeze / bounce-in-place (the engine-level Bar 2 gap): a track's output is
	# rendered to a WAV and the track made to play it, driven END TO END through
	# --control-socket. It is registered unconditionally - the bounce.*/freeze.*
	# groups carry no compile-time switch, because the render is the product's
	# own CLI render path and the take is a plain value on Track - and it takes
	# no fixture: it builds an AUDIBLE session through the socket (an instrument
	# track, two clips, notes), which is what lets it prove the take actually
	# plays. It reports ctest *Skipped* (exit 77) when the build offers no
	# loadable instrument, because an inaudible fixture could not make that
	# proof and a test that cannot prove its claim must not report Passed.
	add_test(NAME ControlFreezeCommandsTranscript
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-freeze-commands-transcript.py
			$<TARGET_FILE:zene>)
	set_tests_properties(ControlFreezeCommandsTranscript PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 900
	)
	# transport.punch_* (0.3.0, punch in/out): set the region, assert the GATE
	# flips with the transport position on both sides of both boundaries, and
	# assert the region survives project.save / project.open - read out of the
	# saved file, not trusted from the reply. No fixture: the region and its
	# predicate need no audio device, which is exactly the part this release
	# ships (the audio-side gate is deferred and named in KNOWN-LIMITATIONS).
	add_test(NAME ControlPunchTranscript
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-punch-transcript.py
			$<TARGET_FILE:zene>)
	set_tests_properties(ControlPunchTranscript PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 600
	)
	# Folder tracks (0.3.0, owner items 3+20+21): a folder is a real container
	# that holds other tracks, with an organisational mode and a routing mode
	# plus pinning and named visibility sets. It builds an AUDIBLE fixture
	# through the socket (two instrument tracks, each with a clip and notes),
	# saves the session, moves the in-memory model elsewhere, reopens the file,
	# and then MEASURES the sum: with the mode on, the FOLDER's own fader at 0
	# silences the children's render and at unity it sounds again, while in group
	# mode the folder owns no channel at all - so the silence can only have come
	# from the routing. It reports ctest *Skipped* (exit 77) when the build
	# offers no loadable instrument, because the audibility half could not run
	# and a test that cannot prove its claim must not report Passed.
	add_test(NAME ControlTrackFolderTranscript
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-track-folder.py
			$<TARGET_FILE:zene>)
	set_tests_properties(ControlTrackFolderTranscript PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 900
		SKIP_RETURN_CODE 77
	)
	# The 0.5.0 shell (docs/UI-SHELL.md) driven over the socket: window.get_state / set_view /
	# toggle / screenshot - views, the region rule, the unhideable centre, reveal-by-view-switch,
	# the 0.4.0 editor names, a floating window, and three distinct rendered views.
	add_test(NAME ControlShell
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-shell.py
			$<TARGET_FILE:zene>)
	set_tests_properties(ControlShell PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 300
	)
	# The note editor over the socket: window.note_editor_set opens a clip and sets the view,
	# note.edit's batch and its single undo, its whole-batch refusal, the lanes' commands, and
	# the Edit view rendered with the editor in the centre (docs/NOTE-EDITOR.md).
	add_test(NAME ControlNoteEditor
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-note-editor.py
			$<TARGET_FILE:zene>)
	set_tests_properties(ControlNoteEditor PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 300
	)
	# The 0.5.0 engine wiring (L01, research/daw-ui-specs/synthesis/engine-audit.md): devices at a
	# slot and on rack chains, chain presets on rack chains, track.set_mixer_channel, the header's
	# transport.record, and the Lua scripting spine (zene.command) - each proven over the socket.
	add_test(NAME ControlEngineWiring
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-engine-wiring.py
			$<TARGET_FILE:zene>)
	set_tests_properties(ControlEngineWiring PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 300
	)
	# Q050-159: the manual's Patterns, clips and cells section and its first-beat tour step,
	# then the example's sample track traced to its mixer channel over the socket with the
	# command names the section gives (track.overview, mixer.get_state, track.set_mixer_channel).
	add_test(NAME ControlWorkflowModels
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-workflow-models.py
			$<TARGET_FILE:zene>)
	set_tests_properties(ControlWorkflowModels PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 300
	)
	# Q050-235: the touch workspace choice is a user preference - selected through window.appearance,
	# one undo restores it, and a restarted instance over the same config reads the choice back.
	add_test(NAME ControlTouchWorkspace
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-touch-workspace.py
			$<TARGET_FILE:zene>)
	set_tests_properties(ControlTouchWorkspace PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 300
	)
	# The 0.5.0 L13 customisation surface: shortcuts, layouts, floating panels, the header,
	# macros, arranger zoom/fit/tool and the pattern list - over the socket, on a throwaway config.
	add_test(NAME ControlCustomisation
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-customisation.py
			$<TARGET_FILE:zene>)
	set_tests_properties(ControlCustomisation PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 300
	)
	# The 0.5.0 L05 audio edits over the socket: range fades, automatic fades and transient marks,
	# splits at beats, a fade shape, transients, and slicing to a SlicerT track.
	add_test(NAME ControlAudioEditing
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-audio-editing.py
			$<TARGET_FILE:zene>)
	set_tests_properties(ControlAudioEditing PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 300
	)
	# Q050-125 over the socket: clip gain, envelope, polarity, one undo each, read-back, save/reopen
	# and an old writer's clip loading non-inverted.
	add_test(NAME ControlClipPolarity
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-clip-polarity.py
			$<TARGET_FILE:zene>)
	set_tests_properties(ControlClipPolarity PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 300
	)
	# The 0.5.0 L09 routing surface over the socket: the listen bus, sends from many channels, send pan,
	# nested and linked VCA groups, the mixer view's sends-on-faders mode.
	add_test(NAME ControlMixingRouting
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-mixing-routing.py
			$<TARGET_FILE:zene>)
	set_tests_properties(ControlMixingRouting PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 300
	)
	# The Inspector and the Device panel over the socket: window.select selects a track, a clip or
	# a mixer channel; the reply is what the two panels draw - the track's devices in chain order,
	# a channel's inserts with the modulator beside the one it drives - and the panel follows a
	# device removed and restored over the socket (docs/UI-SHELL.md §8).
	add_test(NAME ControlDevices
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-devices.py
			$<TARGET_FILE:zene>)
	set_tests_properties(ControlDevices PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 300
	)
	# Structural undo (0.3.0, feature row 75 / task #664): track add / remove /
	# move and effect add / remove are journalled, and ONE control.undo restores
	# them - INCLUDING a deleted track WITH ITS CLIPS. It is driven END TO END
	# through --control-socket, and every assertion about the restored music is
	# made against the CLIPS' CONTENT read back after the undo (position, length
	# and every note's key/position/length/velocity), because "a track with that
	# name exists again" is satisfied by exactly the lossy undo the row is about:
	# ~Track destroys the clips before TrackContainer::removeTrack is reached, so
	# a checkpoint taken at the container would restore an EMPTY track. It also
	# MEASURES the row-51 interaction (clip-<n> is an index-derived ordinal, so a
	# delete shifts every later clip's id and the undo shifts them back), and the
	# byte accounting that an action step got wrong for a structural payload
	# (deleting a track must GROW control.undo_depth's retained_bytes; a step that
	# counted 0 would print no growth). The effect half reports Skipped (exit 77)
	# when the build offers no loadable effect, never Passed.
	add_test(NAME ControlUndoStructuralTranscript
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-undo-structural.py
			$<TARGET_FILE:zene>)
	set_tests_properties(ControlUndoStructuralTranscript PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 900
		SKIP_RETURN_CODE 77
	)
	# VCA / mix-and-edit groups (0.3.0, OWNER-31 item 11): the phase-locked
	# multitrack edit group, driven END TO END through --control-socket. It
	# asserts the fourteen registered ids and their schemas, that a group can be
	# CREATED from the socket (before this lane the only way was to edit the
	# project file), that the fader scales its members without writing any
	# member's own fader or being undoable in more than one step, and - the
	# feature itself - that ONE `vca.edit_move` moves the anchor clip and every
	# other member's overlapping clip by the same delta and that ONE
	# `control.undo` returns all of them. It also proves the edit set through a
	# REAL save/open round trip (the in-memory model is moved elsewhere first,
	# so what comes back can only have come from the file) and drives the junk /
	# refusal surface the agent-surface sweep depends on. It takes no fixture
	# and no compile-time switch, so it has no skip path and reports Passed only
	# when every check held.
	add_test(NAME ControlVcaCommands
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-vca-commands.py
			$<TARGET_FILE:zene>)
	set_tests_properties(ControlVcaCommands PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 900
	)
	# clock.* (0.3.0, MIDI clock): the DAW as a clock master and as a clock slave,
	# driven END TO END through --control-socket. It asserts the master's OWN
	# emission over a real transport - the message set START/pulses/Song Position
	# Pointer/STOP, read back in order, with the counters and a bounded monitor -
	# and the slave's contract against the one direction a socket test can create:
	# enabled with NO clock arriving, it must report itself unlocked, write no
	# tempo and not move the transport (asserted again after several audio periods
	# and three of the follower's polls, so "nothing" is measured, not raced).
	# The rate arithmetic - a pulse stream at a known rate producing that tempo,
	# following a tempo CHANGE, a lock that drops when the stream stops - cannot
	# be driven from a socket (an incoming clock cannot be injected into a running
	# instance), so it is proved by the registered MidiClockTest QTest above, and
	# the bound that remains is stated in docs/KNOWN-LIMITATIONS.md: the counters
	# this test reads are what the engine HANDED to its MIDI client, not bytes an
	# external synth received. It takes no fixture and no compile-time switch.
	add_test(NAME ControlClockCommands
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-clock-commands.py
			$<TARGET_FILE:zene>)
	set_tests_properties(ControlClockCommands PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 600
	)
	# pdc.* (0.3.0, plugin delay compensation - feature row 27 of
	# docs/FEATURE-LIST-0.3.0.md): the mixer's own published PDC graph read off
	# the wire - the total latency, every channel's alignment point and chain
	# latency, the compensation at every send, and whether sidechain routing
	# exists - and that the numbers follow the topology the routing verbs build.
	# NO fixture and no compile-time switch: the mixer publishes its graph in
	# every configuration. The bound it states is in its docstring and in
	# docs/KNOWN-LIMITATIONS.md: a NONZERO graph needs a device that reports
	# latency, and this release's built-ins report 0, so the arithmetic is proven
	# by the registered PdcMixerTest and the SURFACE by this transcript.
	add_test(NAME ControlPdcCommands
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-pdc-commands.py
			$<TARGET_FILE:zene>)
	set_tests_properties(ControlPdcCommands PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 600
	)
	# routing.* and the mixer group's routing verbs (0.3.0, feature rows 27-28):
	# routing.get_state on a MEASURED graph - load effects and read the node set,
	# the connections, the cached topological order and the output node back off
	# the wire - plus mixer.route_to / mixer.send_to / mixer.sidechain_to /
	# mixer.route_remove, which docs/specs/AGENT-TOOLING.md:186 names and the
	# 0.3.0 tip did not register. It asserts the engine's own pre-fader rule for a
	# send into a bus, that the feedback rule is the refusal, and that each verb
	# records a true_inverse transaction. No fixture and no compile-time switch.
	add_test(NAME ControlRoutingCommands
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-routing-commands.py
			$<TARGET_FILE:zene>)
	set_tests_properties(ControlRoutingCommands PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 600
	)
	# feedback.* (board card #709, cycle-permitted signal-graph submode): the
	# loop refused in default mode, accepted only after feedback.enable (which
	# says PDC is suspended and carries REAPER's warning), the suspension read
	# back at the point of use, disable restoring normal PDC, and the negative
	# control - the default-mode project's pdc.report, mixer.get_state and
	# saved bytes equal to the baseline captured on the PRE-CHANGE build,
	# equal again after disable. The immediate neighbour above is the routing
	# proof this submode generalizes (its check_feedback_refusal is the same
	# refusal this test re-asserts as the control).
	add_test(NAME ControlFeedbackCommands
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-feedback-commands.py
			$<TARGET_FILE:zene>)
	set_tests_properties(ControlFeedbackCommands PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 600
	)
	# bus.* (0.3.0, parallel buses - feature row 29 of
	# docs/FEATURE-LIST-0.3.0.md): the bus topology driven end to end, with both
	# A16 answers measured - bus.create is ONE undoable step and bus.remove is
	# REFUSED, typed as 'irreversible', because nothing recreates a channel with
	# state. No fixture and no compile-time switch.
	add_test(NAME ControlBusCommands
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-bus-commands.py
			$<TARGET_FILE:zene>)
	set_tests_properties(ControlBusCommands PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 600
	)
	# port.* (0.3.0, audio ports / pin matrix - feature row 29): the pin matrix
	# the PinConnector view edits, reached through the socket, with the write and
	# its recorded inverse. ALWAYS proves the typed answers a device without an
	# audio-ports model gives and every malformed request's refusal; the pin WRITE
	# needs a device that HAS a model (an AudioPlugin-derived one - the CLAP and
	# VST3 hosts, the analysers) and this build may ship none. It reports ctest
	# *Skipped* (exit 77) in that case, never *Passed*: the
	# ControlFreezeCommandsTranscript precedent - a test that cannot make its
	# measurement must not report that it did.
	add_test(NAME ControlPortsCommands
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-ports-commands.py
			$<TARGET_FILE:zene>)
	set_tests_properties(ControlPortsCommands PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 600
		SKIP_RETURN_CODE 77
	)
	# plugin.scan_cache_* / plugin.rescan (0.3.0, feature row 46 "Plugin scan
	# cache and quarantine"): the cache and the quarantine list driven over
	# --control-socket, with the cache FILE read off disk as well as off the
	# wire (the audit's row names hand-editing that file as the only route
	# before this group). It asserts the recorded inverse restores a
	# quarantined entry WITH its reason, measures the negative control (a
	# path-only re-add loses it), and - when the build has plugin modules, which
	# it points LMMS_PLUGIN_DIR at - that a quarantined plugin really leaves
	# plugin.list and comes back. No compile-time switch; the module-dependent
	# checks are reported as SKIPPED inside the transcript when this build has
	# no plugin files, never as passed.
	add_test(NAME ControlPluginScanCommands
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-plugin-scan-commands.py
			$<TARGET_FILE:zene>)
	set_tests_properties(ControlPluginScanCommands PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 600
	)
	# plugin.scan_start / scan_status / scan_cancel + plugin.list's vendor
	# (Q050-090 "Plug-in manager vendor and background scan"): the background
	# rescan driven over --control-socket - a paced run is followed (progress,
	# per-file outcomes, the cached catalogue still served), cancelled (the
	# previous catalogue kept, the cache still answered) and re-run to done,
	# with the task.list rows and the scan_start irreversible transaction to
	# prove it; every plugin.list entry's vendor is asserted with the
	# built-in amplifier naming "Zene Studio". No compile-time switch; a
	# paced start/cancel needs no plugin files, so nothing is SKIPPED.
	add_test(NAME ControlPluginScanBackground
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-plugin-scan-background.py
			$<TARGET_FILE:zene>)
	set_tests_properties(ControlPluginScanBackground PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 600
	)
	# crash.* (0.3.0, feature row 54 "Crash reporter"): the reporter's state and
	# its two real operations over --control-socket, with the `offered` sentinel
	# and the report file checked ON DISK, both writers' typed `irreversible`
	# undo refusal, and crash.upload_report's by-name refusal (this build has no
	# upload). The report is PLANTED at the path the module names, because a real
	# crash kills the process that would have to ask about it; the transcript
	# says so. No compile-time switch.
	add_test(NAME ControlCrashReporter
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-crash-reporter.py
			$<TARGET_FILE:zene>)
	set_tests_properties(ControlCrashReporter PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 600
	)
	# record.* (0.3.0, recording crash recovery): journal a capture, kill the
	# instance with SIGKILL (a REAL abnormal exit), then start a SECOND instance
	# against the same working directory and assert the NEXT START finds the
	# capture, reports a recoverable count that cannot exceed the file, and states
	# the bound this run measures. The crash is the exit code -9 this test
	# asserts, not a simulation of one.
	add_test(NAME ControlRecordingRecovery
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-recording-recovery.py
			$<TARGET_FILE:zene>)
	set_tests_properties(ControlRecordingRecovery PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 600
	)
	# record.* recording ENGINE surface (0.3.0, feature rows 64/14/16): the
	# arbitrary input count proved ACROSS A RESTART through --control-socket - the
	# real binary writes the new count, the second instance reports it and accepts
	# a route armed for an input channel the first instance had to refuse - plus
	# the arm verbs over the multi-track recorder, track.set_arm's real capture
	# with control.undo taking it back, the retrospective AUDIO window, and the
	# typed refusals of every one of them. No sound card is needed: what the DEVICE
	# did is asserted for internal consistency (capture_capable / capture_open /
	# capture_reason), never for a particular value, because this box's answer is
	# not a property of the feature.
	add_test(NAME ControlRecordInputs
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-record-inputs.py
			$<TARGET_FILE:zene>)
	set_tests_properties(ControlRecordInputs PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 600
	)
	# JACK into the capture path (relief plan R2.3, owner decision 16): the real binary on the
	# JACK backend against a private `jackd -d dummy`; the capture must report open, its frame
	# count must rise and an armed route must receive frames. Exit 77 = skipped (no jackd here),
	# which ctest reports as NOT RUN rather than passed.
	add_test(NAME ControlJackCapture
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-jack-capture.py
			$<TARGET_FILE:zene>)
	set_tests_properties(ControlJackCapture PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		SKIP_RETURN_CODE 77
		TIMEOUT 300
		RUN_SERIAL TRUE
	)
	# R6.3 (gate 1's checked-command ratchet): the success paths only a RUNNING instance reaches - a landed
	# Arrangement Record performance (the audio thread records it) and
	# project.audible_diff (it renders through the running binary as children).
	add_test(NAME ControlSuccessPaths
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-success-paths.py
			$<TARGET_FILE:zene>)
	set_tests_properties(ControlSuccessPaths PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 600
	)
	# Q050-021: the stopped-seek regression runs alone so unrelated import, export and render
	# plug-ins are not prerequisites for its control-socket check.
	add_test(NAME ControlStoppedSeek
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-success-paths.py
			$<TARGET_FILE:zene> --stopped-seek-only
	)
	set_tests_properties(ControlStoppedSeek PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 300
	)
	# midi.retro_capture_* (0.3.0, retrospective MIDI capture - owner's-31 item
	# 14): PLAY real MIDI into the running engine from the host's ALSA sequencer
	# (`aplaymidi` is an external client, exactly like a keyboard) and recover it
	# through --control-socket. It asserts the same file played DISARMED leaves
	# the window empty, the armed window's notes come back through roll.get_state
	# with the positions/lengths/keys/velocities they were played with, one
	# control.undo removes the clip, and more events than the window holds is the
	# DOCUMENTED drop-oldest behaviour rather than a crash. It reports ctest
	# *Skipped* (exit 77) - never *Passed* - on a host with no ALSA-sequencer
	# tooling or an engine that came up on the dummy MIDI client, because a test
	# that cannot make its measurement must not report that it did.
	add_test(NAME ControlRetroCapture
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-retro-capture.py
			$<TARGET_FILE:zene>)
	set_tests_properties(ControlRetroCapture PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 600
		# 77 is "this host cannot supply a real MIDI source" (no alsa-utils, or
		# the engine came up on the dummy MIDI client). CTest reports a test that
		# exits 77 as Skipped, never Passed - the ControlFreezeCommandsTranscript
		# and ControlCommandsSnapshot precedent.
		SKIP_RETURN_CODE 77
	)
	# midi.reconnect_* (0.3.0 feature-list row 18, OWNER-31 item 7): MIDI
	# CONTROLLER AUTO-RECONNECTION proved against a real device-shaped client.
	# The transcript is its own external ALSA-sequencer client (the script
	# re-executes itself in `--probe` mode, so the process that creates the
	# controller, sends through SUBSCRIBERS and dies is a real external client,
	# not a call inside the engine). It opens a project whose <midiport> is bound
	# to that client, asserts the engine attached to it, KILLS the client by PID,
	# creates it again under the SAME name at a NEW sequencer address, and asserts
	# the engine re-attached without user action: the engine's own report
	# (midi.reconnect_status / midi.clients_list), the KERNEL's subscription table
	# (aconnect -l) and events delivered THROUGH the restored subscription are the
	# readings, with a negative control - a second client nothing is bound to -
	# proving the delivery is gated on the binding, and the mode switch measured in
	# both directions. It reports ctest *Skipped* (exit 77) - never *Passed* - on a
	# host with no ALSA-sequencer tooling or an engine that came up on the dummy
	# MIDI client, because a test that cannot make its measurement must not report
	# that it did. aplaymidi cannot supply this client: it requires --port and
	# therefore addresses its destination directly, bypassing subscriptions.
	# The engine half with no device at all is the registered MidiReconnectTest
	# QTest above.
	add_test(NAME ControlMidiReconnect
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-midi-reconnect.py
			$<TARGET_FILE:zene>)
	set_tests_properties(ControlMidiReconnect PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 600
		# 77 is "this host cannot supply a real MIDI controller" (no alsa-utils,
		# or the engine came up on the dummy MIDI client). CTest reports a test
		# that exits 77 as Skipped, never Passed - the ControlRetroCapture and
		# ControlCommandsSnapshot precedent.
		SKIP_RETURN_CODE 77
	)
	# wasm.* (item #614): host the ABI example effect in the WASM DSP sandbox
	# END TO END through --control-socket, and read back what the ABI does with it.
	# Registered only when the sandbox is compiled in (WANT_WASM, which needs the
	# wasmtime C API on the find path - see CMakeLists.txt:957-963), because the
	# commands it drives do not exist otherwise: a test that could not run must not
	# be registered as one that passed. It takes no fixture beyond the committed
	# example module, tests/data/wasm-effect-abi/softclip.wat, which was written
	# from docs/WASM-EFFECT-ABI.md alone - so this is the first thing that ever
	# executes that document's claims.
	if(WANT_WASM)
		add_test(NAME ControlWasmSandbox
			COMMAND ${PYTHON3_EXECUTABLE}
				${CMAKE_CURRENT_SOURCE_DIR}/control-wasm-sandbox.py
				$<TARGET_FILE:zene>)
		set_tests_properties(ControlWasmSandbox PROPERTIES
			ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
			TIMEOUT 300
		)
		# 0.5.0 E22: a wasm_effect device in a chain takes a module headlessly (wasm.device_load).
		add_test(NAME ControlWasmDevice
			COMMAND ${PYTHON3_EXECUTABLE}
				${CMAKE_CURRENT_SOURCE_DIR}/control-wasm-device.py
				$<TARGET_FILE:zene>)
		set_tests_properties(ControlWasmDevice PROPERTIES
			ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
			TIMEOUT 300
		)
	endif()
	# The groove pool and quantise surface (docs/GROOVE-POOL.md): the release
	# contract's section 3.1 claim for it, end to end through --control-socket.
	# It starts the real binary, captures the feel of one clip into a named
	# groove, applies that groove to another clip, quantises with a strength and
	# a humanise amount, undoes every one of them, and reads every number back
	# off the wire (a note's tick position and velocity before and after), so a
	# command that answered ok without moving a note fails here. No fixture file:
	# it builds its own clips through track.add / clip.add / note.add, which is
	# what an agent has.
	add_test(NAME ControlGrooveCommands
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-groove-commands.py
			$<TARGET_FILE:zene>)
	set_tests_properties(ControlGrooveCommands PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 300
	)
	# The chord progression workflow (Q050-019): the suggestions the progression
	# panel shows (chord.progression_list with a scale) and the write its button
	# dispatches (chord.progression_generate), end to end through
	# --control-socket - the release contract's section 3.1 path. The agent asks
	# the catalogue to spell itself in a scale, selects a progression, writes its
	# notes into a clip, reads them back through roll.get_state, and takes them
	# back with ONE control.undo. No fixture file: it builds its own clip through
	# track.add / clip.add / note.add, which is what an agent has.
	add_test(NAME ControlChordProgression
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-chord-progression.py
			$<TARGET_FILE:zene>)
	set_tests_properties(ControlChordProgression PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 300
	)
	# The tempo map as PROJECT STATE (docs/TEMPO-MAP.md section 3): the round trip
	# the certification sweep broke (BUG-D4c-8402 - an ACTIVATED empty map was
	# written, then dropped by the reader, so the element vanished one
	# save -> open -> save later). tests/src/core/TempoMapPersistenceTest.cpp holds
	# the map's own file claim in process; only the real binary can show the
	# release contract's section 3.1 path, so here an external client activates the
	# map over --control-socket, saves with project.save, reloads with project.open
	# and reads the bytes back out of the documents the application wrote. It also
	# covers a POPULATED map as the control: the fix must not change what a
	# non-empty map does. No fixture file: it authors its own events.
	add_test(NAME ControlTempoMapPersistence
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-tempo-map-persistence.py
			$<TARGET_FILE:zene>)
	set_tests_properties(ControlTempoMapPersistence PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 300
	)
	# render.stems (0.3.0 verb wave, feature-tracker row 68): per-track stem
	# export driven end to end through --control-socket, which is the half the
	# engine's own StemExportTest cannot cover - that suite proves the four stems
	# SUM TO THE MIX at the C++ level, and this one proves the DECLARED id drives
	# that engine from the socket. It builds its own tracks, checks the argument
	# refusals create nothing on disk, runs the real export, asserts the files
	# exist, are non-empty RIFF/WAVE and follow the documented
	# `<index>_<name>.wav` naming, runs it a SECOND time into the same directory
	# (a re-export must report the files it rewrote rather than refuse an empty
	# set), and reads control.transactions to show the verb leaves no project
	# record. TIMEOUT is generous because EACH render is given the declared
	# per-command budget the four commands that already render use
	# (tests/freeze_bounce_evidence.py:57, RENDER_TIMEOUT = 180.0) - the wait is
	# declared, not hidden behind a raised socket timeout. docs/RENDER-CHILD-WAIT.md
	# records that the control surface is dead for the child's lifetime and that
	# the deferred-reply fix is designed but not built; this lane does not build
	# it and docs/KNOWN-LIMITATIONS.md says so for render.stems too.
	add_test(NAME ControlStemExportVerb
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-stem-export-verb.py
			$<TARGET_FILE:zene>)
	set_tests_properties(ControlStemExportVerb PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 900
	)
	# stem.* (docs/FEATURE-LIST-0.3.0.md row 26, board task #653): the offline
	# stem-separation engine driven end to end through --control-socket, which
	# is the half the engine's own five tests cannot cover - they prove the
	# separation, the job manager and the model store at the C++ level, and this
	# one proves the DECLARED ids drive them from the socket. It runs the REAL
	# pipeline on the committed 458-byte stub ONNX graph
	# (tests/data/stub-4stem-linear.onnx) through tools/stem_split_cli.py, so no
	# 166 MB model download is needed: it starts a job and shows the surface
	# still answers (control.ping) while the job runs, polls it to completion
	# with stem.job_status, writes the four stems with stem.job_result and
	# asserts each file is a non-empty float32 RIFF/WAVE of the mix's own
	# length, checks the argument and state refusals, cancels a slowed job
	# (LMMS_STEM_CHUNK_DELAY_MS, the separator's own test hook) and reads the
	# model store's policy - the unpinned default spec refuses to download.
	# Registered only when the feature is compiled in (WANT_STEM_SPLIT, ON by
	# default since owner decision 14: a build that configures it OFF has none
	# of the ids at all);
	# SKIP_RETURN_CODE 77 is for a host with no python onnxruntime, which would
	# make the backend unavailable rather than the group wrong. The test itself
	# passes the user-site onnxruntime directory through PYTHONPATH, because the
	# harness's redirected HOME would otherwise hide it from the instance (see
	# its onnxruntime_site_dir).
	IF(LMMS_HAVE_STEM_SPLIT)
		add_test(NAME ControlStemCommands
			COMMAND ${PYTHON3_EXECUTABLE}
				${CMAKE_CURRENT_SOURCE_DIR}/control-stem-commands.py
				$<TARGET_FILE:zene>)
		set_tests_properties(ControlStemCommands PROPERTIES
			ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
			TIMEOUT 900
			SKIP_RETURN_CODE 77
		)
	ENDIF()
	# Plugin chains as reusable presets (the 0.3.0 ladder's "plugin-chain
	# presets", OWNER-31 item 2), which is the release contract's section 3.1
	# claim for it, end to end through --control-socket: it starts the real
	# binary, captures a two-device chain from one track as a named preset,
	# applies that preset to a SECOND track, and reads the ordered device list
	# and every parameter value back off the wire through dsp.get_state - so a
	# command that answered ok without applying anything fails here. It then
	# takes the applied chain through a real project.save -> project.open round
	# trip, proves the preset store survives it (it is NOT project state, which
	# is what makes a preset usable in another project), and reverses the apply,
	# the rename and the remove through control.undo. No fixture file: it builds
	# its own tracks and devices through track.add / plugin.load, which is what
	# an agent has. The store it writes lives in the instance's own temp HOME.
	add_test(NAME ControlChainPresets
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-chain-presets.py
			$<TARGET_FILE:zene>)
	set_tests_properties(ControlChainPresets PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 300
	)
	# The render/export presets and the ranged render (feature rows 70 and 71),
	# end to end through --control-socket: the four store verbs with their typed
	# refusals, an applied preset MEASURED in the next render's own WAV header
	# (96000 Hz / 24 bit parsed out of the file, never out of the reply), and the
	# time range measured in frame counts against the tempo's own arithmetic
	# (Engine::updateFramesPerTick's expression, recomputed here) plus a byte
	# comparison of a rendered span against the same span of a whole-project
	# render. It starts the REAL binary, so the store it writes lives in the
	# instance's own temp HOME and never the developer's. Each render.render call
	# carries the declared RENDER_TIMEOUT budget instead of a raised socket
	# timeout (docs/RENDER-CHILD-WAIT.md).
	add_test(NAME ControlRenderPresets
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-render-presets.py
			$<TARGET_FILE:zene>)
	set_tests_properties(ControlRenderPresets PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 900
	)
	# 0.5.0 L12 (Q050-009 / feature row 90): the batch section export (render.sections) one
	# bounded file per named section, named after the section and rendered to its own ticks; and a
	# stem export (render.stems) whose files carry the rate and bit depth of the applied render
	# preset rather than a fixed 44.1 kHz. Every file's own WAV header is parsed, never the reply.
	# render.sections runs one child per section, so its own budget is a multiple of the declared
	# RENDER_TIMEOUT (docs/RENDER-CHILD-WAIT.md).
	add_test(NAME ControlRenderSections
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-render-sections.py
			$<TARGET_FILE:zene>)
	set_tests_properties(ControlRenderSections PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 1200
	)
	# Session sync, two real instances (D11 "Ableton Link sync", the
	# two-instance proof the brief names): both join one session and one drives
	# the other's tempo and beat phase with no relay between them, through
	# --control-socket. It starts TWO binaries because the claim is about two
	# instances - one process cannot prove a wire.
	#
	# RUN_SERIAL is not tidiness: the session travels over UDP multicast on
	# 224.76.78.75:20808 and the peer set is "whoever is on the segment", so a
	# test running beside this one is a legitimate peer and would make the
	# assertions about "the other instance" ambiguous. ctest is told not to run
	# anything at the same time as this test.
	#
	# SKIP_RETURN_CODE 77: a host whose multicast cannot start reports ctest
	# "Skipped" with the reason printed, NEVER "Passed". The model and the
	# surface are still covered there by ControlLinkCommandsTest.
	add_test(NAME ControlLinkSync
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-link-sync.py
			$<TARGET_FILE:zene>)
	# Pinned to the zene-link-style fallback for the reason ControlLinkCommandsTest is
	# (its transcript asserts that model's revisions and loopback probe); the two
	# binaries inherit ctest's environment through control_socket_harness.py.
	set_tests_properties(ControlLinkSync PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen;ZENE_LINK_BACKEND=zene-link-style"
		TIMEOUT 300
		RUN_SERIAL TRUE
		SKIP_RETURN_CODE 77
	)
	# Two real instances in ONE real Ableton Link session (WF-051): the cross-process
	# half of the Ableton backend's proof - tempo both ways, start/stop both ways and
	# both play heads locked to the session phase, nothing but Link's protocol between
	# them. RUN_SERIAL and SKIP_RETURN_CODE 77 for ControlLinkSync's reasons (a skip
	# only on a measured multicast failure).
	IF(LMMS_HAVE_ABLETON_LINK)
		add_test(NAME ControlLinkInterop
			COMMAND ${PYTHON3_EXECUTABLE}
				${CMAKE_CURRENT_SOURCE_DIR}/control-link-interop.py
				$<TARGET_FILE:zene>)
		set_tests_properties(ControlLinkInterop PROPERTIES
			ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
			TIMEOUT 300
			RUN_SERIAL TRUE
			SKIP_RETURN_CODE 77
		)
	ENDIF()
	# Auto-mastering wave 1 (feature rows 25 and 72 of docs/FEATURE-LIST-0.3.0.md;
	# docs/AUTO-MASTERING.md, task #610), which is the release contract's section
	# 3.1 claim for the `mastering.*` group: it starts the real binary, opens the
	# product's OWN fixture (tools/auto-mastering-demo.py, the generator the doc's
	# reproduction section names), masters the session with one project render and
	# five measured candidates, reads every candidate's LUFS-I / true peak / verdict
	# off the wire, and takes the run back twice over - control.undo after a first
	# run leaves the directory empty (the REMOVAL half: a file that never existed
	# cannot be restored), and after a re-run it restores the replaced revision
	# byte for byte, compared by sha256. It also measures the two typed refusals
	# that matter: out_dir must be absolute (junk args write nothing) and a
	# directory holding more than the 64 MiB capture bound is REFUSED rather than
	# performed without an inverse. It reports ctest *Skipped* (exit 77) - never
	# *Passed* - when the tree has no fixture generator to run.
	add_test(NAME ControlMasteringCommands
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-mastering-commands.py
			$<TARGET_FILE:zene>)
	set_tests_properties(ControlMasteringCommands PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 600
		SKIP_RETURN_CODE 77
	)
	# DEFECT-D3: `mastering.run` must be ANSWERED inside the client's own socket
	# bound however long the render behind it takes, and the run must still be
	# completable and readable afterwards. The render child is HELD past 30 s by
	# the product's own test hook (ZENE_MASTER_SLOW_CHILD_MS, src/core/
	# MasteringJob.cpp) so the pair is load-bearing: with the blocking handler
	# reverted this reports NO REPLY inside 30 s - which is exactly what the
	# 2026-09-24 certification sweep measured twice (evidence
	# /tmp/zene-cert-w2r3-rw/run-6413{,-replay}/). It asserts the ACK
	# (state "running", poll mastering.get_state), that control.ping is answered
	# WHILE the run is in flight and a second run is refused busy, that the run
	# reaches `completed` with its own document in last_run and the six wav files
	# on disk, and that no `zene-master-*` scratch is left in the shared temp
	# directory. It reports ctest *Skipped* (exit 77) - never *Passed* - when the
	# tree has no fixture generator to run.
	add_test(NAME ControlMasteringRunBound
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-mastering-run-bound.py
			$<TARGET_FILE:zene>)
	set_tests_properties(ControlMasteringRunBound PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 300
		SKIP_RETURN_CODE 77
	)
	# The ControlDryRunJournal regression of the 2026-09-25 free-form hunt; see the test's own
	# header for the reproduction, the root cause and the red/green pair.
	add_test(NAME ControlDryRunJournal
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-dry-run-journal.py
			$<TARGET_FILE:zene>)
	set_tests_properties(ControlDryRunJournal PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 300
	)
	# The ControlImportLifetime regression of the 2026-09-25 free-form hunt; see the test's own
	# header for the reproduction, the root cause and the red/green pair.
	add_test(NAME ControlImportLifetime
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-import-lifetime.py
			$<TARGET_FILE:zene>)
	set_tests_properties(ControlImportLifetime PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 300
	)
	# The ControlMasteringQuit regression of the 2026-09-25 free-form hunt; see the test's own
	# header for the reproduction, the root cause and the red/green pair.
	add_test(NAME ControlMasteringQuit
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-mastering-quit.py
			$<TARGET_FILE:zene>)
	set_tests_properties(ControlMasteringQuit PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 300
	)
	# The ControlSaveState regression of the 2026-09-25 free-form hunt; see the test's own
	# header for the reproduction, the root cause and the red/green pair.
	add_test(NAME ControlSaveState
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-save-state.py
			$<TARGET_FILE:zene>)
	set_tests_properties(ControlSaveState PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 300
	)
	# Q050-007: the opt-in v2 project container over the control socket of the real
	# binary - ordinary save legacy, project.save {format: "v2"}, project.open
	# auto-detecting both, the app/savecontainerv2 setting; see the test's header.
	add_test(NAME ControlContainerV2
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-container-v2.py
			$<TARGET_FILE:zene>)
	set_tests_properties(ControlContainerV2 PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 300
	)
	# The ControlRenderFormatCapability regression of the 2026-09-25 free-form hunt; see the test's own
	# header for the reproduction, the root cause and the red/green pair.
	add_test(NAME ControlRenderFormatCapability
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-render-format-capability.py
			$<TARGET_FILE:zene>)
	set_tests_properties(ControlRenderFormatCapability PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 300
	)
	# 0.5.0 F7 (MX-060): the surround export through render.render's `channels` and the CLI's
	# --channels - the schema, the typed refusals, a 6-channel WAVE_FORMAT_EXTENSIBLE render on a 5.1
	# master. The PCM content is SurroundExportTest's.
	add_test(NAME ControlRenderSurround
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-render-surround.py
			$<TARGET_FILE:zene>)
	set_tests_properties(ControlRenderSurround PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 300
	)
	# Import detection (0.3.0, feature row 34 of docs/FEATURE-LIST-0.3.0.md): the
	# `detect.*` group driven END TO END through --control-socket, which is the
	# release contract's section 3.1 claim for it. The fixtures are SYNTHESISED by
	# this test with the standard library's own `wave` writer - a click track at
	# exactly 128 BPM whose first click is at 0.500 s, an A major scale over an A
	# bass, and a steady tone for the refusal case. It asserts that detect.analyze
	# recovers those answers and NAMES its methods, that detect.apply writes the
	# tempo into the TEMPO MAP (read back through transport.tempo_map_get, a
	# different verb) and the key into the project's OWN `<detected-key>` field,
	# that project.save carries both into the file, and that ONE control.undo takes
	# the whole detection off. What it does NOT prove is stated in its docstring and
	# in docs/IMPORT-DETECTION.md: a click track is not real music, and no
	# real-world accuracy was measured on this box. No fixture generator to skip on:
	# the fixtures come from the standard library, so this test runs everywhere
	# python3 and the binary do.
	add_test(NAME ControlDetectCommands
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-detect-commands.py
			$<TARGET_FILE:zene>)
	set_tests_properties(ControlDetectCommands PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 600
	)
	# The loudness surface (feature row 24 of docs/FEATURE-LIST-0.3.0.md, "LUFS /
	# loudness metering"), which is the release contract's section 3.1 claim for
	# the `meter.*` group: it starts the real binary, arms the PASSIVE master tap
	# and reads BS.1770-4 integrated / momentary / short-term / true peak off the
	# wire while the tree's own EBU Tech 3341 fixtures play through the engine,
	# measures a rendered file with the same meter, and carries the negative
	# controls the task names - silence reads null (never a plausible number), a
	# signal 10 dB louder reads 10 LU higher both live and from a file, and the
	# audio is byte-identical with the tap attached (the file's sha256 is
	# unchanged by measuring it; two renders of one project have identical PCM
	# with the tap armed and disarmed). It reports ctest *Skipped* (exit 77) -
	# never *Passed* - when the tree has no loudness fixture generator to run.
	add_test(NAME ControlMeterCommands
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-meter-commands.py
			$<TARGET_FILE:zene>)
	set_tests_properties(ControlMeterCommands PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 600
		SKIP_RETURN_CODE 77
	)
	# The golden-audio integration programme (the release's audio evidence; one of
	# the two verification programmes V0.3-ALPHA-PLAN.md names, the other being the
	# real-time-safety one; board task #679). It exists because this tree's renders
	# are NOT bit-reproducible run to run (docs/RENDER-DETERMINISM.md), so sha256
	# byte-identity is not a test - it fails on a correct build - and a single
	# before/after number is not one either. It builds an audible fixture through
	# the commands an agent has, then for EACH headline path - a render
	# (render.render), a stem export (render.stems) and a freeze/bounce
	# (bounce.in_place) - renders it RUNS times, measures the same-build run-to-run
	# FLOOR over EVERY pair of those renders, and judges both that floor and the
	# candidate's fingerprint against the committed record
	# (tests/golden-audio-record.tsv). Then it PROVES the comparison can fail: it
	# moves the fixture track's own fader by a stated dB through mixer.set_volume,
	# re-renders, and requires the check to catch the change - and sweeps the same
	# fader to measure which changes it CANNOT distinguish (the bound it records).
	# It reports ctest *Skipped* (exit 77) - never *Passed* - when this build has no
	# loadable instrument, because an inaudible fixture could not prove any of it.
	# TIMEOUT is generous because EVERY render here runs in a CHILD process and
	# carries the declared RENDER_TIMEOUT budget rather than a raised socket timeout
	# (docs/RENDER-CHILD-WAIT.md): verify mode is 3 renders per path plus the
	# control and the bound sweep, each one a whole engine start.
	add_test(NAME ControlGoldenAudio
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-golden-audio.py
			$<TARGET_FILE:zene>)
	set_tests_properties(ControlGoldenAudio PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 1800
		SKIP_RETURN_CODE 77
	)
else()
	message(STATUS "python3 not found: ControlSocketIntegration is not registered")
endif()

# ---------------------------------------------------------------------------
# Task #626: the shutdown and readiness defects of the control surface.
#
# ControlShutdown         - a clean shutdown for BOTH reproductions (undo/redo
#                           leaves the project modified -> the quit prompt used to
#                           block; an audio device that cannot open -> the startup
#                           audio dialog used to block before app->exec()), plus a
#                           crash report pending AT STARTUP -> the modal offer used
#                           to park the startup path where nobody could click it
#                           (engine_ready stayed false, the quit died on the
#                           shutdown guard).
# ControlReadiness        - connects BEFORE readiness and asserts the documented
#                           order: connect -> poll control.ping -> wait for
#                           engine_ready -> issue engine commands.
# ControlNoAudioDevice    - a started instance with no usable audio device reports
#                           the reason instead of answering `busy` forever.
# ControlNegativeControl  - proves each of those can fail: the pre-fix artefacts
#                           and real SIGKILL/SIGSTOP symptoms must be detected, so
#                           none of the checks above is a tautology.
#
# All four are bounded by ctest's TIMEOUT and by deadlines inside each script: a
# hang counts as a failure, never as a wait.
# ---------------------------------------------------------------------------
if(PYTHON3_EXECUTABLE AND CONTROL_SUITE_AVAILABLE)
	add_test(NAME ControlShutdown
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-shutdown.py
			$<TARGET_FILE:zene>
			${CMAKE_CURRENT_SOURCE_DIR}/data/agent-control-fixture.mmp
	)
	add_test(NAME ControlReadiness
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-readiness.py
			$<TARGET_FILE:zene>
			${CMAKE_CURRENT_SOURCE_DIR}/data/agent-control-fixture.mmp
	)
	add_test(NAME ControlNoAudioDevice
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-no-audio-device.py
			$<TARGET_FILE:zene>
	)
	add_test(NAME ControlNegativeControl
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-negative-control.py
			$<TARGET_FILE:zene>
	)
	add_test(NAME ControlStableIdsSlice2
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-stable-ids-slice2.py
			$<TARGET_FILE:zene>
	)
	# Slice 1's own live acceptance evidence, registered by the coverage-closure
	# lane (task #680). It was the unregistered half of the pair: the slice-2
	# transcript above has been in this file since it landed, while
	# tests/control-stable-ids.py - the transcript SPEC-stable-ids.md cites for
	# the two-instance claims (an id written into the file, a FRESH instance
	# addressing the track by it, deterministic re-assignment for an id-less
	# legacy project, and the upgrade reported rather than silent) - ran nowhere
	# in CI. Registering it changes no assertion in it: the file is unchanged,
	# it starts its instances through the shared harness, and it fails (never
	# skips) when a claim does not hold, which is why it carries no
	# SKIP_RETURN_CODE.
	add_test(NAME ControlStableIds
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-stable-ids.py
			$<TARGET_FILE:zene>
	)
	# The A16 raw transcript (task #623), also registered by task #680: apply ->
	# undo -> read-back for one command of each reversibility class
	# (track.add true_inverse, project.save snapshot, and the not_mutating
	# control). docs/CONTROL-UNDO-CONNECTION-DROP.md cites this file as the
	# evidence for the connection-drop behaviour and no ctest ran it; the C++
	# A16 tests (ReversibilityContractTest / ReversibilityUndoTest) assert the
	# contract's shape and the engine inverses, not this socket transcript.
	add_test(NAME ControlReversibilityTranscript
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-reversibility-transcript.py
			$<TARGET_FILE:zene>
	)
	set_tests_properties(ControlShutdown ControlReadiness ControlNoAudioDevice
		ControlNegativeControl ControlStableIdsSlice2 ControlStableIds
		ControlReversibilityTranscript PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 420
		SKIP_RETURN_CODE 77
	)
else()
	message(STATUS "python3 not found: the #626 shutdown/readiness tests are not registered")
endif()

# ---------------------------------------------------------------------------
# agent_surface (SPEC A15 / task #620). The anti-drift gate: it reflects the
# LIVE menu/toolbar surface over the control socket, fails on any action that
# has no registered command (grandfathering the ones that do not, in
# tests/agent-surface-baseline.txt, under a shrink-only ratchet), checks that
# every declared command is either swept headless or allowlisted with a reason,
# and then sweeps each non-allowlisted command to a typed result under
# QT_QPA_PLATFORM=offscreen with a throwaway HOME.
#
# The gate is a Python file rather than a test binary on purpose: the reflection
# it needs lives in the running process (control.surface_report walks the real
# MainWindow), so the test drives a real instance exactly like an agent would.
# --check is passed explicitly: this gate must never rewrite its own baseline
# during a test run; only a deliberate --reanchor "reason" does that.
# ---------------------------------------------------------------------------
if(PYTHON3_EXECUTABLE AND CONTROL_SUITE_AVAILABLE)
	# -DZENE_TELEMETRY=OFF: the telemetry.* group is compiled out with the
	# client, so this binary has no telemetry.consent for the allowlist entry to
	# name. The gate is told which ids the configuration removed instead of
	# being handed a second allowlist, and it checks the flag in both
	# directions - a name that the registry DOES declare is a failure, so the
	# flag cannot be used to excuse a real command (docs/TELEMETRY-KILL-SWITCH.md).
	if(ZENE_TELEMETRY_ENABLED)
		set(ZENE_AGENT_SURFACE_COMPILED_OUT "")
	else()
		set(ZENE_AGENT_SURFACE_COMPILED_OUT --compiled-out telemetry.consent)
	endif()
	add_test(NAME agent_surface
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/agent-surface-gate.py
			$<TARGET_FILE:zene>
			${CMAKE_CURRENT_SOURCE_DIR}/data/agent-control-fixture.mmp
			--check
			--report ${CMAKE_CURRENT_BINARY_DIR}/agent-surface-report.json
			${ZENE_AGENT_SURFACE_COMPILED_OUT}
	)
	set_tests_properties(agent_surface PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 300
	)
else()
	message(STATUS "python3 not found: agent_surface is not registered")
endif()

# The notes / clips / tracks command group edits real Song model objects
# (tracks, MidiClips, notes) rather than only reading them, so it needs a Qt
# platform plugin even though the assertions never open a window.
set_tests_properties(ControlEditCommandsTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
# The selection-identity test edits real Song model objects (a track, a clip,
# notes) through the registry, so it wants a Qt platform plugin too.
set_tests_properties(NoteSelectionIdentityTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
# The 0.3.0 verb wave's inverse proof: clip.trim / clip.slip / note.probability_set
# edit real Song model objects through the registry and then undo them through
# control.undo, and render.stems' refusals touch the filesystem - so this wants a
# real Qt platform plugin even though nothing opens a window.
set_tests_properties(ControlVerbInverseTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
# The recording route command test starts the recorder and checks its typed
# undo refusal; it needs a Qt platform even though it opens no window.
set_tests_properties(ControlRecordingRoutesTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
# The sample.* first slice (#706) bakes real SampleClip buffers on a real
# Engine and undoes them through control.undo, which wants a real Qt platform
# plugin even though nothing opens a window.
set_tests_properties(ControlSampleOperatorTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(ClipImportFileTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
# The warp.* group edits a SampleClip created through track.add/clip.add, which
# also wants a real Qt platform plugin even though nothing opens a window.
set_tests_properties(ControlWarpCommandsTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
# Pitch-preserving time stretch (row 30): SampleClipStretchTest constructs a full
# Engine, a SampleTrack and real play handles and drives the warp.stretch command
# through the registry, so it wants a real Qt platform plugin even though nothing
# opens a window. AudioStretcherTest needs no Engine (it is pure DSP), but it is
# built from the same objects; it is listed here only to keep the pair together
# in one place whose reason is written down.
set_tests_properties(SampleClipStretchTest SampleClipRubberBandTest SampleClipWarpModesTest TakeLaneCompPlaybackTest AudioStretcherTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
# The link.* group drives the process-wide session model (and the Song's tempo,
# for an adopted one) and starts the UI-thread service timer, so it wants a real
# Qt platform plugin even though nothing opens a window.
#
# Pinned to the zene-link-style fallback (ZENE_LINK_BACKEND, include/LinkSync.h): this
# test proves THAT model - its packet codec, revision order and peer table - and those
# assertions mean nothing for the real Ableton Link backend, which became the default
# when the library was vendored (WF-051). The Ableton backend has its own proof:
# AbletonLinkSessionTest and AbletonLinkInteropTest below.
set_tests_properties(ControlLinkCommandsTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen;ZENE_LINK_BACKEND=zene-link-style")
# Ableton Link joins the multicast group 224.76.78.75:20808, which is shared state by
# design: a Link participant in a concurrently running test is a legitimate peer, so
# these run alone (the same reason ControlLinkSync is RUN_SERIAL).
IF(LMMS_HAVE_ABLETON_LINK)
	set_tests_properties(AbletonLinkSessionTest AbletonLinkInteropTest PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen" RUN_SERIAL TRUE)
	target_link_libraries(AbletonLinkInteropTest PRIVATE ableton_link)
ENDIF()
# The browser.* group: BrowserCatalogTest drives libsndfile and the config
# manager's working directory, ControlBrowserCommandsTest constructs a full
# Engine and drives project-free commands through the registry. Neither opens a
# window, both want a real Qt platform plugin.
set_tests_properties(BrowserCatalogTest BrowserCatalogQueryTest ControlBrowserCommandsTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
# The chain-preset group constructs a full Engine and drives real devices through
# the registry (plugin.load, plugin.param_set, chain.save/apply), so it wants a
# real Qt platform plugin even though nothing opens a window.
# SessionPlaybackTest is a QTEST_MAIN (a QApplication), so it needs a platform like every other one:
# hosted build.yml linux-x86_64 (run 36747963198) aborted it on "could not connect to display",
# where quality-gates.yml passes it only because that job exports QT_QPA_PLATFORM itself.
set_tests_properties(SessionPlaybackTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(ControlChainPresetTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
# The project-asset reference group constructs a full Engine (the relink's
# recorded action checkpoint goes on the project journal) and drives the three
# verbs through the registry, so it wants a real Qt platform plugin even though
# nothing opens a window - the same reason as the three groups above.
set_tests_properties(ControlProjectArchiveTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
# The template save group constructs a full Engine and writes real template files,
# so it needs a Qt platform plugin even though nothing opens a window.
set_tests_properties(ControlProjectTemplateTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
# The transport.tempo_map_* group constructs a full Engine and drives the map
# through the registry (and the engine's undo stack), so it needs a Qt platform
# plugin even though nothing opens a window - the same reason as the two groups
# above. TempoMapTest additionally drives Song::processNextBuffer() itself, so it
# needs the same headless platform.
set_tests_properties(ControlTempoMapCommandsTest TempoMapPersistenceTest TempoMapTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
# The Standard MIDI File conductor interchange constructs a full Engine, drives
# the tempo map through the registry and writes and reads real files in a
# temporary directory, so it wants a real Qt platform plugin even though nothing
# opens a window - the same reason as the tempo-map group above. Both halves of
# the proof need it.
set_tests_properties(SmfInterchangeTest SmfInterchangeRoundTripTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
# The DAWproject round-trip test constructs a full Engine, drives tracks/clips/
# notes through the registry and writes/reads real files, so it needs a Qt
# platform plugin even though nothing opens a window.
set_tests_properties(DawProjectInterchangeRoundTripTest DawProjectDeflateImportTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
# 0.5.0 wave 2: AAF import, the video track and hardware I/O build a real Engine and drive the
# registry (the video window and lane are widgets), so they need a Qt platform plugin.
set_tests_properties(AafImportTest VideoTrackTest LegacyPatternUpgradeTest HardwareIoTest SurroundTest SurroundExportTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
# VideoTrackTest's Linux/macOS allocation-failure case interposes FFmpeg entry points from
# the test executable, so its symbols must be exported - same reason plugin-loading tests carry ENABLE_EXPORTS.
set_target_properties(VideoTrackTest PROPERTIES ENABLE_EXPORTS ON)
# The retrospective MIDI capture surface (owner item 14) builds a real Engine,
# a MIDI client and MidiClips and drives the registry commands, so it needs a
# Qt platform plugin even though nothing opens a window.
set_tests_properties(RetroMidiCaptureCommandsTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
# The engine half of MIDI controller auto-reconnection (0.3.0 row 18) builds a
# real Engine - a MidiPort is a Model, and a model constructed with no Song
# crashes in its own constructor - so it needs a Qt platform plugin even though
# nothing opens a window.
set_tests_properties(MidiReconnectTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
# The stable-id tests construct a full Engine and drive project.open/save through
# the control registry, so they need a Qt platform plugin even though nothing
# opens a window.
set_tests_properties(StableTrackIdsTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
# ProjectRevIdsTest (ARCH-4 S3) is the same shape: a full Engine, save/load/save
# through Song directly, no window.
set_tests_properties(ProjectRevIdsTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
# ProvenanceSectionTest (ARCH-4 S6) is the same shape: a full Engine, mutating
# commands through the registry, save/load/save through Song, no window.
set_tests_properties(ProvenanceSectionTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(ProvenanceVersionTest PROPERTIES
	ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
IF(LMMS_HAVE_JACK)
	set_tests_properties(AudioJackPortRestartTest PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
ENDIF()
IF(LMMS_HAVE_STEM_SPLIT)
	# The controller regression exercises its progress and completion dialogs.
	set_tests_properties(StemSplitPipelineTest PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
ENDIF()
# Headless load path (task #625, SPEC A13/A15): no modal dialog may block an
# instance started with --control-socket.
#
# Each test starts the real binary under QT_QPA_PLATFORM=offscreen, drives it
# from an external client, and treats a HANG as a failure: every wait is
# bounded. Each also has an `--expect-blocked` mode that asserts the pre-fix
# defect (the startup path parked inside a modal's nested event loop), so the
# tests cannot pass by accident. Reproductions against the base commit
# 6b01b98eb are recorded in the lane's evidence.
# ---------------------------------------------------------------------------
if(PYTHON3_EXECUTABLE AND CONTROL_SUITE_AVAILABLE)
	foreach(HEADLESS_TEST
			"ControlHeadlessProjectOpen|control-headless-project-open.py|${CMAKE_SOURCE_DIR}/data/projects/tutorials/editing_note_volumes.mmp ${CMAKE_CURRENT_SOURCE_DIR}/data/error-carrying-project.mmp"
			"ControlHeadlessWorkingDirectory|control-headless-working-directory.py|"
			"ControlHeadlessNoAudioDevice|control-headless-no-audio-device.py|")
		string(REPLACE "|" ";" FIELDS "${HEADLESS_TEST}")
		list(GET FIELDS 0 TEST_NAME)
		list(GET FIELDS 1 TEST_SCRIPT)
		list(GET FIELDS 2 TEST_ARGS)
		separate_arguments(TEST_ARGS_LIST UNIX_COMMAND "${TEST_ARGS}")
		add_test(NAME ${TEST_NAME}
			COMMAND ${PYTHON3_EXECUTABLE}
				${CMAKE_CURRENT_SOURCE_DIR}/${TEST_SCRIPT}
				$<TARGET_FILE:zene>
				${TEST_ARGS_LIST}
		)
		set_tests_properties(${TEST_NAME} PROPERTIES
			ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
			TIMEOUT 300
		)
	endforeach()
endif()

# ---------------------------------------------------------------------------
# ControlSocketPathSafety (data loss, 2026-09-12). `--control-socket <path>` used
# to UNLINK whatever was already at <path> before bind(), so pointing it at a
# project destroyed that project and said nothing (reproduced on the release
# configuration, commit ba24a9578). The test starts the real binary with the path
# already occupied - a regular file, a directory, a symlink, a stale socket - and
# asserts the typed refusal the process now writes to stderr (the surface's own
# wire shape, since an instance that refused to start has no socket to answer on),
# that the file is byte-identical afterwards, that a stale socket is still
# replaced, and that a request line over ControlServer::MaxRequestLineBytes is
# refused instead of buffering without bound.
# See docs/CONTROL-SOCKET-PATH-SAFETY.md.
# ---------------------------------------------------------------------------
if(PYTHON3_EXECUTABLE AND CONTROL_SUITE_AVAILABLE)
	add_test(NAME ControlSocketPathSafety
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-socket-path-safety.py
			$<TARGET_FILE:zene>
	)
	set_tests_properties(ControlSocketPathSafety PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 300
	)

	# The sibling case above sends an over-cap line with NO newline, which the
	# never-ended branch already refused. This pins the shape that survived: a valid
	# request padded past MaxRequestLineBytes and then TERMINATED, so the line is
	# COMPLETE when the read loop stops appending. Both transports dispatched it.
	add_test(NAME ControlOversizedRequestLine
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-oversized-request-line.py
			$<TARGET_FILE:zene>
	)
	set_tests_properties(ControlOversizedRequestLine PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 300
	)
endif()

# ---------------------------------------------------------------------------
# ControlAbsentSocket (0.2.1 coverage gap 3b). The release notes claim the
# control surface's opt-in is INVISIBLE: "an instance that was not started that
# way has no socket at all". Every other test in this directory starts the binary
# WITH `--control-socket`, so nothing checked the other direction.
#
# The test starts the real binary without the flag and watches what it holds:
# /proc/<pid>/fd against /proc/net/unix answers "which AF_UNIX sockets with a
# filesystem path does this process have open right now", sampled every 20 ms for
# a window measured from a positive control (the same binary WITH the flag, whose
# time-to-socket this test records first, so the window is not a guess), plus a
# recursive S_ISSOCK scan of the instance's own directory. It then asks the
# instance to quit (SIGINT, the product's documented shutdown request for a GUI
# run) and asserts the shutdown was clean: session marker removed, no crash
# report, the product's own SIGINT exit code.
#
# Registered where the OTHER python tests that spawn the binary are registered
# (python3 AND a UNIX-like platform): the script spawns `zene`, and the tag it
# checks is platform-independent, so nothing false is claimed by its absence on
# Windows - this test simply does not run there, and docs/STATUS.md's vocabulary
# is what keeps that visible.
# ---------------------------------------------------------------------------
if(PYTHON3_EXECUTABLE AND CONTROL_SUITE_AVAILABLE)
	add_test(NAME ControlAbsentSocket
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-absent-socket.py
			$<TARGET_FILE:zene>
	)
	set_tests_properties(ControlAbsentSocket PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 300
		# The probe reads held AF_UNIX sockets from /proc where it exists and from
		# `lsof -a -p <pid> -U -Fn` where it does not (Darwin), and SKIPs with a
		# stated reason when a host can do neither - an empty probe is not evidence
		# that the product holds no socket. 77 is what ctest reports as Skipped;
		# without this property the skip would be reported as a failure.
		SKIP_RETURN_CODE 77
	)
endif()

# ---------------------------------------------------------------------------
# RenderSoftwareTag (0.2.1 coverage gap 3c). The release notes claim the WAV
# files this build renders carry `Zene Studio (libsndfile-…)` in their software
# tag "where they used to credit LMMS"; the only witness was a committed log of a
# render parsed by hand once (tests/integration-logs-3d/final/chunk-parse.log).
#
# This renders a short project headlessly with the real binary and reads the tag
# out of the output's LIST/INFO/ISFT chunk, with a method control (a WAV the
# script writes itself carrying the PRE-FIX tag must be parsed and REJECTED, so a
# parser that finds nothing cannot pass). LMMS_PLUGIN_DIR is passed explicitly so
# the render does not depend on the caller's environment. Gated like the other
# scripts that spawn the binary; see the script's own header for what it does NOT
# cover (the FLAC writer's twin call).
# ---------------------------------------------------------------------------
if(PYTHON3_EXECUTABLE AND CONTROL_SUITE_AVAILABLE)
	add_test(NAME RenderSoftwareTag
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/render-software-tag.py
			$<TARGET_FILE:zene>
			${CMAKE_BINARY_DIR}/plugins
			${CMAKE_SOURCE_DIR}/data/projects/tutorials/editing_note_volumes.mmp
	)
	set_tests_properties(RenderSoftwareTag PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 300
	)
endif()
# ControlCommandsSnapshot - the MCP tooling spine's freshness ratchet.
#
# `tools/mcp-zene-control/zene_control/registry.py` generates the bridge's tool
# list from a live instance's `control.commands_list` AT REQUEST TIME, so a new
# command group is automatically an MCP tool with no per-feature bridge work.
# The mode with no live instance (and no cache) is served from the committed
# snapshot `zene_control/commands_snapshot.json`, and nothing enforced that the
# snapshot was fresh: tests/test_units.py asserted only that the file existed and
# held five sample ids. It had drifted to 70 commands while the registry declared
# 108 - 38 missing, every one a 0.3.0 feature - so tooling for the new work
# looked absent in exactly the mode a reader tries first.
#
# This test is that ratchet, in the ControlSessionLaunch mould: it starts
# $<TARGET_FILE:zene> with --control-socket, asks it for
# control.commands_list, loads the committed snapshot through the bridge's own
# loader (`zene_control.registry.load_bundle`) and fails on drift in EITHER
# direction, naming the ids and the exact regeneration command.
#
# IT ALSO CHECKS WHAT THE BRIDGE ACTUALLY EXPOSES, which the snapshot comparison
# alone did not (2026-09-13, 030/mcp-coverage). The registered ratchet compared
# the snapshot FILE with the binary; the bridge, with no instance running, used to
# serve its offline copies in a fixed order - cache first - so a cache written by
# an older instance shadowed a newer committed snapshot for good, and the ratchet
# stayed green while the served list was something else entirely. Measured on this
# tree (COVERAGE-MATRIX-2026-09-13.md §4.4): a 70-id 0.1.0-alpha cache left 74 ids
# of the current surface unreachable with no per-feature bridge work outstanding.
# So the test now asks `zene_control.tools.Bridge` what it would expose, in three
# modes - with the instance answering, with an empty state directory, and with a
# synthetic cache captured 2000-01-01T00:00:00Z planted in it - and fails on any
# id the DAW registers and the bridge offers no tool for, in any mode. The
# freshest readable offline copy must win (`registry.rank_offline_bundles`), and
# the stale-cache mode is the guard on that rule.
#
# Registered on the same gate as every other Python control-surface test here:
# python3 AND CONTROL_SUITE_AVAILABLE (POSIX only). Its live half starts a real
# `zene` and drives it over the AF_UNIX control socket, which the server does not
# carry on Windows - ControlServer::listen's !Q_OS_UNIX branches refuse, and
# Client::onNewConnection returns before accept() - so on Windows there is no
# socket for this test to compare against. It was the one test of the family left
# out of that gate, and run #34802239065 showed the cost: msvc-x64 failed it in
# 2.58 s with FileNotFoundError [WinError 3] from the harness's POSIX temp path
# (control_socket_harness.py: tempfile.mkdtemp(prefix="zctl-run-", dir="/tmp")),
# which names a harness path and not the platform fact this comment records. The
# Windows refusal path stays covered by the unit tests that need no socket, and
# POSIX coverage is unchanged. It is still SKIPPED (exit 77 -> ctest "Skipped",
# never "Passed") when the bridge package itself cannot be imported, so it can
# never pass by doing nothing. There is no WANT_* option that gates the bridge: it
# is Python under tools/, not a build target.
#
# The options that move a whole command group at compile time are passed in
# as prefixes, so a non-release configuration is compared against the ids it could
# actually register instead of being failed for the options it chose:
# ZENE_TELEMETRY=OFF drops telemetry.* (`--compiled-out`, because the snapshot
# was captured from the release configuration, which has it), and WANT_WASM=ON
# with the wasmtime C API on the find path ADDS the wasm.* group (item #614) -
# eight ids the release snapshot
# cannot carry, because the snapshot is one file and a configuration is not, so
# that one is `--compiled-in`. Every flag is checked in BOTH directions - a prefix
# declared compiled out that the binary DOES register is a failure, and one
# declared compiled in that the binary does NOT register (or that the snapshot
# already carries) is a failure too - so no flag can become a blindfold.
# TELEMETRY defaults ON (the configuration the snapshot was captured from);
# WANT_WASM defaults ON but degrades to OFF without the wasmtime C
# API, which is every CI job, so on a default build the sandbox's ids are absent
# on both sides and this test compares the release surface in full.
# ---------------------------------------------------------------------------
if(PYTHON3_EXECUTABLE AND CONTROL_SUITE_AVAILABLE)
	set(ZENE_SNAPSHOT_COMPILED_OUT "")
	if(NOT ZENE_TELEMETRY_ENABLED)
		list(APPEND ZENE_SNAPSHOT_COMPILED_OUT --compiled-out telemetry.)
	endif()
	# The stem.* group (feature row 26, board task #653): since owner decision 14
	# WANT_STEM_SPLIT defaults ON, so its seven ids ARE the release surface and the
	# snapshot carries them. It moved from --compiled-in to telemetry's direction:
	# only a build configured with it OFF is excused, and the same two-way check
	# fails such a build if it registers stem.* anyway.
	if(NOT LMMS_HAVE_STEM_SPLIT)
		list(APPEND ZENE_SNAPSHOT_COMPILED_OUT --compiled-out stem.)
	endif()
	set(ZENE_SNAPSHOT_COMPILED_IN "")
	if(WANT_WASM)
		list(APPEND ZENE_SNAPSHOT_COMPILED_IN --compiled-in wasm.)
	endif()
	add_test(NAME ControlCommandsSnapshot
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-commands-snapshot.py
			$<TARGET_FILE:zene>
			${ZENE_SNAPSHOT_COMPILED_OUT}
			${ZENE_SNAPSHOT_COMPILED_IN}
	)
	set_tests_properties(ControlCommandsSnapshot PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 300
		SKIP_RETURN_CODE 77
	)
	# mmpz-git depth (feature row 42, task #612): the Python test suite for the
	# merge driver, diff, conflict reporter and audible-diff CLI.  It needs no
	# running instance: it operates on project files directly.  The binary-
	# dependent classes (BinarySafety, AudibleDiffBinary) skip when build/zene
	# is absent, exactly as the pre-existing Qt6 test skips without scratch/qtsave2.
	add_test(NAME MmpzGitDepthTest
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_SOURCE_DIR}/tools/mmpz-git/tests/test_mmpz_git.py -v)
	# TIMEOUT 1800, not the 300 it carried (030/wplat-arm64, 2026-09-17): 300 was an x86
	# number.  The suite drives the DAW SEVEN times - 2 `_loads` renders, 2 x 2 audible-diff
	# renders (render_mix, and render_tracks, are one invocation per project) and the render
	# recipe's 1 - and on the linux-arm64 runner each render costs ~45 s (a full
	# Engine::init plus the audio, docs/RENDER-CHILD-WAIT.md): run 35126160372 measured the
	# five DAW-driving tests at 90/88/45/45 s, the fifth still running when the 300.03 s
	# bound hit, while linux-x86_64 passes the whole suite in 139.41 s.  The number is sized
	# so this backstop cannot pre-empt an internal bound: 7 bounded DAW calls x the suite's
	# own declared 180 s per-render budget = 1260 s, plus the suite's non-DAW work (measured
	# 85.3 s here; allow 3x on the slow runner) = ~1520 s.  Every DAW-driving call inside the
	# suite carries its own subprocess bound, so a hang fails THERE, named, and this TIMEOUT
	# is the backstop for a hang outside those calls only.
	set_tests_properties(MmpzGitDepthTest PROPERTIES
		TIMEOUT 1800
	)
else()
	message(STATUS "python3 not found: ControlCommandsSnapshot is not registered")
endif()

# ---------------------------------------------------------------------------
# MmpzV2CliRefusal (ARCH-4 S2c part 2): the `.mmpz` command-line verbs tell the
# two container formats apart by CONTENT and REFUSE a v2 container BY NAME.
#
# Registered wherever python3 exists, unlike the control-surface suite above it:
# `--dump` and `--compress` are verbs of the binary itself, they return before
# the engine comes up and they carry no AF_UNIX assumption, so there is no
# platform on which this check has to be absent. The defects it pins were
# MEASURED on this branch with a hand-built v2 container: `--dump` wrote one byte
# of stdout - a bare newline - and returned EXIT_SUCCESS, so a script reading it
# got an empty document and a zero exit; `--compress` wrapped the container's ZIP
# inside v1's qCompress frame, producing a file neither format reads back. Both
# were silent, and the second is a WRITER that loses the container it was given.
# ---------------------------------------------------------------------------
if(PYTHON3_EXECUTABLE AND TARGET zene)
	add_test(NAME MmpzV2CliRefusal
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/mmpz-v2-container-refusal.py
			$<TARGET_FILE:zene>
	)
	set_tests_properties(MmpzV2CliRefusal PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 300
	)
endif()

# ---------------------------------------------------------------------------
# The zene::api boundary's proof (ARCH-2, board card #672). The boundary target
# (src/CMakeLists.txt: zene_api) makes a widget include in the registry a compile
# error; this test re-measures the same property on the artifacts the build just
# produced, so a regression that PASSES the build - Qt6::Widgets linked back onto
# the target, the module-qualified include form, a widget symbol entering
# through an include that stays - fails here, in the test step. Every check
# carries a liveness control: the same measurement on src/gui/MainWindow.cpp,
# which must show widget reachability, so no check can pass vacuously.
if(PYTHON3_EXECUTABLE AND TARGET zene_api)
	add_test(NAME ZeneApiBoundary
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/zene-api-boundary.py
			--build-dir ${CMAKE_BINARY_DIR}
			--object-dir ${CMAKE_BINARY_DIR}/src/CMakeFiles/zene_api.dir
			--source-list ${CMAKE_BINARY_DIR}/zene-api-sources.txt
			--control-source ${CMAKE_SOURCE_DIR}/src/gui/MainWindow.cpp
			# The object suffix is the toolchain's own (.obj under MSVC and MSYS2 CLANGARM64 - hosted
			# windows-arm64, run 36747963198, found no MainWindow.cpp.o), not a spelling of ours.
			--control-object ${CMAKE_BINARY_DIR}/src/CMakeFiles/lmmsobjs.dir/gui/MainWindow.cpp${CMAKE_CXX_OUTPUT_EXTENSION}
	)
	set_tests_properties(ZeneApiBoundary PROPERTIES
		TIMEOUT 600
	)
else()
	message(STATUS "python3 not found: ZeneApiBoundary is not registered")
endif()
# ---------------------------------------------------------------------------
# The bridge's GROUP COVERAGE, driven rather than listed:
# tests/control-mcp-group-coverage.py starts this build's binary, opens a real MCP
# stdio session to tools/mcp-zene-control/server.py over its control socket, and
# drives one command from EACH of the ten command groups feature-list row 49
# measured as having no MCP tool (browser, comp, export, link, modulator, rack,
# session, telemetry, warp, wasm). It also asserts the stale-offline-list flag
# fires exactly when the live and offline id sets differ, so a staleness report
# that cannot be wrong in either direction is checked in both.
#
# The three compile-time options that move a whole group are passed in the same
# way ControlCommandsSnapshot passes them - a group this configuration does NOT
# compile in is excused, and every flag is checked in BOTH directions (a prefix
# declared compiled out that the binary DOES register is a failure, so no flag
# can become a blindfold):
#   - WANT_WASM=ON with the wasmtime C API on the find path adds wasm.* (item
#     #614). Anywhere the sandbox degrades to OFF, wasm. is declared compiled
#     out; a wasm-enabled build gets no flag and must drive the group for real.
#     The bridge's own half of that case is
#     tools/mcp-zene-control/tests/test_declared_surface.py.
#   - ZENE_TELEMETRY=OFF drops telemetry.* (it defaults ON); the session.*
#     group is native engine code since ARCH-4 S10 and is never compiled out.
#
# SKIPPED (exit 77 -> ctest "Skipped", never "Passed") when no interpreter with
# the `mcp` distribution can be found: the bridge's stdio server is the thing
# being driven, so without it this check cannot look at anything.
# ---------------------------------------------------------------------------
if(PYTHON3_EXECUTABLE)
	set(ZENE_MCP_COMPILED_OUT "")
	if(NOT ZENE_TELEMETRY_ENABLED)
		list(APPEND ZENE_MCP_COMPILED_OUT --compiled-out telemetry.)
	endif()
	if(NOT WANT_WASM)
		list(APPEND ZENE_MCP_COMPILED_OUT --compiled-out wasm.)
	endif()
	add_test(NAME ControlMcpGroupCoverage
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-mcp-group-coverage.py
			$<TARGET_FILE:zene>
			${ZENE_MCP_COMPILED_OUT}
	)
	set_tests_properties(ControlMcpGroupCoverage PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 600
		SKIP_RETURN_CODE 77
	)
else()
	message(STATUS "python3 not found: ControlMcpGroupCoverage is not registered")
endif()

# board card #708: the livecode.* transcript - the registered proof that a
# script scheduled ONCE edits a mixer value on every bar with no re-trigger,
# that the EXPLICIT v0 trigger still runs an existing-shape script unchanged,
# and that a runaway hits its typed instruction budget while the audio keeps
# running. This is this lane's own block, appended as a whole: the named
# insertion anchor for this file (the ScriptEngineTest entry in set(LMMS_TESTS)
# above) cannot hold an add_test() call.
# AF_UNIX clients: registered where the control suite is (see CONTROL_SUITE_AVAILABLE).
if(PYTHON3_EXECUTABLE AND CONTROL_SUITE_AVAILABLE)
	add_test(NAME ControlLivecodeCommands
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-livecode-commands.py
			$<TARGET_FILE:zene>
			${CMAKE_SOURCE_DIR}/data/scripts/create-pattern.lua
	)
	set_tests_properties(ControlLivecodeCommands PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 600
	)
	# The grid test opens no audio device but builds a QApplication: offscreen,
	# the same property the engine QTests carry.
	set_tests_properties(ScriptClockTest PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
	)
else()
	message(STATUS "python3 not found: ControlLivecodeCommands is not registered")
endif()

# BUG-CTL-2 / BUG-SRV-1 / BUG-SRV-2 (lane bugs/hunt-2026-09-24): the control
# surface's own input validation and connection bounds, driven over
# --control-socket against the real binary. Four cases, each the regression test
# for one fix, each measured on the wire because that is where the defect was:
#   * BUG-CTL-2 (src/core/ControlDeviceSupport.cpp) - a channel target that is not
#     ch-<index> is invalid_args; a well-formed but absent one stays not_found.
#     Driven through two channel-targeting commands (plugin.param_get, chain.save)
#     that resolve the target before they touch a device.
#   * BUG-SRV-1 (src/core/ControlServer.cpp) - a request `id` that is not a finite
#     signed 32-bit integer is refused invalid_args and the instance serves on.
#   * BUG-SRV-2 - past MaxClients (64) the excess peers are closed at once rather
#     than left in the listener backlog, and a request line carries an absolute
#     30 s deadline that a partial read does NOT extend while a completed exchange
#     does. The deadline case costs ~35 s of wall clock: the product has no
#     shortened override, so expiry is observed by waiting for it.
# It needs no plugin module (every refusal under test happens before a device is
# touched), and it starts the REAL binary, so nothing it writes escapes the
# instance's own temp HOME. This is this lane's own block, appended as a whole.
# AF_UNIX clients: registered where the control suite is (see CONTROL_SUITE_AVAILABLE).
if(PYTHON3_EXECUTABLE AND CONTROL_SUITE_AVAILABLE)
	add_test(NAME ControlSurfaceHardening
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-surface-hardening.py
			$<TARGET_FILE:zene>)
	set_tests_properties(ControlSurfaceHardening PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 300
	)
else()
	message(STATUS "python3 not found: ControlSurfaceHardening is not registered")
endif()

# One published figure (040/m1-contract, relief plan R6.5): every document that claims the CURRENT
# command-surface figure marks it <!-- canary -->N ids / M groups<!-- /canary -->, and this checks
# each against the committed snapshot. tests/doc-figures.txt lists the docs that must carry one.
if(PYTHON3_EXECUTABLE)
	add_test(NAME DocFiguresGate
		COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/doc-figures-gate.py)
endif()

# Q050-308 (R-workflow-117): the MIDI Clock docs must describe BOTH routes to source_port and
# drift_bound_ms - Edit > MIDI Clock > Clock Source, and Project tools > Sync > Follow MIDI Clock...
# (the clock.slave_set form) - and no current page may still call either field socket-only. The gate
# reads the two GUI files, the schema, the committed snapshot and the docs/manual; file reads only, so
# it needs no binary. MidiClockDocsGateSelfTest is the gate's own red/green control: the stale
# sentence back, the Sync route dropped, a drift bound that disagrees with the schema, or a manual
# that stopped naming the form must each fail the gate.
if(PYTHON3_EXECUTABLE)
	add_test(NAME MidiClockDocsGate
		COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/midi-clock-docs-gate.py)
	add_test(NAME MidiClockDocsGateSelfTest
		COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/midi-clock-docs-gate.py
			--self-test)
endif()

# Q050-014 (R-instruments-11 / -26 / R-ui-19): the docs must keep format LOADING apart from tested
# third-party compatibility (a Kontakt library is the external, unverified case) and must name the
# Plug-in Modules list as the GUI location for per-file scan status and quarantine controls. The gate
# reads the docs, the GUI code that defines that list and the committed command snapshot; file reads
# only, so it needs no binary. It scopes each advertised Linux format to the README plug-in-formats
# paragraph and the KNOWN-LIMITATIONS Linux section (not a document-wide substring) and checks that
# the mapped loader source registers the format. PluginFormatsDocsGateSelfTest is the gate's own
# red/green control: a format dropped from a scoped list, other mentions intact, must fail the gate.
if(PYTHON3_EXECUTABLE)
	add_test(NAME PluginFormatsDocsGate
		COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/plugin-formats-docs-gate.py)
	add_test(NAME PluginFormatsDocsGateSelfTest
		COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/plugin-formats-docs-gate.py
			--self-test)
	# Q050-484: sfizz's render callback runs no file-pool garbage collection (the symbol table of the built library says so).
	if(TARGET sfzplayer_sfizz AND NOT MSVC)
		add_custom_target(SfzRenderThreadGateDeps)
		add_dependencies(SfzRenderThreadGateDeps sfzplayer_sfizz)
		add_test(NAME SfzRenderThreadGate
			COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/sfz-render-thread-gate.py $<TARGET_FILE:sfzplayer_sfizz>)
		set_tests_properties(SfzRenderThreadGate PROPERTIES SKIP_RETURN_CODE 77)
	endif()

	# Q050-071: the published third-party compatibility page is what was measured - subjects pinned by package and file
	# hash, one row per subject, four distinct outcomes, third-party rows apart from the in-tree fixtures, the page
	# rendered from the results byte for byte. File reads only. The self-test is its red control.
	add_test(NAME PluginCompatMatrixGate
		COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/plugin-compat-matrix-gate.py --root ${CMAKE_SOURCE_DIR})
	add_test(NAME PluginCompatMatrixGateSelfTest
		COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/plugin-compat-matrix-gate.py --root ${CMAKE_SOURCE_DIR}
			--self-test)
endif()

# Q050-145: the README's no-tier / no-activation statement stays verifiable - the gate
# repeats the wording checks the section claims (GPL cited as the licence that permits
# charging, the no-tier claim grounded in the search of the product-owned sources and
# the distribution facts, no --version policy promise, announcement channel named) and
# reruns the narrow product-licensing source search with the documented exclusions.
if(PYTHON3_EXECUTABLE)
	add_test(NAME DocPricingModelGate
		COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/doc-pricing-model-gate.py)
endif()

# Q050-035 (R-platform-26): the docs must agree with the build that offline HTDemucs stem
# separation is built in, local and ON by default. tests/KNOWN-LIMITATIONS.md used to say the
# feature was compiled out; this reads the CMake default, the release-honesty manifest row and
# the user-facing docs and fails on any stale "compiled out"/"OFF" claim. File reads only.
if(PYTHON3_EXECUTABLE)
	add_test(NAME StemAvailabilityDocsGate
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/stem-availability-docs-gate.py)
endif()

# Q050-148 (docs lane): the warp docs must keep stating the three warp.stretch modes -
# the pitch-moving resample default beside the pitch-preserving preserve_pitch/rubberband -
# with the per-algorithm caveats and the runnable reviewer example, and must not bring the
# old "pitch-preserving stretch is not built" blanket claims back as current statements.
# Offline (reads docs and the transcript script; no binary, no socket).
if(PYTHON3_EXECUTABLE)
	add_test(NAME WarpDocsAgree
		COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/warp-docs-agree.py)
endif()

# 0.5.0 F1 (MX-040/041/036/061): hardware outputs, interface channels, cue mixes and the hardware
# insert ping driven end to end on a 4-output Dummy whose loopback carries outputs 3/4.
if(PYTHON3_EXECUTABLE)
	add_test(NAME ControlHardwareIo
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-hardware-io.py
			$<TARGET_FILE:zene>)
	set_tests_properties(ControlHardwareIo PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 300
	)
endif()

# 0.5.0 F6 (WF-050/WF-052): two real instances on 127.0.0.1 - host, a local invite, join, edits both
# ways, an undo re-sync and a refused viewer. Exits 77 (skipped) where the TLS-PSK backend is missing.
target_compile_definitions(CollabTransportTest PRIVATE
	COLLAB_HOST_SOURCE_PATH="${CMAKE_SOURCE_DIR}/src/core/CollabHost.cpp"
	COLLAB_SESSION_SYNC_SOURCE_PATH="${CMAKE_SOURCE_DIR}/src/core/CollabSessionSync.cpp")
if(PYTHON3_EXECUTABLE AND ZENE_HAVE_COLLAB)
	set_tests_properties(CollabPolicyTest CollabTransportTest PortMappingCodecTest PortMappingOrchestratorTest UpnpMapperFakeRouterTest PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
	add_test(NAME ControlCollab
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/control-collab.py
			$<TARGET_FILE:zene>)
	set_tests_properties(ControlCollab PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 300
		RUN_SERIAL TRUE
		SKIP_RETURN_CODE 77
	)
endif()

# LADSPA channel-count regression: the module's port names are positional and
# its control input includes "Input" to ensure only audio ports count.
set(LADSPA_CHANNEL_FIXTURE_DIR "${CMAKE_CURRENT_BINARY_DIR}/ladspa-channel-fixture")
add_library(ladspa_channel_count_fixture MODULE src/plugins/LadspaChannelCountFixture.cpp)
target_include_directories(ladspa_channel_count_fixture PRIVATE "${CMAKE_SOURCE_DIR}/include")
target_compile_features(ladspa_channel_count_fixture PRIVATE cxx_std_20)
if(MSVC)
	target_link_options(ladspa_channel_count_fixture PRIVATE /EXPORT:ladspa_descriptor)
endif()
set_target_properties(ladspa_channel_count_fixture PROPERTIES
	LIBRARY_OUTPUT_DIRECTORY "${LADSPA_CHANNEL_FIXTURE_DIR}"
	RUNTIME_OUTPUT_DIRECTORY "${LADSPA_CHANNEL_FIXTURE_DIR}"
)
target_compile_definitions(LadspaChannelCountsTest PRIVATE
	LADSPA_CHANNEL_FIXTURE_PATH="$<TARGET_FILE:ladspa_channel_count_fixture>"
)
add_dependencies(LadspaChannelCountsTest ladspa_channel_count_fixture)

# The NAM engine's local CTest registrations live with its headless harness.
# Mirror those cases here so ctest launched from build/tests discovers them.
get_property(NAM_LOADER_TEST_CASES GLOBAL PROPERTY ZENE_NAM_LOADER_TEST_CASES)
if(TARGET nam_harness AND NAM_LOADER_TEST_CASES)
	foreach(NAM_LOADER_TEST_CASE IN LISTS NAM_LOADER_TEST_CASES)
		add_test(NAME "NamLoaderTest_${NAM_LOADER_TEST_CASE}"
			COMMAND $<TARGET_FILE:nam_harness> --test-loader-config "${NAM_LOADER_TEST_CASE}")
	endforeach()
endif()

# The WAV-run diagnostic must compare the model rate with the WAV's own rate.
if(TARGET nam_harness AND PYTHON3_EXECUTABLE)
	add_test(NAME NeuralAmpWavRunSampleRateWarning
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_CURRENT_SOURCE_DIR}/neuralamp-wav-run-sample-rate-warning.py
			$<TARGET_FILE:nam_harness>)
	set_tests_properties(NeuralAmpWavRunSampleRateWarning PROPERTIES TIMEOUT 30)
endif()

# Q050-024: LinkAudioStreamTest runs Ableton Link (confined to loopback, but it still binds the
# group port 20808 on lo), so it must not overlap a test that multicasts on 224.76.78.75:20808.
set_tests_properties(LinkAudioStreamTest PROPERTIES RUN_SERIAL TRUE)
# BUG new-p9-315: exercise the NAM harness's generated-signal and WAV rate
# validation through its command line using a small deterministic model fixture.
if(PYTHON3_EXECUTABLE AND TARGET nam_harness)
	add_test(NAME NeuralAmpHarnessRateValidation
		COMMAND ${PYTHON3_EXECUTABLE}
			${CMAKE_SOURCE_DIR}/plugins/NeuralAmp/tests/nam_harness_rate_test.py
			$<TARGET_FILE:nam_harness>)
	set_tests_properties(NeuralAmpHarnessRateValidation PROPERTIES TIMEOUT 60)
endif()

# Regression for the NeuralAmp harness's generated-run block-count overflow.
# The test drives the separately built, headless harness with a minimal valid
# .nam model, an oversized block count and the counts around it. The test
# executable depends on the nam_harness target on purpose: building the test
# then always rebuilds the harness it exercises, so the harness under test can
# never be a stale binary that hides a change in its own source.
#
# The source file is named after the test (not "<name>Test.cpp") so that the
# file name, the target and the ctest name are one thing.
if(TARGET nam_harness)
	add_executable(NeuralAmpHarnessBlockCount src/plugins/NeuralAmpHarnessBlockCount.cpp)
	add_test(NAME NeuralAmpHarnessBlockCount COMMAND NeuralAmpHarnessBlockCount)
	target_compile_features(NeuralAmpHarnessBlockCount PRIVATE cxx_std_20)
	target_link_libraries(NeuralAmpHarnessBlockCount PRIVATE ${QT_LIBRARIES} ${QT_QTTEST_LIBRARY})
	target_compile_definitions(NeuralAmpHarnessBlockCount PRIVATE
		"NAM_HARNESS_EXECUTABLE=\"$<TARGET_FILE:nam_harness>\"")
	add_dependencies(NeuralAmpHarnessBlockCount nam_harness)
	set_tests_properties(NeuralAmpHarnessBlockCount PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 60)
endif()
