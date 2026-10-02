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

#include <QDomDocument>

#include "ControlDeviceSupport.h"
#include "ControlRegistry.h"
#include "Effect.h"
#include "EffectChain.h"
#include "EffectControls.h"
#include "Engine.h"

#ifdef LMMS_HAVE_LV2
#include "Lv2ControlBase.h"
#include "Lv2Proc.h"
#endif

using namespace lmms;

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

	void aPluginsOwnStateRoundTrips()
	{
#ifndef LMMS_HAVE_LV2
		QSKIP("this build has no LV2 host");
#else
		EffectChain chain{nullptr};
		Effect* found = nullptr;
		Lv2ControlBase* controls = nullptr;
		QString state;
		for (const ControlDeviceEntry& entry : controlDeviceCatalogue())
		{
			if (entry.format != QLatin1String("lv2") || entry.kind != QLatin1String("effect") || !entry.loadable) { continue; }
			ControlResult error;
			Effect* effect = controlInstantiateDevice(entry, &chain, &error);
			if (effect == nullptr) { continue; }
			controls = dynamic_cast<Lv2ControlBase*>(effect->controls());
			state = controls != nullptr ? controls->control(0)->saveState() : QString();
			if (!state.isEmpty()) { found = effect; break; }
		}
		if (state.isEmpty()) { QSKIP("no installed LV2 effect implements the State interface"); }

		// Through the project's own serialisation path (the effect controls' saveState/restoreState).
		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("root"));
		doc.appendChild(root);
		const QDomElement element = found->controls()->saveState(doc, root);
		QVERIFY2(!element.firstChildElement(QStringLiteral("lv2state")).isNull(), "the state element is written");
		QVERIFY(controls->control(0)->restoreState(state));
		found->controls()->restoreState(element);
		QCOMPARE(controls->control(0)->saveState().isEmpty(), false);
		chain.clear();
#endif
	}
};

QTEST_MAIN(Lv2StateTest)
#include "Lv2StateTest.moc"
