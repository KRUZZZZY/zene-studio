/*
 * ScriptEngine.h - LMMS Lua scripting API v0
 *
 * Copyright (c) 2026 LMMS contributors
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

#ifndef LMMS_SCRIPT_ENGINE_H
#define LMMS_SCRIPT_ENGINE_H

#include <QMutex>
#include <QObject>
#include <QSemaphore>
#include <QString>
#include <QStringList>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>

#include "LocklessRingBuffer.h"

struct lua_State;
class QThread;

namespace lmms
{

class MidiClip;
class Track;

//! Capacity of the script -> apply-side command queue, in commands.
constexpr std::size_t ScriptCommandQueueCapacity = 1024;

//! Size of the fixed payload buffer in ScriptCommand (never heap allocated).
constexpr std::size_t ScriptCommandTextSize = 256;

/*! \brief A single engine mutation requested by a running script.
 *
 * ScriptCommand is a plain old data type on purpose: the queue that carries
 * it is pre-allocated and must never allocate or lock. Strings travel in a
 * fixed-size, NUL-terminated buffer.
 */
struct ScriptCommand
{
	enum class Type : std::uint8_t
	{
		AddPatternTrack,	//!< create a new pattern row
		AddInstrumentTrack,	//!< create an instrument row in the pattern store
		AddNote,		//!< object0 = MidiClip*, i0 = key, i1 = pos, i2 = len, i3 = vol, f0 = pan
		RemoveNote,		//!< object0 = MidiClip*, i0 = note index
		ClearNotes,		//!< object0 = MidiClip*
		SetClipName,		//!< object0 = Clip*, text = new name
		SetClipLength,		//!< object0 = Clip*, i0 = length in ticks
		SetTempo,		//!< i0 = bpm
		SetMasterVolume,	//!< f0 = volume [0..1] (mapped to the 0..100 model)
		SetTrackName,		//!< object0 = Track*, text = new name
		SetTrackVolume,		//!< object0 = Track*, f0 = volume [0..200] (Track::setVolume)
		SetModelValue,		//!< object0 = AutomatableModel*, f0 = new value
		Play,			//!< transport play
		Stop,			//!< transport stop
		AddCheckPoint,		//!< object0 = JournallingObject* (undo checkpoint)
		EmitMidiNote,		//!< i0 = key, i1 = velocity, i2 = channel, i3 = 1 note-on / 0 note-off
	};

	Type type{};
	void* object0{nullptr};
	std::int32_t i0{};
	std::int32_t i1{};
	std::int32_t i2{};
	std::int32_t i3{};
	float f0{};
	char text[ScriptCommandTextSize]{};
};

/*! \brief Single-producer / single-consumer command queue.
 *
 * The producer is the script worker thread; the consumer is the apply side
 * (ScriptEngine::processCommands()). The backing ring buffer is allocated up
 * front, so push() never allocates and never blocks: on overflow the command
 * is dropped and counted as a realtime violation (spec section 4).
 */
class ScriptCommandQueue
{
public:
	explicit ScriptCommandQueue(std::size_t capacity = ScriptCommandQueueCapacity);
	~ScriptCommandQueue() = default;

	//! Producer side. Returns false when the queue is full (command dropped).
	bool push(const ScriptCommand& command);

	//! Consumer side: hands at most \a max commands to \a fn. Returns count.
	std::size_t drain(const std::function<void(const ScriptCommand&)>& fn,
				std::size_t max = ScriptCommandQueueCapacity);

	std::size_t pending() const;
	std::uint64_t dropped() const;

private:
	LocklessRingBuffer<ScriptCommand> m_buffer;
	LocklessRingBufferReader<ScriptCommand> m_reader;
	std::atomic<std::uint64_t> m_dropped{0};
};

/*! \brief MIDI event polled by scripts through the MidiIn wrapper (v0). */
struct ScriptMidiEvent
{
	std::int32_t channel{};
	std::int32_t key{};
	std::int32_t velocity{};
	std::int32_t noteOn{};
};

/*! \brief Embedded Lua scripting engine (spec: specs/SPEC-lua-api-v0.md).
 *
 * Threading model (spec section 4): scripts always execute on a dedicated
 * worker thread, never on the audio thread. All engine mutations are turned
 * into ScriptCommands and pushed onto the SPSC queue; the queue is applied at
 * a safe point by processCommands(). The audio thread may observe queue state
 * through audioThreadTick(), which allocates nothing and takes no locks.
 */
class ScriptEngine : public QObject
{
	Q_OBJECT
public:
	enum class RunResult
	{
		Ok,		//!< script finished without error
		ScriptError,	//!< Lua runtime error (including budget exhaustion)
		VersionError,	//!< missing or incompatible `--! lmms-api` header
		SandboxError,	//!< sandbox violation detected before execution
		Busy,		//!< another script is already running
	};

