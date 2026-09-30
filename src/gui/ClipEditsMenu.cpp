/*
 * ClipEditsMenu.cpp - a sample clip's Gain and fades: clip gain, fade lengths and the fade shape
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

#include "ClipEditsMenu.h"

#include <cmath>

#include <QActionGroup>
#include <QApplication>
#include <QInputDialog>
#include <QJsonObject>
#include <QMenu>
#include <QMessageBox>

#include "Clip.h"
#include "ClipEdits.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "TimePos.h"
#include "UnattendedRun.h"

namespace lmms::gui
{

namespace
{

std::function<std::optional<double>(double)> s_gainPrompt;

std::optional<double> promptGain(double currentDb)
{
	if (s_gainPrompt) { return s_gainPrompt(currentDb); }
	bool ok = false;
	const double db = QInputDialog::getDouble(QApplication::activeWindow(), QMenu::tr("Clip Gain"),
		QMenu::tr("Gain (dB):"), currentDb, -96.0, 24.0, 1, &ok);
	return ok ? std::optional<double>(db) : std::nullopt;
}

double gainDb(const Clip* clip)
{
	const float gain = clip->clipEdits().gain;
	return gain > 0.0f ? 20.0 * std::log10(static_cast<double>(gain)) : -96.0;
}

//! Runs @a command on @a clip; a refusal is shown (or logged when nobody is there).
void run(const Clip* clip, const QString& command, QJsonObject args)
{
	args.insert(QStringLiteral("clip"), control::clipIdOf(clip));
	const ControlResult result = ControlRegistry::instance()->invoke(command, args);
	if (result.ok) { return; }
	if (isUnattendedRun()) { qWarning("%s refused: %s", qPrintable(command), qPrintable(result.errorMessage)); }
	else { QMessageBox::warning(QApplication::activeWindow(), QMenu::tr("Gain and fades"), result.errorMessage); }
}

void addFadeMenu(QMenu* parent, Clip* clip, const QString& title, const QString& key, int current)
{
	QMenu* menu = parent->addMenu(title);
	auto* group = new QActionGroup(menu);
	const int beat = DefaultTicksPerBar / 4;
	const std::pair<QString, int> lengths[] = {
		{QMenu::tr("None"), 0}, {QMenu::tr("1 beat"), beat}, {QMenu::tr("2 beats"), 2 * beat},
		{QMenu::tr("1 bar"), DefaultTicksPerBar}};
	for (const auto& [text, ticks] : lengths)
	{
		QAction* action = menu->addAction(text);
		action->setCheckable(true);
		action->setChecked(ticks == current);
		action->setData(QStringLiteral("clip.set_fade"));
		group->addAction(action);
		QObject::connect(action, &QAction::triggered, menu, [clip, key, ticks = ticks] {
			run(clip, QStringLiteral("clip.set_fade"), {{key, ticks}});
		});
	}
}

} // namespace

void setClipGainPrompt(std::function<std::optional<double>(double)> prompt) { s_gainPrompt = std::move(prompt); }

void addClipEditsMenu(QMenu* menu, Clip* clip)
{
	if (clip == nullptr) { return; }
	const ClipEdits& edits = clip->clipEdits();
	QMenu* sub = menu->addMenu(QMenu::tr("Gain and fades"));

	const double db = gainDb(clip);
	QAction* gain = sub->addAction(QMenu::tr("Clip gain (%1 dB)...").arg(db, 0, 'f', 1));
	gain->setData(QStringLiteral("clip.set_gain"));
	QObject::connect(gain, &QAction::triggered, sub, [clip] {
		if (const auto wanted = promptGain(gainDb(clip)))
		{
			run(clip, QStringLiteral("clip.set_gain"), {{QStringLiteral("gain_db"), *wanted}});
		}
	});
	QAction* reset = sub->addAction(QMenu::tr("Reset gain"));
	reset->setData(QStringLiteral("clip.set_gain"));
	reset->setEnabled(edits.gain != 1.0f);
	QObject::connect(reset, &QAction::triggered, sub, [clip] {
		run(clip, QStringLiteral("clip.set_gain"), {{QStringLiteral("gain_db"), 0.0}});
	});
	sub->addSeparator();

	addFadeMenu(sub, clip, QMenu::tr("Fade in"), QStringLiteral("fade_in"), edits.fadeInTicks);
	addFadeMenu(sub, clip, QMenu::tr("Fade out"), QStringLiteral("fade_out"), edits.fadeOutTicks);

	QMenu* shape = sub->addMenu(QMenu::tr("Fade shape"));
	auto* group = new QActionGroup(shape);
	const std::pair<QString, FadeShape> shapes[] = {{QMenu::tr("Linear"), FadeShape::Linear},
		{QMenu::tr("Exponential"), FadeShape::Exponential}, {QMenu::tr("Equal power"), FadeShape::EqualPower}};
	for (const auto& [text, value] : shapes)
	{
		QAction* action = shape->addAction(text);
		action->setCheckable(true);
		action->setChecked(edits.fadeInShape == value && edits.fadeOutShape == value);
		action->setData(QStringLiteral("clip.set_fade"));
		group->addAction(action);
		const QString name = fadeShapeName(value);
		QObject::connect(action, &QAction::triggered, shape, [clip, name] {
			run(clip, QStringLiteral("clip.set_fade"),
				{{QStringLiteral("fade_in_shape"), name}, {QStringLiteral("fade_out_shape"), name}});
		});
	}
}

} // namespace lmms::gui
