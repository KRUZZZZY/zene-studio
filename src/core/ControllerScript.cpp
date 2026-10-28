/*
 * ControllerScript.cpp - a Lua control-surface script bound to a MIDI port (Q050-081)
 *                                       reversibility contract (0.5.0 F6)
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

#include "ControllerScript.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QTimer>

#include "AudioEngine.h"
#include "CollabPolicy.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "MidiClient.h"
#include "MidiEvent.h"
#include "MidiPort.h"
#include "ScriptCommandBridge.h"

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

namespace lmms
{

namespace
{

constexpr qint64 MaxSourceBytes = 256 * 1024;
constexpr int MaxMessageChars = 512;
constexpr int MaxLogLines = 16;
constexpr int HookEvery = 1000;

QHash<QString, ControllerScript*>& scripts()
{
	static QHash<QString, ControllerScript*> running;
	return running;
}

QString capped(const QString& text)
{
	return text.size() > MaxMessageChars ? text.left(MaxMessageChars - 3) + QStringLiteral("...") : text;
}

QString eventTypeName(int type)
{
	switch (type)
	{
	case MidiNoteOn: return QStringLiteral("note_on");
	case MidiNoteOff: return QStringLiteral("note_off");
	case MidiControlChange: return QStringLiteral("cc");
	case MidiPitchBend: return QStringLiteral("pitch_bend");
	case MidiProgramChange: return QStringLiteral("program");
	case MidiChannelPressure: return QStringLiteral("channel_pressure");
	case MidiKeyPressure: return QStringLiteral("key_pressure");
	default: return QStringLiteral("other");
	}
}

void setField(lua_State* L, const char* key, int value)
{
	lua_pushinteger(L, value);
	lua_setfield(L, -2, key);
}

void openSandbox(lua_State* L)
{
	static const luaL_Reg safeLibs[] = {{LUA_GNAME, luaopen_base}, {LUA_TABLIBNAME, luaopen_table},
		{LUA_STRLIBNAME, luaopen_string}, {LUA_MATHLIBNAME, luaopen_math}, {nullptr, nullptr}};
	for (const luaL_Reg* lib = safeLibs; lib->func != nullptr; ++lib)
	{
		luaL_requiref(L, lib->name, lib->func, 1);
		lua_pop(L, 1);
	}
	for (const char* name : {"dofile", "loadfile", "load", "require", "collectgarbage"})
	{
		lua_pushnil(L);
		lua_setglobal(L, name);
	}
}

} // namespace

ControllerScript* ControllerScript::self(lua_State* L)
{
	return *static_cast<ControllerScript**>(lua_getextraspace(L));
}

//! The instruction budget: called every HookEvery instructions, a Lua error once a call has run
//! InstructionBudget of them (the count is reset before each call).
void ControllerScript::budgetHook(lua_State* L, lua_Debug*)
{
	ControllerScript* script = self(L);
	if (script == nullptr) { return; }
	script->m_instructions += HookEvery;
	if (script->m_instructions > InstructionBudget)
	{
		luaL_error(L, "instruction budget exceeded (%d instructions per call)", static_cast<int>(InstructionBudget));
	}
}

ControllerScript* ControllerScript::start(const Options& options, MidiClient* client, QString* error)
{
	if (client == nullptr && Engine::audioEngine() != nullptr) { client = Engine::audioEngine()->midiClient(); }
	if (client == nullptr) { *error = QStringLiteral("no MIDI client"); return nullptr; }
	if (options.name.isEmpty()) { *error = QStringLiteral("a controller script needs a name"); return nullptr; }
	if (!scripts().contains(options.name) && scripts().size() >= MaxScripts)
	{
		*error = QStringLiteral("%1 controller scripts are running; stop one first").arg(MaxScripts);
		return nullptr;
	}
	if (!client->isRaw())
	{
		if (!options.input.isEmpty() && !client->readablePorts().contains(options.input))
		{
			*error = QStringLiteral("no MIDI input '%1' (inputs: %2)").arg(options.input, client->readablePorts().join(QStringLiteral(", ")));
			return nullptr;
		}
		if (!options.output.isEmpty() && !client->writablePorts().contains(options.output))
		{
			*error = QStringLiteral("no MIDI output '%1' (outputs: %2)").arg(options.output, client->writablePorts().join(QStringLiteral(", ")));
			return nullptr;
		}
	}
	auto* script = new ControllerScript(options, client);
	if (!script->load(error))
	{
		delete script;
		return nullptr;
	}
	stop(options.name);
	scripts().insert(options.name, script);
	if (!options.input.isEmpty()) { script->m_port->subscribeReadablePort(options.input); }
	if (!options.output.isEmpty()) { script->m_port->subscribeWritablePort(options.output); }
	script->m_drainTimer->start();
	script->m_watchTimer->start();
	return script;
}

ControllerScript* ControllerScript::find(const QString& name)
{
	return scripts().value(name, nullptr);
}

bool ControllerScript::stop(const QString& name)
{
	ControllerScript* script = scripts().take(name);
	delete script;
	return script != nullptr;
}

void ControllerScript::stopAll()
{
	for (const QString& name : names()) { stop(name); }
}

QStringList ControllerScript::names()
{
	QStringList out = scripts().keys();
	out.sort();
	return out;
}

ControllerScript::ControllerScript(const Options& options, MidiClient* client) :
	QObject(QCoreApplication::instance()),
	m_options(options),
	m_client(client),
	m_port(std::make_unique<MidiPort>(QStringLiteral("Zene script: %1").arg(options.name), client, this, nullptr,
		MidiPort::Mode::Duplex)),
	m_drainTimer(new QTimer(this)),
	m_watchTimer(new QTimer(this))
{
	m_memory.budget = MemoryBudget;
	m_drainTimer->setInterval(5);
	m_watchTimer->setInterval(1000);
	connect(m_drainTimer, &QTimer::timeout, this, [this] { drain(); });
	connect(m_watchTimer, &QTimer::timeout, this, [this] { checkPorts(); });
	static bool stopsAtQuit = false;
	if (!stopsAtQuit && QCoreApplication::instance() != nullptr)
	{
		// The scripts hold ports of the engine's MIDI client: they go before it does.
		stopsAtQuit = true;
		connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, QCoreApplication::instance(), [] { stopAll(); });
	}
}

ControllerScript::~ControllerScript()
{
	m_drainTimer->stop();
	m_watchTimer->stop();
	m_port.reset();
	if (m_lua != nullptr) { lua_close(m_lua); }
}

bool ControllerScript::load(QString* error)
{
	QFile file(m_options.path);
	if (!QFileInfo(m_options.path).isFile() || !file.open(QIODevice::ReadOnly))
	{
		*error = QStringLiteral("cannot read the script '%1'").arg(m_options.path);
		return false;
	}
	if (file.size() > MaxSourceBytes)
	{
		*error = QStringLiteral("the script is %1 bytes; the limit is %2").arg(file.size()).arg(MaxSourceBytes);
		return false;
	}
	const QByteArray source = file.readAll();
	m_lua = lua_newstate(&ScriptMemoryState::allocate, &m_memory);
	if (m_lua == nullptr) { *error = QStringLiteral("cannot create a Lua state"); return false; }
	*static_cast<ControllerScript**>(lua_getextraspace(m_lua)) = this;
	openSandbox(m_lua);
	lua_newtable(m_lua);
	lua_pushcfunction(m_lua, &ControllerScript::luaCommand);
	lua_setfield(m_lua, -2, "command");
	lua_setglobal(m_lua, "zene");
	lua_newtable(m_lua);
	lua_pushcfunction(m_lua, &ControllerScript::luaSendCc);
	lua_setfield(m_lua, -2, "send_cc");
	lua_pushcfunction(m_lua, &ControllerScript::luaSendNote);
	lua_setfield(m_lua, -2, "send_note");
	lua_pushcfunction(m_lua, &ControllerScript::luaLog);
	lua_setfield(m_lua, -2, "log");
	lua_setglobal(m_lua, "surface");
	lua_sethook(m_lua, &ControllerScript::budgetHook, LUA_MASKCOUNT, HookEvery);

	const QByteArray chunk = "@" + QFileInfo(m_options.path).fileName().toUtf8();
	if (luaL_loadbuffer(m_lua, source.constData(), static_cast<size_t>(source.size()), chunk.constData()) != LUA_OK)
	{
		*error = capped(QString::fromUtf8(lua_tostring(m_lua, -1)));
		return false;
	}
	m_instructions = 0;
	if (lua_pcall(m_lua, 0, 0, 0) != LUA_OK)
	{
		*error = capped(QString::fromUtf8(lua_tostring(m_lua, -1)));
		return false;
	}
	call("on_start", nullptr);
	return true;
}

void ControllerScript::processInEvent(const MidiEvent& event, const TimePos&, f_cnt_t)
{
	// Any thread the MIDI client delivers on, the audio thread included: a copy into a fixed slot and two
	// atomics. Nothing allocates, locks, waits or signals here.
	m_received.fetch_add(1, std::memory_order_relaxed);
	const std::uint64_t write = m_inboxWrite.load(std::memory_order_relaxed);
	const std::uint64_t read = m_inboxRead.load(std::memory_order_acquire);
	if (write - read >= InboxCapacity)
	{
		m_dropped.fetch_add(1, std::memory_order_relaxed);
		return;
	}
	m_inbox[write % InboxCapacity] = Packed{static_cast<std::uint8_t>(event.type()),
		static_cast<std::uint8_t>(event.channel()), event.param(0), event.param(1)};
	m_inboxWrite.store(write + 1, std::memory_order_release);
}

int ControllerScript::drain()
{
	int ran = 0;
	while (ran < MaxEventsPerTurn)
	{
		const std::uint64_t read = m_inboxRead.load(std::memory_order_relaxed);
		if (read == m_inboxWrite.load(std::memory_order_acquire)) { break; }
		const Packed event = m_inbox[read % InboxCapacity];
		m_inboxRead.store(read + 1, std::memory_order_release);
		if (m_faulted)
		{
			++m_droppedWhileFaulted;
			continue;
		}
		call("on_midi", &event);
		++m_handled;
		++ran;
	}
	return ran;
}

bool ControllerScript::call(const char* function, const Packed* event)
{
	if (lua_getglobal(m_lua, function) != LUA_TFUNCTION)
	{
		lua_pop(m_lua, 1);
		return true;
	}
	int args = 0;
	if (event != nullptr)
	{
		lua_createtable(m_lua, 0, 6);
		const QByteArray type = eventTypeName(event->type).toUtf8();
		lua_pushstring(m_lua, type.constData());
		lua_setfield(m_lua, -2, "type");
		setField(m_lua, "channel", event->channel + 1);
		const bool isNote = event->type == MidiNoteOn || event->type == MidiNoteOff || event->type == MidiKeyPressure;
		if (isNote)
		{
			setField(m_lua, "key", event->param0);
			setField(m_lua, "velocity", event->param1);
		}
		else if (event->type == MidiControlChange)
		{
			setField(m_lua, "controller", event->param0);
			setField(m_lua, "value", event->param1);
		}
		else { setField(m_lua, "value", event->param0); }
		args = 1;
	}
	m_instructions = 0;
	m_feedbackThisCall = 0;
	if (lua_pcall(m_lua, args, 0, 0) != LUA_OK)
	{
		fail(QString::fromUtf8(lua_tostring(m_lua, -1)));
		lua_pop(m_lua, 1);
		return false;
	}
	m_consecutiveErrors = 0;
	return true;
}

void ControllerScript::fail(const QString& message)
{
	++m_errors;
	++m_consecutiveErrors;
	m_lastError = capped(message);
	m_log.append(QStringLiteral("error: ") + m_lastError);
	while (m_log.size() > MaxLogLines) { m_log.removeFirst(); }
	qWarning("controller script '%s': %s", qPrintable(m_options.name), qPrintable(m_lastError));
	if (m_consecutiveErrors >= MaxConsecutiveErrors && !m_faulted)
	{
		m_faulted = true;
		m_log.append(QStringLiteral("stopped after %1 errors in a row; start it again to retry").arg(MaxConsecutiveErrors));
	}
}

void ControllerScript::checkPorts()
{
	if (m_options.input.isEmpty() || m_client->isRaw()) { return; }
	const bool present = m_client->readablePorts().contains(m_options.input);
	if (!present && !m_portLost)
	{
		m_portLost = true;
		++m_portLosses;
		m_lastError = QStringLiteral("the MIDI input '%1' went away; waiting for it").arg(m_options.input);
		m_log.append(m_lastError);
		qWarning("controller script '%s': %s", qPrintable(m_options.name), qPrintable(m_lastError));
	}
	else if (present && m_portLost)
	{
		m_portLost = false;
		++m_reconnects;
		m_port->subscribeReadablePort(m_options.input, true);
		if (!m_options.output.isEmpty() && m_client->writablePorts().contains(m_options.output))
		{
			m_port->subscribeWritablePort(m_options.output, true);
		}
		m_log.append(QStringLiteral("the MIDI input '%1' is back").arg(m_options.input));
	}
	while (m_log.size() > MaxLogLines) { m_log.removeFirst(); }
}

QJsonObject ControllerScript::state() const
{
	return {{QStringLiteral("name"), m_options.name}, {QStringLiteral("path"), m_options.path},
		{QStringLiteral("input"), m_options.input}, {QStringLiteral("output"), m_options.output},
		{QStringLiteral("running"), !m_faulted}, {QStringLiteral("faulted"), m_faulted},
		{QStringLiteral("port_lost"), m_portLost},
		{QStringLiteral("received"), static_cast<double>(m_received.load(std::memory_order_relaxed))},
		{QStringLiteral("dropped"), static_cast<double>(m_dropped.load(std::memory_order_relaxed) + m_droppedWhileFaulted)},
		{QStringLiteral("handled"), static_cast<double>(m_handled)},
		{QStringLiteral("commands"), static_cast<double>(m_commands)},
		{QStringLiteral("feedback_sent"), static_cast<double>(m_feedbackSent)},
		{QStringLiteral("errors"), static_cast<double>(m_errors)},
		{QStringLiteral("port_losses"), static_cast<double>(m_portLosses)},
		{QStringLiteral("reconnects"), static_cast<double>(m_reconnects)},
		{QStringLiteral("last_error"), m_lastError}, {QStringLiteral("log"), QJsonArray::fromStringList(m_log)}};
}

//! zene.command(id, args) -> result | nil, message, kind
int ControllerScript::luaCommand(lua_State* L)
{
	ControllerScript* script = self(L);
	const QString id = QString::fromUtf8(luaL_checkstring(L, 1));
	const QJsonObject args = ScriptCommandBridge::tableToObject(L, 2);
	QString refusal = ScriptCommandBridge::refusalFor(id);
	if (refusal.isEmpty() && collab::hasPathLikeArgument(args))
	{
		refusal = QStringLiteral("a controller script may not name a file");
	}
	ControlResult result = refusal.isEmpty() ? ControlRegistry::instance()->invoke(id, args)
		: ControlResult::failure(ControlErrorKind::Refused, refusal);
	++script->m_commands;
	if (!result.ok)
	{
		lua_pushnil(L);
		lua_pushstring(L, result.errorMessage.toUtf8().constData());
		lua_pushstring(L, controlErrorKindName(result.errorKind).toUtf8().constData());
		return 3;
	}
	result.result.remove(QStringLiteral("__transaction"));
	ScriptCommandBridge::pushJsonObject(L, result.result);
	return 1;
}

namespace
{

bool takeFeedbackSlot(lua_State* L, int* used)
{
	if (*used >= ControllerScript::MaxFeedbackPerCall)
	{
		luaL_error(L, "more than %d feedback messages in one call", ControllerScript::MaxFeedbackPerCall);
	}
	++*used;
	return true;
}

} // namespace

//! surface.send_cc(channel 1..16, controller 0..127, value 0..127)
int ControllerScript::luaSendCc(lua_State* L)
{
	ControllerScript* script = self(L);
	const lua_Integer channel = luaL_checkinteger(L, 1);
	const lua_Integer controller = luaL_checkinteger(L, 2);
	const lua_Integer value = luaL_checkinteger(L, 3);
	luaL_argcheck(L, channel >= 1 && channel <= 16, 1, "channel is 1..16");
	luaL_argcheck(L, controller >= 0 && controller <= 127, 2, "controller is 0..127");
	luaL_argcheck(L, value >= 0 && value <= 127, 3, "value is 0..127");
	takeFeedbackSlot(L, &script->m_feedbackThisCall);
	script->m_port->processOutEvent(MidiEvent(MidiControlChange, static_cast<int8_t>(channel - 1),
		static_cast<int16_t>(controller), static_cast<int16_t>(value)), TimePos(), 0);
	++script->m_feedbackSent;
	return 0;
}

//! surface.send_note(channel 1..16, key 0..127, velocity 0..127; 0 = note off)
int ControllerScript::luaSendNote(lua_State* L)
{
	ControllerScript* script = self(L);
	const lua_Integer channel = luaL_checkinteger(L, 1);
	const lua_Integer key = luaL_checkinteger(L, 2);
	const lua_Integer velocity = luaL_checkinteger(L, 3);
	luaL_argcheck(L, channel >= 1 && channel <= 16, 1, "channel is 1..16");
	luaL_argcheck(L, key >= 0 && key <= 127, 2, "key is 0..127");
	luaL_argcheck(L, velocity >= 0 && velocity <= 127, 3, "velocity is 0..127");
	takeFeedbackSlot(L, &script->m_feedbackThisCall);
	script->m_port->processOutEvent(MidiEvent(velocity > 0 ? MidiNoteOn : MidiNoteOff, static_cast<int8_t>(channel - 1),
		static_cast<int16_t>(key), static_cast<int16_t>(velocity)), TimePos(), 0);
	++script->m_feedbackSent;
	return 0;
}

//! surface.log(text)
int ControllerScript::luaLog(lua_State* L)
{
	ControllerScript* script = self(L);
	script->m_log.append(capped(QString::fromUtf8(luaL_checkstring(L, 1))));
	while (script->m_log.size() > MaxLogLines) { script->m_log.removeFirst(); }
	return 0;
}

} // namespace lmms
