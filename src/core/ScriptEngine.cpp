/*
 * ScriptEngine.cpp - LMMS Lua scripting API v0
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

#include "ScriptEngine.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QThread>
#include <QVector>

#include <cstring>

extern "C"
{
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include "AutomatableModel.h"
#include "Clip.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "JournallingObject.h"
#include "MidiClip.h"
#include "Note.h"
#include "PatternStore.h"
#include "ProjectJournal.h"
#include "ScriptApiVersion.h"
#include "ScriptBindings.h"
#include "ScriptConsole.h"
#include "Song.h"
#include "Track.h"
#include "TrackContainer.h"

namespace lmms
{

//! Instructions executed between two invocations of the count hook.
constexpr int ScriptHookStep = 1000;

/*! \brief Per-invocation state shared with the Lua count hook.
 *
 * Lives on the worker thread stack for the duration of one script run; the
 * hook reaches it through the Lua state's extra space.
 */
struct ScriptRunState
{
	quint64 instructions{0};
	quint64 budget{0};
	quint64 hookStep{ScriptHookStep};
};


/*! \brief Runs Lua on the dedicated script worker thread.
 *
 * The worker owns the lua_State for exactly one invocation: a fresh state per
 * script means no script can observe another script's globals and sandbox
 * configuration cannot drift.
 */
class ScriptEngine::ScriptWorker : public QObject
{
public:
	explicit ScriptWorker(ScriptEngine* engine) :
		m_engine(engine)
	{
	}

	ScriptEngine::RunResult run(const QString& source, const QString& chunkName,
					QString* error)
	{
		lua_State* L = luaL_newstate();
		if (L == nullptr)
		{
			*error = QStringLiteral("could not create a Lua state");
			return ScriptEngine::RunResult::ScriptError;
		}

		ScriptBindings::beginRun();
		*static_cast<ScriptWorker**>(lua_getextraspace(L)) = this;
		openSandbox(L);
		ScriptBindings::registerAll(L, m_engine);

		m_state.instructions = 0;
		m_state.budget = m_engine->instructionBudget();
		m_state.hookStep = ScriptHookStep;
		lua_sethook(L, &ScriptWorker::instructionHook, LUA_MASKCOUNT,
				static_cast<int>(ScriptHookStep));

		const QByteArray code = source.toUtf8();
		const QByteArray name = chunkName.toUtf8();
		ScriptEngine::RunResult result = ScriptEngine::RunResult::Ok;

		int status = luaL_loadbuffer(L, code.constData(), static_cast<std::size_t>(code.size()),
						name.constData());
		if (status == LUA_OK)
		{
			status = lua_pcall(L, 0, 0, 0);
		}
		if (status != LUA_OK)
		{
			const char* message = lua_tostring(L, -1);
			*error = message != nullptr ? QString::fromUtf8(message)
							: QStringLiteral("unknown Lua error");
			result = ScriptEngine::RunResult::ScriptError;
		}

		lua_close(L);
		return result;
	}

private:
	//! Sandbox setup (spec section 5): only string/table/math/utf8 survive.
	void openSandbox(lua_State* L)
	{
		static const luaL_Reg safeLibs[] = {
			{LUA_GNAME, luaopen_base},
			{LUA_TABLIBNAME, luaopen_table},
			{LUA_STRLIBNAME, luaopen_string},
			{LUA_MATHLIBNAME, luaopen_math},
			{LUA_UTF8LIBNAME, luaopen_utf8},
			{nullptr, nullptr},
		};
		for (const luaL_Reg* lib = safeLibs; lib->func != nullptr; ++lib)
		{
			luaL_requiref(L, lib->name, lib->func, 1);
			lua_pop(L, 1);
		}

		// os/io/package/debug and the dynamic-loading entry points are removed
		// outright; there is no way for a script to get them back.
		static const char* const removedGlobals[] = {
			"os", "io", "package", "debug", "require",
			"dofile", "loadfile", "load", "loadstring", "module",
			"collectgarbage",
		};
		for (const char* name : removedGlobals)
		{
			lua_pushnil(L);
			lua_setglobal(L, name);
		}

		// A restricted debug table keeps the spec's `debug.sethook(count)`
		// surface, but only the count hook and only downwards: a script can
		// lower its own budget, never disable or raise it.
		lua_newtable(L);
		lua_pushcfunction(L, &ScriptWorker::restrictedSetHook);
		lua_setfield(L, -2, "sethook");
		lua_pushcfunction(L, &ScriptWorker::traceback);
		lua_setfield(L, -2, "traceback");
		lua_setglobal(L, "debug");

		// print() is routed through LuaLog so script output is captured.
		lua_pushcfunction(L, &ScriptWorker::print);
		lua_setglobal(L, "print");
	}

