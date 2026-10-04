/*
 * DeviceActionMenu.cpp - a device card's one action menu: its button and its right-click offer the same entries
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

#include "DeviceActionMenu.h"

#include <algorithm>

#include <QMenu>
#include <QPointer>

#include "CommandForm.h"
#include "CommandLists.h"
#include "DevicePanel.h"
#include "DevicePicker.h"
#include "Effect.h"
#include "EffectChain.h"
#include "EffectDeviceMenu.h"
#include "EffectHostingMenu.h"

namespace lmms::gui
{

namespace
{

ControlResult dispatch(QWidget* parent, const QString& id, const QJsonObject& args, bool ask = false)
{
	return runCommand(parent, CommandCall{id, args, {}, QString(), ask});
}

//! {target, plugin} of @a device: what plugin.bypass, plugin.unload and the preset commands name (a rack chain's device
//! is found on its channel by its fx-<n>).
QJsonObject self(const QJsonObject& device)
{
	return {{QStringLiteral("target"), device.value(QStringLiteral("target"))}, {QStringLiteral("plugin"), device.value(QStringLiteral("plugin"))}};
}

void addEditorEntry(QMenu* menu, DeviceCard* card)
{
	addCommandEntry(menu, DeviceCard::tr("Open Editor"), "plugin.editor_open", [card = QPointer<DeviceCard>(card)] {
		if (!card.isNull()) { card->openEditor(); }
	});
}

void addPresetMenu(QMenu* menu, const QJsonObject& device)
{
	QWidget* parent = menu->parentWidget();
	QMenu* presets = menu->addMenu(DeviceCard::tr("Presets"));
	addCommandEntry(presets, DeviceCard::tr("Load Preset..."), "plugin.preset_list", [parent, device] {
		showCommandList(parent, CommandLists::devicePresets(device));
	});
	addCommandEntry(presets, DeviceCard::tr("Save Preset As..."), "plugin.preset_save", [parent, fixed = self(device)] {
		dispatch(parent, QStringLiteral("plugin.preset_save"), fixed, true);  // the form asks for the name
	});
}

//! Move Left / Move Right: plugin.move one slot along the device's own chain; the ends are offered disabled.
void addMoveEntries(QMenu* menu, const Effect* effect, const QJsonObject& device)
{
	const std::vector<Effect*>& effects = effect->effectChain()->effects();
	const int slot = static_cast<int>(std::find(effects.begin(), effects.end(), effect) - effects.begin());
	const int last = static_cast<int>(effects.size()) - 1;
	QWidget* parent = menu->parentWidget();
	for (const auto& [text, to] : {std::pair{DeviceCard::tr("Move Left"), slot - 1}, std::pair{DeviceCard::tr("Move Right"), slot + 1}})
	{
		QJsonObject args = device;
		args.insert(QStringLiteral("position"), to);
		QAction* move = addCommandEntry(menu, text, "plugin.move", [parent, args] { dispatch(parent, QStringLiteral("plugin.move"), args); });
		move->setEnabled(to >= 0 && to <= last);
	}
}

void addEffectActions(QMenu* menu, DeviceCard* card)
{
	Effect* effect = card->spec().effect;
	const QJsonObject device = deviceAddress(effect);
	if (device.isEmpty()) { return; }
	QWidget* parent = menu->parentWidget();
	const bool bypassed = !effect->isEnabled();
	QAction* bypass = addCommandEntry(menu, DeviceCard::tr("Bypass"), "plugin.bypass", [parent, device, bypassed] {
		QJsonObject args = self(device);
		args.insert(QStringLiteral("bypass"), !bypassed);
		dispatch(parent, QStringLiteral("plugin.bypass"), args);
	});
	bypass->setCheckable(true);
	bypass->setChecked(bypassed);  // a check mark AND the card's state line say it: not colour alone
	addEditorEntry(menu, card);
	addPresetMenu(menu, device);
	addAbMenu(menu, device);
	addReplaceEntry(menu, device);
	addMoveEntries(menu, effect, device);
	addCommandEntry(menu, DeviceCard::tr("Remove"), "plugin.unload", [parent, fixed = self(device)] {
		dispatch(parent, QStringLiteral("plugin.unload"), fixed);
	});
	menu->addSeparator();
	addInsertAndCopyEntries(menu, effect, false);
	addParameterMenus(menu, effect);
	addEffectHostingMenu(menu, effect);
}

void addInstrumentActions(QMenu* menu, DeviceCard* card)
{
	const QJsonObject device{{QStringLiteral("target"), card->spec().target}, {QStringLiteral("plugin"), QStringLiteral("inst")}};
	QWidget* parent = menu->parentWidget();
	addEditorEntry(menu, card);
	addPresetMenu(menu, device);
	addCommandEntry(menu, DeviceCard::tr("Replace Instrument..."), "plugin.load", [parent, target = card->spec().target] {
		const QString chosen = DevicePicker::pick(parent, DevicePicker::Kind::Instrument, DeviceCard::tr("Replace the instrument with"));
		if (!chosen.isEmpty())
		{
			dispatch(parent, QStringLiteral("plugin.load"), {{QStringLiteral("target"), target}, {QStringLiteral("device"), chosen}});
		}
	});
}

} // namespace

void fillDeviceActionMenu(QMenu* menu, DeviceCard* card)
{
	if (card->spec().effect != nullptr) { addEffectActions(menu, card); }
	else if (card->spec().instrumentTrack != nullptr) { addInstrumentActions(menu, card); }
}

} // namespace lmms::gui
