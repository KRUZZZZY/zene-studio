/*
 * LinkedModelGroupsTest.cpp - a LinkedModelGroup erases only the entry that
 * registers the model it is told about
 *
 * Both the destroyed-model callback and removeControl erase by name. A model
 * removed earlier can be destroyed after a replacement took its name, so each
 * erase must check that the entry still points at the model in question.
 * Everything here is synchronous: no timing, threads or audio engine.
 */


#include <QtTest>

#include "AutomatableModel.h"
#include "Engine.h"
#include "LinkedModelGroups.h"

using namespace lmms;

class LinkedModelGroupsTest : public QObject
{
	Q_OBJECT
private slots:
	void initTestCase() { Engine::init(true); }
	void cleanupTestCase() { Engine::destroy(); }

	//! Normal path: removing a model emits once; destroying it later emits nothing
	void removedModelDestroyedLaterEmitsNothing();

	//! Normal path: destroying a still-registered model erases its entry and emits once
	void destroyingRegisteredModelErasesIt();

	//! Bug: a model removed earlier, destroyed after a same-named replacement was
	//! registered, must not emit a removal or erase the replacement
	void staleDestroyKeepsSameNamedReplacement();

	//! The replacement still erases its own entry when it is destroyed
	void replacementCleansUpItsOwnEntry();

	//! Sibling path: removeControl must not erase a registered entry on behalf of
	//! a different model that merely shares its name
	void removeControlIgnoresAnotherModelWithSameName();

	//! A duplicate name is never registered, so destroying it must not erase the
	//! entry of the model that owns the name
	void destroyingUnregisteredDuplicateKeepsRegisteredModel();

	//! The entry is erased under the name it was registered with, even if the
	//! model was renamed afterwards (otherwise the group keeps a dangling pointer)
	void renamedModelIsErasedUnderRegisteredName();
};


namespace
{

//! Records every modelRemoved emission, in order
struct RemovalLog
{
	QList<AutomatableModel*> removed;
	void attach(LinkedModelGroup& group)
	{
		QObject::connect(&group, &LinkedModelGroup::modelRemoved,
			[this](AutomatableModel* m){ removed.append(m); });
	}
};

AutomatableModel* newModel()
{
	return new FloatModel(0.f, 0.f, 1.f, 0.1f);
}

} // namespace


void LinkedModelGroupsTest::removedModelDestroyedLaterEmitsNothing()
{
	LinkedModelGroup group(nullptr);
	RemovalLog log;
	log.attach(group);

	AutomatableModel* model = newModel();
	group.addModel(model, "x");
	group.removeControl(model);

	QCOMPARE(log.removed.size(), 1);
	QVERIFY(log.removed.first() == model);
	QVERIFY(!group.containsModel("x"));

	delete model;

	QCOMPARE(log.removed.size(), 1);
	QCOMPARE(group.modelNum(), std::size_t{0});
}


void LinkedModelGroupsTest::destroyingRegisteredModelErasesIt()
{
	LinkedModelGroup group(nullptr);
	RemovalLog log;
	log.attach(group);

	AutomatableModel* model = newModel();
	group.addModel(model, "x");
	QVERIFY(group.getModel("x") == model);

	delete model;

	QCOMPARE(log.removed.size(), 1);
	QCOMPARE(group.modelNum(), std::size_t{0});
	QVERIFY(!group.containsModel("x"));
}


void LinkedModelGroupsTest::staleDestroyKeepsSameNamedReplacement()
{
	LinkedModelGroup group(nullptr);
	RemovalLog log;
	log.attach(group);

	AutomatableModel* stale = newModel();
	group.addModel(stale, "x");
	group.removeControl(stale);

	AutomatableModel* replacement = newModel();
	group.addModel(replacement, "x");
	QVERIFY(group.getModel("x") == replacement);

	delete stale;

	// only the explicit removal above; the destroy must not report the stale model again
	QCOMPARE(log.removed.size(), 1);
	QVERIFY(group.getModel("x") == replacement);
	QCOMPARE(group.modelNum(), std::size_t{1});
}


void LinkedModelGroupsTest::replacementCleansUpItsOwnEntry()
{
	LinkedModelGroup group(nullptr);
	RemovalLog log;
	log.attach(group);

	AutomatableModel* stale = newModel();
	group.addModel(stale, "x");
	group.removeControl(stale);
	delete stale;

	AutomatableModel* replacement = newModel();
	group.addModel(replacement, "x");

	delete replacement;

	QCOMPARE(log.removed.size(), 2);
	QVERIFY(log.removed.last() == replacement);
	QCOMPARE(group.modelNum(), std::size_t{0});
}


void LinkedModelGroupsTest::removeControlIgnoresAnotherModelWithSameName()
{
	LinkedModelGroup group(nullptr);
	RemovalLog log;
	log.attach(group);

	AutomatableModel* registered = newModel();
	group.addModel(registered, "x");

	FloatModel stranger(0.f, 0.f, 1.f, 0.1f);
	stranger.setObjectName("x");
	group.removeControl(&stranger);

	QVERIFY(group.getModel("x") == registered);
	QCOMPARE(log.removed.size(), 0);
	QCOMPARE(group.modelNum(), std::size_t{1});

	delete registered;
}


void LinkedModelGroupsTest::destroyingUnregisteredDuplicateKeepsRegisteredModel()
{
	LinkedModelGroup group(nullptr);
	RemovalLog log;
	log.attach(group);

	AutomatableModel* first = newModel();
	group.addModel(first, "x");

	AutomatableModel* duplicate = newModel();
	group.addModel(duplicate, "x");
	QVERIFY(group.getModel("x") == first);

	delete duplicate;

	QVERIFY(group.getModel("x") == first);
	QCOMPARE(log.removed.size(), 0);
	QCOMPARE(group.modelNum(), std::size_t{1});

	delete first;
}


void LinkedModelGroupsTest::renamedModelIsErasedUnderRegisteredName()
{
	LinkedModelGroup group(nullptr);
	RemovalLog log;
	log.attach(group);

	AutomatableModel* model = newModel();
	group.addModel(model, "x");
	model->setObjectName("y");

	delete model;

	QCOMPARE(log.removed.size(), 1);
	QCOMPARE(group.modelNum(), std::size_t{0});
}


QTEST_GUILESS_MAIN(LinkedModelGroupsTest)
#include "LinkedModelGroupsTest.moc"
