# MIDI input support

## Status

MIDI 2.0 / Universal MIDI Packet (UMP) input is not yet supported.

Planned items are not available, scheduled or dated. Today's input is MIDI 1 only.

## Current input path (MIDI 1)

**Event vocabulary.** The event types are the MIDI 1 status bytes (note off/on, key pressure, control change, program change, channel pressure, pitch bend, system common and real-time, SysEx, meta). There are no UMP message types (`include/Midi.h:33-61`).

There are two input paths.

1. **Byte-stream path (raw clients).** `MidiClientRaw` (`include/MidiClient.h:231`) parses MIDI 1 bytes in `MidiClientRaw::parseData` (`src/core/midi/MidiClient.cpp:142`). It keeps running status and resets the byte counter (`MidiClient.cpp:258-269`), sets the event type and channel from the status byte (`:267-268`), and switches on the type (`:270`). Four backends feed bytes into it:
   - ALSA rawmidi: `src/core/midi/MidiAlsaRaw.cpp:265`
   - OSS: `src/core/midi/MidiOss.cpp:293`
   - sndio: `src/core/midi/MidiSndio.cpp:225`
   - JACK: `src/core/midi/MidiJack.cpp:206`

2. **Decoded-event path.** `MidiAlsaSeq` (`include/MidiAlsaSeq.h:54`) reads ALSA sequencer events. `run()` starts at `src/core/midi/MidiAlsaSeq.cpp:869`, `snd_seq_event_input` is called at `:943`, and the switch on `ev->type` starts at `:1012`. For example, `SND_SEQ_EVENT_NOTEON` becomes `MidiNoteOn` and is passed to `processInEvent` (`:1014-1015`). `MidiWinMM` (`include/MidiWinMM.h:46`) and `MidiApple` (`include/MidiApple.h:46`) also derive from `MidiClient` directly, and each decodes MIDI 1 channel messages into `MidiEvent` itself. WinMM: the `MIM_DATA` callback is `src/core/midi/MidiWinMM.cpp:179`, and `MidiWinMM::handleInputEvent` (`:190`) builds `MidiEvent` at `:213-223` for note, key pressure, control change, program change, channel pressure and pitch bend. Active sensing returns early (`:193`), and any other status is logged with `qWarning` (`:226-228`). Apple: the CoreMIDI `MIDIPacketList` read callback is `src/core/midi/MidiApple.cpp:228` and `:242`. The status-byte switch is at `:333`, and `MidiEvent` is built at `:341-348` for the same channel-message types. SysEx is copied into a buffer and not dispatched (`:264-277`, `:300-313`).

## Planned: UMP input

PLANNED. Not built, not scheduled. The ALSA path is the planned route.

The following are **not defined** in this repository. None is decided here, and none should be read as a commitment:

- **Scope:** ALSA sequencer, ALSA rawmidi, or both.
- **Message families:** which UMP message types are handled.
- **Platform floor:** the minimum ALSA library or kernel version for UMP.
- **MPE per-note expression:** whether it is in scope for the UMP work (see the next section).

## Supported-platform boundary

All backend sources are listed unconditionally in `src/core/CMakeLists.txt:1365-1378`. The platform gate is the preprocessor guard in each source file, plus the `#ifdef` blocks in the backend selection code in `src/core/AudioEngine.cpp:2041-2127`.

