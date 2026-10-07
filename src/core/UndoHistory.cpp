/*
 * UndoHistory.cpp - WF-033: the undo history saved with the project
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

#include "UndoHistory.h"

#include <QCryptographicHash>
#include <QDomDocument>
#include <QDomElement>
#include <QtGlobal>

#include "ConfigManager.h"
#include "DataFile.h"
#include "DocumentIndex.h"
#include "Engine.h"
#include "ProvenanceSection.h"
#include "Song.h"

namespace lmms
{

namespace
{

const QString ConfigClass = QStringLiteral("undohistory");
const QString StepName = QStringLiteral("z:undostep");
constexpr qint64 Megabyte = 1024ll * 1024;

int configInt(const QString& attribute, int fallback, int low, int high)
{
	bool ok = false;
	const int value = ConfigManager::inst()->value(ConfigClass, attribute).toInt(&ok);
	return ok && value >= low && value <= high ? value : fallback;
}

QString sha256Of(const QByteArray& bytes)
{
	return QStringLiteral("sha256:")
		+ QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

//! The writer's own prune (DataFile::cleanMetaNodes), on a detached copy.
void stripMetadataNodes(QDomElement element)
{
	QDomNode node = element.firstChild();
	while (!node.isNull())
	{
		QDomNode next = node.nextSibling();
		if (node.isElement())
		{
			if (node.toElement().attribute(QStringLiteral("metadata")).toInt()) { element.removeChild(node); }
			else { stripMetadataNodes(node.toElement()); }
		}
		node = next;
	}
}

bool outsideFingerprint(const QString& name)
{
	return name == UndoHistory::instance().elementName() || name == provenance::nodeName()
		|| name == documentIndexNodeName();
}

//! One loaded step: undo replaces the project with \a before, after taking the
//! project as it is then - which is exactly what the redo restores.
void pushLoadedStep(ProjectJournal* journal, const ProjectJournal::Snapshot& before)
{
	auto after = std::make_shared<ProjectJournal::Snapshot>();
	journal->pushLoadedStep(
		[before, after] {
			Song* song = Engine::getSong();
			*after = song->captureUndoSnapshot();
			song->restoreUndoSnapshot(*before);
		},
		[after] {
			if (*after) { Engine::getSong()->restoreUndoSnapshot(**after); }
		},
		before);
}

} // namespace


UndoHistory::UndoHistory() = default;

UndoHistory& UndoHistory::instance()
{
	static UndoHistory* history = [] {
		auto* made = new UndoHistory();
		ProjectSection::registerSection(made);
		return made;
	}();
	return *history;
}

void UndoHistory::attach(ProjectJournal* journal)
{
	if (journal != nullptr) { journal->setSnapshotPolicy(this); }
}

// ---- settings ----------------------------------------------------------------

bool UndoHistory::defaultEnabled()
{
	return configInt(QStringLiteral("persist"), 0, 0, 1) == 1;
}

int UndoHistory::maxSteps()
{
	return configInt(QStringLiteral("steps"), DefaultSteps, 1, MaxStepsLimit);
}

qint64 UndoHistory::capBytes()
{
	return configInt(QStringLiteral("capmb"), DefaultCapMb, 1, MaxCapMb) * Megabyte;
}

void UndoHistory::setDefaultEnabled(bool on)
{
	ConfigManager::inst()->setValue(ConfigClass, QStringLiteral("persist"), on ? QStringLiteral("1") : QStringLiteral("0"));
}

bool UndoHistory::setMaxSteps(int steps)
{
	if (steps < 1 || steps > MaxStepsLimit) { return false; }
	ConfigManager::inst()->setValue(ConfigClass, QStringLiteral("steps"), QString::number(steps));
	return true;
}

bool UndoHistory::setCapMb(int megabytes)
{
	if (megabytes < 1 || megabytes > MaxCapMb) { return false; }
	ConfigManager::inst()->setValue(ConfigClass, QStringLiteral("capmb"), QString::number(megabytes));
	return true;
}

QString UndoHistory::settingName(Setting setting)
{
	switch (setting)
	{
	case Setting::On: return QStringLiteral("on");
	case Setting::Off: return QStringLiteral("off");
	case Setting::Default: break;
	}
	return QStringLiteral("default");
}

bool UndoHistory::parseSetting(const QString& name, Setting* setting)
{
	for (Setting candidate : {Setting::Default, Setting::On, Setting::Off})
	{
		if (name == settingName(candidate)) { *setting = candidate; return true; }
	}
	return false;
}

void UndoHistory::setProjectSetting(Setting setting)
{
	m_setting = setting;
}

bool UndoHistory::enabled() const
{
	return m_setting == Setting::On || (m_setting == Setting::Default && defaultEnabled());
}

void UndoHistory::setProjectState(const ProjectState& state)
{
	m_setting = state.setting;
	m_load = state.load;
	m_loadedTopSerial = state.loadedTopSerial;
}

// ---- the policy the journal asks --------------------------------------------

bool UndoHistory::capturing() const
{
	// The SETTING, not "can capture now": the journal also prunes by this, and a
	// load (when nothing can be captured) must not drop the steps it loads.
	// capture() answers null while a project is loading.
	return enabled();
}

ProjectJournal::Snapshot UndoHistory::capture()
{
	Song* song = Engine::getSong();
	return song == nullptr ? ProjectJournal::Snapshot() : song->captureUndoSnapshot();
}

ProjectJournal::PersistPlan UndoHistory::currentPlan() const
{
	const ProjectJournal* journal = Engine::projectJournal();
	if (journal == nullptr) { return ProjectJournal::PersistPlan(); }
	return ProjectJournal::planPersist(journal->stepSnapshots(), maxSteps(), capBytes());
}

// ---- the fingerprint ---------------------------------------------------------

QString UndoHistory::fingerprint(const QDomDocument& document, bool stripMetadata)
{
	const QDomElement root = document.documentElement();
	QCryptographicHash hash(QCryptographicHash::Sha256);
	const auto add = [&hash, stripMetadata](const QDomElement& element) {
		QDomElement measured = element;
		if (stripMetadata)
		{
			measured = element.cloneNode(true).toElement();
			stripMetadataNodes(measured);
		}
		hash.addData(element.tagName().toUtf8());
		hash.addData(QByteArrayLiteral(" "));
		hash.addData(sectionDigest(measured).toUtf8());
		hash.addData(QByteArrayLiteral("\n"));
	};
	// <head>'s `writer` names the PROCESS that saved (ProjectIds::writerInstance), not the project: any build
	// that re-saves a revised project stamps its own, so it is left out - measured with the base build, whose
	// unchanged re-save otherwise read as an edit.
	QDomElement head = root.firstChildElement(QStringLiteral("head")).cloneNode(true).toElement();
	head.removeAttribute(QStringLiteral("writer"));
	add(head);
	const QDomElement song = root.firstChildElement(QStringLiteral("song"));
	for (QDomElement child = song.firstChildElement(); !child.isNull(); child = child.nextSiblingElement())
	{
		if (!outsideFingerprint(child.tagName())) { add(child); }
	}
	return QStringLiteral("sha256:") + QString::fromLatin1(hash.result().toHex());
}

// ---- the section ---------------------------------------------------------------

QString UndoHistory::elementName() const
{
	return QStringLiteral("z:undohistory");
}

bool UndoHistory::shouldPersist() const
{
	// Only a real save writes it, and only for a project that opted in (or out,
	// explicitly - that choice has to survive the save). A project that did
	// neither writes the bytes it always wrote.
	if (m_saveDepth == 0) { return false; }
	return m_setting != Setting::Default || (enabled() && currentPlan().saved > 0);
}

void UndoHistory::save(QDomDocument& doc, QDomElement& parent) const
{
	QDomElement section = doc.createElement(elementName());
	section.setAttribute(documentIndexNamespaceAttribute(), documentIndexNamespaceUri());
	section.setAttribute(QStringLiteral("v"), QStringLiteral("1"));
	if (m_setting != Setting::Default) { section.setAttribute(QStringLiteral("setting"), settingName(m_setting)); }
	const ProjectJournal::PersistPlan plan = enabled() ? currentPlan() : ProjectJournal::PersistPlan();
	section.setAttribute(QStringLiteral("steps"), plan.saved);
	const QVector<ProjectJournal::StepSnapshot> steps = Engine::projectJournal()->stepSnapshots();
	// Oldest first: the order a load pushes them back onto the journal.
	for (int i = plan.saved - 1; i >= 0; --i)
	{
		const QByteArray& bytes = *steps.at(i).snapshot;
		QDomElement step = doc.createElement(StepName);
		step.setAttribute(QStringLiteral("bytes"), bytes.size());
		step.setAttribute(QStringLiteral("sha256"), sha256Of(bytes));
		step.appendChild(doc.createTextNode(QString::fromLatin1(bytes.toBase64())));
		section.appendChild(step);
	}
	parent.appendChild(section);
}

//! What a save does with the history, from the plan it carried out (the warning is the owner's
//! "never silently": one snapshot over the cap).
static UndoHistory::SaveReport reportOf(const ProjectJournal::PersistPlan& plan)
{
	UndoHistory::SaveReport report;
	report.enabled = true;
	report.savedSteps = plan.saved;
	report.savedBytes = plan.bytes;
	report.droppedOldest = plan.droppedOldest;
	report.oversize = plan.oversize;
	report.oversizeBytes = plan.oversizeBytes;
	report.unavailable = plan.unavailable;
	if (plan.oversize)
	{
		report.warning = QStringLiteral("Undo history NOT fully saved: one undo step's snapshot is %1 MB, "
			"over the %2 MB cap, so the history from that step back (%3 step(s)) was not saved; %4 newer "
			"step(s) were. Raise the cap (setting undohistory/capmb) to keep them.")
			.arg(QString::number(plan.oversizeBytes / double(Megabyte), 'f', 1))
			.arg(UndoHistory::capBytes() / Megabyte).arg(plan.droppedOldest).arg(plan.saved);
	}
	return report;
}

//! A bundle rewrites sample paths after the seal; a history would then describe files the bundle does
//! not carry. Kept out, and said so.
static void keepOutOfBundle(QDomElement section, UndoHistory::SaveReport* report)
{
	while (!section.firstChildElement(StepName).isNull()) { section.removeChild(section.firstChildElement(StepName)); }
	section.setAttribute(QStringLiteral("steps"), 0);
	report->warning = QStringLiteral("Undo history not saved: a project bundle does not carry the undo history.");
	report->savedSteps = 0;
	report->savedBytes = 0;
}

void UndoHistory::seal(DataFile& file, bool withResources)
{
	if (m_saveDepth == 0) { return; }
	m_save = enabled() ? reportOf(currentPlan()) : SaveReport();
	QDomElement section = file.content().firstChildElement(elementName());
	const bool steps = !section.isNull() && m_save.savedSteps > 0;
	if (steps && withResources) { keepOutOfBundle(section, &m_save); }
	m_save.written = steps && !withResources;
	if (m_save.written) { section.setAttribute(QStringLiteral("fingerprint"), fingerprint(file, true)); }
	if (!m_save.warning.isEmpty()) { qWarning("%s", qUtf8Printable(m_save.warning)); }
}

void UndoHistory::beginLoad(const DataFile& file)
{
	m_loadFingerprint.clear();
	const QDomElement song = file.documentElement().firstChildElement(QStringLiteral("song"));
	if (!song.firstChildElement(elementName()).isNull()) { m_loadFingerprint = fingerprint(file, false); }
}

void UndoHistory::load(const QDomElement& element)
{
	// ALWAYS claimed, whatever it holds: a history this build cannot use is
	// dropped, never re-emitted - a stale history kept verbatim would undo to a
	// project that no longer exists.
	m_load = LoadReport();
	m_load.found = true;
	if (element.attribute(QStringLiteral("v")) != QLatin1String("1"))
	{
		m_load.discarded = QStringLiteral("unknown undo-history version '%1'").arg(element.attribute(QStringLiteral("v")));
		return;
	}
	Setting setting = Setting::Default;
	if (element.hasAttribute(QStringLiteral("setting"))
		&& !parseSetting(element.attribute(QStringLiteral("setting")), &setting))
	{
		m_load.discarded = QStringLiteral("unknown setting '%1'").arg(element.attribute(QStringLiteral("setting")));
		return;
	}
	m_setting = setting;
	if (element.firstChildElement(StepName).isNull()) { return; }
	if (element.attribute(QStringLiteral("fingerprint")) != m_loadFingerprint)
	{
		m_load.discarded = QStringLiteral("the project was changed after its undo history was saved "
			"(re-saved by a build that does not read the history), so the history no longer describes it");
		return;
	}
	loadSteps(element);
}

void UndoHistory::loadSteps(const QDomElement& section)
{
	// Validated whole before anything is pushed: a history is contiguous, so one
	// bad step makes the steps below it unreachable anyway.
	QVector<ProjectJournal::Snapshot> snapshots;
	for (QDomElement step = section.firstChildElement(); !step.isNull(); step = step.nextSiblingElement())
	{
		const QByteArray bytes = QByteArray::fromBase64(step.text().toLatin1());
		if (step.tagName() != StepName || bytes.isEmpty()
			|| step.attribute(QStringLiteral("bytes")).toLongLong() != bytes.size()
			|| step.attribute(QStringLiteral("sha256")) != sha256Of(bytes))
		{
			m_load.discarded = QStringLiteral("undo step %1 of the saved history is damaged").arg(snapshots.size());
			return;
		}
		snapshots.append(std::make_shared<const QByteArray>(bytes));
	}
	ProjectJournal* journal = Engine::projectJournal();
	if (journal == nullptr) { return; }
	for (const ProjectJournal::Snapshot& before : snapshots) { pushLoadedStep(journal, before); }
	m_load.loadedSteps = snapshots.size();
	m_loadedTopSerial = journal->topStepSerial();
}

void UndoHistory::clear()
{
	m_setting = Setting::Default;
	m_load = LoadReport();
	m_loadedTopSerial = 0;
}

// ---- reports -----------------------------------------------------------------

QJsonObject UndoHistory::SaveReport::toJson() const
{
	return QJsonObject{
		{QStringLiteral("enabled"), enabled},
		{QStringLiteral("written"), written},
		{QStringLiteral("saved_steps"), savedSteps},
		{QStringLiteral("saved_bytes"), static_cast<double>(savedBytes)},
		{QStringLiteral("max_steps"), maxSteps()},
		{QStringLiteral("cap_bytes"), static_cast<double>(capBytes())},
		{QStringLiteral("dropped_oldest"), droppedOldest},
		{QStringLiteral("oversize"), oversize},
		{QStringLiteral("oversize_bytes"), static_cast<double>(oversizeBytes)},
		{QStringLiteral("unavailable"), unavailable},
		{QStringLiteral("warning"), warning},
	};
}

QJsonObject UndoHistory::LoadReport::toJson() const
{
	return QJsonObject{
		{QStringLiteral("found"), found},
		{QStringLiteral("loaded_steps"), loadedSteps},
		{QStringLiteral("discarded"), discarded},
	};
}

} // namespace lmms
