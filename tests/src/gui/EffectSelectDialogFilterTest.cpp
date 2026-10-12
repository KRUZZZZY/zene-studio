/*
 * EffectSelectDialogFilterTest.cpp - the effect picker's name filter handles favourite decoration
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

#include <QtTest>

#include "EffectSelectDialog.h"

using namespace lmms::gui;

class EffectSelectDialogFilterTest : public QObject
{
	Q_OBJECT

private slots:
	void anchoredSearchFindsFavouriteName()
	{
		QStandardItemModel source;
		auto* name = new QStandardItem(QStringLiteral("★ Zita"));
		name->setData(true, pickerorganiser::FavouriteRole);
		source.setItem(0, 0, name);
		source.setItem(0, 1, new QStandardItem(QStringLiteral("Zene Studio")));

		DualColumnFilterProxyModel proxy;
		proxy.setSourceModel(&source);
		proxy.setFilterRegularExpression(QRegularExpression(QStringLiteral("^Zita")));

		QCOMPARE(proxy.rowCount(), 1);
	}
};

QTEST_GUILESS_MAIN(EffectSelectDialogFilterTest)
#include "EffectSelectDialogFilterTest.moc"
