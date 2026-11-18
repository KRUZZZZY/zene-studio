/*
 * CarlaParamFloatModelTest.cpp - Carla parameter model defaults
 *
 * This file is part of Zene Studio.
 */

#include <array>
#include <cstddef>
#include <cstring>
#include <new>

#include <QFile>
#include <QRegularExpression>
#include <QtTest>

#include "Carla.h"
#include "Engine.h"

using namespace lmms;

class CarlaParamFloatModelTest final : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	void groupIdDefaultsToZero()
	{
		alignas(CarlaParamFloatModel) std::array<std::byte, sizeof(CarlaParamFloatModel)> storage;
		std::memset(storage.data(), 0xa5, storage.size());

		auto* model = ::new (storage.data()) CarlaParamFloatModel(nullptr);
		const auto groupId = model->groupId();
		model->~CarlaParamFloatModel();

		QCOMPARE(groupId, uint8_t{0});
	}

	void groupIdHasZeroDefaultInitializer()
	{
		QFile header(CARLA_HEADER_PATH);
		QVERIFY(header.open(QIODevice::ReadOnly | QIODevice::Text));
		const auto source = QString::fromUtf8(header.readAll());
		const QRegularExpression initializer(R"(uint8_t\s+m_groupId\s*\{\s*\})");
		QVERIFY(initializer.match(source).hasMatch());
	}
};

QTEST_GUILESS_MAIN(CarlaParamFloatModelTest)

#include "CarlaParamFloatModelTest.moc"