	static ScriptWorker* workerFromState(lua_State* L)
	{
		return *static_cast<ScriptWorker**>(lua_getextraspace(L));
	}

	static void instructionHook(lua_State* L, lua_Debug* debug)
	{
		Q_UNUSED(debug);
		ScriptWorker* worker = workerFromState(L);
		if (worker == nullptr)
		{
			return;
		}
		ScriptRunState& state = worker->m_state;
		state.instructions += state.hookStep;
		if (state.instructions > state.budget)
		{
			// lua_pushfstring only understands %d, %I, %f, %p, %s, %U, %c and
			// %%; %llu is rejected as an invalid option ('%l').
			luaL_error(L, "instruction budget exceeded (%I instructions, budget %I)"
					" - script aborted",
					static_cast<lua_Integer>(state.instructions),
					static_cast<lua_Integer>(state.budget));
		}
	}

	static int restrictedSetHook(lua_State* L)
	{
		ScriptWorker* worker = workerFromState(L);
		const lua_Integer budget = luaL_checkinteger(L, 1);
		if (budget < 1)
		{
			return luaL_error(L, "debug.sethook: budget must be >= 1");
		}
		if (worker != nullptr && static_cast<quint64>(budget) < worker->m_state.budget)
		{
			worker->m_state.budget = static_cast<quint64>(budget);
		}
		return 0;
	}

	static int traceback(lua_State* L)
	{
		luaL_traceback(L, L, luaL_optstring(L, 1, ""), 1);
		return 1;
	}

	static int print(lua_State* L)
	{
		ScriptWorker* worker = workerFromState(L);
		const int count = lua_gettop(L);
		QString message;
		for (int i = 1; i <= count; ++i)
		{
			std::size_t length = 0;
			const char* text = luaL_tolstring(L, i, &length);
			if (i > 1)
			{
				message += QLatin1Char('\t');
			}
			message += QString::fromUtf8(text, static_cast<int>(length));
			lua_pop(L, 1);
		}
		if (worker != nullptr)
		{
			worker->m_engine->logMessage(message);
		}
		return 0;
	}

	ScriptEngine* m_engine{nullptr};
	ScriptRunState m_state;
};


ScriptEngine* ScriptEngine::instance()
{
	static ScriptEngine engine;
	return &engine;
}


ScriptEngine::ScriptEngine(QObject* parent) :
	QObject(parent),
	m_workerThread(new QThread()),
	m_commands(),
	m_midiIn(1024),
	m_midiInReader(m_midiIn)
{
	m_workerThread->setObjectName(QStringLiteral("zene-lua-script-worker"));
	m_worker = new ScriptWorker(this);
	m_worker->moveToThread(m_workerThread);
	m_workerThread->start();
	m_projectDir = QDir::currentPath();
}


ScriptEngine::~ScriptEngine()
{
	if (m_workerThread != nullptr)
	{
		m_workerThread->quit();
		m_workerThread->wait();
		delete m_worker;
		delete m_workerThread;
	}
}


