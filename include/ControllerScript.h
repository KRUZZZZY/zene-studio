/*
 * ControllerScript.h - a Lua control-surface script bound to a MIDI port (Q050-081)
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

#ifndef LMMS_CONTROLLER_SCRIPT_H
#define LMMS_CONTROLLER_SCRIPT_H

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

#include "MidiEventProcessor.h"
#include "ScriptMemoryBudget.h"
#include "lmms_export.h"

struct lua_State;
struct lua_Debug;
class QTimer;

namespace lmms
{

class ControlRegistry;
class MidiClient;
class MidiPort;

/*! A control-surface script (Q050-081): a Lua file bound to one MIDI input and one MIDI output of the MIDI
 *  client. Every event the bound input delivers calls the script's on_midi(event); the script runs registered
 *  commands with zene.command(id, args) - the same registry, refusals and undo as everywhere else - and sends
 *  feedback (LEDs, motor faders) to the bound output with surface.send_cc / surface.send_note.
 *
 *  Threads. The MIDI client delivers on its own thread - with JACK that is the audio thread - so
 *  processInEvent() only copies the event into a fixed lock-free ring: no allocation, no lock, no Qt signal;
 *  a full ring drops and counts. The script runs on the UI thread, drained at most MaxEventsPerTurn per timer
 *  tick, in a sandboxed Lua state (base, table, string and math only; no files, no OS) with an instruction
 *  budget per call and a memory cap. A call that errs is counted and reported; MaxConsecutiveErrors in a row
 *  fault the script, which then drops (and counts) further events until it is started again. The bound input
 *  is watched once a second: when it leaves the client's port list the script reports port_lost and keeps
 *  waiting; when it comes back the binding is made again. Session state: not saved with the project. */
class LMMS_EXPORT ControllerScript : public QObject, public MidiEventProcessor
{
	Q_OBJECT
public:
	static constexpr int InboxCapacity = 256;           //!< events between two drains; more are dropped, counted
	static constexpr int MaxEventsPerTurn = 64;         //!< script calls per UI-thread turn
	static constexpr int MaxConsecutiveErrors = 8;      //!< errors in a row that fault the script
	static constexpr int MaxFeedbackPerCall = 64;       //!< surface.send_* per on_midi call
	static constexpr std::int64_t InstructionBudget = 1000000;  //!< Lua instructions per call
	static constexpr std::size_t MemoryBudget = 8u << 20;      //!< bytes of Lua heap
	static constexpr int MaxScripts = 8;

	struct Options
	{
		QString name;    //!< the script's handle (controller.script_stop)
		QString path;    //!< the Lua file
		QString input;   //!< a readable port of the MIDI client (empty: none)
		QString output;  //!< a writable port of the MIDI client (empty: none)
	};

	//! Start a script on @a client (the engine's when nullptr); replaces a running one of the same name.
	static ControllerScript* start(const Options& options, MidiClient* client, QString* error);
	static ControllerScript* find(const QString& name);
	static bool stop(const QString& name);
	static void stopAll();
	static QStringList names();

	~ControllerScript() override;

	QJsonObject state() const;
	MidiPort& port() { return *m_port; }

	//! MidiEventProcessor: the MIDI client's thread (possibly the audio thread). A lock-free copy, nothing else.
	void processInEvent(const MidiEvent& event, const TimePos& time = TimePos(), f_cnt_t offset = 0) override;
	void processOutEvent(const MidiEvent&, const TimePos& = TimePos(), f_cnt_t = 0) override {}

	//! UI thread: run the script for the events waiting (at most MaxEventsPerTurn); how many it ran.
	int drain();
	//! UI thread: compare the bound input with the client's port list (the one-second watch).
	void checkPorts();

private:
	struct Packed
	{
		std::uint8_t type = 0;
		std::uint8_t channel = 0;
		std::int16_t param0 = 0;
		std::int16_t param1 = 0;
	};

	ControllerScript(const Options& options, MidiClient* client);
	bool load(QString* error);
	bool call(const char* function, const Packed* event);
	void fail(const QString& message);
	void checkOutputPort();
	static int luaCommand(lua_State* L);
	static int luaSendCc(lua_State* L);
	static int luaSendNote(lua_State* L);
	static int luaLog(lua_State* L);
	static ControllerScript* self(lua_State* L);
	static void budgetHook(lua_State* L, lua_Debug* debug);

	Options m_options;
	MidiClient* m_client;
	std::unique_ptr<MidiPort> m_port;
	lua_State* m_lua = nullptr;
	QTimer* m_drainTimer = nullptr;
	QTimer* m_watchTimer = nullptr;

	std::array<Packed, InboxCapacity> m_inbox{};
	std::atomic<std::uint64_t> m_inboxWrite{0};
	std::atomic<std::uint64_t> m_inboxRead{0};
	std::atomic<std::uint64_t> m_received{0};
	std::atomic<std::uint64_t> m_dropped{0};

	ScriptMemoryState m_memory;
	std::int64_t m_instructions = 0;
	int m_feedbackThisCall = 0;
	quint64 m_handled = 0;
	quint64 m_commands = 0;
	quint64 m_feedbackSent = 0;
	quint64 m_errors = 0;
	quint64 m_droppedWhileFaulted = 0;
	int m_consecutiveErrors = 0;
	bool m_faulted = false;
	bool m_portLost = false;
	bool m_outputPortLost = false;
	quint64 m_portLosses = 0;
	quint64 m_reconnects = 0;
	QString m_lastError;
	QStringList m_log;
};

//! controller.script_start / script_stop / script_state (ControlCommandsControllerScripts.cpp).
LMMS_EXPORT void registerControllerScriptCommands(ControlRegistry& registry);

namespace control
{
struct ReversibilityRow;
//! The three commands' rows, joined into reversibilityRowTable().
LMMS_EXPORT const ReversibilityRow* reversibilityControllerScriptRowTable(int* rowCount);
} // namespace control

} // namespace lmms

#endif // LMMS_CONTROLLER_SCRIPT_H
