/*
 * ScriptDawEdit.cpp - the Lua API's DAW-control WRITE half (SPEC A11-A16).
 *
 * A script never mutates engine state on its own thread. Every write here
 * becomes one ScriptCommand on the engine's SPSC queue and is applied by the
 * apply side (ScriptEngine::processCommands / flushCommandsForRead), the same
 * route the pattern-editing binding already uses.
 *
 * The fixed command vocabulary grows by exactly ONE type (ScriptCommand::Type
 * ::DawEdit) carrying an opcode in i0, because ScriptEngine::applyCommand is a
 * grandfathered cyclomatic-ratchet entry in tests/complexity-baseline.tsv: one
 * new `case` label per op would move a number this lane may not move. The
 * dispatch over ScriptDawBindings::Op lives in applyDawEdit() below, which is
 * its own (small) function, so both the ratchet and the hot switch stay put.
 *
 * The read-back rule: a queued write is only in the engine after the apply side
 * has drained the queue, so handlers that must hand the script an object
 * created by a queued op (Mixer::addChannel, EffectChain::loadEffect) flush
 * before they resolve it - and the op helpers here log a refusal through the
 * script console when the engine refuses, because "the effect did not appear"
 * is not an answer a script can act on.
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "ScriptDawBindings.h"

#include "AutomatableModel.h"
#include "ControlDeviceSupport.h"
#include "ControlReversibility.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "Mixer.h"
#include "ScriptEngine.h"

#include <QString>
#include <QStringList>

#include <cstring>
#include <memory>

namespace lmms
{

namespace
{

//! One daw edit on the queue: op in i0, target in object0, a name in text.
ScriptCommand dawCommand(ScriptDawBindings::Op op, void* target, int i1 = 0)
{
	ScriptCommand command;
	command.type = ScriptCommand::Type::DawEdit;
	command.object0 = target;
	command.i0 = static_cast<std::int32_t>(op);
	command.i1 = static_cast<std::int32_t>(i1);
	return command;
}

void enqueueDaw(ScriptCommand command)
{
	ScriptEngine::instance()->enqueue(command);
}

//! A rename travels in the fixed 256-byte payload, like SetTrackName's.
ScriptCommand namedDawCommand(ScriptDawBindings::Op op, void* target, const QString& name)
{
	ScriptCommand command = dawCommand(op, target);
	const QByteArray utf8 = name.toUtf8();
	std::strncpy(command.text, utf8.constData(), sizeof(command.text) - 1);
	return command;
}

//! Write one model through the existing vocabulary, with the checkpoint that
//! makes the write undoable queued IMMEDIATELY BEFORE it (the queue is FIFO,
//! and both are applied by the same drain).
void enqueueModelWrite(AutomatableModel* model, float value)
{
	if (model == nullptr) { return; }
	ScriptEngine* engine = ScriptEngine::instance();
	ScriptCommand checkpoint;
	checkpoint.type = ScriptCommand::Type::AddCheckPoint;
	checkpoint.object0 = static_cast<JournallingObject*>(model);
	engine->enqueue(checkpoint);

	ScriptCommand write;
	write.type = ScriptCommand::Type::SetModelValue;
	write.object0 = model;
	write.f0 = value;
	engine->enqueue(write);
}

//! Say why a queued op did nothing. The queue is one-way, so a refusal is
//! reported where the script's own print() goes rather than dropped.
void logRefusal(const QString& what, const ControlResult& error)
{
	ScriptEngine* engine = ScriptEngine::instance();
	engine->logMessage(error.errorMessage.isEmpty()
		? QStringLiteral("[error] %1 refused").arg(what)
		: QStringLiteral("[error] %1 refused: %2").arg(what, error.errorMessage));
}

//! The catalogue entry \a reference names: a "dev-<n>" id (plugin.list), or a
//! plugin name this build lists. The id form is the control surface's own
//! vocabulary; the name form is what a script author actually knows.
bool resolveDeviceReference(const QString& reference, ControlDeviceEntry* entry, int* index,
	ControlResult* error)
{
	if (reference.startsWith(QStringLiteral("dev-")))
	{
		return controlDeviceById(reference, entry, index, error);
	}
	const QList<ControlDeviceEntry> catalogue = controlDeviceCatalogue();
	int matches = 0;
	for (int i = 0; i < catalogue.size(); ++i)
	{
		const ControlDeviceEntry& candidate = catalogue.at(i);
		if (candidate.name != reference && candidate.displayName != reference) { continue; }
		if (matches == 0) { *entry = candidate; *index = i; }
		++matches;
	}
	if (matches == 1) { return true; }
	const QString why = matches == 0
		? QStringLiteral("no device named '%1' in this build's catalogue (plugin.list)")
		: QStringLiteral("'%1' names %2 devices; use the dev-<n> id instead")
			.arg(reference).arg(matches);
	*error = ControlResult::failure(matches == 0 ? ControlErrorKind::NotFound
		: ControlErrorKind::InvalidArgs, why);
	return false;
}

// ---------------------------------------------------------------------------
// The four apply-side ops
// ---------------------------------------------------------------------------

//! Append a mixer channel. A16: a fresh channel carries only defaults, so the
//! inverse is the OPERATION - one action step deletes the channel it created,
//! through the same Mixer::deleteChannel path mixer.remove_channel uses.
void applyCreateChannel(Mixer* mixer)
{
	if (mixer == nullptr) { return; }
	const int before = static_cast<int>(mixer->numChannels());
	control::addUndoStep(
		[mixer, before]() {
			if (static_cast<int>(mixer->numChannels()) > before)
			{
				mixer->deleteChannel(static_cast<int>(mixer->numChannels()) - 1);
			}
		},
		[mixer]() { mixer->createChannel(); });
	mixer->createChannel();
}

void applyLoadEffect(EffectChain* chain, const QString& reference)
{
	if (chain == nullptr) { return; }
	ControlResult error;
	ControlDeviceEntry entry;
	int deviceIndex = -1;
	if (!resolveDeviceReference(reference, &entry, &deviceIndex, &error))
	{
		logRefusal(QStringLiteral("loading '%1'").arg(reference), error);
		return;
	}
	if (!entry.loadable)
	{
		error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("device '%1' is not loadable in this build").arg(reference));
		logRefusal(QStringLiteral("loading '%1'").arg(reference), error);
		return;
	}
	Effect* effect = controlInstantiateDevice(entry, chain, &error);
	if (effect == nullptr)
	{
		logRefusal(QStringLiteral("loading '%1'").arg(reference), error);
		return;
	}
	// SPEC A16: the same recorded action step plugin.load records - unloading
	// the instance it appended restores the chain exactly.
	auto holder = std::make_shared<Effect*>(effect);
	EffectChain* owned = chain;
	control::addUndoStep(
		[holder, owned]() {
			if (*holder != nullptr)
			{
				owned->removeEffect(*holder);
				(*holder)->deleteLater();
				*holder = nullptr;
			}
		},
		[holder, owned, entry]() {
			ControlResult ignored;
			*holder = controlInstantiateDevice(entry, owned, &ignored);
		});
}

void applyRemoveEffect(EffectChain* chain, int index)
{
	if (chain == nullptr) { return; }
	const std::vector<Effect*>& effects = chain->effects();
	if (index < 0 || index >= static_cast<int>(effects.size()))
	{
		ControlResult error = ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("no effect %1 in this chain (it has %2)").arg(index).arg(effects.size()));
		logRefusal(QStringLiteral("removing effect %1").arg(index), error);
		return;
	}
	Effect* effect = effects[index];
	chain->removeEffect(effect);
	effect->deleteLater();
}

void applySetEffectEnabled(Effect* effect, bool enabled)
{
	if (effect == nullptr) { return; }
	// ProjectJournal: the Effect is a JournallingObject (Plugin's base), and
	// the checkpoint is taken before the switch, so the GUI's Ctrl+Z and
	// control.undo meet the same write.
	effect->addJournalCheckPoint();
	effect->setEnabled(enabled);
}

void applySetChannelName(MixerChannel* channel, const QString& name)
{
	if (channel == nullptr) { return; }
	// MixerChannel::m_name is a plain QString: not journalled, so the binding
	// claims no inverse for a rename (docs/LUA-API-STABILISATION.md, withheld).
	channel->m_name = name;
}

//! Dispatch one queued daw edit. One decision per op, and the four helpers
//! above keep every branch out of ScriptEngine::applyCommand.
void applyDawEdit(const ScriptCommand& command)
{
	const ScriptDawBindings::Op op = static_cast<ScriptDawBindings::Op>(command.i0);
	switch (op)
	{
	case ScriptDawBindings::Op::CreateChannel:
		applyCreateChannel(static_cast<Mixer*>(command.object0));
		return;
	case ScriptDawBindings::Op::LoadEffect:
		applyLoadEffect(static_cast<EffectChain*>(command.object0),
			QString::fromUtf8(command.text));
		return;
	case ScriptDawBindings::Op::RemoveEffect:
		applyRemoveEffect(static_cast<EffectChain*>(command.object0), command.i1);
		return;
	case ScriptDawBindings::Op::SetEffectEnabled:
		applySetEffectEnabled(static_cast<Effect*>(command.object0), command.i1 != 0);
		return;
	case ScriptDawBindings::Op::SetChannelName:
		applySetChannelName(static_cast<MixerChannel*>(command.object0),
			QString::fromUtf8(command.text));
		return;
	}
}

} // namespace

namespace ScriptDawBindings
{

bool applyQueuedCommand(const ScriptCommand& command)
{
	if (command.type != ScriptCommand::Type::DawEdit)
	{
		return false;
	}
	applyDawEdit(command);
	return true;
}

} // namespace ScriptDawBindings

// ---------------------------------------------------------------------------
// LuaMixer
// ---------------------------------------------------------------------------

LuaMixerChannel& LuaMixer::addChannel() const
{
	Mixer* mixer = Engine::mixer();
	if (mixer == nullptr)
	{
		return ScriptDawBindings::newMixerChannel(nullptr);
	}
	const int before = static_cast<int>(mixer->numChannels());
	enqueueDaw(dawCommand(ScriptDawBindings::Op::CreateChannel, mixer));
	// Applied now, not "eventually": the wrapper this call returns must
	// address the channel it just asked for (LuaPatternStore::addInstrumentTrack
	// resolves its track the same way).
	ScriptEngine::instance()->flushCommandsForRead();
	if (static_cast<int>(mixer->numChannels()) <= before)
	{
		return ScriptDawBindings::newMixerChannel(nullptr);
	}
	return ScriptDawBindings::newMixerChannel(mixer->mixerChannel(before));
}

// ---------------------------------------------------------------------------
// LuaMixerChannel
// ---------------------------------------------------------------------------

void LuaMixerChannel::setGain(float gain)
{
	if (m_channel == nullptr) { return; }
	enqueueModelWrite(&m_channel->m_volumeModel, gain);
}

void LuaMixerChannel::setMuted(bool muted)
{
	if (m_channel == nullptr) { return; }
	enqueueModelWrite(&m_channel->m_muteModel, muted ? 1.0f : 0.0f);
}

void LuaMixerChannel::setSoloed(bool soloed)
{
	if (m_channel == nullptr) { return; }
	enqueueModelWrite(&m_channel->m_soloModel, soloed ? 1.0f : 0.0f);
}

void LuaMixerChannel::setName(const QString& name)
{
	if (m_channel == nullptr) { return; }
	enqueueDaw(namedDawCommand(ScriptDawBindings::Op::SetChannelName, m_channel, name));
}

// ---------------------------------------------------------------------------
// LuaEffectChain / LuaEffect
// ---------------------------------------------------------------------------

LuaEffect& LuaEffectChain::loadEffect(const QString& device) const
{
	if (m_chain == nullptr)
	{
		return ScriptDawBindings::newEffect(nullptr, nullptr);
	}
	const int before = effectCount();
	enqueueDaw(namedDawCommand(ScriptDawBindings::Op::LoadEffect, m_chain, device));
	ScriptEngine::instance()->flushCommandsForRead();
	// The count is the only honest witness that the load happened: the queue
	// carries no reply. A refusal was logged for the script (logRefusal).
	return effect(effectCount() > before ? effectCount() - 1 : -1);
}

void LuaEffectChain::removeEffect(int index)
{
	if (m_chain == nullptr) { return; }
	enqueueDaw(dawCommand(ScriptDawBindings::Op::RemoveEffect, m_chain, index));
	ScriptEngine::instance()->flushCommandsForRead();
}

void LuaEffect::setEnabled(bool enabled)
{
	Effect* effect = m_effective();
	if (effect == nullptr)
	{
		logRefusal(QStringLiteral("switching an effect that is no longer in its chain"),
			ControlResult());
		return;
	}
	enqueueDaw(dawCommand(ScriptDawBindings::Op::SetEffectEnabled, effect, enabled ? 1 : 0));
}

} // namespace lmms
