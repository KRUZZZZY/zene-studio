/*
 * VstEffectControlsTest.cpp - parameter-model lifetime regression coverage
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

#include "../../../plugins/VstEffect/VstEffectControls.h"

#include <QtTest>

#include "Engine.h"

namespace lmms
{

class VstEffectControlsTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase();
	void cleanupTestCase();
	void repeatedParameterLoadsDoNotRetainOldModels();
};

void VstEffectControlsTest::initTestCase()
{
	Engine::init(true);
	Engine::audioEngine()->audioDev()->stopProcessing();
}

void VstEffectControlsTest::cleanupTestCase()
{
	Engine::destroy();
}

void VstEffectControlsTest::repeatedParameterLoadsDoNotRetainOldModels()
{
	FloatModel parent;
	std::vector<FloatModel*> models;
	auto createModel = [&parent](int index) {
		return new FloatModel(0.0f, 0.0f, 1.0f, 0.01f, &parent, QString::number(index));
	};

	vstEffectControlDetail::replaceParameterModels(models, 2, createModel);
	QCOMPARE(parent.findChildren<FloatModel*>().size(), 2);

	vstEffectControlDetail::replaceParameterModels(models, 2, createModel);
	QCOMPARE(models.size(), std::size_t{2});
	QCOMPARE(parent.findChildren<FloatModel*>().size(), 2);
}

} // namespace lmms

QTEST_GUILESS_MAIN(lmms::VstEffectControlsTest)

#include "VstEffectControlsTest.moc"
