/*
 * ClipEffectsDialog.cpp - an audio clip's own effects, in the clip's menu (Q050-111)
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
 */

#include "ClipEffectsDialog.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "DevicePicker.h"
#include "EffectChain.h"
#include "SampleClip.h"
#include "UnattendedRun.h"

namespace lmms::gui
{

namespace
{

constexpr int IdRole = Qt::UserRole;          //!< a device row's fx-<n>, a parameter row's device fx-<n>
constexpr int IndexRole = Qt::UserRole + 1;   //!< a device row's slot, a parameter row's parameter index
constexpr int MinRole = Qt::UserRole + 2;
constexpr int MaxRole = Qt::UserRole + 3;
constexpr int ValueRole = Qt::UserRole + 4;

std::function<std::optional<double>(const QString&, double, double, double)> s_valuePrompt;

std::optional<double> promptValue(QWidget* parent, const QString& name, double value, double lowest, double highest)
{
	if (s_valuePrompt) { return s_valuePrompt(name, value, lowest, highest); }
	bool ok = false;
	const double chosen = QInputDialog::getDouble(parent, ClipEffectsDialog::tr("Clip effects"), name, value, lowest,
		highest, 2, &ok);
	return ok ? std::optional<double>(chosen) : std::nullopt;
}

//! The open window of each clip, so the menu raises it instead of opening a second one.
QList<QPointer<ClipEffectsDialog>>& openDialogs()
{
	static QList<QPointer<ClipEffectsDialog>> dialogs;
	return dialogs;
}

} // namespace

ClipEffectsDialog::ClipEffectsDialog(SampleClip* clip, QWidget* parent) :
	QDialog(parent),
	m_clip(clip)
{
	setAttribute(Qt::WA_DeleteOnClose);
	setWindowTitle(tr("Clip effects - %1").arg(clip->name().isEmpty() ? control::clipIdOf(clip) : clip->name()));
	setObjectName(QStringLiteral("ClipEffectsDialog"));

	auto* layout = new QVBoxLayout(this);
	auto* note = new QLabel(tr("These effects process this clip only, before the track's own effects."), this);
	note->setWordWrap(true);
	layout->addWidget(note);

	m_list = new QTreeWidget(this);
	m_list->setAccessibleName(tr("Clip effects"));
	m_list->setColumnCount(2);
	m_list->setHeaderLabels({tr("Device"), tr("Value")});
	m_list->header()->setStretchLastSection(true);
	layout->addWidget(m_list);

	auto* buttons = new QHBoxLayout;
	const auto button = [this, buttons](const QString& text, void (ClipEffectsDialog::*slot)()) {
		auto* b = new QPushButton(text, this);
		connect(b, &QPushButton::clicked, this, slot);
		buttons->addWidget(b);
		return b;
	};
	button(tr("Add effect..."), &ClipEffectsDialog::addEffect);
	m_moveUp = button(tr("Move up"), &ClipEffectsDialog::moveSelectedUp);
	m_moveDown = button(tr("Move down"), &ClipEffectsDialog::moveSelectedDown);
	m_bypass = button(tr("Bypass"), &ClipEffectsDialog::toggleSelectedBypass);
	m_remove = button(tr("Remove"), &ClipEffectsDialog::removeSelected);
	layout->addLayout(buttons);

	connect(m_list, &QTreeWidget::itemSelectionChanged, this, &ClipEffectsDialog::updateButtons);
	connect(m_list, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) { editParameter(item); });
	// An edit that did not come from this window (an agent's command, an undo, a project load) re-reads it.
	connect(clip->clipEffects(), &EffectChain::dataChanged, this, &ClipEffectsDialog::refresh, Qt::QueuedConnection);
	connect(clip, &QObject::destroyed, this, &QWidget::close);

	resize(420, 360);
	refresh();
}


ClipEffectsDialog* ClipEffectsDialog::showFor(SampleClip* clip)
{
	auto& dialogs = openDialogs();
	dialogs.removeAll(QPointer<ClipEffectsDialog>());
	for (const auto& dialog : dialogs)
	{
		if (dialog->m_clip == clip)
		{
			dialog->refresh();
			dialog->raise();
			dialog->activateWindow();
			return dialog;
		}
	}
	auto* dialog = new ClipEffectsDialog(clip, QApplication::activeWindow());
	dialogs.append(dialog);
	dialog->show();
	return dialog;
}


