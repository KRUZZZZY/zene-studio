/*
 * GrooveMenu.cpp - the piano roll's Groove menu: extract, apply and quantise with strength
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

#include "GrooveMenu.h"

#include <QApplication>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QLineEdit>
#include <QMenu>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "MidiClip.h"

namespace lmms::gui
{

namespace
{

GroovePrompts& prompts()
{
	static GroovePrompts s_prompts;
	return s_prompts;
}

std::optional<QString> askName()
{
	if (prompts().name) { return prompts().name(); }
	bool ok = false;
	const QString name = QInputDialog::getText(QApplication::activeWindow(), QMenu::tr("Extract groove"),
		QMenu::tr("Groove name:"), QLineEdit::Normal, QString(), &ok).trimmed();
	return ok && !name.isEmpty() ? std::optional<QString>(name) : std::nullopt;
}

std::optional<double> askPercent(const QString& title)
{
	if (prompts().percent) { return prompts().percent(title); }
	bool ok = false;
	const int value = QInputDialog::getInt(QApplication::activeWindow(), title, QMenu::tr("Strength (%):"), 100, 0, 100, 5, &ok);
	return ok ? std::optional<double>(value) : std::nullopt;
}

void run(const char* command, QJsonObject args, const MidiClip* clip)
{
	args.insert(QStringLiteral("clip"), control::clipIdOf(clip));
	ControlRegistry::instance()->invoke(QString::fromLatin1(command), args);
}

void populate(QMenu* menu, const std::function<const MidiClip*()>& clip, const std::function<int()>& grid)
{
	menu->clear();
	const MidiClip* target = clip();
	menu->setEnabled(target != nullptr);
	if (target == nullptr) { return; }
	QAction* extract = menu->addAction(QMenu::tr("Extract groove from this clip..."));
	extract->setData(QStringLiteral("groove.extract"));
	QObject::connect(extract, &QAction::triggered, menu, [clip, grid] {
		if (const auto name = askName()) { run("groove.extract", {{QStringLiteral("name"), *name}, {QStringLiteral("grid"), grid()}}, clip()); }
	});
	QMenu* apply = menu->addMenu(QMenu::tr("Apply groove"));
	const QJsonArray pool = ControlRegistry::instance()->invoke(QStringLiteral("groove.list"), QJsonObject{})
		.result.value(QStringLiteral("templates")).toArray();
	apply->setEnabled(!pool.isEmpty());
	for (const QJsonValue& value : pool)
	{
		const QString name = value.toObject().value(QStringLiteral("name")).toString();
		QAction* one = apply->addAction(name);
		one->setData(QStringLiteral("groove.apply"));
		QObject::connect(one, &QAction::triggered, menu, [clip, name] {
			if (const auto percent = askPercent(QMenu::tr("Apply %1").arg(name)))
			{
				run("groove.apply", {{QStringLiteral("name"), name}, {QStringLiteral("strength"), *percent / 100.0}}, clip());
			}
		});
	}
	QAction* quantize = menu->addAction(QMenu::tr("Quantize with strength..."));
	quantize->setData(QStringLiteral("groove.quantize"));
	QObject::connect(quantize, &QAction::triggered, menu, [clip, grid] {
		if (const auto percent = askPercent(QMenu::tr("Quantize")))
		{
			run("groove.quantize", {{QStringLiteral("grid"), grid()}, {QStringLiteral("strength"), *percent / 100.0}}, clip());
		}
	});
}

} // namespace

void setGroovePrompts(GroovePrompts replacement)
{
	prompts() = std::move(replacement);
}

QMenu* makeGrooveMenu(std::function<const MidiClip*()> clip, std::function<int()> grid, QWidget* parent)
{
	auto* menu = new QMenu(QMenu::tr("Groove"), parent);
	QObject::connect(menu, &QMenu::aboutToShow, menu, [menu, clip, grid] { populate(menu, clip, grid); });
	populate(menu, clip, grid);
	return menu;
}

} // namespace lmms::gui
