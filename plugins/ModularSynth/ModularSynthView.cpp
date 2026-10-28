/*
 * ModularSynthView.cpp - Q050-059: the modular instrument's window
 *
 * This file is part of Zene Studio, derived from LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include "ModularSynthView.h"

#include <QGridLayout>
#include <QLabel>
#include <QPainter>
#include <QScrollArea>
#include <QVBoxLayout>

#include "FontHelper.h"
#include "Knob.h"
#include "ModularSynth.h"

namespace lmms::gui
{

namespace
{

//! What a whole-number parameter's values mean, for its knob's hint.
QString choicesOf(modular::ModuleKind kind, const QString& key)
{
	if (key == QLatin1String("wave"))
	{
		return kind == modular::ModuleKind::Oscillator
			? ModularSynthView::tr("0 sine, 1 saw, 2 square, 3 triangle")
			: ModularSynthView::tr("0 sine, 1 triangle, 2 saw, 3 square");
	}
	if (key == QLatin1String("mode")) { return ModularSynthView::tr("0 low-pass, 1 high-pass, 2 band-pass"); }
	return {};
}

} // namespace


ModularSynthView::ModularSynthView(Instrument* instrument, QWidget* parent)
	: InstrumentViewFixedSize(instrument, parent)
	, m_scroll(new QScrollArea(this))
{
	setAutoFillBackground(true);
	QPalette palette;
	palette.setColor(backgroundRole(), QColor(34, 38, 44));
	setPalette(palette);

	m_scroll->setGeometry(0, 40, 250, 210);
	m_scroll->setFrameShape(QFrame::NoFrame);
	m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_scroll->setWidgetResizable(true);
}

void ModularSynthView::modelChanged()
{
	connect(castModel<ModularSynth>(), &ModularSynth::graphChanged, this, &ModularSynthView::rebuild,
		Qt::UniqueConnection);
	rebuild();
}

void ModularSynthView::rebuild()
{
	auto* synth = castModel<ModularSynth>();
	const modular::Graph& graph = synth->graph();

	auto* content = new QWidget;
	content->setObjectName(QStringLiteral("modularModules"));
	auto* rows = new QVBoxLayout(content);
	rows->setContentsMargins(6, 0, 6, 6);
	rows->setSpacing(2);
	for (int i = 0; i < static_cast<int>(graph.modules.size()); ++i)
	{
		const modular::Module& module = graph.modules[static_cast<std::size_t>(i)];
		const modular::KindSpec& spec = modular::kindSpec(module.kind);
		auto* title = new QLabel(QStringLiteral("%1  <span style='color:#8a96a3'>%2</span>")
			.arg(graph.moduleLabel(i).toUpper(), module.id), content);
		title->setFont(adjustedToPixelSize(font(), SMALL_FONT_SIZE));
		rows->addWidget(title);

		auto* knobs = new QGridLayout;
		knobs->setHorizontalSpacing(4);
		const std::vector<FloatModel*> params = synth->moduleParameters(i);
		for (std::size_t k = 0; k < params.size(); ++k)
		{
			const modular::ParamSpec& p = spec.params[k];
			auto* knob = new Knob(KnobType::Bright26, tr(p.label), content);
			knob->setObjectName(QStringLiteral("%1.%2").arg(module.id, QString::fromLatin1(p.key)));
			knob->setModel(params[k]);
			const QString choices = choicesOf(module.kind, QString::fromLatin1(p.key));
			knob->setHintText(params[k]->displayName() + (choices.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(choices))
				+ QStringLiteral(":"), QString::fromLatin1(p.unit));
			knobs->addWidget(knob, 0, static_cast<int>(k), Qt::AlignLeft);
		}
		knobs->setColumnStretch(modular::MaxParams, 1);
		rows->addLayout(knobs);
	}

	auto* wiring = new QLabel(tr("Wiring:\n%1").arg(graph.describeWiring()), content);
	wiring->setObjectName(QStringLiteral("modularWiring"));
	wiring->setFont(adjustedToPixelSize(font(), SMALL_FONT_SIZE));
	wiring->setWordWrap(true);
	rows->addWidget(wiring);
	rows->addStretch(1);

	m_scroll->setWidget(content); // deletes the previous rows and their knobs
	update();
}

void ModularSynthView::paintEvent(QPaintEvent*)
{
	QPainter painter(this);
	painter.setPen(QColor(235, 235, 235));
	QFont title = painter.font();
	title.setBold(true);
	title.setPixelSize(20);
	painter.setFont(title);
	painter.drawText(QRect(8, 4, 234, 24), Qt::AlignLeft | Qt::AlignVCenter, tr("MODULAR SYNTH"));
	painter.setFont(adjustedToPixelSize(font(), SMALL_FONT_SIZE));
	painter.setPen(QColor(150, 150, 150));
	const auto* synth = castModel<ModularSynth>();
	painter.drawText(QRect(8, 26, 234, 12), Qt::AlignLeft | Qt::AlignVCenter,
		tr("%1 modules, %2 wires").arg(synth->graph().modules.size()).arg(synth->graph().wires.size()));
}

} // namespace lmms::gui
