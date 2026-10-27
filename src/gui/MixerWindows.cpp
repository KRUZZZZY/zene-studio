/*
 * MixerWindows.cpp - the large meters, the routing matrix's menu entry and the EQ comparison
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
#include "MixerWindows.h"

#include <algorithm>
#include <cmath>

#include <QComboBox>
#include <QDialog>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPointer>
#include <QTimer>
#include <QVBoxLayout>

#include "CommandForm.h"
#include "ControlRegistry.h"
#include "DesignTokens.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "InputChannelMeters.h"
#include "InputLevelMeter.h"
#include "MainWindow.h"
#include "Mixer.h"
#include "MixerRoutingMatrix.h"

namespace lmms::gui
{

namespace
{

//! MX-131: the master's RMS as two tall bars (-60..0 dB), the correlation and the loudness under them;
//! MX-134: the engine's input, live, in a narrower pair at the right (InputLevelMeter.h).
class LargeMeters : public QDialog
{
public:
	static constexpr int InputWidth = 64;  //!< the input meter's column

	explicit LargeMeters(QWidget* parent) :
		QDialog(parent),
		m_input(new InputLevelMeter(this))
	{
		setWindowTitle(QObject::tr("Large Meters"));
		setAttribute(Qt::WA_DeleteOnClose);
		setAccessibleName(QObject::tr("Large Meters"));
		setMinimumSize(160 + InputWidth, 240);  // below it the bars' rectangles would turn negative
		resize(260 + InputWidth, 520);
		auto* timer = new QTimer(this);
		connect(timer, &QTimer::timeout, this, qOverload<>(&QWidget::update));
		timer->start(40);
		auto* slow = new QTimer(this);  // the loudness reading changes slowly; asked twice a second
		connect(slow, &QTimer::timeout, this, [this] { readLoudness(); });
		slow->start(500);
		readLoudness();
	}

protected:
	void resizeEvent(QResizeEvent*) override { m_input->setGeometry(width() - InputWidth, 0, InputWidth - 4, height() - 60); }

	void paintEvent(QPaintEvent*) override
	{
		const tokens::Palette& pal = tokens::palette();
		QPainter p(this);
		p.fillRect(rect(), pal.panel);
		const MixerChannel* master = Engine::mixer()->mixerChannel(0);
		const float levels[2] = {master->m_rmsLeft.get(), master->m_rmsRight.get()};
		const int bottom = height() - 60;
		const int masterWidth = width() - InputWidth;  // the input meter has the right-hand column
		for (int side = 0; side < 2; ++side)
		{
			const double db = levels[side] > 0.f ? 20.0 * std::log10(levels[side]) : -60.0;
			const double share = std::clamp((db + 60.0) / 60.0, 0.0, 1.0);
			const QRect bar(20 + side * (masterWidth / 2 - 10), 20, masterWidth / 2 - 40, bottom - 20);
			p.fillRect(bar, pal.control);
			// Which side, and its level in figures, above the bar.
			p.setPen(pal.text);
			const QString figure = levels[side] > 0.f ? QString::number(db, 'f', 1) : QStringLiteral("-inf");
			p.drawText(QRect(bar.left(), 0, bar.width(), 20), Qt::AlignCenter,
				QStringLiteral("%1 %2 dB").arg(side == 0 ? QObject::tr("L") : QObject::tr("R"), figure));
			const int top = bar.bottom() - static_cast<int>(share * bar.height());
			p.fillRect(QRect(bar.left(), top, bar.width(), bar.bottom() - top), db > -6.0 ? pal.meterHigh : db > -18.0 ? pal.meterMid : pal.meterLow);
		}
		p.setPen(pal.text);
		p.setFont(tokens::font(tokens::TextRole::Title));
		p.drawText(QRect(0, bottom + 4, width(), 26), Qt::AlignCenter, QObject::tr("Correlation %1").arg(master->m_correlation.get(), 0, 'f', 2));
		p.drawText(QRect(0, bottom + 30, width(), 26), Qt::AlignCenter, p.fontMetrics().elidedText(m_loudness, Qt::ElideRight, width() - 8));
	}

private:
	//! meter.get_state keeps its readings under "live"; a failed call says why, a disarmed meter says how to arm it.
	void readLoudness()
	{
		const ControlResult state = ControlRegistry::instance()->invoke(QStringLiteral("meter.get_state"));
		const QJsonValue lufs = state.result.value(QStringLiteral("live")).toObject().value(QStringLiteral("short_term_lufs"));
		if (!state.ok) { m_loudness = QObject::tr("Loudness unavailable: %1").arg(state.errorMessage); }
		else if (!state.result.value(QStringLiteral("armed")).toBool()) { m_loudness = QObject::tr("Loudness Meter off (channel menu)"); }
		else { m_loudness = lufs.isDouble() ? QObject::tr("%1 LUFS (short term)").arg(lufs.toDouble(), 0, 'f', 1) : QObject::tr("Loudness: no reading yet"); }
		setToolTip(m_loudness);  // the whole line, when the window is too narrow for it
	}

	InputLevelMeter* m_input;
	QString m_loudness;
};

//! MX-140: the two responses mixer.eq_compare reports, -18..18 dB over 20 Hz..20 kHz; A in the accent colour.
class EqCurves : public QWidget
{
public:
	using QWidget::QWidget;
	void setCurve(const QJsonArray& curve) { m_curve = curve; update(); }

protected:
	void paintEvent(QPaintEvent*) override
	{
		const tokens::Palette& pal = tokens::palette();
		QPainter p(this);
		p.fillRect(rect(), pal.control);
		p.setPen(pal.separator);
		p.drawLine(0, height() / 2, width(), height() / 2);
		drawCurve(p, QStringLiteral("a_db"), pal.accent);
		drawCurve(p, QStringLiteral("b_db"), pal.meterMid);
		drawAxes(p, pal);
	}

private:
	//! The scale (gain on the right, the log frequency span along the bottom) and which colour is which channel.
	void drawAxes(QPainter& p, const tokens::Palette& pal)
	{
		p.setFont(tokens::font(tokens::TextRole::Caption));
		const QRect inner = rect().adjusted(4, 2, -4, -2);
		p.setPen(pal.textMuted);
		p.drawText(inner, Qt::AlignRight | Qt::AlignTop, QObject::tr("+18 dB"));
		p.drawText(QRect(inner.left(), height() / 2 - 16, inner.width(), 16), Qt::AlignRight | Qt::AlignBottom, QObject::tr("0 dB"));
		p.drawText(inner.adjusted(0, 0, 0, -16), Qt::AlignRight | Qt::AlignBottom, QObject::tr("-18 dB"));
		p.drawText(inner, Qt::AlignLeft | Qt::AlignBottom, QObject::tr("20 Hz"));
		p.drawText(inner, Qt::AlignHCenter | Qt::AlignBottom, QObject::tr("632 Hz"));
		p.drawText(inner, Qt::AlignRight | Qt::AlignBottom, QObject::tr("20 kHz"));
		p.setPen(pal.accent);
		p.drawText(inner, Qt::AlignLeft | Qt::AlignTop, QObject::tr("A"));
		p.setPen(pal.meterMid);
		p.drawText(inner.adjusted(16, 0, 0, 0), Qt::AlignLeft | Qt::AlignTop, QObject::tr("B"));
	}

	void drawCurve(QPainter& p, const QString& key, const QColor& color)
	{
		if (m_curve.size() < 2) { return; }
		QPolygonF line;
		for (int i = 0; i < m_curve.size(); ++i)
		{
			const double db = std::clamp(m_curve.at(i).toObject().value(key).toDouble(), -18.0, 18.0);
			line << QPointF(width() * i / (m_curve.size() - 1.0), height() * (0.5 - db / 36.0));
		}
		p.setPen(QPen(color, 2));
		p.drawPolyline(line);
	}

	QJsonArray m_curve;
};

class EqCompare : public QDialog
{
public:
	explicit EqCompare(QWidget* parent) :
		QDialog(parent),
		m_a(new QComboBox(this)),
		m_b(new QComboBox(this)),
		m_curves(new EqCurves(this)),
		m_summary(new QLabel(this))
	{
		setWindowTitle(QObject::tr("Compare Channel EQ"));
		setAttribute(Qt::WA_DeleteOnClose);
		setProperty("controlCommand", QStringLiteral("mixer.eq_compare"));
		const QJsonArray channels = ControlRegistry::instance()->invoke(QStringLiteral("routing.matrix")).result.value(QStringLiteral("channels")).toArray();
		for (const QJsonValue& value : channels)
		{
			m_a->addItem(value.toObject().value(QStringLiteral("name")).toString(), value.toObject().value(QStringLiteral("channel")).toString());
			m_b->addItem(m_a->itemText(m_a->count() - 1), m_a->itemData(m_a->count() - 1));
		}
		m_b->setCurrentIndex(std::min(1, m_b->count() - 1));
		m_a->setAccessibleName(QObject::tr("Channel A"));
		m_b->setAccessibleName(QObject::tr("Channel B"));
		auto* row = new QHBoxLayout;
		row->addWidget(new QLabel(QObject::tr("A"), this));
		row->addWidget(m_a, 1);
		row->addWidget(new QLabel(QObject::tr("B"), this));
		row->addWidget(m_b, 1);
		auto* layout = new QVBoxLayout(this);
		layout->addLayout(row);
		layout->addWidget(m_curves, 1);
		layout->addWidget(m_summary);
		connect(m_a, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { compare(); });
		connect(m_b, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { compare(); });
		resize(520, 320);
		compare();
	}

private:
	void compare()
	{
		const ControlResult compared = ControlRegistry::instance()->invoke(QStringLiteral("mixer.eq_compare"),
			{{QStringLiteral("a"), m_a->currentData().toString()}, {QStringLiteral("b"), m_b->currentData().toString()}, {QStringLiteral("points"), 64}});
		// A failed comparison says why and keeps the curves last drawn - never a flat "identical" 0 dB.
		if (!compared.ok)
		{
			m_summary->setText(QObject::tr("Could not compare: %1").arg(compared.errorMessage));
			return;
		}
		const QJsonObject& result = compared.result;
		m_curves->setCurve(result.value(QStringLiteral("curve")).toArray());
		const QJsonObject d = result.value(QStringLiteral("difference")).toObject();
		m_summary->setText(QObject::tr("B - A: low %1 dB, mid %2 dB at %3 Hz, high %4 dB")
			.arg(d.value(QStringLiteral("low")).toDouble(), 0, 'f', 1).arg(d.value(QStringLiteral("mid")).toDouble(), 0, 'f', 1)
			.arg(d.value(QStringLiteral("mid_freq")).toDouble(), 0, 'f', 0).arg(d.value(QStringLiteral("high")).toDouble(), 0, 'f', 1));
	}

	QComboBox* m_a;
	QComboBox* m_b;
	EqCurves* m_curves;
	QLabel* m_summary;
};

/*! One window of each kind, raised when asked for again. It belongs to the main window, not to the
 *  menu: a strip's context menu is deleted as soon as it closes, and would take the window with it. */
