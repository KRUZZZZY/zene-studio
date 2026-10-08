/*
 * TelemetryConsentDialogTest.cpp - the telemetry consent window: what it shows, previews and saves
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

/*! Telemetry is off until a person turns it on, and the window must show exactly what would be sent. Held here: the
 *  window opens on the stored consent (off on a fresh install), every tick re-renders the payload preview from the
 *  same builder submit() uses and the status line says whether anything can be sent, Save stores the consent with
 *  the notice's version (and a withdrawn consent with version 0), and Close stores nothing. */

#include <QtTest>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>

#include "ConfigManager.h"
#include "Telemetry.h"
#include "TelemetryConsentDialog.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

QCheckBox* boxTexted(QWidget* dialog, const QString& start)
{
	for (QCheckBox* box : dialog->findChildren<QCheckBox*>())
	{
		if (box->text().startsWith(start)) { return box; }
	}
	return nullptr;
}

//! The status line: the one label that speaks of telemetry being on, off or built out.
QString statusOf(QWidget* dialog)
{
	for (QLabel* label : dialog->findChildren<QLabel*>())
	{
		if (label->text().startsWith(QStringLiteral("Telemetry is")) || label->text().startsWith(QStringLiteral("This build was packaged"))) { return label->text(); }
	}
	return {};
}

} // namespace

class TelemetryConsentDialogTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		for (const char* name : {"HOME", "XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME"}) { qputenv(name, m_home.path().toLocal8Bit()); }
		ConfigManager::inst()->loadConfigFile(m_home.filePath(QStringLiteral("zene.xml")));
	}

	void init()
	{
		Telemetry::saveConsent(TelemetryConsent{});
	}

	void aFreshInstallOpensOffAndSaysNothingIsSent()
	{
		TelemetryConsentDialog dialog;
		dialog.show();
		QVERIFY(!boxTexted(&dialog, QStringLiteral("Send anonymous"))->isChecked());
		QVERIFY(!boxTexted(&dialog, QStringLiteral("Hardware"))->isChecked());
		QVERIFY(!boxTexted(&dialog, QStringLiteral("Feature usage"))->isChecked());
		QVERIFY(!boxTexted(&dialog, QStringLiteral("Crash counts"))->isChecked());
		if (Telemetry::isCompiledIn()) { QCOMPARE(statusOf(&dialog), QStringLiteral("Telemetry is off. Nothing is sent.")); }
		else { QVERIFY(statusOf(&dialog).startsWith(QStringLiteral("This build was packaged without telemetry"))); }
		auto* preview = dialog.findChild<QPlainTextEdit*>();
		QVERIFY(preview != nullptr && preview->isReadOnly());
		QVERIFY(!QJsonDocument::fromJson(preview->toPlainText().toUtf8()).isNull());  // the preview is the payload, as JSON
	}

	void everyTickRebuildsThePreviewAndTheStatus()
	{
		TelemetryConsentDialog dialog;
		dialog.show();
		auto* preview = dialog.findChild<QPlainTextEdit*>();
		const QString off = preview->toPlainText();
		boxTexted(&dialog, QStringLiteral("Send anonymous"))->setChecked(true);
		boxTexted(&dialog, QStringLiteral("Hardware"))->setChecked(true);
		const QString on = preview->toPlainText();
		QVERIFY2(!QJsonDocument::fromJson(on.toUtf8()).isNull(), "the preview is not the payload's JSON");
		if (Telemetry::isCompiledIn())
		{
			QVERIFY2(on != off, "ticking the hardware box left the payload preview as it was");
			QCOMPARE(statusOf(&dialog), QStringLiteral("Telemetry is on. Turn it off to stop sending, immediately."));
			// Master on but no category: nothing would go, and the line says so.
			boxTexted(&dialog, QStringLiteral("Hardware"))->setChecked(false);
			QCOMPARE(statusOf(&dialog), QStringLiteral("Telemetry is off. Nothing is sent."));
			boxTexted(&dialog, QStringLiteral("Crash counts"))->setChecked(true);
			QCOMPARE(statusOf(&dialog), QStringLiteral("Telemetry is on. Turn it off to stop sending, immediately."));
		}
		// Nothing is stored until Save.
		QVERIFY(!Telemetry::loadConsent().enabled);
	}

	void saveStoresTheConsentWithTheNoticesVersionAndCloseStoresNothing()
	{
		{
			TelemetryConsentDialog dialog;
			dialog.show();
			boxTexted(&dialog, QStringLiteral("Send anonymous"))->setChecked(true);
			boxTexted(&dialog, QStringLiteral("Feature usage"))->setChecked(true);
			dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
			const TelemetryConsent saved = Telemetry::loadConsent();
			QVERIFY(saved.enabled && saved.featureUsage);
			QVERIFY(!saved.hardware && !saved.crashCounts);
			QCOMPARE(saved.consentVersion, Telemetry::ConsentVersion);
			QVERIFY(dialog.isVisible());  // Save keeps the window open
		}
		{
			// Reopened, the window shows what was saved.
			TelemetryConsentDialog dialog;
			QVERIFY(boxTexted(&dialog, QStringLiteral("Send anonymous"))->isChecked());
			QVERIFY(boxTexted(&dialog, QStringLiteral("Feature usage"))->isChecked());
			QVERIFY(!boxTexted(&dialog, QStringLiteral("Hardware"))->isChecked());
			// Withdrawn: Save records version 0, and Close afterwards changes nothing.
			boxTexted(&dialog, QStringLiteral("Send anonymous"))->setChecked(false);
			dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
			QVERIFY(!Telemetry::loadConsent().enabled);
			QCOMPARE(Telemetry::loadConsent().consentVersion, 0);
		}
		TelemetryConsentDialog closing;
		closing.show();
		boxTexted(&closing, QStringLiteral("Crash counts"))->setChecked(true);
		closing.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Close)->click();
		QVERIFY(!closing.isVisible());
		QVERIFY(!Telemetry::loadConsent().crashCounts);
	}

private:
	QTemporaryDir m_home;
};

QTEST_MAIN(TelemetryConsentDialogTest)
#include "TelemetryConsentDialogTest.moc"
