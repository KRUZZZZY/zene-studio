/*
 * ScriptEngine.h - LMMS Lua scripting API v0
 *
 * Copyright (c) 2026 Zene Studio contributors
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

#include <QFile>
#include <QFileInfo>
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
#include "ScriptConsole.h"

struct lua_State;
class QThread;

namespace lmms
{

class MidiClip;
class Track;

/*! \brief The deepest ancestor of \a path that already exists on disk.
 *
 * Stops at any existing component, including a symlink whose target is
 * missing: `QFileInfo::exists()` is false for a dangling symlink, so a
 * naive walk climbs past the link and validates only its in-project
 * parent. Returns \a root once the walk has consumed it.
 */
inline QString deepestExistingAncestor(const QString& path, const QString& root)
{
	QString probe = path;
	while (probe.size() > root.size() && !QFileInfo::exists(probe) && !QFileInfo(probe).isSymLink())
	{
		probe = QFileInfo(probe).absolutePath();
	}
	return probe;
}

/*! \brief Largest script source the host reads from a FILE, in bytes.
 *
 * The Lua memory budget (ScriptEngine::memoryBudget) bounds what the SCRIPT
 * may hold inside its Lua state. The host's own copies of the source - the
 * bytes read from the file and the QString they decode into - are made before
 * Lua ever sees the script and are NOT charged to that budget, so the source
 * is bounded independently by this fixed cap: a file above it is refused
 * before it is read, regardless of the Lua budget.
 */
constexpr quint64 MaxScriptSourceBytes = 16ull * 1024ull * 1024ull;

//! Bytes read per chunk while a script file is read (bounds the read buffer).
constexpr qint64 ScriptSourceChunkBytes = 64 * 1024;

/*! \brief Read a script file into \a source, refusing anything above \a limit.
 *
 * The file is refused on its SIZE before it is read, so a huge file never
 * reaches the host's memory; the read that follows is bounded as well (a file
 * that grew since the size check, or a stream of unknown size, is cut off at
 * the limit). The conversion to \a source therefore never sees more than
 * \a limit bytes.
 *
 * \return false when the file cannot be read or is above the limit; \a error
 * then says which.
 */
inline bool readScriptSource(const QString& path, quint64 limit, QString& source, QString* error)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
	{
		if (error != nullptr)
		{
			*error = QStringLiteral("cannot open script '%1': %2").arg(path, file.errorString());
		}
		return false;
	}
	const qint64 size = file.size();
	if (size > static_cast<qint64>(limit))
	{
		if (error != nullptr)
		{
			*error = QStringLiteral("script '%1' is too large: %2 bytes, the limit is %3 bytes")
					.arg(path).arg(size).arg(limit);
		}
		return false;
	}
	QByteArray bytes;
	if (size > 0)
	{
		bytes.reserve(static_cast<int>(size));
	}
	while (true)
	{
		const QByteArray chunk = file.read(ScriptSourceChunkBytes);
		if (chunk.isEmpty())
		{
			break;
		}
		bytes += chunk;
		if (static_cast<quint64>(bytes.size()) > limit)
		{
			if (error != nullptr)
			{
				*error = QStringLiteral("script '%1' is too large: the limit is %2 bytes")
						.arg(path).arg(limit);
			}
			return false;
		}
	}
	source = QString::fromUtf8(bytes);
	return true;
}

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
		/*! One DAW-control edit (mixer channel, effect chain, effect).
		 *  i0 = ScriptDawBindings::Op, object0 = the target, i1 = an index or a
		 *  flag, text = a device reference or a name. ONE type carries the whole
		 *  op family because applyCommand() is a grandfathered entry in
		 *  tests/complexity-baseline.tsv: the dispatch over the opcode lives in
		 *  src/core/ScriptDawEdit.cpp. */
		DawEdit,
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