template<typename Window>
void showOne(QPointer<QDialog>* open)
{
	if (open->isNull()) { *open = new Window(getGUI() != nullptr ? getGUI()->mainWindow() : nullptr); }
	(*open)->show();
	(*open)->raise();
	(*open)->activateWindow();
}

QPointer<QDialog> s_meters;
QPointer<QDialog> s_eq;

} // namespace

void addMixerWindows(QMenu* menu)
{
	QAction* meters = menu->addAction(QObject::tr("Large Meters..."));
	meters->setProperty("controlCommand", QStringLiteral("meter.get_state"));
	QObject::connect(meters, &QAction::triggered, menu, [] { showOne<LargeMeters>(&s_meters); });
	QAction* inputs = menu->addAction(QObject::tr("Input Meters..."));  // Q050-340: each capture channel, each armed track
	inputs->setProperty("controlCommand", QStringLiteral("window.input_meters"));
	QObject::connect(inputs, &QAction::triggered, menu, [] { ControlRegistry::instance()->invoke(QStringLiteral("window.input_meters")); });
	QAction* scope = menu->addAction(QObject::tr("Phase Scope..."));  // MX-137
	scope->setProperty("controlCommand", QStringLiteral("window.phase_scope"));
	QObject::connect(scope, &QAction::triggered, menu, [] { ControlRegistry::instance()->invoke(QStringLiteral("window.phase_scope")); });
	QAction* matrix = menu->addAction(QObject::tr("Routing Matrix..."));
	matrix->setProperty("controlCommand", QStringLiteral("routing.matrix"));
	QObject::connect(matrix, &QAction::triggered, menu, [] { showRoutingMatrix(); });  // theme 16: sparse (MixerRoutingMatrix.h)
	QAction* eq = menu->addAction(QObject::tr("Compare Channel EQ..."));
	eq->setProperty("controlCommand", QStringLiteral("mixer.eq_compare"));
	QObject::connect(eq, &QAction::triggered, menu, [] { showOne<EqCompare>(&s_eq); });
}

} // namespace lmms::gui
