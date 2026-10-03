/*
 * EffectDeviceMenu.cpp - an effect's device entries: insert beside it, copy it, compare two settings
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
#include "EffectDeviceMenu.h"

#include <algorithm>

#include <QJsonArray>
#include <QMenu>

#include "ChannelPickDialog.h"
#include "CommandForm.h"
#include "CommandLists.h"
#include "ControlDeviceSupport.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Effect.h"
#include "EffectChain.h"
#include "EffectPickerOrganiser.h"
#include "EffectSelectDialog.h"
#include "Engine.h"
#include "Mixer.h"
#include "PluginNativeEditor.h"
#include "PluginOrganiser.h"

namespace lmms::gui
{

namespace
{

//! The catalogue's dev-<n> for the picker's @a key, or empty: the same organiser key on both sides.
QString deviceIdOf(const EffectKey& key)
{
	const QString wanted = pickerorganiser::keyOf(key);
	const QList<ControlDeviceEntry> catalogue = controlDeviceCatalogue();
	for (int i = 0; i < catalogue.size(); ++i)
	{
		if (pluginorganiser::keyOf(catalogue.at(i)) == wanted) { return control::deviceId(i); }
	}
	return QString();
}

int slotOf(const Effect* effect)
{
	const std::vector<Effect*>& effects = effect->effectChain()->effects();
	return static_cast<int>(std::find(effects.begin(), effects.end(), effect) - effects.begin());
}

void insertBeside(QWidget* parent, Effect* effect, bool after)
{
	EffectSelectDialog picker(parent);
	if (picker.exec() != QDialog::Accepted) { return; }
	const QString device = deviceIdOf(picker.selectedKey());
	if (device.isEmpty()) { return; }
	QJsonObject args = chainAddress(effect->effectChain());
	args.insert(QStringLiteral("device"), device);
	args.insert(QStringLiteral("position"), slotOf(effect) + (after ? 1 : 0));
	runCommand(parent, CommandCall{QStringLiteral("plugin.load"), args, {}, QString(), false});
}

//! The picker, then plugin.replace: another device in this one's place (L11, WF-016).
void replaceWith(QWidget* parent, const QJsonObject& device)
{
	EffectSelectDialog picker(parent);
	if (picker.exec() != QDialog::Accepted) { return; }
	const QString chosen = deviceIdOf(picker.selectedKey());
	if (chosen.isEmpty()) { return; }
	QJsonObject args = device;
	args.insert(QStringLiteral("device"), chosen);
	runCommand(parent, CommandCall{QStringLiteral("plugin.replace"), args, {}, QString(), false});
}

void copyTo(QWidget* parent, const QJsonObject& self, const char* id, const QString& title)
{
	const QStringList channels = pickChannels(parent, title, self.value(QStringLiteral("target")).toString());
	for (const QString& channel : channels)
	{
		QJsonObject args = self;
		args.insert(QStringLiteral("to"), channel);
		runCommand(parent, CommandCall{QLatin1String(id), args, {}, QString(), false});
	}
}

QAction* entry(QMenu* menu, const QString& text, const char* id, std::function<void()> run)
{
	QAction* action = menu->addAction(text);
	action->setProperty("controlCommand", QLatin1String(id));
	QObject::connect(action, &QAction::triggered, menu, std::move(run));
	return action;
}

} // namespace

QJsonObject chainAddress(const EffectChain* chain)
{
	const QString owner = targetIdOfChain(chain);
	if (!owner.isEmpty()) { return {{QStringLiteral("target"), owner}}; }
	Mixer* mixer = Engine::mixer();
	for (int i = 0; i < static_cast<int>(mixer->numChannels()); ++i)
	{
		const Rack& rack = mixer->mixerChannel(i)->m_rack;
		for (int c = 1; c < rack.chainCount(); ++c)
		{
			if (rack.chain(c) == chain)
			{
				return {{QStringLiteral("target"), control::channelIdOf(mixer->mixerChannel(i))}, {QStringLiteral("chain"), c}};
			}
		}
	}
	return {};
}

void addEffectDeviceMenus(QMenu* menu, Effect* effect)
{
	if (effect == nullptr || effect->effectChain() == nullptr) { return; }
	QJsonObject self = chainAddress(effect->effectChain());
	if (self.isEmpty()) { return; }
	QWidget* parent = menu->parentWidget();
	entry(menu, QObject::tr("Insert Effect Before..."), "plugin.load", [parent, effect] { insertBeside(parent, effect, false); });
	entry(menu, QObject::tr("Insert Effect After..."), "plugin.load", [parent, effect] { insertBeside(parent, effect, true); });
	QJsonObject device = self;
	device.insert(QStringLiteral("plugin"), control::effectIdOf(effect));
	entry(menu, QObject::tr("Replace With..."), "plugin.replace", [parent, device] { replaceWith(parent, device); });
	entry(menu, QObject::tr("Copy to Channels..."), "plugin.copy", [parent, device] {
		copyTo(parent, device, "plugin.copy", QObject::tr("Copy the device to"));
	});
	entry(menu, QObject::tr("Copy Chain to Channels..."), "chain.copy", [parent, self] {
		copyTo(parent, self, "chain.copy", QObject::tr("Copy the chain to"));
	});
	entry(menu, QObject::tr("Presets..."), "plugin.preset_list", [parent, device] {  // L11 (WF-025)
		showCommandList(parent, CommandLists::devicePresets(device));
	});
	QMenu* ab = menu->addMenu(QObject::tr("A/B"));
	const auto abCall = [device](const QString& action, const QString& slot) {
		QJsonObject args = device;
		args.insert(QStringLiteral("action"), action);
		if (!slot.isEmpty()) { args.insert(QStringLiteral("slot"), slot); }
		return CommandCall{QStringLiteral("plugin.ab"), args, {}, QString(), false};
	};
	for (const auto& [text, action, slot] : {std::tuple{QObject::tr("Store in A"), QStringLiteral("store"), QStringLiteral("a")},
			std::tuple{QObject::tr("Store in B"), QStringLiteral("store"), QStringLiteral("b")},
			std::tuple{QObject::tr("Toggle A/B"), QStringLiteral("toggle"), QString()}})
	{
		const CommandCall call = abCall(action, slot);
		entry(ab, text, "plugin.ab", [parent, call] { runCommand(parent, call); });
	}
}

} // namespace lmms::gui
