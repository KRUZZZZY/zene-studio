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

#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

#include "CommandForm.h"
#include "ControlVocabulary.h"
#include "DesignTokens.h"
#include "SampleClip.h"

namespace lmms::gui
{

SampleSelectionView::SampleSelectionView(QWidget* parent) :
	QWidget(parent)
{
	setMinimumHeight(120);
	setAccessibleName(QObject::tr("Waveform - drag to select"));
	setToolTip(QObject::tr("Drag across the waveform to select a range: each button below edits the clip's audio over it, "
		"one undo step per edit."));
}

void SampleSelectionView::setClip(SampleClip* clip)
{
	const int frames = clip != nullptr ? static_cast<int>(clip->sample().sampleSize()) : 0;
	// The same clip at the same length (after a gain, silence, reverse or fade) keeps its selection, so
	// the next action applies to it too; a new clip or length starts afresh (review L17 #5).
	if (clip != m_clip || frames != m_frames) { m_anchor = m_end = 0; }
	m_clip = clip;
	m_frames = frames;
	m_thumbnail = clip != nullptr ? SampleThumbnail(clip->sample()) : SampleThumbnail();
	update();
	emit selectionChanged();
}

int SampleSelectionView::frameAt(int x) const
{
	return std::clamp(static_cast<int>(static_cast<double>(x) / std::max(1, width()) * m_frames), 0, m_frames);
}

void SampleSelectionView::paintEvent(QPaintEvent*)
{
	const tokens::Palette& pal = tokens::palette();
	QPainter p(this);
	p.fillRect(rect(), pal.control);
	if (m_frames <= 0) { return; }
	const int left = static_cast<int>(static_cast<double>(from()) / m_frames * width());
	const int right = static_cast<int>(static_cast<double>(to()) / m_frames * width());
	QColor selection = pal.accent;
	selection.setAlpha(70);
	p.fillRect(QRect(left, 0, std::max(1, right - left), height()), selection);
	p.setPen(pal.text);
	m_thumbnail.visualize({.sampleRect = rect(), .viewportRect = rect()}, p);
}

void SampleSelectionView::mousePressEvent(QMouseEvent* event)
{
	if (event->button() != Qt::LeftButton) { return; }  // as the drag: only the left button selects
	m_anchor = m_end = frameAt(event->pos().x());
	update();
	emit selectionChanged();
}

void SampleSelectionView::mouseMoveEvent(QMouseEvent* event)
{
	if (!(event->buttons() & Qt::LeftButton)) { return; }
	m_end = frameAt(event->pos().x());
	update();
	emit selectionChanged();
}

SampleEditorDialog::SampleEditorDialog(SampleClip* clip, QWidget* parent) :
	QDialog(parent),
	m_clip(clip),
	m_view(new SampleSelectionView(this))
{
	setWindowTitle(QObject::tr("Sample Editor - %1").arg(clip->name().isEmpty() ? QObject::tr("audio") : clip->name()));
	setAttribute(Qt::WA_DeleteOnClose);
	setProperty("controlCommand", QStringLiteral("sample.edit_range"));
	auto* buttons = new QHBoxLayout;
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
		auto* button = new QPushButton(QObject::tr(action.label), this);
		button->setToolTip(QObject::tr(action.tip));
		button->setProperty("controlCommand", QStringLiteral("sample.edit_range"));
		const QString op = QLatin1String(action.op);
		const double gain = action.gain;
		connect(button, &QPushButton::clicked, this, [this, op, gain] { apply(op, gain); });
		buttons->addWidget(button);
		m_actions.push_back(button);
	}
	// A button with no selection to edit would do nothing: it is enabled only while there is one.
	connect(m_view, &SampleSelectionView::selectionChanged, this, [this] {
		for (QPushButton* button : m_actions) { button->setEnabled(m_view->to() > m_view->from()); }
	});
	m_view->setClip(clip);
	auto* layout = new QVBoxLayout(this);
	layout->addWidget(m_view, 1);
	layout->addLayout(buttons);
}

void SampleEditorDialog::apply(const QString& op, double gainDb)
{
	if (m_clip.isNull() || m_view->to() <= m_view->from()) { return; }
	runCommand(this, CommandCall{QStringLiteral("sample.edit_range"), {{QStringLiteral("clip"), control::clipIdOf(m_clip)},
		{QStringLiteral("from"), m_view->from()}, {QStringLiteral("to"), m_view->to()}, {QStringLiteral("op"), op},
		{QStringLiteral("gain_db"), gainDb}}, {}, {}, false});
	m_view->setClip(m_clip);  // the source changed: draw it again
}

} // namespace lmms::gui