| Backend | Platform | Compile condition | Guard |
|---|---|---|---|
| ALSA sequencer (`MidiAlsaSeq`) | Where ALSA is found (in practice Linux; CMake has no Linux-only gate) | `WANT_ALSA` is ON by default (`CMakeLists.txt:100`) and `FIND_PACKAGE(Alsa)` succeeds, which sets `LMMS_HAVE_ALSA` (`:678-687`). Win32/Cygwin and Apple force `WANT_ALSA` OFF (`:295`, `:281`). | `LMMS_HAVE_ALSA` (`src/core/midi/MidiAlsaSeq.cpp:38`); selection `AudioEngine.cpp:2041-2051` |
| ALSA rawmidi (`MidiAlsaRaw`) | Where ALSA is found (in practice Linux; CMake has no Linux-only gate) | `WANT_ALSA` is ON by default (`CMakeLists.txt:100`) and `FIND_PACKAGE(Alsa)` succeeds, which sets `LMMS_HAVE_ALSA` (`:678-687`). Win32/Cygwin and Apple force `WANT_ALSA` OFF (`:295`, `:281`). | `LMMS_HAVE_ALSA` (`src/core/midi/MidiAlsaRaw.cpp:29`); selection `AudioEngine.cpp:2053-2062` |
| JACK (`MidiJack`) | Wherever JACK is found | `WANT_JACK`. Found as jack ≥ 0.77 (`CMakeLists.txt:704`), or weak-linked (`:695-702`). Sets `LMMS_HAVE_JACK` (`:700`, `:706`). | `LMMS_HAVE_JACK` (`src/core/midi/MidiJack.cpp:30`); selection `AudioEngine.cpp:2065-2076` |
| OSS (`MidiOss`) | Wherever the soundcard header is found | `WANT_OSS` and the soundcard header found (`CMakeLists.txt:669-671`). Win32/Cygwin and Apple force OSS OFF (`:296`, `:282`). | `LMMS_HAVE_OSS` (`src/core/midi/MidiOss.cpp:28`); selection `AudioEngine.cpp:2078-2089` |
| sndio (`MidiSndio`) | Wherever sndio is found | `WANT_SNDIO` and `FIND_PACKAGE(Sndio)` succeed (`CMakeLists.txt:777-785`). Win32/Cygwin force sndio OFF (`:298`). | `LMMS_HAVE_SNDIO` (`src/core/midi/MidiSndio.cpp:28`); selection `AudioEngine.cpp:2091-2102` |
| WinMM (`MidiWinMM`) | Win32 and Cygwin | Win32/Cygwin set `WANT_WINMM` ON and `LMMS_HAVE_WINMM` (`CMakeLists.txt:300-302`). The option defaults to OFF (`:126`). | `LMMS_HAVE_WINMM` (`src/core/midi/MidiWinMM.cpp:27`); selection `AudioEngine.cpp:2104-2115` |
| Apple (`MidiApple`) | macOS (`LMMS_BUILD_APPLE`) | `STATUS_APPLEMIDI` is "OK" (`CMakeLists.txt:288`). ALSA and OSS are forced OFF (`:281-282`). The macOS floor is 10.13 (`:271`). `MidiApple.cpp` includes `<CoreMIDI/CoreMIDI.h>` (`src/core/midi/MidiApple.cpp:39`) and calls `MIDIClientCreate` (`:535`). | `LMMS_BUILD_APPLE` (`src/core/midi/MidiApple.cpp:28`); selection `AudioEngine.cpp:2117-2127` |

Selection rules, from `src/core/AudioEngine.cpp`:

- If `audioengine/mididev` names a valid client, only that backend is tried (`:2035-2039`).
- If it is empty, the backends are tried in the order shown above.
- If none opens, the engine falls back to the dummy client (`:2129-2140`).

Platforms not in this table (other BSDs, Haiku and so on) are **unverified**. Do not claim a backend for a platform the build does not compile it for.

## Relation to MPE and the spec

`docs/specs/SPEC-zene-studio.md:60` (row A6) reads "MIDI 2.0 / MPE 1.0 note expression". It is a **planned-feature row** about per-note MPE expression stored on `Note`. It is not a statement that UMP input ships.

`src/core/midi/MpeExpression.cpp:1-80` contains an enable flag (`:38-52`, default off), clamps for pitch, velocity and 7-bit values (`:57-68`), and `isNeutral` (`:73-76`). It contains no MIDI byte or event parsing. **How MPE data arrives from a device is unverified.** It is not confirmed that it reads MIDI 1 channel messages.

A grep of `src/` and `include/` for `UMP` or `dialect` finds no MIDI input code that handles UMP. The one plug-in dialect flag is the CLAP host note report: `dialectJson` (`src/core/ControlCommandsHostNotes.cpp:49-57`) names a `midi2` note-dialect bit (`:55`). It reports a plug-in's note dialects. It is not a MIDI device input path, and whether the host sends UMP or MIDI 2 events to plug-ins is **unverified**. The vendored JUCE/Carla UMP sources under `plugins/CarlaBase/` are third-party code, not Zene MIDI input.

## Checking at runtime

The agent command `midi.device_list` (`src/core/ControlCommandsSettings.cpp:383-420`, id at `:386`) returns:

- `client`: the running client name (`engine->midiClientName()`).
- `configured`: the value of `audioengine/mididev`.
- `readable` and `writable`: the port lists of the running client.
- `count`: the number of readable plus writable ports (`:401-418`).

With no backend open, the engine reports the dummy client. The command returns a success result with empty lists, not an error (description `:389-392`, `ControlResult::success` at `:419`). The dummy client's port lists were not read, so that they are empty is **unverified** beyond the command's own description.
