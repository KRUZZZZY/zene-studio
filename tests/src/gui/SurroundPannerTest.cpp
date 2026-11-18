/*
 * SurroundPannerTest.cpp - regressions for the surround panner's live controls and route display
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
 */

#include <QtTest>

#include <algorithm>
#include <cmath>

#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QSlider>
#include <QTemporaryDir>
#include <QTimer>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "ControlRegistry.h"
#include "DesignTokens.h"
#include "Engine.h"
#include "GuiTestApplication.h"
#include "Mixer.h"
#include "MixerSurround.h"
#include "SurroundPanner.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QJsonObject panState(const QString& channel)
{
	const QJsonArray channels = run(QStringLiteral("mixer.surround_get"), {{QStringLiteral("channel"), channel}})
		.result.value(QStringLiteral("channels")).toArray();
	return channels.isEmpty() ? QJsonObject() : channels.at(0).toObject().value(QStringLiteral("pan")).toObject();
}

QWidget* pannerWindow()
{
	for (QWidget* widget : QApplication::topLevelWidgets())
	{
		if (widget->windowTitle().startsWith(QStringLiteral("Surround Panner")) && widget->isVisible()) { return widget; }
	}
	return nullptr;
}

template<typename T>
T* childNamed(QWidget* parent, const QString& accessibleName)
{
	for (T* child : parent->findChildren<T*>())
	{
		if (child->accessibleName() == accessibleName) { return child; }
	}
	return nullptr;
}

QColor padPixelAtRoom(QWidget* pad, double x, double y)
{
	const double side = std::min(pad->width(), pad->height()) - 56.0;
	const QPointF at(pad->width() / 2.0 + x * side / 2.0, pad->height() / 2.0 - y * side / 2.0);
	return pad->grab().toImage().pixelColor(static_cast<int>(at.x()), static_cast<int>(at.y()));
}

} // namespace

class SurroundPannerTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		guitest::startGui(m_home);
		QVERIFY(getGUI() != nullptr);
		Engine::audioEngine()->audioDev()->stopProcessing();
		ControlRegistry::setReady(true);
		while (Engine::mixer()->numChannels() < 4) { Engine::mixer()->createChannel(); }
		for (int i = 0; i < 4; ++i) { m_channels[i] = control::channelIdOf(Engine::mixer()->mixerChannel(i)); }
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void aSliderEditKeepsNewerPanValues()
	{
		QVERIFY(run(QStringLiteral("mixer.surround_pan"), {{QStringLiteral("channel"), m_channels[1]}, {QStringLiteral("x"), 0.1},
			{QStringLiteral("y"), 0.8}, {QStringLiteral("lfe"), 0.1}, {QStringLiteral("spread"), 0.2}, {QStringLiteral("enabled"), false}}).ok);
		showSurroundPanner(m_channels[1]);
		QWidget* window = pannerWindow();
		QVERIFY(window != nullptr);
		auto* lfe = childNamed<QSlider>(window, QStringLiteral("LFE send"));
		QVERIFY(lfe != nullptr);
		QVERIFY(run(QStringLiteral("mixer.surround_pan"), {{QStringLiteral("channel"), m_channels[1]}, {QStringLiteral("x"), -0.4},
			{QStringLiteral("y"), -0.3}, {QStringLiteral("spread"), 0.65}, {QStringLiteral("enabled"), true}}).ok);
		lfe->setValue(45);
		const QJsonObject pan = panState(m_channels[1]);
		const bool preserved = std::abs(pan.value(QStringLiteral("x")).toDouble() + 0.4) < 0.01
			&& std::abs(pan.value(QStringLiteral("y")).toDouble() + 0.3) < 0.01
			&& std::abs(pan.value(QStringLiteral("spread")).toDouble() - 0.65) < 0.01
			&& pan.value(QStringLiteral("enabled")).toBool()
			&& std::abs(pan.value(QStringLiteral("lfe")).toDouble() - 0.45) < 0.01;
		window->close();
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		QVERIFY(preserved);
	}

	void aRouteAndFormatChangeRefreshThePanner()
	{
		QVERIFY(run(QStringLiteral("mixer.channel_format"), {{QStringLiteral("channel"), m_channels[3]}, {QStringLiteral("format"), QStringLiteral("5.1")}}).ok);
		QVERIFY(run(QStringLiteral("mixer.channel_format"), {{QStringLiteral("channel"), m_channels[2]}, {QStringLiteral("format"), QStringLiteral("7.1")}}).ok);
		QVERIFY(run(QStringLiteral("mixer.send_to"), {{QStringLiteral("channel"), m_channels[1]}, {QStringLiteral("to"), m_channels[3]},
			{QStringLiteral("amount"), 1.0}}).ok);
		showSurroundPanner(m_channels[1]);
		QWidget* window = pannerWindow();
		QVERIFY(window != nullptr);
		QLabel* destination = nullptr;
		for (QLabel* label : window->findChildren<QLabel*>())
		{
			if (label->text().startsWith(QStringLiteral("Pans into"))) { destination = label; }
		}
		QVERIFY(destination != nullptr);
		QCOMPARE(destination->text(), QStringLiteral("Pans into %1 (5.1).").arg(m_channels[3]));
		QVERIFY(run(QStringLiteral("mixer.route_remove"), {{QStringLiteral("channel"), m_channels[1]}, {QStringLiteral("to"), m_channels[3]}}).ok);
		QVERIFY(run(QStringLiteral("mixer.send_to"), {{QStringLiteral("channel"), m_channels[1]}, {QStringLiteral("to"), m_channels[2]},
			{QStringLiteral("amount"), 1.0}}).ok);
		auto* refresh = window->findChild<QTimer*>(QStringLiteral("surroundPannerRefresh"));
		if (refresh == nullptr)
		{
			window->close();
			QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
			run(QStringLiteral("mixer.route_remove"), {{QStringLiteral("channel"), m_channels[1]}, {QStringLiteral("to"), m_channels[2]}});
			QFAIL("the panner has no route refresh hook");
		}
		QVERIFY(QMetaObject::invokeMethod(refresh, "timeout", Qt::DirectConnection));
		QCOMPARE(destination->text(), QStringLiteral("Pans into %1 (7.1).").arg(m_channels[2]));
		QWidget* pad = childNamed<QWidget>(window, QStringLiteral("Surround position"));
		QVERIFY(pad != nullptr);
		float speakerX = 0.f;
		float speakerY = 0.f;
		surround::speakerPoint(surround::Format::Surround71, 6, &speakerX, &speakerY);
		QCOMPARE(padPixelAtRoom(pad, speakerX, speakerY), tokens::palette().panel);
		QVERIFY(run(QStringLiteral("mixer.channel_format"), {{QStringLiteral("channel"), m_channels[2]}, {QStringLiteral("format"), QStringLiteral("5.1")}}).ok);
		QVERIFY(QMetaObject::invokeMethod(refresh, "timeout", Qt::DirectConnection));
		QCOMPARE(destination->text(), QStringLiteral("Pans into %1 (5.1).").arg(m_channels[2]));
		QVERIFY(padPixelAtRoom(pad, speakerX, speakerY) != tokens::palette().panel);
		window->close();
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		QVERIFY(run(QStringLiteral("mixer.route_remove"), {{QStringLiteral("channel"), m_channels[1]}, {QStringLiteral("to"), m_channels[2]}}).ok);
		QVERIFY(run(QStringLiteral("mixer.channel_format"), {{QStringLiteral("channel"), m_channels[2]}, {QStringLiteral("format"), QStringLiteral("stereo")}}).ok);
		QVERIFY(run(QStringLiteral("mixer.channel_format"), {{QStringLiteral("channel"), m_channels[3]}, {QStringLiteral("format"), QStringLiteral("stereo")}}).ok);
	}

	void rightClickDoesNotMoveOrEnableThePanner()
	{
		QVERIFY(run(QStringLiteral("mixer.surround_pan"), {{QStringLiteral("channel"), m_channels[2]}, {QStringLiteral("x"), -0.25},
			{QStringLiteral("y"), -0.4}, {QStringLiteral("lfe"), 0.2}, {QStringLiteral("spread"), 0.3},
			{QStringLiteral("enabled"), false}}).ok);
		showSurroundPanner(m_channels[2]);
		QWidget* window = pannerWindow();
		QVERIFY(window != nullptr);
		auto* pad = childNamed<QWidget>(window, QStringLiteral("Surround position"));
		QVERIFY(pad != nullptr);
		const double side = std::min(pad->width(), pad->height()) - 56.0;
		const QPoint target(pad->width() / 2 + static_cast<int>(side / 4), pad->height() / 2 - static_cast<int>(side / 4));
		QTest::mouseClick(pad, Qt::RightButton, Qt::NoModifier, target);
		const QJsonObject pan = panState(m_channels[2]);
		const bool stayedUnchanged = std::abs(pan.value(QStringLiteral("x")).toDouble() + 0.25) < 0.01
			&& std::abs(pan.value(QStringLiteral("y")).toDouble() + 0.4) < 0.01 && !pan.value(QStringLiteral("enabled")).toBool();
		window->close();
		QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
		QVERIFY2(stayedUnchanged, "right-click moved the channel or enabled its panner");
	}

private:
	QTemporaryDir m_home;
	QString m_channels[4];
};

QTEST_MAIN(SurroundPannerTest)
#include "SurroundPannerTest.moc"
