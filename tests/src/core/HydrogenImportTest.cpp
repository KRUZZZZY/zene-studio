#include "../../../plugins/HydrogenImport/LocalFileMng.h"

#include <QDomDocument>
#include <QtTest/QtTest>

using lmms::LocalFileMng;

class HydrogenImportTest : public QObject
{
	Q_OBJECT

private slots:
	void invalidFloatUsesDefault()
	{
		QDomDocument document;
		QVERIFY(document.setContent(QStringLiteral("<song><bpm>oops</bpm></song>")));

		QCOMPARE(LocalFileMng::readXmlFloat(document.documentElement(), "bpm", 120.0f), 120.0f);
	}

	void invalidIntUsesDefault()
	{
		QDomDocument document;
		QVERIFY(document.setContent(QStringLiteral("<song><size>oops</size></song>")));

		QCOMPARE(LocalFileMng::readXmlInt(document.documentElement(), "size", 16), 16);
	}

	void invalidBooleanUsesDefault()
	{
		QDomDocument document;
		QVERIFY(document.setContent(QStringLiteral("<song><enabled>maybe</enabled></song>")));

		QCOMPARE(LocalFileMng::readXmlBool(document.documentElement(), "enabled", true), true);
	}

	void recognizedBooleansArePreserved()
	{
		QDomDocument document;
		QVERIFY(document.setContent(QStringLiteral("<song><enabled>true</enabled><disabled>false</disabled></song>")));
		const auto root = document.documentElement();

		QCOMPARE(LocalFileMng::readXmlBool(root, "enabled", false), true);
		QCOMPARE(LocalFileMng::readXmlBool(root, "disabled", true), false);
	}
};

QTEST_GUILESS_MAIN(HydrogenImportTest)
#include "HydrogenImportTest.moc"
