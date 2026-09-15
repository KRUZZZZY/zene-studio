# The Lua API surface of this build, per version. DERIVED FILE - do not hand-edit.
#
# Producer: tests/lua-api-surface.py --write (registered as the ctest LuaApiSurface,
# which re-derives this file and fails on any drift). Enforcement and policy:
# docs/LUA-COMPATIBILITY-POLICY.md section 2.
#
# The surface is derived from the registration sources: src/core/ScriptBindings.cpp, src/core/ScriptDawBindings.cpp, src/core/ScriptDawEffects.cpp, src/core/ScriptDawEdit.cpp.
# Entries: '<namespace>: <function>' for a namespace function, 'class <Name>: <method>'
# for a class member.
api 0.2
class BoolModel: isValid
class BoolModel: name
class BoolModel: setValue
class BoolModel: value
class Effect: displayName
class Effect: enabled
class Effect: id
class Effect: index
class Effect: isValid
class Effect: parameter
class Effect: parameterCount
class Effect: parameterName
class Effect: pluginName
class Effect: setEnabled
class EffectChain: effect
class EffectChain: effectById
class EffectChain: effectCount
class EffectChain: isValid
class EffectChain: loadEffect
class EffectChain: removeEffect
class FloatModel: isValid
class FloatModel: maxValue
class FloatModel: minValue
class FloatModel: name
class FloatModel: setValue
class FloatModel: type
class FloatModel: value
class Instrument: isValid
class Instrument: name
class Instrument: parameterCount
class Instrument: parameterModel
class Instrument: parameterName
class InstrumentTrack: instrument
class InstrumentTrack: instrumentName
class InstrumentTrack: isValid
class InstrumentTrack: name
class InstrumentTrack: panning
class InstrumentTrack: panningModel
class InstrumentTrack: setName
class InstrumentTrack: setPanning
class InstrumentTrack: setVolume
class InstrumentTrack: volume
class InstrumentTrack: volumeModel
class LuaLog: error
class LuaLog: info
class LuaLog: warn
class LuaLog: write
class MidiEvent: channel
class MidiEvent: isNoteOn
class MidiEvent: isValid
class MidiEvent: key
class MidiEvent: velocity
class MidiIn: hasEvent
class MidiIn: next
class MidiOut: noteOff
class MidiOut: noteOn
class Mixer: addChannel
class Mixer: channel
class Mixer: channelById
class Mixer: channelCount
class Mixer: ids
class Mixer: master
class MixerChannel: chain
class MixerChannel: gain
class MixerChannel: gainModel
class MixerChannel: id
class MixerChannel: index
class MixerChannel: isBus
class MixerChannel: isMaster
class MixerChannel: isValid
class MixerChannel: muteModel
class MixerChannel: muted
class MixerChannel: name
class MixerChannel: sendAmount
class MixerChannel: sendCount
class MixerChannel: sendPreFader
class MixerChannel: sendTarget
class MixerChannel: setGain
class MixerChannel: setMuted
class MixerChannel: setName
class MixerChannel: setSoloed
class MixerChannel: soloModel
class MixerChannel: soloed
class Note: key
class Note: length
class Note: panning
class Note: pos
class Note: setKey
class Note: setLength
class Note: setPanning
class Note: setPos
class Note: setVolume
class Note: volume
class NoteBuilder: addTo
class NoteBuilder: at
class NoteBuilder: build
class NoteBuilder: key
class NoteBuilder: len
class NoteBuilder: length
class NoteBuilder: pan
class NoteBuilder: panning
class NoteBuilder: pos
class NoteBuilder: position
class NoteBuilder: vol
class NoteBuilder: volume
class PatternClip: addCheckPoint
class PatternClip: addNote
class PatternClip: addNoteAt
class PatternClip: clearNotes
class PatternClip: isValid
class PatternClip: lengthTicks
class PatternClip: name
class PatternClip: note
class PatternClip: noteCount
class PatternClip: patternIndex
class PatternClip: removeNote
class PatternClip: setLengthTicks
class PatternClip: setName
class PatternClip: trackIndex
class PatternStore: addInstrumentTrack
class PatternStore: addPattern
class PatternStore: patternClip
class PatternStore: patternCount
class PatternStore: track
class PatternStore: trackCount
class ProjectFile: exists
class ProjectFile: list
class ProjectFile: projectDir
class ProjectFile: read
class ProjectFile: write
class Song: addPattern
class Song: masterVolume
class Song: pattern
class Song: patternCount
class Song: patternStore
class Song: saveProject
class Song: setMasterVolume
class Song: setTempo
class Song: tempo
class Song: transport
class Track: asInstrumentTrack
class Track: clipCount
class Track: index
class Track: instrumentName
class Track: isValid
class Track: muteModel
class Track: name
class Track: setName
class Track: type
class Track: volumeModel
class Transport: isPlaying
class Transport: play
class Transport: position
class Transport: stop
zene: apiStability
zene: apiSurface
zene: apiVersion
zene: apiVersionMajor
zene: apiVersionMinor
zene: instructionBudget
zene: log
zene: midiIn
zene: midiOut
zene: mixer
zene: note
zene: patternStore
zene: projectDir
zene: projectFile
zene: setInstructionBudget
zene: song
zene: stepsPerBar
zene: ticksPerBar
zene: transport
zene: version