/*! \brief Copy \a text into \a command's fixed payload, or report that it does not fit.
 *
 * The payload is a fixed 256-byte, NUL-terminated buffer, so a strncpy of a
 * longer name truncates it - and a truncation that lands mid-character splits a
 * UTF-8 sequence, leaving a corrupt name rather than merely a short one. Callers
 * that carry a user-visible string refuse instead of storing a damaged one.
 * An embedded NUL is refused for the same reason: the payload is read back as a
 * C string, so the bytes after it would be lost.
 *
 * \return false when \a text does not fit whole or contains a NUL byte (the
 *         payload is left untouched).
 */
bool setCommandText(ScriptCommand& command, const QString& text);

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

	/*! Producer side, for the TWO commands one change needs: its undo
	 *  checkpoint and its value write.
	 *
	 *  The pair is all-or-nothing. A queue with room for the checkpoint but
	 *  not for the write would take the checkpoint and drop the write, and
	 *  the change would leave an empty undo step behind - one that restores
	 *  nothing. Both slots are reserved before either command is queued, so
	 *  the change is either wholly queued or wholly refused.
	 *
	 *  \return false when the pair did not fit: both commands are dropped and
	 *  counted, as two dropped commands are.
	 */
	bool push(const ScriptCommand& first, const ScriptCommand& second);

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

