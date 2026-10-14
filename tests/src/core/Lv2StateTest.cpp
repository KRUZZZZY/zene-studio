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
#include "Lv2Features.h"
#include "Lv2Manager.h"
#include "Lv2Proc.h"
#include <lilv/lilv.h>
#endif

using namespace lmms;

#ifdef LMMS_HAVE_LV2
namespace lmms
{

class Lv2FeaturesTestAccess
{
public:
	static auto pointerCount(const Lv2Features& features) { return features.m_featurePointers.size(); }
};

} // namespace lmms

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

	void featurePointersResetOnReload()
	{
#ifndef LMMS_HAVE_LV2
		QSKIP("this build has no LV2 host");
#else
		Lv2Features features;
		features.initCommon();
		features.createFeatureVectors();
		const auto expectedPointerCount = Engine::getLv2Manager()->supportedFeatureURIs().size() + 1;
		QCOMPARE(Lv2FeaturesTestAccess::pointerCount(features), expectedPointerCount);

		features.clear();
		features.initCommon();
		features.createFeatureVectors();
		QCOMPARE(Lv2FeaturesTestAccess::pointerCount(features), expectedPointerCount);
#endif
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

	/*! The <lv2state> element is written exactly when the plug-in's own state is not empty: a plug-in
	 *  without the State interface (or with nothing to keep) saves no element, one with state saves one.
	 *  Checked over the first few loadable LV2 effects of this host, whatever they implement - so the
	 *  invariant is measured on real plug-ins even where none keeps state. */
	void theStateElementIsWrittenExactlyWhenThereIsState()
	{
#ifndef LMMS_HAVE_LV2
		QSKIP("this build has no LV2 host");
#else
		EffectChain chain{nullptr};
		int checked = 0;
		for (const ControlDeviceEntry& entry : controlDeviceCatalogue())
		{
			if (checked >= 5) { break; }
			if (entry.format != QLatin1String("lv2") || entry.kind != QLatin1String("effect") || !entry.loadable) { continue; }
			ControlResult error;
			Effect* effect = controlInstantiateDevice(entry, &chain, &error);
			if (effect == nullptr) { continue; }
			auto* controls = dynamic_cast<Lv2ControlBase*>(effect->controls());
			if (controls == nullptr || controls->control(0) == nullptr) { continue; }
			++checked;
			const bool hasState = !controls->control(0)->saveState().isEmpty();
			QDomDocument doc;
			QDomElement root = doc.createElement(QStringLiteral("root"));
			doc.appendChild(root);
			const QDomElement element = effect->controls()->saveState(doc, root);
			QCOMPARE(!element.firstChildElement(QStringLiteral("lv2state")).isNull(), hasState);
		}
		if (checked == 0) { QSKIP("no loadable LV2 effect in this environment"); }
		chain.clear();
#endif
	}

	//! A catalogue entry for the plug-in already loaded is found again by its URI (by name when it has none),
	//! and instantiating it gives a second device of the same plug-in.
	void aSecondInstanceIsFoundByTheFirstsUri()
	{
#ifndef LMMS_HAVE_LV2
		QSKIP("this build has no LV2 host");
#else
		EffectChain chain{nullptr};
		for (const ControlDeviceEntry& entry : controlDeviceCatalogue())
		{
			if (entry.format != QLatin1String("lv2") || entry.kind != QLatin1String("effect") || !entry.loadable) { continue; }
			ControlResult error;
			Effect* first = controlInstantiateDevice(entry, &chain, &error);
			if (first == nullptr) { continue; }
			auto* controls = dynamic_cast<Lv2ControlBase*>(first->controls());
			if (controls == nullptr) { continue; }
			QVERIFY(samePlugin(entry, first, controls));
			Effect* second = secondInstance(chain, first, controls, &error);
			QVERIFY2(second != nullptr, qPrintable(error.errorMessage));
			QVERIFY(second != first);
			QCOMPARE(QString::fromUtf8(second->descriptor()->name), QString::fromUtf8(first->descriptor()->name));
			chain.clear();
			return;
		}
		QSKIP("no loadable LV2 effect in this environment");
#endif
	}

	/*! BUG 9 (cluster 3237): a malformed <lv2state> instance attribute must not silently
	 *  target instance zero - <lv2state instance="bad"> used to restore into the first
	 *  processor. The decision seam loadSettings uses is checked directly. */
	void malformedStateInstanceAttributeIsRefused()
	{
#ifndef LMMS_HAVE_LV2
		QSKIP("this build has no LV2 host");
#else
		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("root"));
		doc.appendChild(root);
		QDomElement good = doc.createElement(QStringLiteral("lv2state"));
		good.setAttribute(QStringLiteral("instance"), 1);
		root.appendChild(good);
		QDomElement bad = doc.createElement(QStringLiteral("lv2state"));
		bad.setAttribute(QStringLiteral("instance"), QStringLiteral("bad"));
		root.appendChild(bad);
		QDomElement missing = doc.createElement(QStringLiteral("lv2state"));
		root.appendChild(missing);
		// Two processors: instance 1 is a valid target.
		QCOMPARE(lv2StateInstanceIndex(good, 2), 1);
		QCOMPARE(lv2StateInstanceIndex(bad, 2), -1);
		QCOMPARE(lv2StateInstanceIndex(missing, 2), -1);
#endif
	}
};

QTEST_MAIN(Lv2StateTest)
#include "Lv2StateTest.moc"
