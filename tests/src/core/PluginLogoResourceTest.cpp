/*
 * PluginLogoResourceTest.cpp - the built-in plugin logo must actually LOAD.
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of Zene Studio, a derivative work of LMMS (https://lmms.io).
 * It is free software; you can redistribute it and/or modify it under the terms
 * of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 *
 * Why this test exists.  `embed::loadSvgPixmap()` resolves a resource name by
 * appending ".svg" and opening it through the artwork search path.  When the
 * file is not there it does `qWarning() << "Failed to open resource for SVG: "`
 * and returns a 1x1 transparent QPixmap -- no exception, no non-zero exit, no
 * failed build.  So a renamed resource file whose call sites still pass the old
 * name renders every plugin dialog with a blank one-pixel image and the only
 * signal anywhere is a warning line.
 *
 * The rename of the plugin logo resource is exactly that hazard, so it is pinned
 * by loading the pixmap the way the product does rather than by checking that a
 * file exists on disk.
 */

#include <QDir>
#include <QImage>
#include <QObject>
#include <QPixmap>
#include <QtTest>

#include <array>
#include <utility>

#include "AutomatableModelView.h"
#include "CaptionMenu.h"
#include "ConfigManager.h"
#include "Engine.h"
#include "embed.h"
#include "PixmapButton.h"

// Exercise the production constructor in this configured GUI test. The
// selector's generated metaobject belongs to its plugin target; construction
// does not use its signal, so suppress Q_OBJECT and stub that unused signal.
#pragma push_macro("Q_OBJECT")
#undef Q_OBJECT
#define Q_OBJECT
#include "../../../plugins/Vibed/NineButtonSelector.cpp"
#pragma pop_macro("Q_OBJECT")

namespace lmms::gui
{

void NineButtonSelector::NineButtonSelection(int)
{
}

} // namespace lmms::gui

namespace
{

class EngineScope
{
public:
	EngineScope() { lmms::Engine::init(true); }
	~EngineScope() { lmms::Engine::destroy(); }
};

} // namespace

using namespace lmms;

class PluginLogoResourceTest : public QObject
{
	Q_OBJECT

private slots:

	void initTestCase()
	{
		// The same artwork search path the running product registers
		// (src/gui/GuiApplication.cpp), so this resolves exactly as the GUI does.
		QDir::addSearchPath("artwork", ConfigManager::inst()->themeDir());
		QDir::addSearchPath("artwork", ConfigManager::inst()->defaultThemeDir());
		QDir::addSearchPath("artwork", ":/artwork");
	}

	// The renamed resource resolves to real art.
	void pluginLogoLoads()
	{
		const QPixmap logo = embed::getIconPixmap("zene-plugin-logo");
		QVERIFY2( !logo.isNull(), "the plugin logo pixmap must not be null" );
		QVERIFY2( logo.width() > 1 && logo.height() > 1,
			qPrintable( QString( "the plugin logo is the %1x%2 fallback pixmap: the "
					     "resource did not resolve" )
					.arg( logo.width() ).arg( logo.height() ) ) );

		const QImage img = logo.toImage();
		qInfo().noquote() << "plugin logo 'zene-plugin-logo' resolved to"
			<< logo.width() << "x" << logo.height() << "px from the artwork search path";
		bool anyVisiblePixel = false;
		for( int y = 0; y < img.height() && !anyVisiblePixel; ++y )
		{
			for( int x = 0; x < img.width(); ++x )
			{
				if( qAlpha( img.pixel( x, y ) ) > 0 )
				{
					anyVisiblePixel = true;
					break;
				}
			}
		}
		QVERIFY2( anyVisiblePixel,
			"the plugin logo must contain visible pixels rather than be blank" );
	}

	// ...and the assertion above is not vacuous: a name with nothing behind it
	// IS the 1x1 fallback, which is the state a stale call site would produce.
	void aMissingResourceIsTheOnePixelFallback()
	{
		const QPixmap missing = embed::getIconPixmap( "zene-plugin-logo-does-not-exist" );
		QCOMPARE( missing.size(), QSize( 1, 1 ) );
		QCOMPARE( missing.width(), 1 );
	}

	void invalidDefaultSelectsNearestButton()
	{
		EngineScope engine;
		const std::array<QPixmap, 18> icons{};
		for (const auto& [defaultButton, expectedButton] : {std::pair{-1, 0}, std::pair{9, 8}})
		{
			gui::NineButtonSelector selector{icons, defaultButton, 0, 0, nullptr};
			const auto buttons = selector.findChildren<gui::PixmapButton*>();
			QCOMPARE(buttons.size(), 9);

			int selectedCount = 0;
			bool expectedButtonSelected = false;
			for (const auto* button : buttons)
			{
				if (!static_cast<const BoolModel*>(button->model())->value())
				{
					continue;
				}

				++selectedCount;
				expectedButtonSelected = button->pos() == QPoint{
					1 + (expectedButton % 3) * 17,
					1 + (expectedButton / 3) * 17};
			}

			QCOMPARE(selectedCount, 1);
			QVERIFY(expectedButtonSelected);
		}
	}
};

QTEST_MAIN( PluginLogoResourceTest )
#include "PluginLogoResourceTest.moc"