/*! \brief Embedded Lua scripting engine (spec: docs/specs/SPEC-lua-api-v0.md).
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

	/*! Memory budget per script invocation, in bytes - the budget that sits
	 *  BESIDE the instruction budget (CODE-6).
	 *
	 *  The Lua state is created through lua_newstate() with this engine's own
	 *  allocator rather than luaL_newstate()'s, so every byte the script asks
	 *  Lua to hold is counted, and a request that would cross this cap is
	 *  REFUSED (the allocator returns NULL, Lua raises its memory error, and
	 *  the run fails with "memory budget exceeded"). An unbounded allocation is
	 *  therefore a script error the caller reads, not an OOM kill of the
	 *  process: the instruction budget bounds time, this bounds space, and
	 *  neither bounds the other.
	 *
	 *  0 means "no cap" and is only reachable from C++ - the control surface's
	 *  script.set_memory_budget refuses anything outside
	 *  [MinMemoryBudgetBytes, MaxMemoryBudgetBytes].
	 */
	//! The budget itself. Defined here rather than in ScriptEngine.cpp: the
	//! file-length ratchet grandfathers that file at its current size, and the
	//! budget's three accessors must not be the reason the number moves.
	void setMemoryBudget(quint64 bytes) { m_memoryBudget.store(bytes, std::memory_order_relaxed); }
	quint64 memoryBudget() const { return m_memoryBudget.load(std::memory_order_relaxed); }

	//! Live bytes the last run's Lua state held when it finished (0 after a
	//! run that could not open a state).
	quint64 lastRunMemoryBytes() const { return m_lastRunMemory.load(std::memory_order_relaxed); }
	//! Peak live bytes during the last run - the number that says how close a
	//! script came to the cap.
	quint64 lastRunMemoryPeakBytes() const
	{
		return m_lastRunMemoryPeak.load(std::memory_order_relaxed);
	}
	//! Allocations the allocator refused during the last run (0 = the cap was
	//! never reached). Non-zero is what a refused run reports.
	quint64 lastRunMemoryRefusals() const
	{
		return m_lastRunMemoryRefusals.load(std::memory_order_relaxed);
	}

	//! Default memory budget: 64 MiB, i.e. far above any shipped script's
	//! working set and far below a machine's patience for a runaway one.
	static constexpr quint64 DefaultMemoryBudgetBytes = 64ull * 1024ull * 1024ull;
	//! Smallest budget the control surface accepts. A Lua state plus the
	//! sandbox's libraries need a floor to exist at all; below it the failure
	//! would be "could not create a Lua state" rather than a budget refusal.
	static constexpr quint64 MinMemoryBudgetBytes = 512ull * 1024ull;
	//! Largest budget the control surface accepts: 1 GiB. It is deliberately
	//! the largest value the wire's integer property can carry (INT32_MAX), so
	//! the command's schema and its handler bound the same range and a client
	//! can read the limit out of control.commands_list instead of discovering
	//! it by refusal.
	static constexpr quint64 MaxMemoryBudgetBytes = 1024ull * 1024ull * 1024ull;

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
	/*! MIDI-in events the ring buffer REFUSED because it was full.
	 *
	 * The buffer is a fixed-size ring and the policy is non-blocking: an
	 * event that does not fit is dropped rather than stalling the MIDI
	 * thread. A dropped event is a LOST event (a lost note-off leaves a note
	 * sounding), so the loss is counted and the count is readable by the host
	 * that feeds the buffer - the same shape as the command queue's
	 * droppedCommandCount().
	 */
	quint64 droppedMidiInEventCount() const
	{
		return m_midiInDropped.load(std::memory_order_relaxed);
	}

	//! Parse a `--! lmms-api <major>.<minor>` header. Empty when absent.
	static QString parseVersionHeader(const QString& source);
	//! Human-readable result for the API version in \a header.
	static bool isCompatibleVersion(const QString& version, QString* reason = nullptr);

	// --- used by the binding layer, which runs on the worker thread ---
	/*! Queue \a command for the apply side.
	 *
	 *  \return false when the bounded queue was full and the command was
	 *  DROPPED (spec section 4: the producer is never blocked, and the drop
	 *  is counted). The caller owns the refusal - only it knows what the
	 *  command was for, so only it can tell the script what was lost. The
	 *  count is unchanged either way (droppedCommandCount()).
	 */
	bool enqueue(const ScriptCommand& command);

	/*! Queue a checkpoint and a write as ONE reservation: both are queued, or
	 *  neither is.
	 *
	 *  \return false when the queue had no room for BOTH: the change is
	 *  refused whole, so it never leaves a checkpoint behind without its
	 *  write (an empty undo step).
	 */
	bool enqueue(const ScriptCommand& first, const ScriptCommand& second);
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

	//! \p importRoot: directory `require` may load siblings from; empty = no `require`.
	RunResult runOnWorker(const QString& source, const QString& chunkName, QString* error,
				const QString& importRoot = QString());

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
	//! MIDI-in events the ring buffer refused (it was full). Written by the
	//! producer thread, read by anyone - the same atomic discipline the
	//! command queue's drop counter uses.
	std::atomic<quint64> m_midiInDropped{0};

	//! Count a MIDI-in event the ring buffer refused: a write of zero means
	//! the buffer was full and the event was dropped.
	void countDroppedMidiInEvent(std::size_t written)
	{
		if (written == 0)
		{
			m_midiInDropped.fetch_add(1, std::memory_order_relaxed);
		}
	}

	std::atomic<bool> m_running{false};
	std::atomic<bool> m_autoApply{true};
	std::atomic<quint64> m_instructionBudget{5000000};

	//! CODE-6: the memory budget and what the last run measured against it.
	//! Written by the worker thread, read by any thread - the same atomic
	//! discipline the instruction budget uses.
	std::atomic<quint64> m_memoryBudget{DefaultMemoryBudgetBytes};
	std::atomic<quint64> m_lastRunMemory{0};
	std::atomic<quint64> m_lastRunMemoryPeak{0};
	std::atomic<quint64> m_lastRunMemoryRefusals{0};

	//! Publish one run's memory measurements (worker thread -> everyone).
	void recordRunMemory(quint64 live, quint64 peak, quint64 refusals)
	{
		m_lastRunMemory.store(live, std::memory_order_relaxed);
		m_lastRunMemoryPeak.store(peak, std::memory_order_relaxed);
		m_lastRunMemoryRefusals.store(refusals, std::memory_order_relaxed);
	}

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
	ScriptConsole::CaptureBuffer m_logCapture;
};

} // namespace lmms

#endif // LMMS_SCRIPT_ENGINE_H
