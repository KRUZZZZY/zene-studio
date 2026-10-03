/*
 * MacroKnobs.cpp - a rack's macro knobs, for the Device panel and the mixer strip
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
#include "MacroKnobs.h"

#include <QHBoxLayout>

#include "AutomatableModel.h"
#include "ControlRackSupport.h"
#include "ControlRegistry.h"
#include "Knob.h"
#include "Rack.h"
#include "RackMacros.h"

namespace lmms::gui
{

namespace
{

const RackMacros* macrosOf(const QString& channel)
{
	ControlResult ignored;
	const Rack* rack = resolveRack(channel, &ignored);
	return rack != nullptr ? &rack->macros() : nullptr;
}

//! What the knobs were built from: each macro's name, so an added, removed or renamed one rebuilds them.
QString signatureOf(const RackMacros* macros, int maxKnobs)
{
	QString out;
	for (int i = 0; macros != nullptr && i < std::min(macros->macroCount(), maxKnobs); ++i) { out += macros->macro(i)->name + QLatin1Char('\n'); }
	return out;
}

} // namespace

MacroKnobs::MacroKnobs(const QString& channel, int maxKnobs, bool small, QWidget* parent) :
	QWidget(parent),
	m_channel(channel),
	m_maxKnobs(maxKnobs),
	m_small(small),
	m_row(new QHBoxLayout(this))
{
	m_row->setContentsMargins(0, 0, 0, 0);
	m_row->setSpacing(small ? 1 : 6);
	setObjectName(QStringLiteral("macroKnobs"));
	rebuild();
}

MacroKnobs::~MacroKnobs()
{
	// The knobs go before the models they show.
	for (Knob* knob : m_knobs) { delete knob; }
}

void MacroKnobs::rebuild()
{
	for (Knob* knob : m_knobs) { delete knob; }
	m_knobs.clear();
	m_models.clear();
	const RackMacros* macros = macrosOf(m_channel);
	m_signature = signatureOf(macros, m_maxKnobs);
	for (int i = 0; macros != nullptr && i < std::min(macros->macroCount(), m_maxKnobs); ++i)
	{
		const QString name = macros->macro(i)->name;
		auto model = std::make_unique<FloatModel>(macros->macro(i)->value, 0.f, 1.f, 0.001f, nullptr, name);
		model->setJournalling(false);  // the undo step is rack.macro_set's, not this view's
		auto* knob = new Knob(m_small ? KnobType::Small17 : KnobType::Bright26, m_small ? QString() : name, this);
		knob->setModel(model.get());
		knob->setHintText(name + QLatin1Char(':'), QString());
		knob->setAccessibleName(name);
		knob->setProperty("controlCommand", QStringLiteral("rack.macro_set"));
		const QString macroId = QStringLiteral("macro-%1").arg(i);
		connect(model.get(), &FloatModel::dataChanged, this, [this, macroId, raw = model.get()] {
			if (m_syncing) { return; }
			ControlRegistry::instance()->invoke(QStringLiteral("rack.macro_set"), {{QStringLiteral("channel"), m_channel},
				{QStringLiteral("macro"), macroId}, {QStringLiteral("value"), static_cast<double>(raw->value())}});
		});
		m_row->addWidget(knob);
		m_knobs.push_back(knob);
		m_models.push_back(std::move(model));
	}
	setVisible(!m_knobs.empty());
}

void MacroKnobs::refresh()
{
	const RackMacros* macros = macrosOf(m_channel);
	if (signatureOf(macros, m_maxKnobs) != m_signature)
	{
		rebuild();
		return;
	}
	m_syncing = true;
	for (std::size_t i = 0; i < m_models.size(); ++i)
	{
		const float value = macros->macro(static_cast<int>(i))->value;
		if (m_models[i]->value() != value) { m_models[i]->setValue(value); }
	}
	m_syncing = false;
}

} // namespace lmms::gui