ScriptEngine::RunResult ScriptEngine::runFile(const QString& path, QString* error)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
	{
		if (error != nullptr)
		{
			*error = QStringLiteral("cannot open script '%1': %2").arg(path, file.errorString());
		}
		return RunResult::ScriptError;
	}
	const QString source = QString::fromUtf8(file.readAll());
	file.close();

	const QString version = parseVersionHeader(source);
	if (version.isEmpty())
	{
		if (error != nullptr)
		{
			*error = QStringLiteral("script '%1' has no '--! zene-api <major>.<minor>' header")
					.arg(path);
		}
		return RunResult::VersionError;
	}
	QString reason;
	if (!isCompatibleVersion(version, &reason))
	{
		if (error != nullptr)
		{
			*error = QStringLiteral("script '%1' requires zene-api %2: %3")
					.arg(path, version, reason);
		}
		return RunResult::VersionError;
	}

	return runOnWorker(source, path, error);
}


ScriptEngine::RunResult ScriptEngine::runString(const QString& source, QString* error,
						const QString& chunkName)
{
	return runOnWorker(source, chunkName, error);
}


ScriptEngine::RunResult ScriptEngine::runOnWorker(const QString& source, const QString& chunkName,
							QString* error)
{
	RunResult result = RunResult::ScriptError;
	QString message;
	bool wasRunning = false;
	if (!m_running.compare_exchange_strong(wasRunning, true))
	{
		if (error != nullptr)
		{
			*error = QStringLiteral("another script is already running");
		}
		return RunResult::Busy;
	}

	{
		QMutexLocker locker(&m_stateMutex);
		m_lastError.clear();
		m_logMessages.clear();
	}

	if (QThread::currentThread() == m_workerThread)
	{
		// Re-entrant invocation from the worker itself: run inline. No
		// apply-side pump exists, so flushCommandsForRead() applies inline too.
		result = m_worker->run(source, chunkName, &message);
	}
	else
	{
		std::atomic<bool> finished{false};
		m_lastApplyThread.store(nullptr, std::memory_order_release);
		m_applyPumpActive.store(true, std::memory_order_release);

		const bool invoked = QMetaObject::invokeMethod(m_worker, [&]() {
			result = m_worker->run(source, chunkName, &message);
			finished.store(true, std::memory_order_release);
		}, Qt::QueuedConnection);
		if (!invoked)
		{
			finished.store(true, std::memory_order_release);
			message = QStringLiteral("could not dispatch to the script worker thread");
			result = RunResult::ScriptError;
		}

		// The apply side owns engine state. While the script runs, serve its
		// flushCommandsForRead() requests here instead of letting the worker
		// mutate the engine (spec section 4 / section 10). The worker only
		// sets `finished` after the last request has been served.
		while (!finished.load(std::memory_order_acquire))
		{
			if (m_applyRequest.tryAcquire(1, 2))
			{
				applyCommands(ScriptCommandQueueCapacity);
				m_applyDone.release();
			}
		}
		m_applyPumpActive.store(false, std::memory_order_release);
	}

	if (m_autoApply.load(std::memory_order_relaxed))
	{
		processCommands();
	}

	{
		QMutexLocker locker(&m_stateMutex);
		m_lastError = message;
	}
	reportRunResult(result, message);
	m_running.store(false);

	if (error != nullptr)
	{
		*error = message;
	}
	return result;
}


int ScriptEngine::processCommands()
{
	const int before = static_cast<int>(m_commands.pending());
	applyCommands(ScriptCommandQueueCapacity);
	const int after = static_cast<int>(m_commands.pending());
	const int applied = before - after;
	if (applied > 0)
	{
		emit commandsApplied(applied);
	}
	return applied;
}


void ScriptEngine::applyCommands(std::size_t max)
{
	m_commands.drain([this](const ScriptCommand& command) {
		applyCommand(command);
	}, max);
}


