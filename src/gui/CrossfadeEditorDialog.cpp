/*
 * CrossfadeEditorDialog.cpp - the crossfade editor: both sides of two overlapping clips' crossfade
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

#include "CrossfadeEditorDialog.h"

#include <algorithm>
#include <cstdint>

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "AccessibleWidgets.h"
#include "CommandForm.h"
#include "ControlVocabulary.h"
#include "SampleClip.h"
#include "Song.h"
#include "Track.h"

namespace lmms::gui
{

namespace
{

//! The five shapes, in the order the combo boxes list them.
constexpr FadeShape Shapes[] = {FadeShape::EqualPower, FadeShape::Linear, FadeShape::Exponential, FadeShape::Logarithmic,
	FadeShape::SCurve};

QString shapeLabel(FadeShape shape)
{
	switch (shape)
	{
	case FadeShape::Linear: return QObject::tr("Linear");
	case FadeShape::Exponential: return QObject::tr("Exponential");
	case FadeShape::Logarithmic: return QObject::tr("Logarithmic");
	case FadeShape::SCurve: return QObject::tr("S-Curve");
	default: return QObject::tr("Equal Power");
	}
}

FadeShape shapeOf(const QComboBox* box)
{
	FadeShape shape = FadeShape::EqualPower;
	fadeShapeFromName(box->currentData().toString(), &shape);
	return shape;
}

void selectShape(QComboBox* box, FadeShape shape)
{
	const int index = box->findData(fadeShapeName(shape));
	box->setCurrentIndex(std::max(0, index));
}

//! A side as a clip holds it: its ramp and shape, or the overlap at equal power when it has no ramp yet.
CrossfadeSide sideOf(int ticks, FadeShape shape, int overlap)
{
	return ticks > 0 ? CrossfadeSide{ticks, shape} : CrossfadeSide{std::max(1, overlap), FadeShape::EqualPower};
}

} // namespace

CrossfadeCurveView::CrossfadeCurveView(QWidget* parent) :
	QWidget(parent)
{
	setAccessibleName(tr("Crossfade curves"));
	setMinimumSize(240, 100);
}

void CrossfadeCurveView::setRamps(int outEnd, const CrossfadeSide& out, int inStart, const CrossfadeSide& in)
{
	m_outEnd = outEnd;
	m_inStart = inStart;
	m_out = out;
	m_in = in;
	setAccessibleDescription(tr("Fade out over %1 ticks (%2) into a fade in over %3 ticks (%4)")
		.arg(out.ticks).arg(shapeLabel(out.shape)).arg(in.ticks).arg(shapeLabel(in.shape)));
	update();
}

void CrossfadeCurveView::paintEvent(QPaintEvent*)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	const QRectF area = QRectF(rect()).adjusted(6, 6, -6, -6);
	p.fillRect(rect(), palette().base());
	// The span drawn: both ramps, and the overlap between the incoming clip's start and the outgoing clip's end.
	const std::int64_t from = std::min(static_cast<std::int64_t>(m_outEnd) - m_out.ticks,
		static_cast<std::int64_t>(m_inStart));
	const std::int64_t to = std::max(static_cast<std::int64_t>(m_inStart) + m_in.ticks,
		static_cast<std::int64_t>(m_outEnd));
	if (to <= from || area.width() <= 0) { return; }
	const double firstTick = static_cast<double>(from);
	const double tickSpan = static_cast<double>(to - from);
	const auto xAt = [&](double tick) { return area.left() + (tick - firstTick) / tickSpan * area.width(); };
	const auto yAt = [&](double gain) { return area.bottom() - gain * area.height(); };
	p.fillRect(QRectF(QPointF(xAt(m_inStart), area.top()), QPointF(xAt(m_outEnd), area.bottom())), palette().alternateBase());
	const auto ramp = [&](const CrossfadeSide& side, std::int64_t begin, bool rising, const QColor& colour) {
		QPainterPath path;
		constexpr int Steps = 64;
		for (int i = 0; i <= Steps; ++i)
		{
			const double t = static_cast<double>(i) / Steps;
			const double gain = fadeShapeGain(side.shape, rising ? t : 1.0 - t);
			const QPointF point(xAt(begin + t * side.ticks), yAt(gain));
			if (i == 0) { path.moveTo(point); } else { path.lineTo(point); }
		}
		p.setPen(QPen(colour, 2));
		p.drawPath(path);
	};
	ramp(m_out, static_cast<std::int64_t>(m_outEnd) - m_out.ticks, false, palette().text().color());
	ramp(m_in, m_inStart, true, palette().highlight().color());
}

CrossfadeEditorDialog::CrossfadeEditorDialog(SampleClip* out, SampleClip* in, QWidget* parent) :
	QDialog(parent),
	m_out(out),
	m_in(in),
	m_view(new CrossfadeCurveView(this))
{
	setWindowTitle(tr("Crossfade Editor"));
	setAttribute(Qt::WA_DeleteOnClose);
	setProperty("controlCommand", QStringLiteral("clip.crossfade"));
	auto* layout = new QVBoxLayout(this);
	m_summary = new QLabel(this);
	m_summary->setWordWrap(true);
	layout->addWidget(m_summary);
	layout->addWidget(m_view, 1);
	auto* sides = new QHBoxLayout();
	sides->addWidget(buildSide(tr("Outgoing clip - fade out"), &m_outLength, &m_outShape));
	sides->addWidget(buildSide(tr("Incoming clip - fade in"), &m_inLength, &m_inShape));
	layout->addLayout(sides);
	auto* buttons = new QDialogButtonBox(this);
	QPushButton* applyButton = buttons->addButton(tr("Apply"), QDialogButtonBox::ApplyRole);
	applyButton->setProperty("controlCommand", QStringLiteral("clip.crossfade"));
	applyButton->setToolTip(tr("Set both sides of the crossfade (one undo step)"));
	QPushButton* close = buttons->addButton(QDialogButtonBox::Close);
	connect(applyButton, &QPushButton::clicked, this, [this] { apply(); });
	connect(close, &QPushButton::clicked, this, &QDialog::close);
	layout->addWidget(buttons);
	m_status = new QLabel(this);
	m_status->setAccessibleName(tr("Crossfade editor status"));
	m_status->setWordWrap(true);
	layout->addWidget(m_status);
	const auto watchClip = [this](SampleClip* clip) {
		connect(clip, &Clip::positionChanged, this, [this] { reload(); });
		connect(clip, &Clip::lengthChanged, this, [this] { reload(); });
		connect(clip, &Clip::clipEditsChanged, this, [this] { reload(); });
	};
	watchClip(out);
	watchClip(in);
	reload();
}

QWidget* CrossfadeEditorDialog::buildSide(const QString& title, QSpinBox** length, QComboBox** shape)
{
	auto* box = new QGroupBox(title, this);
	auto* form = new QFormLayout(box);
	*length = new QSpinBox(box);
	(*length)->setRange(1, MaxSongLength);
	(*length)->setSuffix(tr(" ticks"));
	(*length)->setAccessibleName(title + tr(" length"));
	*shape = new QComboBox(box);
	(*shape)->setAccessibleName(title + tr(" shape"));
	for (const FadeShape each : Shapes) { (*shape)->addItem(shapeLabel(each), fadeShapeName(each)); }
	form->addRow(tr("&Length"), *length);
	form->addRow(tr("&Shape"), *shape);
	connect(*length, qOverload<int>(&QSpinBox::valueChanged), this, &CrossfadeEditorDialog::sync);
	connect(*shape, qOverload<int>(&QComboBox::currentIndexChanged), this, &CrossfadeEditorDialog::sync);
	return box;
}

CrossfadeEditorDialog* CrossfadeEditorDialog::openFor(SampleClip* first, SampleClip* second, QWidget* parent)
{
	if (second->startPosition() < first->startPosition()) { std::swap(first, second); }
	for (QWidget* window : QApplication::topLevelWidgets())
	{
		auto* open = qobject_cast<CrossfadeEditorDialog*>(window);
		if (open != nullptr && open->isVisible() && open->outClip() == first && open->inClip() == second)
		{
			open->show();
			open->raise();
			return open;
		}
	}
	auto* dialog = new CrossfadeEditorDialog(first, second, parent);
	dialog->show();
	return dialog;
}

namespace
{

bool overlaps(const Clip* a, const Clip* b) { return a->startPosition() < b->endPosition() && b->startPosition() < a->endPosition(); }

//! Whether @a candidate starts nearer @a clip than @a best, which is on the same side of it (or nullptr).
bool nearer(const Clip* clip, const Clip* candidate, const Clip* best)
{
	if (best == nullptr) { return true; }
	return candidate->startPosition() >= clip->startPosition() ? candidate->startPosition() < best->startPosition()
		: candidate->startPosition() > best->startPosition();
}

} // namespace

SampleClip* CrossfadeEditorDialog::overlapping(const Clip* clip)
{
	const Track* track = clip != nullptr ? clip->getTrack() : nullptr;
	if (track == nullptr) { return nullptr; }
	SampleClip* after = nullptr;
	SampleClip* before = nullptr;
	for (Clip* other : track->getClips())
	{
		auto* sample = dynamic_cast<SampleClip*>(other);
		if (sample == nullptr || other == clip || !overlaps(clip, other)) { continue; }
		SampleClip*& side = other->startPosition() >= clip->startPosition() ? after : before;
		if (nearer(clip, other, side)) { side = sample; }
	}
	return after != nullptr ? after : before;
}

SampleClip* CrossfadeEditorDialog::outClip() const { return m_out.data(); }

SampleClip* CrossfadeEditorDialog::inClip() const { return m_in.data(); }

int CrossfadeEditorDialog::overlap() const
{
	if (m_out.isNull() || m_in.isNull()) { return 0; }
	return std::max(0, std::min(m_out->endPosition().getTicks(), m_in->endPosition().getTicks()) - m_in->startPosition().getTicks());
}

CrossfadeSide CrossfadeEditorDialog::outSide() const { return {m_outLength->value(), shapeOf(m_outShape)}; }

CrossfadeSide CrossfadeEditorDialog::inSide() const { return {m_inLength->value(), shapeOf(m_inShape)}; }

void CrossfadeEditorDialog::setOutSide(const CrossfadeSide& side)
{
	m_outLength->setValue(side.ticks);
	selectShape(m_outShape, side.shape);
}

void CrossfadeEditorDialog::setInSide(const CrossfadeSide& side)
{
	m_inLength->setValue(side.ticks);
	selectShape(m_inShape, side.shape);
}

void CrossfadeEditorDialog::reload()
{
	if (m_out.isNull() || m_in.isNull()) { return; }
	const ClipEdits& out = m_out->clipEdits();
	const ClipEdits& in = m_in->clipEdits();
	setOutSide(sideOf(out.fadeOutTicks, out.fadeOutShape, overlap()));
	setInSide(sideOf(in.fadeInTicks, in.fadeInShape, overlap()));
	sync();
}

void CrossfadeEditorDialog::sync()
{
	if (m_out.isNull() || m_in.isNull()) { return; }
	m_summary->setText(tr("The clips overlap by %1 ticks. The outgoing clip fades out over the end of it, the incoming "
		"clip fades in from its start.").arg(overlap()));
	m_view->setRamps(m_out->endPosition().getTicks(), outSide(), m_in->startPosition().getTicks(), inSide());
}

bool CrossfadeEditorDialog::apply()
{
	if (m_out.isNull() || m_in.isNull()) { return false; }
	const CrossfadeSide out = outSide();
	const CrossfadeSide in = inSide();
	const ControlResult result = runCommand(this, CommandCall{QStringLiteral("clip.crossfade"),
		{{QStringLiteral("out"), control::clipIdOf(m_out)}, {QStringLiteral("in"), control::clipIdOf(m_in)},
			{QStringLiteral("out_length"), out.ticks}, {QStringLiteral("in_length"), in.ticks},
			{QStringLiteral("out_shape"), fadeShapeName(out.shape)}, {QStringLiteral("in_shape"), fadeShapeName(in.shape)}},
		{}, {}, false});
	if (!result.ok)
	{
		report(tr("The crossfade was not applied: %1").arg(result.errorMessage), true);
		return false;
	}
	report(tr("Crossfade applied: %1 ticks out, %2 ticks in. Undo (Ctrl+Z) reverts it.").arg(out.ticks).arg(in.ticks), false);
	return true;
}

QString CrossfadeEditorDialog::status() const { return m_status->text(); }

void CrossfadeEditorDialog::report(const QString& message, bool failed)
{
	m_status->setText(message);
	a11y::announceStatus(m_status, message, failed ? a11y::Politeness::Assertive : a11y::Politeness::Polite);
}

} // namespace lmms::gui
