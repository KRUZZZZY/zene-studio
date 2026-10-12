/*
 * SlicerTWaveformTest.cpp - slice endpoints remain fixed in the waveform editor
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <QtTest>

#include <cmath>

#include <QDomDocument>
#include <QMouseEvent>

#include "Engine.h"
#include "SampleBuffer.h"
#include "SlicerT.h"
#include "SlicerTWaveform.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

void setThreeSlices(SlicerT& instrument)
{
	QDomDocument doc;
	QDomElement settings = doc.createElement(QStringLiteral("slicert"));
	const SampleBuffer buffer(std::vector<SampleFrame>(64), Engine::audioEngine()->outputSampleRate());
	settings.setAttribute(QStringLiteral("sampledata"), buffer.toBase64());
	settings.setAttribute(QStringLiteral("totalSlices"), 3);
	settings.setAttribute(QStringLiteral("slice_0"), 0.0);
	settings.setAttribute(QStringLiteral("slice_1"), 0.5);
	settings.setAttribute(QStringLiteral("slice_2"), 1.0);
	instrument.loadSettings(settings);
}

QDomElement savedSettings(SlicerT& instrument)
{
	QDomDocument doc;
	QDomElement settings = doc.createElement(QStringLiteral("slicert"));
	instrument.saveSettings(doc, settings);
	return settings;
}

void sendMouse(SlicerTWaveform& waveform, QEvent::Type type, QPoint point,
	Qt::MouseButton button, Qt::MouseButtons buttons)
{
	QMouseEvent event(type, QPointF(point), QPointF(waveform.mapToGlobal(point)), button, buttons, Qt::NoModifier);
	QApplication::sendEvent(&waveform, &event);
}

void clickEndpoint(SlicerTWaveform& waveform, bool last, Qt::MouseButton button)
{
	const QPoint point(last ? waveform.width() - 1 : 0, 60);
	sendMouse(waveform, QEvent::MouseMove, point, Qt::NoButton, Qt::NoButton);
	sendMouse(waveform, QEvent::MouseButtonPress, point, button, button);
}

void dragEndpoint(SlicerTWaveform& waveform, bool last)
{
	const QPoint start(last ? waveform.width() - 1 : 0, 60);
	const QPoint end(last ? waveform.width() * 3 / 4 : waveform.width() / 4, 60);
	sendMouse(waveform, QEvent::MouseMove, start, Qt::NoButton, Qt::NoButton);
	sendMouse(waveform, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
	sendMouse(waveform, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
	sendMouse(waveform, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
}

} // namespace

class SlicerTWaveformTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase() { Engine::init(true); }
	void cleanupTestCase() { Engine::destroy(); }

	void firstEndpointCannotBeDeleted()
	{
		SlicerT instrument(nullptr);
		setThreeSlices(instrument);
		SlicerTWaveform waveform(248, 128, &instrument, nullptr);
		waveform.resize(248, 128);
		waveform.show();
		QCoreApplication::processEvents();

		clickEndpoint(waveform, false, Qt::RightButton);

		const QDomElement saved = savedSettings(instrument);
		QCOMPARE(saved.attribute(QStringLiteral("totalSlices")).toInt(), 3);
		QCOMPARE(saved.attribute(QStringLiteral("slice_0")).toFloat(), 0.0f);
	}

	void lastEndpointCannotBeDeleted()
	{
		SlicerT instrument(nullptr);
		setThreeSlices(instrument);
		SlicerTWaveform waveform(248, 128, &instrument, nullptr);
		waveform.resize(248, 128);
		waveform.show();
		QCoreApplication::processEvents();

		clickEndpoint(waveform, true, Qt::RightButton);

		const QDomElement saved = savedSettings(instrument);
		QCOMPARE(saved.attribute(QStringLiteral("totalSlices")).toInt(), 3);
		QCOMPARE(saved.attribute(QStringLiteral("slice_2")).toFloat(), 1.0f);
	}

	void firstEndpointCannotBeMoved()
	{
		SlicerT instrument(nullptr);
		setThreeSlices(instrument);
		SlicerTWaveform waveform(248, 128, &instrument, nullptr);
		waveform.resize(248, 128);
		waveform.show();
		QCoreApplication::processEvents();

		dragEndpoint(waveform, false);

		QCOMPARE(savedSettings(instrument).attribute(QStringLiteral("slice_0")).toFloat(), 0.0f);
	}

	void lastEndpointCannotBeMoved()
	{
		SlicerT instrument(nullptr);
		setThreeSlices(instrument);
		SlicerTWaveform waveform(248, 128, &instrument, nullptr);
		waveform.resize(248, 128);
		waveform.show();
		QCoreApplication::processEvents();

		dragEndpoint(waveform, true);

		QCOMPARE(savedSettings(instrument).attribute(QStringLiteral("slice_2")).toFloat(), 1.0f);
	}

	void interiorSliceRemainsMovable()
	{
		SlicerT instrument(nullptr);
		setThreeSlices(instrument);
		SlicerTWaveform waveform(248, 128, &instrument, nullptr);
		waveform.resize(248, 128);
		waveform.show();
		QCoreApplication::processEvents();

		const QPoint start(waveform.width() / 2, 60);
		const QPoint end(waveform.width() * 3 / 10, 60);
		sendMouse(waveform, QEvent::MouseMove, start, Qt::NoButton, Qt::NoButton);
		sendMouse(waveform, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
		sendMouse(waveform, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
		sendMouse(waveform, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);

		const QDomElement saved = savedSettings(instrument);
		QCOMPARE(saved.attribute(QStringLiteral("totalSlices")).toInt(), 3);
		QVERIFY(std::abs(saved.attribute(QStringLiteral("slice_1")).toFloat() - 0.3f) < 0.01f);
	}
};

QTEST_MAIN(SlicerTWaveformTest)
#include "SlicerTWaveformTest.moc"