void ClipEffectsDialog::setValuePrompt(
	std::function<std::optional<double>(const QString&, double, double, double)> prompt)
{
	s_valuePrompt = std::move(prompt);
}


QString ClipEffectsDialog::target() const
{
	return m_clip.isNull() ? QString() : control::clipIdOf(m_clip.data());
}


bool ClipEffectsDialog::run(const QString& command, QJsonObject args)
{
	if (m_clip.isNull()) { return false; }
	args.insert(QStringLiteral("target"), target());
	const ControlResult result = ControlRegistry::instance()->invoke(command, args);
	if (result.ok) { return true; }
	if (isUnattendedRun()) { qWarning("%s refused: %s", qPrintable(command), qPrintable(result.errorMessage)); }
	else { QMessageBox::warning(this, tr("Clip effect refused"), result.errorMessage); }
	return false;
}


void ClipEffectsDialog::refresh()
{
	const QString selected = selectedDevice() != nullptr ? selectedDevice()->data(0, IdRole).toString() : QString();
	m_list->clear();
	if (!m_clip.isNull())
	{
		const ControlResult state = ControlRegistry::instance()->invoke(QStringLiteral("dsp.get_state"),
			{{QStringLiteral("target"), target()}});
		const QJsonArray chains = state.result.value(QStringLiteral("chains")).toArray();
		const QJsonArray devices = chains.isEmpty() ? QJsonArray()
			: chains.first().toObject().value(QStringLiteral("devices")).toArray();
		for (const QJsonValue& value : devices)
		{
			const QJsonObject device = value.toObject();
			const QString id = device.value(QStringLiteral("id")).toString();
			auto* row = new QTreeWidgetItem(m_list, {QStringLiteral("%1. %2").arg(device.value(QStringLiteral("index")).toInt() + 1)
				.arg(device.value(QStringLiteral("display_name")).toString()),
				device.value(QStringLiteral("enabled")).toBool() ? tr("On") : tr("Bypassed")});
			row->setData(0, IdRole, id);
			row->setData(0, IndexRole, device.value(QStringLiteral("index")).toInt());
			for (const QJsonValue& p : device.value(QStringLiteral("parameters")).toArray())
			{
				const QJsonObject parameter = p.toObject();
				const double v = parameter.value(QStringLiteral("value")).toDouble();
				auto* child = new QTreeWidgetItem(row, {parameter.value(QStringLiteral("name")).toString(),
					QString::number(v, 'g', 6)});
				child->setData(0, IdRole, id);
				child->setData(0, IndexRole, parameter.value(QStringLiteral("index")).toInt());
				child->setData(0, MinRole, parameter.value(QStringLiteral("min")).toDouble());
				child->setData(0, MaxRole, parameter.value(QStringLiteral("max")).toDouble());
				child->setData(0, ValueRole, v);
			}
		}
	}
	m_list->expandAll();
	selectDevice(selected);
	updateButtons();
}


QTreeWidgetItem* ClipEffectsDialog::selectedDevice() const
{
	const QList<QTreeWidgetItem*> items = m_list->selectedItems();
	if (items.isEmpty()) { return nullptr; }
	QTreeWidgetItem* item = items.first();
	return item->parent() != nullptr ? item->parent() : item;
}


void ClipEffectsDialog::selectDevice(const QString& pluginId)
{
	if (pluginId.isEmpty()) { return; }
	for (int i = 0; i < m_list->topLevelItemCount(); ++i)
	{
		QTreeWidgetItem* row = m_list->topLevelItem(i);
		if (row->data(0, IdRole).toString() == pluginId) { m_list->setCurrentItem(row); return; }
	}
}


void ClipEffectsDialog::updateButtons()
{
	const QTreeWidgetItem* device = selectedDevice();
	const int slot = device != nullptr ? device->data(0, IndexRole).toInt() : -1;
	m_moveUp->setEnabled(device != nullptr && slot > 0);
	m_moveDown->setEnabled(device != nullptr && slot + 1 < m_list->topLevelItemCount());
	m_bypass->setEnabled(device != nullptr);
	m_bypass->setText(device != nullptr && device->text(1) == tr("Bypassed") ? tr("Turn on") : tr("Bypass"));
	m_remove->setEnabled(device != nullptr);
}


