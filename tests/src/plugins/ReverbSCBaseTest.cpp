#include <QtTest>

extern "C"
{
#include "base.h"
}

class ReverbSCBaseTest : public QObject
{
	Q_OBJECT

private slots:
	void createAndDestroyReturnSpOk()
	{
		sp_data* multiChannel = nullptr;
		const auto multiCreateStatus = sp_createn(&multiChannel, 2);
		QVERIFY(multiChannel != nullptr);
		QCOMPARE(multiChannel->nchan, 2);
		QVERIFY(multiChannel->out != nullptr);
		const auto multiDestroyStatus = sp_destroy(&multiChannel);

		sp_data* singleChannel = nullptr;
		const auto singleCreateStatus = sp_create(&singleChannel);
		QVERIFY(singleChannel != nullptr);
		QCOMPARE(singleChannel->nchan, 1);
		const auto singleDestroyStatus = sp_destroy(&singleChannel);

		QCOMPARE(multiCreateStatus, SP_OK);
		QCOMPARE(multiDestroyStatus, SP_OK);
		QCOMPARE(singleCreateStatus, SP_OK);
		QCOMPARE(singleDestroyStatus, SP_OK);
	}
};

QTEST_GUILESS_MAIN(ReverbSCBaseTest)
#include "ReverbSCBaseTest.moc"
