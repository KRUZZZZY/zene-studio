/*
 * SampleEditorDialog.cpp - a sample clip's source edited over a selection
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

#include "SampleEditorDialog.h"

#include <algorithm>
#include <functional>

#include <QApplication>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include "AccessibleWidgets.h"
#include "CommandForm.h"
#include "ControlVocabulary.h"
#include "SampleClip.h"

namespace lmms::gui
{

namespace
{

//! A row of controls with no margins of its own.
QHBoxLayout* rowLayout(QWidget* row)
{
	auto* layout = new QHBoxLayout(row);
	layout->setContentsMargins(0, 0, 0, 0);
	return layout;
}

} // namespace

SampleEditorDialog::SampleEditorDialog(SampleClip* clip, QWidget* parent) :
	QDialog(parent),
	m_clip(clip),
	m_view(new SampleSelectionView(this)),
	m_unit(samplerange::unitFor(musicaltime::currentFormat()))
{
	setWindowTitle(QObject::tr("Sample Editor - %1").arg(clip->name().isEmpty() ? QObject::tr("audio") : clip->name()));
	setAttribute(Qt::WA_DeleteOnClose);
	setProperty("controlCommand", QStringLiteral("sample.edit_range"));
	auto* layout = new QVBoxLayout(this);
	layout->addWidget(m_view, 1);
	layout->addWidget(buildFields());
	layout->addWidget(buildZoom());
	layout->addWidget(buildActions());
	m_status = new QLabel(this);
	m_status->setAccessibleName(tr("Sample editor status"));
	m_status->setWordWrap(true);
	layout->addWidget(m_status);
	connect(m_view, &SampleSelectionView::selectionChanged, this, &SampleEditorDialog::syncFields);
	// The fields read in the musical-time format (theme 7) until the Units menu picks another unit.
	connect(musicaltime::formatSignal(), &musicaltime::FormatSignal::changed, this,
		[this] { setUnit(samplerange::unitFor(musicaltime::currentFormat())); });
	m_view->setClip(clip);
}

SampleClip* SampleEditorDialog::clip() const
{
	return m_clip.data();
}

SampleEditorDialog* SampleEditorDialog::openFor(SampleClip* clip, QWidget* parent)
{
	for (QWidget* window : QApplication::topLevelWidgets())
	{
		auto* open = qobject_cast<SampleEditorDialog*>(window);
		if (open != nullptr && open->clip() == clip)
		{
			open->show();
			open->raise();
			return open;
		}
	}
	auto* dialog = new SampleEditorDialog(clip, parent);
	dialog->show();
	return dialog;
}

QWidget* SampleEditorDialog::buildFields()
{
	auto* row = new QWidget(this);
	QHBoxLayout* layout = rowLayout(row);
	const struct { SampleFrameField** field; bool length; const char* label; const char* name; const char* tip; } fields[] = {
		{&m_start, false, "&Start", "Range start", "Where the range starts in the clip's source; type a value or step it with Up and Down"},
		{&m_end, false, "&End", "Range end", "Where the range ends: the first frame after it"},
		{&m_length, true, "&Length", "Range length", "How long the range is; a new length moves its end"}};
	for (const auto& entry : fields)
	{
		auto* field = new SampleFrameField(entry.length, row);
		auto* label = new QLabel(tr(entry.label), row);
		label->setBuddy(field);
		field->setAccessibleName(tr(entry.name));
		field->setToolTip(tr(entry.tip));
		field->setProperty("controlCommand", QStringLiteral("window.sample_editor"));
		layout->addWidget(label);
		layout->addWidget(field, 1);
		*entry.field = field;
	}
	// Each field moves its own edge; a start past the end (or an end before the start) takes the other with it.
	connect(m_start, qOverload<int>(&QSpinBox::valueChanged), this, [this](int v) { m_view->setRange(v, std::max(v, m_view->to())); });
	connect(m_end, qOverload<int>(&QSpinBox::valueChanged), this, [this](int v) { m_view->setRange(std::min(m_view->from(), v), v); });
	connect(m_length, qOverload<int>(&QSpinBox::valueChanged), this,
		[this](int v) { m_view->setRange(m_view->from(), std::min(m_view->frames(), m_view->from() + v)); });
	m_unitBox = new QComboBox(row);
	for (int i = 0; i < samplerange::unitNames().size(); ++i)
	{
		m_unitBox->addItem(samplerange::unitLabel(static_cast<samplerange::Unit>(i)), samplerange::unitNames().at(i));
	}
	m_unitBox->setCurrentIndex(static_cast<int>(m_unit));
	m_unitBox->setAccessibleName(tr("Range units"));
	m_unitBox->setToolTip(tr("The unit the range fields read in: the time from the source's start, bars and beats at the "
		"song's tempo from where the clip starts, or sample frames. A typed suffix (s, smp, bars) overrides it."));
	auto* unitLabel = new QLabel(tr("&Units"), row);
	unitLabel->setBuddy(m_unitBox);
	connect(m_unitBox, qOverload<int>(&QComboBox::currentIndexChanged), this,
		[this](int index) { setUnit(static_cast<samplerange::Unit>(index)); });
	layout->addWidget(unitLabel);
	layout->addWidget(m_unitBox);
	return row;
}

QWidget* SampleEditorDialog::buildZoom()
{
	auto* row = new QWidget(this);
	QHBoxLayout* layout = rowLayout(row);
	const struct { const char* label; const char* tip; std::function<void()> act; } zooms[] = {
		{"Zoom &In", "Show half as much of the waveform (Ctrl+wheel zooms around the pointer)",
			[this] { m_view->zoomBy(2.0, m_view->width() / 2); }},
		{"Zoom &Out", "Show twice as much of the waveform", [this] { m_view->zoomBy(0.5, m_view->width() / 2); }},
		{"Zoom to Selectio&n", "Fit the selected range to the waveform", [this] { m_view->zoomToSelection(); }},
		{"Show &All", "Show the whole source", [this] { m_view->showAll(); }}};
	for (const auto& zoom : zooms)
	{
		auto* button = new QPushButton(tr(zoom.label), row);
		button->setToolTip(tr(zoom.tip));
		button->setProperty("controlCommand", QStringLiteral("window.sample_editor"));
		connect(button, &QPushButton::clicked, this, zoom.act);
		layout->addWidget(button);
	}
	layout->addStretch(1);
	return row;
}

QWidget* SampleEditorDialog::buildActions()
{
	auto* row = new QWidget(this);
	QHBoxLayout* layout = rowLayout(row);
	// Each button says in its tooltip that it edits only the selected range, and what it does there.
	const struct { const char* label; const char* op; double gain; const char* tip; } actions[] = {
		{"-3 dB", "gain", -3.0, "Lower the selected range by 3 dB"}, {"+3 dB", "gain", 3.0, "Raise the selected range by 3 dB"},
		{"Silence", "silence", 0.0, "Silence the selected range"},
		{"Delete", "delete", 0.0, "Delete the selected range from the audio; what follows it closes up"},
		{"Reverse", "reverse", 0.0, "Reverse the selected range"}, {"Fade In", "fade_in", 0.0, "Fade the selected range in (linear)"},
		{"Fade Out", "fade_out", 0.0, "Fade the selected range out (linear)"},
		{"Crop", "crop", 0.0, "Keep only the selected range of the audio"}};
	for (const auto& action : actions)
	{
		auto* button = new QPushButton(QObject::tr(action.label), row);
		button->setToolTip(QObject::tr(action.tip));
		button->setProperty("controlCommand", QStringLiteral("sample.edit_range"));
		const QString op = QLatin1String(action.op);
		const double gain = action.gain;
		const QString label = button->text();
		connect(button, &QPushButton::clicked, this, [this, op, gain, label] { apply(op, gain, label); });
		layout->addWidget(button);
		m_actions.push_back(button);
	}
	return row;
}

samplerange::Context SampleEditorDialog::context() const
{
	samplerange::Context context;
	if (m_clip.isNull()) { return context; }
	context.sampleRate = std::max(1, m_clip->sample().sampleRate());
	context.origin = m_clip->startPosition().getTicks();
	return context;
}

void SampleEditorDialog::setUnit(samplerange::Unit unit)
{
	m_unit = unit;
	const QSignalBlocker blocker(m_unitBox);
	m_unitBox->setCurrentIndex(static_cast<int>(unit));
	syncFields();
}

void SampleEditorDialog::syncFields()
{
	const int from = m_view->from();
	const int to = m_view->to();
	const samplerange::Context ctx = context();
	m_length->setLengthStart(from);
	const std::pair<SampleFrameField*, int> values[] = {{m_start, from}, {m_end, to}, {m_length, to - from}};
	for (const auto& [field, value] : values)
	{
		// The fields show the selection; only the user's own edits move it.
		const QSignalBlocker blocker(field);
		field->setRange(0, m_view->frames());
		field->setValue(value);
		field->setFormat(ctx, m_unit);
	}
	m_view->setAccessibleDescription(rangeText());
	// A button with no selection to edit would do nothing: it is enabled only while there is one.
	for (QPushButton* button : m_actions) { button->setEnabled(to > from); }
}

QString SampleEditorDialog::rangeText() const
{
	const int from = m_view->from();
	const int to = m_view->to();
	if (to <= from) { return tr("Nothing selected"); }
	const samplerange::Context ctx = context();
	return tr("%1 to %2, %3 long").arg(samplerange::frameText(from, ctx, m_unit), samplerange::frameText(to, ctx, m_unit),
		samplerange::frameText(to - from, ctx, m_unit, true, from));
}

QJsonObject SampleEditorDialog::describe() const
{
	const int from = m_view->from();
	const int to = m_view->to();
	return {{QStringLiteral("clip"), m_clip.isNull() ? QString() : control::clipIdOf(m_clip.data())},
		{QStringLiteral("frames"), m_view->frames()}, {QStringLiteral("sample_rate"), context().sampleRate},
		{QStringLiteral("from"), from}, {QStringLiteral("to"), to}, {QStringLiteral("length"), to - from},
		{QStringLiteral("view_from"), m_view->viewFrom()}, {QStringLiteral("view_to"), m_view->viewFrom() + m_view->viewFrames()},
		{QStringLiteral("unit"), samplerange::unitNames().at(static_cast<int>(m_unit))},
		{QStringLiteral("start_text"), m_start->text()}, {QStringLiteral("end_text"), m_end->text()},
		{QStringLiteral("length_text"), m_length->text()}, {QStringLiteral("status"), status()}};
}

QString SampleEditorDialog::status() const
{
	return m_status->text();
}

void SampleEditorDialog::report(const QString& message, bool failed)
{
	m_status->setText(message);
	a11y::announceStatus(m_status, message, failed ? a11y::Politeness::Assertive : a11y::Politeness::Polite);
}

void SampleEditorDialog::apply(const QString& op, double gainDb, const QString& label)
{
	const int from = m_view->from();
	const int to = m_view->to();
	if (m_clip.isNull() || to <= from) { return; }
	const QString range = rangeText();
	const ControlResult result = runCommand(this, CommandCall{QStringLiteral("sample.edit_range"),
		{{QStringLiteral("clip"), control::clipIdOf(m_clip)}, {QStringLiteral("from"), from}, {QStringLiteral("to"), to},
		{QStringLiteral("op"), op}, {QStringLiteral("gain_db"), gainDb}}, {}, {}, false});
	if (!result.ok)
	{
		report(tr("%1 was not applied: %2").arg(label, result.errorMessage), true);  // the selection stays as it was
		return;
	}
	m_view->setClip(m_clip);  // the source changed: draw it again (the same length keeps the selection)
	// A crop leaves the range as the whole source; a delete closes it up, leaving the place it was.
	if (op == QLatin1String("crop")) { m_view->setRange(0, m_view->frames()); }
	else if (op == QLatin1String("delete")) { m_view->setRange(from, from); }
	report(tr("%1 applied to %2. Undo (Ctrl+Z) reverts it.").arg(label, range), false);
}

} // namespace lmms::gui