void ScriptEngine::applyCommand(const ScriptCommand& command)
{
	m_lastApplyThread.store(QThread::currentThread(), std::memory_order_release);
	switch (command.type)
	{
	case ScriptCommand::Type::AddPatternTrack:
		Engine::getSong()->addPatternTrack();
		break;

	case ScriptCommand::Type::AddInstrumentTrack:
		Track::create(Track::Type::Instrument, Engine::patternStore());
		break;

	case ScriptCommand::Type::AddNote:
	{
		auto* clip = static_cast<MidiClip*>(command.object0);
		if (clip == nullptr)
		{
			break;
		}
		// quant_pos=false: quantization needs the GUI piano roll, and there is
		// no GUI in a headless build.
		clip->addNote(Note(TimePos(command.i2), TimePos(command.i1), command.i0,
					static_cast<volume_t>(command.i3),
					static_cast<panning_t>(command.f0)), false);
		break;
	}

	case ScriptCommand::Type::RemoveNote:
	{
		auto* clip = static_cast<MidiClip*>(command.object0);
		if (clip == nullptr)
		{
			break;
		}
		const NoteVector& notes = clip->notes();
		if (command.i0 >= 0 && command.i0 < static_cast<int>(notes.size()))
		{
			clip->removeNote(notes[command.i0]);
		}
		break;
	}

	case ScriptCommand::Type::ClearNotes:
		if (auto* clip = static_cast<MidiClip*>(command.object0))
		{
			clip->clearNotes();
		}
		break;

	case ScriptCommand::Type::SetClipName:
		if (auto* clip = static_cast<Clip*>(command.object0))
		{
			clip->setName(QString::fromUtf8(command.text));
		}
		break;

	case ScriptCommand::Type::SetClipLength:
		if (auto* clip = static_cast<Clip*>(command.object0))
		{
			clip->changeLength(TimePos(command.i0));
		}
		break;

	case ScriptCommand::Type::SetTempo:
		Engine::getSong()->setTempo(command.i0);
		break;

	case ScriptCommand::Type::SetMasterVolume:
		// The enqueue side fills i0 (see LuaSong::setMasterVolume); f0 is
		// unused for this command and reading it always applied 0.
		Engine::getSong()->setMasterVolume(command.i0);
		break;

	case ScriptCommand::Type::SetTrackName:
		if (auto* track = static_cast<Track*>(command.object0))
		{
			track->setName(QString::fromUtf8(command.text));
		}
		break;

	case ScriptCommand::Type::SetTrackVolume:
		if (auto* track = dynamic_cast<InstrumentTrack*>(static_cast<Track*>(command.object0)))
		{
			track->setVolume(static_cast<int>(command.f0));
		}
		break;

	case ScriptCommand::Type::SetModelValue:
		if (auto* model = static_cast<AutomatableModel*>(command.object0))
		{
			model->setValue(command.f0);
		}
		break;

	case ScriptCommand::Type::Play:
		Engine::getSong()->playSong();
		break;

	case ScriptCommand::Type::Stop:
		Engine::getSong()->stop();
		break;

	case ScriptCommand::Type::AddCheckPoint:
		if (auto* object = static_cast<JournallingObject*>(command.object0))
		{
			object->addJournalCheckPoint();
		}
		break;

	case ScriptCommand::Type::EmitMidiNote:
		logMessage(QStringLiteral("MIDI out: %1 ch%2 key%3 vel%4")
				.arg(command.i3 != 0 ? QStringLiteral("note-on") : QStringLiteral("note-off"))
				.arg(command.i2)
				.arg(command.i0)
				.arg(command.i1));
		break;
	}
}


int ScriptEngine::pendingCommandCount() const
{
	return static_cast<int>(m_commands.pending());
}


quint64 ScriptEngine::droppedCommandCount() const
{
	return m_commands.dropped();
}


bool ScriptEngine::audioThreadTick()
{
	// Audio-thread safe: two relaxed atomic reads, no allocation, no lock.
	return m_commands.pending() != 0;
}


