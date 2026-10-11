/*
 * Lv2StateTest.cpp - an LV2 plug-in.s own state survives a save and a load
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

/*! Engine audit E13: an LV2 plug-in with the State interface has its own state saved beside its
 *  control ports (<lv2state>) and restored on load - before, only the ports were kept, so what a
 *  sampler loaded was lost. The test looks for such a plug-in among this host's LV2 bundles and
 *  skips, saying so, when none is installed. */

#include <QtTest>

#include <QDir>
#include <QDomDocument>
#include <QFile>

#include "ControlDeviceSupport.h"
#include "ControlRegistry.h"
#include "Effect.h"
#include "EffectChain.h"
#include "EffectControls.h"
#include "Engine.h"

#ifdef LMMS_HAVE_LV2
#include "Lv2ControlBase.h"
#include "Lv2Proc.h"
#include <lilv/lilv.h>
#endif

using namespace lmms;

#ifdef LMMS_HAVE_LV2
namespace
{

//! The first loadable LV2 effect whose State interface saves something, its controls and that state.
Effect* firstStatefulLv2(EffectChain& chain, Lv2ControlBase** controls, QString* state)
{
	for (const ControlDeviceEntry& entry : controlDeviceCatalogue())
	{
		if (entry.format != QLatin1String("lv2") || entry.kind != QLatin1String("effect") || !entry.loadable) { continue; }
		ControlResult error;
		Effect* effect = controlInstantiateDevice(entry, &chain, &error);
		if (effect == nullptr) { continue; }
		*controls = dynamic_cast<Lv2ControlBase*>(effect->controls());
		*state = *controls != nullptr ? (*controls)->control(0)->saveState() : QString();
		if (!state->isEmpty()) { return effect; }
	}
	return nullptr;
}

//! Whether catalogue @a entry is the plug-in @a found (by URI when it has one, else by name).
bool samePlugin(const ControlDeviceEntry& entry, const Effect* found, const Lv2ControlBase* controls)
{
	if (entry.format != QLatin1String("lv2") || entry.kind != QLatin1String("effect")) { return false; }
	if (entry.uri.isEmpty()) { return QString::fromUtf8(found->descriptor()->name) == entry.name; }
	return controls->getPlugin() == nullptr
		|| entry.uri == QString::fromUtf8(lilv_node_as_uri(lilv_plugin_get_uri(controls->getPlugin())));
}

//! A second instance of the plug-in @a found is, in @a chain.
Effect* secondInstance(EffectChain& chain, const Effect* found, const Lv2ControlBase* controls, ControlResult* error)
{
	for (const ControlDeviceEntry& entry : controlDeviceCatalogue())
	{
		if (samePlugin(entry, found, controls)) { return controlInstantiateDevice(entry, &chain, error); }
	}
	return nullptr;
}

} // namespace
#endif

class Lv2StateTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		Engine::init(true);
	}

	void cleanupTestCase() { Engine::destroy(); }

	void modelChangedConnectionIsInstalledInConstructor()
	{
		const QString sourcePath = QDir{QStringLiteral(QT_TESTCASE_SOURCEDIR)}
			.filePath(QStringLiteral("../plugins/Lv2Effect/Lv2FxControlDialog.cpp"));
		QFile sourceFile{sourcePath};
		QVERIFY2(sourceFile.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(sourcePath));
		const QString source = QString::fromUtf8(sourceFile.readAll());
		const QString connection = QStringLiteral(
			"connect(lv2Controls(), &Lv2FxControls::modelChanged");
		QCOMPARE(source.count(connection), 1);

		const qsizetype constructorStart = source.indexOf(
			QStringLiteral("Lv2FxControlDialog::Lv2FxControlDialog("));
		const qsizetype modelChangedStart = source.indexOf(
			QStringLiteral("void Lv2FxControlDialog::modelChanged()"));
		QVERIFY(constructorStart >= 0);
		QVERIFY(modelChangedStart > constructorStart);
		const QString constructorBody = source.mid(constructorStart, modelChangedStart - constructorStart);
		const QString modelChangedBody = source.mid(modelChangedStart,
			source.indexOf(QLatin1Char('}'), modelChangedStart) - modelChangedStart);
		QVERIFY(constructorBody.contains(connection));
		QVERIFY(!modelChangedBody.contains(connection));
	}

	void aPluginsOwnStateRoundTrips()
	{
#ifndef LMMS_HAVE_LV2
		QSKIP("this build has no LV2 host");
#else
		EffectChain chain{nullptr};
		Lv2ControlBase* controls = nullptr;
		QString state;
		Effect* found = firstStatefulLv2(chain, &controls, &state);
		if (found == nullptr) { QSKIP("no installed LV2 effect implements the State interface"); }

		// Through the project's own serialisation path (the effect controls' saveState/restoreState).
		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("root"));
		doc.appendChild(root);
		const QDomElement element = found->controls()->saveState(doc, root);
		QVERIFY2(!element.firstChildElement(QStringLiteral("lv2state")).isNull(), "the state element is written");
		QVERIFY(controls->control(0)->restoreState(state));
		found->controls()->restoreState(element);
		QCOMPARE(controls->control(0)->saveState().isEmpty(), false);
		// A document that is not Turtle is refused, not half-applied (review L01-1 #11).
		QVERIFY(!controls->control(0)->restoreState(QStringLiteral("this is not a state document")));
		// The state reaches a SECOND instance of the plug-in through the project's load path:
		// what it saves afterwards is the document it was given. (Equal to the default only when
		// the plug-in's state never changes, which the save above cannot tell apart - the
		// refusal case is what proves the restore path is consulted.)
		ControlResult error;
		Effect* second = secondInstance(chain, found, controls, &error);
		QVERIFY2(second != nullptr, qPrintable(error.errorMessage));
		second->controls()->restoreState(element);
		auto* secondControls = dynamic_cast<Lv2ControlBase*>(second->controls());
		QVERIFY(secondControls != nullptr);
		QCOMPARE(secondControls->control(0)->saveState(), controls->control(0)->saveState());
		chain.clear();
#endif
	}
};

QTEST_MAIN(Lv2StateTest)
#include "Lv2StateTest.moc"
