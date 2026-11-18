/*
 * BrowserSearchViewMedia.cpp - the Search view's Project Media inventory and cleanup
 *                               (Q050-121), and the result row menu.
 *
 * Split from BrowserSearchView.cpp so both translation units stay under the
 * file-length ratchet; the members and the class are the same.
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

#include "BrowserSearchView.h"

#include <functional>

#include <QAbstractScrollArea>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QMessageBox>

#include "BrowserResults.h"
#include "BrowserTagMenu.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "Song.h"

namespace lmms::gui
{

namespace
{

using namespace browserresults;

//! Q050-121: the cleanup confirmation tests replace. Empty means the default.
std::function<bool(const QStringList&)> s_cleanupConfirm;

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

} // namespace

void BrowserSearchView::showProjectMedia()
{
	Song* song = Engine::getSong();
	const QString project = song != nullptr ? song->projectFileName() : QString();
	if (project.isEmpty())
	{
		m_status->setText(tr("Project Media needs a saved project: save the project first"));
		return;
	}
	const ControlResult listed = run(QStringLiteral("project.media_usage"), {{QStringLiteral("project"), project}});
	if (!listed.ok)
	{
		m_status->setText(listed.errorMessage);
		return;
	}
	m_mediaFiles = listed.result.value(QStringLiteral("files")).toArray();
	m_mediaUnusedCount = listed.result.value(QStringLiteral("unused_count")).toInt();
	m_mediaMissingCount = listed.result.value(QStringLiteral("missing_count")).toInt();
	m_mediaExternalCount = listed.result.value(QStringLiteral("external_count")).toInt();
	m_showingMedia = true;
	m_liveEntry = false;
	m_actions->stopPreview();
	m_unused->setEnabled(true);
	m_cleanup->setEnabled(true);
	showMediaColumns(true);
	showItems(mediaItems());
	setShowUnused(m_unused->isChecked());
}

QJsonArray BrowserSearchView::mediaItems() const
{
	QJsonArray out;
	for (const QJsonValue& value : m_mediaFiles)
	{
		QJsonObject file = value.toObject();
		const QString state = file.value(QStringLiteral("state")).toString();
		if (m_unused->isChecked() && state == QLatin1String("used")) { continue; }
		QString name = file.value(QStringLiteral("name")).toString();
		if (name.isEmpty()) { name = file.value(QStringLiteral("raw")).toString(); }
		file.insert(QStringLiteral("name"), name);
		file.insert(QStringLiteral("kind"), state == QLatin1String("missing") || state == QLatin1String("unresolved")
			? QStringLiteral("missing") : QStringLiteral("audio"));
		const QString path = file.value(QStringLiteral("path")).toString();
		const bool exists = file.value(QStringLiteral("exists")).toBool() && !path.isEmpty();
		file.insert(QStringLiteral("size_bytes"), exists ? QFileInfo(path).size() : qint64(-1));
		file.insert(QStringLiteral("modified_ms"), exists ? QFileInfo(path).lastModified().toMSecsSinceEpoch() : qint64(-1));
		out.append(file);
	}
	return out;
}

void BrowserSearchView::setShowUnused(bool on)
{
	if (!m_showingMedia)
	{
		if (on) { showProjectMedia(); }
		return;
	}
	showItems(mediaItems());
	QString status = tr("Project Media: %1 unused, %2 missing, %3 external")
		.arg(m_mediaUnusedCount).arg(m_mediaMissingCount).arg(m_mediaExternalCount);
	if (Engine::getSong() != nullptr && Engine::getSong()->isModified())
	{
		status += tr(" - unsaved changes: save before cleaning up");
	}
	m_status->setText(status);
}

void BrowserSearchView::showMediaColumns(bool on)
{
	m_results->setColumnHidden(Usage, !on);
	m_results->setColumnHidden(State, !on);
}

QStringList BrowserSearchView::selectedUnusedPaths() const
{
	QStringList paths;
	for (QTreeWidgetItem* item : m_results->selectedItems())
	{
		if (item->text(State) == QLatin1String("unused"))
		{
			const QString path = item->data(Name, PathRole).toString();
			if (!path.isEmpty() && !paths.contains(path)) { paths << path; }
		}
	}
	return paths;
}

void BrowserSearchView::cleanupSelectedMedia()
{
	if (!m_showingMedia) { return; }
	const QList<QTreeWidgetItem*> selected = m_results->selectedItems();
	if (selected.isEmpty())
	{
		m_status->setText(tr("Select the unused files to clean up first"));
		return;
	}
	for (QTreeWidgetItem* item : selected)
	{
		if (item->text(State) != QLatin1String("unused"))
		{
			m_status->setText(tr("Only rows whose state is unused can be cleaned up; %1 is %2")
				.arg(item->text(Name), item->text(State)));
			return;
		}
	}
	const QStringList paths = selectedUnusedPaths();
	if (paths.isEmpty())
	{
		m_status->setText(tr("Select the unused files to clean up first"));
		return;
	}
	const QString detail = paths.join(QStringLiteral("\n"));
	const bool confirmed = s_cleanupConfirm ? s_cleanupConfirm(paths)
		: QMessageBox::question(this, tr("Clean Up Unused Media"),
			tr("Remove these %1 unused files?\n\n%2\n\nThis cannot be undone.")
				.arg(paths.size()).arg(detail),
			QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
	if (!confirmed)
	{
		m_status->setText(tr("Media cleanup cancelled: nothing was removed"));
		return;
	}
	const QString project = Engine::getSong()->projectFileName();
	const ControlResult cleaned = run(QStringLiteral("project.media_cleanup"),
		{{QStringLiteral("project"), project}, {QStringLiteral("paths"), QJsonArray::fromStringList(paths)},
			{QStringLiteral("confirm"), true}});  // the dialog above is the authorization
	if (!cleaned.ok)
	{
		m_status->setText(cleaned.errorMessage);
		return;
	}
	const int removed = cleaned.result.value(QStringLiteral("removed")).toArray().size();
	const int refused = cleaned.result.value(QStringLiteral("refused")).toArray().size();
	showProjectMedia();
	m_status->setText(refused > 0
		? tr("Cleanup: %1 removed, %2 refused (still referenced or outside the unused set)").arg(removed).arg(refused)
		: tr("Cleanup: %1 unused files removed").arg(removed));
}

void BrowserSearchView::addPreviewEntries(QMenu* menu, const QString& path)
{
	QMenu* preview = menu->addMenu(tr("Preview"));  // WF-009
	const std::pair<QString, const char*> modes[] = {{tr("As It Is"), "none"}, {tr("At the Song's Tempo"), "tempo"},
		{tr("In the Song's Key"), "key"}, {tr("Matched to Tempo and Key"), "both"}};
	for (const auto& [label, match] : modes)
	{
		QAction* action = preview->addAction(label);
		action->setProperty("controlCommand", QStringLiteral("browser.preview"));
		connect(action, &QAction::triggered, this, [this, path, match = QLatin1String(match)] {
			m_actions->preview(path, QStringLiteral("audio"), match);  // an error shows on the status line
		});
	}
	QAction* stop = preview->addAction(tr("Stop"));
	stop->setProperty("controlCommand", QStringLiteral("browser.preview_stop"));
	connect(stop, &QAction::triggered, this, [this] {
		m_actions->stopPreview();
		run(QStringLiteral("browser.preview_stop"));  // whoever started it
	});
}

void BrowserSearchView::showRowMenu(QWidget* view, const QPoint& at, const QString& path, const QString& kind)
{
	QMenu menu(this);
	m_actions->addLoadEntry(&menu, path, kind);  // theme 3: what Enter does, named where the other actions are
	if (kind == QLatin1String("audio"))
	{
		addPreviewEntries(&menu, path);
		QAction* similar = menu.addAction(tr("Find Similar Sounds"));  // WF-020
		similar->setProperty("controlCommand", QStringLiteral("browser.similar"));
		connect(similar, &QAction::triggered, this, [this, path] { findSimilar(path); });
		menu.addSeparator();
	}
	addBrowserTagMenu(&menu, path);
	addBrowserCategoryMenu(&menu, path);
	const QAction* chosen = menu.exec(static_cast<QAbstractScrollArea*>(view)->viewport()->mapToGlobal(at));
	// A tag or category edit: the list is shown again as it stands (the current history entry,
	// not a new one), and the category filter's vocabulary follows the edit.
	const QString command = chosen != nullptr ? chosen->data().toString() : QString();
	if (command.startsWith(QLatin1String("browser.tag")) || command.startsWith(QLatin1String("browser.category")))
	{
		refreshCategories();
		m_showingMedia ? showProjectMedia() : goHistory(0);
	}
}

void setBrowserMediaCleanupConfirm(BrowserMediaCleanupConfirm confirm) { s_cleanupConfirm = std::move(confirm); }

} // namespace lmms::gui