void ScriptEngine::setAutoApply(bool enabled)
{
	m_autoApply.store(enabled, std::memory_order_relaxed);
}


bool ScriptEngine::autoApply() const
{
	return m_autoApply.load(std::memory_order_relaxed);
}


void ScriptEngine::setProjectDir(const QString& dir)
{
	QMutexLocker locker(&m_stateMutex);
	m_projectDir = QDir(dir).absolutePath();
}


QString ScriptEngine::projectDir() const
{
	QMutexLocker locker(&m_stateMutex);
	return m_projectDir;
}


void ScriptEngine::setInstructionBudget(quint64 instructions)
{
	m_instructionBudget.store(instructions, std::memory_order_relaxed);
}


quint64 ScriptEngine::instructionBudget() const
{
	return m_instructionBudget.load(std::memory_order_relaxed);
}


QThread* ScriptEngine::workerThread() const
{
	return m_workerThread;
}


QThread* ScriptEngine::lastApplyThread() const
{
	return m_lastApplyThread.load(std::memory_order_acquire);
}


QStringList ScriptEngine::takeLogMessages()
{
	QMutexLocker locker(&m_stateMutex);
	const QStringList messages = m_logMessages;
	m_logMessages.clear();
	return messages;
}


QStringList ScriptEngine::logMessages() const
{
	QMutexLocker locker(&m_stateMutex);
	return m_logMessages;
}


bool ScriptEngine::lastRunFailed() const
{
	QMutexLocker locker(&m_stateMutex);
	return !m_lastError.isEmpty();
}


QString ScriptEngine::lastError() const
{
	QMutexLocker locker(&m_stateMutex);
	return m_lastError;
}


void ScriptEngine::enqueue(const ScriptCommand& command)
{
	m_commands.push(command);
}


void ScriptEngine::flushCommandsForRead()
{
	// Engine state is only ever mutated by the apply side (spec section 4 /
	// section 10). A read on the worker thread must therefore never apply the
	// queue itself; while a script runs, runOnWorker() pumps apply requests on
	// the apply-side thread and this waits for it to drain. Off the worker (or
	// in the re-entrant inline case, where no pump exists) the caller is the
	// apply side and may apply directly.
	if (QThread::currentThread() == m_workerThread &&
			m_applyPumpActive.load(std::memory_order_acquire))
	{
		if (m_commands.pending() == 0)
		{
			return;
		}
		m_applyRequest.release();
		m_applyDone.acquire();
		return;
	}
	applyCommands(ScriptCommandQueueCapacity);
}


void ScriptEngine::logMessage(const QString& message)
{
	{
		QMutexLocker locker(&m_stateMutex);
		m_logMessages.append(message);
	}
	emit logged(message);
	// The console is the part a script author sees while debugging: without it
	// a print() only lands in the capture buffer above, which nothing renders.
	ScriptConsole::streamLine(message);
}


void ScriptEngine::reportRunResult(RunResult result, const QString& message)
{
	if (result == RunResult::Ok || message.isEmpty())
	{
		return;
	}
	// A script that died must say so on the console as well as in the returned
	// error: an author debugging by prints would otherwise see the output stop
	// with no reason. Console only - m_lastError already carries the verdict for
	// programmatic callers.
	logMessage(message);
}


