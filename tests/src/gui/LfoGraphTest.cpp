/*
 * LfoGraphTest.cpp - the LFO label backing follows the rendered font
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
 * along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

#include <QtTest>

#include <QDataStream>
#include <QDomDocument>
#include <QDropEvent>
#include <QFile>
#include <QFontMetrics>
#include <QMimeData>
#include <QPainter>
#include <QTemporaryDir>

#include "Clipboard.h"
#include "Engine.h"
#include "EnvelopeAndLfoParameters.h"
#include "EnvelopeAndLfoView.h"
#include "FontHelper.h"
#include "LfoGraph.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

//! A small wave file, so the LFO has a user-defined shape to sample.
bool writeWaveFile(const QString& path, bool rising)
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly)) { return false; }
	QDataStream out(&file);
	out.setByteOrder(QDataStream::LittleEndian);
	constexpr quint32 sampleCount = 8;
	constexpr quint32 dataBytes = sampleCount * sizeof(qint16);
	out.writeRawData("RIFF", 4);
	out << quint32(36 + dataBytes);
	out.writeRawData("WAVEfmt ", 8);
	out << quint32(16) << quint16(1) << quint16(1) << quint32(44100)
		<< quint32(88200) << quint16(2) << quint16(16);
	out.writeRawData("data", 4);
	out << dataBytes;
	for (quint32 i = 0; i < sampleCount; ++i)
	{
		out << static_cast<qint16>(rising ? i * 1000 : (sampleCount - i) * 1000);
	}
	return out.status() == QDataStream::Ok;
}

//! The envelope view with its drop handler reachable, so that a test can
//! drive the drop path without a running drag.
class TestableEnvelopeAndLfoView : public EnvelopeAndLfoView
{
public:
	using EnvelopeAndLfoView::EnvelopeAndLfoView;
	using EnvelopeAndLfoView::dropEvent;
};

} // namespace

class LfoGraphTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		QVERIFY(m_tempDir.isValid());
		m_wavePath = m_tempDir.filePath(QStringLiteral("custom-lfo.wav"));
		QVERIFY(writeWaveFile(m_wavePath, true));
		m_otherWavePath = m_tempDir.filePath(QStringLiteral("custom-lfo-2.wav"));
		QVERIFY(writeWaveFile(m_otherWavePath, false));
	}
	void cleanupTestCase() { Engine::destroy(); }

	void labelBackingUsesAdjustedFontMetrics()
	{
		EnvelopeAndLfoParameters params(0.f, nullptr);

		LfoGraph graph(nullptr);
		LfoGraph background(nullptr);
		graph.setModel(&params);
		graph.resize(400, 200);
		background.resize(graph.size());
		graph.show();
		background.show();

		const QImage rendered = graph.grab().toImage().convertToFormat(QImage::Format_ARGB32);
		const QImage backgroundImage = background.grab().toImage().convertToFormat(QImage::Format_ARGB32);
		QVERIFY(!rendered.isNull());
		QCOMPARE(rendered.size(), backgroundImage.size());

		const QString infoText = QStringLiteral("0.500 Hz");
		const QPoint textPosition(4, graph.height() - 6);
		const QFont originalFont = graph.font();
		const QFont adjustedFont = adjustedToPixelSize(originalFont, graph.height() * 0.2);
		const QRect expectedRect = QFontMetrics(adjustedFont).boundingRect(infoText).translated(textPosition);
		const QRect originalMetricsRect = QFontMetrics(originalFont).boundingRect(infoText).translated(textPosition);
		QVERIFY(expectedRect.width() > originalMetricsRect.width());

		QImage expectedBacking = backgroundImage.copy();
		{
			QPainter painter(&expectedBacking);
			painter.fillRect(expectedRect, QColor{0, 0, 0, 192});
		}

		int backingPixelsOutsideOriginalMetrics = 0;
		const QRect visibleExpectedRect = expectedRect.intersected(rendered.rect());
		for (int y = visibleExpectedRect.top(); y <= visibleExpectedRect.bottom(); ++y)
		{
			for (int x = visibleExpectedRect.left(); x <= visibleExpectedRect.right(); ++x)
			{
				const QPoint point{x, y};
				if (!originalMetricsRect.contains(point) &&
					rendered.pixelColor(point) == expectedBacking.pixelColor(point) &&
					rendered.pixelColor(point) != backgroundImage.pixelColor(point))
				{
					++backingPixelsOutsideOriginalMetrics;
				}
			}
		}
		QVERIFY2(backingPixelsOutsideOriginalMetrics > 0,
			"the label background did not cover the adjusted-font area beyond the original metrics");
	}

	/*! Dropping a wave file on the envelope view must publish the new wave:
	 *  the audio path samples the wave of the published parameters, also when
	 *  the LFO wave shape is already the user-defined one and no model value
	 *  changes (that is the case that used to leave playback on the old wave). */
	void droppingAUserWavePublishesIt()
	{
		EnvelopeAndLfoParameters params(0.f, nullptr);
		QDomDocument document;
		QDomElement settings = document.createElement(QStringLiteral("elvol"));
		settings.setAttribute(QStringLiteral("lshp"),
			QString::number(static_cast<int>(EnvelopeAndLfoParameters::LfoShape::UserDefinedWave)));
		settings.setAttribute(QStringLiteral("userwavefile"), m_wavePath);
		params.loadSettings(settings);
		const auto before = params.getLfoUserWave();
		QVERIFY(before != nullptr);
		QVERIFY(before->size() > 0);

		TestableEnvelopeAndLfoView view(nullptr);
		view.setModel(&params);

		QMimeData mime;
		mime.setData(Clipboard::mimeType(Clipboard::MimeType::StringPair),
			QStringLiteral("samplefile:").append(m_otherWavePath).toUtf8());
		QDropEvent drop(QPointF(10, 10), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
		view.dropEvent(&drop);

		const auto after = params.getLfoUserWave();
		QVERIFY2(after != before,
			"the dropped wave was not published: playback would keep the previous wave");
		QVERIFY(after != nullptr);
		QVERIFY(after->size() > 0);
	}

private:
	QTemporaryDir m_tempDir;
	QString m_wavePath;
	QString m_otherWavePath;
};

QTEST_MAIN(LfoGraphTest)
#include "LfoGraphTest.moc"