	static ScriptEngine* instance();

	//! Run a script file. A `--! lmms-api <major>.<minor>` header is required.
	RunResult runFile(const QString& path, QString* error = nullptr);
	//! Run a script from memory. The version header is optional here.
	RunResult runString(const QString& source, QString* error = nullptr,
				const QString& chunkName = QStringLiteral("=(string)"));

	//! Apply queued commands at a safe point. Returns the number applied.
	int processCommands();
	int pendingCommandCount() const;
	quint64 droppedCommandCount() const;

	//! Audio-thread observation point: allocation-free, lock-free, no-op safe.
	//! Returns true when the apply side has work waiting.
	bool audioThreadTick();

	void setAutoApply(bool enabled);
	bool autoApply() const;

	//! Root of the sandbox for ProjectFile; defaults to the current project's
	//! directory (or the user's home when no project is loaded).
	void setProjectDir(const QString& dir);
	QString projectDir() const;

	//! Instruction budget per script invocation (spec section 5).
	void setInstructionBudget(quint64 instructions);
	quint64 instructionBudget() const;

	//! The dedicated worker thread scripts execute on (never the audio thread).
	QThread* workerThread() const;

	//! Thread that last applied a queued command. Regression guard: engine
	//! state is only ever mutated by the apply side, never by the worker
	//! (spec section 4 / section 10). Null before the first apply.
	QThread* lastApplyThread() const;

	QStringList takeLogMessages();
	QStringList logMessages() const;

	bool lastRunFailed() const;
	QString lastError() const;

	//! Feed the polled MIDI-in buffer used by MidiIn / midi-router.lua.
	void pushMidiInEvent(int channel, int key, int velocity, bool noteOn);
	//! Consumer side of the MIDI-in buffer (worker thread only).
	bool popMidiInEvent(ScriptMidiEvent* event);
	//! Number of queued MIDI-in events (worker thread only).
	int midiInPending() const;

	//! Parse a `--! lmms-api <major>.<minor>` header. Empty when absent.
	static QString parseVersionHeader(const QString& source);
	//! Human-readable result for the API version in \a header.
	static bool isCompatibleVersion(const QString& version, QString* reason = nullptr);

	// --- used by the binding layer, which runs on the worker thread ---
	void enqueue(const ScriptCommand& command);
	//! Apply pending commands synchronously so reads see a consistent state.
	void flushCommandsForRead();
	void logMessage(const QString& message);
	//! Resolve \a path against the sandbox root; empty on violation.
	QString resolveProjectPath(const QString& path, bool forWrite, QString* error) const;

	int patternCount() const;
	int patternTrackCount() const;
	MidiClip* patternClipAt(int patternIndex, int trackIndex) const;
	Track* trackAt(int trackIndex) const;

signals:
	void logged(const QString& message);
	void commandsApplied(int count);

private:
	explicit ScriptEngine(QObject* parent = nullptr);
	~ScriptEngine() override;

	RunResult runOnWorker(const QString& source, const QString& chunkName, QString* error);

	void applyCommand(const ScriptCommand& command);
	void applyCommands(std::size_t max);
	void resetRunState();

	/*! Report a finished run to the console.
	 *
	 *  Only a failure says anything: a script that died must explain itself
	 *  where its prints went. Kept out of runOnWorker() so the error check does
	 *  not add decision points to the run path (the complexity ratchet measures
	 *  runOnWorker, and this is the branch that used to live inside it).
	 */
	void reportRunResult(RunResult result, const QString& message);

	class ScriptWorker;
	ScriptWorker* m_worker{nullptr};
	QThread* m_workerThread{nullptr};

	ScriptCommandQueue m_commands;
	LocklessRingBuffer<ScriptMidiEvent> m_midiIn;
	LocklessRingBufferReader<ScriptMidiEvent> m_midiInReader;

	std::atomic<bool> m_running{false};
	std::atomic<bool> m_autoApply{true};
	std::atomic<quint64> m_instructionBudget{5000000};

	//! Apply-side pump state: while a script runs, runOnWorker() serves the
	//! worker's flushCommandsForRead() requests on the apply-side thread.
	std::atomic<bool> m_applyPumpActive{false};
	QSemaphore m_applyRequest;
	QSemaphore m_applyDone;
	std::atomic<QThread*> m_lastApplyThread{nullptr};

	mutable QMutex m_stateMutex;
	QString m_projectDir;
	QString m_lastError;
	QStringList m_logMessages;
};

} // namespace lmms

#endif // LMMS_SCRIPT_ENGINE_H