QString ScriptEngine::resolveProjectPath(const QString& path, bool forWrite, QString* error) const
{
	const QString root = QDir(projectDir()).canonicalPath();
	if (root.isEmpty())
	{
		if (error != nullptr)
		{
			*error = QStringLiteral("sandbox root does not exist");
		}
		return QString();
	}

	QString candidate = QDir(root).absoluteFilePath(path);
	if (QFileInfo(candidate).isAbsolute() && !candidate.startsWith(root))
	{
		if (error != nullptr)
		{
			*error = QStringLiteral("path '%1' escapes the project directory").arg(path);
		}
		return QString();
	}

	// Canonicalize the deepest existing ancestor so symlinks cannot be used to
	// step outside the sandbox.
	QString probe = candidate;
	while (!QFileInfo::exists(probe) && probe.size() > root.size())
	{
		probe = QFileInfo(probe).absolutePath();
	}
	const QString realProbe = QFileInfo(probe).canonicalFilePath();
	if (realProbe != root && !realProbe.startsWith(root + QLatin1Char('/')))
	{
		if (error != nullptr)
		{
			*error = QStringLiteral("path '%1' escapes the project directory").arg(path);
		}
		return QString();
	}
	if (!forWrite && !QFileInfo::exists(candidate))
	{
		if (error != nullptr)
		{
			*error = QStringLiteral("file '%1' does not exist in the project directory").arg(path);
		}
		return QString();
	}
	return candidate;
}


void ScriptEngine::pushMidiInEvent(int channel, int key, int velocity, bool noteOn)
{
	const ScriptMidiEvent event{channel, key, velocity, noteOn ? 1 : 0};
	m_midiIn.write(&event, 1);
}


bool ScriptEngine::popMidiInEvent(ScriptMidiEvent* event)
{
	auto sequence = m_midiInReader.read_max(1);
	if (sequence.size() == 0)
	{
		return false;
	}
	if (event != nullptr)
	{
		*event = sequence[0];
	}
	return true;
}


int ScriptEngine::midiInPending() const
{
	return static_cast<int>(m_midiInReader.read_space());
}


int ScriptEngine::patternCount() const
{
	return Engine::patternStore()->numOfPatterns();
}


int ScriptEngine::patternTrackCount() const
{
	return static_cast<int>(Engine::patternStore()->tracks().size());
}


Track* ScriptEngine::trackAt(int trackIndex) const
{
	const TrackContainer::TrackList& tracks = Engine::patternStore()->tracks();
	if (trackIndex < 0 || trackIndex >= static_cast<int>(tracks.size()))
	{
		return nullptr;
	}
	return tracks[trackIndex];
}


MidiClip* ScriptEngine::patternClipAt(int patternIndex, int trackIndex) const
{
	Track* track = trackAt(trackIndex);
	if (track == nullptr || patternIndex < 0)
	{
		return nullptr;
	}
	return dynamic_cast<MidiClip*>(track->getClip(patternIndex));
}


QString ScriptEngine::parseVersionHeader(const QString& source)
{
	static const QRegularExpression header(
		QStringLiteral("^--!\\s*(?:zene|lmms)-api\\s+(\\d+\\.\\d+)\\s*$"),
		QRegularExpression::MultilineOption);
	const QRegularExpressionMatch match = header.match(source.left(4096));
	return match.hasMatch() ? match.captured(1) : QString();
}


bool ScriptEngine::isCompatibleVersion(const QString& version, QString* reason)
{
	const QStringList parts = version.split(QLatin1Char('.'));
	if (parts.size() != 2)
	{
		if (reason != nullptr)
		{
			*reason = QStringLiteral("malformed version '%1'").arg(version);
		}
		return false;
	}
	bool okMajor = false;
	bool okMinor = false;
	const int major = parts[0].toInt(&okMajor);
	const int minor = parts[1].toInt(&okMinor);
	if (!okMajor || !okMinor)
	{
		if (reason != nullptr)
		{
			*reason = QStringLiteral("malformed version '%1'").arg(version);
		}
		return false;
	}
	if (major != ScriptApi::major())
	{
		if (reason != nullptr)
		{
			*reason = QStringLiteral("API major version %1 is not supported by this build"
					" (implements %2)").arg(major).arg(ScriptApi::version());
		}
		return false;
	}
	if (minor > ScriptApi::minor())
	{
		if (reason != nullptr)
		{
			*reason = QStringLiteral("API version %1.%2 is newer than this build supports"
					" (%3)").arg(major).arg(minor).arg(ScriptApi::version());
		}
		return false;
	}
	return true;
}

} // namespace lmms
