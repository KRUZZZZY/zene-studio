/*
 * ScriptDawModelRegistrationTest.cpp - recorded device model handoff and
 * audio-change-lock regressions from review-51e7da51f.
 *
 * These cases run on a real engine and native effect modules. Registration
 * failures and lock ownership are injected with deterministic test seams.
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

#include <QtTest>
#include <QScopeGuard>
#include <QStringList>

#include <future>
#include <new>
#include <thread>

#include "AudioEngine.h"
#include "AutomatableModel.h"
#include "ControlDeviceSupport.h"
#include "ControlRegistry.h"
#include "ControlStructuralSupport.h"
#include "ControlVocabulary.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "Mixer.h"
#include "ScriptEngine.h"

using namespace lmms;

namespace
{

QString qstr(const char* text) { return QString::fromUtf8(text); }

} // namespace

class ScriptDawModelRegistrationTest : public QObject
{
	Q_OBJECT
private slots:

	void initTestCase()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

private:
	static bool loadableDevice(QString* device, QString* deviceName)
	{
		const QList<ControlDeviceEntry> catalogue = controlDeviceCatalogue();
		for (int i = 0; i < catalogue.size(); ++i)
		{
			if (catalogue.at(i).loadable)
			{
				*device = control::deviceId(i);
				*deviceName = catalogue.at(i).name;
				return true;
			}
		}
		return false;
	}

private slots:

	/*! The load and settings restore must not try model registration while the
	 *  audio change lock is held elsewhere. A deterministic registration fault
	 *  returns before taking the lock, so a regression is observed without a
	 *  timeout or sleep. */
	void aRecordedDeviceIsBuiltWithoutTheAudioChangeLock()
	{
		QString device;
		QString deviceName;
		if (!loadableDevice(&device, &deviceName)) { QSKIP("this build lists no loadable effect module"); }
		Mixer* mixer = Engine::mixer();
		const int index = mixer->createChannel();
		EffectChain* chain = &mixer->mixerChannel(index)->m_fxChain;
		QString error;
		QCOMPARE(int(ScriptEngine::instance()->runString(qstr(
			"zene.mixer():channel(%1):chain():loadEffect(\"%2\")\n").arg(index).arg(device), &error)),
			int(ScriptEngine::RunResult::Ok));
		QVERIFY2(error.isEmpty(), qPrintable(error));
		QCOMPARE(static_cast<int>(chain->effects().size()), 1);
		const QString xml = controlEffectStateXml(chain->effects().front());
		QVERIFY(!xml.isEmpty());

		AudioEngine* audioEngine = Engine::audioEngine();
		std::promise<void> acquiredLock;
		std::promise<void> releaseLock;
		std::future<void> releaseSignal = releaseLock.get_future();
		bool releaseSent = false;
		std::thread holder([&] {
			audioEngine->requestChangeInModel();
			acquiredLock.set_value();
			releaseSignal.wait();
			audioEngine->doneChangeInModel();
		});
		acquiredLock.get_future().wait();
		const auto releaseHolder = qScopeGuard([&] {
			if (!releaseSent) { releaseLock.set_value(); }
			if (holder.joinable()) { holder.join(); }
		});

		const auto registrationFailures = audioEngine->automatableModelRegistrationFailures();
		audioEngine->setAutomatableModelRegistrationFailureForTest(true);
		const auto restoreFailureSetting = qScopeGuard([audioEngine] {
			audioEngine->setAutomatableModelRegistrationFailureForTest(false);
		});
		Effect* built = nullptr;
		try
		{
			built = control::instantiateRecordedDevice(chain, xml);
		}
		catch (const std::bad_alloc&)
		{
			// The injected failure turns the old constructor registration into a
			// deterministic result before it can wait for the held mutex.
		}
		QCOMPARE(audioEngine->automatableModelRegistrationFailures(), registrationFailures);
		QVERIFY2(built != nullptr, "the recorded device could not be built");
		QCOMPARE(QString::fromUtf8(built->descriptor()->name), deviceName);
		releaseLock.set_value();
		releaseSent = true;
		holder.join();
		built->deleteLater();

		mixer->deleteChannel(index);
		ScriptEngine::instance()->takeLogMessages();
	}

	/*! A failed model-registration handoff must leave the fresh device unattached
	 *  and report a structural restore failure. The amplifier's controls provide
	 *  real AutomatableModels, so the injected failure is exercised immediately
	 *  before the restored device would enter the chain. */
	void aRecordedDeviceIsNotAttachedWhenModelRegistrationFails()
	{
		const QList<ControlDeviceEntry> catalogue = controlDeviceCatalogue();
		bool amplifierAvailable = false;
		for (const ControlDeviceEntry& entry : catalogue)
		{
			if (entry.name == QStringLiteral("amplifier") && entry.loadable)
			{
				amplifierAvailable = true;
				break;
			}
		}
		if (!amplifierAvailable) { QSKIP("this build lists no loadable amplifier module"); }

		Mixer* mixer = Engine::mixer();
		const int index = mixer->createChannel();
		EffectChain* chain = &mixer->mixerChannel(index)->m_fxChain;
		Effect* original = Effect::instantiate(QStringLiteral("amplifier"), chain, nullptr);
		QVERIFY(original != nullptr);
		QVERIFY(!original->findChildren<AutomatableModel*>(
			QString(), Qt::FindChildrenRecursively).isEmpty());
		{
			const auto changes = Engine::audioEngine()->requestChangesGuard();
			chain->appendEffect(original);
		}
		const QString xml = controlEffectStateXml(original);
		QVERIFY(!xml.isEmpty());

		control::clearStructuralRestoreFailure();
		AudioEngine* audioEngine = Engine::audioEngine();
		const auto registrationFailures = audioEngine->automatableModelRegistrationFailures();
		audioEngine->setAutomatableModelRegistrationFailureForTest(true);
		const auto restoreFailureSetting = qScopeGuard([audioEngine] {
			audioEngine->setAutomatableModelRegistrationFailureForTest(false);
		});
		Effect* restored = control::recreateEffectFromState(chain, xml, 0);
		QVERIFY(restored == nullptr);
		QCOMPARE(static_cast<int>(chain->effects().size()), 1);
		QCOMPARE(chain->effects().front(), original);
		QCOMPARE(audioEngine->automatableModelRegistrationFailures(), registrationFailures + 1);
		const auto failure = control::takeStructuralRestoreFailure();
		QVERIFY(failure.has_value());
		QCOMPARE(int(failure->errorKind), int(ControlErrorKind::Refused));
		QVERIFY(failure->errorMessage.contains(QStringLiteral("registered")));

		mixer->deleteChannel(index);
		ScriptEngine::instance()->takeLogMessages();
	}


};

QTEST_MAIN(ScriptDawModelRegistrationTest)
#include "ScriptDawModelRegistrationTest.moc"