void ClipEffectsDialog::addEffect()
{
	const QString device = DevicePicker::pick(this, DevicePicker::Kind::Effect, tr("Add clip effect"));
	if (device.isEmpty()) { return; }
	if (run(QStringLiteral("plugin.load"), {{QStringLiteral("device"), device}})) { refresh(); }
}


void ClipEffectsDialog::moveSelectedUp()
{
	const QTreeWidgetItem* device = selectedDevice();
	if (device == nullptr) { return; }
	const QString id = device->data(0, IdRole).toString();
	const int slot = device->data(0, IndexRole).toInt();
	if (slot <= 0) { return; }
	if (run(QStringLiteral("plugin.move"), {{QStringLiteral("plugin"), id}, {QStringLiteral("position"), slot - 1}}))
	{
		refresh();
		selectDevice(id);
	}
}


void ClipEffectsDialog::moveSelectedDown()
{
	const QTreeWidgetItem* device = selectedDevice();
	if (device == nullptr) { return; }
	const QString id = device->data(0, IdRole).toString();
	const int slot = device->data(0, IndexRole).toInt();
	if (slot + 1 >= m_list->topLevelItemCount()) { return; }
	if (run(QStringLiteral("plugin.move"), {{QStringLiteral("plugin"), id}, {QStringLiteral("position"), slot + 1}}))
	{
		refresh();
		selectDevice(id);
	}
}


void ClipEffectsDialog::toggleSelectedBypass()
{
	const QTreeWidgetItem* device = selectedDevice();
	if (device == nullptr) { return; }
	const QString id = device->data(0, IdRole).toString();
	const bool bypassed = device->text(1) == tr("Bypassed");
	if (run(QStringLiteral("plugin.bypass"), {{QStringLiteral("plugin"), id}, {QStringLiteral("bypass"), !bypassed}}))
	{
		refresh();
		selectDevice(id);
	}
}


void ClipEffectsDialog::removeSelected()
{
	const QTreeWidgetItem* device = selectedDevice();
	if (device == nullptr) { return; }
	if (run(QStringLiteral("plugin.unload"), {{QStringLiteral("plugin"), device->data(0, IdRole).toString()}}))
	{
		refresh();
	}
}


void ClipEffectsDialog::editParameter(QTreeWidgetItem* item)
{
	if (item == nullptr || item->parent() == nullptr) { return; }
	const QString id = item->data(0, IdRole).toString();
	const std::optional<double> value = promptValue(this, item->text(0), item->data(0, ValueRole).toDouble(),
		item->data(0, MinRole).toDouble(), item->data(0, MaxRole).toDouble());
	if (!value) { return; }
	if (run(QStringLiteral("plugin.param_set"), {{QStringLiteral("plugin"), id},
			{QStringLiteral("index"), item->data(0, IndexRole).toInt()}, {QStringLiteral("value"), *value}}))
	{
		refresh();
		selectDevice(id);
	}
}


void addClipEffectsAction(QMenu* menu, Clip* clip)
{
	auto* audio = dynamic_cast<SampleClip*>(clip);
	if (audio == nullptr) { return; }
	const auto count = audio->clipEffects()->effects().size();
	QAction* action = menu->addAction(count == 0 ? QMenu::tr("Clip effects...")
		: QMenu::tr("Clip effects (%1)...").arg(count));
	action->setData(QStringLiteral("plugin.load"));
	action->setToolTip(QMenu::tr("Realtime effects on this clip only, before the track's own effects"));
	QPointer<SampleClip> guarded(audio);
	QObject::connect(action, &QAction::triggered, menu, [guarded] {
		if (!guarded.isNull()) { ClipEffectsDialog::showFor(guarded.data()); }
	});
}


QString clipEffectsBadgeText(const SampleClip& clip)
{
	const auto count = clip.clipEffects()->effects().size();
	if (count == 0) { return QString(); }
	return count == 1 ? QStringLiteral("FX") : QStringLiteral("FX %1").arg(count);
}


void paintClipEffectsBadge(QPainter& p, const QRect& rect, const SampleClip& clip, const QColor& text,
	const QColor& shadow)
{
	const QString badge = clipEffectsBadgeText(clip);
	if (badge.isEmpty()) { return; }
	const int width = p.fontMetrics().horizontalAdvance(badge);
	const int x = rect.right() - width - 6;
	const int y = rect.bottom() - 4;
	p.setPen(shadow);
	p.drawText(x + 1, y + 1, badge);
	p.setPen(text);
	p.drawText(x, y, badge);
}

} // namespace lmms::gui
