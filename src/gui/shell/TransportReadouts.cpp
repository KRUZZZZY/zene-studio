/*
 * TransportReadouts.cpp - the shell header's tempo, position, meter, level and load readouts
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

#include "TransportReadouts.h"

#include <cmath>

#include <QHBoxLayout>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include "Accessibility.h"
#include "AudioEngine.h"
#include "ControlRegistry.h"
#include "DesignTokens.h"
#include "Engine.h"
#include "MeterModel.h"
#include "Song.h"

namespace lmms::gui
{

namespace
{

constexpr int kCaptionHeight = 13;

QString signedValue(int v) { return v > 0 ? QStringLiteral("+%1").arg(v) : QString::number(v); }

} // namespace

ReadoutField::ReadoutField(const QString& caption, QWidget* parent)
	: QWidget(parent)
	, m_caption(caption)
{
	setFocusPolicy(Qt::TabFocus);
	setAttribute(Qt::WA_Hover, true);
	a11y::announce(this, caption);
}

void ReadoutField::bind(IntModel* model, std::function<QString(int)> format, int pixelsPerStep)
{
	m_model = model;
	m_format = std::move(format);
	m_pixelsPerStep = std::max(1, pixelsPerStep);
	setCursor(Qt::SizeVerCursor);
	setToolTip(tr("%1: drag or scroll to change, double-click to type").arg(m_caption));
	connect(model, &Model::dataChanged, this, &ReadoutField::refresh);
	refresh();
}

void ReadoutField::bindText(std::function<QString()> text, std::function<bool(const QString&)> typed)
{
	m_text = std::move(text);
	m_typed = std::move(typed);
	if (m_typed) { setToolTip(tr("%1: double-click to type").arg(m_caption)); }
	refresh();
}

void ReadoutField::refresh()
{
	QString value;
	if (m_model != nullptr && m_format) { value = m_format(m_model->value()); }
	else if (m_text) { value = m_text(); }
	if (value == m_value) { return; }
	m_value = value;
	setAccessibleDescription(m_value);
	updateGeometry();
	update();
}

QSize ReadoutField::sizeHint() const
{
	const QFontMetrics value(tokens::font(tokens::TextRole::Display));
	const QFontMetrics caption(tokens::font(tokens::TextRole::Caption));
	const int w = std::max(value.horizontalAdvance(m_value.isEmpty() ? QStringLiteral("000") : m_value),
		caption.horizontalAdvance(m_caption.toUpper()) + 2);
	return {w + 2 * tokens::space::md, tokens::size::headerHeight - 2 * tokens::space::sm};
}

void ReadoutField::paintEvent(QPaintEvent*)
{
	const tokens::Palette& pal = tokens::palette();
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
	const bool live = m_model != nullptr || m_typed;
	p.setPen(Qt::NoPen);
	p.setBrush(m_dragging ? pal.selected : (underMouse() && live) ? pal.controlHover : pal.control);
	p.drawRoundedRect(r, tokens::radius::lg, tokens::radius::lg);
	if (hasFocus())
	{
		p.setPen(QPen(pal.focusRing, 1.5));
		p.setBrush(Qt::NoBrush);
		p.drawRoundedRect(r.adjusted(0.75, 0.75, -0.75, -0.75), tokens::radius::lg, tokens::radius::lg);
	}
	const QRect inner = rect().adjusted(tokens::space::md, tokens::space::xs, -tokens::space::md, -tokens::space::xs);
	p.setFont(tokens::font(tokens::TextRole::Caption));
	p.setPen(pal.textMuted);
	p.drawText(QRect(inner.left(), inner.top(), inner.width(), kCaptionHeight), Qt::AlignLeft | Qt::AlignVCenter,
		m_caption.toUpper());
	p.setFont(tokens::font(tokens::TextRole::Display));
	p.setPen(pal.text);
	p.drawText(QRect(inner.left(), inner.top() + kCaptionHeight, inner.width(), inner.height() - kCaptionHeight),
		Qt::AlignLeft | Qt::AlignVCenter, m_value);
}

void ReadoutField::mousePressEvent(QMouseEvent* event)
{
	if (m_model == nullptr || event->button() != Qt::LeftButton) { return; }
	// One journal step for the whole drag, as LcdSpinBox makes it.
	m_model->addJournalCheckPoint();
	m_model->saveJournallingState(false);
	m_pressY = event->pos().y();
	m_pressValue = m_model->value();
	m_dragging = true;
	update();
}

void ReadoutField::mouseMoveEvent(QMouseEvent* event)
{
	if (!m_dragging || m_model == nullptr) { return; }
	const int perStep = event->modifiers().testFlag(Qt::ShiftModifier) ? m_pixelsPerStep * 4 : m_pixelsPerStep;
	m_model->setValue(static_cast<float>(m_pressValue + (m_pressY - event->pos().y()) / perStep));
}

void ReadoutField::mouseReleaseEvent(QMouseEvent*)
{
	if (!m_dragging) { return; }
	m_dragging = false;
	if (m_model != nullptr) { m_model->restoreJournallingState(); }
	update();
}

void ReadoutField::wheelEvent(QWheelEvent* event)
{
	if (m_model == nullptr) { return; }
	step(event->angleDelta().y() > 0 ? 1 : -1);
	event->accept();
}

void ReadoutField::keyPressEvent(QKeyEvent* event)
{
	if (event->key() == Qt::Key_Up) { step(1); }
	else if (event->key() == Qt::Key_Down) { step(-1); }
	else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) { mouseDoubleClickEvent(nullptr); }
	else { QWidget::keyPressEvent(event); }
}

void ReadoutField::step(int delta)
{
	if (m_model == nullptr) { return; }
	m_model->setValue(static_cast<float>(m_model->value() + delta));
}

void ReadoutField::mouseDoubleClickEvent(QMouseEvent*)
{
	if (m_model == nullptr && !m_typed) { return; }
	if (m_editor == nullptr)
	{
		m_editor = new QLineEdit(this);
		m_editor->setFrame(false);
		m_editor->setFont(tokens::font(tokens::TextRole::Display));
		m_editor->setAccessibleName(tr("Type %1").arg(m_caption));
		connect(m_editor, &QLineEdit::editingFinished, this, &ReadoutField::commitTyped);
	}
	m_editor->setGeometry(rect().adjusted(tokens::space::xs, kCaptionHeight, -tokens::space::xs, -tokens::space::xxs));
	m_editor->setText(m_model != nullptr ? QString::number(m_model->value()) : m_value);
	m_editor->selectAll();
	m_editor->show();
	m_editor->setFocus();
}

void ReadoutField::commitTyped()
{
	if (m_editor == nullptr || !m_editor->isVisible()) { return; }
	const QString text = m_editor->text().trimmed();
	m_editor->hide();
	if (m_typed) { m_typed(text); }
	else if (m_model != nullptr)
	{
		bool ok = false;
		const int v = text.toInt(&ok);
		if (ok)
		{
			m_model->addJournalCheckPoint();
			m_model->setValue(static_cast<float>(v));
		}
	}
	refresh();
	setFocus();
}

namespace
{

//! "-14.2 / -1.0" (short-term LUFS / true peak dBTP) while the meter runs; "off" otherwise.
QString loudnessText()
{
	const ControlResult state = ControlRegistry::instance()->invoke(QStringLiteral("meter.get_state"));
	if (!state.ok || !state.result.value(QStringLiteral("armed")).toBool()) { return QObject::tr("off"); }
	const QJsonObject live = state.result.value(QStringLiteral("live")).toObject();
	const auto reading = [&live](const char* key) {
		const QJsonValue value = live.value(QLatin1String(key));
		return value.isDouble() ? QString::number(value.toDouble(), 'f', 1) : QStringLiteral("-");
	};
	return QStringLiteral("%1 / %2").arg(reading("short_term_lufs"), reading("true_peak_dbtp"));
}

} // namespace

TransportReadouts::TransportReadouts(QWidget* parent)
	: QWidget(parent)
{
	setObjectName(QStringLiteral("transportReadouts"));
	auto* row = new QHBoxLayout(this);
	row->setContentsMargins(0, 0, 0, 0);
	row->setSpacing(tokens::space::xs);
	Song* song = Engine::getSong();
	m_tempo = new ReadoutField(tr("Tempo"), this);
	m_meter = new ReadoutField(tr("Meter"), this);
	m_position = new ReadoutField(tr("Position"), this);
	m_time = new ReadoutField(tr("Time"), this);
	m_volume = new ReadoutField(tr("Master"), this);
	m_pitch = new ReadoutField(tr("Transpose"), this);
	m_load = new ReadoutField(tr("CPU"), this);
	m_loudness = new ReadoutField(tr("LUFS"), this);
	for (ReadoutField* f : {m_tempo, m_meter, m_position, m_time, m_volume, m_pitch, m_load, m_loudness}) { row->addWidget(f); }
	// A shell built without an engine (a widget test) shows the readouts inert.
	if (song != nullptr) { bindToSong(song); }
}

void TransportReadouts::bindToSong(Song* song)
{
	m_tempo->bind(&song->tempoModel(), [](int v) { return QStringLiteral("%1").arg(v); }, 3);
	MeterModel* meter = &song->getTimeSigModel();
	m_meter->bindText([meter]() { return QStringLiteral("%1/%2").arg(meter->getNumerator()).arg(meter->getDenominator()); },
		[meter](const QString& typed) {
			const QStringList parts = typed.split(QLatin1Char('/'));
			bool okN = false;
			bool okD = false;
			const int n = parts.value(0).toInt(&okN);
			const int d = parts.value(1).toInt(&okD);
			if (!okN || !okD || n < 1 || d < 1) { return false; }
			meter->numeratorModel().setValue(static_cast<float>(n));
			meter->denominatorModel().setValue(static_cast<float>(d));
			return true;
		});
	connect(meter, &Model::dataChanged, m_meter, &ReadoutField::refresh);
	m_position->bindText([song]() {
		const TimePos pos = song->getPlayPos();
		const int ticksPerBeat = std::max(1, song->ticksPerBar() / std::max(1, song->getTimeSigModel().getNumerator()));
		const int sixteenth = std::max(1, ticksPerBeat / 4);
		return QStringLiteral("%1.%2.%3").arg(pos.getBar() + 1).arg(song->getBeat() + 1)
			.arg(song->getBeatTicks() / sixteenth + 1);
	}, [song](const QString& typed) {
		// "bar" or "bar.beat": jump there (transport.seek), as typing a position does in any DAW.
		const QStringList parts = typed.split(QLatin1Char('.'));
		bool okBar = false;
		const int bar = parts.value(0).toInt(&okBar);
		const int beat = parts.size() > 1 ? parts.value(1).toInt() : 1;
		if (!okBar || bar < 1) { return false; }
		const int ticksPerBeat = song->ticksPerBar() / std::max(1, song->getTimeSigModel().getNumerator());
		const int ticks = (bar - 1) * song->ticksPerBar() + std::max(0, beat - 1) * ticksPerBeat;
		return ControlRegistry::instance()->invoke(QStringLiteral("transport.seek"), {{QStringLiteral("ticks"), ticks}}).ok;
	});
	m_time->bindText([song]() {
		const int ms = song->getMilliseconds();
		return QStringLiteral("%1:%2.%3").arg(ms / 60000).arg((ms / 1000) % 60, 2, 10, QLatin1Char('0'))
			.arg((ms % 1000) / 10, 2, 10, QLatin1Char('0'));
	});
	m_volume->bind(&song->masterVolumeModel(), [](int v) { return QStringLiteral("%1%").arg(v); }, 2);
	m_pitch->bind(&song->masterPitchModel(), [](int v) { return signedValue(v); }, 8);
	m_load->bindText([]() { return QStringLiteral("%1%").arg(Engine::audioEngine()->cpuLoad()); });
	// MX-132: the master's loudness (meter.get_state) - short-term LUFS and true peak while the
	// meter is armed; typing "on" or "off" arms or disarms it (meter.arm).
	m_loudness->bindText(loudnessText, [](const QString& typed) {
		const QString word = typed.trimmed().toLower();
		if (word != QStringLiteral("on") && word != QStringLiteral("off")) { return false; }
		return ControlRegistry::instance()->invoke(QStringLiteral("meter.arm"),
			{{QStringLiteral("enabled"), word == QStringLiteral("on")}}).ok;
	});
}

void TransportReadouts::tick()
{
	if (Engine::getSong() == nullptr) { return; }
	m_position->refresh();
	m_time->refresh();
	m_load->refresh();
	// The loudness reading changes at the meter's own 100 ms block rate; 60 Hz would only
	// re-ask the registry for the same numbers.
	constexpr int LoudnessEvery = 15;
	if (++m_ticks % LoudnessEvery == 0) { m_loudness->refresh(); }
}

} // namespace lmms::gui
