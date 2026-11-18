/*
 * Lv2ViewBaseLifetimeTest.cpp - LV2 help windows follow their owning view
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

#include <QPointer>
#include <QMdiSubWindow>
#include <memory>

#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "MainWindow.h"
#include "Model.h"

#include "lmmsconfig.h"

#ifdef LMMS_HAVE_LV2
#include "Lv2ControlBase.h"
#include "Lv2Manager.h"
#include "Lv2ViewBase.h"
#endif

using namespace lmms;
using namespace lmms::gui;

#ifdef LMMS_HAVE_LV2
namespace
{

QString pluginWithHelpText(Lv2Manager& manager)
{
	AutoLilvNode commentUri = manager.uri(LILV_NS_RDFS "comment");
	for (auto iterator = manager.begin(); iterator != manager.end(); ++iterator)
	{
		const auto& info = iterator->second;
		if (!info.isValid() || info.type() != Plugin::Type::Effect) { continue; }

		AutoLilvNodes comments(lilv_plugin_get_value(info.plugin(), commentUri.get()));
		if (comments.get() != nullptr && lilv_nodes_get_first(comments.get()) != nullptr)
		{
			return QString::fromStdString(iterator->first);
		}
	}
	return {};
}


class TestLv2Control final : public Lv2ControlBase
{
public:
	TestLv2Control(Model* parent, const QString& uri) : Lv2ControlBase(parent, uri) {}
};


class TestLv2View final : public QWidget, public Lv2ViewBase
{
public:
	explicit TestLv2View(Lv2ControlBase* controls) : QWidget(), Lv2ViewBase(this, controls) {}
};

} // namespace
#endif

class Lv2ViewBaseLifetimeTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		guitest::startGui(m_home);
		QVERIFY(getGUI() != nullptr);
	}

	void cleanupTestCase() { Engine::destroy(); }

	void helpWindowIsDestroyedWithItsView()
	{
#ifndef LMMS_HAVE_LV2
		QSKIP("this build has no LV2 host");
#else
		Lv2Manager* manager = Engine::getLv2Manager();
		QVERIFY(manager != nullptr);
		const QString pluginUri = pluginWithHelpText(*manager);
		if (pluginUri.isEmpty()) { QSKIP("no loadable LV2 effect with help text is installed"); }

		Model parent(nullptr);
		TestLv2Control controls(&parent, pluginUri);
		MainWindow* mainWindow = getGUI()->mainWindow();
		const QList<QMdiSubWindow*> before = mainWindow->findChildren<QMdiSubWindow*>();
		auto view = std::make_unique<TestLv2View>(&controls);

		QPointer<QMdiSubWindow> helpWindow;
		for (QMdiSubWindow* window : mainWindow->findChildren<QMdiSubWindow*>())
		{
			if (!before.contains(window)) { helpWindow = window; break; }
		}
		QVERIFY(helpWindow != nullptr);
		QVERIFY(!helpWindow->isVisible());

		view.reset();
		QVERIFY(helpWindow.isNull());
#endif
	}

private:
	QTemporaryDir m_home;
};

QTEST_MAIN(Lv2ViewBaseLifetimeTest)
#include "Lv2ViewBaseLifetimeTest.moc"
