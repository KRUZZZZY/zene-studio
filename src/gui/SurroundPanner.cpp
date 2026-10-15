/*
 * SurroundPanner.cpp - the surround panner's window: a room to place a channel in, its LFE send and spread
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

#include "SurroundPanner.h"

#include <algorithm>
#include <functional>

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QKeyEvent>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QSlider>
#include <QVBoxLayout>

#include "DeprecationHelper.h"
#include "CommandForm.h"
#include "ControlRegistry.h"
#include "DesignTokens.h"
#include "MixerSurround.h"

namespace lmms::gui
{

namespace
{

//! The room seen from above, front at the top: the speakers of @a format and the channel's point.
class PannerPad : public QWidget
{
public:
	PannerPad(surround::Format format, QPointF point, QWidget* parent) :
		QWidget(parent), m_format(format), m_point(point)
	{
		setMinimumSize(200, 200);
		setAccessibleName(QObject::tr("Surround position"));
		setCursor(Qt::CrossCursor);
		// 0.5.0 theme 16: the arrow keys move the point (Shift: a larger step); each step is one call.
		setFocusPolicy(Qt::StrongFocus);
		describePoint();
	}

	QPointF point() const { return m_point; }
	//! Called when a drag ends, with the point.
	std::function<void()> released;

protected:
	void paintEvent(QPaintEvent*) override
	{
		const tokens::Palette& pal = tokens::palette();
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		p.fillRect(rect(), pal.control);
		p.setPen(pal.separator);
		p.drawRect(room());
		p.drawLine(toWidget({-1.0, 0.0}), toWidget({1.0, 0.0}));
		p.drawLine(toWidget({0.0, -1.0}), toWidget({0.0, 1.0}));
		drawSpeakers(p, pal);
		p.setPen(QPen(pal.textOnAccent, 1.5));
		p.setBrush(pal.accent);
		p.drawEllipse(toWidget(m_point), 7.0, 7.0);
		if (hasFocus())
		{
			p.setPen(QPen(pal.focusRing, tokens::focusRingWidth()));
			p.setBrush(Qt::NoBrush);
			p.drawRect(QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0));
		}
	}

	void keyPressEvent(QKeyEvent* event) override
	{
		const QPointF direction = arrowDirection(event->key());
		if (direction.isNull())
		{
			QWidget::keyPressEvent(event);
			return;
		}
		const double step = event->modifiers() & Qt::ShiftModifier ? 0.25 : 0.05;
		setPoint(m_point + direction * step);
		if (released) { released(); }
	}

	void mousePressEvent(QMouseEvent* event) override { moveTo(positionF(event)); }
	void mouseMoveEvent(QMouseEvent* event) override { moveTo(positionF(event)); }
	void mouseReleaseEvent(QMouseEvent* event) override
	{
		moveTo(positionF(event));
		if (released) { released(); }
	}

private:
	void drawSpeakers(QPainter& p, const tokens::Palette& pal)
	{
		const QStringList names = surround::speakerNames(m_format);
		p.setFont(tokens::font(tokens::TextRole::Caption));
		for (int c = 0; c < surround::channelCount(m_format); ++c)
		{
			if (surround::roleOf(m_format, c) == surround::Role::Lfe) { continue; }
			float x = 0.f;
			float y = 0.f;
			surround::speakerPoint(m_format, c, &x, &y);
			const QPointF at = toWidget({x, y});
			p.setPen(pal.textMuted);
			p.setBrush(pal.panel);
			p.drawRect(QRectF(at.x() - 6.0, at.y() - 6.0, 12.0, 12.0));
			p.setPen(pal.text);
			p.drawText(QRectF(at.x() - 20.0, at.y() + (y > 0.f ? 8.0 : -24.0), 40.0, 16.0), Qt::AlignCenter, names.at(c));
		}
	}

	//! The square the room is drawn in, inset so the speakers' labels fit.
	QRectF room() const
	{
		const double side = std::min(width(), height()) - 56.0;
		return {(width() - side) / 2.0, (height() - side) / 2.0, side, side};
	}

	QPointF toWidget(QPointF point) const
	{
		const QRectF r = room();
		return {r.center().x() + point.x() * r.width() / 2.0, r.center().y() - point.y() * r.height() / 2.0};
	}

	void moveTo(QPointF at)
	{
		const QRectF r = room();
		setPoint({(at.x() - r.center().x()) / (r.width() / 2.0), (r.center().y() - at.y()) / (r.height() / 2.0)});
	}

	void setPoint(QPointF point)
	{
		m_point = {std::clamp(point.x(), -1.0, 1.0), std::clamp(point.y(), -1.0, 1.0)};
		describePoint();
		update();
	}

	//! The point as a screen reader reads it: left/right and front/back, -1..1.
	void describePoint()
	{
		const QString text = QObject::tr("Left-right %1, back-front %2 (arrow keys move it)")
			.arg(m_point.x(), 0, 'f', 2).arg(m_point.y(), 0, 'f', 2);
		if (accessibleDescription() != text) { setAccessibleDescription(text); }
	}

	static QPointF arrowDirection(int key)
	{
		switch (key)
		{
		case Qt::Key_Left: return {-1.0, 0.0};
		case Qt::Key_Right: return {1.0, 0.0};
		case Qt::Key_Up: return {0.0, 1.0};
		case Qt::Key_Down: return {0.0, -1.0};
		default: return {};
		}
	}

	surround::Format m_format;
	QPointF m_point;
};

QSlider* percentSlider(double value, QWidget* parent)
{
	auto* slider = new QSlider(Qt::Horizontal, parent);
	slider->setRange(0, 100);
	slider->setValue(static_cast<int>(value * 100.0 + 0.5));
	return slider;
}

//! The window's controls, and the one call they make when a change ends.
struct Controls
{
	QString channelId;
	PannerPad* pad = nullptr;
	QSlider* lfe = nullptr;
	QSlider* spread = nullptr;
	QCheckBox* enabled = nullptr;

	void commit(QWidget* parent) const
	{
		const QPointF point = pad->point();
		runCommand(parent, CommandCall{QStringLiteral("mixer.surround_pan"), {{QStringLiteral("channel"), channelId},
			{QStringLiteral("x"), point.x()}, {QStringLiteral("y"), point.y()}, {QStringLiteral("lfe"), lfe->value() / 100.0},
			{QStringLiteral("spread"), spread->value() / 100.0}, {QStringLiteral("enabled"), enabled->isChecked()}}, {}, QString(), false});
	}
};

//! The format the panner pans into: the first surround channel the channel sends into, else 5.1.
surround::Format targetFormat(const QJsonObject& state, QString* into)
{
	surround::Format format = surround::Format::Surround51;
	const QJsonArray sends = state.value(QStringLiteral("surround_sends")).toArray();
	if (sends.isEmpty()) { return format; }
	const QJsonObject first = sends.at(0).toObject();
	surround::formatFromName(first.value(QStringLiteral("format")).toString(), &format);
	*into = QStringLiteral("%1 (%2)").arg(first.value(QStringLiteral("to")).toString(), first.value(QStringLiteral("format")).toString());
	return format;
}

//! A slider commits when it is let go, and on a keyboard step (no drag under way).
void commitOnChange(QSlider* slider, QDialog* dialog, const Controls& controls)
{
	QObject::connect(slider, &QSlider::sliderReleased, dialog, [dialog, controls] { controls.commit(dialog); });
	QObject::connect(slider, &QSlider::valueChanged, dialog, [dialog, controls, slider] {
		if (!slider->isSliderDown()) { controls.commit(dialog); }
	});
}

void connectControls(QDialog* dialog, const Controls& controls)
{
	controls.pad->released = [dialog, controls] {
		const QSignalBlocker quiet(controls.enabled);
		controls.enabled->setChecked(true);  // placing the channel turns the panner on
		controls.commit(dialog);
	};
	commitOnChange(controls.lfe, dialog, controls);
	commitOnChange(controls.spread, dialog, controls);
	QObject::connect(controls.enabled, &QCheckBox::toggled, dialog, [dialog, controls] { controls.commit(dialog); });
}

} // namespace

void showSurroundPanner(const QString& channelId, QWidget* parent)
{
	const QJsonArray channels = ControlRegistry::instance()->invoke(QStringLiteral("mixer.surround_get"),
		{{QStringLiteral("channel"), channelId}}).result.value(QStringLiteral("channels")).toArray();
	const QJsonObject state = channels.isEmpty() ? QJsonObject() : channels.at(0).toObject();
	const QJsonObject pan = state.value(QStringLiteral("pan")).toObject();
	QString into;
	const surround::Format format = targetFormat(state, &into);

	auto* dialog = new QDialog(parent);
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	dialog->setWindowTitle(QObject::tr("Surround Panner - %1").arg(state.value(QStringLiteral("name")).toString()));
	dialog->setProperty("controlCommand", QStringLiteral("mixer.surround_pan"));
	Controls controls{channelId, new PannerPad(format, {pan.value(QStringLiteral("x")).toDouble(0.0), pan.value(QStringLiteral("y")).toDouble(1.0)}, dialog),
		percentSlider(pan.value(QStringLiteral("lfe")).toDouble(), dialog), percentSlider(pan.value(QStringLiteral("spread")).toDouble(), dialog),
		new QCheckBox(QObject::tr("Panner on"), dialog)};
	controls.enabled->setChecked(pan.value(QStringLiteral("enabled")).toBool());
	controls.lfe->setAccessibleName(QObject::tr("LFE send"));
	controls.spread->setAccessibleName(QObject::tr("Spread"));

	auto* layout = new QVBoxLayout(dialog);
	layout->addWidget(new QLabel(into.isEmpty() ? QObject::tr("This channel sends into no surround channel: route it into one "
		"(set to LCR, quad, 5.1 or 7.1) for the panner to act.") : QObject::tr("Pans into %1.").arg(into), dialog));
	layout->addWidget(controls.pad, 1);
	auto* form = new QFormLayout;
	form->addRow(QObject::tr("LFE"), controls.lfe);
	form->addRow(QObject::tr("Spread"), controls.spread);
	form->addRow(QString(), controls.enabled);
	layout->addLayout(form);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
	QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
	layout->addWidget(buttons);
	connectControls(dialog, controls);
	dialog->show();
}

} // namespace lmms::gui
